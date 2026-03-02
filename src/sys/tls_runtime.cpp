#include "tls_runtime.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

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
#include <mutex>

// ── OpenSSL one-time init ─────────────────────────────────────────────────────

static std::once_flag g_ssl_init;

static void ssl_global_init() {
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS |
                     OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
}

// ── Internal struct ───────────────────────────────────────────────────────────

struct AimlTlsSocket {
    SSL_CTX* ctx;        // Owned; holds certs, verify mode, CA
    SSL*     ssl;        // nullptr until connect/accept succeeds
    int      fd;         // TCP socket FD (-1 = closed)
    int      timeout_ms; // 0 = blocking
};

// Must match buffer_impl.h layout
struct AimlBuffer {
    int32_t size;
    int32_t capacity;
    int32_t readIndex;
    int32_t writeIndex;
    char*   data;
};

static AimlTlsSocket* to_tls(void* p) { return static_cast<AimlTlsSocket*>(p); }

// ── Lifecycle ─────────────────────────────────────────────────────────────────

extern "C" void* aiml_tls_socket(void) {
    std::call_once(g_ssl_init, ssl_global_init);

    SSL_CTX* ctx = SSL_CTX_new(TLS_method());
    if (!ctx) return nullptr;

    // Default: verify peer (server cert)
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
    // Load system CA store
    SSL_CTX_set_default_verify_paths(ctx);

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        SSL_CTX_free(ctx);
        return nullptr;
    }
    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    return new AimlTlsSocket{ctx, nullptr, fd, 0};
}

extern "C" int aiml_tls_close(void* sock) {
    if (!sock) return -1;
    AimlTlsSocket* s = to_tls(sock);
    if (s->ssl) {
        SSL_shutdown(s->ssl);
        SSL_free(s->ssl);
        s->ssl = nullptr;
    }
    if (s->ctx) {
        SSL_CTX_free(s->ctx);
        s->ctx = nullptr;
    }
    int rc = 0;
    if (s->fd >= 0) {
        rc = ::close(s->fd);
        s->fd = -1;
    }
    delete s;
    return rc;
}

extern "C" int32_t aiml_tls_fd(void* sock) {
    return sock ? to_tls(sock)->fd : -1;
}

// ── Config ────────────────────────────────────────────────────────────────────

extern "C" int aiml_tls_set_cert(void* sock, const char* cert_path, const char* key_path) {
    if (!sock || !cert_path || !key_path) return -1;
    AimlTlsSocket* s = to_tls(sock);
    if (SSL_CTX_use_certificate_file(s->ctx, cert_path, SSL_FILETYPE_PEM) != 1) return -1;
    if (SSL_CTX_use_PrivateKey_file(s->ctx, key_path, SSL_FILETYPE_PEM) != 1) return -1;
    return SSL_CTX_check_private_key(s->ctx) == 1 ? 0 : -1;
}

extern "C" int aiml_tls_set_ca(void* sock, const char* ca_path) {
    if (!sock || !ca_path) return -1;
    AimlTlsSocket* s = to_tls(sock);
    return SSL_CTX_load_verify_locations(s->ctx, ca_path, nullptr) == 1 ? 0 : -1;
}

extern "C" int aiml_tls_set_verify(void* sock, const char* mode) {
    if (!sock || !mode) return -1;
    AimlTlsSocket* s = to_tls(sock);
    int flags = SSL_VERIFY_NONE;
    if (std::strcmp(mode, "peer") == 0) {
        flags = SSL_VERIFY_PEER;
    } else if (std::strcmp(mode, "require") == 0) {
        flags = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
    }
    SSL_CTX_set_verify(s->ctx, flags, nullptr);
    return 0;
}

