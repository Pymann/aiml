#include "ws_runtime.h"
#include "string_runtime.h"

#ifdef __EMSCRIPTEN__
// ══════════════════════════════════════════════════════════════════════════════
// Browser implementation via Emscripten WebSocket API
// Same external ABI as native — server functions return null with errno=ENOTSUP.
// recv/poll block via emscripten_sleep (requires -sASYNCIFY at link time).
// ══════════════════════════════════════════════════════════════════════════════

#include <emscripten/emscripten.h>
#include <emscripten/websocket.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <cstdint>

// Singly-linked rx queue node. Plain C — no STL containers (libc++-noexcept
// makes std::vector/deque trap on alloc failure during Asyncify rewind).
struct WsRxNode {
    uint8_t*  data;
    int32_t   len;
    WsRxNode* next;
};

struct AimlWs {
    EMSCRIPTEN_WEBSOCKET_T sock;
    bool                   is_open;
    bool                   is_closed;
    WsRxNode*              rx_head;
    WsRxNode*              rx_tail;
};

static void rx_push(AimlWs* ws, const void* data, int32_t len) {
    auto* n = (WsRxNode*)malloc(sizeof(WsRxNode));
    if (!n) return;
    n->data = (uint8_t*)malloc((size_t)len);
    if (!n->data) { free(n); return; }
    memcpy(n->data, data, (size_t)len);
    n->len  = len;
    n->next = nullptr;
    if (!ws->rx_head) ws->rx_head = n;
    else              ws->rx_tail->next = n;
    ws->rx_tail = n;
}

// Caller owns n->data; must free both.
static WsRxNode* rx_pop(AimlWs* ws) {
    if (!ws->rx_head) return nullptr;
    WsRxNode* n = ws->rx_head;
    ws->rx_head = n->next;
    if (!ws->rx_head) ws->rx_tail = nullptr;
    return n;
}

static EM_BOOL ws_on_open(int, const EmscriptenWebSocketOpenEvent*, void* ud) {
    static_cast<AimlWs*>(ud)->is_open = true;
    return EM_TRUE;
}
static EM_BOOL ws_on_message(int, const EmscriptenWebSocketMessageEvent* e, void* ud) {
    rx_push(static_cast<AimlWs*>(ud), e->data, (int32_t)e->numBytes);
    return EM_TRUE;
}
static EM_BOOL ws_on_close(int, const EmscriptenWebSocketCloseEvent*, void* ud) {
    static_cast<AimlWs*>(ud)->is_closed = true;
    return EM_TRUE;
}
static EM_BOOL ws_on_error(int, const EmscriptenWebSocketErrorEvent*, void* ud) {
    static_cast<AimlWs*>(ud)->is_closed = true;
    return EM_TRUE;
}

static AimlWs* ws_connect_url(const char* url) {
    if (!emscripten_websocket_is_supported()) {
        fprintf(stderr, "[ws] WebSocket API not supported in this environment\n");
        return nullptr;
    }
    EmscriptenWebSocketCreateAttributes attrs = {url, nullptr, EM_TRUE};
    EMSCRIPTEN_WEBSOCKET_T s = emscripten_websocket_new(&attrs);
    if (s <= 0) {
        fprintf(stderr, "[ws] emscripten_websocket_new(%s) failed: %d\n", url, (int)s);
        return nullptr;
    }
    auto* ws = (AimlWs*)calloc(1, sizeof(AimlWs));
    ws->sock = s;
    emscripten_websocket_set_onopen_callback(s, ws, ws_on_open);
    emscripten_websocket_set_onmessage_callback(s, ws, ws_on_message);
    emscripten_websocket_set_onclose_callback(s, ws, ws_on_close);
    emscripten_websocket_set_onerror_callback(s, ws, ws_on_error);
    while (!ws->is_open && !ws->is_closed) emscripten_sleep(5);
    if (ws->is_closed) {
        emscripten_websocket_close(s, 1000, "open-failed");
        emscripten_websocket_delete(s);
        free(ws);
        return nullptr;
    }
    return ws;
}

