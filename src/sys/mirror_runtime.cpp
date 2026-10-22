/*
 * mirror_runtime.cpp — AIML mirror namespace (44.5)
 *
 * Synchronizes AIML model instances over TCP / WebSocket.
 *
 * v1 dirty-tracking: whole-struct memcmp/memcpy (no field-level delta).
 *
 * Packet format:
 *   Header  (6 bytes): [net_id:u32 le][type:u8][reserved:u8]
 *   Types:  0x01=delta  0x02=rpc  0x03=register  0x04=unregister
 *   Delta payload: raw struct bytes (obj_size bytes)
 *   Register payload: [net_id:i32 le][obj_size:i32 le]
 *   RPC payload:    [method_len:u8][method:N bytes][arg:i32 le]
 */

#include "mirror_runtime.h"
#include "ser_runtime.h"
#include "ws_runtime.h"
#include "string_runtime.h"   // aiml_str_dup — refcounted peer-ip return
#include "log_runtime.h"      // aiml_trace_dump — model-lifetime ring on crash
#include "time_runtime.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <mutex>
#include <queue>
#include <deque>
#include <algorithm>

/* POSIX */
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/select.h>
#include <cerrno>
#ifndef __EMSCRIPTEN__
#  include <execinfo.h>
#  include <sys/syscall.h>
#else
#  include <emscripten.h>
#endif
#include <signal.h>

/* ── Render-loop watchdog: RPC breadcrumb ─────────────────────────────────────
 * send_and_await sets this to the in-flight method name while it blocks in the
 * poll loop, and clears it on exit.  aiml_rt_frame_counter() (engine_runtime)
 * is the heartbeat; when the JS watchdog sees the counter frozen it reads this
 * breadcrumb to name the call the render loop is stuck inside.  A blocking RPC
 * from inside the frame is the prime suspect for a WASM freeze. */
namespace { const char* volatile g_rt_rpc_phase = ""; }
/* Generic frame-phase breadcrumb, shared across runtime files (mirror + prefab).
 * Each instrumented op sets it at entry and clears it ("") at exit; if the frame
 * is STUCK inside an op it never reaches the clear, so the JS watchdog reads the
 * op name.  External linkage so prefab_runtime can mark its render primitives. */
extern "C" void aiml_rt_set_phase(const char* p) { g_rt_rpc_phase = p ? p : ""; }
#if defined(__EMSCRIPTEN__)
extern "C" EMSCRIPTEN_KEEPALIVE const char* aiml_rt_rpc_phase(void) {
    const char* p = g_rt_rpc_phase;
    return p ? p : "";
}
#else
extern "C" const char* aiml_rt_rpc_phase(void) {
    const char* p = g_rt_rpc_phase;
    return p ? p : "";
}
#endif

/* Emit a loud stack trace when an RPC misbehaves (timeout / decode failure).
 * WASM: emscripten_get_callstack gives a mixed JS+wasm trace (needs -g for
 * names).  Native: fall back to a plain marker line.  Shipped to the browser
 * log-bridge via stderr. */
static void aiml_rpc_stall_trace(const char* tag, const char* method, int32_t waited_ms) {
#if defined(__EMSCRIPTEN__)
    char buf[4096];
    int flags = EM_LOG_C_STACK | EM_LOG_JS_STACK | EM_LOG_DEMANGLE | EM_LOG_NO_PATHS;
    emscripten_get_callstack(flags, buf, (int)sizeof(buf));
    fprintf(stderr, "\n=== AIML RPC %s: method='%s' waited=%dms ===\n%s\n",
            tag, method ? method : "?", waited_ms, buf);
#else
    fprintf(stderr, "[rpc] %s: method='%s' waited=%dms\n",
            tag, method ? method : "?", waited_ms);
#endif
    fflush(stderr);
}

/* ── Debug logging (AIML_DEBUG=1 env var) ─────────────────────────────────── */

static int g_aiml_debug = (getenv("AIML_DEBUG") != nullptr) ? 1 : 0;
#define AIML_DBG(fmt, ...) do { if (g_aiml_debug) fprintf(stderr, "[mirror] " fmt "\n", ##__VA_ARGS__); } while(0)

/* ── Crash handler (auto-installed via constructor) ──────────────────────── */

#ifndef __EMSCRIPTEN__
static void aiml_crash_handler(int sig, siginfo_t* info, void*) {
    char msg[256];
    int n = snprintf(msg, sizeof(msg),
        "\n=== AIML CRASH (signal %d at addr=%p tid=%ld) ===\n",
        sig, info ? info->si_addr : nullptr, (long)syscall(SYS_gettid));
    (void)write(STDERR_FILENO, msg, (size_t)n);
    void* bt[64];
    int cnt = backtrace(bt, 64);
    n = snprintf(msg, sizeof(msg), "backtrace: %d frames\n", cnt);
    (void)write(STDERR_FILENO, msg, (size_t)n);
    /* Flush stderr BEFORE backtrace_symbols_fd, so we get at least frame count
     * even if symbol resolution itself faults. */
    fsync(STDERR_FILENO);
    backtrace_symbols_fd(bt, cnt, STDERR_FILENO);
    fsync(STDERR_FILENO);
    /* AIML_MODEL_TRACE: dump the whole model-lifetime ring so the alloc/free/UAF
     * history leading to this fault is visible (no-op when tracing is off). Not
     * strictly async-signal-safe, but the process is already dying. */
    aiml_trace_dump(nullptr, "SIGSEGV — full model-lifetime ring");
    fsync(STDERR_FILENO);
    struct sigaction sa = {};
    sa.sa_handler = SIG_DFL;
    sigaction(sig, &sa, nullptr);
    raise(sig);
}

__attribute__((constructor))
static void aiml_install_crash_handler() {
    struct sigaction sa = {};
    sa.sa_sigaction = aiml_crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
}
#endif // !__EMSCRIPTEN__

/* ── Internal structs ─────────────────────────────────────────────────────── */

enum MirrorTransportType { MT_TCP = 0, MT_WS = 1, MT_WSS = 2 };

struct AimlMirrorTransport {
    MirrorTransportType type = MT_TCP;
    int   tcp_fd  = -1;
    void* ws_ptr  = nullptr;   /* AimlWs* */
};

struct AimlMirrorPeer {
    AimlMirrorTransport transport;
    int32_t id = 0;
    std::string ip;   /* dotted peer IP, captured at accept (for rate-limit / bans) */
    /* spec/44_5 §10 TCP framing: per-peer receive buffer + unsent backlog */
    std::vector<uint8_t> rx;  size_t rx_off = 0;
    std::vector<uint8_t> tx;  size_t tx_off = 0;
    int32_t rpc_version = 0;   /* from the peer's HELLO: 0 = none/v2, 3 = RPC v3 */
    std::deque<std::vector<uint8_t>> held_deltas;   /* AIML_MIRROR_TEST_HOLD_DELTA */
    bool    dead        = false;   /* invalid frame / overload / send error → disconnect */
    const char* dead_reason = nullptr;
};

struct AimlMirrorRegistered {
    void*    obj_ptr  = nullptr;
    int64_t  obj_size = 0;
    uint8_t* shadow   = nullptr;   /* malloc(obj_size) */
    int32_t  net_id   = 0;
    bool     is_owner = false;
    float    sync_rate   = 0.0f;   /* Hz; 0 = every update */
    bool     manual_sync = false;
    /* Typed serialization (spec/44_5 §10 DELTA format 2) */
    const AimlTypeNode* ser_nodes   = nullptr;
    int32_t             ser_n       = 0;
    uint64_t            snapshot_revision = 0;  /* remote snapshots applied successfully */
    int32_t             last_decode_error = 0;  /* 0 ok, -1 decode, -2 schema/format */
    /* DELTA received before register_id: kept with its format, applied at binding time */
    uint8_t*            pending_blob     = nullptr;
    int32_t             pending_blob_len = 0;
    uint8_t             pending_format   = 0;
    /* Typed dirty tracking: last serialized blob (detects deep heap mutations) */
    uint8_t*            shadow_blob      = nullptr;
    int32_t             shadow_blob_len  = 0;
    /* spec/44_5 §7.1 automatic object RPCs: the class's rpc table + per-method target */
    struct RpcMethod {
        std::string name;
        const AimlTypeNode* nodes = nullptr; int32_t n_nodes = 0; int64_t args_size = 0;
        void (*thunk)(void* obj, void* args) = nullptr;
        int target = 0;   /* 0 owner, 1 server, 2 all, 3 others */
    };
    std::vector<RpcMethod> rpc;
};

struct AimlMirrorEvent {
    const char*     type    = nullptr;
    AimlMirrorPeer* peer    = nullptr;
    int32_t         net_id  = 0;
    int32_t         peer_id = 0;   /* Unique per-connection peer id (1..N), host-assigned at accept */
};

/* Layout MUST match the LLVM `rpc_event` struct (CompilerContext::rpcEventType, context.h):
   { i8*, i32, i32, i32, i8*, i32, i32, i32, i64 } — natural alignment (pinned below). */
struct AimlRpcEvent {
    const char*    method        = nullptr;  /* heap-allocated, freed on session close */
    int32_t        net_id        = 0;        /* object id (object_call) — never a sender identity */
    int32_t        arg           = 0;        /* legacy i32 arg (also valid for arg_kind=0) */
    int32_t        arg_kind      = 0;        /* 0=i32 only, 1=model blob */
    const uint8_t* arg_blob      = nullptr;  /* heap-allocated payload (typed ser blob WITH 4B prefix) */
    int32_t        arg_blob_len  = 0;        /* length of arg_blob in bytes */
    int32_t        peer_id       = 0;        /* Unique per-connection sender id (host-assigned at accept) */
    int32_t        kind          = 0;        /* spec/44_5: 0 push, 1 request, 2 response, 3 object_call */
    uint64_t       request_id    = 0;        /* request/response correlation (0 = none) */
};
static_assert(offsetof(AimlRpcEvent, peer_id) == (sizeof(void*) == 8 ? 36 : 24), "rpc_event layout");
static_assert(offsetof(AimlRpcEvent, request_id) == (sizeof(void*) == 8 ? 48 : 32), "rpc_event layout");
static_assert(sizeof(AimlRpcEvent) == (sizeof(void*) == 8 ? 56 : 40), "rpc_event layout");

struct AimlMirrorSession {
    AimlMirrorTransport  listener;        /* server socket for accept loop */
    bool                 is_host = false;
    int32_t              max_peers = 64;
    int32_t              next_net_id = 1;
    int32_t              next_peer_id = 1;   /* spec/44_5 §7: peer ids are never reused in a session */

    std::vector<AimlMirrorPeer*>                     peers;
    std::unordered_map<int32_t, AimlMirrorRegistered*> by_net_id;
    std::unordered_map<void*,   AimlMirrorRegistered*> by_ptr;
    std::queue<AimlMirrorEvent*>                      event_queue;
    std::deque<AimlRpcEvent*>                         rpc_queue;   /* spec/44_5 §7: stable order, take-by-predicate */
    std::unordered_map<int32_t, size_t>               rpc_q_count; /* queued events per sender peer */
    std::unordered_map<int32_t, size_t>               rpc_q_bytes; /* queued bytes per sender peer */
    uint64_t                                          next_request_id = 1;
    std::unordered_set<uint64_t>                      outstanding; /* request ids awaiting a response */
    /* Last event/rpc handed to AIML via the next/poll accessors.  The returned heap
     * object is borrowed by the caller until its NEXT poll (its method/arg_blob
     * pointers are aliased into the AIML struct and read/decoded before then); we free
     * it on the following poll and on session close.  Bounds the per-RPC leak to one. */
    AimlRpcEvent*                                    last_consumed_rpc   = nullptr;
    AimlMirrorEvent*                                 last_consumed_event = nullptr;
};

