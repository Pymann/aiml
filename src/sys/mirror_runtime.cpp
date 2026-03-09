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
};

struct AimlMirrorEvent {
    const char*     type   = nullptr;
    AimlMirrorPeer* peer   = nullptr;
    int32_t         net_id = 0;
};

struct AimlRpcEvent {
    const char* method = nullptr;  /* heap-allocated, freed on session close */
    int32_t     net_id = 0;
    int32_t     arg    = 0;
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
    else if (u.substr(0, 6) == "ws://")  { r.type = MT_WS;  u = u.substr(5); }
    else if (u.substr(0, 7) == "wss://") { r.type = MT_WSS; u = u.substr(6); }
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
    }
    /* WS transport: v2 (requires AimlBuffer wrapper) */
}

/* Returns bytes received into `buf` (max `max_len`); 0 = no data yet */
static int mirror_recv_available(AimlMirrorPeer* peer, void* buf, int max_len) {
    if (!peer) return 0;
    if (peer->transport.type == MT_TCP) {
        int fd = peer->transport.tcp_fd;
        if (!tcp_readable(fd)) return 0;
        ssize_t n = recv(fd, buf, (size_t)max_len, 0);
        return (n > 0) ? (int)n : -1;   /* -1 = disconnected */
    }
    /* WS transport: v2 */
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

static void send_delta_pkt(AimlMirrorSession* s, AimlMirrorRegistered* reg, AimlMirrorPeer* exclude) {
    size_t total = 6 + (size_t)reg->obj_size;
    std::vector<uint8_t> pkt(total);
    write_u32_le(pkt.data(), (uint32_t)reg->net_id);
    pkt[4] = PKT_DELTA; pkt[5] = 0;
    memcpy(pkt.data() + 6, reg->obj_ptr, (size_t)reg->obj_size);
    broadcast_pkt(s, pkt.data(), total, exclude);
    /* update shadow */
    memcpy(reg->shadow, reg->obj_ptr, (size_t)reg->obj_size);
}

/* ── Packet processing ────────────────────────────────────────────────────── */

static void process_packet(AimlMirrorSession* s, AimlMirrorPeer* from_peer,
                            const uint8_t* data, int data_len) {
    if (data_len < 6) return;
    int32_t net_id = read_i32_le(data);
    uint8_t  type  = data[4];

    if (type == PKT_DELTA) {
        auto it = s->by_net_id.find(net_id);
        if (it != s->by_net_id.end()) {
            AimlMirrorRegistered* reg = it->second;
            int payload_len = data_len - 6;
            int copy_len = (reg->obj_size > 0 && payload_len > 0)
                ? (payload_len < (int)reg->obj_size ? payload_len : (int)reg->obj_size)
                : 0;
            if (!reg->is_owner && reg->obj_ptr && copy_len > 0) {
                /* Normal path: apply delta to live object */
                memcpy(reg->obj_ptr, data + 6, (size_t)copy_len);
                if (reg->shadow) memcpy(reg->shadow, reg->obj_ptr, (size_t)reg->obj_size);
            } else if (!reg->is_owner && !reg->obj_ptr && copy_len > 0 && reg->shadow) {
                /* register_id not yet called: cache delta in shadow so it can
                 * be applied to obj_ptr when register_id is eventually called. */
                memcpy(reg->shadow, data + 6, (size_t)copy_len);
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
                free(reg->shadow);
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
        /* payload: [method_len:u8][method:N][arg:i32le] */
        if (data_len < 8) return;  /* 6 hdr + 1 len + 0 method + 4 arg minimum (empty method) */
        uint8_t method_len = data[6];
        if (data_len < 7 + (int)method_len + 4) return;
        char* method = (char*)malloc((size_t)method_len + 1);
        if (!method) return;
        memcpy(method, data + 7, method_len);
        method[method_len] = '\0';
        int32_t arg = read_i32_le(data + 7 + method_len);
        auto* ev = new AimlRpcEvent();
        ev->method = method;
        ev->net_id = net_id;
        ev->arg    = arg;
        s->rpc_queue.push(ev);
        /* Host: forward to other peers (multi-host relay, unlikely in v1) */
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
                /* Also send current delta */
                uint8_t hdr[6]; write_u32_le(hdr, (uint32_t)reg->net_id);
                hdr[4] = PKT_DELTA; hdr[5] = 0;
                mirror_send(peer, hdr, 6);
                mirror_send(peer, reg->obj_ptr, (size_t)reg->obj_size);
            }
        }

        auto* ev = new AimlMirrorEvent();
        ev->type   = "peer_connected";
        ev->peer   = peer;
        ev->net_id = 0;
        s->event_queue.push(ev);
    }
    /* WS accept handled differently — aiml_ws_accept returns i8* */
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
                size_t  payload_len = 0;
                if (pkt_type == PKT_DELTA) {
                    /* Need obj_size — look up by net_id */
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
                /* WS: recv returns one frame at a time */
                int ws_fd = peer->transport.ws_ptr ? aiml_ws_fd(peer->transport.ws_ptr) : -1;
                if (ws_fd < 0 || !tcp_readable(ws_fd)) break;
                const char* msg = aiml_ws_recv(peer->transport.ws_ptr);
                if (!msg) { disconnected.push_back(peer); break; }
                process_packet(s, peer, (const uint8_t*)msg, (int)strlen(msg));
                break;  /* WS delivers one frame per recv call */
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
        /* WS/WSS connect: v2 — aiml_ws_connect requires (host, port, path) separately */
        fprintf(stderr, "[mirror] WS transport not yet supported in v1; use tcp://\n");
        delete peer; delete s; return nullptr;
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
        free(reg->shadow);
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

    /* 1. Accept new TCP peers (host only) */
    mirror_try_accept(s);

    /* 2. Receive and process incoming packets */
    mirror_recv_from_peers(s);

    /* 3. Send dirty updates for owned objects */
    for (auto& [id, reg] : s->by_net_id) {
        if (!reg->is_owner || reg->obj_ptr == nullptr || reg->obj_size == 0) continue;
        if (reg->manual_sync) continue;
        if (memcmp(reg->obj_ptr, reg->shadow, (size_t)reg->obj_size) != 0) {
            send_delta_pkt(s, reg, nullptr);
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
    free(reg->shadow);
    delete reg;
}

/* ── Event queue ──────────────────────────────────────────────────────────── */

int32_t aiml_mirror_session_has_event(void* session) {
    auto* s = (AimlMirrorSession*)session;
    if (!s) return 0;
    return s->event_queue.empty() ? 0 : 1;
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

void aiml_mirror_session_send_rpc(void* session, int32_t net_id,
                                  const char* method, int32_t arg) {
    auto* s = (AimlMirrorSession*)session;
    if (!s || !method) return;
    uint8_t method_len = (uint8_t)(strlen(method) & 0xFF);
    size_t pkt_size = 6 + 1 + (size_t)method_len + 4;
    std::vector<uint8_t> pkt(pkt_size);
    write_u32_le(pkt.data(), (uint32_t)net_id);
    pkt[4] = PKT_RPC; pkt[5] = 0;
    pkt[6] = method_len;
    memcpy(pkt.data() + 7, method, method_len);
    write_i32_le(pkt.data() + 7 + method_len, arg);
    for (auto* peer : s->peers)
        mirror_send(peer, pkt.data(), pkt_size);
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