extern "C" {

void* aiml_ws_connect(const char* host, int32_t port, const char* path) {
    char url[512];
    snprintf(url, sizeof(url), "ws://%s:%d%s",
             host ? host : "localhost", port, path ? path : "/");
    return ws_connect_url(url);
}
void* aiml_wss_connect(const char* host, int32_t port, const char* path) {
    char url[512];
    snprintf(url, sizeof(url), "wss://%s:%d%s",
             host ? host : "localhost", port, path ? path : "/");
    return ws_connect_url(url);
}

void* aiml_ws_server(const char*, int32_t)                                  { errno = ENOTSUP; return nullptr; }
void* aiml_wss_server(const char*, int32_t, const char*, const char*)       { errno = ENOTSUP; return nullptr; }
void* aiml_ws_accept(void*)                                                 { errno = ENOTSUP; return nullptr; }
int32_t aiml_ws_set_timeout(void*, int32_t)                                 { errno = ENOTSUP; return -1; }
const char* aiml_ws_peer_ip(void*)                                          { return ""; }
void aiml_ws_set_client_insecure(int32_t)                                   { /* browser TLS is enforced by the browser */ }
int32_t aiml_ws_client_insecure()                                           { return 0; }

int aiml_ws_send(void* h, const char* text) {
    if (!h || !text) return -1;
    auto* ws = static_cast<AimlWs*>(h);
    EMSCRIPTEN_RESULT r = emscripten_websocket_send_utf8_text(ws->sock, text);
    return (r == EMSCRIPTEN_RESULT_SUCCESS) ? (int)strlen(text) : -1;
}

int aiml_ws_send_raw(void* h, const void* data, int32_t len) {
    if (!h || !data) return -1;
    auto* ws = static_cast<AimlWs*>(h);
    EMSCRIPTEN_RESULT r = emscripten_websocket_send_binary(
        ws->sock, const_cast<void*>(data), static_cast<uint32_t>(len));
    return (r == EMSCRIPTEN_RESULT_SUCCESS) ? len : -1;
}

int aiml_ws_send_binary(void* h, void* buf_ptr) {
    // AIML buffer layout (shared with native, same compiler emits it):
    //   { i32 size, i32 cap, i32 rIdx, i32 wIdx; i8* data }  — data at offset 16.
    // Length sent = wIdx (bytes written), matching the native send path.
    if (!h || !buf_ptr) return -1;
    struct AimlBuf { int32_t size, cap, rIdx, wIdx; char* data; };
    AimlBuf* b = static_cast<AimlBuf*>(buf_ptr);
    if (!b->data || b->wIdx <= 0) return 0;
    return aiml_ws_send_raw(h, b->data, b->wIdx);
}

char* aiml_ws_recv(void* h) {
    if (!h) return nullptr;
    auto* ws = static_cast<AimlWs*>(h);
    while (!ws->rx_head && !ws->is_closed) emscripten_sleep(5);
    WsRxNode* n = rx_pop(ws);
    if (!n) return nullptr;
    char* out = aiml_str_alloc((size_t)n->len);
    memcpy(out, n->data, (size_t)n->len);
    free(n->data);
    free(n);
    return out;
}

void* aiml_ws_recv_raw(void* h, int32_t* out_len) {
    if (!h) { if (out_len) *out_len = 0; return nullptr; }
    auto* ws = static_cast<AimlWs*>(h);
    while (!ws->rx_head && !ws->is_closed) emscripten_sleep(5);
    WsRxNode* n = rx_pop(ws);
    if (!n) { if (out_len) *out_len = 0; return nullptr; }
    void* p = malloc((size_t)n->len);
    memcpy(p, n->data, (size_t)n->len);
    if (out_len) *out_len = n->len;
    free(n->data);
    free(n);
    return p;
}

// Binary receive into a caller-owned AIML buffer (browser client side).
// Mirrors the native ABI; one copy (rx node → dst).  See native impl for docs.
int32_t aiml_ws_recv_binary(void* conn, void* dst_buf) {
    if (!conn || !dst_buf) return -1;
    struct AimlBuf { int32_t size, cap, rIdx, wIdx; char* data; };
    AimlBuf* dst = static_cast<AimlBuf*>(dst_buf);
    if (!dst->data || dst->cap <= 0) return -1;
    int32_t len = 0;
    void* payload = aiml_ws_recv_raw(conn, &len);
    if (!payload) return 0;
    int32_t n = len, rc = len;
    if (n > dst->cap) { n = dst->cap; rc = -2; }
    memcpy(dst->data, payload, (size_t)n);
    free(payload);
    dst->size = n; dst->wIdx = n; dst->rIdx = 0;
    return rc;
}

// Append a received frame at the buffer's write index (browser client side) —
// for reassembling N chunk frames.  Mirrors the native ABI.
int32_t aiml_ws_recv_append(void* conn, void* dst_buf) {
    if (!conn || !dst_buf) return -1;
    struct AimlBuf { int32_t size, cap, rIdx, wIdx; char* data; };
    AimlBuf* dst = static_cast<AimlBuf*>(dst_buf);
    if (!dst->data || dst->cap <= 0) return -1;
    int32_t len = 0;
    void* payload = aiml_ws_recv_raw(conn, &len);
    if (!payload) return 0;
    int32_t pos = dst->wIdx;
    if (pos < 0) pos = 0;
    int32_t n = len, rc = len;
    if (pos + n > dst->cap) { n = dst->cap - pos; rc = -2; }
    if (n > 0) memcpy(dst->data + pos, payload, (size_t)n);
    free(payload);
    dst->wIdx = pos + n;
    dst->size = dst->wIdx;
    return rc;
}

// No mmap'd KV in the browser — the media server (which owns the store) is
// native only.  Present for ABI symmetry; always fails in WASM.
int32_t aiml_ws_send_kv_blob(void*, void*, const char*) { errno = ENOTSUP; return -1; }

int aiml_ws_ping(void*) {
    // Browser WebSocket API does not expose client-initiated ping; runtime handles keepalive.
    return 0;
}

int aiml_ws_close(void* h) {
    if (!h) return -1;
    auto* ws = static_cast<AimlWs*>(h);
    emscripten_websocket_close(ws->sock, 1000, "client-close");
    emscripten_websocket_delete(ws->sock);
    while (WsRxNode* n = rx_pop(ws)) { free(n->data); free(n); }
    free(ws);
    return 0;
}

int32_t aiml_ws_fd(void* h) {
    return h ? static_cast<int32_t>(static_cast<AimlWs*>(h)->sock) : -1;
}

int aiml_ws_poll(void* h, int32_t timeout_ms) {
    if (!h) return -1;
    auto* ws = static_cast<AimlWs*>(h);
    int waited = 0;
    while (!ws->rx_head && !ws->is_closed) {
        if (timeout_ms == 0) return 0;
        if (timeout_ms > 0 && waited >= timeout_ms) return 0;
        emscripten_sleep(5);
        waited += 5;
    }
    if (ws->rx_head) return 1;
    return -1;
}

int aiml_ws_poll_listener(void*, int32_t) { errno = ENOTSUP; return -1; }

} // extern "C"

#else // ─── native (POSIX + OpenSSL) ───────────────────────────────────────────

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>   // X509_CHECK_FLAG_* for strict hostname verification
#include "tls_runtime.h"   // aiml_tls_harden_server_ctx / aiml_tls_require_client_cert (shared policy)

#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <strings.h>    // strncasecmp

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <signal.h>
#include <poll.h>
#include <deque>
#include <string>
#include <vector>
#include <algorithm>

// ══════════════════════════════════════════════════════════════════════════════
// SHA-1  (FIPS 180-4 / RFC 3174)  — no external dependency
// ══════════════════════════════════════════════════════════════════════════════