/* spec/44_5 §7.1: object → session for automatic object RPCs (an object routes in ONE session). */
static std::mutex g_rpc_obj_mu;
static std::unordered_map<void*, AimlMirrorSession*> g_rpc_obj_session;
static void rpc_obj_forget(void* obj, AimlMirrorSession* s) {
    std::lock_guard<std::mutex> lk(g_rpc_obj_mu);
    auto it = g_rpc_obj_session.find(obj);
    if (it != g_rpc_obj_session.end() && (!s || it->second == s)) g_rpc_obj_session.erase(it);
}

/* Free helpers for consumed queue objects (defined after the empty sentinels). */
static void aiml_free_rpc_event(AimlRpcEvent* ev);
static void aiml_free_mirror_event(AimlMirrorEvent* ev);
struct AimlMirrorPeer;
static bool rpc_enqueue(AimlMirrorSession* s, AimlMirrorPeer* from, AimlRpcEvent* ev);
static AimlRpcEvent* rpc_take_at(AimlMirrorSession* s, size_t idx);
struct RpcFields;
static void rpc_receive_object_call(AimlMirrorSession* s, AimlMirrorPeer* from, int32_t net_id, const RpcFields& f);

/* ── URL parsing ──────────────────────────────────────────────────────────── */

struct ParsedUrl {
    MirrorTransportType type;
    std::string host;
    int         port;
    bool        ok;
};

static ParsedUrl mirror_parse_url(const char* url) {
    ParsedUrl r{}; r.ok = false;
    if (!url) return r;
    std::string u(url);
    if (u.substr(0, 6) == "tcp://") { r.type = MT_TCP; u = u.substr(6); }
    else if (u.substr(0, 5) == "ws://")  { r.type = MT_WS;  u = u.substr(5); }
    else if (u.substr(0, 6) == "wss://") { r.type = MT_WSS; u = u.substr(6); }
    else return r;

    auto colon = u.rfind(':');
    if (colon == std::string::npos) return r;
    r.host = u.substr(0, colon);
    try { r.port = std::stoi(u.substr(colon + 1)); } catch(...) { return r; }
    r.ok = true;
    return r;
}

/* ── TCP helpers ──────────────────────────────────────────────────────────── */

