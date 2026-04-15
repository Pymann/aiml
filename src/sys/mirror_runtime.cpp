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

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
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
#include <execinfo.h>
#include <signal.h>

/* ── Debug logging (AIML_DEBUG=1 env var) ─────────────────────────────────── */

static int g_aiml_debug = (getenv("AIML_DEBUG") != nullptr) ? 1 : 0;
#define AIML_DBG(fmt, ...) do { if (g_aiml_debug) fprintf(stderr, "[mirror] " fmt "\n", ##__VA_ARGS__); } while(0)

/* ── Crash handler (auto-installed via constructor) ──────────────────────── */

static void aiml_crash_handler(int sig, siginfo_t*, void*) {
    char msg[128];
    int n = snprintf(msg, sizeof(msg), "\n=== AIML CRASH (signal %d) ===\n", sig);
    (void)write(STDERR_FILENO, msg, (size_t)n);
    void* bt[64];
    int cnt = backtrace(bt, 64);
    backtrace_symbols_fd(bt, cnt, STDERR_FILENO);
    /* Flush stderr before re-raising */
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
};

struct AimlMirrorRegistered {
    void*    obj_ptr  = nullptr;
    int64_t  obj_size = 0;
    uint8_t* shadow   = nullptr;   /* malloc(obj_size) */
    int32_t  net_id   = 0;
    bool     is_owner = false;
    float    sync_rate   = 0.0f;   /* Hz; 0 = every update */
    bool     manual_sync = false;
    /* Typed serialization (heap-field structs) */
    const AimlTypeNode* ser_nodes   = nullptr;
    int32_t             ser_n       = 0;
    bool                has_decoded = false;  /* true after first aiml_ser_decode */
    /* Typed delta received before register_id_typed: stash raw blob here */
    uint8_t*            pending_blob     = nullptr;
    int32_t             pending_blob_len = 0;
    /* Typed dirty tracking: last serialized blob (detects deep heap mutations) */
    uint8_t*            shadow_blob      = nullptr;
    int32_t             shadow_blob_len  = 0;
};

struct AimlMirrorEvent {
    const char*     type   = nullptr;
    AimlMirrorPeer* peer   = nullptr;
    int32_t         net_id = 0;
};

/* Layout MUST match the LLVM `rpc_event` struct in builtin_registry.cpp:
   { i8*, i32, i32, i32, i8*, i32 } — natural alignment.
   Fields after `arg` are additive (RPC payload v2). */
struct AimlRpcEvent {
    const char*    method        = nullptr;  /* heap-allocated, freed on session close */
    int32_t        net_id        = 0;
    int32_t        arg           = 0;        /* legacy i32 arg (also valid for arg_kind=0) */
    int32_t        arg_kind      = 0;        /* 0=i32 only, 1=model blob */
    const uint8_t* arg_blob      = nullptr;  /* heap-allocated payload (typed ser blob WITH 4B prefix) */
    int32_t        arg_blob_len  = 0;        /* length of arg_blob in bytes */
};

struct AimlMirrorSession {
    AimlMirrorTransport  listener;        /* server socket for accept loop */
    bool                 is_host = false;
    int32_t              max_peers = 64;
    int32_t              next_net_id = 1;

    std::vector<AimlMirrorPeer*>                     peers;
    std::unordered_map<int32_t, AimlMirrorRegistered*> by_net_id;
    std::unordered_map<void*,   AimlMirrorRegistered*> by_ptr;
    std::queue<AimlMirrorEvent*>                      event_queue;
    std::queue<AimlRpcEvent*>                         rpc_queue;
};

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

/* Read exactly `n` bytes from fd; returns n on success, <n on error/eof */
static ssize_t tcp_recv_all(int fd, void* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, (char*)buf + got, n - got, 0);
        if (r <= 0) return (ssize_t)got;
        got += (size_t)r;
    }
    return (ssize_t)got;
}

/* Send exactly `n` bytes to fd */
static ssize_t tcp_send_all(int fd, const void* buf, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t s = send(fd, (const char*)buf + sent, n - sent, MSG_NOSIGNAL);
        if (s <= 0) return (ssize_t)sent;
        sent += (size_t)s;
    }
    return (ssize_t)sent;
}

/* ── Packet helpers ───────────────────────────────────────────────────────── */

#define PKT_DELTA      0x01
#define PKT_RPC        0x02
#define PKT_REGISTER   0x03
#define PKT_UNREGISTER 0x04

