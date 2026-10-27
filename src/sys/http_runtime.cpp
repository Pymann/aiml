#include "http_runtime.h"
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cctype>
#include <cassert>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>   // X509_CHECK_FLAG_* for strict hostname verification
#include "ws_runtime.h"       // aiml_ws_client_insecure — shared client-TLS toggle
#include <openssl/err.h>
#include "tls_runtime.h"   // aiml_tls_harden_server_ctx / aiml_tls_require_client_cert (shared policy)
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <ctime>
#include <ctime>              // time() — request read deadline (slowloris defence)
#include <poll.h>
#include <chrono>
#include "string_runtime.h"   // aiml_str_dup: refcounted copy (AIML decref frees it, no leak)

/* ────────────────────────────────────────────────────────────────
 * Helpers
 * ──────────────────────────────────────────────────────────────── */

static char* xstrdup(const char* s) { return s ? strdup(s) : nullptr; }

static std::string str_tolower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

// URL-decode a single string (replaces %XX and + in query)
static std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int v = 0;
            sscanf(s.substr(i+1,2).c_str(), "%2x", &v);
            out += static_cast<char>(v);
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

static int send_all_fd(int fd, void* ssl, const char* buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n;
        if (ssl) {
            n = SSL_write(static_cast<SSL*>(ssl), buf + sent, len - sent);
            if (n <= 0) return -1;
        } else {
            n = static_cast<int>(::send(fd, buf + sent,
                                        static_cast<size_t>(len - sent), MSG_NOSIGNAL));
            if (n <= 0) return -1;
        }
        sent += n;
    }
    return sent;
}

static int recv_line(int fd, void* ssl, char* buf, int max) {
    int i = 0;
    while (i < max - 1) {
        char c;
        int n;
        if (ssl) n = SSL_read(static_cast<SSL*>(ssl), &c, 1);
        else     n = static_cast<int>(::recv(fd, &c, 1, 0));
        if (n <= 0) break;
        if (c == '\n') { buf[i++] = '\n'; break; }
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

static int recv_n(int fd, void* ssl, char* buf, int n) {
    int got = 0;
    while (got < n) {
        int r;
        if (ssl) r = SSL_read(static_cast<SSL*>(ssl), buf + got, n - got);
        else     r = static_cast<int>(::recv(fd, buf + got, static_cast<size_t>(n - got), 0));
        if (r <= 0) return got;
        got += r;
    }
    return got;
}

/* AimlHttpMap (aiml_http_map_new/put/get/len/free) lives in http_map.cpp — a
 * portable translation unit so the WASM runtime (no HTTP server) can still link
 * sys.env, which returns one of these maps. */

/* ────────────────────────────────────────────────────────────────
 * URL parsing
 * ──────────────────────────────────────────────────────────────── */

extern "C" AimlUrl* aiml_url_parse(const char* raw) {
    if (!raw) return nullptr;
    AimlUrl* u = static_cast<AimlUrl*>(calloc(1, sizeof(AimlUrl)));
    u->get_param = aiml_http_map_new();

    std::string s(raw);

    // Extract fragment
    size_t frag = s.find('#');
    if (frag != std::string::npos) {
        u->fragment = xstrdup(s.substr(frag + 1).c_str());
        s = s.substr(0, frag);
    }

    // Extract query string
    size_t qpos = s.find('?');
    if (qpos != std::string::npos) {
        std::string query = s.substr(qpos + 1);
        s = s.substr(0, qpos);
        // parse key=value pairs
        std::istringstream ss(query);
        std::string token;
        while (std::getline(ss, token, '&')) {
            size_t eq = token.find('=');
            std::string k = url_decode(eq == std::string::npos ? token : token.substr(0, eq));
            std::string v = eq == std::string::npos ? "" : url_decode(token.substr(eq + 1));
            aiml_http_map_put(u->get_param, k.c_str(), v.c_str());
        }
    }

    // Extract scheme
    size_t scheme_end = s.find("://");
    if (scheme_end != std::string::npos) {
        u->protocol = xstrdup(s.substr(0, scheme_end).c_str());
        s = s.substr(scheme_end + 3);
    } else {
        // relative or protocol-less
        u->path = xstrdup(s.c_str());
        return u;
    }

    // Extract path (after first /)
    size_t slash = s.find('/');
    std::string authority;
    if (slash != std::string::npos) {
        u->path     = xstrdup(s.substr(slash).c_str());
        authority   = s.substr(0, slash);
    } else {
        u->path     = xstrdup("/");
        authority   = s;
    }

    // Extract credentials (user:pass@)
    size_t at = authority.find('@');
    if (at != std::string::npos) {
        std::string creds = authority.substr(0, at);
        authority = authority.substr(at + 1);
        AimlUrlCredential* c = static_cast<AimlUrlCredential*>(calloc(1, sizeof(AimlUrlCredential)));
        size_t colon = creds.find(':');
        if (colon != std::string::npos) {
            c->user = xstrdup(creds.substr(0, colon).c_str());
            c->pass = xstrdup(creds.substr(colon + 1).c_str());
        } else {
            c->user = xstrdup(creds.c_str());
        }
        u->credential = c;
    }

    // Extract port
    size_t colon = authority.find(':');
    if (colon != std::string::npos) {
        u->port = atoi(authority.substr(colon + 1).c_str());
        u->host = xstrdup(authority.substr(0, colon).c_str());
    } else {
        u->host = xstrdup(authority.c_str());
        // Default ports
        if (u->protocol && strcmp(u->protocol, "https") == 0) u->port = 443;
        else if (u->protocol && strcmp(u->protocol, "http") == 0) u->port = 80;
    }

    return u;
}

extern "C" char* aiml_url_to_string(AimlUrl* u) {
    if (!u) return xstrdup("");
    std::string s;
    if (u->protocol) { s += u->protocol; s += "://"; }
    if (u->credential) {
        AimlUrlCredential* c = static_cast<AimlUrlCredential*>(u->credential);
        if (c->user) s += c->user;
        if (c->pass) { s += ':'; s += c->pass; }
        s += '@';
    }
    if (u->host) s += u->host;
    if (u->port && u->port != 80 && u->port != 443) {
        s += ':'; s += std::to_string(u->port);
    }
    if (u->path) s += u->path;
    // append query params
    if (u->get_param && u->get_param->size > 0) {
        bool first = true;
        for (int i = 0; i < u->get_param->capacity; ++i) {
            if (!u->get_param->occupied[i]) continue;
            s += first ? '?' : '&';
            first = false;
            s += u->get_param->keys[i];
            s += '=';
            s += u->get_param->values[i];
        }
    }
    if (u->fragment) { s += '#'; s += u->fragment; }
    return xstrdup(s.c_str());
}

extern "C" int8_t aiml_url_is_https(AimlUrl* u) {
    return u && u->protocol && strcmp(u->protocol, "https") == 0;
}

extern "C" char*    aiml_url_protocol(AimlUrl* u)   { return u ? u->protocol : nullptr; }
extern "C" char*    aiml_url_host(AimlUrl* u)        { return u ? u->host : nullptr; }
extern "C" char*    aiml_url_path(AimlUrl* u)        { return u ? u->path : nullptr; }
extern "C" char*    aiml_url_fragment(AimlUrl* u)    { return u ? u->fragment : nullptr; }
extern "C" int32_t  aiml_url_port(AimlUrl* u)        { return u ? u->port : 0; }
extern "C" void*    aiml_url_get_param(AimlUrl* u)   { return u ? u->get_param : nullptr; }
extern "C" void*    aiml_url_credential(AimlUrl* u)  { return u ? u->credential : nullptr; }
extern "C" char*    aiml_url_cred_user(AimlUrlCredential* c) { return c ? c->user : nullptr; }
extern "C" char*    aiml_url_cred_pass(AimlUrlCredential* c) { return c ? c->pass : nullptr; }

/* ────────────────────────────────────────────────────────────────
 * HTTP/1.1 client helpers
 * ──────────────────────────────────────────────────────────────── */

static int tcp_connect(const char* host, int port, int timeout_ms) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    struct addrinfo hints{}, *res;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_str, &hints, &res) != 0) return -1;
    int fd = -1;
    for (struct addrinfo* p = res; p; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype | SOCK_CLOEXEC, p->ai_protocol);
        if (fd < 0) continue;
        if (timeout_ms > 0) {
            // non-blocking connect with timeout
            fcntl(fd, F_SETFL, O_NONBLOCK);
            ::connect(fd, p->ai_addr, p->ai_addrlen);
            struct timeval tv;
            tv.tv_sec  = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
            if (select(fd + 1, nullptr, &fds, nullptr, &tv) <= 0) {
                ::close(fd); fd = -1; continue;
            }
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
        } else {
            if (::connect(fd, p->ai_addr, p->ai_addrlen) < 0) {
                ::close(fd); fd = -1; continue;
            }
        }
        break;
    }
    freeaddrinfo(res);
    return fd;
}