static int tcp_set_nonblocking(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

/* Check if `fd` has readable data (select timeout = 0) */
static bool tcp_readable(int fd) {
    if (fd < 0) return false;
    fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
    struct timeval tv = {0, 0};
    return select(fd + 1, &fds, nullptr, nullptr, &tv) == 1;
}



/* ── Packet helpers ───────────────────────────────────────────────────────── */

#define PKT_DELTA      0x01
#define PKT_RPC        0x02
#define PKT_REGISTER   0x03
#define PKT_UNREGISTER 0x04
#define PKT_RPC_V3     0x05
#define PKT_HELLO      0x06

#define AIML_RPC_VERSION 3                                /* highest RPC protocol we speak */
static const size_t kMaxRpcFrame   = (size_t)8 << 20;    /* spec/44_5 §10: max RPC / object payload */
static const size_t kMaxPacket     = kMaxRpcFrame + 512; /* + headers / method name */
static const size_t kMaxTxBacklog  = (size_t)64 << 20;   /* unsent bytes per peer before disconnect */

static void write_u32_le(uint8_t* p, uint32_t v) {
    p[0]=(v)&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF;
}
static uint32_t read_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static void write_i32_le(uint8_t* p, int32_t v) { write_u32_le(p, (uint32_t)v); }
static int32_t read_i32_le(const uint8_t* p)    { return (int32_t)read_u32_le(p); }

/* ── Transport send/recv ──────────────────────────────────────────────────── */

static void mirror_mark_dead(AimlMirrorPeer* peer, const char* why) {
    if (!peer || peer->dead) return;
    peer->dead = true;
    peer->dead_reason = why;
    fprintf(stderr, "[mirror] peer %d: %s — disconnecting\n", peer->id, why);
}

/* Flush a TCP peer's unsent backlog (non-blocking; the rest waits for the next update). */
static void tcp_flush(AimlMirrorPeer* peer) {
    if (!peer || peer->dead || peer->transport.type != MT_TCP) return;
    int fd = peer->transport.tcp_fd;
    while (peer->tx_off < peer->tx.size()) {
        ssize_t n = send(fd, peer->tx.data() + peer->tx_off, peer->tx.size() - peer->tx_off, MSG_NOSIGNAL);
        if (n > 0) { peer->tx_off += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        mirror_mark_dead(peer, "send failed");
        return;
    }
    if (peer->tx_off == peer->tx.size()) { peer->tx.clear(); peer->tx_off = 0; }
    else if (peer->tx_off > ((size_t)1 << 20)) {
        peer->tx.erase(peer->tx.begin(), peer->tx.begin() + (long)peer->tx_off);
        peer->tx_off = 0;
    }
}

static void mirror_send(AimlMirrorPeer* peer, const void* buf, size_t len) {
    if (!peer || peer->dead) return;
    if (peer->transport.type == MT_TCP) {
        /* spec/44_5 §10: never lose a short send — queue the rest, flush on the next update */
        const uint8_t* p = (const uint8_t*)buf;
        size_t done = 0;
        if (peer->tx_off == peer->tx.size()) {
            while (done < len) {
                ssize_t n = send(peer->transport.tcp_fd, p + done, len - done, MSG_NOSIGNAL);
                if (n > 0) { done += (size_t)n; continue; }
                if (n < 0 && errno == EINTR) continue;
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                mirror_mark_dead(peer, "send failed");
                return;
            }
        }
        if (done < len) {
            if (peer->tx.size() - peer->tx_off + (len - done) > kMaxTxBacklog) {
                mirror_mark_dead(peer, "send backlog over 64 MiB (slow peer)");
                return;
            }
            peer->tx.insert(peer->tx.end(), p + done, p + len);
        }
    } else if (peer->transport.ws_ptr) {
        aiml_ws_send_raw(peer->transport.ws_ptr, buf, (int32_t)len);
    }
}


/* ── Packet parser (spec/44_5 §10) ────────────────────────────────────────── */

enum PktParse { PKT_NEED_MORE, PKT_COMPLETE, PKT_INVALID };

/* Total length of the packet at d (avail bytes).  framed = one WebSocket message carries exactly one
 * packet: a raw DELTA then spans the whole message, and NEED_MORE means a truncated frame. */
static PktParse mirror_packet_length(AimlMirrorSession* s, const uint8_t* d, size_t avail,
                                     bool framed, size_t* total);

struct RpcFields {
    int      version    = 2;
    uint8_t  kind       = 0;
    uint64_t request_id = 0;
    const char* method  = nullptr;  size_t method_len = 0;
    uint8_t  arg_kind   = 0;
    int32_t  arg_i32    = 0;
    const uint8_t* blob = nullptr;  uint32_t blob_len = 0;
};

static uint64_t read_u64_le(const uint8_t* p) {
    return (uint64_t)read_u32_le(p) | ((uint64_t)read_u32_le(p + 4) << 32);
}
static void write_u64_le(uint8_t* p, uint64_t v) {
    write_u32_le(p, (uint32_t)v); write_u32_le(p + 4, (uint32_t)(v >> 32));
}

/* Validate a complete RPC packet (v2 or v3) of exactly len bytes; fills f on success. */
static bool mirror_parse_rpc(const uint8_t* d, size_t len, RpcFields& f) {
    size_t o;
    if (d[4] == PKT_RPC) {
        if (len < 7) return false;
        f.version = 2;
        f.method_len = d[6];
        f.method = (const char*)(d + 7);
        o = 7 + f.method_len;
    } else {
        if (len < 30 || d[5] != 3 || read_u32_le(d + 6) != len) return false;
        f.version = 3;
        f.kind = d[10];
        if (f.kind > 3 || d[11] != 0) return false;
        f.request_id = read_u64_le(d + 12);
        f.method_len = d[20];
        f.method = (const char*)(d + 21);
        o = 21 + f.method_len;
    }
    if (f.method_len == 0 || memchr(f.method, 0, f.method_len)) return false;
    if (len < o || len - o < 9) return false;
    f.arg_kind = d[o];
    f.arg_i32  = read_i32_le(d + o + 1);
    f.blob_len = read_u32_le(d + o + 5);
    if (f.blob_len != len - o - 9) return false;
    f.blob = f.blob_len ? d + o + 9 : nullptr;
    if (f.arg_kind > 1) return false;
    if (f.arg_kind == 0 && f.blob_len != 0) return false;
    if (f.arg_kind == 1) {
        if (f.blob_len < 4 || read_u32_le(f.blob) != f.blob_len - 4) return false;
        if (f.version == 3 && (f.blob_len < 12 || read_u32_le(f.blob + 4) != 0xA1F5C0DFu)) return false;
    }
    return true;
}

static PktParse mirror_packet_length(AimlMirrorSession* s, const uint8_t* d, size_t avail,
                                     bool framed, size_t* total) {
    const PktParse more = framed ? PKT_INVALID : PKT_NEED_MORE;
    if (avail < 6) return more;
    size_t t = 0;
    switch (d[4]) {
    case PKT_DELTA:
        if (d[5] == 2) {
            if (avail < 10) return more;
            uint32_t dl = read_u32_le(d + 6);
            if (dl < 8 || dl > kMaxRpcFrame) return PKT_INVALID;
            t = 10 + (size_t)dl;
        } else if (d[5] != 0) {
            return PKT_INVALID;   /* incl. the untagged former format 1 */
        } else if (framed) {
            t = avail;   /* raw struct bytes: the message is the packet */
        } else {
            auto it = s->by_net_id.find(read_i32_le(d));
            size_t osz = (it != s->by_net_id.end() && it->second->obj_size > 0) ? (size_t)it->second->obj_size : 0;
            t = 6 + osz;
        }
        break;
    case PKT_REGISTER:   t = 14; break;
    case PKT_UNREGISTER: t = 6;  break;
    case PKT_HELLO:      t = 6;  break;
    case PKT_RPC: {
        if (avail < 7) return more;
        size_t o = 7 + (size_t)d[6];
        if (avail < o + 9) return more;
        uint32_t bl = read_u32_le(d + o + 5);
        if (bl > kMaxRpcFrame) return PKT_INVALID;
        t = o + 9 + (size_t)bl;
        break;
    }
    case PKT_RPC_V3: {
        if (avail < 10) return more;
        uint32_t tl = read_u32_le(d + 6);
        if (tl < 30 || tl > kMaxPacket) return PKT_INVALID;
        t = tl;
        break;
    }
    default:
        return PKT_INVALID;
    }
    if (t > kMaxPacket) return PKT_INVALID;
    if (avail < t) return more;
    if (framed && t != avail) return PKT_INVALID;   /* exactly one packet per message */
    *total = t;
    return PKT_COMPLETE;
}

/* ── Packet send helpers ──────────────────────────────────────────────────── */

static void broadcast_pkt(AimlMirrorSession* s, const uint8_t* pkt, size_t pkt_len, AimlMirrorPeer* exclude) {
    for (auto* peer : s->peers) {
        if (peer == exclude) continue;
        mirror_send(peer, pkt, pkt_len);
    }
}

static void send_register_pkt(AimlMirrorPeer* peer, int32_t net_id, int32_t obj_size) {
    uint8_t pkt[14];
    write_u32_le(pkt, (uint32_t)net_id);
    pkt[4] = PKT_REGISTER; pkt[5] = 0;
    write_i32_le(pkt + 6, net_id);
    write_i32_le(pkt + 10, obj_size);
    mirror_send(peer, pkt, 14);
}

static void send_unregister_pkt(AimlMirrorSession* s, int32_t net_id, AimlMirrorPeer* exclude) {
    uint8_t pkt[6];
    write_u32_le(pkt, (uint32_t)net_id);
    pkt[4] = PKT_UNREGISTER; pkt[5] = 0;
    broadcast_pkt(s, pkt, 6, exclude);
}

/* spec/44_5 §10 test hook (optional, production code): while the file named by
 * AIML_MIRROR_TEST_HOLD_DELTA does not exist, outgoing DELTAs are held per peer, in order. */
static bool delta_hold_active() {
    static const char* path = getenv("AIML_MIRROR_TEST_HOLD_DELTA");
    return path && *path && access(path, F_OK) != 0;
}

static void flush_held_deltas(AimlMirrorPeer* peer) {
    while (!peer->held_deltas.empty()) {
        mirror_send(peer, peer->held_deltas.front().data(), peer->held_deltas.front().size());
        peer->held_deltas.pop_front();
    }
}

static void send_delta_bytes(AimlMirrorPeer* peer, const std::vector<uint8_t>& pkt) {
    if (!peer || peer->dead) return;
    if (delta_hold_active()) { peer->held_deltas.push_back(pkt); return; }
    flush_held_deltas(peer);
    mirror_send(peer, pkt.data(), pkt.size());
}

/* Encode a typed registration as a DELTA format 2 blob ([len][magic][fp][fields]); the caller
 * owns the returned blob.  nullptr if encoding fails or the blob exceeds the frame limit. */
static char* encode_delta_blob(AimlMirrorRegistered* reg, size_t* blob_len) {
    char* blob = aiml_ser_encode_wire(reg->obj_ptr, reg->ser_nodes, reg->ser_n);
    if (!blob) return nullptr;
    size_t len = (size_t)read_u32_le((const uint8_t*)blob) + 4;
    if (len < 12 || len - 4 > kMaxRpcFrame) {
        fprintf(stderr, "[mirror] DELTA net_id=%d too large (%zu bytes), not sent\n", reg->net_id, len);
        free(blob);
        return nullptr;
    }
    *blob_len = len;
    return blob;
}

static std::vector<uint8_t> make_delta_packet(int32_t net_id, uint8_t format, const void* body, size_t len) {
    std::vector<uint8_t> pkt(6 + len);
    write_u32_le(pkt.data(), (uint32_t)net_id);
    pkt[4] = PKT_DELTA; pkt[5] = format;
    if (len) memcpy(pkt.data() + 6, body, len);
    return pkt;
}

/* The current DELTA of a registration: format 2 for typed, raw format 0 for the legacy C API.
 * `blob_out` (typed only) receives the encoded blob for dirty tracking; else it is freed. */
static bool build_delta(AimlMirrorRegistered* reg, std::vector<uint8_t>& pkt, char** blob_out, size_t* blob_len) {
    if (!reg || !reg->obj_ptr) return false;
    if (reg->ser_nodes) {
        size_t len = 0;
        char* blob = encode_delta_blob(reg, &len);
        if (!blob) return false;
        pkt = make_delta_packet(reg->net_id, 2, blob, len);
        if (blob_out) { *blob_out = blob; *blob_len = len; } else free(blob);
        return true;
    }
    pkt = make_delta_packet(reg->net_id, 0, reg->obj_ptr, (size_t)reg->obj_size);
    return true;
}

static void remember_sent(AimlMirrorRegistered* reg, char* blob, size_t len) {
    if (blob) {
        free(reg->shadow_blob);
        reg->shadow_blob = (uint8_t*)blob;
        reg->shadow_blob_len = (int32_t)len;
    }
    memcpy(reg->shadow, reg->obj_ptr, (size_t)reg->obj_size);
}

/* Send the current state to a single peer.  Does NOT update the shadow. */
static void send_delta_to_peer(AimlMirrorPeer* peer, AimlMirrorRegistered* reg) {
    if (!peer) return;
    std::vector<uint8_t> pkt;
    if (build_delta(reg, pkt, nullptr, nullptr)) send_delta_bytes(peer, pkt);
}

static void send_delta_pkt(AimlMirrorSession* s, AimlMirrorRegistered* reg, AimlMirrorPeer* exclude) {
    std::vector<uint8_t> pkt;
    char* blob = nullptr; size_t len = 0;
    if (!build_delta(reg, pkt, &blob, &len)) return;
    for (auto* peer : s->peers) if (peer != exclude) send_delta_bytes(peer, pkt);
    remember_sent(reg, blob, len);
}

/* spec/44_5 §10: complete, bounded, transactional apply; the replica changes only on success. */
static int apply_typed_delta(AimlMirrorRegistered* reg, const uint8_t* blob, size_t len) {
    if (!reg->obj_ptr || reg->obj_size <= 0 || !reg->ser_nodes || reg->ser_n <= 0)
        return reg->last_decode_error = -1;
    if (reg->snapshot_revision == UINT64_MAX) return reg->last_decode_error = -1;
    int rc = aiml_ser_decode_delta_tx((const char*)blob, len, reg->obj_ptr, (size_t)reg->obj_size,
                                      reg->ser_nodes, reg->ser_n);
    reg->last_decode_error = rc;
    if (rc != 0) return rc;
    ++reg->snapshot_revision;
    if (reg->shadow) memcpy(reg->shadow, reg->obj_ptr, (size_t)reg->obj_size);
    return 0;
}

/* Remove a registration from every lookup and free what the session allocated for it.  The
 * object and its fields belong to the AIML caller (spec/44_5 §4: the session borrows). */
static void erase_registration(AimlMirrorSession* s, AimlMirrorRegistered* reg) {
    if (!reg) return;
    if (reg->obj_ptr) {
        rpc_obj_forget(reg->obj_ptr, s);
        auto it = s->by_ptr.find(reg->obj_ptr);
        if (it != s->by_ptr.end() && it->second == reg) s->by_ptr.erase(it);
    }
    auto nit = s->by_net_id.find(reg->net_id);
    if (nit != s->by_net_id.end() && nit->second == reg) s->by_net_id.erase(nit);
    free(reg->shadow);
    free(reg->shadow_blob);
    free(reg->pending_blob);
    delete reg;
}

/* ── Packet processing ────────────────────────────────────────────────────── */

/* `data` is ONE complete packet of exactly data_len bytes (mirror_packet_length). */
static void process_packet(AimlMirrorSession* s, AimlMirrorPeer* from_peer,
                            const uint8_t* data, int data_len) {
    if (data_len < 6) return;
    int32_t net_id = read_i32_le(data);
    uint8_t  type  = data[4];

    if (type == PKT_DELTA) {
        uint8_t reserved = data[5];
        bool accepted = false;   /* spec/44_5 §10: the host forwards only what it accepted or cannot judge */
        auto it = s->by_net_id.find(net_id);
        if (it != s->by_net_id.end()) {
            AimlMirrorRegistered* reg = it->second;
            if (!reg->is_owner && reg->obj_ptr) {
                if (reg->ser_nodes) {
                    if (reserved != 2) {
                        reg->last_decode_error = -2;   /* raw bytes never reach a typed object */
                    } else if (apply_typed_delta(reg, data + 6, (size_t)(data_len - 6)) == 0) {
                        accepted = true;
                    }
                    if (!accepted)
                        fprintf(stderr, "[mirror] DELTA rejected net_id=%d rc=%d\n", net_id, reg->last_decode_error);
                } else if (reserved == 0) {
                    /* legacy C API raw object */
                    int payload_len = data_len - 6;
                    int copy_len = (reg->obj_size > 0 && payload_len > 0)
                        ? (payload_len < (int)reg->obj_size ? payload_len : (int)reg->obj_size)
                        : 0;
                    if (copy_len > 0) memcpy(reg->obj_ptr, data + 6, (size_t)copy_len);
                    if (reg->shadow) memcpy(reg->shadow, reg->obj_ptr, (size_t)reg->obj_size);
                    accepted = true;
                } else {
                    reg->last_decode_error = -2;
                }
            } else if (!reg->is_owner && !reg->obj_ptr) {
                /* register_id not yet called: keep the latest DELTA with its format */
                int blob_len = data_len - 6;
                if (blob_len > 0) {
                    uint8_t* copy = (uint8_t*)malloc((size_t)blob_len);
                    if (copy) {
                        memcpy(copy, data + 6, (size_t)blob_len);
                        free(reg->pending_blob);
                        reg->pending_blob = copy;
                        reg->pending_blob_len = blob_len;
                        reg->pending_format = reserved;
                    }
                }
                if (reserved == 0 && reg->shadow) {
                    /* raw cache for a later untyped register_id */
                    int copy_len = (reg->obj_size > 0 && blob_len > 0)
                        ? (blob_len < (int)reg->obj_size ? blob_len : (int)reg->obj_size) : 0;
                    if (copy_len > 0) memcpy(reg->shadow, data + 6, (size_t)copy_len);
                }
                accepted = true;   /* placeholder: the bound receivers validate */
            }
        } else {
            accepted = true;       /* unknown here: forward, receivers validate */
        }
        if (s->is_host && accepted) broadcast_pkt(s, data, (size_t)data_len, from_peer);
    }
    else if (type == PKT_REGISTER) {
        if (data_len < 14) return;
        int32_t new_net_id  = read_i32_le(data + 6);
        int32_t new_obj_size = read_i32_le(data + 10);
        if (new_obj_size <= 0 || (size_t)new_obj_size > kMaxRpcFrame) {
            mirror_mark_dead(from_peer, "invalid REGISTER object size");
            return;
        }
        if (s->by_net_id.find(new_net_id) == s->by_net_id.end()) {
            AimlMirrorRegistered* reg = new AimlMirrorRegistered();
            reg->net_id  = new_net_id;
            reg->obj_size = new_obj_size;
            reg->is_owner = false;
            reg->shadow = (uint8_t*)calloc(1, (size_t)(new_obj_size > 0 ? new_obj_size : 1));
            s->by_net_id[new_net_id] = reg;
            /* Event */
            auto* ev = new AimlMirrorEvent();
            ev->type    = "object_registered";
            ev->peer    = from_peer;
            ev->net_id  = new_net_id;
            ev->peer_id = from_peer ? from_peer->id : 0;
            s->event_queue.push(ev);
        }
        /* Host: forward to other peers */
        if (s->is_host) broadcast_pkt(s, data, (size_t)data_len, from_peer);
    }
    else if (type == PKT_UNREGISTER) {
        auto it = s->by_net_id.find(net_id);
        if (it != s->by_net_id.end()) {
            AimlMirrorRegistered* reg = it->second;
            if (!reg->is_owner) erase_registration(s, reg);
        }
        auto* ev = new AimlMirrorEvent();
        ev->type    = "object_unregistered";
        ev->peer    = from_peer;
        ev->net_id  = net_id;
        ev->peer_id = from_peer ? from_peer->id : 0;
        s->event_queue.push(ev);
        if (s->is_host) broadcast_pkt(s, data, (size_t)data_len, from_peer);
    }
    else if (type == PKT_HELLO) {
        /* spec/44_5 §10: the peer announces its highest RPC protocol version */
        if (from_peer) from_peer->rpc_version = data[5];
    }
    else if (type == PKT_RPC || type == PKT_RPC_V3) {
        RpcFields f;
        if (!mirror_parse_rpc(data, (size_t)data_len, f)) {
            mirror_mark_dead(from_peer, "invalid RPC frame");
            return;
        }
        if (f.version == 3 && f.kind == 3) {   /* spec/44_5 §7.1: object call → dispatch, never queued */
            rpc_receive_object_call(s, from_peer, net_id, f);
            return;
        }
        char* method = (char*)malloc(f.method_len + 1);
        if (!method) return;
        memcpy(method, f.method, f.method_len);
        method[f.method_len] = '\0';
        uint8_t* blob = nullptr;
        if (f.blob_len > 0) {
            blob = (uint8_t*)malloc(f.blob_len);
            if (!blob) { free(method); return; }
            memcpy(blob, f.blob, f.blob_len);
        }
        auto* ev = new AimlRpcEvent();
        ev->method       = method;
        ev->net_id       = net_id;
        ev->arg          = f.arg_i32;
        ev->arg_kind     = (int32_t)f.arg_kind;
        ev->arg_blob     = blob;
        ev->arg_blob_len = (int32_t)f.blob_len;
        ev->peer_id      = from_peer ? from_peer->id : 0;
        ev->kind         = (int32_t)f.kind;
        ev->request_id   = f.request_id;
        /* spec/44_5 §7: a response must belong to a request we are still waiting for; late,
         * duplicate or unknown responses are dropped here (pushes are never affected). */
        if (ev->kind == 2 && ev->request_id != 0 && !s->outstanding.count(ev->request_id)) {
            aiml_free_rpc_event(ev);
            return;
        }
        rpc_enqueue(s, from_peer, ev);
    }
}

/* ── Peer accept ──────────────────────────────────────────────────────────── */

static void mirror_send_hello(AimlMirrorPeer* peer) {
    uint8_t pkt[6] = {0, 0, 0, 0, PKT_HELLO, AIML_RPC_VERSION};
    mirror_send(peer, pkt, 6);
}

static void mirror_try_accept(AimlMirrorSession* s) {
    if (!s->is_host) return;
    if ((int)s->peers.size() >= s->max_peers) return;

    if (s->listener.type == MT_TCP) {
        int srv_fd = s->listener.tcp_fd;
        if (srv_fd < 0) return;
        if (!tcp_readable(srv_fd)) return;
        struct sockaddr_in addr{}; socklen_t len = sizeof(addr);
        int client_fd = accept4(srv_fd, (struct sockaddr*)&addr, &len, SOCK_CLOEXEC);
        if (client_fd < 0) return;
        tcp_set_nonblocking(client_fd);

        auto* peer = new AimlMirrorPeer();
        peer->transport.type   = MT_TCP;
        peer->transport.tcp_fd = client_fd;
        peer->id = s->next_peer_id++;
        peer->ip = inet_ntoa(addr.sin_addr);
        s->peers.push_back(peer);
        mirror_send_hello(peer);

        /* Send all known objects to new peer */
        for (auto& [id, reg] : s->by_net_id) {
            if (reg->is_owner) {
                send_register_pkt(peer, reg->net_id, (int32_t)reg->obj_size);
                send_delta_to_peer(peer, reg);  /* typed-aware */
            }
        }

        auto* ev = new AimlMirrorEvent();
        ev->type    = "peer_connected";
        ev->peer    = peer;
        ev->net_id  = 0;
        ev->peer_id = peer->id;
        s->event_queue.push(ev);
    } else if (s->listener.ws_ptr) {
        /* WS/WSS accept — poll listener via WS layer (avoids raw fd leak) */
        int poll_r = aiml_ws_poll_listener(s->listener.ws_ptr, 0);
        if (poll_r != 1) return;
        void* ws_client = aiml_ws_accept(s->listener.ws_ptr);
        if (!ws_client) return;

        auto* peer = new AimlMirrorPeer();
        peer->transport.type   = s->listener.type;
        peer->transport.ws_ptr = ws_client;
        peer->id = s->next_peer_id++;
        peer->ip = aiml_ws_peer_ip(ws_client);
        s->peers.push_back(peer);
        mirror_send_hello(peer);

        /* Send all known objects to new peer */
        for (auto& [id, reg] : s->by_net_id) {
            if (reg->is_owner) {
                send_register_pkt(peer, reg->net_id, (int32_t)reg->obj_size);
                send_delta_to_peer(peer, reg);
            }
        }

        auto* ev = new AimlMirrorEvent();
        ev->type    = "peer_connected";
        ev->peer    = peer;
        ev->net_id  = 0;
        ev->peer_id = peer->id;
        s->event_queue.push(ev);
    }
}

/* ── Peer receive loop ────────────────────────────────────────────────────── */

/* Consume every complete packet in a TCP peer's receive buffer (spec/44_5 §10). */
static void tcp_consume(AimlMirrorSession* s, AimlMirrorPeer* peer) {
    while (!peer->dead) {
        size_t avail = peer->rx.size() - peer->rx_off;
        size_t total = 0;
        PktParse r = mirror_packet_length(s, peer->rx.data() + peer->rx_off, avail, false, &total);
        if (r == PKT_NEED_MORE) break;
        if (r == PKT_INVALID) { mirror_mark_dead(peer, "invalid frame"); break; }
        process_packet(s, peer, peer->rx.data() + peer->rx_off, (int)total);
        peer->rx_off += total;
    }
    if (peer->rx_off == peer->rx.size()) { peer->rx.clear(); peer->rx_off = 0; }
    else if (peer->rx_off > 0) {
        peer->rx.erase(peer->rx.begin(), peer->rx.begin() + (long)peer->rx_off);
        peer->rx_off = 0;
    }
}

static void mirror_recv_from_peers(AimlMirrorSession* s) {
    std::vector<AimlMirrorPeer*> disconnected;
    for (auto* peer : s->peers) {
        if (peer->dead) { disconnected.push_back(peer); continue; }
        if (peer->transport.type == MT_TCP) {
            /* Non-blocking: read what is there, keep partial packets for the next update. */
            tcp_flush(peer);
            bool closed = false;
            for (int iter = 0; iter < 64 && !peer->dead; ++iter) {
                /* read straight into the receive buffer — a 64 KiB stack chunk overflows the
                 * 64 KiB WASM stack */
                const size_t kChunk = 65536, have = peer->rx.size();
                peer->rx.resize(have + kChunk);
                ssize_t n = recv(peer->transport.tcp_fd, peer->rx.data() + have, kChunk, 0);
                peer->rx.resize(have + (n > 0 ? (size_t)n : 0));
                if (n > 0) {
                    tcp_consume(s, peer);
                    continue;
                }
                if (n == 0) { closed = true; break; }
                if (errno == EINTR) continue;
                if (errno != EAGAIN && errno != EWOULDBLOCK) closed = true;
                break;
            }
            if (closed || peer->dead) disconnected.push_back(peer);
        } else {
            /* WS: drain available frames; every message must be exactly one valid packet.
             * aiml_ws_poll instead of tcp_readable(aiml_ws_fd) — the "fd" is target-specific
             * (real socket on native, opaque handle on wasm/emscripten). */
            for (int iter = 0; iter < 64; ++iter) {
                if (!peer->transport.ws_ptr) break;
                int pr = aiml_ws_poll(peer->transport.ws_ptr, 0);
                if (pr == 0) break;                       /* no data ready */
                if (pr < 0) { disconnected.push_back(peer); break; }
                int32_t msg_len = 0;
                const char* _rr_prev = g_rt_rpc_phase; g_rt_rpc_phase = "ws.recv_raw";
                void* msg = aiml_ws_recv_raw(peer->transport.ws_ptr, &msg_len);
                g_rt_rpc_phase = _rr_prev;
                if (!msg || msg_len <= 0) { free(msg); disconnected.push_back(peer); break; }
                size_t total = 0;
                if (mirror_packet_length(s, (const uint8_t*)msg, (size_t)msg_len, true, &total) != PKT_COMPLETE) {
                    free(msg);
                    mirror_mark_dead(peer, "invalid frame");
                    disconnected.push_back(peer);
                    break;
                }
                process_packet(s, peer, (const uint8_t*)msg, msg_len);
                free(msg);
                if (peer->dead) { disconnected.push_back(peer); break; }
            }
        }
    }

    /* Handle disconnected peers (each once) */
    std::sort(disconnected.begin(), disconnected.end());
    disconnected.erase(std::unique(disconnected.begin(), disconnected.end()), disconnected.end());
    for (auto* peer : disconnected) {
        auto* ev = new AimlMirrorEvent();
        ev->type    = "peer_disconnected";
        ev->peer    = peer;
        ev->net_id  = 0;
        ev->peer_id = peer->id;
        s->event_queue.push(ev);
        if (peer->transport.type == MT_TCP && peer->transport.tcp_fd >= 0)
            close(peer->transport.tcp_fd);
        else if (peer->transport.ws_ptr)
            aiml_ws_close(peer->transport.ws_ptr);   /* WS/WSS: full close (socket+SSL+object) — was leaked */
        s->peers.erase(std::remove(s->peers.begin(), s->peers.end(), peer), s->peers.end());
        delete peer;
    }
}

/* ── Session creation ─────────────────────────────────────────────────────── */

static AimlMirrorSession* mirror_make_session() {
    return new AimlMirrorSession();
}

// Shared listen implementation. For a wss:// URL, cert+key (PEM paths) are
// required — the WSS server needs a certificate to complete a TLS handshake.
static void* mirror_listen_impl(const char* url, int32_t max_peers,
                                const char* cert, const char* key) {
    ParsedUrl pu = mirror_parse_url(url);
    if (!pu.ok) {
        fprintf(stderr, "[mirror] invalid url: %s\n", url ? url : "(null)");
        return nullptr;
    }
    if (pu.type == MT_WSS && (!cert || !*cert || !key || !*key)) {
        fprintf(stderr, "[mirror] wss:// requires a cert and key — use mirror.listen_tls(url, cert, key)\n");
        return nullptr;
    }

    AimlMirrorSession* s = mirror_make_session();
    s->is_host   = true;
    s->max_peers = max_peers;

    if (pu.type == MT_TCP) {
        int srv = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (srv < 0) { delete s; return nullptr; }
        int opt = 1;
        setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons((uint16_t)pu.port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (pu.host != "0.0.0.0" && !pu.host.empty())
            inet_pton(AF_INET, pu.host.c_str(), &addr.sin_addr);
        if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
            listen(srv, 16) < 0) {
            close(srv); delete s; return nullptr;
        }
        tcp_set_nonblocking(srv);
        s->listener.type   = MT_TCP;
        s->listener.tcp_fd = srv;
    } else {
        /* WS/WSS server — WSS gets the hardened TLS defaults from ws_runtime */
        void* ws_srv = nullptr;
        if (pu.type == MT_WS)
            ws_srv = aiml_ws_server(pu.host.c_str(), pu.port);
        else
            ws_srv = aiml_wss_server(pu.host.c_str(), pu.port, cert, key);
        if (!ws_srv) { delete s; return nullptr; }
        s->listener.type   = pu.type;
        s->listener.ws_ptr = ws_srv;
    }

    return s;
}

void* aiml_mirror_listen(const char* url) {
    return mirror_listen_impl(url, 64, nullptr, nullptr);
}

void* aiml_mirror_listen_max(const char* url, int32_t max_peers) {
    return mirror_listen_impl(url, max_peers, nullptr, nullptr);
}

// wss:// listener with an explicit server certificate (PEM cert + key paths).
void* aiml_mirror_listen_tls(const char* url, const char* cert, const char* key) {
    return mirror_listen_impl(url, 64, cert, key);
}

// Dotted IP of a connected peer (by peer_id), captured at accept. "" if unknown.
// Lets RPC handlers rate-limit / ban by source IP (peer_id comes from rpc_event).
char* aiml_mirror_peer_ip(void* session, int32_t peer_id) {
    AimlMirrorSession* s = static_cast<AimlMirrorSession*>(session);
    if (!s) return aiml_str_dup("");
    for (auto* peer : s->peers) {
        if (peer && peer->id == peer_id) return aiml_str_dup(peer->ip.c_str());
    }
    return aiml_str_dup("");
}

void* aiml_mirror_connect(const char* url) {
    return aiml_mirror_connect_timeout(url, 5000);
}

void* aiml_mirror_connect_timeout(const char* url, int32_t ms) {
    ParsedUrl pu = mirror_parse_url(url);
    if (!pu.ok) {
        fprintf(stderr, "[mirror] invalid url: %s\n", url ? url : "(null)");
        return nullptr;
    }

    AimlMirrorSession* s = mirror_make_session();
    s->is_host = false;

    AimlMirrorPeer* peer = new AimlMirrorPeer();
    peer->id = 0;  /* server is peer 0 */

    if (pu.type == MT_TCP) {
        int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) { delete peer; delete s; return nullptr; }
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        std::string port_str = std::to_string(pu.port);
        if (getaddrinfo(pu.host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
            close(fd); delete peer; delete s; return nullptr;
        }
        /* Set non-blocking for timeout connect */
        tcp_set_nonblocking(fd);
        connect(fd, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);

        /* Wait for connection up to `ms` milliseconds */
        fd_set wfds; FD_ZERO(&wfds); FD_SET(fd, &wfds);
        struct timeval tv;
        tv.tv_sec  = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        if (select(fd + 1, nullptr, &wfds, nullptr, &tv) <= 0) {
            close(fd); delete peer; delete s; return nullptr;
        }
        /* Verify connection */
        int err = 0; socklen_t errlen = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen);
        if (err != 0) { close(fd); delete peer; delete s; return nullptr; }

        peer->transport.type   = MT_TCP;
        peer->transport.tcp_fd = fd;
    } else {
        /* WS/WSS connect */
        void* ws_client = nullptr;
        if (pu.type == MT_WS)
            ws_client = aiml_ws_connect(pu.host.c_str(), pu.port, "/");
        else
            ws_client = aiml_wss_connect(pu.host.c_str(), pu.port, "/");
        if (!ws_client) {
            fprintf(stderr, "[mirror] WS connect to %s:%d failed\n", pu.host.c_str(), pu.port);
            delete peer; delete s; return nullptr;
        }
        peer->transport.type   = pu.type;
        peer->transport.ws_ptr = ws_client;
    }

    s->peers.push_back(peer);
    mirror_send_hello(peer);
    /* Note: no peer_connected event on client side — connection success is
     * implicit (connect_timeout returned non-null). The server will send
     * object_registered events via update() for any already-registered objects. */

    return s;
}

/* ── Session lifecycle ────────────────────────────────────────────────────── */

void aiml_mirror_session_close(void* session) {
    auto* s = (AimlMirrorSession*)session;
    if (!s) return;
    /* Close all peers */
    for (auto* peer : s->peers) {
        if (peer->transport.type == MT_TCP && peer->transport.tcp_fd >= 0)
            close(peer->transport.tcp_fd);
        else if (peer->transport.ws_ptr)
            aiml_ws_close(peer->transport.ws_ptr);
        delete peer;
    }
    s->peers.clear();
    /* Close listener */
    if (s->is_host) {
        if (s->listener.type == MT_TCP && s->listener.tcp_fd >= 0)
            close(s->listener.tcp_fd);
        else if (s->listener.ws_ptr)
            aiml_ws_close(s->listener.ws_ptr);
    }
    /* Drop all registrations (the objects themselves stay with their owners) */
    while (!s->by_net_id.empty()) erase_registration(s, s->by_net_id.begin()->second);
    /* Free the last events still borrowed by AIML (not yet freed by a later poll). */
    aiml_free_rpc_event(s->last_consumed_rpc);      s->last_consumed_rpc   = nullptr;
    aiml_free_mirror_event(s->last_consumed_event); s->last_consumed_event = nullptr;
    /* Drain event queue */
    while (!s->event_queue.empty()) {
        aiml_free_mirror_event(s->event_queue.front());
        s->event_queue.pop();
    }
    /* Drain RPC queue (free method string + payload blob + event). */
    for (AimlRpcEvent* ev : s->rpc_queue) aiml_free_rpc_event(ev);
    s->rpc_queue.clear();
    delete s;
}

void aiml_mirror_session_update(void* session) {
    auto* s = (AimlMirrorSession*)session;
    if (!s) return;
    /* Watchdog: net_pump calls this every frame; if the WS recv path below
     * emscripten_sleep()s and never rewinds, the render loop dies with this
     * breadcrumb naming session.update as the culprit. */
    const char* _su_prev = g_rt_rpc_phase; g_rt_rpc_phase = "mirror.session_update";
    struct _SuGuard { const char* p; ~_SuGuard(){ g_rt_rpc_phase = p; } } _su_guard{_su_prev};

    if (!s->event_queue.empty() || !s->rpc_queue.empty())
        AIML_DBG("update peers=%d registered=%d events=%d rpcs=%d",
                 (int)s->peers.size(), (int)s->by_net_id.size(),
                 (int)s->event_queue.size(), (int)s->rpc_queue.size());

    /* 1. Accept new TCP peers (host only) */
    mirror_try_accept(s);

    /* 2. Receive and process incoming packets */
    mirror_recv_from_peers(s);

    /* 3. Send dirty updates for owned objects */
    for (auto& [id, reg] : s->by_net_id) {
        if (!reg->is_owner || reg->obj_ptr == nullptr || reg->obj_size == 0) continue;
        if (reg->manual_sync) continue;
        if (reg->ser_nodes) {
            /* Typed: compare exactly the blob that is sent — detects deep heap mutations */
            size_t total = 0;
            char* blob = encode_delta_blob(reg, &total);
            if (!blob) continue;
            bool dirty = ((int32_t)total != reg->shadow_blob_len) ||
                         memcmp(blob, reg->shadow_blob, total) != 0;
            if (dirty) {
                AIML_DBG("  dirty net_id=%d typed=1 blob=%d — sending delta", id, (int)total);
                std::vector<uint8_t> pkt = make_delta_packet(id, 2, blob, total);
                for (auto* peer : s->peers) send_delta_bytes(peer, pkt);
                remember_sent(reg, blob, total);
            } else {
                free(blob);
            }
        } else {
            /* Untyped: shallow memcmp on inline struct bytes */
            if (memcmp(reg->obj_ptr, reg->shadow, (size_t)reg->obj_size) != 0) {
                AIML_DBG("  dirty net_id=%d size=%d typed=0 — sending delta",
                         id, (int)reg->obj_size);
                send_delta_pkt(s, reg, nullptr);
            }
        }
    }
    /* 4. Release held DELTAs (test hook) and flush unsent TCP backlogs (spec/44_5 §10) */
    if (!delta_hold_active())
        for (auto* peer : s->peers) flush_held_deltas(peer);
    for (auto* peer : s->peers) tcp_flush(peer);
}

int32_t aiml_mirror_session_is_connected(void* session) {
    auto* s = (AimlMirrorSession*)session;
    if (!s) return 0;
    return s->peers.empty() ? 0 : 1;
}

int32_t aiml_mirror_session_peer_count(void* session) {
    auto* s = (AimlMirrorSession*)session;
    if (!s) return 0;
    return (int32_t)s->peers.size();
}

/* ── Object registration ──────────────────────────────────────────────────── */

int32_t aiml_mirror_session_register(void* session, void* obj, int64_t obj_size) {
    auto* s = (AimlMirrorSession*)session;
    if (!s || !obj || obj_size <= 0) return -1;

    int32_t nid = s->next_net_id++;
    aiml_mirror_session_register_id(session, obj, obj_size, nid);
    return nid;
}

void aiml_mirror_session_register_id(void* session, void* obj, int64_t obj_size, int32_t id) {
    auto* s = (AimlMirrorSession*)session;
    if (!s || !obj || obj_size <= 0) return;
    /* Ensure next_net_id stays ahead of any explicitly assigned id */
    if (id >= s->next_net_id) s->next_net_id = id + 1;

    /* If a placeholder entry was created by PKT_REGISTER (obj_ptr==null, is_owner=false),
     * update it with the local object pointer rather than replacing it. */
    if (s->by_net_id.count(id)) {
        AimlMirrorRegistered* existing = s->by_net_id[id];
        if (existing->obj_ptr == nullptr) {
            existing->obj_ptr  = obj;
            existing->obj_size = obj_size;
            /* If a delta arrived before register_id was called, the shadow holds
             * the cached state — apply it to obj_ptr so the replica is up-to-date
             * immediately (no need to wait for the next dirty delta). */
            if (existing->shadow) {
                memcpy(obj, existing->shadow, (size_t)obj_size);
                /* shadow already reflects current state — no realloc needed */
            } else {
                existing->shadow = (uint8_t*)calloc(1, (size_t)obj_size);
            }
            s->by_ptr[obj] = existing;
        }
        return;
    }

    AimlMirrorRegistered* reg = new AimlMirrorRegistered();
    reg->obj_ptr  = obj;
    reg->obj_size = obj_size;
    reg->net_id   = id;
    reg->is_owner = s->is_host; /* host = owner, client = replica */
    reg->shadow = (uint8_t*)malloc((size_t)obj_size);
    memcpy(reg->shadow, obj, (size_t)obj_size);

    s->by_net_id[id]  = reg;
    s->by_ptr[obj]    = reg;

    AIML_DBG("register_id net_id=%d obj_size=%d owner=%d", id, (int)obj_size, (int)reg->is_owner);

    /* Notify all peers of new object */
    for (auto* peer : s->peers) {
        send_register_pkt(peer, id, (int32_t)obj_size);
        /* Send current state as initial delta — ONE packet (spec/44_5 §10) */
        std::vector<uint8_t> dpkt(6 + (size_t)obj_size);
        write_u32_le(dpkt.data(), (uint32_t)id);
        dpkt[4] = PKT_DELTA; dpkt[5] = 0;
        memcpy(dpkt.data() + 6, obj, (size_t)obj_size);
        mirror_send(peer, dpkt.data(), dpkt.size());
    }
}

void aiml_mirror_session_unregister(void* session, void* obj) {
    auto* s = (AimlMirrorSession*)session;
    if (!s || !obj) return;
    auto pit = s->by_ptr.find(obj);
    if (pit == s->by_ptr.end()) return;

    AimlMirrorRegistered* reg = pit->second;
    send_unregister_pkt(s, reg->net_id, nullptr);
    erase_registration(s, reg);
}

/* ── Typed registration ───────────────────────────────────────────────────── */

static bool typed_syncable(const AimlTypeNode* nodes, int32_t n_nodes) {
    if (aiml_ser_wire_syncable(nodes, n_nodes)) return true;
    fprintf(stderr, "[mirror] register: model has a recursive or resource field and cannot be synchronized\n");
    return false;
}

int32_t aiml_mirror_session_register_typed(void* session, void* obj, int64_t obj_size,
                                            const AimlTypeNode* nodes, int32_t n_nodes) {
    auto* s = (AimlMirrorSession*)session;
    if (!s || !obj || obj_size <= 0) return -1;
    if (!typed_syncable(nodes, n_nodes)) return -1;
    int32_t nid = s->next_net_id++;
    aiml_mirror_session_register_id_typed(session, obj, obj_size, nid, nodes, n_nodes);
    return nid;
}

void aiml_mirror_session_register_id_typed(void* session, void* obj, int64_t obj_size,
                                            int32_t id,
                                            const AimlTypeNode* nodes, int32_t n_nodes) {
    auto* s = (AimlMirrorSession*)session;
    if (!s || !obj || obj_size <= 0) return;
    if (!typed_syncable(nodes, n_nodes)) return;

    /* Ensure next_net_id stays ahead of any explicitly assigned id */
    if (id >= s->next_net_id) s->next_net_id = id + 1;

    /* If a placeholder was created by PKT_REGISTER, fill it in */
    if (s->by_net_id.count(id)) {
        AimlMirrorRegistered* existing = s->by_net_id[id];
        if (existing->obj_ptr == nullptr) {
            existing->obj_ptr   = obj;
            existing->obj_size  = obj_size;
            existing->ser_nodes = nodes;
            existing->ser_n     = n_nodes;
            /* Always reallocate shadow with client's obj_size.
             * PKT_REGISTER used server's sizeof — may differ from client's sizeof.
             * memcpy(shadow, obj, obj_size) would overflow a too-small shadow. */
            if (existing->shadow) free(existing->shadow);
            existing->shadow = (uint8_t*)malloc((size_t)obj_size);
            if (existing->shadow) memcpy(existing->shadow, obj, (size_t)obj_size);
            s->by_ptr[obj] = existing;
            /* A DELTA that arrived before binding: same rules as a live one (spec/44_5 §10) */
            if (existing->pending_blob) {
                int rc = existing->pending_format == 2
                    ? apply_typed_delta(existing, existing->pending_blob, (size_t)existing->pending_blob_len)
                    : (existing->last_decode_error = -2);
                if (rc != 0)
                    fprintf(stderr, "[mirror] initial DELTA rejected net_id=%d rc=%d\n", id, rc);
                free(existing->pending_blob);
                existing->pending_blob     = nullptr;
                existing->pending_blob_len = 0;
                existing->pending_format   = 0;
            }
        }
        return;
    }

    AimlMirrorRegistered* reg = new AimlMirrorRegistered();
    reg->obj_ptr   = obj;
    reg->obj_size  = obj_size;
    reg->net_id    = id;
    reg->is_owner  = s->is_host;
    reg->ser_nodes = nodes;
    reg->ser_n     = n_nodes;
    reg->shadow = (uint8_t*)malloc((size_t)obj_size);
    memcpy(reg->shadow, obj, (size_t)obj_size);
    /* Initialize shadow_blob for typed dirty tracking (owner side only) */
    if (reg->is_owner && nodes && n_nodes > 0) {
        size_t len = 0;
        char* blob = encode_delta_blob(reg, &len);
        if (blob) { reg->shadow_blob = (uint8_t*)blob; reg->shadow_blob_len = (int32_t)len; }
    }

    s->by_net_id[id] = reg;
    s->by_ptr[obj]   = reg;

    AIML_DBG("register_id_typed net_id=%d obj_size=%d owner=%d nodes=%d",
             id, (int)obj_size, (int)reg->is_owner, n_nodes);

    /* Notify all peers of new object, send initial typed delta */
    for (auto* peer : s->peers) {
        send_register_pkt(peer, id, (int32_t)obj_size);
        send_delta_to_peer(peer, reg);
    }
}

/* spec/44_5 §4 readiness: snapshots applied to a registration / the last snapshot result. */
uint64_t aiml_mirror_session_snapshot_revision(void* session, int32_t net_id) {
    auto* s = (AimlMirrorSession*)session;
    if (!s) return 0;
    auto it = s->by_net_id.find(net_id);
    return it == s->by_net_id.end() ? 0 : it->second->snapshot_revision;
}

int32_t aiml_mirror_session_snapshot_error(void* session, int32_t net_id) {
    auto* s = (AimlMirrorSession*)session;
    if (!s) return -3;
    auto it = s->by_net_id.find(net_id);
    return it == s->by_net_id.end() ? -3 : it->second->last_decode_error;
}

/* ── Event queue ──────────────────────────────────────────────────────────── */

/* ── poll_event / poll_rpc ────────────────────────────────────────────────── */
/* These combine has_* + next_* into one call.
 * They return a pointer to the event struct if one was queued, or a pointer to
 * a static empty sentinel (type/method == "") when the queue is empty.
 * The caller never receives a null pointer, so no null-check needed on AIML side.
 */
static AimlMirrorEvent s_empty_mirror_event = { "", nullptr, 0, 0 };
static AimlRpcEvent    s_empty_rpc_event    = { "", 0, 0, 0, nullptr, 0, 0, 0, 0 };

/* Free a consumed RPC event: method string + payload blob (both malloc'd) + the
 * heap event itself.  Never touches the static empty sentinel. */
static void aiml_free_rpc_event(AimlRpcEvent* ev) {
    if (!ev || ev == &s_empty_rpc_event) return;
    if (ev->method)   free((void*)ev->method);
    if (ev->arg_blob) free((void*)ev->arg_blob);
    delete ev;
}
/* Free a consumed mirror event: only the heap object — `type` is a static string
 * literal and `peer` is owned by the session (or already deleted on disconnect). */
static void aiml_free_mirror_event(AimlMirrorEvent* ev) {
    if (!ev || ev == &s_empty_mirror_event) return;
    delete ev;
}

void* aiml_mirror_session_poll_event(void* session) {
    auto* s = (AimlMirrorSession*)session;
    if (!s) return &s_empty_mirror_event;
    /* Previous event borrowed by AIML is done now (next poll) — free it. */
    aiml_free_mirror_event(s->last_consumed_event);
    s->last_consumed_event = nullptr;
    if (s->event_queue.empty()) return &s_empty_mirror_event;
    AimlMirrorEvent* ev = s->event_queue.front();
    s->event_queue.pop();
    s->last_consumed_event = ev;
    AIML_DBG("poll_event type=%s net_id=%d", ev->type ? ev->type : "", ev->net_id);
    return ev;
}

void* aiml_mirror_session_poll_rpc(void* session) {
    auto* s = (AimlMirrorSession*)session;
    if (!s) return &s_empty_rpc_event;
    /* Previous rpc borrowed by AIML is done now (next poll) — free it. */
    aiml_free_rpc_event(s->last_consumed_rpc);
    s->last_consumed_rpc = nullptr;
    AimlRpcEvent* ev = rpc_take_at(s, 0);
    if (!ev) return &s_empty_rpc_event;
    s->last_consumed_rpc = ev;
    AIML_DBG("poll_rpc method=%s net_id=%d arg=%d arg_kind=%d",
             ev->method ? ev->method : "", ev->net_id, ev->arg, ev->arg_kind);
    return ev;
}

int32_t aiml_mirror_session_has_event(void* session) {
    auto* s = (AimlMirrorSession*)session;
    if (!s) return 0;
    int32_t r = s->event_queue.empty() ? 0 : 1;
    return r;
}

void* aiml_mirror_session_next_event(void* session) {
    auto* s = (AimlMirrorSession*)session;
    if (!s) return nullptr;
    aiml_free_mirror_event(s->last_consumed_event);
    s->last_consumed_event = nullptr;
    if (s->event_queue.empty()) return nullptr;
    AimlMirrorEvent* ev = s->event_queue.front();
    s->event_queue.pop();
    s->last_consumed_event = ev;
    return ev;
}

/* ── Sync control ─────────────────────────────────────────────────────────── */

void aiml_mirror_session_set_sync_rate(void* session, void* obj, float hz) {
    auto* s = (AimlMirrorSession*)session;
    if (!s || !obj) return;
    auto it = s->by_ptr.find(obj);
    if (it != s->by_ptr.end()) it->second->sync_rate = hz;
}

void aiml_mirror_session_sync_now(void* session, void* obj) {
    auto* s = (AimlMirrorSession*)session;
    if (!s || !obj) return;
    auto it = s->by_ptr.find(obj);
    if (it != s->by_ptr.end()) send_delta_pkt(s, it->second, nullptr);
}

void aiml_mirror_session_set_sync_mode(void* session, void* obj, const char* mode) {
    auto* s = (AimlMirrorSession*)session;
    if (!s || !obj || !mode) return;
    auto it = s->by_ptr.find(obj);
    if (it != s->by_ptr.end())
        it->second->manual_sync = (strcmp(mode, "manual") == 0);
}

/* spec/44_5 §7.1: attach the class's rpc table (compiler-emitted) to a registered object. */
struct AimlRpcTableEntry {
    const char* method; const AimlTypeNode* nodes; int32_t n_nodes; int64_t args_size;
    void (*thunk)(void*, void*);
};
void aiml_mirror_session_set_rpc_table(void* session, void* obj, const void* table, int32_t count) {
    auto* s = (AimlMirrorSession*)session;
    if (!s || !obj || !table || count <= 0) return;
    auto it = s->by_ptr.find(obj);
    if (it == s->by_ptr.end()) return;
    {
        std::lock_guard<std::mutex> lk(g_rpc_obj_mu);
        auto g = g_rpc_obj_session.find(obj);
        if (g != g_rpc_obj_session.end() && g->second != s) {
            fprintf(stderr, "[mirror] rpc: object %p is already registered in another session — rpc routing refused\n", obj);
            return;
        }
        g_rpc_obj_session[obj] = s;
    }
    AimlMirrorRegistered* reg = it->second;
    reg->rpc.clear();
    const auto* rows = (const AimlRpcTableEntry*)table;
    for (int32_t i = 0; i < count; ++i) {
        AimlMirrorRegistered::RpcMethod m;
        m.name = rows[i].method ? rows[i].method : "";
        m.nodes = rows[i].nodes; m.n_nodes = rows[i].n_nodes; m.args_size = rows[i].args_size;
        m.thunk = rows[i].thunk;
        reg->rpc.push_back(m);
    }
}

static AimlMirrorRegistered::RpcMethod* rpc_find_method(AimlMirrorRegistered* reg, const char* method) {
    if (!reg || !method) return nullptr;
    for (auto& m : reg->rpc) if (m.name == method) return &m;
    return nullptr;
}

void aiml_mirror_session_set_rpc_target(void* session, void* obj, const char* method, const char* target) {
    auto* s = (AimlMirrorSession*)session;
    if (!s || !obj || !method || !target) return;
    auto it = s->by_ptr.find(obj);
    if (it == s->by_ptr.end()) {
        fprintf(stderr, "[mirror] set_rpc_target: object is not registered in this session\n");
        return;
    }
    auto* m = rpc_find_method(it->second, method);
    if (!m) {
        fprintf(stderr, "[mirror] set_rpc_target: '%s' is not an rpc method of this object\n", method);
        return;
    }
    int t = -1;
    if      (!strcmp(target, "owner"))  t = 0;
    else if (!strcmp(target, "server")) t = 1;
    else if (!strcmp(target, "all"))    t = 2;
    else if (!strcmp(target, "others")) t = 3;
    if (t < 0) {
        fprintf(stderr, "[mirror] set_rpc_target: unknown target '%s' (owner|server|all|others)\n", target);
        return;
    }
    m->target = t;
}

/* ── mirror_event accessors ───────────────────────────────────────────────── */

const char* aiml_mirror_event_type(void* ev) {
    auto* e = (AimlMirrorEvent*)ev;
    return e ? e->type : "";
}

// String event type → MirrorEventType enum value (must match the injected enum
// in ast_compiler.cpp registerTypes).  0 = none/empty sentinel.
int32_t aiml_mirror_type_enum(const char* type) {
    if (!type) return 0;
    if (strcmp(type, "peer_connected")      == 0) return 1;
    if (strcmp(type, "peer_disconnected")   == 0) return 2;
    if (strcmp(type, "object_registered")   == 0) return 3;
    if (strcmp(type, "object_unregistered") == 0) return 4;
    return 0;
}

void* aiml_mirror_event_peer(void* ev) {
    auto* e = (AimlMirrorEvent*)ev;
    return e ? (void*)e->peer : nullptr;
}

int32_t aiml_mirror_event_net_id(void* ev) {
    auto* e = (AimlMirrorEvent*)ev;
    return e ? e->net_id : 0;
}

/* ── RPC (spec/44_5 §7) ───────────────────────────────────────────────────── */

/* RPC kinds (v3 header) */
enum { RPC_PUSH = 0, RPC_REQUEST = 1, RPC_RESPONSE = 2, RPC_OBJECT_CALL = 3 };

/* Receive-side queue limits per sender peer (AP4): overload disconnects THAT peer, visibly;
 * events of other consumers are never dropped to make room. */
static const size_t kMaxQueuedPerPeer      = 4096;
static const size_t kMaxQueuedBytesPerPeer = (size_t)64 << 20;

static size_t rpc_ev_bytes(const AimlRpcEvent* ev) {
    return sizeof(AimlRpcEvent) + (ev->method ? strlen(ev->method) + 1 : 0) + (size_t)ev->arg_blob_len;
}

static bool rpc_enqueue(AimlMirrorSession* s, AimlMirrorPeer* from, AimlRpcEvent* ev) {
    size_t& cnt = s->rpc_q_count[ev->peer_id];
    size_t& byt = s->rpc_q_bytes[ev->peer_id];
    size_t b = rpc_ev_bytes(ev);
    if (cnt + 1 > kMaxQueuedPerPeer || byt + b > kMaxQueuedBytesPerPeer) {
        fprintf(stderr, "[mirror] rpc queue overload from peer %d (%zu events, %zu bytes)\n",
                ev->peer_id, cnt, byt);
        mirror_mark_dead(from, "rpc queue overload");
        aiml_free_rpc_event(ev);
        return false;
    }
    cnt += 1; byt += b;
    s->rpc_queue.push_back(ev);
    return true;
}

/* Remove the event at idx (others keep their positions) and release its accounting. */
static AimlRpcEvent* rpc_take_at(AimlMirrorSession* s, size_t idx) {
    if (idx >= s->rpc_queue.size()) return nullptr;
    AimlRpcEvent* ev = s->rpc_queue[idx];
    s->rpc_queue.erase(s->rpc_queue.begin() + (long)idx);
    size_t& cnt = s->rpc_q_count[ev->peer_id];
    size_t& byt = s->rpc_q_bytes[ev->peer_id];
    size_t b = rpc_ev_bytes(ev);
    cnt = cnt > 0 ? cnt - 1 : 0;
    byt = byt > b ? byt - b : 0;
    return ev;
}

/* AP4: the one search-and-remove helper (send_and_await, take_rpc, take_response). */
template <class Pred>
static AimlRpcEvent* rpc_take_matching(AimlMirrorSession* s, Pred pred) {
    for (size_t i = 0; i < s->rpc_queue.size(); ++i)
        if (s->rpc_queue[i] && pred(s->rpc_queue[i])) return rpc_take_at(s, i);
    return nullptr;
}

static uint64_t rpc_new_request_id(AimlMirrorSession* s) {
    uint64_t id = s->next_request_id++;
    if (s->next_request_id == 0) s->next_request_id = 1;   /* 0 = push / no correlation */
    while (s->outstanding.count(id) || id == 0) id = s->next_request_id++;
    return id;
}

static std::vector<uint8_t> rpc_build_v2(int32_t target, const char* method, uint8_t arg_kind,
                                         int32_t arg_i32, const uint8_t* blob, uint32_t blob_len) {
    size_t mlen = strlen(method);
    std::vector<uint8_t> pkt(6 + 1 + mlen + 1 + 4 + 4 + (size_t)blob_len);
    write_u32_le(pkt.data(), (uint32_t)target);
    pkt[4] = PKT_RPC; pkt[5] = 0;
    pkt[6] = (uint8_t)mlen;
    memcpy(pkt.data() + 7, method, mlen);
    size_t o = 7 + mlen;
    pkt[o] = arg_kind;
    write_i32_le(pkt.data() + o + 1, arg_i32);
    write_u32_le(pkt.data() + o + 5, blob_len);
    if (blob_len && blob) memcpy(pkt.data() + o + 9, blob, blob_len);
    return pkt;
}

static std::vector<uint8_t> rpc_build_v3(int32_t object_id, uint8_t kind, uint64_t request_id,
                                         const char* method, uint8_t arg_kind, int32_t arg_i32,
                                         const uint8_t* blob, uint32_t blob_len) {
    size_t mlen = strlen(method);
    size_t total = 30 + mlen + (size_t)blob_len;
    std::vector<uint8_t> pkt(total);
    write_u32_le(pkt.data(), (uint32_t)object_id);
    pkt[4] = PKT_RPC_V3; pkt[5] = 3;
    write_u32_le(pkt.data() + 6, (uint32_t)total);
    pkt[10] = kind; pkt[11] = 0;
    write_u64_le(pkt.data() + 12, request_id);
    pkt[20] = (uint8_t)mlen;
    memcpy(pkt.data() + 21, method, mlen);
    size_t o = 21 + mlen;
    pkt[o] = arg_kind;
    write_i32_le(pkt.data() + o + 1, arg_i32);
    write_u32_le(pkt.data() + o + 5, blob_len);
    if (blob_len && blob) memcpy(pkt.data() + o + 9, blob, blob_len);
    return pkt;
}

/* Send one RPC.  Routing (spec/44_5 §7, AP3): a client sends to the server only; the host sends
 * to `target` (> 0: exactly that peer — an unknown id sends nothing; 0: explicit broadcast).
 * Per destination: v3 if its HELLO announced >= 3, else v2.  A request (needs correlation) is
 * never downgraded — returns the number of peers the packet went to. */
static int mirror_send_rpc_any(AimlMirrorSession* s, int32_t target, uint8_t kind, uint64_t request_id,
                               const char* method, uint8_t arg_kind, int32_t arg_i32,
                               const uint8_t* blob, uint32_t blob_len)
{
    if (!s || !method) return 0;
    size_t mlen = strlen(method);
    if (mlen == 0 || mlen > 255) {   /* spec/44_5 §10: refuse instead of truncating */
        fprintf(stderr, "[mirror] send_rpc: method name length %zu not in 1..255 — not sent\n", mlen);
        return 0;
    }
    if (target < 0) {
        fprintf(stderr, "[mirror] send_rpc: negative target %d — not sent\n", target);
        return 0;
    }
    if (blob_len > kMaxRpcFrame) {
        fprintf(stderr, "[mirror] send_rpc: payload %u bytes over the 8 MiB frame limit — not sent\n", blob_len);
        return 0;
    }
    std::vector<uint8_t> v2, v3;
    int sent = 0;
    for (auto* peer : s->peers) {
        if (!peer || peer->dead) continue;
        if (s->is_host && target > 0 && peer->id != target) continue;
        if (peer->rpc_version >= 3) {
            if (v3.empty()) v3 = rpc_build_v3(0, kind, request_id, method, arg_kind, arg_i32, blob, blob_len);
            mirror_send(peer, v3.data(), v3.size());
        } else {
            if (kind == RPC_REQUEST) continue;   /* no correlation possible on v2 */
            if (v2.empty()) v2 = rpc_build_v2(target, method, arg_kind, arg_i32, blob, blob_len);
            mirror_send(peer, v2.data(), v2.size());
        }
        ++sent;
    }
    if (s->is_host && target > 0 && sent == 0)
        AIML_DBG("send_rpc target peer_id=%d not found, dropping", target);
    return sent;
}

/* Encode a typed payload with the RPC fingerprint header; returns malloc'd blob or null. */
static char* rpc_encode(const void* obj, const AimlTypeNode* nodes, int32_t n_nodes, uint32_t* total) {
    int prev_rpc = aiml_ser_set_rpc(1);
    char* blob = aiml_ser_encode(obj, nodes, n_nodes);
    aiml_ser_set_rpc(prev_rpc);
    if (!blob) return nullptr;
    *total = (uint32_t)read_i32_le((const uint8_t*)blob) + 4;
    return blob;
}

/* ── automatic object RPCs (spec/44_5 §7.1) ─────────────────────────────── */

/* arg_i32 of an object call: 1 = the sender already ran the method locally. */
static void rpc_send_object_call(AimlMirrorSession* s, int32_t net_id, const char* method, int32_t ran_locally,
                                 const uint8_t* blob, uint32_t len, int32_t exclude_peer) {
    std::vector<uint8_t> pkt = rpc_build_v3(net_id, RPC_OBJECT_CALL, 0, method, 1, ran_locally, blob, len);
    for (auto* peer : s->peers) {
        if (!peer || peer->dead || peer->rpc_version < 3) continue;
        if (exclude_peer >= 0 && peer->id == exclude_peer) continue;
        mirror_send(peer, pkt.data(), pkt.size());
    }
}

static void rpc_execute(AimlMirrorRegistered* reg, AimlMirrorRegistered::RpcMethod* m,
                        const uint8_t* blob, uint32_t len) {
    if (!reg->obj_ptr || !m->thunk || m->args_size <= 0) return;
    void* args = calloc(1, (size_t)m->args_size);
    if (!args) return;
    if (aiml_ser_decode_sized((const char*)blob, len, args, m->nodes, m->n_nodes) == 0)
        m->thunk(reg->obj_ptr, args);
    else
        fprintf(stderr, "[mirror] object call '%s': undecodable arguments — not executed\n", m->name.c_str());
    aiml_ser_free_decoded(args, m->nodes, m->n_nodes);
    free(args);
}

static void rpc_receive_object_call(AimlMirrorSession* s, AimlMirrorPeer* from, int32_t net_id, const RpcFields& f) {
    auto it = s->by_net_id.find(net_id);
    std::string method(f.method, f.method_len);
    AimlMirrorRegistered::RpcMethod* m = (it != s->by_net_id.end()) ? rpc_find_method(it->second, method.c_str()) : nullptr;
    if (!m || f.arg_kind != 1) {
        fprintf(stderr, "[mirror] object call '%s' on object %d: not a registered rpc method — dropped\n",
                method.c_str(), net_id);
        return;
    }
    AimlMirrorRegistered* reg = it->second;
    if (s->is_host) {
        /* route by the HOST's target (a client cannot widen it); the host is never the caller here */
        int32_t sender = from ? from->id : -1;
        if (m->target == 2)        /* all: everyone once — the sender only if it did not run it yet */
            rpc_send_object_call(s, net_id, method.c_str(), 1, f.blob, f.blob_len, f.arg_i32 ? sender : -1);
        else if (m->target == 3)   /* others: everyone except the caller */
            rpc_send_object_call(s, net_id, method.c_str(), 1, f.blob, f.blob_len, sender);
    }
    rpc_execute(reg, m, f.blob, f.blob_len);
}

/* Called by the synthesized rpc wrapper: 0 = run the body locally (also for unregistered objects),
 * 1 = handled by other peers only. */
int32_t aiml_mirror_object_call(void* obj, const char* method, void* args) {
    AimlMirrorSession* s = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_rpc_obj_mu);
        auto g = g_rpc_obj_session.find(obj);
        if (g == g_rpc_obj_session.end()) return 0;
        s = g->second;
    }
    auto it = s->by_ptr.find(obj);
    if (it == s->by_ptr.end()) return 0;
    AimlMirrorRegistered* reg = it->second;
    AimlMirrorRegistered::RpcMethod* m = rpc_find_method(reg, method);
    if (!m || !m->nodes) return 0;
    bool send, local;
    if (s->is_host) {   /* the host owns its objects */
        send  = (m->target == 2 || m->target == 3);
        local = (m->target != 3);
    } else {            /* a client replica: everything goes via the host */
        send  = true;
        local = (m->target == 2);
    }
    if (send) {
        uint32_t total = 0;
        char* blob = rpc_encode(args, m->nodes, m->n_nodes, &total);
        if (blob) {
            rpc_send_object_call(s, reg->net_id, method, local ? 1 : 0, (const uint8_t*)blob, total, -1);
            free(blob);
        }
    }
    return local ? 0 : 1;
}

