#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ────────────────────────────────────────────────────────────────
 * AimlMap shim — matches the LLVM struct layout used by map<K:V>:
 *   { i32 size, i32 capacity, i8* occupied, i8* keys, i8* values }
 * The HTTP runtime builds these for headers / params / cookies so
 * AIML map methods (get, put, …) work on them transparently.
 * ──────────────────────────────────────────────────────────────── */
typedef struct AimlHttpMap {
    int32_t  size;
    int32_t  capacity;
    int8_t*  occupied;   /* bool[] */
    char**   keys;
    char**   values;
} AimlHttpMap;

/* ─── url ─────────────────────────────────────────────────────── */
typedef struct AimlUrl {
    char*        protocol;    /* "http", "https", … */
    char*        host;        /* "example.com" */
    char*        path;        /* "/api/users" */
    char*        fragment;    /* "section1" or NULL */
    int32_t      port;        /* 0 = use default */
    AimlHttpMap* get_param;   /* query params map, never NULL */
    void*        credential;  /* AimlUrlCredential* or NULL */
} AimlUrl;

typedef struct AimlUrlCredential {
    char* user;
    char* pass;
} AimlUrlCredential;

/* ─── http_response_handle ────────────────────────────────────── */
typedef struct AimlHttpResponse {
    int32_t      status;       /* 200, 404, … (default 200) */
    int8_t       ok;           /* status >= 200 && < 300 */
    AimlHttpMap* headers;      /* always initialised, never NULL */
    char*        body;         /* text body or NULL */
    void*        body_bytes;   /* AimlBuffer* or NULL */
    char*        body_file;    /* path or NULL */
    /* --- internal (server-side) --- */
    int          conn_fd;      /* -1 for client-side responses */
    void*        ssl;          /* SSL* or NULL */
    int8_t       resp_sent;    /* 1 after send/write/close */
    int8_t       chunked;      /* 1 while chunked transfer active */
    char**       set_cookies;  /* NULL or malloc'd array of full Set-Cookie header values */
    int          n_set_cookies;/* count in set_cookies (0 → none). calloc-zeroed. */
    char*        req_range;    /* server-side: the request's Range header (or NULL), for body_file */
} AimlHttpResponse;

/* ─── http_request_handle ─────────────────────────────────────── */
typedef struct AimlHttpRequest {
    char*        method;       /* "GET", "POST", … */
    char*        path;         /* "/api/users/42" */
    AimlUrl*     url;          /* fully parsed URL */
    AimlHttpMap* params;       /* named path segments, never NULL */
    char*        body;         /* request body string or NULL */
    void*        body_bytes;   /* AimlBuffer* or NULL */
    AimlHttpMap* req_headers;  /* all request headers, never NULL */
    AimlHttpMap* cookies;      /* parsed Cookie: header, never NULL */
    int          reject_status; /* 0 = ok; else an HTTP error to send instead of
                                 * dispatching (413/431/414/400) — parser limit hit */
    char*        peer_ip;      /* trusted socket peer IP (dotted), set server-side; NULL client-side */
    int64_t      body_len;     /* exact byte length of body (binary-safe; body may contain NULs) */
} AimlHttpRequest;

/* ─── http_client_handle ──────────────────────────────────────── */
typedef struct AimlHttpClient {
    AimlHttpMap* headers;      /* request headers, never NULL */
    char*        body;         /* request body or NULL */
    void*        body_bytes;   /* AimlBuffer* or NULL */
    int32_t      timeout;      /* ms, 0 = no timeout */
    int8_t       stream;       /* streaming mode */
    char*        method;       /* set internally */
    AimlUrl*     url;          /* set internally */
} AimlHttpClient;

/* ─── Route handler function pointer ─────────────────────────── */
typedef void (*AimlHandlerFn)(void* req, void* resp);

/* ─── http_server_handle (opaque) ─────────────────────────────── */
typedef struct AimlHttpServer AimlHttpServer;

/* ─── http_route_handle (opaque) ──────────────────────────────── */
typedef struct AimlHttpRoute AimlHttpRoute;

