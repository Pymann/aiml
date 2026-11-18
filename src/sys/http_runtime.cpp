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

// ── memV3 (spec/61 §4/§5) views of the request/url string tables ────────────
// The parser keeps params/query/cookies in an AimlHttpMap (open addressing,
// libc-strdup'd strings, no [rc][pad] header) and url parts as libc strings.
// Under memV3 an AIML map<string:string> is a refcounted HANDLE with
// aiml-managed strings, and a string local is managed too: handing out the raw
// table makes the caller's incref/drop touch 8 bytes before the block (heap
// corruption, valgrind "Invalid write"). These helpers build the managed form —
// same approach as sys.env (mem_runtime.cpp).
extern "C" void* aiml_map_handle_new(void);

// Fresh map<string:string> handle (rc=1, dense store) copying an AimlHttpMap.
extern "C" void* aiml_http_map_to_handle(void* src_ptr) {
    void* handle = aiml_map_handle_new();
    if (!handle) return handle;
    AimlHttpMap* dst = static_cast<AimlHttpMap*>(handle);   // AimlHttpMap == AimlMapRaw layout
    AimlHttpMap* src = static_cast<AimlHttpMap*>(src_ptr);
    if (!src || src->size <= 0 || !src->keys) return handle;
    int32_t n = src->size;
    dst->keys     = static_cast<char**>(calloc((size_t)n, sizeof(char*)));
    dst->values   = static_cast<char**>(calloc((size_t)n, sizeof(char*)));
    dst->occupied = nullptr;                                 // dense store
    dst->capacity = n;
    if (!dst->keys || !dst->values) { dst->size = 0; return handle; }
    int32_t idx = 0;
    for (int32_t i = 0; i < src->capacity && idx < n; ++i) {
        bool used = src->occupied ? src->occupied[i] != 0 : i < src->size;
        if (!used || !src->keys[i]) continue;
        dst->keys[idx]   = aiml_str_dup(src->keys[i]);
        dst->values[idx] = aiml_str_dup(src->values[i] ? src->values[i] : "");
        ++idx;
    }
    dst->size = idx;
    return handle;
}

// Managed copy of a libc string; nullptr stays nullptr (absent stays null).
extern "C" char* aiml_http_str_managed(const char* s) {
    return s ? aiml_str_dup(s) : nullptr;
}