void aiml_mirror_session_send_rpc(void* session, int32_t net_id,
                                  const char* method, int32_t arg) {
    AIML_DBG("send_rpc net_id=%d method=%s arg=%d", net_id, method ? method : "", arg);
    mirror_send_rpc_any((AimlMirrorSession*)session, net_id, RPC_PUSH, 0, method, 0, arg, nullptr, 0);
}

/* Send an RPC whose payload is a typed model instance (push). */
void aiml_mirror_session_send_rpc_model(void* session, int32_t net_id,
                                        const char* method, int32_t arg_i32,
                                        const void* obj,
                                        const AimlTypeNode* nodes,
                                        int32_t n_nodes)
{
    auto* s = (AimlMirrorSession*)session;
    if (!s || !method || !obj || !nodes || n_nodes <= 0) return;
    AIML_DBG("send_rpc_model net_id=%d method=%s arg_i32=%d nodes=%d", net_id, method, arg_i32, n_nodes);
    uint32_t total = 0;
    char* blob = rpc_encode(obj, nodes, n_nodes, &total);
    if (!blob) return;
    mirror_send_rpc_any(s, net_id, RPC_PUSH, 0, method, 1, arg_i32, (const uint8_t*)blob, total);
    free(blob);
}

/* spec/44_5 §7 session.reply_rpc(ev, method, arg[, model]): answer the request in `ev` — to the
 * sender peer (from the connection, never from payload fields), echoing its request id. */