/* ─── fs_options ──────────────────────────────────────────────── */
typedef struct AimlFsOptions {
    char*   index;         /* default: "index.html" */
    int8_t  spa;           /* SPA fallback */
    int32_t cache_max_age; /* Cache-Control max-age, 0 = off */
} AimlFsOptions;

/* ══════════════════════════════════════════════════════════════
 * url API
 * ════════════════════════════════════════════════════════════ */
AimlUrl*  aiml_url_parse(const char* raw);
char*     aiml_url_to_string(AimlUrl* u);
int8_t    aiml_url_is_https(AimlUrl* u);
/* member reads — return pointers directly (no copy) */
char*     aiml_url_protocol(AimlUrl* u);
char*     aiml_url_host(AimlUrl* u);
char*     aiml_url_path(AimlUrl* u);
char*     aiml_url_fragment(AimlUrl* u);
int32_t   aiml_url_port(AimlUrl* u);
void*     aiml_url_get_param(AimlUrl* u);   /* AimlHttpMap* */
void*     aiml_url_credential(AimlUrl* u);  /* AimlUrlCredential* or NULL */

/* ══════════════════════════════════════════════════════════════
 * url_credential API
 * ════════════════════════════════════════════════════════════ */
char*     aiml_url_cred_user(AimlUrlCredential* c);
char*     aiml_url_cred_pass(AimlUrlCredential* c);

/* ══════════════════════════════════════════════════════════════
 * AimlHttpMap API (used internally + exposed for header/param access)
 * ════════════════════════════════════════════════════════════ */
AimlHttpMap* aiml_http_map_new(void);
void         aiml_http_map_put(AimlHttpMap* m, const char* key, const char* val);
char*        aiml_http_map_get(AimlHttpMap* m, const char* key);
int32_t      aiml_http_map_len(AimlHttpMap* m);
void         aiml_http_map_free(AimlHttpMap* m);

/* ══════════════════════════════════════════════════════════════
 * http namespace — convenience client functions
 * ════════════════════════════════════════════════════════════ */
void* aiml_http_get(void* url);      /* AimlUrl* → AimlHttpResponse* */
void* aiml_http_post(void* url, const char* body);
void* aiml_http_put(void* url, const char* body);
void* aiml_http_delete(void* url);
void* aiml_http_head(void* url);
void* aiml_http_new_request(const char* method, void* url); /* → AimlHttpClient* */

/* ══════════════════════════════════════════════════════════════
 * http_client_handle API
 * ════════════════════════════════════════════════════════════ */
void* aiml_http_client_new(const char* method, void* url); /* → AimlHttpClient* */
void* aiml_http_client_send(void* client);                 /* → AimlHttpResponse* */

/* ══════════════════════════════════════════════════════════════
 * http_response_handle API (client-side reads)
 * ════════════════════════════════════════════════════════════ */
int32_t aiml_http_resp_status(void* resp);
int8_t  aiml_http_resp_ok(void* resp);
void*   aiml_http_resp_headers(void* resp);   /* AimlHttpMap* */
char*   aiml_http_resp_body(void* resp);
void    aiml_http_resp_free(void* resp);

/* ══════════════════════════════════════════════════════════════
 * http.server / http.server_tls
 * ════════════════════════════════════════════════════════════ */
void* aiml_http_server(const char* host, int32_t port);
void* aiml_http_server_tls(const char* host, int32_t port,
                            const char* cert_path, const char* key_path);
/* mTLS: require a client cert signed by the CA bundle at ca_path (PEM).
 * Server must be TLS. Returns 0 on success, -1 on error. */
int   aiml_http_server_require_client_cert(void* srv, const char* ca_path);

/* ══════════════════════════════════════════════════════════════
 * http_server_handle API
 * ════════════════════════════════════════════════════════════ */
void  aiml_http_server_route(void* srv, const char* method,
                              const char* pattern, void* handler_fn);
