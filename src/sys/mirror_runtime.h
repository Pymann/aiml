#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * mirror_runtime.h — AIML mirror namespace (44.5)
 *
 * Synchronizes AIML model instances across the network.
 * Transport: tcp://host:port (raw TCP) | ws://host:port | wss://host:port
 *
 * mirror_session = AimlMirrorSession*  (opaque i8*)
 * mirror_peer    = AimlMirrorPeer*     (opaque i8*)
 * mirror_event   = AimlMirrorEvent*    (opaque i8*; also returned as LLVM struct)
 */

/* ── Session creation ─────────────────────────────────────────────────────── */

/* Host: listen for incoming connections (non-blocking accept loop in update) */
void* aiml_mirror_listen         (const char* url);
void* aiml_mirror_listen_max     (const char* url, int32_t max_peers);

/* Client: connect to host */
void* aiml_mirror_connect        (const char* url);
void* aiml_mirror_connect_timeout(const char* url, int32_t ms);

/* ── Session lifecycle ────────────────────────────────────────────────────── */

void    aiml_mirror_session_close       (void* session);
void    aiml_mirror_session_update      (void* session);
int32_t aiml_mirror_session_is_connected(void* session);
int32_t aiml_mirror_session_peer_count  (void* session);

/* ── Object registration ──────────────────────────────────────────────────── */

/* Register an object for sync; returns network-wide net_id */
int32_t aiml_mirror_session_register    (void* session, void* obj, int64_t obj_size);
/* Register with fixed id (for predefined world entities) */
void    aiml_mirror_session_register_id (void* session, void* obj, int64_t obj_size, int32_t id);
/* Remove from sync */
void    aiml_mirror_session_unregister  (void* session, void* obj);

/* ── Event queue ──────────────────────────────────────────────────────────── */

int32_t aiml_mirror_session_has_event  (void* session);
/* Returns AimlMirrorEvent* — valid until next call to next_event */
void*   aiml_mirror_session_next_event (void* session);

/* ── Sync control ─────────────────────────────────────────────────────────── */

void aiml_mirror_session_set_sync_rate  (void* session, void* obj, float hz);
void aiml_mirror_session_sync_now       (void* session, void* obj);
void aiml_mirror_session_set_sync_mode  (void* session, void* obj, const char* mode);
void aiml_mirror_session_set_rpc_target (void* session, void* obj, const char* method, const char* target);

/* ── RPC ──────────────────────────────────────────────────────────────────── */

/* Client: send an RPC call to the owner of net_id.
 * arg is a single i32 (v1 limitation). */
void    aiml_mirror_session_send_rpc(void* session, int32_t net_id, const char* method, int32_t arg);

/* Host: poll pending RPC calls (PKT_RPC received from peers) */
int32_t aiml_mirror_session_has_rpc (void* session);
/* Returns AimlRpcEvent* — caller owns the pointer, must not be freed (freed on session close) */
void*   aiml_mirror_session_next_rpc(void* session);

/* ── mirror_event field accessors ─────────────────────────────────────────── */

const char* aiml_mirror_event_type  (void* ev);  /* "peer_connected" | "peer_disconnected" |
                                                     "object_registered" | "object_unregistered" */
void*       aiml_mirror_event_peer  (void* ev);  /* AimlMirrorPeer* (null for object_* events) */
int32_t     aiml_mirror_event_net_id(void* ev);  /* net_id (0 for peer_* events) */

#ifdef __cplusplus
}
#endif