static inline uint32_t rot32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void sha1_compress(uint32_t h[5], const uint8_t blk[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t(blk[i*4])   << 24) | (uint32_t(blk[i*4+1]) << 16)
             | (uint32_t(blk[i*4+2]) <<  8) |  uint32_t(blk[i*4+3]);
    for (int i = 16; i < 80; i++)
        w[i] = rot32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if      (i < 20) { f = (b & c) | (~b & d);          k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
        uint32_t t = rot32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rot32(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

static void sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
    uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };

    size_t i = 0;
    for (; i + 64 <= len; i += 64) sha1_compress(h, data + i);

    uint8_t blk[64] = {};
    size_t rem = len - i;
    memcpy(blk, data + i, rem);
    blk[rem] = 0x80;
    if (rem >= 56) { sha1_compress(h, blk); memset(blk, 0, 64); }

    uint64_t bits = uint64_t(len) * 8;
    for (int j = 0; j < 8; j++) blk[56 + j] = (bits >> (56 - j * 8)) & 0xFF;
    sha1_compress(h, blk);

    for (int j = 0; j < 5; j++) {
        out[j*4]   = (h[j] >> 24) & 0xFF;
        out[j*4+1] = (h[j] >> 16) & 0xFF;
        out[j*4+2] = (h[j] >>  8) & 0xFF;
        out[j*4+3] =  h[j]        & 0xFF;
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// Base64 encode  — no external dependency
// ══════════════════════════════════════════════════════════════════════════════

static const char B64T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// out must be at least ceil(len/3)*4 + 1 bytes
static void b64_encode(const uint8_t* in, size_t len, char* out) {
    size_t i = 0, j = 0;
    for (; i + 2 < len; i += 3) {
        out[j++] = B64T[(in[i]   >> 2) & 0x3F];
        out[j++] = B64T[((in[i]   & 0x03) << 4) | ((in[i+1] >> 4) & 0x0F)];
        out[j++] = B64T[((in[i+1] & 0x0F) << 2) | ((in[i+2] >> 6) & 0x03)];
        out[j++] = B64T[ in[i+2] & 0x3F];
    }
    if (i < len) {
        out[j++] = B64T[(in[i] >> 2) & 0x3F];
        if (i + 1 < len) {
            out[j++] = B64T[((in[i] & 0x03) << 4) | ((in[i+1] >> 4) & 0x0F)];
            out[j++] = B64T[ (in[i+1] & 0x0F) << 2];
        } else {
            out[j++] = B64T[(in[i] & 0x03) << 4];
            out[j++] = '=';
        }
        out[j++] = '=';
    }
    out[j] = '\0';
}

// ══════════════════════════════════════════════════════════════════════════════
// WebSocket handshake helpers
// ══════════════════════════════════════════════════════════════════════════════

static const char WS_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// Compute Sec-WebSocket-Accept from client's Sec-WebSocket-Key
static void ws_accept_key(const char* client_key, char out[29]) {
    size_t klen = strlen(client_key);
    size_t glen = sizeof(WS_GUID) - 1;
    uint8_t* cat = (uint8_t*)malloc(klen + glen);
    memcpy(cat, client_key, klen);
    memcpy(cat + klen, WS_GUID, glen);
    uint8_t hash[20];
    sha1(cat, klen + glen, hash);
    free(cat);
    b64_encode(hash, 20, out);
}

// Generate a random 16-byte client key, base64-encoded
static void ws_gen_key(char out[25]) {
    uint8_t raw[16];
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) { fread(raw, 1, 16, f); fclose(f); }
    else   { for (int i = 0; i < 16; i++) raw[i] = (uint8_t)(rand() >> 3); }
    b64_encode(raw, 16, out);
}

// Simple PRNG for mask keys (seeded once from time)
static uint32_t ws_next_mask() {
    static std::once_flag flag;
    std::call_once(flag, [] { srand((unsigned)time(nullptr)); });
    return ((uint32_t)rand() << 16) ^ (uint32_t)rand();
}

// ══════════════════════════════════════════════════════════════════════════════
// Internal struct
// ══════════════════════════════════════════════════════════════════════════════

struct WsPending;

struct AimlWs {
    int      fd;
    SSL*     ssl;            // null = plain TCP
    SSL_CTX* ctx;            // server listener holds CTX here; client SSL_CTX freed after connect
    bool     is_listener;    // true = server listen socket
    bool     is_client_side; // true = WS client role (must mask outgoing frames)
    char     ip[46];         // accepted peer's IP (dotted); "" unless set at accept
    int32_t    hs_timeout_ms = 5000;  // listener: per-connection handshake timeout (spec/32_4 §5.1)
    WsPending* pend = nullptr;        // listener: in-flight handshakes + ready clients
};

static AimlWs* to_ws(void* p) { return static_cast<AimlWs*>(p); }

// ══════════════════════════════════════════════════════════════════════════════
// Low-level I/O (unified TCP / TLS)
// ══════════════════════════════════════════════════════════════════════════════

static int ws_read_n(AimlWs* ws, void* buf, int n) {
    int done = 0;
    while (done < n) {
        int r = ws->ssl
            ? SSL_read(ws->ssl, (char*)buf + done, n - done)
            : (int)::recv(ws->fd, (char*)buf + done, n - done, 0);
        if (r <= 0) return -1;
        done += r;
    }
    return done;
}

static int ws_write_n(AimlWs* ws, const void* buf, int n) {
    int done = 0;
    while (done < n) {
        int r = ws->ssl
            ? SSL_write(ws->ssl, (const char*)buf + done, n - done)
            : (int)::send(ws->fd, (const char*)buf + done, n - done, MSG_NOSIGNAL);
        if (r <= 0) return -1;
        done += r;
    }
    return done;
}

// ══════════════════════════════════════════════════════════════════════════════
// HTTP header helpers
// ══════════════════════════════════════════════════════════════════════════════

// Read until \r\n\r\n; returns malloc'd string (caller frees)
static char* read_http_head(AimlWs* ws) {
    char* buf = (char*)malloc(8193);
    int pos = 0;
    while (pos < 8192) {
        char c;
        if (ws_read_n(ws, &c, 1) < 0) { free(buf); return nullptr; }
        buf[pos++] = c;
        if (pos >= 4 && buf[pos-4]=='\r' && buf[pos-3]=='\n'
                     && buf[pos-2]=='\r' && buf[pos-1]=='\n') {
            buf[pos] = '\0';
            return buf;
        }
    }
    free(buf);
    return nullptr;
}

// Find header value (case-insensitive name); returns malloc'd string (caller frees)
static char* hdr_get(const char* hdrs, const char* name) {
    size_t nlen = strlen(name);
    // Skip request/status line
    const char* p = strstr(hdrs, "\r\n");
    if (!p) return nullptr;
    p += 2;
    while (*p && !(*p == '\r' && *(p+1) == '\n' && *(p+2) == '\0')) {
        if (strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
            const char* v = p + nlen + 1;
            while (*v == ' ' || *v == '\t') v++;
            const char* e = strstr(v, "\r\n");
            if (!e) e = v + strlen(v);
            while (e > v && (e[-1] == ' ' || e[-1] == '\t')) e--;
            size_t vl = (size_t)(e - v);
            // Internal helper — caller uses plain free(), so keep raw malloc.
            char* out = (char*)malloc(vl + 1);
            memcpy(out, v, vl);
            out[vl] = '\0';
            return out;
        }
        const char* nl = strstr(p, "\r\n");
        if (!nl) break;
        p = nl + 2;
    }
    return nullptr;
}

// ══════════════════════════════════════════════════════════════════════════════
// HTTP Upgrade handshake
// ══════════════════════════════════════════════════════════════════════════════

static int ws_client_handshake(AimlWs* ws, const char* host, int32_t port, const char* path) {
    char key[25];
    ws_gen_key(key);

    char req[1024];
    snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        path, host, port, key);

    if (ws_write_n(ws, req, (int)strlen(req)) < 0) return -1;

    char* resp = read_http_head(ws);
    if (!resp) return -1;
    bool ok = strncmp(resp, "HTTP/1.1 101", 12) == 0;
    free(resp);
    return ok ? 0 : -1;
}

// ── Asynchronous server handshake (spec/32_4 §5.1) ─────────────────────────────
// The listener drives every incoming connection's TLS handshake + HTTP upgrade
// non-blocking; only fully handshaken clients are handed out by accept.

static const size_t kMaxPendingHandshakes = 64;

static int64_t ws_mono_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

struct WsHs {
    AimlWs*     ws;
    int64_t     deadline;
    int         stage;        // 0 = TLS, 1 = read upgrade request, 2 = write 101
    short       ev = POLLIN;  // readiness the stalled step waits for
    std::string head;
    std::string out;
    size_t      out_off = 0;
};

struct WsPending {
    std::mutex            mtx;
    std::vector<WsHs*>    hs;
    std::deque<AimlWs*>   ready;
};

static void ws_free_conn(AimlWs* ws) {
    if (ws->ssl) SSL_free(ws->ssl);
    ::close(ws->fd);
    delete ws;
}

// Map a non-positive I/O result to "would block" (0, h->ev set) or hard error (-1).
static int hs_blocked(WsHs* h, int r, bool wr) {
    if (h->ws->ssl) {
        int e = SSL_get_error(h->ws->ssl, r);
        if (e == SSL_ERROR_WANT_READ)  { h->ev = POLLIN;  return 0; }
        if (e == SSL_ERROR_WANT_WRITE) { h->ev = POLLOUT; return 0; }
        return -1;
    }
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        h->ev = wr ? POLLOUT : POLLIN;
        return 0;
    }
    return -1;
}