// Parse an HTTP/1.1 response from fd/ssl into a new AimlHttpResponse
static AimlHttpResponse* parse_http_response(int fd, void* ssl) {
    AimlHttpResponse* r = static_cast<AimlHttpResponse*>(calloc(1, sizeof(AimlHttpResponse)));
    r->headers = aiml_http_map_new();
    r->conn_fd = -1;

    // Status line
    char line[4096];
    recv_line(fd, ssl, line, sizeof(line));
    // "HTTP/1.1 200 OK\r\n"
    int status = 200;
    if (sscanf(line, "HTTP/%*s %d", &status) == 1) r->status = status;
    else r->status = 200;
    r->ok = (r->status >= 200 && r->status < 300) ? 1 : 0;

    // Headers
    int content_length = -1;
    bool chunked = false;
    while (true) {
        int n = recv_line(fd, ssl, line, sizeof(line));
        if (n <= 0) break;
        // strip \r\n
        while (n > 0 && (line[n-1] == '\r' || line[n-1] == '\n')) line[--n] = '\0';
        if (n == 0) break; // blank line = end of headers
        char* colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            const char* k = line;
            const char* v = colon + 1;
            while (*v == ' ') ++v;
            std::string lk = str_tolower(k);
            aiml_http_map_put(r->headers, lk.c_str(), v);
            if (lk == "content-length") content_length = atoi(v);
            if (lk == "transfer-encoding" && strstr(v, "chunked")) chunked = true;
        }
    }

    // Body
    std::string body;
    if (chunked) {
        while (true) {
            recv_line(fd, ssl, line, sizeof(line));
            int chunk_size = strtol(line, nullptr, 16);
            if (chunk_size <= 0) break;
            std::vector<char> chunk(chunk_size + 2);
            recv_n(fd, ssl, chunk.data(), chunk_size + 2);
            body.append(chunk.data(), chunk_size);
        }
    } else if (content_length > 0) {
        std::vector<char> buf(content_length + 1, '\0');
        recv_n(fd, ssl, buf.data(), content_length);
        body = buf.data();
    }

    if (!body.empty()) r->body = xstrdup(body.c_str());
    return r;
}

static AimlHttpResponse* do_http_request(const char* method, AimlUrl* u,
                                          AimlHttpMap* extra_headers,
                                          const char* body_str,
                                          int timeout_ms) {
    if (!u || !u->host) return nullptr;

    bool use_ssl = u->protocol && strcmp(u->protocol, "https") == 0;
    int port = u->port ? u->port : (use_ssl ? 443 : 80);

    int fd = tcp_connect(u->host, port, timeout_ms > 0 ? timeout_ms : 10000);
    if (fd < 0) return nullptr;

    SSL_CTX* ssl_ctx = nullptr;
    SSL*     ssl     = nullptr;
    if (use_ssl) {
        SSL_library_init();
        ssl_ctx = SSL_CTX_new(TLS_client_method());
        SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);
        SSL_CTX_set_default_verify_paths(ssl_ctx);
        const char* https_ca = getenv("AIML_WSS_CA");   // shared client-TLS CA bundle
        if (https_ca && *https_ca) SSL_CTX_load_verify_locations(ssl_ctx, https_ca, nullptr);
        // Verify the server cert (chain + hostname) by default → defeats active MITM.
        // Shares the client-TLS insecure toggle with the wss client
        // (mirror.tls_insecure(1) / AIML_WSS_INSECURE=1) for local dev.
        bool https_insecure = aiml_ws_client_insecure() != 0;
        SSL_CTX_set_verify(ssl_ctx, https_insecure ? SSL_VERIFY_NONE : SSL_VERIFY_PEER, nullptr);
        ssl = SSL_new(ssl_ctx);
        SSL_set_fd(ssl, fd);
        SSL_set_tlsext_host_name(ssl, u->host);
        if (!https_insecure) {
            SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
            SSL_set1_host(ssl, u->host);
        }
        if (SSL_connect(ssl) <= 0) {
            SSL_free(ssl); SSL_CTX_free(ssl_ctx); ::close(fd);
            return nullptr;
        }
    }

    // Build request
    std::string req;
    req += method; req += " ";
    req += (u->path ? u->path : "/");
    if (u->get_param && u->get_param->size > 0) {
        bool first = true;
        for (int i = 0; i < u->get_param->capacity; ++i) {
            if (!u->get_param->occupied[i]) continue;
            req += first ? '?' : '&';
            first = false;
            req += u->get_param->keys[i];
            req += '=';
            req += u->get_param->values[i];
        }
    }
    req += " HTTP/1.1\r\nHost: ";
    req += u->host;
    req += "\r\nConnection: close\r\n";

    if (extra_headers) {
        for (int i = 0; i < extra_headers->capacity; ++i) {
            if (!extra_headers->occupied[i]) continue;
            req += extra_headers->keys[i]; req += ": ";
            req += extra_headers->values[i]; req += "\r\n";
        }
    }

    size_t body_len = body_str ? strlen(body_str) : 0;
    if (body_len > 0) {
        req += "Content-Length: "; req += std::to_string(body_len); req += "\r\n";
    }
    req += "\r\n";
    if (body_len > 0) req += body_str;

    send_all_fd(fd, ssl, req.c_str(), static_cast<int>(req.size()));

    AimlHttpResponse* resp = parse_http_response(fd, ssl);

    if (ssl)     { SSL_shutdown(ssl); SSL_free(ssl); }
    if (ssl_ctx) SSL_CTX_free(ssl_ctx);
    ::close(fd);

    return resp;
}

/* ────────────────────────────────────────────────────────────────
 * http namespace — convenience functions
 * ──────────────────────────────────────────────────────────────── */

extern "C" void* aiml_http_get(void* url) {
    return do_http_request("GET", static_cast<AimlUrl*>(url), nullptr, nullptr, 0);
}
extern "C" void* aiml_http_post(void* url, const char* body) {
    return do_http_request("POST", static_cast<AimlUrl*>(url), nullptr, body, 0);
}
extern "C" void* aiml_http_put(void* url, const char* body) {
    return do_http_request("PUT", static_cast<AimlUrl*>(url), nullptr, body, 0);
}
extern "C" void* aiml_http_delete(void* url) {
    return do_http_request("DELETE", static_cast<AimlUrl*>(url), nullptr, nullptr, 0);
}
extern "C" void* aiml_http_head(void* url) {
    return do_http_request("HEAD", static_cast<AimlUrl*>(url), nullptr, nullptr, 0);
}

/* ────────────────────────────────────────────────────────────────
 * http_client_handle
 * ──────────────────────────────────────────────────────────────── */

extern "C" void* aiml_http_new_request(const char* method, void* url) {
    AimlHttpClient* c = static_cast<AimlHttpClient*>(calloc(1, sizeof(AimlHttpClient)));
    c->headers = aiml_http_map_new();
    c->method  = xstrdup(method ? method : "GET");
    c->url     = static_cast<AimlUrl*>(url);
    return c;
}

extern "C" void* aiml_http_client_new(const char* method, void* url) {
    return aiml_http_new_request(method, url);
}

extern "C" void* aiml_http_client_send(void* client_ptr) {
    AimlHttpClient* c = static_cast<AimlHttpClient*>(client_ptr);
    if (!c) return nullptr;
    return do_http_request(c->method ? c->method : "GET",
                           c->url, c->headers, c->body,
                           c->timeout > 0 ? c->timeout : 0);
}

extern "C" int32_t aiml_http_resp_status(void* resp) {
    return resp ? static_cast<AimlHttpResponse*>(resp)->status : 0;
}
extern "C" int8_t aiml_http_resp_ok(void* resp) {
    return resp ? static_cast<AimlHttpResponse*>(resp)->ok : 0;
}
extern "C" void* aiml_http_resp_headers(void* resp) {
    return resp ? static_cast<AimlHttpResponse*>(resp)->headers : nullptr;
}
extern "C" char* aiml_http_resp_body(void* resp) {
    // Return a REFCOUNTED copy (aiml_str_dup) so the string's lifetime is managed
    // by the AIML runtime (decref frees it — no leak), independent of the response
    // object.  Handing out the internal ->body pointer caused a use-after-free
    // (garbage body once the caller did resp.free(), which frees ->body).
    char* b = resp ? static_cast<AimlHttpResponse*>(resp)->body : nullptr;
    return aiml_str_dup(b ? b : "");
}
extern "C" void aiml_http_resp_free(void* resp) {
    if (!resp) return;
    AimlHttpResponse* r = static_cast<AimlHttpResponse*>(resp);
    aiml_http_map_free(r->headers);
    free(r->body);
    free(r);
}

/* ────────────────────────────────────────────────────────────────
 * HTTP Server internals
 * ──────────────────────────────────────────────────────────────── */

struct RouteEntry {
    std::string method;     // "GET", "*", etc.
    std::string pattern;    // "/users/:id", "/static/*"
    void*       handler;    // AimlHandlerFn (ignored if is_fs)
    void*       group_mw;   // group-level middleware fn (or nullptr)
    std::string prefix;     // group prefix (empty for root routes)
    // Filesystem mode (registered via srv.filesystem / route.filesystem):
    bool                     is_fs = false;
    std::vector<std::string> fs_dirs;
    std::string              fs_index = "index.html";
    bool                     fs_spa   = false;
    int32_t                  fs_cache_max_age = 0;
    std::string              fs_url_prefix; // url prefix to strip from path
};

struct AimlHttpServer {
    int         listen_fd   = -1;
    SSL_CTX*    ssl_ctx     = nullptr;
    std::vector<RouteEntry> routes;
    std::vector<void*>      global_mw; // middleware fn ptrs
    AimlHttpMap* default_headers = nullptr;
    volatile int running    = 0;
    pthread_t    accept_tid = 0;
    pthread_mutex_t lock    = PTHREAD_MUTEX_INITIALIZER;
    int          read_timeout_secs = 15; // per-recv + total request-read deadline (slowloris)
    long         max_body_bytes    = 8L * 1024 * 1024; // request body cap (srv.set_max_body)

    // ── Abuse resistance (spec/32_2 §3.12) ────────────────────────────────
    int              max_conns = 0;          // 0 = unlimited concurrent connections
    std::atomic<int> active_conns{0};        // live handler threads
    int              rate_max  = 0;          // 0 = per-IP rate limiting off
    int              rate_window_secs = 60;  // fixed-window length
    std::unordered_map<uint32_t, std::pair<time_t,int>> rate_map; // ip → (window_start, count)
    std::mutex       rate_mtx;
};

struct AimlHttpRoute {
    AimlHttpServer* server;
    std::string prefix;
    void*       middleware; // single group middleware fn (nullptr if none)
};