static void write_u32_le(uint8_t* p, uint32_t v) {
    p[0]=(v)&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF;
}
static uint32_t read_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static void write_i32_le(uint8_t* p, int32_t v) { write_u32_le(p, (uint32_t)v); }
static int32_t read_i32_le(const uint8_t* p)    { return (int32_t)read_u32_le(p); }

/* ── Transport send/recv ──────────────────────────────────────────────────── */

static void mirror_send(AimlMirrorPeer* peer, const void* buf, size_t len) {
    if (!peer) return;
    if (peer->transport.type == MT_TCP) {
        tcp_send_all(peer->transport.tcp_fd, buf, len);
    } else if (peer->transport.ws_ptr) {
        aiml_ws_send_raw(peer->transport.ws_ptr, buf, (int32_t)len);
    }
}

/* Returns bytes received into `buf` (max `max_len`); 0 = no data yet */
static int mirror_recv_available(AimlMirrorPeer* peer, void* buf, int max_len) {
    if (!peer) return 0;
    if (peer->transport.type == MT_TCP) {
        int fd = peer->transport.tcp_fd;
        if (!tcp_readable(fd)) return 0;
        ssize_t n = recv(fd, buf, (size_t)max_len, 0);
        return (n > 0) ? (int)n : -1;   /* -1 = disconnected */
    } else if (peer->transport.ws_ptr) {
        int ws_fd = aiml_ws_fd(peer->transport.ws_ptr);
        if (ws_fd < 0 || !tcp_readable(ws_fd)) return 0;
        int32_t msg_len = 0;
        void* msg = aiml_ws_recv_raw(peer->transport.ws_ptr, &msg_len);
        if (!msg || msg_len <= 0) return -1;
        int copy = (msg_len < max_len) ? msg_len : max_len;
        memcpy(buf, msg, (size_t)copy);
        free(msg);
        return copy;
    }
    return 0;
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

/* Send a delta packet to a single peer (typed or untyped).
 * Does NOT update the shadow. */
static void send_delta_to_peer(AimlMirrorPeer* peer, AimlMirrorRegistered* reg) {
    if (!peer || !reg || !reg->obj_ptr) return;
    if (reg->ser_nodes) {
        /* Typed: encode struct → blob, reserved byte = 1 */
        char* blob = aiml_ser_encode(reg->obj_ptr, reg->ser_nodes, reg->ser_n);
        if (!blob) return;
        int32_t data_len;
        memcpy(&data_len, blob, 4);
        uint32_t total_blob = 4 + (uint32_t)data_len;
        std::vector<uint8_t> pkt(6 + total_blob);
        write_u32_le(pkt.data(), (uint32_t)reg->net_id);
        pkt[4] = PKT_DELTA; pkt[5] = 1;  /* reserved=1: typed/serialized */
        memcpy(pkt.data() + 6, blob, total_blob);
        free(blob);
        mirror_send(peer, pkt.data(), pkt.size());
    } else {
        /* Untyped: raw struct bytes, reserved byte = 0 */
        uint8_t hdr[6];
        write_u32_le(hdr, (uint32_t)reg->net_id);
        hdr[4] = PKT_DELTA; hdr[5] = 0;
        mirror_send(peer, hdr, 6);
        mirror_send(peer, reg->obj_ptr, (size_t)reg->obj_size);
    }
}

static void send_delta_pkt(AimlMirrorSession* s, AimlMirrorRegistered* reg, AimlMirrorPeer* exclude) {
    if (!reg || !reg->obj_ptr) return;
    if (reg->ser_nodes) {
        char* blob = aiml_ser_encode(reg->obj_ptr, reg->ser_nodes, reg->ser_n);
        if (!blob) return;
        int32_t data_len;
        memcpy(&data_len, blob, 4);
        uint32_t total_blob = 4 + (uint32_t)data_len;
        std::vector<uint8_t> pkt(6 + total_blob);
        write_u32_le(pkt.data(), (uint32_t)reg->net_id);
        pkt[4] = PKT_DELTA; pkt[5] = 1;
        memcpy(pkt.data() + 6, blob, total_blob);
        broadcast_pkt(s, pkt.data(), pkt.size(), exclude);
        /* update shadow_blob for typed dirty tracking */
        free(reg->shadow_blob);
        reg->shadow_blob = (uint8_t*)blob;
        reg->shadow_blob_len = (int32_t)total_blob;
    } else {
        size_t total = 6 + (size_t)reg->obj_size;
        std::vector<uint8_t> pkt(total);
        write_u32_le(pkt.data(), (uint32_t)reg->net_id);
        pkt[4] = PKT_DELTA; pkt[5] = 0;
        memcpy(pkt.data() + 6, reg->obj_ptr, (size_t)reg->obj_size);
        broadcast_pkt(s, pkt.data(), total, exclude);
    }
    /* update shadow (raw pointer-value copy for dirty detection) */
    memcpy(reg->shadow, reg->obj_ptr, (size_t)reg->obj_size);
}

/* ── Packet processing ────────────────────────────────────────────────────── */

static void process_packet(AimlMirrorSession* s, AimlMirrorPeer* from_peer,
                            const uint8_t* data, int data_len) {
    if (data_len < 6) return;
    int32_t net_id = read_i32_le(data);
    uint8_t  type  = data[4];

    if (type == PKT_DELTA) {
        uint8_t reserved = data[5];
        auto it = s->by_net_id.find(net_id);
        if (it != s->by_net_id.end()) {
            AimlMirrorRegistered* reg = it->second;
            if (!reg->is_owner && reg->obj_ptr) {
                if (reg->ser_nodes && reserved == 1) {
                    /* Typed: decode blob (data+6 = blob with 4-byte length prefix) */
                    if (reg->has_decoded) {
                        aiml_ser_free_decoded(reg->obj_ptr, reg->ser_nodes, reg->ser_n);
                    }
                    int dec_rc = aiml_ser_decode((const char*)(data + 6), reg->obj_ptr,
                                                 reg->ser_nodes, reg->ser_n);
                    if (dec_rc == 0) {
                        reg->has_decoded = true;
                        AIML_DBG("recv_delta net_id=%d typed=1 len=%d ok", net_id, data_len - 6);
                    } else {
                        fprintf(stderr, "[mirror-ERROR] ser_decode failed (schema mismatch?) — discarding packet\n");
                    }
                } else {
                    /* Untyped: raw memcpy */
                    int payload_len = data_len - 6;
                    int copy_len = (reg->obj_size > 0 && payload_len > 0)
                        ? (payload_len < (int)reg->obj_size ? payload_len : (int)reg->obj_size)
                        : 0;
                    if (copy_len > 0) memcpy(reg->obj_ptr, data + 6, (size_t)copy_len);
                    AIML_DBG("recv_delta net_id=%d typed=0 len=%d", net_id, copy_len);
                }
                if (reg->shadow && (reserved != 1 || reg->has_decoded)) {
                    memcpy(reg->shadow, reg->obj_ptr, (size_t)reg->obj_size);
                }
            } else if (!reg->is_owner && !reg->obj_ptr && reg->shadow) {
                /* register_id not yet called */
                if (reserved == 1) {
                    /* Typed delta: stash raw blob for decode once register_id_typed sets ser_nodes */
                    int blob_len = data_len - 6;
                    AIML_DBG("recv_delta net_id=%d typed=1 len=%d — PENDING (register_id not yet called)", net_id, blob_len);
                    if (blob_len > 0) {
                        free(reg->pending_blob);
                        reg->pending_blob = (uint8_t*)malloc((size_t)blob_len);
                        if (reg->pending_blob) {
                            memcpy(reg->pending_blob, data + 6, (size_t)blob_len);
                            reg->pending_blob_len = blob_len;
                        }
                    }
                } else {
                    /* Untyped delta: cache raw struct bytes in shadow */
                    int payload_len = data_len - 6;
                    int copy_len = (reg->obj_size > 0 && payload_len > 0)
                        ? (payload_len < (int)reg->obj_size ? payload_len : (int)reg->obj_size)
                        : 0;
                    if (copy_len > 0) memcpy(reg->shadow, data + 6, (size_t)copy_len);
                }
            }
        }
        /* If host: forward delta to all other peers */
        if (s->is_host) {
            size_t total = (size_t)data_len;
            broadcast_pkt(s, data, total, from_peer);
        }
    }
    else if (type == PKT_REGISTER) {
        if (data_len < 14) return;
        int32_t new_net_id  = read_i32_le(data + 6);
        int32_t new_obj_size = read_i32_le(data + 10);
        if (s->by_net_id.find(new_net_id) == s->by_net_id.end()) {
            AimlMirrorRegistered* reg = new AimlMirrorRegistered();
            reg->net_id  = new_net_id;
            reg->obj_size = new_obj_size;
            reg->is_owner = false;
            reg->shadow = (uint8_t*)calloc(1, (size_t)(new_obj_size > 0 ? new_obj_size : 1));
            s->by_net_id[new_net_id] = reg;
            /* Event */
            auto* ev = new AimlMirrorEvent();
            ev->type   = "object_registered";
            ev->peer   = from_peer;
            ev->net_id = new_net_id;
            s->event_queue.push(ev);
        }
        /* Host: forward to other peers */
        if (s->is_host) broadcast_pkt(s, data, (size_t)data_len, from_peer);
    }
    else if (type == PKT_UNREGISTER) {
        auto it = s->by_net_id.find(net_id);
        if (it != s->by_net_id.end()) {
            AimlMirrorRegistered* reg = it->second;
            if (!reg->is_owner) {
                if (reg->ser_nodes && reg->has_decoded && reg->obj_ptr)
                    aiml_ser_free_decoded(reg->obj_ptr, reg->ser_nodes, reg->ser_n);
                free(reg->shadow);
                free(reg->shadow_blob);
                free(reg->pending_blob);
                delete reg;
                s->by_net_id.erase(it);
            }
        }
        auto* ev = new AimlMirrorEvent();
        ev->type   = "object_unregistered";
        ev->peer   = from_peer;
        ev->net_id = net_id;
        s->event_queue.push(ev);
        if (s->is_host) broadcast_pkt(s, data, (size_t)data_len, from_peer);
    }
    else if (type == PKT_RPC) {
        /* RPC payload v2:
         *   [method_len:u8][method:N][arg_kind:u8][arg_i32:i32le][blob_len:u32le][blob:M]
         * arg_kind: 0 = legacy i32 only (blob_len must be 0)
         *           1 = typed model blob (full ser blob WITH 4-byte length prefix) */
        if (data_len < 6 + 1 + 0 + 1 + 4 + 4) return;
        uint8_t method_len = data[6];
        int hdr_off = 7 + (int)method_len;
        if (data_len < hdr_off + 1 + 4 + 4) return;
        uint8_t  arg_kind = data[hdr_off];
        int32_t  arg_i32  = read_i32_le(data + hdr_off + 1);
        uint32_t blob_len = read_u32_le(data + hdr_off + 1 + 4);
        if (data_len < hdr_off + 1 + 4 + 4 + (int)blob_len) return;

        char* method = (char*)malloc((size_t)method_len + 1);
        if (!method) return;
        memcpy(method, data + 7, method_len);
        method[method_len] = '\0';

        uint8_t* blob = nullptr;
        if (blob_len > 0) {
            blob = (uint8_t*)malloc(blob_len);
            if (!blob) { free(method); return; }
            memcpy(blob, data + hdr_off + 1 + 4 + 4, blob_len);
        }

        auto* ev = new AimlRpcEvent();
        ev->method       = method;
        ev->net_id       = net_id;
        ev->arg          = arg_i32;
        ev->arg_kind     = (int32_t)arg_kind;
        ev->arg_blob     = blob;
        ev->arg_blob_len = (int32_t)blob_len;
        s->rpc_queue.push(ev);
        /* Host: forward to other peers (multi-host relay, byte-level passthrough) */
        if (s->is_host) broadcast_pkt(s, data, (size_t)data_len, from_peer);
    }
}

/* ── Peer accept ──────────────────────────────────────────────────────────── */

static void mirror_try_accept(AimlMirrorSession* s) {
    if (!s->is_host) return;
    if ((int)s->peers.size() >= s->max_peers) return;

    if (s->listener.type == MT_TCP) {
        int srv_fd = s->listener.tcp_fd;
        if (srv_fd < 0) return;
        if (!tcp_readable(srv_fd)) return;
        struct sockaddr_in addr{}; socklen_t len = sizeof(addr);
        int client_fd = accept(srv_fd, (struct sockaddr*)&addr, &len);
        if (client_fd < 0) return;
        tcp_set_nonblocking(client_fd);

        auto* peer = new AimlMirrorPeer();
        peer->transport.type   = MT_TCP;
        peer->transport.tcp_fd = client_fd;
        peer->id = (int32_t)s->peers.size() + 1;
        s->peers.push_back(peer);

        /* Send all known objects to new peer */
        for (auto& [id, reg] : s->by_net_id) {
            if (reg->is_owner) {
                send_register_pkt(peer, reg->net_id, (int32_t)reg->obj_size);
                send_delta_to_peer(peer, reg);  /* typed-aware */
            }
        }

        auto* ev = new AimlMirrorEvent();
        ev->type   = "peer_connected";
        ev->peer   = peer;
        ev->net_id = 0;
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
        peer->id = (int32_t)s->peers.size() + 1;
        s->peers.push_back(peer);

        /* Send all known objects to new peer */
        for (auto& [id, reg] : s->by_net_id) {
            if (reg->is_owner) {
                send_register_pkt(peer, reg->net_id, (int32_t)reg->obj_size);
                send_delta_to_peer(peer, reg);
            }
        }

        auto* ev = new AimlMirrorEvent();
        ev->type   = "peer_connected";
        ev->peer   = peer;
        ev->net_id = 0;
        s->event_queue.push(ev);
    }
}

/* ── Peer receive loop ────────────────────────────────────────────────────── */

static void mirror_recv_from_peers(AimlMirrorSession* s) {
    std::vector<AimlMirrorPeer*> disconnected;
    for (auto* peer : s->peers) {
        /* Read packets in a loop until no more data */
        for (int iter = 0; iter < 64; ++iter) {
            /* Read 6-byte header first */
            uint8_t hdr[6];
            int fd = (peer->transport.type == MT_TCP) ? peer->transport.tcp_fd : -1;

            if (peer->transport.type == MT_TCP) {
                if (!tcp_readable(fd)) break;
                ssize_t n = tcp_recv_all(fd, hdr, 6);
                if (n < 6) { disconnected.push_back(peer); break; }

                uint8_t pkt_type = hdr[4];
                uint8_t reserved = hdr[5];
                size_t  payload_len = 0;
                if (pkt_type == PKT_DELTA && reserved == 1) {
                    /* Typed delta: payload = blob (starts with 4-byte data_len) */
                    uint8_t len_buf[4];
                    if (tcp_recv_all(fd, len_buf, 4) < 4) { disconnected.push_back(peer); break; }
                    int32_t blob_data_len = (int32_t)read_u32_le(len_buf);
                    size_t full_size = 6 + 4 + (size_t)blob_data_len;
                    std::vector<uint8_t> full(full_size);
                    memcpy(full.data(), hdr, 6);
                    memcpy(full.data() + 6, len_buf, 4);
                    if (blob_data_len > 0) {
                        ssize_t got = tcp_recv_all(fd, full.data() + 10, (size_t)blob_data_len);
                        if ((size_t)got < (size_t)blob_data_len) { disconnected.push_back(peer); break; }
                    }
                    process_packet(s, peer, full.data(), (int)full.size());
                    continue;
                } else if (pkt_type == PKT_DELTA) {
                    /* Untyped delta: payload = raw struct bytes, size from registered obj */
                    int32_t nid = read_i32_le(hdr);
                    auto it = s->by_net_id.find(nid);
                    payload_len = (it != s->by_net_id.end()) ? (size_t)it->second->obj_size : 0;
                } else if (pkt_type == PKT_REGISTER) {
                    payload_len = 8;  /* net_id(4) + obj_size(4) */
                } else if (pkt_type == PKT_RPC) {
                    /* Read method_len byte first to know total payload size */
                    uint8_t method_len_byte = 0;
                    if (tcp_recv_all(fd, &method_len_byte, 1) < 1) { disconnected.push_back(peer); break; }
                    /* payload = [method_len:1][method:N][arg:4]; we already consumed the first byte */
                    payload_len = 1 + (size_t)method_len_byte + 4;
                    std::vector<uint8_t> full2(6 + payload_len);
                    memcpy(full2.data(), hdr, 6);
                    full2[6] = method_len_byte;
                    size_t remaining = (size_t)method_len_byte + 4;
                    if (remaining > 0) {
                        ssize_t got = tcp_recv_all(fd, full2.data() + 7, remaining);
                        if ((size_t)got < remaining) { disconnected.push_back(peer); break; }
                    }
                    process_packet(s, peer, full2.data(), (int)full2.size());
                    continue;
                }

                std::vector<uint8_t> full(6 + payload_len);
                memcpy(full.data(), hdr, 6);
                if (payload_len > 0) {
                    ssize_t got = tcp_recv_all(fd, full.data() + 6, payload_len);
                    if ((size_t)got < payload_len) { disconnected.push_back(peer); break; }
                }
                process_packet(s, peer, full.data(), (int)full.size());
            } else {
                /* WS: drain all available frames (same as TCP path) */
                int ws_fd = peer->transport.ws_ptr ? aiml_ws_fd(peer->transport.ws_ptr) : -1;
                if (ws_fd < 0 || !tcp_readable(ws_fd)) break;
                int32_t msg_len = 0;
                void* msg = aiml_ws_recv_raw(peer->transport.ws_ptr, &msg_len);
                if (!msg || msg_len <= 0) { disconnected.push_back(peer); break; }
                process_packet(s, peer, (const uint8_t*)msg, msg_len);
                free(msg);
                /* continue: check for more frames */
            }
        }
    }

    /* Handle disconnected peers */
    for (auto* peer : disconnected) {
        auto* ev = new AimlMirrorEvent();
        ev->type   = "peer_disconnected";
        ev->peer   = peer;
        ev->net_id = 0;
        s->event_queue.push(ev);
        if (peer->transport.type == MT_TCP && peer->transport.tcp_fd >= 0)
            close(peer->transport.tcp_fd);
        s->peers.erase(std::remove(s->peers.begin(), s->peers.end(), peer), s->peers.end());
        delete peer;
    }
}

/* ── Session creation ─────────────────────────────────────────────────────── */

static AimlMirrorSession* mirror_make_session() {
    return new AimlMirrorSession();
}

void* aiml_mirror_listen(const char* url) {
    return aiml_mirror_listen_max(url, 64);
}

void* aiml_mirror_listen_max(const char* url, int32_t max_peers) {
    ParsedUrl pu = mirror_parse_url(url);
    if (!pu.ok) {
        fprintf(stderr, "[mirror] invalid url: %s\n", url ? url : "(null)");
        return nullptr;
    }

    AimlMirrorSession* s = mirror_make_session();
    s->is_host   = true;
    s->max_peers = max_peers;

    if (pu.type == MT_TCP) {
        int srv = socket(AF_INET, SOCK_STREAM, 0);
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
        /* WS/WSS server */
        void* ws_srv = nullptr;
        if (pu.type == MT_WS)
            ws_srv = aiml_ws_server(pu.host.c_str(), pu.port);
        else
            ws_srv = aiml_wss_server(pu.host.c_str(), pu.port, nullptr, nullptr);
        if (!ws_srv) { delete s; return nullptr; }
        s->listener.type   = pu.type;
        s->listener.ws_ptr = ws_srv;
    }

    return s;
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
        int fd = socket(AF_INET, SOCK_STREAM, 0);
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
    /* Free registered objects */
    for (auto& [id, reg] : s->by_net_id) {
        if (!reg->is_owner && reg->ser_nodes && reg->has_decoded && reg->obj_ptr)
            aiml_ser_free_decoded(reg->obj_ptr, reg->ser_nodes, reg->ser_n);
        free(reg->shadow);
        free(reg->shadow_blob);
        delete reg;
    }
    /* Drain event queue */
    while (!s->event_queue.empty()) {
        delete s->event_queue.front();
        s->event_queue.pop();
    }
    /* Drain RPC queue (free heap-allocated method strings) */
    while (!s->rpc_queue.empty()) {
        AimlRpcEvent* ev = s->rpc_queue.front();
        s->rpc_queue.pop();
        if (ev->method) free((void*)ev->method);
        delete ev;
    }
    delete s;
}

void aiml_mirror_session_update(void* session) {
    auto* s = (AimlMirrorSession*)session;
    if (!s) return;

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
            /* Typed: compare serialized blobs — detects deep heap mutations
             * (e.g. list element field changes where the list pointer is stable) */
            char* blob = aiml_ser_encode(reg->obj_ptr, reg->ser_nodes, reg->ser_n);
            if (!blob) continue;
            int32_t dl; memcpy(&dl, blob, 4);
            int32_t total = 4 + dl;
            bool dirty = (total != reg->shadow_blob_len) ||
                         memcmp(blob, reg->shadow_blob, (size_t)total) != 0;
            if (dirty) {
                AIML_DBG("  dirty net_id=%d typed=1 blob=%d — sending delta", id, total);
                std::vector<uint8_t> pkt(6 + (size_t)total);
                write_u32_le(pkt.data(), (uint32_t)id);
                pkt[4] = PKT_DELTA; pkt[5] = 1;
                memcpy(pkt.data() + 6, blob, (size_t)total);
                broadcast_pkt(s, pkt.data(), pkt.size(), nullptr);
                free(reg->shadow_blob);
                reg->shadow_blob = (uint8_t*)blob; blob = nullptr;
                reg->shadow_blob_len = total;
                memcpy(reg->shadow, reg->obj_ptr, (size_t)reg->obj_size);
            }
            if (blob) free(blob);
        } else {
            /* Untyped: shallow memcmp on inline struct bytes */
            if (memcmp(reg->obj_ptr, reg->shadow, (size_t)reg->obj_size) != 0) {
                AIML_DBG("  dirty net_id=%d size=%d typed=0 — sending delta",
                         id, (int)reg->obj_size);
                send_delta_pkt(s, reg, nullptr);
            }
        }
    }
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
        /* Send current state as initial delta */
        uint8_t hdr[6]; write_u32_le(hdr, (uint32_t)id);
        hdr[4] = PKT_DELTA; hdr[5] = 0;
        mirror_send(peer, hdr, 6);
        mirror_send(peer, obj, (size_t)obj_size);
    }
}