// 1 = handshake complete (fd back to blocking), 0 = in progress, -1 = failed.
static int hs_step(WsHs* h) {
    AimlWs* ws = h->ws;
    if (h->stage == 0) {
        ERR_clear_error();
        int r = SSL_accept(ws->ssl);
        if (r != 1) return hs_blocked(h, r, false);
        h->stage = 1;
    }
    if (h->stage == 1) {
        for (;;) {
            char c;
            if (ws->ssl) ERR_clear_error();
            int r = ws->ssl ? SSL_read(ws->ssl, &c, 1) : (int)::recv(ws->fd, &c, 1, 0);
            if (r <= 0) return hs_blocked(h, r, false);
            h->head.push_back(c);
            if (h->head.size() > 8192) return -1;
            size_t n = h->head.size();
            if (n >= 4 && h->head.compare(n - 4, 4, "\r\n\r\n") == 0) break;
        }
        char* key = hdr_get(h->head.c_str(), "Sec-WebSocket-Key");
        if (!key) return -1;
        char accept[29];
        ws_accept_key(key, accept);
        free(key);
        h->out = std::string("HTTP/1.1 101 Switching Protocols\r\n"
                             "Upgrade: websocket\r\n"
                             "Connection: Upgrade\r\n"
                             "Sec-WebSocket-Accept: ") + accept + "\r\n\r\n";
        h->stage = 2;
    }
    while (h->out_off < h->out.size()) {
        const char* b = h->out.data() + h->out_off;
        int n = (int)(h->out.size() - h->out_off);
        if (ws->ssl) ERR_clear_error();
        int r = ws->ssl ? SSL_write(ws->ssl, b, n) : (int)::send(ws->fd, b, n, MSG_NOSIGNAL);
        if (r <= 0) return hs_blocked(h, r, true);
        h->out_off += r;
    }
    int fl = fcntl(ws->fd, F_GETFL, 0);
    fcntl(ws->fd, F_SETFL, fl & ~O_NONBLOCK);
    return 1;
}