void aiml_mirror_session_reply_rpc(void* session, void* ev_ptr, const char* method, int32_t arg_i32,
                                   const void* obj, const AimlTypeNode* nodes, int32_t n_nodes)
{
    auto* s  = (AimlMirrorSession*)session;
    auto* ev = (AimlRpcEvent*)ev_ptr;
    if (!s || !ev || !method) return;
    uint8_t  kind = (ev->kind == RPC_REQUEST) ? RPC_RESPONSE : RPC_PUSH;
    uint64_t rid  = (ev->kind == RPC_REQUEST) ? ev->request_id : 0;
    int32_t  target = s->is_host ? ev->peer_id : 0;
    if (s->is_host && target <= 0) return;   /* no sender identity → nobody to answer */
    if (obj && nodes && n_nodes > 0) {
        uint32_t total = 0;
        char* blob = rpc_encode(obj, nodes, n_nodes, &total);
        if (!blob) return;
        mirror_send_rpc_any(s, target, kind, rid, method, 1, arg_i32, (const uint8_t*)blob, total);
        free(blob);
    } else {
        mirror_send_rpc_any(s, target, kind, rid, method, 0, arg_i32, nullptr, 0);
    }
}

/* Async request (spec/44_5 §7): returns the request id (> 0) or 0 when it could not be sent
 * (no peer with RPC v3 yet).  Pair with take_response(id, target). */