extern "C" int aiml_tls_set_timeout(void* sock, int32_t timeout_ms) {
    if (!sock) return -1;
    AimlTlsSocket* s = to_tls(sock);
    s->timeout_ms = timeout_ms;
    if (timeout_ms > 0) {
        struct timeval tv{};
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ::setsockopt(s->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(s->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }
    return 0;
}

// ── Client ────────────────────────────────────────────────────────────────────

extern "C" int aiml_tls_connect(void* sock, const char* host, int32_t port) {
    if (!sock || !host) return -1;
    AimlTlsSocket* s = to_tls(sock);
    int fd = s->fd;

    // ── TCP connect ────────────────────────────────────────────────────────
    char port_str[16];
    std::snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (::getaddrinfo(host, port_str, &hints, &res) != 0 || !res) return -1;

    int tcp_rc;
    if (s->timeout_ms <= 0) {
        tcp_rc = ::connect(fd, res->ai_addr, res->ai_addrlen);
    } else {
        int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        tcp_rc = ::connect(fd, res->ai_addr, res->ai_addrlen);
        if (tcp_rc != 0 && errno == EINPROGRESS) {
            struct pollfd pfd{ fd, POLLOUT, 0 };
            int ready = ::poll(&pfd, 1, s->timeout_ms);
            if (ready > 0) {
                int err = 0; socklen_t len = sizeof(err);
                ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                tcp_rc = (err == 0) ? 0 : -1;
            } else {
                tcp_rc = -1;
            }
        }
        ::fcntl(fd, F_SETFL, flags);
    }
    ::freeaddrinfo(res);
    if (tcp_rc != 0) return -1;

    // ── TLS handshake ──────────────────────────────────────────────────────
    SSL* ssl = SSL_new(s->ctx);
    if (!ssl) return -1;

    SSL_set_fd(ssl, fd);
    // SNI
    SSL_set_tlsext_host_name(ssl, host);
    // Hostname verification
    SSL_set1_host(ssl, host);

    if (SSL_connect(ssl) != 1) {
        SSL_free(ssl);
        return -1;
    }
    s->ssl = ssl;
    return 0;
}

// ── Server ────────────────────────────────────────────────────────────────────

extern "C" int aiml_tls_bind(void* sock, const char* host, int32_t port) {
    if (!sock) return -1;
    int fd = to_tls(sock)->fd;

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

extern "C" int aiml_tls_listen(void* sock, int32_t backlog) {
    if (!sock) return -1;
    return ::listen(to_tls(sock)->fd, backlog > 0 ? backlog : 128);
}

extern "C" void* aiml_tls_accept(void* sock) {
    if (!sock) return nullptr;
    AimlTlsSocket* srv = to_tls(sock);

    struct sockaddr_in client_addr{};
    socklen_t len = sizeof(client_addr);
    int client_fd = ::accept(srv->fd,
                             reinterpret_cast<struct sockaddr*>(&client_addr), &len);
    if (client_fd < 0) return nullptr;

    // ── TLS handshake on accepted connection ───────────────────────────────
    // New SSL object sharing the server's CTX (inherits cert/verify settings)
    SSL* ssl = SSL_new(srv->ctx);
    if (!ssl) { ::close(client_fd); return nullptr; }

    SSL_set_fd(ssl, client_fd);
    if (SSL_accept(ssl) != 1) {
        SSL_free(ssl);
        ::close(client_fd);
        return nullptr;
    }

    // New AimlTlsSocket: create a fresh CTX for future config (not needed for
    // an already-accepted socket, but keeps the struct consistent)
    SSL_CTX* new_ctx = SSL_CTX_new(TLS_method());
    return new AimlTlsSocket{new_ctx, ssl, client_fd, 0};
}

// ── I/O ───────────────────────────────────────────────────────────────────────

extern "C" int aiml_tls_send(void* sock, void* buf_ptr) {
    if (!sock || !buf_ptr) return -1;
    AimlTlsSocket* s = to_tls(sock);
    if (!s->ssl) return -1;
    AimlBuffer* buf = static_cast<AimlBuffer*>(buf_ptr);
    if (!buf->data || buf->writeIndex <= 0) return 0;
    int total = 0, remaining = buf->writeIndex;
    const char* ptr = buf->data;
    while (remaining > 0) {
        int n = SSL_write(s->ssl, ptr, remaining);
        if (n <= 0) return -1;
        ptr       += n;
        total     += n;
        remaining -= n;
    }
    return total;
}

extern "C" int aiml_tls_recv(void* sock, void* buf_ptr) {
    if (!sock || !buf_ptr) return -1;
    AimlTlsSocket* s = to_tls(sock);
    if (!s->ssl) return -1;
    AimlBuffer* buf = static_cast<AimlBuffer*>(buf_ptr);
    if (!buf->data || buf->capacity <= 0) return -1;
    int n = SSL_read(s->ssl, buf->data, buf->capacity);
    if (n < 0) return -1;
    buf->size       = n;
    buf->writeIndex = n;
    return n;
}

extern "C" int aiml_tls_send_string(void* sock, const char* str) {
    if (!sock || !str) return -1;
    AimlTlsSocket* s = to_tls(sock);
    if (!s->ssl) return -1;
    int len = static_cast<int>(std::strlen(str));
    if (len == 0) return 0;
    int total = 0, remaining = len;
    const char* ptr = str;
    while (remaining > 0) {
        int n = SSL_write(s->ssl, ptr, remaining);
        if (n <= 0) return -1;
        ptr       += n;
        total     += n;
        remaining -= n;
    }
    return total;
}

extern "C" char* aiml_tls_recv_string(void* sock, int32_t max_bytes) {
    if (!sock || max_bytes <= 0) return nullptr;
    AimlTlsSocket* s = to_tls(sock);
    if (!s->ssl) return nullptr;
    char* buf = static_cast<char*>(std::malloc(static_cast<size_t>(max_bytes) + 1));
    if (!buf) return nullptr;
    int n = SSL_read(s->ssl, buf, max_bytes);
    if (n < 0) { std::free(buf); return nullptr; }
    buf[n] = '\0';
    return buf;
}

// ── Peer certificate ──────────────────────────────────────────────────────────

extern "C" char* aiml_tls_peer_cert_cn(void* sock) {
    if (!sock) return nullptr;
    AimlTlsSocket* s = to_tls(sock);
    if (!s->ssl) return nullptr;
    X509* cert = SSL_get_peer_certificate(s->ssl);
    if (!cert) return nullptr;
    X509_NAME* subj = X509_get_subject_name(cert);
    char buf[256] = {};
    X509_NAME_get_text_by_NID(subj, NID_commonName, buf, sizeof(buf));
    X509_free(cert);
    return strdup(buf);
}

extern "C" int32_t aiml_tls_peer_cert_valid(void* sock) {
    if (!sock) return 0;
    AimlTlsSocket* s = to_tls(sock);
    if (!s->ssl) return 0;
    return SSL_get_verify_result(s->ssl) == X509_V_OK ? 1 : 0;
}