// Accept new connections, advance pending handshakes, drop failed/expired ones.
// Caller holds p->mtx.
static void ws_pump(AimlWs* srv, WsPending* p) {
    for (;;) {
        struct sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        int cfd = ::accept4(srv->fd, (sockaddr*)&addr, &len, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (cfd < 0) break;
        if (p->hs.size() >= kMaxPendingHandshakes) { ::close(cfd); continue; }
        AimlWs* c = new AimlWs{cfd, nullptr, nullptr, false, false, {0}};
        inet_ntop(AF_INET, &addr.sin_addr, c->ip, sizeof(c->ip));
        if (srv->ctx) {
            c->ssl = SSL_new(srv->ctx);
            SSL_set_fd(c->ssl, cfd);
        }
        p->hs.push_back(new WsHs{c, ws_mono_ms() + srv->hs_timeout_ms, srv->ctx ? 0 : 1});
    }
    int64_t now = ws_mono_ms();
    for (size_t i = 0; i < p->hs.size(); ) {
        WsHs* h = p->hs[i];
        int r = hs_step(h);
        if (r == 0 && now < h->deadline) { ++i; continue; }
        if (r == 1) p->ready.push_back(h->ws);
        else        ws_free_conn(h->ws);
        delete h;
        p->hs.erase(p->hs.begin() + i);
    }
}

// 1 = a handshaken client is ready, 0 = timeout, -1 = error. timeout_ms < 0 waits forever.
static int ws_listener_wait(AimlWs* srv, int32_t timeout_ms) {
    if (!srv->is_listener) return -1;
    if (!srv->pend) srv->pend = new WsPending();
    WsPending* p = srv->pend;
    int64_t end = timeout_ms < 0 ? INT64_MAX : ws_mono_ms() + timeout_ms;
    std::vector<struct pollfd> fds;
    for (;;) {
        int64_t wait;
        {
            std::lock_guard<std::mutex> g(p->mtx);
            ws_pump(srv, p);
            if (!p->ready.empty()) return 1;
            int64_t now = ws_mono_ms();
            if (now >= end) return 0;
            wait = end - now;
            fds.assign(1, { srv->fd, POLLIN, 0 });
            for (WsHs* h : p->hs) {
                fds.push_back({ h->ws->fd, h->ev, 0 });
                wait = std::min(wait, std::max<int64_t>(h->deadline - now, 0));
            }
        }
        if (::poll(fds.data(), fds.size(), (int)std::min<int64_t>(wait, INT32_MAX)) < 0 && errno != EINTR)
            return -1;
    }
}

static void ws_pending_free(WsPending* p) {
    for (WsHs* h : p->hs) { ws_free_conn(h->ws); delete h; }
    for (AimlWs* c : p->ready) ws_free_conn(c);
    delete p;
}

// ══════════════════════════════════════════════════════════════════════════════
// RFC 6455 frame codec
// ══════════════════════════════════════════════════════════════════════════════
//
// Frame layout:
//   [0] FIN(1) RSV(3) Opcode(4)
//   [1] MASK(1) Payload-len(7)   [126→next 2B, 127→next 8B]
//   [mask key 4B if MASK=1]
//   [payload]
//
// Opcodes: 0=continuation, 1=text, 2=binary, 8=close, 9=ping, 0xA=pong

static int ws_send_frame(AimlWs* ws, uint8_t opcode,
                         const char* data, size_t len, bool masked) {
    uint8_t hdr[14]; int hl = 0;
    hdr[hl++] = 0x80 | opcode;          // FIN=1

    uint8_t mbit = masked ? 0x80 : 0;
    if      (len < 126)   { hdr[hl++] = mbit | (uint8_t)len; }
    else if (len < 65536) { hdr[hl++] = mbit | 126;
                            hdr[hl++] = (len >> 8) & 0xFF;
                            hdr[hl++] =  len       & 0xFF; }
    else                  { hdr[hl++] = mbit | 127;
                            for (int i = 7; i >= 0; i--)
                                hdr[hl++] = (len >> (i * 8)) & 0xFF; }

    uint8_t mk[4] = {};
    if (masked) {
        uint32_t r = ws_next_mask();
        mk[0] = r; mk[1] = r >> 8; mk[2] = r >> 16; mk[3] = r >> 24;
        hdr[hl++] = mk[0]; hdr[hl++] = mk[1];
        hdr[hl++] = mk[2]; hdr[hl++] = mk[3];
    }

    if (ws_write_n(ws, hdr, hl) < 0) return -1;
    if (len == 0) return 0;

    if (!masked) return ws_write_n(ws, data, (int)len) < 0 ? -1 : 0;

    // Masked: XOR payload in 4 KB chunks, send each chunk immediately
    char chunk[4096];
    size_t sent = 0;
    while (sent < len) {
        size_t batch = len - sent;
        if (batch > sizeof(chunk)) batch = sizeof(chunk);
        for (size_t i = 0; i < batch; i++)
            chunk[i] = data[sent + i] ^ mk[(sent + i) & 3];
        if (ws_write_n(ws, chunk, (int)batch) < 0) return -1;
        sent += batch;
    }
    return 0;
}

// Returns malloc'd payload (caller frees), sets out_opcode and out_len
static char* ws_recv_frame_raw(AimlWs* ws, uint8_t* out_opcode, size_t* out_len) {
    uint8_t h2[2];
    if (ws_read_n(ws, h2, 2) < 0) return nullptr;

    uint8_t opcode  = h2[0] & 0x0F;
    bool    masked  = (h2[1] & 0x80) != 0;
    uint64_t plen   = h2[1] & 0x7F;

    if (plen == 126) {
        uint8_t e[2];
        if (ws_read_n(ws, e, 2) < 0) return nullptr;
        plen = ((uint64_t)e[0] << 8) | e[1];
    } else if (plen == 127) {
        uint8_t e[8];
        if (ws_read_n(ws, e, 8) < 0) return nullptr;
        plen = 0;
        for (int i = 0; i < 8; i++) plen = (plen << 8) | e[i];
    }

    // Hard frame-size ceiling (spec/32_4 §6): reject an attacker-declared length BEFORE any
    // allocation.  Without this, plen is client-controlled up to 2^64-1 → malloc(plen+1) wraps
    // (plen=2^64-1 → malloc(0)), (int)plen truncates to -1 (skipping the short-read guard), and
    // the mask-XOR / payload[plen]='\0' then write out of bounds → unauthenticated remote crash
    // (tests/test_ws_oversized_frame.aiml).  64 MiB is above any legitimate single frame (Mirror
    // RPCs ≤ 8 MiB, media/KV blobs) and well below the overflow/truncation danger (< 2^31).
    static const uint64_t WS_MAX_FRAME = (uint64_t)64 << 20;
    if (plen > WS_MAX_FRAME) return nullptr;

    uint8_t mk[4] = {};
    if (masked && ws_read_n(ws, mk, 4) < 0) return nullptr;

    char* payload = (char*)malloc(plen + 1);
    if (!payload) return nullptr;

    if (plen > 0 && ws_read_n(ws, payload, (int)plen) < 0) {
        free(payload); return nullptr;
    }

    if (masked)
        for (size_t i = 0; i < plen; i++) payload[i] ^= mk[i & 3];

    payload[plen] = '\0';
    *out_opcode = opcode;
    *out_len    = (size_t)plen;
    return payload;
}

// ══════════════════════════════════════════════════════════════════════════════
// TCP + OpenSSL helpers
// ══════════════════════════════════════════════════════════════════════════════

/* Non-blocking TCP connect with millisecond timeout.
 * Returns connected fd (blocking mode restored) or -1 on failure. */
static int tcp_connect(const char* host, int32_t port, int32_t timeout_ms = 5000) {
    char ps[16];
    snprintf(ps, sizeof(ps), "%d", port);
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, ps, &hints, &res) != 0 || !res) return -1;
    int fd = ::socket(res->ai_family, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) { freeaddrinfo(res); return -1; }

    /* Switch to non-blocking so connect() returns immediately */
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    int rc = ::connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    if (rc == 0) {
        /* Immediate success (unlikely but possible for loopback) */
    } else if (errno != EINPROGRESS) {
        ::close(fd); return -1;
    } else {
        /* Wait for writable (connection complete) up to timeout_ms */
        fd_set wfds, efds;
        FD_ZERO(&wfds); FD_SET(fd, &wfds);
        FD_ZERO(&efds); FD_SET(fd, &efds);
        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        if (select(fd + 1, nullptr, &wfds, &efds, &tv) <= 0) {
            ::close(fd); return -1;
        }
        int err = 0; socklen_t elen = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen);
        if (err != 0) { ::close(fd); return -1; }
    }

    /* Restore blocking mode for WS handshake read/write */
    fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    return fd;
}