int64_t aiml_mirror_session_send_request(void* session, int32_t target, const char* method, int32_t arg_i32,
                                         const void* obj, const AimlTypeNode* nodes, int32_t n_nodes)
{
    auto* s = (AimlMirrorSession*)session;
    if (!s || !method) return 0;
    uint64_t id = rpc_new_request_id(s);
    int sent;
    if (obj && nodes && n_nodes > 0) {
        uint32_t total = 0;
        char* blob = rpc_encode(obj, nodes, n_nodes, &total);
        if (!blob) return 0;
        sent = mirror_send_rpc_any(s, target, RPC_REQUEST, id, method, 1, arg_i32, (const uint8_t*)blob, total);
        free(blob);
    } else {
        sent = mirror_send_rpc_any(s, target, RPC_REQUEST, id, method, 0, arg_i32, nullptr, 0);
    }
    if (sent <= 0) return 0;
    s->outstanding.insert(id);
    return (int64_t)id;
}

/* Take the response of request `id`: 1 = decoded into out, 0 = not arrived yet, -1 = protocol
 * error (wrong method/type/undecodable — the request is finished either way). */
int32_t aiml_mirror_session_take_response(void* session, int64_t id, const char* expect_method,
                                          void* out, int64_t out_size,
                                          const AimlTypeNode* nodes, int32_t n_nodes)
{
    auto* s = (AimlMirrorSession*)session;
    if (!s || id <= 0 || !out || out_size <= 0 || !nodes || n_nodes <= 0) return -1;
    uint64_t rid = (uint64_t)id;
    if (!s->outstanding.count(rid)) return -1;
    AimlRpcEvent* ev = rpc_take_matching(s, [rid](AimlRpcEvent* e) {
        return e->kind == RPC_RESPONSE && e->request_id == rid;
    });
    if (!ev) return 0;
    s->outstanding.erase(rid);
    int32_t rc = -1;
    if (ev->method && (!expect_method || !*expect_method || std::strcmp(ev->method, expect_method) == 0)
        && ev->arg_kind == 1 && ev->arg_blob
        && aiml_ser_decode_sized_tx((const char*)ev->arg_blob, (size_t)ev->arg_blob_len,
                                    out, (size_t)out_size, nodes, n_nodes, 1) == 0)
        rc = 1;
    else
        fprintf(stderr, "[mirror] take_response id=%lld: response '%s' is not the expected '%s' payload\n",
                (long long)id, ev->method ? ev->method : "?", expect_method ? expect_method : "");
    aiml_free_rpc_event(ev);
    return rc;
}