// Pattern matching: returns true + fills params if match
static bool match_pattern(const std::string& pattern, const std::string& path,
                           AimlHttpMap* params) {
    // Split both by '/'
    std::vector<std::string> pp, rp;
    {
        std::istringstream ss(pattern);
        std::string seg;
        while (std::getline(ss, seg, '/')) pp.push_back(seg);
    }
    {
        std::istringstream ss(path);
        std::string seg;
        while (std::getline(ss, seg, '/')) rp.push_back(seg);
    }

    for (size_t i = 0; i < pp.size(); ++i) {
        if (pp[i] == "*") return true; // wildcard: match rest
        if (i >= rp.size()) return false;
        if (pp[i].size() > 0 && pp[i][0] == ':') {
            // Named segment
            aiml_http_map_put(params, pp[i].substr(1).c_str(), rp[i].c_str());
        } else if (pp[i] != rp[i]) {
            return false;
        }
    }
    return pp.size() == rp.size();
}

// Parse HTTP/1.1 request from fd/ssl
static AimlHttpRequest* parse_http_request(int fd, void* ssl, int timeout_secs, long max_body_bytes) {
    AimlHttpRequest* req = static_cast<AimlHttpRequest*>(calloc(1, sizeof(AimlHttpRequest)));
    req->params      = aiml_http_map_new();
    req->req_headers = aiml_http_map_new();
    req->cookies     = aiml_http_map_new();
    // Total wall-clock deadline for reading the whole request (slowloris/drip-feed
    // defence — complements the per-recv SO_RCVTIMEO set on the socket).
    const time_t deadline = time(nullptr) + (timeout_secs > 0 ? timeout_secs : 15);

    char line[8192];
    // Request line
    int n = recv_line(fd, ssl, line, sizeof(line));
    if (n <= 0) {
        // EOF / recv-timeout before any request line (bare connect, dropped peer,
        // slowloris).  Skip dispatch (null method would crash route matching).
        req->method = xstrdup("GET"); req->path = xstrdup("/");
        req->reject_status = 408;
        return req;
    }
    // recv_line truncates at the buffer size; a full buffer without a terminating
    // newline means the request line (method + URI) is over-long → reject.
    if (n >= static_cast<int>(sizeof(line)) - 1 && line[n-1] != '\n') {
        req->method = xstrdup("GET"); req->path = xstrdup("/");
        req->reject_status = 414;                      // URI Too Long
        return req;
    }
    while (n > 0 && (line[n-1] == '\r' || line[n-1] == '\n')) line[--n] = '\0';

    char method[16] = {}, path_buf[4096] = {};
    sscanf(line, "%15s %4095s", method, path_buf);
    req->method = xstrdup(method);

    // Separate path from query
    std::string full_path(path_buf);
    std::string query_str;
    size_t qpos = full_path.find('?');
    if (qpos != std::string::npos) {
        query_str = full_path.substr(qpos + 1);
        full_path = full_path.substr(0, qpos);
    }
    req->path = xstrdup(full_path.c_str());

    // Build url
    AimlUrl* u = static_cast<AimlUrl*>(calloc(1, sizeof(AimlUrl)));
    u->path      = xstrdup(full_path.c_str());
    u->get_param = aiml_http_map_new();
    // parse query
    if (!query_str.empty()) {
        std::istringstream ss(query_str);
        std::string token;
        while (std::getline(ss, token, '&')) {
            size_t eq = token.find('=');
            std::string k = url_decode(eq == std::string::npos ? token : token.substr(0, eq));
            std::string v = eq == std::string::npos ? "" : url_decode(token.substr(eq + 1));
            aiml_http_map_put(u->get_param, k.c_str(), v.c_str());
        }
    }
    req->url = u;

    // ── Parser limits (DoS/abuse resistance, spec/32_2 §hardening) ─────────────
    //   Reject early instead of allocating unbounded memory or looping forever.
    constexpr int    MAX_HEADER_COUNT = 100;         // # of header lines
    constexpr size_t MAX_HEADER_BYTES = 64 * 1024;   // total header bytes
    // Request body cap: server-configurable via srv.set_max_body (default 8 MiB).
    const long       MAX_BODY_BYTES   = max_body_bytes > 0 ? max_body_bytes : 8L * 1024 * 1024;

    // Headers
    long   content_length = 0;
    int    header_count   = 0;
    size_t header_bytes   = 0;
    while (true) {
        if (time(nullptr) > deadline) { req->reject_status = 408; return req; } // Request Timeout
        n = recv_line(fd, ssl, line, sizeof(line));
        // raw recv_line == 0 means EOF/recv-timeout BEFORE the terminating blank
        // line — an incomplete request (slowloris / dropped peer), NOT end-of-headers.
        // A real blank line arrives as "\r\n" (raw >= 1) and strips to 0 below.
        if (n <= 0) { req->reject_status = 408; return req; }
        while (n > 0 && (line[n-1] == '\r' || line[n-1] == '\n')) line[--n] = '\0';
        if (n == 0) break;   // terminating blank line → headers complete
        header_bytes += static_cast<size_t>(n) + 2;   // + CRLF
        if (++header_count > MAX_HEADER_COUNT || header_bytes > MAX_HEADER_BYTES) {
            req->reject_status = 431;                  // Request Header Fields Too Large
            return req;
        }
        char* colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            const char* k = line; const char* v = colon + 1;
            while (*v == ' ') ++v;
            std::string lk = str_tolower(k);
            aiml_http_map_put(req->req_headers, lk.c_str(), v);
            if (lk == "content-length") {
                // Validate: a single, well-formed, non-negative integer within cap.
                errno = 0;
                char* endp = nullptr;
                long cl = strtol(v, &endp, 10);
                while (endp && (*endp == ' ' || *endp == '\t')) ++endp;
                if (errno != 0 || !endp || *endp != '\0' || cl < 0) {
                    req->reject_status = 400;          // Bad Request (malformed length)
                    return req;
                }
                if (cl > MAX_BODY_BYTES) {
                    req->reject_status = 413;          // Payload Too Large
                    return req;
                }
                content_length = cl;
            }
            if (lk == "cookie") {
                // Parse "key=val; key2=val2"
                std::string cookie_str(v);
                std::istringstream ss(cookie_str);
                std::string part;
                while (std::getline(ss, part, ';')) {
                    while (!part.empty() && part[0] == ' ') part.erase(0, 1);
                    size_t eq = part.find('=');
                    if (eq != std::string::npos) {
                        aiml_http_map_put(req->cookies,
                            part.substr(0, eq).c_str(),
                            part.substr(eq + 1).c_str());
                    }
                }
            }
        }
    }

    // Body (content_length already validated + capped to MAX_BODY_BYTES above).
    // Stored binary-safe: `body` keeps a trailing NUL for string access, and
    // `body_len` holds the exact byte count for req.body_bytes.
    if (content_length > 0) {
        char* raw = static_cast<char*>(malloc(static_cast<size_t>(content_length) + 1));
        if (!raw) { req->reject_status = 413; return req; }
        int got = recv_n(fd, ssl, raw, static_cast<int>(content_length));
        if (got < 0) got = 0;
        raw[got] = '\0';
        req->body     = raw;
        req->body_len = got;
    }

    return req;
}

static void free_request(AimlHttpRequest* req) {
    if (!req) return;
    free(req->method); free(req->path); free(req->body);
    if (req->url) {
        free(req->url->path); free(req->url->protocol); free(req->url->host);
        free(req->url->fragment);
        aiml_http_map_free(req->url->get_param);
        if (req->url->credential) {
            AimlUrlCredential* c = static_cast<AimlUrlCredential*>(req->url->credential);
            free(c->user); free(c->pass); free(c);
        }
        free(req->url);
    }
    aiml_http_map_free(req->params);
    aiml_http_map_free(req->req_headers);
    aiml_http_map_free(req->cookies);
    free(req->peer_ip);
    free(req);
}

static void free_response(AimlHttpResponse* resp) {
    if (!resp) return;
    aiml_http_map_free(resp->headers);
    free(resp->body); free(resp->body_file); free(resp->req_range);
    for (int i = 0; i < resp->n_set_cookies; ++i) free(resp->set_cookies[i]);
    free(resp->set_cookies);
    free(resp);
}

// Format and send a full HTTP/1.1 response
static void send_response(AimlHttpResponse* resp) {
    if (!resp || resp->resp_sent || resp->conn_fd < 0) return;
    resp->resp_sent = 1;

    int fd = resp->conn_fd;
    void* ssl = resp->ssl;

    // Status line
    std::string out = "HTTP/1.1 ";
    out += std::to_string(resp->status);
    out += " \r\n";

    // Default headers
    if (!aiml_http_map_get(resp->headers, "content-type"))
        aiml_http_map_put(resp->headers, "content-type", "text/plain");

    // body_file: read and serve
    std::string file_body;
    if (resp->body_file && !resp->body) {
        FILE* f = fopen(resp->body_file, "rb");
        if (f) {
            fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
            file_body.resize(static_cast<size_t>(sz));
            fread(&file_body[0], 1, static_cast<size_t>(sz), f);
            fclose(f);
        }
    }
    const char* body_ptr  = !file_body.empty() ? file_body.c_str() : resp->body;
    size_t      body_size = body_ptr ? strlen(body_ptr) : 0;
    if (!file_body.empty()) body_size = file_body.size();

    char cl[32];
    snprintf(cl, sizeof(cl), "%zu", body_size);
    aiml_http_map_put(resp->headers, "content-length", cl);

    // User headers
    for (int i = 0; i < resp->headers->capacity; ++i) {
        if (!resp->headers->occupied[i]) continue;
        out += resp->headers->keys[i]; out += ": ";
        out += resp->headers->values[i]; out += "\r\n";
    }
    // Set-Cookie headers — kept out of the header map because a client may receive
    // several, and the map holds one value per key. Emitted as distinct lines.
    for (int i = 0; i < resp->n_set_cookies; ++i) {
        if (!resp->set_cookies[i]) continue;
        out += "Set-Cookie: "; out += resp->set_cookies[i]; out += "\r\n";
    }
    out += "\r\n";

    send_all_fd(fd, ssl, out.c_str(), static_cast<int>(out.size()));
    if (body_ptr && body_size > 0)
        send_all_fd(fd, ssl, body_ptr, static_cast<int>(body_size));
}