static std::once_flag g_ws_ssl_once;
static void ws_ssl_init() {
    signal(SIGPIPE, SIG_IGN); // SSL_write has no MSG_NOSIGNAL equivalent
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS |
                     OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
}

// Also ignore SIGPIPE for plain ws:// (early init)
static std::once_flag g_ws_sigpipe_once;
static void ws_ignore_sigpipe() { signal(SIGPIPE, SIG_IGN); }

static int tcp_server_fd(const char* host, int32_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (!host || strcmp(host, "0.0.0.0") == 0)
        addr.sin_addr.s_addr = INADDR_ANY;
    else
        inet_pton(AF_INET, host, &addr.sin_addr);
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0 ||
        listen(fd, 128) < 0) { ::close(fd); return -1; }
    return fd;
}

// ══════════════════════════════════════════════════════════════════════════════
// Public API
// ══════════════════════════════════════════════════════════════════════════════

extern "C" void* aiml_ws_connect(const char* host, int32_t port, const char* path) {
    std::call_once(g_ws_sigpipe_once, ws_ignore_sigpipe);
    int fd = tcp_connect(host, port);
    if (fd < 0) return nullptr;
    AimlWs* ws = new AimlWs{fd, nullptr, nullptr, false, true};
    if (ws_client_handshake(ws, host, port, path ? path : "/") < 0) {
        ::close(fd); delete ws; return nullptr;
    }
    return ws;
}

// wss client certificate-verification policy. Default: verify (secure). Seeded
// from AIML_WSS_INSECURE on first use; mirror.tls_insecure(on) overrides at runtime.
// -1 = not yet seeded, 0 = verify, 1 = insecure (dev only).
static int g_wss_insecure = -1;
static bool ws_client_insecure() {
    if (g_wss_insecure < 0) {
        const char* e = getenv("AIML_WSS_INSECURE");
        g_wss_insecure = (e && e[0] == '1') ? 1 : 0;
    }
    return g_wss_insecure != 0;
}
extern "C" void aiml_ws_set_client_insecure(int32_t on) { g_wss_insecure = on ? 1 : 0; }
// Shared "insecure client TLS" flag — also consumed by the http client so a single
// mirror.tls_insecure(1) / AIML_WSS_INSECURE=1 relaxes verification for both.
extern "C" int32_t aiml_ws_client_insecure() { return ws_client_insecure() ? 1 : 0; }