// Managed copy of a freshly malloc'd libc string, which is freed (adopted).
extern "C" char* aiml_http_str_adopt(char* s) {
    if (!s) return nullptr;
    char* managed = aiml_str_dup(s);
    free(s);
    return managed;
}
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
            // Writable also means "the connect FAILED" (refused, unreachable):
            // only SO_ERROR == 0 is a connection.  Without this check a refused
            // port produced an empty answer that read as HTTP 200.
            int soerr = 0; socklen_t soerr_len = sizeof(soerr);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &soerr_len) != 0 || soerr != 0) {
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

/* ────────────────────────────────────────────────────────────────
 * HTTP/1.1 client (spec/32_2 §2.3a): proxy from the environment, redirects,
 * a 2xx body streamed into a file.  One exchange per connection
 * (`Connection: close`).
 * ──────────────────────────────────────────────────────────────── */

extern "C" void aiml_http_resp_free(void* resp);

// Why the last client request on this thread got no answer ("" = it did).
static thread_local std::string g_http_client_error;

static void client_fail(const std::string& why) { g_http_client_error = why; }

// Where one request goes.
struct HttpTarget {
    bool        tls = false;
    std::string host;
    int         port = 80;
    bool        explicit_port = false;
    std::string target = "/";      // origin-form: path + query
    std::string user, pass;        // userinfo (used for proxy URLs only)
};

static HttpTarget target_of(const AimlUrl* u) {
    HttpTarget t;
    t.tls  = u->protocol && strcmp(u->protocol, "https") == 0;
    t.host = u->host ? u->host : "";
    t.explicit_port = u->port != 0;
    t.port = u->port ? u->port : (t.tls ? 443 : 80);
    t.target = (u->path && *u->path) ? u->path : "/";
    if (u->get_param && u->get_param->size > 0) {
        bool first = true;
        for (int i = 0; i < u->get_param->capacity; ++i) {
            if (!u->get_param->occupied[i]) continue;
            t.target += first ? '?' : '&';
            first = false;
            t.target += u->get_param->keys[i];
            t.target += '=';
            t.target += u->get_param->values[i];
        }
    }
    return t;
}

static std::string host_header(const HttpTarget& t) {
    bool dflt = (t.tls && t.port == 443) || (!t.tls && t.port == 80);
    return dflt ? t.host : t.host + ":" + std::to_string(t.port);
}

static std::string origin_of(const HttpTarget& t) {
    return std::string(t.tls ? "https://" : "http://") + host_header(t);
}

// "scheme://[user[:pass]@]host[:port][/path[?query]]" → t.  false = not an absolute http(s) URL.
static bool parse_absolute(const std::string& text, HttpTarget& t) {
    size_t sep = text.find("://");
    if (sep == std::string::npos) return false;
    std::string scheme = str_tolower(text.substr(0, sep));
    if (scheme != "http" && scheme != "https") return false;
    t = HttpTarget();
    t.tls = scheme == "https";
    std::string rest = text.substr(sep + 3);
    size_t slash = rest.find_first_of("/?#");
    std::string auth = rest.substr(0, slash);
    std::string tail = slash == std::string::npos ? "/" : rest.substr(slash);
    tail = tail.substr(0, tail.find('#'));
    if (tail.empty() || tail[0] == '?') tail = "/" + tail;
    t.target = tail;
    size_t at = auth.rfind('@');
    if (at != std::string::npos) {
        std::string info = auth.substr(0, at);
        auth = auth.substr(at + 1);
        size_t colon = info.find(':');
        t.user = url_decode(info.substr(0, colon));
        t.pass = colon == std::string::npos ? "" : url_decode(info.substr(colon + 1));
    }
    t.port = t.tls ? 443 : 80;
    std::string port_text;
    if (!auth.empty() && auth[0] == '[') {                 // [v6]:port
        size_t close = auth.find(']');
        if (close == std::string::npos) return false;
        t.host = auth.substr(1, close - 1);
        if (close + 1 < auth.size() && auth[close + 1] == ':') port_text = auth.substr(close + 2);
    } else {
        size_t colon = auth.rfind(':');
        t.host = auth.substr(0, colon);
        if (colon != std::string::npos) port_text = auth.substr(colon + 1);
    }
    if (!port_text.empty()) {
        t.port = atoi(port_text.c_str());
        t.explicit_port = true;
    }
    return !t.host.empty() && t.port > 0 && t.port < 65536;
}

/* ── proxy selection (spec/32_5 §6.1, the environment part) ────────────── */

struct ProxyChoice {
    bool        use = false;
    std::string host;
    int         port = 0;
    std::string auth;      // "Basic …" or ""
};

static std::string env_first(const char* upper, const char* lower) {
    const char* v = getenv(upper);
    if (!v || !*v) v = getenv(lower);
    return v ? std::string(v) : std::string();
}

static std::string base64_of(const std::string& in) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t n = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8) | (uint8_t)in[i + 2];
        out += tbl[(n >> 18) & 63]; out += tbl[(n >> 12) & 63];
        out += tbl[(n >> 6) & 63];  out += tbl[n & 63];
    }
    size_t left = in.size() - i;
    if (left > 0) {
        uint32_t n = (uint8_t)in[i] << 16;
        if (left == 2) n |= (uint8_t)in[i + 1] << 8;
        out += tbl[(n >> 18) & 63]; out += tbl[(n >> 12) & 63];
        out += left == 2 ? tbl[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

static std::string trimmed(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

// One NO_PROXY entry: "*", "example.com" (also its subdomains), ".example.com",
// "*.example.com", an IP, an IPv4 CIDR "10.0.0.0/8", each optionally ":port".
static bool no_proxy_entry_matches(const std::string& raw, const std::string& host, int port) {
    std::string e = str_tolower(trimmed(raw));
    if (e.empty()) return false;
    if (e == "*") return true;
    if (e[0] == '[') {                                      // [v6] or [v6]:port
        size_t close = e.find(']');
        if (close == std::string::npos) return false;
        if (close + 1 < e.size() && e[close + 1] == ':' && atoi(e.c_str() + close + 2) != port) return false;
        e = e.substr(1, close - 1);
    } else if (std::count(e.begin(), e.end(), ':') == 1) {  // host:port (a bare v6 has more colons)
        size_t colon = e.find(':');
        if (atoi(e.c_str() + colon + 1) != port) return false;
        e = e.substr(0, colon);
    }
    std::string h = str_tolower(host);
    size_t slash = e.find('/');
    if (slash != std::string::npos) {                       // IPv4 CIDR
        struct in_addr net{}, ip{};
        if (inet_pton(AF_INET, e.substr(0, slash).c_str(), &net) != 1) return false;
        if (inet_pton(AF_INET, h.c_str(), &ip) != 1) return false;
        int bits = std::min(32, std::max(0, atoi(e.c_str() + slash + 1)));
        uint32_t mask = bits == 0 ? 0u : ~0u << (32 - bits);
        return (ntohl(net.s_addr) & mask) == (ntohl(ip.s_addr) & mask);
    }
    if (e.rfind("*.", 0) == 0) e = e.substr(1);             // "*.x" → ".x"
    bool suffix = h.size() > e.size() && h.compare(h.size() - e.size(), e.size(), e) == 0;
    if (e[0] == '.') return suffix;
    return h == e || (suffix && h[h.size() - e.size() - 1] == '.');
}

static bool no_proxy_matches(const std::string& list, const std::string& host, int port) {
    std::stringstream ss(list);
    std::string entry;
    while (std::getline(ss, entry, ','))
        if (no_proxy_entry_matches(entry, host, port)) return true;
    return false;
}

// The proxy for `t` from the client's setting:
//   NULL/"" → HTTPS_PROXY / HTTP_PROXY (by scheme), else ALL_PROXY; NO_PROXY exempts
//   "direct" → none;  any other text → exactly that proxy (the caller chose it)
// false = the setting is unusable (g_http_client_error says why).
static bool choose_proxy(const HttpTarget& t, const char* setting, ProxyChoice& out) {
    out = ProxyChoice();
    std::string spec = setting ? setting : "";
    if (spec == "direct") return true;
    if (spec.empty()) {
        spec = t.tls ? env_first("HTTPS_PROXY", "https_proxy") : env_first("HTTP_PROXY", "http_proxy");
        if (spec.empty()) spec = env_first("ALL_PROXY", "all_proxy");
        if (spec.empty() || no_proxy_matches(env_first("NO_PROXY", "no_proxy"), t.host, t.port)) return true;
    }
    if (spec.find("://") == std::string::npos) spec = "http://" + spec;
    HttpTarget p;
    // the setting may carry credentials: never quote it in an error
    if (!parse_absolute(spec, p)) { client_fail("unusable proxy setting"); return false; }
    if (p.tls) { client_fail("an https:// proxy is not supported (spec/32_5 §3.5)"); return false; }
    out.use  = true;
    out.host = p.host;
    out.port = p.explicit_port ? p.port : 1080;             // curl's default proxy port
    if (!p.user.empty()) out.auth = "Basic " + base64_of(p.user + ":" + p.pass);
    return true;
}

/* ── one connection ──────────────────────────────────────────────────── */

struct HttpConn {
    int      fd = -1;
    SSL_CTX* ctx = nullptr;
    SSL*     ssl = nullptr;
    bool     forward = false;      // plain http through a proxy: absolute-form target
    std::string proxy_auth;
    HttpConn() = default;
    HttpConn(const HttpConn&) = delete;
    HttpConn& operator=(const HttpConn&) = delete;
    ~HttpConn() {
        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
        if (ctx) SSL_CTX_free(ctx);
        if (fd >= 0) ::close(fd);
    }
};

// Status line and headers (lower-cased keys into `headers`, if given).  0 = no HTTP answer.
static int read_head(int fd, SSL* ssl, AimlHttpMap* headers) {
    char line[8192];
    recv_line(fd, ssl, line, sizeof(line));
    int status = 0;
    if (sscanf(line, "HTTP/%*s %d", &status) != 1) status = 0;
    while (true) {
        int n = recv_line(fd, ssl, line, sizeof(line));
        if (n <= 0) break;
        while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n')) line[--n] = '\0';
        if (n == 0) break;                                  // blank line: end of the head
        char* colon = strchr(line, ':');
        if (!colon || !headers) continue;
        *colon = '\0';
        const char* v = colon + 1;
        while (*v == ' ' || *v == '\t') ++v;
        aiml_http_map_put(headers, str_tolower(line).c_str(), v);
    }
    return status;
}

static bool start_tls(const HttpTarget& t, HttpConn& c) {
    SSL_library_init();
    c.ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_min_proto_version(c.ctx, TLS1_2_VERSION);
    SSL_CTX_set_default_verify_paths(c.ctx);
    const char* https_ca = getenv("AIML_WSS_CA");           // shared client-TLS CA bundle
    if (https_ca && *https_ca) SSL_CTX_load_verify_locations(c.ctx, https_ca, nullptr);
    // Verify the server cert (chain + hostname) by default → defeats active MITM.
    // Shares the client-TLS insecure toggle with the wss client
    // (mirror.tls_insecure(1) / AIML_WSS_INSECURE=1) for local dev.
    bool insecure = aiml_ws_client_insecure() != 0;
    SSL_CTX_set_verify(c.ctx, insecure ? SSL_VERIFY_NONE : SSL_VERIFY_PEER, nullptr);
    c.ssl = SSL_new(c.ctx);
    SSL_set_fd(c.ssl, c.fd);
    SSL_set_tlsext_host_name(c.ssl, t.host.c_str());
    if (!insecure) {
        SSL_set_hostflags(c.ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        SSL_set1_host(c.ssl, t.host.c_str());
    }
    if (SSL_connect(c.ssl) > 0) return true;
    long vr = SSL_get_verify_result(c.ssl);
    client_fail(t.host + ": TLS: " + (vr != X509_V_OK ? X509_verify_cert_error_string(vr) : "handshake failed"));
    return false;
}

// Connect to `t` directly, through a CONNECT tunnel (https) or a forward proxy (http).
static bool open_conn(const HttpTarget& t, const ProxyChoice& px, int timeout_ms, HttpConn& c) {
    const std::string& dial_host = px.use ? px.host : t.host;
    int dial_port = px.use ? px.port : t.port;
    c.fd = tcp_connect(dial_host.c_str(), dial_port, timeout_ms > 0 ? timeout_ms : 10000);
    if (c.fd < 0) {
        client_fail(std::string(px.use ? "proxy " : "") + dial_host + ":" + std::to_string(dial_port)
                    + ": cannot connect");
        return false;
    }
    if (timeout_ms > 0) {                                   // the same limit for every read/write
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(c.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(c.fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }
    if (px.use && !t.tls) {
        c.forward = true;
        c.proxy_auth = px.auth;
        return true;
    }
    if (px.use) {
        std::string hp = t.host + ":" + std::to_string(t.port);
        std::string req = "CONNECT " + hp + " HTTP/1.1\r\nHost: " + hp + "\r\n";
        if (!px.auth.empty()) req += "Proxy-Authorization: " + px.auth + "\r\n";
        req += "\r\n";
        int st = send_all_fd(c.fd, nullptr, req.c_str(), (int)req.size()) < 0
                 ? 0 : read_head(c.fd, nullptr, nullptr);
        if (st < 200 || st >= 300) {
            client_fail("proxy " + px.host + ":" + std::to_string(px.port) + " refused CONNECT " + hp
                        + (st ? " (HTTP " + std::to_string(st) + ")" : " (no answer)"));
            return false;
        }
    }
    return !t.tls || start_tls(t, c);
}

// Where a response body goes: memory, or a file (download_to, 2xx only).
struct ClientBody {
    std::string mem;
    FILE*       fp = nullptr;
    int64_t     written = 0;
    bool put(const char* data, size_t n) {
        if (!fp) { mem.append(data, n); return true; }
        if (fwrite(data, 1, n, fp) != n) return false;
        written += (int64_t)n;
        return true;
    }
};

// The body: chunked, Content-Length, or until the server closes.
// false = it ended early or the sink failed.
static bool read_body(HttpConn& c, AimlHttpMap* headers, ClientBody& sink) {
    const char* te = aiml_http_map_get(headers, "transfer-encoding");
    const char* cl = aiml_http_map_get(headers, "content-length");
    std::vector<char> buf(1 << 16);
    if (te && str_tolower(te).find("chunked") != std::string::npos) {
        char line[1024];
        while (true) {
            if (recv_line(c.fd, c.ssl, line, sizeof(line)) <= 0) return false;
            long long size = strtoll(line, nullptr, 16);
            if (size <= 0) {
                while (recv_line(c.fd, c.ssl, line, sizeof(line)) > 2) {}   // trailers
                return true;
            }
            while (size > 0) {
                int want = (int)std::min<long long>(size, (long long)buf.size());
                int got = recv_n(c.fd, c.ssl, buf.data(), want);
                if (got > 0 && !sink.put(buf.data(), (size_t)got)) return false;
                if (got < want) return false;
                size -= got;
            }
            char crlf[2];
            if (recv_n(c.fd, c.ssl, crlf, 2) != 2) return false;
        }
    }
    if (cl) {
        long long left = strtoll(cl, nullptr, 10);
        while (left > 0) {
            int want = (int)std::min<long long>(left, (long long)buf.size());
            int got = recv_n(c.fd, c.ssl, buf.data(), want);
            if (got > 0 && !sink.put(buf.data(), (size_t)got)) return false;
            if (got < want) return false;
            left -= got;
        }
        return true;
    }
    while (true) {                                          // neither: until the peer closes
        int got = c.ssl ? SSL_read(c.ssl, buf.data(), (int)buf.size())
                        : (int)::recv(c.fd, buf.data(), buf.size(), 0);
        if (got <= 0) return true;
        if (!sink.put(buf.data(), (size_t)got)) return false;
    }
}

struct RequestSpec {
    std::string  method = "GET";
    AimlHttpMap* headers = nullptr;
    const char*  body_str = nullptr;
    const char*  body_file = nullptr;
    int          timeout_ms = 0;
    const char*  download_to = nullptr;
    const char*  proxy = nullptr;
    int          max_redirects = 0;
};

static bool is_redirect(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

static bool send_request(HttpConn& c, const HttpTarget& t, const RequestSpec& rq, bool send_auth) {
    FILE* body_fp = nullptr;
    long long file_len = -1;
    if (rq.body_file && *rq.body_file) {
        body_fp = std::fopen(rq.body_file, "rb");
        if (!body_fp) { client_fail(std::string("cannot read the body file ") + rq.body_file); return false; }
        std::fseek(body_fp, 0, SEEK_END);
        file_len = std::ftell(body_fp);
        std::fseek(body_fp, 0, SEEK_SET);
    }
    std::string req = rq.method + " " + (c.forward ? origin_of(t) + t.target : t.target) + " HTTP/1.1\r\n";
    req += "Host: " + host_header(t) + "\r\nConnection: close\r\n";
    if (c.forward && !c.proxy_auth.empty()) req += "Proxy-Authorization: " + c.proxy_auth + "\r\n";
    if (rq.headers) {
        for (int i = 0; i < rq.headers->capacity; ++i) {
            if (!rq.headers->occupied[i]) continue;
            const char* k = rq.headers->keys[i];
            if (!send_auth && str_tolower(k) == "authorization") continue;
            req += k; req += ": "; req += rq.headers->values[i]; req += "\r\n";
        }
    }
    size_t body_len = rq.body_str ? strlen(rq.body_str) : 0;
    if (body_fp)           req += "Content-Length: " + std::to_string(file_len) + "\r\n";
    else if (body_len > 0) req += "Content-Length: " + std::to_string(body_len) + "\r\n";
    req += "\r\n";
    if (!body_fp && body_len > 0) req += rq.body_str;
    bool sent = send_all_fd(c.fd, c.ssl, req.c_str(), (int)req.size()) >= 0;
    if (body_fp) {
        std::vector<char> chunk(1 << 20);
        while (sent) {
            size_t got = std::fread(chunk.data(), 1, chunk.size(), body_fp);
            if (got > 0) sent = send_all_fd(c.fd, c.ssl, chunk.data(), (int)got) >= 0;
            if (got < chunk.size()) break;
        }
        std::fclose(body_fp);
    }
    if (!sent) client_fail(host_header(t) + ": the request could not be sent");
    return sent;
}

// The body of `r` into memory or the download file.  false = no usable answer.
static bool receive_body(HttpConn& c, const HttpTarget& t, const RequestSpec& rq, AimlHttpResponse* r) {
    ClientBody sink;
    std::string part;
    if (r->ok && rq.download_to && *rq.download_to) {
        part = std::string(rq.download_to) + ".part";
        sink.fp = std::fopen(part.c_str(), "wb");
        if (!sink.fp) { client_fail("cannot write " + part); return false; }
    }
    bool complete = read_body(c, r->headers, sink);
    if (sink.fp) {
        bool closed = std::fclose(sink.fp) == 0;
        if (complete && closed && std::rename(part.c_str(), rq.download_to) == 0) {
            r->raw_len = sink.written;
            return true;
        }
        std::remove(part.c_str());
        client_fail(host_header(t) + ": the download into " + rq.download_to + " broke off");
        return false;
    }
    if (!complete) { client_fail(host_header(t) + ": the answer broke off"); return false; }
    if (!sink.mem.empty()) {
        r->body = xstrdup(sink.mem.c_str());               // text view (stops at a NUL)
        r->raw_len = (int64_t)sink.mem.size();
        r->raw_body = static_cast<char*>(malloc(sink.mem.size()));
        if (r->raw_body) memcpy(r->raw_body, sink.mem.data(), sink.mem.size());
        else r->raw_len = 0;
    }
    return true;
}

// One request on one connection.  nullptr = no answer (g_http_client_error says why).
static AimlHttpResponse* exchange(const HttpTarget& t, const RequestSpec& rq, bool send_auth) {
    ProxyChoice px;
    if (!choose_proxy(t, rq.proxy, px)) return nullptr;
    HttpConn c;
    if (!open_conn(t, px, rq.timeout_ms, c)) return nullptr;
    if (!send_request(c, t, rq, send_auth)) return nullptr;

    AimlHttpResponse* r = static_cast<AimlHttpResponse*>(calloc(1, sizeof(AimlHttpResponse)));
    r->headers = aiml_http_map_new();
    r->conn_fd = -1;
    r->status = read_head(c.fd, c.ssl, r->headers);
    if (r->status == 0) {
        aiml_http_resp_free(r);
        client_fail(host_header(t) + ": no HTTP answer");
        return nullptr;
    }
    r->ok = (r->status >= 200 && r->status < 300) ? 1 : 0;
    r->final_url = xstrdup((origin_of(t) + t.target).c_str());
    // A response to HEAD has no body whatever Content-Length says (RFC 9110
    // §9.3.2), nor do 1xx/204/304.  A redirect that will be followed is not
    // read: the connection closes anyway.
    bool followed = is_redirect(r->status) && rq.max_redirects > 0
                    && aiml_http_map_get(r->headers, "location");
    bool bodiless = rq.method == "HEAD" || r->status < 200 || r->status == 204 || r->status == 304;
    if (!followed && !bodiless && !receive_body(c, t, rq, r)) {
        aiml_http_resp_free(r);
        return nullptr;
    }
    return r;
}

// The Location of a redirect, resolved against the URL that answered.
static HttpTarget resolve_location(const HttpTarget& from, const std::string& loc) {
    HttpTarget next;
    if (parse_absolute(loc, next)) return next;
    if (loc.rfind("//", 0) == 0 && parse_absolute((from.tls ? "https:" : "http:") + loc, next))
        return next;                                        // scheme-relative
    next = from;
    std::string path = loc.substr(0, loc.find('#'));
    std::string dir = from.target.substr(0, from.target.find('?'));
    if (!path.empty() && path[0] == '/')      next.target = path;
    else if (!path.empty() && path[0] == '?') next.target = dir + path;
    else                                      next.target = dir.substr(0, dir.rfind('/') + 1) + path;
    return next;
}

// The request and its redirects (spec/32_2 §2.3a).
static AimlHttpResponse* http_request(const AimlUrl* u, RequestSpec rq) {
    g_http_client_error.clear();
    if (!u || !u->host) { client_fail("no URL"); return nullptr; }
    HttpTarget t = target_of(u);
    const std::string first_origin = origin_of(t);
    bool send_auth = true;
    for (int hop = 0;; ++hop) {
        AimlHttpResponse* r = exchange(t, rq, send_auth);
        if (!r) return nullptr;
        const char* loc = aiml_http_map_get(r->headers, "location");
        if (!is_redirect(r->status) || !loc || rq.max_redirects <= 0) return r;
        if (hop >= rq.max_redirects) {
            aiml_http_resp_free(r);
            client_fail("more than " + std::to_string(rq.max_redirects) + " redirects");
            return nullptr;
        }
        HttpTarget next = resolve_location(t, loc);
        // 303, and 301/302 after a POST: continue with GET and without the body
        if (r->status == 303 || ((r->status == 301 || r->status == 302) && rq.method == "POST")) {
            if (rq.method != "HEAD") rq.method = "GET";
            rq.body_str = nullptr;
            rq.body_file = nullptr;
        }
        // credentials stay with the origin they were given for
        if (origin_of(next) != first_origin) send_auth = false;
        aiml_http_resp_free(r);
        t = next;
    }
}

static AimlHttpResponse* do_http_request(const char* method, AimlUrl* u, const char* body_str) {
    RequestSpec rq;
    rq.method = method;
    rq.body_str = body_str;
    rq.max_redirects = 10;
    return http_request(u, rq);
}

/* ────────────────────────────────────────────────────────────────
 * http namespace — convenience functions
 * ──────────────────────────────────────────────────────────────── */

extern "C" void* aiml_http_get(void* url) {
    return do_http_request("GET", static_cast<AimlUrl*>(url), nullptr);
}
extern "C" void* aiml_http_post(void* url, const char* body) {
    return do_http_request("POST", static_cast<AimlUrl*>(url), body);
}
extern "C" void* aiml_http_put(void* url, const char* body) {
    return do_http_request("PUT", static_cast<AimlUrl*>(url), body);
}
extern "C" void* aiml_http_delete(void* url) {
    return do_http_request("DELETE", static_cast<AimlUrl*>(url), nullptr);
}
extern "C" void* aiml_http_head(void* url) {
    return do_http_request("HEAD", static_cast<AimlUrl*>(url), nullptr);
}

/* ────────────────────────────────────────────────────────────────
 * http_client_handle
 * ──────────────────────────────────────────────────────────────── */

extern "C" void* aiml_http_new_request(const char* method, void* url) {
    AimlHttpClient* c = static_cast<AimlHttpClient*>(calloc(1, sizeof(AimlHttpClient)));
    c->headers = aiml_http_map_new();
    c->method  = xstrdup(method ? method : "GET");
    c->url     = static_cast<AimlUrl*>(url);
    c->max_redirects = 10;
    return c;
}

extern "C" void* aiml_http_client_new(const char* method, void* url) {
    return aiml_http_new_request(method, url);
}

extern "C" void* aiml_http_client_send(void* client_ptr) {
    AimlHttpClient* c = static_cast<AimlHttpClient*>(client_ptr);
    if (!c) { client_fail("no request"); return nullptr; }
    RequestSpec rq;
    rq.method        = c->method ? c->method : "GET";
    rq.headers       = c->headers;
    rq.body_str      = c->body;
    rq.body_file     = c->body_file;
    rq.timeout_ms    = c->timeout > 0 ? c->timeout : 0;
    rq.download_to   = c->download_to;
    rq.proxy         = c->proxy;
    rq.max_redirects = c->max_redirects;
    return http_request(c->url, rq);
}

// http.last_error() — why the last request on this thread got no answer
// (null response); "" after one that was answered.  Never contains credentials.
extern "C" char* aiml_http_last_error(void) {
    return aiml_str_dup(g_http_client_error.c_str());
}

// resp.url — the URL that gave this answer (after redirects); "" server-side.
extern "C" char* aiml_http_resp_url(void* resp) {
    char* u = resp ? static_cast<AimlHttpResponse*>(resp)->final_url : nullptr;
    return aiml_str_dup(u ? u : "");
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
    free(r->raw_body);
    free(r->final_url);
    free(r);
}

// resp.body_len — exact byte length of a received body (binary-safe).
extern "C" int64_t aiml_http_resp_body_len(void* resp) {
    return resp ? static_cast<AimlHttpResponse*>(resp)->raw_len : 0;
}

// resp.save_to(path) — writes the received body byte-exact to `path`
// (staged as <path>.part, then renamed).  0 on success, -1 on error.
// An empty body creates an empty file.
extern "C" int32_t aiml_http_resp_save_to(void* resp, const char* path) {
    if (!resp || !path || !*path) return -1;
    AimlHttpResponse* r = static_cast<AimlHttpResponse*>(resp);
    std::string stage = std::string(path) + ".part";
    FILE* fp = std::fopen(stage.c_str(), "wb");
    if (!fp) return -1;
    bool ok = true;
    if (r->raw_body && r->raw_len > 0)
        ok = std::fwrite(r->raw_body, 1, static_cast<size_t>(r->raw_len), fp) == static_cast<size_t>(r->raw_len);
    if (std::fclose(fp) != 0) ok = false;
    if (!ok) { std::remove(stage.c_str()); return -1; }
    if (std::rename(stage.c_str(), path) != 0) { std::remove(stage.c_str()); return -1; }
    return 0;
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
    // Body spooling (srv.set_spool): bodies larger than spool_threshold — and
    // chunked bodies once they grow past it — are written to a temp file in
    // spool_dir instead of memory; spool_max caps them (0 = max_body_bytes).
    std::string  spool_dir;
    long long    spool_threshold   = 0;
    long long    spool_max         = 0;

    // ── Abuse resistance (spec/32_2 §3.12) ────────────────────────────────
    int              max_conns = 0;          // 0 = unlimited concurrent connections
    std::atomic<int> active_conns{0};        // live handler threads
    int              rate_max  = 0;          // 0 = per-IP rate limiting off
    int              rate_window_secs = 60;  // fixed-window length
    std::unordered_map<std::string, std::pair<time_t,int>> rate_map; // ip → (window_start, count)
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
// ── Request body sink: memory, spilling to a temp file (srv.set_spool) ─────
struct BodySink {
    std::string mem;
    FILE*       file = nullptr;
    std::string path;
    long long   total = 0;
    const char* spool_dir = nullptr;
    long long   threshold = 0;     // 0 = never spool
    bool        failed = false;

    bool spill() {
        if (file || !spool_dir || !*spool_dir) return file != nullptr;
        std::string tmpl = std::string(spool_dir) + "/aiml-body-XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end()); buf.push_back('\0');
        int tfd = mkstemp(buf.data());
        if (tfd < 0) { failed = true; return false; }
        file = fdopen(tfd, "wb");
        if (!file) { ::close(tfd); unlink(buf.data()); failed = true; return false; }
        path = buf.data();
        if (!mem.empty() && fwrite(mem.data(), 1, mem.size(), file) != mem.size()) { failed = true; return false; }
        mem.clear(); mem.shrink_to_fit();
        return true;
    }
    bool add(const char* data, size_t n) {
        if (n == 0) return true;
        if (!file && threshold > 0 && total + (long long)n > threshold && !spill()) return false;
        if (file) { if (fwrite(data, 1, n, file) != n) { failed = true; return false; } }
        else mem.append(data, n);
        total += (long long)n;
        return true;
    }
    void discard() {
        if (file) { fclose(file); file = nullptr; }
        if (!path.empty()) { unlink(path.c_str()); path.clear(); }
    }
};

// Read exactly `n` bytes into the sink in bounded pieces (n may exceed INT_MAX).
static bool recv_into_sink(int fd, void* ssl, BodySink& sink, long long n, time_t deadline) {
    char buf[64 * 1024];
    while (n > 0) {
        if (time(nullptr) > deadline) return false;
        int want = (int)(n < (long long)sizeof(buf) ? n : (long long)sizeof(buf));
        int got = recv_n(fd, ssl, buf, want);
        if (got <= 0) return false;
        if (!sink.add(buf, (size_t)got)) return false;
        n -= got;
        if (got < want) return false;
    }
    return true;
}

static AimlHttpRequest* parse_http_request(int fd, void* ssl, int timeout_secs, long max_body_bytes,
                                          const char* spool_dir = nullptr, long long spool_threshold = 0,
                                          long long spool_max = 0) {
    AimlHttpRequest* req = static_cast<AimlHttpRequest*>(calloc(1, sizeof(AimlHttpRequest)));
    req->params      = aiml_http_map_new();
    req->req_headers = aiml_http_map_new();
    req->cookies     = aiml_http_map_new();
    // Total wall-clock deadline for reading the whole request (slowloris/drip-feed
    // defence — complements the per-recv SO_RCVTIMEO set on the socket).
    const time_t deadline = time(nullptr) + (timeout_secs > 0 ? timeout_secs : 15);

    char line[8192];
    // A TLS ClientHello (record type 0x16) on a plain-HTTP listener: close at
    // once instead of waiting for a request line that never comes.  Clients
    // that probe https first (Go: docker, skopeo, podman) then fall back to
    // http immediately instead of after their 10 s TLS handshake timeout.
    if (!ssl) {
        unsigned char first = 0;
        if (::recv(fd, &first, 1, MSG_PEEK) == 1 && first == 0x16) {
            req->method = xstrdup("GET"); req->path = xstrdup("/");
            req->reject_status = -1;                   // close without a response
            return req;
        }
    }
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
    // With spooling the cap is spool_max (bodies leave memory), else the memory cap.
    const bool       spooling   = spool_dir && *spool_dir && spool_threshold > 0;
    const long long  BODY_CAP   = (spooling && spool_max > 0) ? spool_max : (long long)MAX_BODY_BYTES;
    bool             chunked_te = false;
    bool             have_cl    = false;

    // Headers
    long long content_length = 0;
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
                long long cl = strtoll(v, &endp, 10);
                while (endp && (*endp == ' ' || *endp == '\t')) ++endp;
                if (errno != 0 || !endp || *endp != '\0' || cl < 0 || have_cl) {
                    req->reject_status = 400;          // Bad Request (malformed / repeated length)
                    return req;
                }
                if (cl > BODY_CAP) {
                    req->reject_status = 413;          // Payload Too Large
                    return req;
                }
                content_length = (long)cl;
                have_cl = true;
            }
            if (lk == "transfer-encoding") {
                // Only "chunked" (case-insensitive) is understood; anything else
                // (gzip, chained codings) is refused rather than misread.
                std::string te = str_tolower(v);
                while (!te.empty() && (te.back() == ' ' || te.back() == '\t')) te.pop_back();
                if (te != "chunked") { req->reject_status = 501; return req; }
                chunked_te = true;
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

    // Content-Length together with Transfer-Encoding is the classic request-
    // smuggling ambiguity (RFC 9112 §6.1): refuse instead of picking one.
    if (chunked_te && have_cl) { req->reject_status = 400; return req; }

    // Body.  Stored binary-safe: in memory `body` keeps a trailing NUL for
    // string access and `body_len` holds the exact byte count (req.body_bytes);
    // spooled bodies (srv.set_spool) live in a temp file (`body_path`).
    BodySink sink;
    sink.spool_dir = spooling ? spool_dir : nullptr;
    sink.threshold = spooling ? spool_threshold : 0;
    if (chunked_te) {
        // chunk = size [;ext] CRLF data CRLF ; last-chunk = 0 CRLF ; trailers ; CRLF
        while (true) {
            if (time(nullptr) > deadline) { sink.discard(); req->reject_status = 408; return req; }
            n = recv_line(fd, ssl, line, sizeof(line));
            if (n <= 0) { sink.discard(); req->reject_status = 400; return req; }
            while (n > 0 && (line[n-1] == '\r' || line[n-1] == '\n')) line[--n] = '\0';
            char* semi = strchr(line, ';');
            if (semi) *semi = '\0';
            if (!line[0]) { sink.discard(); req->reject_status = 400; return req; }
            long long size = 0;
            for (const char* q = line; *q; ++q) {
                int d = (*q >= '0' && *q <= '9') ? *q - '0'
                      : (*q >= 'a' && *q <= 'f') ? *q - 'a' + 10
                      : (*q >= 'A' && *q <= 'F') ? *q - 'A' + 10 : -1;
                if (d < 0 || size > (BODY_CAP >> 4)) { sink.discard(); req->reject_status = d < 0 ? 400 : 413; return req; }
                size = size * 16 + d;
            }
            if (size == 0) break;
            if (sink.total + size > BODY_CAP) { sink.discard(); req->reject_status = 413; return req; }
            if (!recv_into_sink(fd, ssl, sink, size, deadline)) {
                sink.discard(); req->reject_status = sink.failed ? 507 : 400; return req;
            }
            char crlf[2];
            if (recv_n(fd, ssl, crlf, 2) != 2 || crlf[0] != '\r' || crlf[1] != '\n') {
                sink.discard(); req->reject_status = 400; return req;
            }
        }
        // Trailer section: header lines up to the terminating blank line (ignored).
        for (int t = 0; ; ++t) {
            n = recv_line(fd, ssl, line, sizeof(line));
            if (n <= 0 || t > MAX_HEADER_COUNT) { sink.discard(); req->reject_status = 400; return req; }
            while (n > 0 && (line[n-1] == '\r' || line[n-1] == '\n')) line[--n] = '\0';
            if (n == 0) break;
        }
    } else if (content_length > 0) {
        if (spooling && content_length > spool_threshold && !sink.spill()) {
            sink.discard(); req->reject_status = 507; return req;
        }
        if (!recv_into_sink(fd, ssl, sink, content_length, deadline)) {
            sink.discard(); req->reject_status = sink.failed ? 507 : 400; return req;
        }
    }

    if (sink.file) {
        if (fclose(sink.file) != 0) { sink.file = nullptr; sink.discard(); req->reject_status = 507; return req; }
        sink.file = nullptr;
        req->body_path = xstrdup(sink.path.c_str());
        req->body_len  = sink.total;
    } else if (sink.total > 0) {
        if (sink.total > (long long)MAX_BODY_BYTES) { req->reject_status = 413; return req; }
        char* raw = static_cast<char*>(malloc(static_cast<size_t>(sink.total) + 1));
        if (!raw) { req->reject_status = 413; return req; }
        memcpy(raw, sink.mem.data(), static_cast<size_t>(sink.total));
        raw[sink.total] = '\0';
        req->body     = raw;
        req->body_len = sink.total;
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
    // A spooled body the handler did not move away is removed with the request.
    if (req->body_path) { unlink(req->body_path); free(req->body_path); }
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

// Standard reason phrase for the status line (RFC 9110 §15); "" for unknown codes.
static const char* http_reason(int status) {
    switch (status) {
        case 200: return "OK";             case 201: return "Created";
        case 202: return "Accepted";       case 204: return "No Content";
        case 206: return "Partial Content";
        case 301: return "Moved Permanently"; case 302: return "Found";
        case 303: return "See Other";      case 304: return "Not Modified";
        case 307: return "Temporary Redirect"; case 308: return "Permanent Redirect";
        case 400: return "Bad Request";    case 401: return "Unauthorized";
        case 403: return "Forbidden";      case 404: return "Not Found";
        case 405: return "Method Not Allowed"; case 408: return "Request Timeout";
        case 409: return "Conflict";       case 411: return "Length Required";
        case 413: return "Content Too Large"; case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type"; case 416: return "Range Not Satisfiable";
        case 422: return "Unprocessable Content"; case 429: return "Too Many Requests";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error"; case 501: return "Not Implemented";
        case 502: return "Bad Gateway";    case 503: return "Service Unavailable";
        case 507: return "Insufficient Storage";
        default:  return "";
    }
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
    out += " ";
    out += http_reason(resp->status);
    out += "\r\n";

    // Default headers
    if (!aiml_http_map_get(resp->headers, "content-type"))
        aiml_http_map_put(resp->headers, "content-type", "text/plain");

    // body_file: read and serve
    std::string file_body;
    if (resp->body_file && !resp->body && !resp->head_only) {
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
    // The server closes every connection after one response (no keep-alive);
    // say so, or HTTP/1.1 clients (Go's net/http) send the next request on
    // this connection and read EOF instead of a response.
    aiml_http_map_put(resp->headers, "connection", "close");

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
    // HEAD (RFC 9110 §9.3.2): same headers, including Content-Length, no body.
    if (body_ptr && body_size > 0 && !resp->head_only)
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
    const char* reason = http_reason(resp->status);
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

    if (unsatisfiable || resp->head_only) { ::close(ffd); return; }

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
    std::string     peer_ip;   // dotted/colon form of the connecting socket (v4 or v6)
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
    std::string peer_ip = ctx->peer_ip;
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

    AimlHttpRequest* req = parse_http_request(fd, ssl, to_secs, srv->max_body_bytes,
                                              srv->spool_dir.c_str(), srv->spool_threshold, srv->spool_max);

    // Record the trusted socket peer IP (dotted) so req.client_ip() never has to
    // trust a spoofable X-Forwarded-For header. struct in_addr wants network order.
    req->peer_ip = xstrdup(peer_ip.c_str());

    // TLS on the plain listener (see parse_http_request): just close.
    if (req->reject_status == -1) {
        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
        ::close(fd);
        free_request(req);
        return nullptr;
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
                        : req->reject_status == 501 ? "Not Implemented"
                        : req->reject_status == 507 ? "Insufficient Storage"
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
    resp->head_only = (req->method && strcmp(req->method, "HEAD") == 0) ? 1 : 0;

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

/* Peer address → string, any family.  A dual-stack listener (spec/32_4 §1.1)
 * delivers IPv4 peers V4-MAPPED (::ffff:a.b.c.d); unwrap those so a rate-limit
 * key or client_ip() reads the same whichever stack the client arrived on. */
static std::string http_render_peer_ip(const struct sockaddr* sa) {
    char buf[INET6_ADDRSTRLEN] = {0};
    if (!sa) return std::string();
    if (sa->sa_family == AF_INET) {
        const auto* a4 = reinterpret_cast<const struct sockaddr_in*>(sa);
        inet_ntop(AF_INET, &a4->sin_addr, buf, sizeof(buf));
    } else if (sa->sa_family == AF_INET6) {
        const auto* a6 = reinterpret_cast<const struct sockaddr_in6*>(sa);
        if (IN6_IS_ADDR_V4MAPPED(&a6->sin6_addr)) {
            struct in_addr v4{};
            memcpy(&v4, reinterpret_cast<const uint8_t*>(&a6->sin6_addr) + 12, 4);
            inet_ntop(AF_INET, &v4, buf, sizeof(buf));
        } else {
            inet_ntop(AF_INET6, &a6->sin6_addr, buf, sizeof(buf));
        }
    }
    return std::string(buf);
}

// Per-IP fixed-window rate limit. Returns true if the connection is allowed.
// Prunes stale windows when the table grows, so unique-IP floods stay bounded.
static bool rate_allow(AimlHttpServer* srv, const std::string& ip) {
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
        struct sockaddr_storage addr{};
        socklen_t len = sizeof(addr);
        int cfd = ::accept4(srv->listen_fd, reinterpret_cast<sockaddr*>(&addr), &len, SOCK_CLOEXEC);
        if (cfd < 0) {
            if (srv->running) continue;
            break;
        }
        std::string peer_ip = http_render_peer_ip(reinterpret_cast<const sockaddr*>(&addr));

        // Per-IP rate limit — reject before spending a thread or TLS handshake.
        if (!rate_allow(srv, peer_ip)) {
            if (!srv->ssl_ctx) send_raw_status(cfd, 429, "Too Many Requests");
            ::close(cfd);
            fprintf(stderr, "[http] rate-limit: rejected %s (>%d/%ds)\n",
                    peer_ip.c_str(), srv->rate_max, srv->rate_window_secs);
            continue;
        }
        // Concurrent-connection cap — bound thread/resource growth under load.
        if (srv->max_conns > 0 &&
            srv->active_conns.load(std::memory_order_relaxed) >= srv->max_conns) {
            if (!srv->ssl_ctx) send_raw_status(cfd, 503, "Service Unavailable");
            ::close(cfd);
            fprintf(stderr, "[http] conn-cap: rejected %s (active=%d max=%d)\n",
                    peer_ip.c_str(),
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
    /* spec/32_4 §1.1: a wildcard bind opens ONE dual-stack AF_INET6 socket so the
     * host answers on both stacks.  AF_INET-only meant a page served under a name
     * with an AAAA record was refused over IPv6 — silently, because the connection
     * never reached this server. */
    const bool wildcard = !host || !*host || strcmp(host, "0.0.0.0") == 0
                       || strcmp(host, "::") == 0 || strcmp(host, "*") == 0;
    int fd = -1;
    if (wildcard) {
        fd = ::socket(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd >= 0) {
            int yes = 1, no = 0;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
            setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no));
            struct sockaddr_in6 a6{};
            a6.sin6_family = AF_INET6;
            a6.sin6_addr   = in6addr_any;
            a6.sin6_port   = htons(static_cast<uint16_t>(port));
            if (::bind(fd, reinterpret_cast<sockaddr*>(&a6), sizeof(a6)) < 0 ||
                ::listen(fd, 128) < 0) { ::close(fd); fd = -1; }
        }
    }
    if (fd < 0) {
        char ps[16]; snprintf(ps, sizeof(ps), "%d", port);
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags    = AI_PASSIVE;
        if (getaddrinfo(wildcard ? nullptr : host, ps, &hints, &res) != 0 || !res) return nullptr;
        for (struct addrinfo* ai = res; ai && fd < 0; ai = ai->ai_next) {
            int cand = ::socket(ai->ai_family, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (cand < 0) continue;
            int yes = 1;
            setsockopt(cand, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
            if (::bind(cand, ai->ai_addr, ai->ai_addrlen) == 0 && ::listen(cand, 128) == 0) fd = cand;
            else ::close(cand);
        }
        freeaddrinfo(res);
    }
    if (fd < 0) return nullptr;
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

// Spool request bodies above `threshold` bytes (and chunked bodies once they
// grow past it) to temp files in `dir`; `max_bytes` caps spooled bodies
// (<= 0 keeps the memory cap from set_max_body).  threshold <= 0 or an empty
// dir turns spooling off.
extern "C" void aiml_http_server_set_spool(void* srv_ptr, const char* dir,
                                           int64_t threshold, int64_t max_bytes) {
    AimlHttpServer* s = static_cast<AimlHttpServer*>(srv_ptr);
    if (!s) return;
    s->spool_dir       = dir ? dir : "";
    s->spool_threshold = threshold > 0 ? threshold : 0;
    s->spool_max       = max_bytes > 0 ? max_bytes : 0;
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

// req.body_path — temp file of a spooled body (srv.set_spool), or NULL when the
// body is in memory.  The file is removed after the handler returns unless the
// handler moved it (io.rename) — that is how a large upload is kept.
extern "C" char* aiml_http_req_body_path(void* req) {
    AimlHttpRequest* r = static_cast<AimlHttpRequest*>(req);
    return (r && r->body_path) ? xstrdup(r->body_path) : nullptr;
}

// req.body_len — exact body length in bytes (in memory or spooled).
extern "C" int64_t aiml_http_req_body_len(void* req) {
    AimlHttpRequest* r = static_cast<AimlHttpRequest*>(req);
    return r ? r->body_len : 0;
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
    // Spooled body: load the file (the caller asked for bytes in memory).
    std::string spooled;
    if (r && !r->body && r->body_path && r->body_len > 0 && r->body_len < INT32_MAX - 1) {
        FILE* f = fopen(r->body_path, "rb");
        if (f) {
            spooled.resize(static_cast<size_t>(r->body_len));
            size_t got = fread(&spooled[0], 1, spooled.size(), f);
            fclose(f);
            spooled.resize(got);
            len = static_cast<int64_t>(got);
        }
    }
    if (len < 0 || len > INT32_MAX - 1) len = 0;
    AimlBuf* b = static_cast<AimlBuf*>(calloc(1, sizeof(AimlBuf)));
    if (!b) return nullptr;
    b->data = static_cast<char*>(malloc(static_cast<size_t>(len) + 1));
    if (!b->data) { free(b); return nullptr; }
    if (len > 0) memcpy(b->data, spooled.empty() ? r->body : spooled.data(), static_cast<size_t>(len));
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

// client.free() — releases a request handle (headers, body, method, body_file).
// The url belongs to the caller's `url` value and is not touched.  Null-safe.
extern "C" void aiml_http_client_free(void* client) {
    if (!client) return;
    AimlHttpClient* c = static_cast<AimlHttpClient*>(client);
    aiml_http_map_free(c->headers);
    free(c->body);
    free(c->method);
    free(c->body_file);
    free(c->download_to);
    free(c->proxy);
    free(c);
}

static void replace_text(char*& slot, const char* text) {
    free(slot);
    slot = (text && *text) ? xstrdup(text) : nullptr;
}

// client.download_to = path — a 2xx body is streamed into `path` (staged as
// <path>.part, renamed when complete); other answers stay in memory.
extern "C" void aiml_http_client_set_download_to(void* client, const char* path) {
    if (client) replace_text(static_cast<AimlHttpClient*>(client)->download_to, path);
}

// client.proxy = "" (environment) | "direct" | "http://[user:pass@]host:port"
extern "C" void aiml_http_client_set_proxy(void* client, const char* proxy) {
    if (client) replace_text(static_cast<AimlHttpClient*>(client)->proxy, proxy);
}

// client.max_redirects = n — redirects followed (0 = the 3xx is the answer).
extern "C" void aiml_http_client_set_max_redirects(void* client, int32_t count) {
    if (client) static_cast<AimlHttpClient*>(client)->max_redirects = count < 0 ? 0 : count;
}

// client.body_file = path — stream the request body from a file (binary-safe).
extern "C" void aiml_http_client_set_body_file(void* client, const char* path) {
    if (client) replace_text(static_cast<AimlHttpClient*>(client)->body_file, path);
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