static void serve_file(AimlHttpResponse* resp,
                        const std::string& file_path,
                        const RouteEntry& route,
                        const char* range_header = nullptr);

// server-side response methods
// A handler that set `resp.body_file` (and no text body) gets the same
// streamed, range-aware file delivery as srv.filesystem (spec/32_2: "File as
// body — streamed during send"). Without this the file was read fully into
// memory and its length taken with strlen, truncating binary files.
extern "C" void aiml_http_resp_send(void* resp_ptr) {
    AimlHttpResponse* r = static_cast<AimlHttpResponse*>(resp_ptr);
    if (r && !r->resp_sent && r->conn_fd >= 0 && r->body_file && !r->body) {
        RouteEntry plain;   // no cache max-age → ETag revalidation
        serve_file(r, r->body_file, plain, r->req_range);
        return;
    }
    send_response(r);
}

extern "C" void aiml_http_resp_set_body_file(void* resp_ptr, const char* file_path) {
    AimlHttpResponse* r = static_cast<AimlHttpResponse*>(resp_ptr);
    if (!r) return;
    free(r->body_file);
    r->body_file = (file_path && file_path[0]) ? xstrdup(file_path) : nullptr;
}

extern "C" void aiml_http_resp_write(void* resp_ptr, const char* chunk) {
    AimlHttpResponse* r = static_cast<AimlHttpResponse*>(resp_ptr);
    if (!r || r->conn_fd < 0) return;
    if (!r->resp_sent) {
        // Start chunked transfer
        r->resp_sent = 1;
        r->chunked = 1;
        std::string hdr = "HTTP/1.1 ";
        hdr += std::to_string(r->status);
        hdr += " \r\nTransfer-Encoding: chunked\r\n";
        if (!aiml_http_map_get(r->headers, "content-type"))
            aiml_http_map_put(r->headers, "content-type", "text/plain");
        for (int i = 0; i < r->headers->capacity; ++i) {
            if (!r->headers->occupied[i]) continue;
            hdr += r->headers->keys[i]; hdr += ": ";
            hdr += r->headers->values[i]; hdr += "\r\n";
        }
        hdr += "\r\n";
        send_all_fd(r->conn_fd, r->ssl, hdr.c_str(), static_cast<int>(hdr.size()));
    }
    if (!chunk) return;
    size_t len = strlen(chunk);
    char sz[16]; snprintf(sz, sizeof(sz), "%zx\r\n", len);
    send_all_fd(r->conn_fd, r->ssl, sz, static_cast<int>(strlen(sz)));
    send_all_fd(r->conn_fd, r->ssl, chunk, static_cast<int>(len));
    send_all_fd(r->conn_fd, r->ssl, "\r\n", 2);
}

extern "C" void aiml_http_resp_write_bytes(void* resp_ptr, void* buf) {
    // buf = AimlBuffer*: { i32 size, i32 cap, i32 rIdx, i32 wIdx, char* data }
    struct AimlBuf { int32_t sz, cap, rIdx, wIdx; char* data; };
    AimlHttpResponse* r = static_cast<AimlHttpResponse*>(resp_ptr);
    AimlBuf* b = static_cast<AimlBuf*>(buf);
    if (!r || !b || r->conn_fd < 0) return;
    // Ensure chunked is started
    if (!r->resp_sent) {
        aiml_http_resp_write(resp_ptr, nullptr); // start chunked, send headers
    }
    size_t len = static_cast<size_t>(b->wIdx > 0 ? b->wIdx : b->sz);
    char sz[16]; snprintf(sz, sizeof(sz), "%zx\r\n", len);
    send_all_fd(r->conn_fd, r->ssl, sz, static_cast<int>(strlen(sz)));
    send_all_fd(r->conn_fd, r->ssl, b->data, static_cast<int>(len));
    send_all_fd(r->conn_fd, r->ssl, "\r\n", 2);
}

extern "C" void aiml_http_resp_flush(void* resp_ptr) {
    // no-op for now (kernel handles buffering)
    (void)resp_ptr;
}

extern "C" void aiml_http_resp_close(void* resp_ptr) {
    AimlHttpResponse* r = static_cast<AimlHttpResponse*>(resp_ptr);
    if (!r || r->conn_fd < 0) return;
    if (r->chunked) {
        // terminal chunk
        send_all_fd(r->conn_fd, r->ssl, "0\r\n\r\n", 5);
    }
    r->resp_sent = 1;
}

// Request header/cookie access
extern "C" char* aiml_http_req_header(void* req_ptr, const char* name) {
    AimlHttpRequest* req = static_cast<AimlHttpRequest*>(req_ptr);
    if (!req || !name) return nullptr;
    std::string lname = str_tolower(name);
    // Refcounted copy so the value outlives the request (no UAF after req free).
    // Ein fehlender Header ist der LEERE String — nullptr liess das Beispiel aus
    // spec/32_3 §2.2 (`set: [auth, req.header("Authorization")]`) abstuerzen.
    const char* v = aiml_http_map_get(req->req_headers, lname.c_str());
    return aiml_str_dup(v ? v : "");
}

extern "C" char* aiml_http_req_cookie(void* req_ptr, const char* name) {
    AimlHttpRequest* req = static_cast<AimlHttpRequest*>(req_ptr);
    if (!req || !name) return nullptr;
    const char* v = aiml_http_map_get(req->cookies, name);
    return v ? aiml_str_dup(v) : nullptr;   // refcounted copy; nullptr if absent
}

// req.client_ip() — the trusted socket peer IP (dotted), captured at accept time.
// It deliberately IGNORES X-Forwarded-For / X-Real-IP: those are attacker-controlled
// unless the app explicitly reads and validates them behind a trusted proxy. Returns
// a refcounted string ("" if unknown, e.g. a client-side response object).
extern "C" char* aiml_http_req_client_ip(void* req_ptr) {
    AimlHttpRequest* req = static_cast<AimlHttpRequest*>(req_ptr);
    if (!req || !req->peer_ip) return aiml_str_dup("");
    return aiml_str_dup(req->peer_ip);
}

/* ────────────────────────────────────────────────────────────────
 * Static file serving (srv.filesystem / route.filesystem)
 * ──────────────────────────────────────────────────────────────── */

// Minimal MIME type table. Default is application/octet-stream.
static const char* mime_for_ext(const std::string& path) {
    const char* dot = strrchr(path.c_str(), '.');
    if (!dot) return "application/octet-stream";
    std::string ext = str_tolower(dot);
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".js"   || ext == ".mjs") return "application/javascript";
    if (ext == ".wasm")                  return "application/wasm";
    if (ext == ".css")                   return "text/css; charset=utf-8";
    if (ext == ".json")                  return "application/json";
    if (ext == ".svg")                   return "image/svg+xml";
    if (ext == ".png")                   return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif")                   return "image/gif";
    if (ext == ".ico")                   return "image/x-icon";
    if (ext == ".webp")                  return "image/webp";
    if (ext == ".txt" || ext == ".md")   return "text/plain; charset=utf-8";
    if (ext == ".xml")                   return "application/xml";
    if (ext == ".pdf")                   return "application/pdf";
    if (ext == ".data")                  return "application/octet-stream";
    if (ext == ".woff")                  return "font/woff";
    if (ext == ".woff2")                 return "font/woff2";
    return "application/octet-stream";
}

// Reject (already URL-decoded) relative paths that could escape the served root:
// any ".." segment, a leading "/" (absolute), a NUL byte (truncation attack), or a
// backslash (Windows-style separator).  The realpath containment check in
// resolve_under is the definitive backstop; this rejects the obvious cases early.
static bool path_is_safe(const std::string& rel) {
    if (rel.find("..")   != std::string::npos) return false;
    if (rel.find('\0')   != std::string::npos) return false;
    if (rel.find('\\')   != std::string::npos) return false;
    if (!rel.empty() && rel[0] == '/') return false;
    return true;
}

// Strip the route's URL prefix from the request path; returns relative path.
// Empty prefix or "/" → return request path without leading slash.
static std::string strip_url_prefix(const std::string& url_prefix,
                                     const std::string& req_path) {
    std::string rel = req_path;
    if (!url_prefix.empty() && url_prefix != "/") {
        if (rel.rfind(url_prefix, 0) == 0) {
            rel = rel.substr(url_prefix.size());
        }
    }
    while (!rel.empty() && rel[0] == '/') rel.erase(0, 1);
    return rel;
}

// Read whole file into a buffer. Returns true on success.
static bool read_file_bytes(const std::string& path, std::vector<char>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 0) { fclose(f); return false; }
    out.resize(static_cast<size_t>(sz));
    if (sz > 0) {
        if (fread(out.data(), 1, static_cast<size_t>(sz), f) != static_cast<size_t>(sz)) {
            fclose(f);
            return false;
        }
    }
    fclose(f);
    return true;
}

static bool is_directory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

