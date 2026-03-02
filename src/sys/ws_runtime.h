#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// ── ws_handle: opaque AimlWs* ─────────────────────────────────────────────────
// Used for both ws:// (plain TCP) and wss:// (TLS) WebSockets.
// Client frames are masked (RFC 6455 §5.3); server frames are unmasked.

// ── Client ────────────────────────────────────────────────────────────────────
void*   aiml_ws_connect(const char* host, int32_t port, const char* path);
void*   aiml_wss_connect(const char* host, int32_t port, const char* path);

// ── Server ────────────────────────────────────────────────────────────────────
void*   aiml_ws_server(const char* host, int32_t port);
void*   aiml_wss_server(const char* host, int32_t port, const char* cert, const char* key);
// Blocks until a client arrives; performs HTTP Upgrade handshake (+ TLS if server is wss).
void*   aiml_ws_accept(void* srv);

// ── I/O ───────────────────────────────────────────────────────────────────────
int     aiml_ws_send(void* ws, const char* text);        // UTF-8 text frame
int     aiml_ws_send_binary(void* ws, void* buf_ptr);    // binary frame from buffer
char*   aiml_ws_recv(void* ws);                          // recv next text/binary payload (malloc'd)
int     aiml_ws_ping(void* ws);                          // send ping frame

// ── Common ────────────────────────────────────────────────────────────────────
int     aiml_ws_close(void* ws);   // send close frame, shutdown, free
int32_t aiml_ws_fd(void* ws);      // raw TCP fd

#ifdef __cplusplus
}
#endif