/* Forget a request whose response is no longer wanted (a late response is then dropped). */
/* spec/44_5 §7.2: drop interest in `id`.  Erasing the registration alone is not
 * enough — a response that already ARRIVED sits in the rpc queue, and once the id
 * is gone take_response can no longer reach it (it checks `outstanding` first).
 * Nothing is ever dropped to make room, so such an orphan counts against the
 * per-peer 4096/64 MiB budget forever and eventually disconnects our own peer.
 * So remove and free the queued response too. */
void aiml_mirror_session_cancel_request(void* session, int64_t id) {
    auto* s = (AimlMirrorSession*)session;
    if (!s || id <= 0) return;
    const uint64_t rid = (uint64_t)id;
    s->outstanding.erase(rid);
    while (AimlRpcEvent* ev = rpc_take_matching(s, [rid](AimlRpcEvent* e) {
               return e->kind == RPC_RESPONSE && e->request_id == rid;
           }))
        aiml_free_rpc_event(ev);
}

/* Decode a received typed-RPC payload into a caller-allocated, zero-initialised struct
 * (legacy entry; the compiler uses aiml_rpc_event_decode_tx).  0 = ok, -1 = error. */
int32_t aiml_rpc_event_decode_into(void* ev_ptr, void* out,
                                   const AimlTypeNode* nodes, int32_t n_nodes)
{
    auto* ev = (AimlRpcEvent*)ev_ptr;
    if (!ev || !out || !nodes || n_nodes <= 0) return -1;
    if (ev->arg_kind != 1 || !ev->arg_blob || ev->arg_blob_len <= 0) return -1;
    int rc = aiml_ser_decode_sized((const char*)ev->arg_blob, (size_t)ev->arg_blob_len, out, nodes, n_nodes);
    if (rc == 0) return 0;
    aiml_ser_free_decoded(out, nodes, n_nodes);
    aiml_ser_init_empty_containers(out, nodes, n_nodes);
    return -1;
}