extern "C" void* aiml_wss_connect(const char* host, int32_t port, const char* path) {
    std::call_once(g_ws_ssl_once, ws_ssl_init);
    int fd = tcp_connect(host, port);
    if (fd < 0) return nullptr;

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_default_verify_paths(ctx);
    // Optional private/self-signed CA bundle (e.g. a dev or internal CA).
    const char* wss_ca = getenv("AIML_WSS_CA");
    if (wss_ca && *wss_ca) SSL_CTX_load_verify_locations(ctx, wss_ca, nullptr);
    // Verify the server certificate by default (chain + hostname via SSL_set1_host
    // below) → defeats active MITM.  Disable only for local dev with a self-signed
    // cert — via mirror.tls_insecure(1) or AIML_WSS_INSECURE=1 — with a loud warning.
    bool insecure = ws_client_insecure();
    if (insecure) {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
        fprintf(stderr, "[wss] WARNING: TLS certificate verification DISABLED (AIML_WSS_INSECURE=1)\n");
    } else {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
    }
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, host);
    // Enforce hostname match against the cert (SAN/CN) under SSL_VERIFY_PEER.
    if (!insecure) {
        SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        SSL_set1_host(ssl, host);
    }

    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl); SSL_CTX_free(ctx); ::close(fd); return nullptr;
    }
    // ctx is no longer needed after connect; free it to save memory
    SSL_CTX_free(ctx);

    AimlWs* ws = new AimlWs{fd, ssl, nullptr, false, true};
    if (ws_client_handshake(ws, host, port, path ? path : "/") < 0) {
        SSL_free(ssl); ::close(fd); delete ws; return nullptr;
    }
    return ws;
}

extern "C" void* aiml_ws_server(const char* host, int32_t port) {
    std::call_once(g_ws_sigpipe_once, ws_ignore_sigpipe);
    int fd = tcp_server_fd(host, port);
    if (fd < 0) return nullptr;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    return new AimlWs{fd, nullptr, nullptr, true, false};
}

extern "C" void* aiml_wss_server(const char* host, int32_t port,
                                  const char* cert, const char* key) {
    std::call_once(g_ws_ssl_once, ws_ssl_init);
    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) return nullptr;
    // Same hardened server defaults as the HTTPS server (shared policy in
    // tls_runtime.cpp): min TLS 1.2, no SSLv2/3/TLS1.0/1.1, no compression.
    aiml_tls_harden_server_ctx(ctx);
    // Fail loudly if the cert/key cannot be loaded — otherwise the listener
    // would come up with no usable certificate and every handshake would fail.
    if (SSL_CTX_use_certificate_chain_file(ctx, cert) <= 0 ||
        SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) <= 0) {
        SSL_CTX_free(ctx); return nullptr;
    }

    int fd = tcp_server_fd(host, port);
    if (fd < 0) { SSL_CTX_free(ctx); return nullptr; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    // ctx stored in listener socket; used when accepting clients
    return new AimlWs{fd, nullptr, ctx, true, false};
}

extern "C" void* aiml_ws_accept(void* srv_ptr) {
    if (!srv_ptr) return nullptr;
    AimlWs* srv = to_ws(srv_ptr);
    if (ws_listener_wait(srv, -1) != 1) return nullptr;
    std::lock_guard<std::mutex> g(srv->pend->mtx);
    AimlWs* c = srv->pend->ready.front();
    srv->pend->ready.pop_front();
    return c;
}