static bool is_regular_file(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

// Try to resolve a candidate file under one root directory.
// Returns absolute path on success, empty string otherwise.
// Canonical-containment backstop: resolve symlinks/./.. in BOTH paths and confirm
// the resolved file really lives under the resolved root.  Catches symlink escapes
// and any traversal that slipped past the string checks.  Both must exist.
static bool path_within_root(const std::string& root, const std::string& candidate) {
    char* rp_root = realpath(root.c_str(), nullptr);
    if (!rp_root) return false;
    char* rp_cand = realpath(candidate.c_str(), nullptr);
    if (!rp_cand) { free(rp_root); return false; }
    std::string r(rp_root), c(rp_cand);
    free(rp_root); free(rp_cand);
    if (c == r) return true;                       // the root itself
    if (!r.empty() && r.back() != '/') r += '/';
    return c.rfind(r, 0) == 0;                      // c starts with "<root>/"
}

static std::string resolve_under(const std::string& root,
                                  const std::string& rel_path,
                                  const std::string& index_file) {
    std::string candidate = root;
    if (!candidate.empty() && candidate.back() != '/') candidate += '/';
    candidate += rel_path;
    // Directory request → append index file
    if (rel_path.empty() || (rel_path.back() == '/') || is_directory(candidate)) {
        if (!candidate.empty() && candidate.back() != '/') candidate += '/';
        candidate += index_file;
    }
    // Must be a regular file AND canonically inside the served root (symlink-safe).
    if (is_regular_file(candidate) && path_within_root(root, candidate)) return candidate;
    return std::string();
}

// Serve a single file: status 200 + content-type + cache-control + body.
static void serve_file(AimlHttpResponse* resp,
                        const std::string& file_path,
                        const RouteEntry& route,
                        const char* range_header) {
    // Open + stat the file. We stream via sendfile() rather than reading
    // into a userspace buffer — this avoids per-request 10–100 MB heap
    // allocations and lets the kernel handle TCP back-pressure cleanly,
    // which matters when browsers (Firefox/Chrome fetch streams) drain
    // slowly. The previous read-all-then-send-all path worked for curl
    // but reliably broke browser fetch on bundles > ~30 MB.
    int ffd = ::open(file_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (ffd < 0) {
        resp->status = 500;
        resp->body   = xstrdup("file open failed");
        send_response(resp);
        return;
    }
    struct stat st;
    if (::fstat(ffd, &st) != 0 || !S_ISREG(st.st_mode)) {
        ::close(ffd);
        resp->status = 500;
        resp->body   = xstrdup("file stat failed");
        send_response(resp);
        return;
    }
    size_t total = static_cast<size_t>(st.st_size);

    // A content-type chosen by the handler (resp.body_file path) wins over the
    // extension guess; srv.filesystem responses start without one.
    if (!aiml_http_map_get(resp->headers, "content-type"))
        aiml_http_map_put(resp->headers, "content-type", mime_for_ext(file_path));
    // We don't support keep-alive yet — tell HTTP/1.1 clients explicitly
    // so they don't try to reuse the connection.
    aiml_http_map_put(resp->headers, "connection", "close");
    // Advertise byte-range support so clients can resume / chunk large bundles
    // (the WASM client streams client.data in ranged chunks — big single
    // fetches OOM/abort on mobile browsers).
    aiml_http_map_put(resp->headers, "accept-ranges", "bytes");
    // Version tag (size + mtime) so a client can skip re-downloading an unchanged
    // file.  The WASM client caches the big bundle in IndexedDB keyed by this ETag;
    // browsers also use it for If-None-Match / If-Modified-Since revalidation.
    {
        char etag[80];
        snprintf(etag, sizeof(etag), "\"%llx-%llx\"",
                 (unsigned long long)st.st_size,
                 (unsigned long long)st.st_mtime);
        aiml_http_map_put(resp->headers, "etag", etag);
        char lm[48]; struct tm gmt;
        time_t mt = (time_t)st.st_mtime;
        if (gmtime_r(&mt, &gmt)) {
            strftime(lm, sizeof(lm), "%a, %d %b %Y %H:%M:%S GMT", &gmt);
            aiml_http_map_put(resp->headers, "last-modified", lm);
        }
    }
    if (route.fs_cache_max_age > 0) {
        char cache_val[64];
        snprintf(cache_val, sizeof(cache_val),
                 "max-age=%d", route.fs_cache_max_age);
        aiml_http_map_put(resp->headers, "cache-control", cache_val);
    } else {
        // No explicit max-age → force ETag revalidation on every request rather
        // than letting the browser heuristically reuse a stale copy.  Without this,
        // a rebuilt client.js (loaded via a <script> tag) is served from cache while
        // the fresh client.wasm is fetched → EM_ASM address mismatch → crash.
        aiml_http_map_put(resp->headers, "cache-control", "no-cache");
    }

    // ── Parse a single `Range: bytes=START-END` (START or END may be empty;
    // a "-N" suffix range means the last N bytes).  Multi-range is not
    // supported — we serve the whole file in that case.
    size_t range_start = 0, range_end = (total > 0 ? total - 1 : 0);
    bool is_range = false, unsatisfiable = false;
    if (range_header && total > 0) {
        const char* p = strstr(range_header, "bytes=");
        if (p && !strchr(range_header, ',')) {
            p += 6;
            long long s = -1, e = -1;
            char* endp = nullptr;
            if (*p != '-') { s = strtoll(p, &endp, 10); p = endp; }
            if (*p == '-') { p++; if (*p && *p != ' ') e = strtoll(p, &endp, 10); }
            if (s < 0 && e >= 0) {                       // suffix: last e bytes
                long long len = e > (long long)total ? (long long)total : e;
                range_start = total - (size_t)len; range_end = total - 1;
                is_range = true;
            } else if (s >= 0) {
                range_start = (size_t)s;
                range_end = (e >= 0 && (size_t)e < total) ? (size_t)e : total - 1;
                if (range_start >= total || range_start > range_end) unsatisfiable = true;
                else is_range = true;
            }
        }
    }

    resp->resp_sent = 1;
    int fd = resp->conn_fd;
    void* ssl = resp->ssl;
    char cl_buf[32];

    if (unsatisfiable) {
        resp->status = 416;
        char cr[64]; snprintf(cr, sizeof(cr), "bytes */%zu", total);
        aiml_http_map_put(resp->headers, "content-range", cr);
        aiml_http_map_put(resp->headers, "content-length", "0");
    } else {
        resp->status = is_range ? 206 : 200;
        if (is_range) {
            char cr[96];
            snprintf(cr, sizeof(cr), "bytes %zu-%zu/%zu",
                     range_start, range_end, total);
            aiml_http_map_put(resp->headers, "content-range", cr);
        }
        size_t body_len = is_range ? (range_end - range_start + 1) : total;
        snprintf(cl_buf, sizeof(cl_buf), "%zu", body_len);
        aiml_http_map_put(resp->headers, "content-length", cl_buf);
    }

    // Build + send the status line + headers.
    const char* reason = resp->status == 206 ? "Partial Content"
                       : resp->status == 416 ? "Range Not Satisfiable" : "OK";
    std::string head = "HTTP/1.1 ";
    head += std::to_string(resp->status); head += " "; head += reason; head += "\r\n";
    for (int i = 0; i < resp->headers->capacity; ++i) {
        if (!resp->headers->occupied[i]) continue;
        head += resp->headers->keys[i];
        head += ": ";
        head += resp->headers->values[i];
        head += "\r\n";
    }
    head += "\r\n";
    send_all_fd(fd, ssl, head.c_str(), static_cast<int>(head.size()));

    if (unsatisfiable) { ::close(ffd); return; }

    // Stream the (possibly ranged) body. sendfile() needs a non-SSL fd; for
    // TLS routes we fall back to a read+SSL_write loop with a small buffer.
    size_t body_off = is_range ? range_start : 0;
    size_t body_len = is_range ? (range_end - range_start + 1) : total;
    if (body_len > 0) {
        if (ssl) {
            char buf[64 * 1024];
            if (body_off > 0) ::lseek(ffd, (off_t)body_off, SEEK_SET);
            size_t remaining = body_len;
            while (remaining > 0) {
                ssize_t r = ::read(ffd, buf,
                    remaining < sizeof(buf) ? remaining : sizeof(buf));
                if (r <= 0) { if (r < 0 && errno == EINTR) continue; break; }
                if (send_all_fd(fd, ssl, buf, static_cast<int>(r)) < 0) break;
                remaining -= static_cast<size_t>(r);
            }
        } else {
            off_t off = (off_t)body_off;
            size_t remaining = body_len;
            while (remaining > 0) {
                ssize_t n = ::sendfile(fd, ffd, &off, remaining);
                if (n < 0) {
                    if (errno == EINTR || errno == EAGAIN) continue;
                    // EPIPE / ECONNRESET / etc. — peer closed mid-stream.
                    fprintf(stderr,
                        "[http] serve_file: sendfile aborted at %zu/%zu "
                        "errno=%d (%s) path=%s\n",
                        body_len - remaining, body_len, errno, strerror(errno),
                        file_path.c_str());
                    break;
                }
                if (n == 0) {
                    fprintf(stderr,
                        "[http] serve_file: sendfile returned 0 at %zu/%zu "
                        "path=%s\n", body_len - remaining, body_len,
                        file_path.c_str());
                    break;
                }
                remaining -= static_cast<size_t>(n);
            }
        }
    }
    ::close(ffd);
}

static void fs_handle(AimlHttpRequest* req, AimlHttpResponse* resp,
                       const RouteEntry& route) {
    std::string req_path = req->path ? req->path : "/";
    // URL-decode BEFORE the traversal check so percent-encoded escapes
    // (e.g. %2e%2e%2f = "../", %00 = NUL) cannot slip past path_is_safe.
    std::string rel = url_decode(strip_url_prefix(route.fs_url_prefix, req_path));
    if (!path_is_safe(rel)) {
        resp->status = 400;
        resp->body   = xstrdup("Bad Request");
        send_response(resp);
        return;
    }
    const char* range = aiml_http_map_get(req->req_headers, "range");

    // Try each directory in order, first hit wins.
    for (const auto& dir : route.fs_dirs) {
        std::string found = resolve_under(dir, rel, route.fs_index);
        if (!found.empty()) {
            serve_file(resp, found, route, range);
            return;
        }
    }

    // SPA fallback: serve the index from the first directory.
    if (route.fs_spa && !route.fs_dirs.empty()) {
        std::string found =
            resolve_under(route.fs_dirs.front(), "", route.fs_index);
        if (!found.empty()) {
            serve_file(resp, found, route, range);
            return;
        }
    }

    resp->status = 404;
    resp->body   = xstrdup("Not Found");
    send_response(resp);
}

/* ────────────────────────────────────────────────────────────────
 * Per-connection handler thread
 * ──────────────────────────────────────────────────────────────── */

struct ConnCtx {
    int             fd;
    SSL*            ssl;
    AimlHttpServer* server;
    uint32_t        peer_ip;   // network-order IPv4 of the connecting socket
};

// Decrements the live-connection counter on scope exit (both handler return paths).
struct ConnGuard {
    AimlHttpServer* srv;
    ~ConnGuard() { srv->active_conns.fetch_sub(1, std::memory_order_relaxed); }
};

// Non-blocking SSL_accept bounded by a total deadline (spec/32_2 §3.10).
static bool tls_accept_deadline(SSL* ssl, int fd, int timeout_ms) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    bool ok = false;
    for (;;) {
        ERR_clear_error();
        int r = SSL_accept(ssl);
        if (r == 1) { ok = true; break; }
        int e = SSL_get_error(ssl, r);
        short ev = e == SSL_ERROR_WANT_READ ? POLLIN : e == SSL_ERROR_WANT_WRITE ? POLLOUT : 0;
        if (!ev) break;
        auto rem = std::chrono::duration_cast<std::chrono::milliseconds>(
            end - std::chrono::steady_clock::now()).count();
        struct pollfd pfd = { fd, ev, 0 };
        if (rem <= 0 || ::poll(&pfd, 1, (int)rem) <= 0) break;
    }
    fcntl(fd, F_SETFL, fl);
    return ok;
}

static void* handle_connection(void* arg) {
    ConnCtx* ctx = static_cast<ConnCtx*>(arg);
    int fd = ctx->fd;
    SSL* ssl = ctx->ssl;
    AimlHttpServer* srv = ctx->server;
    uint32_t peer_ip = ctx->peer_ip;
    delete ctx;
    ConnGuard conn_guard{srv};   // decrements active_conns on every return path

    // Per-recv read timeout on the connection socket: a client that opens a
    // connection and then stalls will not pin this handler thread forever.
    int to_secs = srv->read_timeout_secs > 0 ? srv->read_timeout_secs : 15;
    struct timeval rtv = { to_secs, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));

    // TLS handshake here, not on the accept loop: a silent client only stalls its own thread.
    if (srv->ssl_ctx) {
        ssl = SSL_new(srv->ssl_ctx);
        SSL_set_fd(ssl, fd);
        if (!tls_accept_deadline(ssl, fd, to_secs * 1000)) {
            SSL_free(ssl); ::close(fd);
            return nullptr;
        }
    }

    AimlHttpRequest* req = parse_http_request(fd, ssl, to_secs, srv->max_body_bytes);

    // Record the trusted socket peer IP (dotted) so req.client_ip() never has to
    // trust a spoofable X-Forwarded-For header. struct in_addr wants network order.
    {
        struct in_addr ina; ina.s_addr = peer_ip;
        req->peer_ip = xstrdup(inet_ntoa(ina));
    }

    // Parser limit hit → send a clean protocol error and skip dispatch entirely.
    if (req->reject_status != 0) {
        AimlHttpResponse* er = static_cast<AimlHttpResponse*>(calloc(1, sizeof(AimlHttpResponse)));
        er->status  = req->reject_status;
        er->headers = aiml_http_map_new();
        er->conn_fd = fd;
        er->ssl     = ssl;
        // Security/default headers apply to parser-reject errors too (consistency).
        if (srv->default_headers) {
            for (int i = 0; i < srv->default_headers->capacity; ++i) {
                if (!srv->default_headers->occupied[i]) continue;
                aiml_http_map_put(er->headers, srv->default_headers->keys[i],
                                  srv->default_headers->values[i]);
            }
        }
        const char* msg = req->reject_status == 413 ? "Payload Too Large"
                        : req->reject_status == 431 ? "Request Header Fields Too Large"
                        : req->reject_status == 414 ? "URI Too Long"
                        : req->reject_status == 408 ? "Request Timeout"
                        : "Bad Request";
        er->body = xstrdup(msg);
        send_response(er);
        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
        ::close(fd);
        free_response(er);
        free_request(req);
        return nullptr;
    }

    // Find matching route
    RouteEntry* matched = nullptr;
    for (auto& re : srv->routes) {
        if (re.method != "*" && re.method != req->method) continue;
        // Full pattern = prefix + pattern
        std::string full_pat = re.prefix + re.pattern;
        if (full_pat.empty()) full_pat = "/";
        if (match_pattern(full_pat, req->path ? req->path : "/", req->params)) {
            matched = &re;
            break;
        }
    }

    // Build response
    AimlHttpResponse* resp = static_cast<AimlHttpResponse*>(calloc(1, sizeof(AimlHttpResponse)));
    resp->status  = 200;
    resp->headers = aiml_http_map_new();
    resp->conn_fd = fd;
    resp->ssl     = ssl;
    if (const char* rng = aiml_http_map_get(req->req_headers, "range"))
        resp->req_range = xstrdup(rng);

    // Apply default server headers
    if (srv->default_headers) {
        for (int i = 0; i < srv->default_headers->capacity; ++i) {
            if (!srv->default_headers->occupied[i]) continue;
            aiml_http_map_put(resp->headers,
                srv->default_headers->keys[i],
                srv->default_headers->values[i]);
        }
    }

    // Run global middlewares
    for (void* mw : srv->global_mw) {
        if (resp->resp_sent) break;
        ((AimlHandlerFn)mw)(req, resp);
    }

    if (!resp->resp_sent && matched) {
        // Run group middleware
        if (matched->group_mw) {
            if (!resp->resp_sent)
                ((AimlHandlerFn)matched->group_mw)(req, resp);
        }
        // Run handler — filesystem route or regular function pointer.
        if (!resp->resp_sent) {
            if (matched->is_fs) {
                fs_handle(req, resp, *matched);
            } else if (matched->handler) {
                ((AimlHandlerFn)matched->handler)(req, resp);
            }
        }
    }

    // Auto-send 404 or 500 if nothing sent
    if (!resp->resp_sent) {
        resp->status = matched ? 500 : 404;
        resp->body   = matched ? xstrdup("Internal Server Error")
                               : xstrdup("Not Found");
        send_response(resp);
    }

    // Close connection — graceful half-close pattern. Without this,
    // ::close(fd) on a socket where the kernel receive queue still
    // contains any unread bytes (e.g. trailing newlines / pipelined
    // bytes from HTTP/1.1 clients) triggers a TCP RST instead of FIN.
    // RST mid-stream aborts the peer's fetch body reader — visible in
    // browsers as "Unexpected error while handling …" when downloading
    // large response bodies. curl is tolerant of this and works
    // anyway; Firefox/Chrome fetch streams are not.
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    ::shutdown(fd, SHUT_WR);
    {
        struct timeval tv = {1, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        char drain[256];
        while (::recv(fd, drain, sizeof(drain), 0) > 0) { /* discard */ }
    }
    ::close(fd);

    free_request(req);
    free_response(resp);
    return nullptr;
}

/* ────────────────────────────────────────────────────────────────
 * http_server_handle API
 * ──────────────────────────────────────────────────────────────── */

// Minimal one-shot status line for connections rejected before dispatch (plain
// HTTP only — on a TLS listener the client speaks TLS first, so we just close).
static void send_raw_status(int fd, int code, const char* reason) {
    char buf[160];
    int n = snprintf(buf, sizeof(buf),
        "HTTP/1.1 %d %s\r\nConnection: close\r\nContent-Length: 0\r\n\r\n", code, reason);
    if (n > 0) ::send(fd, buf, static_cast<size_t>(n), MSG_NOSIGNAL);
}

// Per-IP fixed-window rate limit. Returns true if the connection is allowed.
// Prunes stale windows when the table grows, so unique-IP floods stay bounded.
static bool rate_allow(AimlHttpServer* srv, uint32_t ip) {
    if (srv->rate_max <= 0) return true;
    time_t now = time(nullptr);
    std::lock_guard<std::mutex> g(srv->rate_mtx);
    if (srv->rate_map.size() > 4096) {
        for (auto it = srv->rate_map.begin(); it != srv->rate_map.end(); ) {
            if (now - it->second.first >= srv->rate_window_secs) it = srv->rate_map.erase(it);
            else ++it;
        }
    }
    auto& e = srv->rate_map[ip];
    if (now - e.first >= srv->rate_window_secs) { e.first = now; e.second = 0; }
    e.second++;
    return e.second <= srv->rate_max;
}

static void* accept_loop(void* arg) {
    AimlHttpServer* srv = static_cast<AimlHttpServer*>(arg);
    while (srv->running) {
        struct sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        int cfd = ::accept4(srv->listen_fd, reinterpret_cast<sockaddr*>(&addr), &len, SOCK_CLOEXEC);
        if (cfd < 0) {
            if (srv->running) continue;
            break;
        }
        uint32_t peer_ip = addr.sin_addr.s_addr;

        // Per-IP rate limit — reject before spending a thread or TLS handshake.
        if (!rate_allow(srv, peer_ip)) {
            if (!srv->ssl_ctx) send_raw_status(cfd, 429, "Too Many Requests");
            ::close(cfd);
            fprintf(stderr, "[http] rate-limit: rejected %s (>%d/%ds)\n",
                    inet_ntoa(addr.sin_addr), srv->rate_max, srv->rate_window_secs);
            continue;
        }
        // Concurrent-connection cap — bound thread/resource growth under load.
        if (srv->max_conns > 0 &&
            srv->active_conns.load(std::memory_order_relaxed) >= srv->max_conns) {
            if (!srv->ssl_ctx) send_raw_status(cfd, 503, "Service Unavailable");
            ::close(cfd);
            fprintf(stderr, "[http] conn-cap: rejected %s (active=%d max=%d)\n",
                    inet_ntoa(addr.sin_addr),
                    srv->active_conns.load(std::memory_order_relaxed), srv->max_conns);
            continue;
        }

        srv->active_conns.fetch_add(1, std::memory_order_relaxed);
        ConnCtx* c = new ConnCtx{cfd, nullptr, srv, peer_ip};
        pthread_t tid;
        pthread_create(&tid, nullptr, handle_connection, c);
        pthread_detach(tid);
    }
    return nullptr;
}

extern "C" void* aiml_http_server(const char* host, int32_t port) {
    signal(SIGPIPE, SIG_IGN);
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return nullptr;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = host && strcmp(host, "0.0.0.0") != 0
                            ? inet_addr(host) : INADDR_ANY;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        ::listen(fd, 128) < 0) {
        ::close(fd); return nullptr;
    }
    AimlHttpServer* srv = new AimlHttpServer();
    srv->listen_fd       = fd;
    srv->default_headers = aiml_http_map_new();
    return srv;
}