/* spec/44_5 (AP1): transactional decode — a failed decode leaves `out` untouched; on success the
 * target's previous heap is released and replaced.  out_size = target struct size (compiler). */
int32_t aiml_rpc_event_decode_tx(void* ev_ptr, void* out, int64_t out_size,
                                 const AimlTypeNode* nodes, int32_t n_nodes)
{
    auto* ev = (AimlRpcEvent*)ev_ptr;
    if (!ev || !out || out_size <= 0 || !nodes || n_nodes <= 0) return -1;
    if (ev->arg_kind != 1 || !ev->arg_blob || ev->arg_blob_len <= 0) return -1;
    return aiml_ser_decode_sized_tx((const char*)ev->arg_blob, (size_t)ev->arg_blob_len,
                                    out, (size_t)out_size, nodes, n_nodes, 1) == 0 ? 0 : -1;
}

/* spec/44_5 session.take_rpc(method, target): remove + decode the FIRST queued push (or
 * uncorrelated reply) with this method whose payload is of the target's type; every other event
 * keeps its position.  A queue filter — not a request correlation.  1 = taken, 0 = none. */
int32_t aiml_mirror_session_take_rpc(void* session, const char* method, void* out, int64_t out_size,
                                     const AimlTypeNode* nodes, int32_t n_nodes)
{
    auto* s = (AimlMirrorSession*)session;
    if (!s || !method || !out || out_size <= 0 || !nodes || n_nodes <= 0) return 0;
    AimlRpcEvent* ev = rpc_take_matching(s, [&](AimlRpcEvent* e) {
        return (e->kind == RPC_PUSH || (e->kind == RPC_RESPONSE && e->request_id == 0))
            && e->method && std::strcmp(e->method, method) == 0 && e->arg_kind == 1 && e->arg_blob
            && aiml_ser_rpc_blob_matches_sized((const char*)e->arg_blob, (size_t)e->arg_blob_len, nodes, n_nodes);
    });
    if (!ev) return 0;
    int rc = aiml_ser_decode_sized_tx((const char*)ev->arg_blob, (size_t)ev->arg_blob_len,
                                      out, (size_t)out_size, nodes, n_nodes, 1);
    aiml_free_rpc_event(ev);
    return rc == 0 ? 1 : 0;
}

/* Find the peer an expected response comes from: the host's target peer, or the client's server. */
static AimlMirrorPeer* rpc_peer_for(AimlMirrorSession* s, int32_t target) {
    for (auto* p : s->peers)
        if (p && !p->dead && (!s->is_host || p->id == target)) return p;
    return nullptr;
}

/* Client convenience (spec/44_5 §7): send a typed request and wait for ITS response (request id +
 * responding peer), up to timeout_ms on a monotonic clock.  1 = decoded into out, 0 = timeout,
 * no v3 peer, or a correlated but unusable response.  Unrelated events stay queued in order. */
int32_t aiml_mirror_session_send_and_await(
    void* session, int32_t net_id,
    const char* method, int32_t arg_i32,
    const void* req_obj, const AimlTypeNode* req_nodes, int32_t req_n_nodes,
    const char* expect_method,
    void* out_obj, int64_t out_size, const AimlTypeNode* out_nodes, int32_t out_n_nodes,
    int32_t timeout_ms)
{
    auto* s = (AimlMirrorSession*)session;
    if (!s || !method || !req_obj || !req_nodes || req_n_nodes <= 0
          || !out_obj || out_size <= 0 || !out_nodes || out_n_nodes <= 0)
        return 0;

    /* Watchdog breadcrumb: name the in-flight method while we block here. */
    const char* prev_phase = g_rt_rpc_phase;
    g_rt_rpc_phase = method;
    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();
    const auto deadline = t0 + std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 0);
    auto elapsed_ms = [&]() {
        return (int32_t)std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t0).count();
    };
    const int32_t tick_ms = 2;

    /* 1. Capability: correlation needs RPC v3 on the responding peer (its HELLO arrives with the
     *    first update after connecting).  A v2 peer never sends HELLO → report, do not guess. */
    AimlMirrorPeer* peer = rpc_peer_for(s, net_id);
    const auto cap_deadline = t0 + std::chrono::milliseconds(timeout_ms < 1500 ? (timeout_ms > 0 ? timeout_ms : 0) : 1500);
    while (peer && peer->rpc_version < 3 && clk::now() < cap_deadline) {
        aiml_mirror_session_update(s);
        peer = rpc_peer_for(s, net_id);
        if (peer && peer->rpc_version >= 3) break;
        time_sleep_ms(tick_ms);
    }
    if (!peer || peer->rpc_version < 3) {
        fprintf(stderr, "[mirror] send_and_await '%s': peer does not support RPC v3 request/response\n", method);
        g_rt_rpc_phase = prev_phase;
        return 0;
    }
    const int32_t responder = peer->id;

    /* 2. Send the request. */
    int64_t id = aiml_mirror_session_send_request(s, net_id, method, arg_i32, req_obj, req_nodes, req_n_nodes);
    if (id <= 0) { g_rt_rpc_phase = prev_phase; return 0; }
    const uint64_t rid = (uint64_t)id;

    /* 3. Wait for response(id) from that peer. */
    const int32_t warn_after_ms = 2000;
    bool warned = false;
    for (;;) {
        aiml_mirror_session_update(s);
        AimlRpcEvent* ev = rpc_take_matching(s, [&](AimlRpcEvent* e) {
            return e->kind == RPC_RESPONSE && e->request_id == rid && e->peer_id == responder;
        });
        if (ev) {
            s->outstanding.erase(rid);
            int32_t ok = 0;
            if (ev->method && (!expect_method || std::strcmp(ev->method, expect_method) == 0)
                && ev->arg_kind == 1 && ev->arg_blob
                && aiml_ser_decode_sized_tx((const char*)ev->arg_blob, (size_t)ev->arg_blob_len,
                                            out_obj, (size_t)out_size, out_nodes, out_n_nodes, 1) == 0)
                ok = 1;
            else
                fprintf(stderr, "[mirror] send_and_await '%s': response is not the expected '%s' payload\n",
                        method, expect_method ? expect_method : "");
            aiml_free_rpc_event(ev);
            g_rt_rpc_phase = prev_phase;
            return ok;
        }
        if (clk::now() >= deadline) break;
        if (!warned && elapsed_ms() >= warn_after_ms) {
            warned = true;
            aiml_rpc_stall_trace("SLOW-NO-RESPONSE", method, elapsed_ms());
        }
        time_sleep_ms(tick_ms);
    }
    s->outstanding.erase(rid);   /* a late response is dropped on arrival */
    aiml_rpc_stall_trace("TIMEOUT", method, timeout_ms);
    g_rt_rpc_phase = prev_phase;
    return 0;
}

int32_t aiml_mirror_session_has_rpc(void* session) {
    auto* s = (AimlMirrorSession*)session;
    return s ? (int32_t)s->rpc_queue.size() : 0;
}

void* aiml_mirror_session_next_rpc(void* session) {
    auto* s = (AimlMirrorSession*)session;
    if (!s) return nullptr;
    aiml_free_rpc_event(s->last_consumed_rpc);
    s->last_consumed_rpc = nullptr;
    AimlRpcEvent* ev = rpc_take_at(s, 0);
    if (!ev) return nullptr;
    s->last_consumed_rpc = ev;
    return ev;
}
