#include "net_runtime.h"
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// ── Internal structs ─────────────────────────────────────────────────────────

struct AimlSocket {
    int fd;
    int timeout_ms; // 0 = blocking; >0 = connect/recv/send timeout in ms
};

// Must match buffer_impl.h field order: size, capacity, readIndex, writeIndex, data
struct AimlBuffer {
    int32_t size;
    int32_t capacity;
    int32_t readIndex;
    int32_t writeIndex;
    char*   data;
};

// ── Helpers ───────────────────────────────────────────────────────────────────

static AimlSocket* to_sock(void* p) { return static_cast<AimlSocket*>(p); }

// send_all: keeps sending until all bytes are written or error
static int send_all(int fd, const char* buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n = static_cast<int>(::send(fd, buf + sent, static_cast<size_t>(len - sent), MSG_NOSIGNAL));
        if (n <= 0) return -1;
        sent += n;
    }
    return sent;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

extern "C" void* aiml_net_socket(const char* proto) {
    int type = (proto && std::strcmp(proto, "udp") == 0) ? SOCK_DGRAM : SOCK_STREAM;
    int fd = ::socket(AF_INET, type, 0);
    if (fd < 0) return nullptr;
    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    return new AimlSocket{fd, 0};
}

extern "C" int aiml_net_close(void* sock) {
    if (!sock) return -1;
    AimlSocket* s = to_sock(sock);
    int rc = ::close(s->fd);
    delete s;
    return rc;
}

extern "C" int32_t aiml_net_fd(void* sock) {
    return sock ? to_sock(sock)->fd : -1;
}

// ── Client ────────────────────────────────────────────────────────────────────

extern "C" int aiml_net_connect(void* sock, const char* host, int32_t port) {
    if (!sock || !host) return -1;
    AimlSocket* s = to_sock(sock);
    int fd = s->fd;

    char port_str[16];
    std::snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    if (::getaddrinfo(host, port_str, &hints, &res) != 0 || !res) return -1;

    if (s->timeout_ms <= 0) {
        // Blocking connect
        int ret = ::connect(fd, res->ai_addr, res->ai_addrlen);
        ::freeaddrinfo(res);
        return ret;
    }

    // Non-blocking connect + poll (for timeout support)
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = ::connect(fd, res->ai_addr, res->ai_addrlen);
    ::freeaddrinfo(res);

    if (rc == 0) {
        ::fcntl(fd, F_SETFL, flags); // Connected immediately — restore blocking
        return 0;
    }
    if (errno != EINPROGRESS) {
        ::fcntl(fd, F_SETFL, flags);
        return -1;
    }

    // Wait until writable or timeout
    struct pollfd pfd{ fd, POLLOUT, 0 };
    int ready = ::poll(&pfd, 1, s->timeout_ms);
    ::fcntl(fd, F_SETFL, flags); // Restore blocking regardless of outcome

    if (ready <= 0) return -1; // 0 = timeout, <0 = error

    // Check if the connection actually succeeded
    int err = 0;
    socklen_t len = sizeof(err);
    ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
    return (err == 0) ? 0 : -1;
}

extern "C" int aiml_net_set_timeout(void* sock, int32_t timeout_ms) {
    if (!sock) return -1;
    AimlSocket* s = to_sock(sock);
    s->timeout_ms = timeout_ms;

    // Apply recv/send timeouts via setsockopt (connect timeout handled in aiml_net_connect)
    struct timeval tv{};
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(s->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(s->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return 0;
}

// ── Server ────────────────────────────────────────────────────────────────────

extern "C" int aiml_net_bind(void* sock, const char* host, int32_t port) {
    if (!sock) return -1;
    int fd = to_sock(sock)->fd;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port));

    if (!host || std::strcmp(host, "0.0.0.0") == 0 || host[0] == '\0') {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (::inet_pton(AF_INET, host, &addr.sin_addr) <= 0) return -1;
    }

    return ::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
}

extern "C" int aiml_net_listen(void* sock, int32_t backlog) {
    if (!sock) return -1;
    return ::listen(to_sock(sock)->fd, backlog > 0 ? backlog : 128);
}

extern "C" void* aiml_net_accept(void* sock) {
    if (!sock) return nullptr;
    struct sockaddr_in client_addr{};
    socklen_t len = sizeof(client_addr);
    int client_fd = ::accept(to_sock(sock)->fd,
                             reinterpret_cast<struct sockaddr*>(&client_addr), &len);
    if (client_fd < 0) return nullptr;
    return new AimlSocket{client_fd};
}

// ── Buffer I/O ────────────────────────────────────────────────────────────────

extern "C" int aiml_net_send(void* sock, void* buf_ptr) {
    if (!sock || !buf_ptr) return -1;
    AimlBuffer* buf = static_cast<AimlBuffer*>(buf_ptr);
    if (!buf->data || buf->writeIndex <= 0) return 0;
    return send_all(to_sock(sock)->fd, buf->data, buf->writeIndex);
}

extern "C" int aiml_net_recv(void* sock, void* buf_ptr) {
    if (!sock || !buf_ptr) return -1;
    AimlBuffer* buf = static_cast<AimlBuffer*>(buf_ptr);
    if (!buf->data || buf->capacity <= 0) return -1;
    int n = static_cast<int>(::recv(to_sock(sock)->fd, buf->data,
                                    static_cast<size_t>(buf->capacity), 0));
    if (n < 0) return -1;
    buf->size       = n;
    buf->writeIndex = n;
    return n;
}

// ── String I/O ────────────────────────────────────────────────────────────────

extern "C" int aiml_net_send_string(void* sock, const char* str) {
    if (!sock || !str) return -1;
    int len = static_cast<int>(std::strlen(str));
    if (len == 0) return 0;
    return send_all(to_sock(sock)->fd, str, len);
}

extern "C" char* aiml_net_recv_string(void* sock, int32_t max_bytes) {
    if (!sock || max_bytes <= 0) return nullptr;
    char* buf = static_cast<char*>(std::malloc(static_cast<size_t>(max_bytes) + 1));
    if (!buf) return nullptr;
    int n = static_cast<int>(::recv(to_sock(sock)->fd, buf,
                                    static_cast<size_t>(max_bytes), 0));
    if (n < 0) { std::free(buf); return nullptr; }
    buf[n] = '\0';
    return buf;
}