extern "C" void* aiml_http_server_tls(const char* host, int32_t port,
                                       const char* cert_path, const char* key_path) {
    AimlHttpServer* srv = static_cast<AimlHttpServer*>(aiml_http_server(host, port));
    if (!srv) return nullptr;
    // Modern, thread-safe one-time OpenSSL init (mirrors tls_runtime.cpp).
    static std::once_flag s_http_ssl_init;
    std::call_once(s_http_ssl_init, []{
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
    });
    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) { delete srv; return nullptr; }
    // Hardened server defaults shared with the WSS server (see tls_runtime.cpp):
    // min TLS 1.2, no SSLv2/3/TLS1.0/1.1, no compression, server cipher preference.
    aiml_tls_harden_server_ctx(ctx);
    if (SSL_CTX_use_certificate_chain_file(ctx, cert_path) <= 0 ||
        SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) <= 0) {
        SSL_CTX_free(ctx); delete srv; return nullptr;
    }
    srv->ssl_ctx = ctx;
    return srv;
}

// Enable mutual-TLS on an HTTPS server: require every client to present a
// certificate signed by the CA bundle at `ca_path` (PEM). Must be called on a
// server created via aiml_http_server_tls (an SSL_CTX must already exist).
// Returns 0 on success, -1 if the server is non-TLS or the CA cannot be loaded.
extern "C" int aiml_http_server_require_client_cert(void* srv_ptr, const char* ca_path) {
    AimlHttpServer* s = static_cast<AimlHttpServer*>(srv_ptr);
    if (!s || !s->ssl_ctx) return -1;
    return aiml_tls_require_client_cert(s->ssl_ctx, ca_path);
}