extern "C" int32_t aiml_ws_set_timeout(void* p, int32_t ms) {
    if (!p) return -1;
    AimlWs* ws = to_ws(p);
    if (ws->is_listener) {
        ws->hs_timeout_ms = ms > 0 ? ms : 5000;
        return 0;
    }
    struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    if (ms < 0) tv = { 0, 0 };
    if (setsockopt(ws->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) return -1;
    if (setsockopt(ws->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) return -1;
    return 0;
}

// Peer IP (dotted) of a client returned by aiml_ws_accept; "" if unknown.
extern "C" const char* aiml_ws_peer_ip(void* p) {
    if (!p) return "";
    AimlWs* ws = to_ws(p);
    return ws->ip;
}

extern "C" int aiml_ws_send(void* p, const char* text) {
    if (!p || !text) return -1;
    AimlWs* ws = to_ws(p);
    return ws_send_frame(ws, 0x1, text, strlen(text), ws->is_client_side);
}

extern "C" int aiml_ws_send_binary(void* p, void* buf_ptr) {
    if (!p || !buf_ptr) return -1;
    struct AimlBuf { int32_t size, cap, rIdx, wIdx; char* data; };
    AimlWs*  ws  = to_ws(p);
    AimlBuf* buf = (AimlBuf*)buf_ptr;
    if (!buf->data || buf->wIdx <= 0) return 0;
    return ws_send_frame(ws, 0x2, buf->data, (size_t)buf->wIdx, ws->is_client_side);
}

extern "C" char* aiml_ws_recv(void* p) {
    if (!p) return nullptr;
    AimlWs* ws = to_ws(p);
    while (true) {
        uint8_t opcode; size_t len;
        char* payload = ws_recv_frame_raw(ws, &opcode, &len);
        if (!payload) return nullptr;
        switch (opcode) {
            case 0x0:  // continuation
            case 0x1:  // text
            case 0x2: {  // binary — refcounted AIML string, like the wasm path
                char* out = aiml_str_alloc(len);
                memcpy(out, payload, len);
                free(payload);
                return out;
            }
            case 0x8:  // close — echo close frame, return null
                ws_send_frame(ws, 0x8, "", 0, ws->is_client_side);
                free(payload); return nullptr;
            case 0x9:  // ping → auto-pong
                ws_send_frame(ws, 0xA, payload, len, ws->is_client_side);
                free(payload); continue;
            case 0xA:  // pong — discard
                free(payload); continue;
            default:
                free(payload); return nullptr;
        }
    }
}

// ── Raw binary I/O (for mirror transport) ────────────────────────────────────

extern "C" int aiml_ws_send_raw(void* p, const void* data, int32_t len) {
    if (!p || !data || len <= 0) return -1;
    AimlWs* ws = to_ws(p);
    return ws_send_frame(ws, 0x2, (const char*)data, (size_t)len, ws->is_client_side);
}

extern "C" void* aiml_ws_recv_raw(void* p, int32_t* out_len) {
    if (!p) return nullptr;
    if (out_len) *out_len = 0;
    AimlWs* ws = to_ws(p);
    while (true) {
        uint8_t opcode; size_t len;
        char* payload = ws_recv_frame_raw(ws, &opcode, &len);
        if (!payload) return nullptr;
        switch (opcode) {
            case 0x0:  // continuation
            case 0x1:  // text
            case 0x2:  // binary
                if (out_len) *out_len = (int32_t)len;
                return payload;
            case 0x8:  // close
                ws_send_frame(ws, 0x8, "", 0, ws->is_client_side);
                free(payload); return nullptr;
            case 0x9:  // ping → auto-pong
                ws_send_frame(ws, 0xA, payload, len, ws->is_client_side);
                free(payload); continue;
            case 0xA:  // pong — discard
                free(payload); continue;
            default:
                free(payload); return nullptr;
        }
    }
}

// ── Binary receive into a caller-owned AIML buffer ───────────────────────────
// dst is an AIML buffer { i32 size, cap, rIdx, wIdx; i8* data }.  Copies the
// next data frame into dst->data (bounded by dst->cap), sets size/wIdx = len and
// rIdx = 0 so a following seek(0)+read works.  Returns bytes received, 0 on a
// clean close, -1 on error, -2 if the frame exceeded dst capacity (dst is filled
// to cap, the remainder dropped).  One copy (frame → dst); no whole-payload
// juggling — callers stream fixed-size chunks through a reused buffer.
extern "C" int32_t aiml_ws_recv_binary(void* conn, void* dst_buf) {
    if (!conn || !dst_buf) return -1;
    struct AimlBuf { int32_t size, cap, rIdx, wIdx; char* data; };
    AimlBuf* dst = static_cast<AimlBuf*>(dst_buf);
    if (!dst->data || dst->cap <= 0) return -1;
    int32_t len = 0;
    void* payload = aiml_ws_recv_raw(conn, &len);
    if (!payload) return 0;                       // close / error
    int32_t n = len, rc = len;
    if (n > dst->cap) { n = dst->cap; rc = -2; }
    memcpy(dst->data, payload, (size_t)n);
    free(payload);
    dst->size = n; dst->wIdx = n; dst->rIdx = 0;
    return rc;
}

// ── Binary receive APPENDED at the buffer's write index ──────────────────────
// Like aiml_ws_recv_binary but copies the frame at dst->wIdx (not offset 0) and
// advances wIdx/size — so a client can reassemble N chunk frames into one buffer
// (alloc once, recv_append N times).  Returns bytes received, 0 on close, -1 err,
// -2 if the frame would overflow (appended up to capacity, remainder dropped).
extern "C" int32_t aiml_ws_recv_append(void* conn, void* dst_buf) {
    if (!conn || !dst_buf) return -1;
    struct AimlBuf { int32_t size, cap, rIdx, wIdx; char* data; };
    AimlBuf* dst = static_cast<AimlBuf*>(dst_buf);
    if (!dst->data || dst->cap <= 0) return -1;
    int32_t len = 0;
    void* payload = aiml_ws_recv_raw(conn, &len);
    if (!payload) return 0;
    int32_t pos = dst->wIdx;
    if (pos < 0) pos = 0;
    int32_t n = len, rc = len;
    if (pos + n > dst->cap) { n = dst->cap - pos; rc = -2; }
    if (n > 0) memcpy(dst->data + pos, payload, (size_t)n);
    free(payload);
    dst->wIdx = pos + n;
    dst->size = dst->wIdx;
    return rc;
}

// ── Zero-copy send of a KV blob straight from the mmap'd WAL to the socket ────
// No intermediate AIML buffer: resolves the blob's pointer in the mmap region
// and streams it as one binary frame (mmap page → socket, no userspace copy).
// Returns bytes sent, 0 if the key is absent/empty, -1 on error.
extern "C" void* aiml_db_kv_get_blob(void* kv_ptr, const char* key, int32_t* out_size);
extern "C" int32_t aiml_ws_send_kv_blob(void* conn, void* kv, const char* key) {
    if (!conn || !kv || !key) return -1;
    int32_t sz = 0;
    void* ptr = aiml_db_kv_get_blob(kv, key, &sz);
    if (!ptr || sz <= 0) return 0;
    return aiml_ws_send_raw(conn, ptr, sz);
}

extern "C" int aiml_ws_ping(void* p) {
    if (!p) return -1;
    AimlWs* ws = to_ws(p);
    return ws_send_frame(ws, 0x9, "", 0, ws->is_client_side);
}

extern "C" int aiml_ws_close(void* p) {
    if (!p) return -1;
    AimlWs* ws = to_ws(p);
    if (!ws->is_listener)
        ws_send_frame(ws, 0x8, "\x03\xE8", 2, ws->is_client_side); // close code 1000
    if (ws->pend) ws_pending_free(ws->pend);
    if (ws->ssl) { SSL_shutdown(ws->ssl); SSL_free(ws->ssl); }
    if (ws->ctx) SSL_CTX_free(ws->ctx);
    ::close(ws->fd);
    delete ws;
    return 0;
}

extern "C" int32_t aiml_ws_fd(void* p) {
    return p ? to_ws(p)->fd : -1;
}

extern "C" int aiml_ws_poll(void* p, int32_t timeout_ms) {
    if (!p) return -1;
    AimlWs* ws = to_ws(p);
    // Check TLS internal buffer first (data already decrypted but not yet read)
    if (ws->ssl && SSL_pending(ws->ssl) > 0) return 1;
    struct pollfd pfd = { ws->fd, POLLIN, 0 };
    int r = ::poll(&pfd, 1, timeout_ms);
    return r > 0 ? 1 : (r == 0 ? 0 : -1);
}

/* Poll a WS listener socket for a pending accept()-able connection.
 * Returns 1 if a client is waiting, 0 if timeout, -1 on error. */
extern "C" int aiml_ws_poll_listener(void* srv_ptr, int32_t timeout_ms) {
    if (!srv_ptr) return -1;
    return ws_listener_wait(to_ws(srv_ptr), timeout_ms);
}

#endif // __EMSCRIPTEN__
