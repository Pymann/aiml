#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// ── Lifecycle ─────────────────────────────────────────────────────────────────
// proto: "tcp" (default) or "udp"
// Returns heap-allocated AimlSocket* cast to void*, or nullptr on failure.
void*   aiml_net_socket(const char* proto);

// ── Client ────────────────────────────────────────────────────────────────────
// Returns 0 on success, -1 on error.
int     aiml_net_connect(void* sock, const char* host, int32_t port);

// ── Server ────────────────────────────────────────────────────────────────────
int     aiml_net_bind(void* sock, const char* host, int32_t port);
int     aiml_net_listen(void* sock, int32_t backlog); // backlog=0 → 128
void*   aiml_net_accept(void* sock);                  // → new heap AimlSocket*

// ── I/O (buffer-based) ────────────────────────────────────────────────────────
// buf_ptr points to { i32 size, i32 cap, i32 rIdx, i32 wIdx, char* data }
// net.send: sends buf->writeIndex bytes from buf->data
// net.recv: receives up to buf->capacity bytes into buf->data, sets size+writeIndex
int     aiml_net_send(void* sock, void* buf_ptr);
int     aiml_net_recv(void* sock, void* buf_ptr);

// ── I/O (string-based, convenience) ──────────────────────────────────────────
int     aiml_net_send_string(void* sock, const char* str);
char*   aiml_net_recv_string(void* sock, int32_t max_bytes); // malloc'd, caller frees

// ── Common ────────────────────────────────────────────────────────────────────
int     aiml_net_close(void* sock);              // closes fd, frees AimlSocket
int32_t aiml_net_fd(void* sock);                 // raw fd (for debugging)
int     aiml_net_set_timeout(void* sock, int32_t timeout_ms); // 0=blocking; sets SO_RCVTIMEO/SNDTIMEO + connect timeout

#ifdef __cplusplus
}
#endif
