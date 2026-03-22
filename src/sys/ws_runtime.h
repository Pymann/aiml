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

// ── Raw binary I/O (used by mirror transport) ────────────────────────────────
int     aiml_ws_send_raw(void* ws, const void* data, int32_t len); // binary frame from raw ptr+len
void*   aiml_ws_recv_raw(void* ws, int32_t* out_len);              // recv next frame, sets length (caller frees)

// ── Common ────────────────────────────────────────────────────────────────────
int     aiml_ws_close(void* ws);   // send close frame, shutdown, free
int32_t aiml_ws_fd(void* ws);      // raw TCP fd
int     aiml_ws_poll(void* ws, int32_t timeout_ms);          // 1=data ready, 0=timeout, -1=error
int     aiml_ws_poll_listener(void* srv, int32_t timeout_ms); // 1=accept-ready, 0=timeout, -1=error

#ifdef __cplusplus
}
#endif