extern "C" void aiml_http_server_route(void* srv_ptr, const char* method,
                                        const char* pattern, void* handler_fn) {
    AimlHttpServer* s = static_cast<AimlHttpServer*>(srv_ptr);
    if (!s || !handler_fn) return;
    RouteEntry re;
    re.method   = method ? method : "*";
    re.pattern  = pattern ? pattern : "/";
    re.handler  = handler_fn;
    re.group_mw = nullptr;
    s->routes.push_back(re);
}

extern "C" void aiml_http_server_use(void* srv_ptr, void* mw_fn) {
    AimlHttpServer* s = static_cast<AimlHttpServer*>(srv_ptr);
    if (!s || !mw_fn) return;
    s->global_mw.push_back(mw_fn);
}

extern "C" void* aiml_http_server_group(void* srv_ptr, const char* prefix,
                                         void* mw_fn) {
    AimlHttpServer* s = static_cast<AimlHttpServer*>(srv_ptr);
    if (!s) return nullptr;
    AimlHttpRoute* r = new AimlHttpRoute();
    r->server     = s;
    r->prefix     = prefix ? prefix : "";
    r->middleware = mw_fn;
    return r;
}

extern "C" void aiml_http_server_set_header(void* srv_ptr, const char* name, const char* val) {
    AimlHttpServer* s = static_cast<AimlHttpServer*>(srv_ptr);
    if (!s || !name) return;
    aiml_http_map_put(s->default_headers, name, val);
}

// Configure the request-read timeout (seconds): the per-recv socket timeout AND
// the total request-read deadline (slowloris defence).  <= 0 keeps the default.
extern "C" void aiml_http_server_set_timeout(void* srv_ptr, int32_t secs) {
    AimlHttpServer* s = static_cast<AimlHttpServer*>(srv_ptr);
    if (!s || secs <= 0) return;
    s->read_timeout_secs = secs;
}

// Configure the request body cap in bytes (default 8 MiB). Values above
// INT32_MAX-1 are clamped because AIML buffers carry an i32 size. <= 0 keeps
// the current cap.
extern "C" void aiml_http_server_set_max_body(void* srv_ptr, int64_t bytes) {
    AimlHttpServer* s = static_cast<AimlHttpServer*>(srv_ptr);
    if (!s || bytes <= 0) return;
    const int64_t cap = static_cast<int64_t>(INT32_MAX) - 1;
    s->max_body_bytes = static_cast<long>(bytes > cap ? cap : bytes);
}

// Cap concurrent handler threads. n <= 0 → unlimited. Over the cap, new
// connections are rejected (503 on plain HTTP, immediate close on TLS) without
// spawning a thread — bounds resource growth under load / connection floods.
extern "C" void aiml_http_server_set_max_connections(void* srv_ptr, int32_t n) {
    AimlHttpServer* s = static_cast<AimlHttpServer*>(srv_ptr);
    if (!s) return;
    s->max_conns = n > 0 ? n : 0;
}

// Per-IP fixed-window rate limit: at most max_per_window connections per
// window_secs from one IP; excess is rejected (429 on plain HTTP) before any
// thread/TLS work. max_per_window <= 0 disables it; window_secs <= 0 → 60s.
extern "C" void aiml_http_server_set_rate_limit(void* srv_ptr,
                                                 int32_t max_per_window, int32_t window_secs) {
    AimlHttpServer* s = static_cast<AimlHttpServer*>(srv_ptr);
    if (!s) return;
    s->rate_max          = max_per_window > 0 ? max_per_window : 0;
    s->rate_window_secs  = window_secs > 0 ? window_secs : 60;
}

// Enable a conservative bundle of browser-facing security response headers on every
// response (opt-in, since e.g. X-Frame-Options can break legitimate embedding).
// hsts != 0 also adds Strict-Transport-Security (only meaningful behind HTTPS/TLS).
// Existing values set via set_header win (put-if-absent), so callers can override.
extern "C" void aiml_http_server_secure_headers(void* srv_ptr, int32_t hsts) {
    AimlHttpServer* s = static_cast<AimlHttpServer*>(srv_ptr);
    if (!s) return;
    if (!s->default_headers) s->default_headers = aiml_http_map_new();
    auto put_if_absent = [&](const char* k, const char* v) {
        if (!aiml_http_map_get(s->default_headers, k))
            aiml_http_map_put(s->default_headers, k, v);
    };
    put_if_absent("X-Content-Type-Options", "nosniff");
    put_if_absent("X-Frame-Options",        "DENY");
    put_if_absent("Referrer-Policy",        "no-referrer");
    if (hsts)
        put_if_absent("Strict-Transport-Security", "max-age=31536000; includeSubDomains");
}

extern "C" void aiml_http_server_start(void* srv_ptr) {
    AimlHttpServer* s = static_cast<AimlHttpServer*>(srv_ptr);
    if (!s || s->listen_fd < 0) return;
    s->running = 1;
    accept_loop(s); // blocking
}

extern "C" void* aiml_http_server_start_async(void* srv_ptr) {
    // Returns a thread_handle* — matching layout {i64 tid, i8 joined, i8 detached}
    struct ThreadHandle { int64_t tid; int8_t joined; int8_t detached; };
    AimlHttpServer* s = static_cast<AimlHttpServer*>(srv_ptr);
    if (!s) return nullptr;
    s->running = 1;
    ThreadHandle* h = static_cast<ThreadHandle*>(calloc(1, sizeof(ThreadHandle)));
    pthread_t tid;
    pthread_create(&tid, nullptr, accept_loop, s);
    s->accept_tid = tid;
    h->tid = static_cast<int64_t>(tid);
    return h;
}

extern "C" void aiml_http_server_stop(void* srv_ptr) {
    AimlHttpServer* s = static_cast<AimlHttpServer*>(srv_ptr);
    if (!s) return;
    s->running = 0;
    ::shutdown(s->listen_fd, SHUT_RDWR);
    ::close(s->listen_fd);
    s->listen_fd = -1;
}

// Build a RouteEntry for filesystem serving from prefix/dirs/opts.
// dirs_list is an AimlList of `path` (char* per entry).
static RouteEntry build_fs_route(const char* prefix, void* dirs_list,
                                  void* opts, const std::string& group_prefix,
                                  void* group_mw) {
    struct AimlList { int32_t size; int32_t capacity; void* data; };
    RouteEntry re;
    re.method   = "GET";
    re.is_fs    = true;
    re.handler  = nullptr;
    re.group_mw = group_mw;
    re.prefix   = group_prefix;
    re.fs_url_prefix = group_prefix + (prefix ? prefix : "/");

    // Route pattern: prefix + /* (catch-all under prefix)
    std::string pat = prefix ? prefix : "/";
    if (pat.empty()) pat = "/";
    if (pat.back() == '/') pat += "*";
    else pat += "/*";
    re.pattern = pat;

    // Copy directory list
    if (dirs_list) {
        AimlList* l = static_cast<AimlList*>(dirs_list);
        char** items = static_cast<char**>(l->data);
        for (int32_t i = 0; i < l->size; ++i) {
            if (items && items[i]) re.fs_dirs.emplace_back(items[i]);
        }
    }

    // Copy fs_options
    if (opts) {
        AimlFsOptions* o = static_cast<AimlFsOptions*>(opts);
        if (o->index && *o->index) re.fs_index = o->index;
        re.fs_spa = (o->spa != 0);
        re.fs_cache_max_age = o->cache_max_age;
    }
    return re;
}

