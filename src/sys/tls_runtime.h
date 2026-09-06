#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// ── Lifecycle ─────────────────────────────────────────────────────────────────
// Returns heap-allocated AimlTlsSocket* cast to void*, or nullptr on failure.
void*   aiml_tls_socket(void);

// ── Shared server-side TLS policy (SSL_CTX* passed as void*) ──────────────────
// Apply hardened server defaults to an SSL_CTX: min TLS 1.2, no SSLv2/3/TLS1.0/1.1,
// no TLS compression, server cipher preference. Used by both the HTTPS and WSS servers.
void    aiml_tls_harden_server_ctx(void* ssl_ctx);
// Enable mutual-TLS: require + verify a client cert against the CA bundle PEM at
// ca_path. Returns 0 on success, -1 if the CA bundle cannot be loaded.
int     aiml_tls_require_client_cert(void* ssl_ctx, const char* ca_path);

// ── Config (call before connect/bind) ────────────────────────────────────────
// cert_path + key_path: PEM files (server or client cert)
int     aiml_tls_set_cert(void* sock, const char* cert_path, const char* key_path);
// ca_path: CA bundle PEM (replaces system store)
int     aiml_tls_set_ca(void* sock, const char* ca_path);
// mode: "peer" (default, verify server), "require" (require+verify client), "none" (skip)
int     aiml_tls_set_verify(void* sock, const char* mode);
// timeout_ms: 0 = blocking; >0 = connect/recv/send timeout
int     aiml_tls_set_timeout(void* sock, int32_t timeout_ms);

// ── Client ────────────────────────────────────────────────────────────────────
// TCP connect + TLS handshake + SNI; returns 0 on success, -1 on error.
int     aiml_tls_connect(void* sock, const char* host, int32_t port);

// ── Server ────────────────────────────────────────────────────────────────────
int     aiml_tls_bind(void* sock, const char* host, int32_t port);
int     aiml_tls_listen(void* sock, int32_t backlog);
// Blocks until client arrives, performs TLS handshake; returns new AimlTlsSocket*.
void*   aiml_tls_accept(void* sock);

// ── I/O (buffer-based) ────────────────────────────────────────────────────────
// buf_ptr: { i32 size, i32 cap, i32 rIdx, i32 wIdx, char* data }
int     aiml_tls_send(void* sock, void* buf_ptr);
int     aiml_tls_recv(void* sock, void* buf_ptr);

// ── I/O (string-based) ────────────────────────────────────────────────────────
int     aiml_tls_send_string(void* sock, const char* str);
char*   aiml_tls_recv_string(void* sock, int32_t max_bytes); // malloc'd, caller frees

// ── Common ────────────────────────────────────────────────────────────────────
int     aiml_tls_close(void* sock);
int32_t aiml_tls_fd(void* sock);

// ── Peer certificate (after successful connect/accept) ────────────────────────
char*   aiml_tls_peer_cert_cn(void* sock);    // malloc'd Common Name, caller frees
int32_t aiml_tls_peer_cert_valid(void* sock); // 1 = chain+expiry OK, 0 = not valid

#ifdef __cplusplus
}
#endif