void  aiml_http_server_use(void* srv, void* middleware_fn);
void* aiml_http_server_group(void* srv, const char* prefix,
                              void* middleware_fn);   /* → AimlHttpRoute* */
void  aiml_http_server_set_header(void* srv, const char* name, const char* val);
void  aiml_http_server_start(void* srv);              /* blocking */
void* aiml_http_server_start_async(void* srv);        /* → thread_handle* */
void  aiml_http_server_stop(void* srv);
void  aiml_http_server_filesystem(void* srv, const char* prefix,
                                   void* dirs_list, void* opts);

/* ══════════════════════════════════════════════════════════════
 * http_route_handle API
 * ════════════════════════════════════════════════════════════ */
void  aiml_http_route_route(void* route, const char* method,
                             const char* pattern, void* handler_fn);
void  aiml_http_route_use(void* route, void* middleware_fn);
void* aiml_http_route_group(void* route, const char* prefix,
                             void* middleware_fn);    /* → AimlHttpRoute* */
void  aiml_http_route_filesystem(void* route, const char* prefix,
                                  void* dirs_list, void* opts);

/* ══════════════════════════════════════════════════════════════
 * http_request_handle API (server-side reads)
 * ════════════════════════════════════════════════════════════ */
char*  aiml_http_req_header(void* req, const char* name);
char*  aiml_http_req_cookie(void* req, const char* name);

/* ══════════════════════════════════════════════════════════════
 * http_response_handle API (server-side writes)
 * ════════════════════════════════════════════════════════════ */
void  aiml_http_resp_send(void* resp);
void  aiml_http_resp_write(void* resp, const char* chunk);
void  aiml_http_resp_write_bytes(void* resp, void* buf);
void  aiml_http_resp_flush(void* resp);
void  aiml_http_resp_close(void* resp);

/* ══════════════════════════════════════════════════════════════
 * http_response_handle setters (server-side member writes)
 * ════════════════════════════════════════════════════════════ */
void aiml_http_resp_set_status(void* resp, int32_t status);
void aiml_http_resp_set_body(void* resp, const char* body);
void aiml_http_resp_set_body_bytes(void* resp, void* buf);

/* ══════════════════════════════════════════════════════════════
 * http_request_handle getters (member reads)
 * ════════════════════════════════════════════════════════════ */
char*  aiml_http_req_method(void* req);
char*  aiml_http_req_path(void* req);
char*  aiml_http_req_body(void* req);
void*  aiml_http_req_body_bytes(void* req); /* fresh AimlBuffer* copy of the raw body */
void   aiml_http_server_set_max_body(void* srv, int64_t bytes);
void   aiml_http_resp_set_body_file(void* resp, const char* file_path);
void*  aiml_http_req_url(void* req);      /* AimlUrl* */
void*  aiml_http_req_params(void* req);   /* AimlHttpMap* */
void*  aiml_http_req_cookies(void* req);  /* AimlHttpMap* */

/* ══════════════════════════════════════════════════════════════
 * http_client_handle setters (member writes)
 * ════════════════════════════════════════════════════════════ */
void aiml_http_client_set_body(void* client, const char* body);
void aiml_http_client_set_timeout(void* client, int32_t timeout_ms);
void aiml_http_client_set_stream(void* client, int8_t stream);
void* aiml_http_client_get_headers(void* client);   /* AimlHttpMap* */

/* ══════════════════════════════════════════════════════════════
 * http_response_handle getters (client-side)
 * ════════════════════════════════════════════════════════════ */
void* aiml_http_resp_get_headers(void* resp);  /* AimlHttpMap* */

/* ══════════════════════════════════════════════════════════════
 * fs_options
 * ════════════════════════════════════════════════════════════ */
void* aiml_fs_options_new(void);   /* → AimlFsOptions* (defaults) */
void  aiml_fs_options_set_index(void* opts, const char* index);
void  aiml_fs_options_set_spa(void* opts, int8_t spa);
void  aiml_fs_options_set_cache_max_age(void* opts, int32_t max_age);

#ifdef __cplusplus
}
#endif