extern "C" void aiml_http_server_filesystem(void* srv_ptr, const char* prefix,
                                              void* dirs_list, void* opts) {
    AimlHttpServer* s = static_cast<AimlHttpServer*>(srv_ptr);
    if (!s) return;
    RouteEntry re = build_fs_route(prefix, dirs_list, opts,
                                     /*group_prefix=*/"", /*group_mw=*/nullptr);
    s->routes.push_back(re);
}

/* ────────────────────────────────────────────────────────────────
 * http_route_handle API
 * ──────────────────────────────────────────────────────────────── */

extern "C" void aiml_http_route_route(void* route_ptr, const char* method,
                                       const char* pattern, void* handler_fn) {
    AimlHttpRoute* r = static_cast<AimlHttpRoute*>(route_ptr);
    if (!r || !r->server || !handler_fn) return;
    RouteEntry re;
    re.method   = method ? method : "*";
    re.pattern  = pattern ? pattern : "/";
    re.handler  = handler_fn;
    re.group_mw = r->middleware;
    re.prefix   = r->prefix;
    r->server->routes.push_back(re);
}

extern "C" void aiml_http_route_use(void* route_ptr, void* mw_fn) {
    AimlHttpRoute* r = static_cast<AimlHttpRoute*>(route_ptr);
    if (!r || !r->server) return;
    r->server->global_mw.push_back(mw_fn); // add to global for now
}

extern "C" void* aiml_http_route_group(void* route_ptr, const char* prefix,
                                        void* mw_fn) {
    AimlHttpRoute* r = static_cast<AimlHttpRoute*>(route_ptr);
    if (!r) return nullptr;
    AimlHttpRoute* sub = new AimlHttpRoute();
    sub->server     = r->server;
    sub->prefix     = r->prefix + (prefix ? prefix : "");
    sub->middleware = mw_fn;
    return sub;
}

extern "C" void aiml_http_route_filesystem(void* route_ptr, const char* prefix,
                                             void* dirs_list, void* opts) {
    AimlHttpRoute* r = static_cast<AimlHttpRoute*>(route_ptr);
    if (!r || !r->server) return;
    RouteEntry re = build_fs_route(prefix, dirs_list, opts,
                                     r->prefix, r->middleware);
    r->server->routes.push_back(re);
}

/* ────────────────────────────────────────────────────────────────
 * fs_options
 * ──────────────────────────────────────────────────────────────── */

extern "C" void* aiml_fs_options_new(void) {
    AimlFsOptions* o = static_cast<AimlFsOptions*>(calloc(1, sizeof(AimlFsOptions)));
    o->index         = xstrdup("index.html");
    o->spa           = 0;
    o->cache_max_age = 0;
    return o;
}

extern "C" void aiml_fs_options_set_index(void* opts, const char* index) {
    if (!opts) return;
    AimlFsOptions* o = static_cast<AimlFsOptions*>(opts);
    free(o->index);
    o->index = xstrdup(index);
}

extern "C" void aiml_fs_options_set_spa(void* opts, int8_t spa) {
    if (!opts) return;
    static_cast<AimlFsOptions*>(opts)->spa = spa;
}

extern "C" void aiml_fs_options_set_cache_max_age(void* opts, int32_t max_age) {
    if (!opts) return;
    static_cast<AimlFsOptions*>(opts)->cache_max_age = max_age;
}

/* ────────────────────────────────────────────────────────────────
 * http_response_handle setters (server-side)
 * ──────────────────────────────────────────────────────────────── */

extern "C" void aiml_http_resp_set_status(void* resp, int32_t status) {
    if (!resp) return;
    AimlHttpResponse* r = static_cast<AimlHttpResponse*>(resp);
    r->status = status;
    r->ok     = (status >= 200 && status < 300) ? 1 : 0;
}

extern "C" void aiml_http_resp_set_body(void* resp, const char* body) {
    if (!resp) return;
    AimlHttpResponse* r = static_cast<AimlHttpResponse*>(resp);
    free(r->body);
    r->body = xstrdup(body);
}

// A cookie name/value must not carry control chars or the delimiters we use to
// build the header — otherwise an attacker-controlled value could inject extra
// headers (HTTP response splitting) or forge attributes.
static bool cookie_token_safe(const char* s) {
    if (!s) return false;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        if (*p < 0x20 || *p == 0x7f) return false;   // CTLs incl. CR/LF
        if (*p == ';' || *p == ',' || *p == ' ')     return false;   // cookie delimiters
    }
    return true;
}

// resp.set_cookie(name, value, max_age, same_site): append a Set-Cookie header with
// secure-by-default attributes — Path=/, HttpOnly, Secure, SameSite (default "Lax").
// max_age >= 0 → persistent cookie with Max-Age; max_age < 0 → session cookie.
// Rejects (no-op) names/values that would allow header injection.
extern "C" void aiml_http_resp_set_cookie(void* resp, const char* name, const char* value,
                                          int32_t max_age, const char* same_site) {
    AimlHttpResponse* r = static_cast<AimlHttpResponse*>(resp);
    if (!r || !cookie_token_safe(name) || !cookie_token_safe(value)) return;

    const char* ss = (same_site && *same_site) ? same_site : "Lax";
    // Only allow the three valid SameSite tokens; anything else falls back to Lax.
    if (strcmp(ss, "Strict") != 0 && strcmp(ss, "Lax") != 0 && strcmp(ss, "None") != 0)
        ss = "Lax";

    std::string c = std::string(name) + "=" + value + "; Path=/; HttpOnly; Secure; SameSite=" + ss;
    if (max_age >= 0) { c += "; Max-Age="; c += std::to_string(max_age); }

    char** grown = static_cast<char**>(
        realloc(r->set_cookies, sizeof(char*) * (r->n_set_cookies + 1)));
    if (!grown) return;
    r->set_cookies = grown;
    r->set_cookies[r->n_set_cookies++] = xstrdup(c.c_str());
}

extern "C" void aiml_http_resp_set_body_bytes(void* resp, void* buf) {
    if (!resp) return;
    static_cast<AimlHttpResponse*>(resp)->body_bytes = buf;
}

extern "C" void* aiml_http_resp_get_headers(void* resp) {
    if (!resp) return nullptr;
    return static_cast<AimlHttpResponse*>(resp)->headers;
}

/* ────────────────────────────────────────────────────────────────
 * http_request_handle getters
 * ──────────────────────────────────────────────────────────────── */

// method/path/body: return refcounted copies (aiml_str_dup) so they are lifetime-
// safe if referenced past the request's own free — consistent with aiml_http_resp_body.
// NIE nullptr: ein aiml-`string` hat fuer "nichts da" den LEEREN String.  Eine
// Anfrage ohne Content-Length (jedes GET) liess `req->body` null, und ein voellig
// korrektes `- set: [body, req.body]` starb dann in strlen(nullptr).
extern "C" char* aiml_http_req_method(void* req) {
    const char* v = req ? static_cast<AimlHttpRequest*>(req)->method : nullptr;
    return aiml_str_dup(v ? v : "");
}

extern "C" char* aiml_http_req_path(void* req) {
    const char* v = req ? static_cast<AimlHttpRequest*>(req)->path : nullptr;
    return aiml_str_dup(v ? v : "");
}

extern "C" char* aiml_http_req_body(void* req) {
    const char* v = req ? static_cast<AimlHttpRequest*>(req)->body : nullptr;
    return aiml_str_dup(v ? v : "");
}

// req.body_bytes — binary-safe copy of the raw request body as a fresh
// AimlBuffer header { i32 size, i32 cap, i32 rIdx, i32 wIdx, char* data }.
// The caller copies the header by value and frees it; `data` is owned by the
// resulting AIML buffer<u8>. An empty body yields a zero-length buffer.
extern "C" void* aiml_http_req_body_bytes(void* req) {
    struct AimlBuf { int32_t sz, cap, rIdx, wIdx; char* data; };
    AimlHttpRequest* r = static_cast<AimlHttpRequest*>(req);
    int64_t len = (r && r->body) ? r->body_len : 0;
    if (len < 0 || len > INT32_MAX - 1) len = 0;
    AimlBuf* b = static_cast<AimlBuf*>(calloc(1, sizeof(AimlBuf)));
    if (!b) return nullptr;
    b->data = static_cast<char*>(malloc(static_cast<size_t>(len) + 1));
    if (!b->data) { free(b); return nullptr; }
    if (len > 0) memcpy(b->data, r->body, static_cast<size_t>(len));
    b->data[len] = '\0';
    b->sz   = static_cast<int32_t>(len);
    b->cap  = static_cast<int32_t>(len);
    b->wIdx = static_cast<int32_t>(len);
    return b;
}

extern "C" void* aiml_http_req_url(void* req) {
    return req ? static_cast<AimlHttpRequest*>(req)->url : nullptr;
}

extern "C" void* aiml_http_req_params(void* req) {
    return req ? static_cast<AimlHttpRequest*>(req)->params : nullptr;
}

extern "C" void* aiml_http_req_cookies(void* req) {
    return req ? static_cast<AimlHttpRequest*>(req)->cookies : nullptr;
}

/* ────────────────────────────────────────────────────────────────
 * http_client_handle setters
 * ──────────────────────────────────────────────────────────────── */

extern "C" void aiml_http_client_set_body(void* client, const char* body) {
    if (!client) return;
    AimlHttpClient* c = static_cast<AimlHttpClient*>(client);
    free(c->body);
    c->body = xstrdup(body);
}

extern "C" void aiml_http_client_set_timeout(void* client, int32_t timeout_ms) {
    if (!client) return;
    static_cast<AimlHttpClient*>(client)->timeout = timeout_ms;
}

extern "C" void aiml_http_client_set_stream(void* client, int8_t stream) {
    if (!client) return;
    static_cast<AimlHttpClient*>(client)->stream = stream;
}

extern "C" void* aiml_http_client_get_headers(void* client) {
    if (!client) return nullptr;
    return static_cast<AimlHttpClient*>(client)->headers;
}