void aiml_mirror_session_unregister(void* session, void* obj) {
    auto* s = (AimlMirrorSession*)session;
    if (!s || !obj) return;
    auto pit = s->by_ptr.find(obj);
    if (pit == s->by_ptr.end()) return;

    AimlMirrorRegistered* reg = pit->second;
    int32_t nid = reg->net_id;

    send_unregister_pkt(s, nid, nullptr);

    s->by_net_id.erase(nid);
    s->by_ptr.erase(pit);
    /* Owners never have has_decoded set (they're only encoded) */
    free(reg->shadow);
    free(reg->shadow_blob);
    delete reg;
}

/* ── Typed registration ───────────────────────────────────────────────────── */

int32_t aiml_mirror_session_register_typed(void* session, void* obj, int64_t obj_size,
                                            const AimlTypeNode* nodes, int32_t n_nodes) {
    auto* s = (AimlMirrorSession*)session;
    if (!s || !obj || obj_size <= 0) return -1;
    int32_t nid = s->next_net_id++;
    aiml_mirror_session_register_id_typed(session, obj, obj_size, nid, nodes, n_nodes);
    return nid;
}

void aiml_mirror_session_register_id_typed(void* session, void* obj, int64_t obj_size,
                                            int32_t id,
                                            const AimlTypeNode* nodes, int32_t n_nodes) {
    auto* s = (AimlMirrorSession*)session;
    if (!s || !obj || obj_size <= 0) return;

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
            existing->shadow = (uint8_t*)calloc(1, (size_t)obj_size);
            /* Decode any typed delta that arrived before register_id_typed was called */
            if (existing->pending_blob && existing->pending_blob_len > 0) {
                aiml_ser_decode((const char*)existing->pending_blob, obj, nodes, n_nodes);
                existing->has_decoded = true;
                memcpy(existing->shadow, obj, (size_t)obj_size);
                free(existing->pending_blob);
                existing->pending_blob     = nullptr;
                existing->pending_blob_len = 0;
            }
            s->by_ptr[obj] = existing;
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
        char* blob = aiml_ser_encode(obj, nodes, n_nodes);
        if (blob) {
            int32_t dl; memcpy(&dl, blob, 4);
            reg->shadow_blob_len = 4 + dl;
            reg->shadow_blob = (uint8_t*)blob;
        }
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

/* ── Event queue ──────────────────────────────────────────────────────────── */

/* ── poll_event / poll_rpc ────────────────────────────────────────────────── */
/* These combine has_* + next_* into one call.
 * They return a pointer to the event struct if one was queued, or a pointer to
 * a static empty sentinel (type/method == "") when the queue is empty.
 * The caller never receives a null pointer, so no null-check needed on AIML side.
 */
static AimlMirrorEvent s_empty_mirror_event = { "", nullptr, 0 };
static AimlRpcEvent    s_empty_rpc_event    = { "", 0, 0, 0, nullptr, 0 };

void* aiml_mirror_session_poll_event(void* session) {
    auto* s = (AimlMirrorSession*)session;
    if (!s || s->event_queue.empty()) return &s_empty_mirror_event;
    AimlMirrorEvent* ev = s->event_queue.front();
    s->event_queue.pop();
    AIML_DBG("poll_event type=%s net_id=%d", ev->type ? ev->type : "", ev->net_id);
    return ev;
}

void* aiml_mirror_session_poll_rpc(void* session) {
    auto* s = (AimlMirrorSession*)session;
    if (!s || s->rpc_queue.empty()) return &s_empty_rpc_event;
    AimlRpcEvent* ev = s->rpc_queue.front();
    s->rpc_queue.pop();
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
    if (!s || s->event_queue.empty()) return nullptr;
    AimlMirrorEvent* ev = s->event_queue.front();
    s->event_queue.pop();
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

void aiml_mirror_session_set_rpc_target(void* session, void* obj, const char* method, const char* target) {
    /* Infrastructure stub in v1 — RPC interception is v2 */
    (void)session; (void)obj; (void)method; (void)target;
}

/* ── mirror_event accessors ───────────────────────────────────────────────── */

const char* aiml_mirror_event_type(void* ev) {
    auto* e = (AimlMirrorEvent*)ev;
    return e ? e->type : "";
}

void* aiml_mirror_event_peer(void* ev) {
    auto* e = (AimlMirrorEvent*)ev;
    return e ? (void*)e->peer : nullptr;
}

int32_t aiml_mirror_event_net_id(void* ev) {
    auto* e = (AimlMirrorEvent*)ev;
    return e ? e->net_id : 0;
}

/* ── RPC ──────────────────────────────────────────────────────────────────── */

/* Internal: build + send a v2 PKT_RPC packet. */
static void mirror_send_rpc_v2(AimlMirrorSession* s, int32_t net_id,
                               const char* method, uint8_t arg_kind,
                               int32_t arg_i32,
                               const uint8_t* blob, uint32_t blob_len)
{
    if (!s || !method) return;
    uint8_t method_len = (uint8_t)(strlen(method) & 0xFF);
    size_t pkt_size = 6 + 1 + (size_t)method_len + 1 + 4 + 4 + (size_t)blob_len;
    std::vector<uint8_t> pkt(pkt_size);
    write_u32_le(pkt.data(), (uint32_t)net_id);
    pkt[4] = PKT_RPC; pkt[5] = 0;
    pkt[6] = method_len;
    memcpy(pkt.data() + 7, method, method_len);
    int o = 7 + (int)method_len;
    pkt[o] = arg_kind;
    write_i32_le(pkt.data() + o + 1, arg_i32);
    write_u32_le(pkt.data() + o + 1 + 4, blob_len);
    if (blob_len && blob) memcpy(pkt.data() + o + 1 + 4 + 4, blob, blob_len);
    for (auto* peer : s->peers)
        mirror_send(peer, pkt.data(), pkt_size);
}

void aiml_mirror_session_send_rpc(void* session, int32_t net_id,
                                  const char* method, int32_t arg) {
    AIML_DBG("send_rpc net_id=%d method=%s arg=%d", net_id, method ? method : "", arg);
    mirror_send_rpc_v2((AimlMirrorSession*)session, net_id, method,
                       /*arg_kind=*/0, arg, nullptr, 0);
}

/* Send an RPC whose payload is a typed model instance.
 * `obj`     — pointer to the model struct (caller-owned).
 * `nodes/n` — model schema (pass __aiml_sernodes_<Type> + count).
 * The arg_i32 is sent alongside the blob for backwards compatibility / quick discriminator.
 */
void aiml_mirror_session_send_rpc_model(void* session, int32_t net_id,
                                        const char* method, int32_t arg_i32,
                                        const void* obj,
                                        const AimlTypeNode* nodes,
                                        int32_t n_nodes)
{
    auto* s = (AimlMirrorSession*)session;
    if (!s || !method || !obj || !nodes || n_nodes <= 0) return;
    AIML_DBG("send_rpc_model net_id=%d method=%s arg_i32=%d nodes=%d", net_id, method, arg_i32, n_nodes);
    char* blob = aiml_ser_encode(obj, nodes, n_nodes);
    if (!blob) return;
    /* aiml_ser_encode prefixes the blob with a 4-byte little-endian length
       (NOT including the prefix itself); we ship the entire blob (prefix + payload). */
    int32_t payload_len = read_i32_le((const uint8_t*)blob);
    uint32_t total_len  = (uint32_t)(payload_len + 4);
    mirror_send_rpc_v2(s, net_id, method, /*arg_kind=*/1, arg_i32,
                       (const uint8_t*)blob, total_len);
    free(blob);
}

/* Decode a received typed-RPC payload into a caller-allocated, zero-initialised
 * struct. Returns 0 on success, -1 on error / wrong arg_kind. The decoded struct
 * may contain heap allocations (strings, lists, maps, buffers); the caller must
 * release them with `aiml_ser_free_decoded()` when done. */
int32_t aiml_rpc_event_decode_into(void* ev_ptr, void* out,
                                   const AimlTypeNode* nodes, int32_t n_nodes)
{
    auto* ev = (AimlRpcEvent*)ev_ptr;
    if (!ev || !out || !nodes || n_nodes <= 0) return -1;
    if (ev->arg_kind != 1 || !ev->arg_blob || ev->arg_blob_len <= 0) return -1;
    return aiml_ser_decode((const char*)ev->arg_blob, out, nodes, n_nodes);
}

int32_t aiml_mirror_session_has_rpc(void* session) {
    auto* s = (AimlMirrorSession*)session;
    return s ? (int32_t)s->rpc_queue.size() : 0;
}

void* aiml_mirror_session_next_rpc(void* session) {
    auto* s = (AimlMirrorSession*)session;
    if (!s || s->rpc_queue.empty()) return nullptr;
    AimlRpcEvent* ev = s->rpc_queue.front();
    s->rpc_queue.pop();
    return ev;
}
