#pragma once
#include <stdint.h>
#include "ser_runtime.h"

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
/* wss:// host with an explicit server cert (PEM cert + key paths); required for TLS. */
void* aiml_mirror_listen_tls     (const char* url, const char* cert, const char* key);
/* Dotted IP of a connected peer (by peer_id), captured at accept; refcounted, "" if unknown. */
char* aiml_mirror_peer_ip        (void* session, int32_t peer_id);

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

/* Typed variants: obj struct contains heap fields (strings, lists, …).
 * nodes/n_nodes: compiler-emitted @__aiml_sernodes_<Type> descriptor.
 * Delta packets use aiml_ser_encode/decode; WAL-replay is pointer-safe. */
int32_t aiml_mirror_session_register_typed   (void* session, void* obj, int64_t obj_size,
                                              const AimlTypeNode* nodes, int32_t n_nodes);
void    aiml_mirror_session_register_id_typed(void* session, void* obj, int64_t obj_size,
                                              int32_t id,
                                              const AimlTypeNode* nodes, int32_t n_nodes);
/* spec/44_5 §4 readiness: successful remote snapshots / last result (0, -1, -2; -3 unknown) */
uint64_t aiml_mirror_session_snapshot_revision(void* session, int32_t net_id);
int32_t  aiml_mirror_session_snapshot_error   (void* session, int32_t net_id);

/* ── Event queue ──────────────────────────────────────────────────────────── */

int32_t aiml_mirror_session_has_event  (void* session);
/* Returns AimlMirrorEvent* — valid until next call to next_event */
void*   aiml_mirror_session_next_event (void* session);
/* Combines has_event + next_event. Returns AimlMirrorEvent* with type=="" if queue empty. Never null. */
void*   aiml_mirror_session_poll_event (void* session);

/* ── Sync control ─────────────────────────────────────────────────────────── */

void aiml_mirror_session_set_sync_rate  (void* session, void* obj, float hz);
void aiml_mirror_session_sync_now       (void* session, void* obj);
void aiml_mirror_session_set_sync_mode  (void* session, void* obj, const char* mode);
void aiml_mirror_session_set_rpc_target (void* session, void* obj, const char* method, const char* target);
/* spec/44_5 §7.1 automatic object RPCs: compiler-emitted rpc table for a registered object
 * ({method, args_nodes, n_nodes, args_size, thunk} × count) and the wrapper's routing call
 * (0 = run locally, 1 = handled remotely only). */
void    aiml_mirror_session_set_rpc_table(void* session, void* obj, const void* table, int32_t count);
int32_t aiml_mirror_object_call(void* obj, const char* method, void* args);

/* ── RPC ──────────────────────────────────────────────────────────────────── */

/* Send a push RPC (spec/44_5 §7).  Client: always to the server.  Host: target > 0 = exactly that
 * peer (unknown → nothing sent), 0 = broadcast to all peers, < 0 = rejected. */
void    aiml_mirror_session_send_rpc(void* session, int32_t net_id, const char* method, int32_t arg);

/* Send an RPC whose payload is a typed model instance encoded via aiml_ser_encode.
 * `obj`     — pointer to the model struct (caller-owned).
 * `nodes/n` — model schema (e.g. __aiml_sernodes_<Type>). */
void    aiml_mirror_session_send_rpc_model(void* session, int32_t net_id,
                                           const char* method, int32_t arg_i32,
                                           const void* obj,
                                           const AimlTypeNode* nodes,
                                           int32_t n_nodes);

/* Decode a received typed-RPC payload into a caller-allocated, zero-initialised
 * struct. Heap fields inside the decoded struct must be released with
 * aiml_ser_free_decoded() when no longer needed. Returns 0 on success, -1 on error. */
int32_t aiml_rpc_event_decode_into(void* ev, void* out,
                                   const AimlTypeNode* nodes, int32_t n_nodes);
/* Transactional decode (spec/44_5): failure leaves `out` untouched; success releases its old heap. */
int32_t aiml_rpc_event_decode_tx(void* ev, void* out, int64_t out_size,
                                 const AimlTypeNode* nodes, int32_t n_nodes);

/* Poll pending RPCs (PKT_RPC received from peers).  Event lifetime: the AimlRpcEvent* returned by
 * next_rpc/poll_rpc is BORROWED until the next next_rpc/poll_rpc call or session close (the runtime
 * frees it then).  Never free it yourself. */
int32_t aiml_mirror_session_has_rpc (void* session);
void*   aiml_mirror_session_next_rpc(void* session);
/* Combines has_rpc + next_rpc. Returns AimlRpcEvent* with method=="" if queue empty. Never null. */
void*   aiml_mirror_session_poll_rpc(void* session);

/* spec/44_5 session.take_rpc(method, target): remove + decode the FIRST queued RPC with `method`
 * whose payload is of the target's model type; every other event keeps its position.  A queue
 * filter for pushes / legacy replies — NOT a request correlation (use request ids for that).
 * 1 = taken, 0 = none (target untouched). */
int32_t aiml_mirror_session_take_rpc(void* session, const char* method, void* out, int64_t out_size,
                                     const AimlTypeNode* nodes, int32_t n_nodes);

/* spec/44_5 §7 request/response (RPC v3).  reply_rpc answers the request in `ev` (sender peer +
 * request id from the event); obj/nodes may be null for an i32-only reply. */
void    aiml_mirror_session_reply_rpc(void* session, void* ev, const char* method, int32_t arg_i32,
                                      const void* obj, const AimlTypeNode* nodes, int32_t n_nodes);
/* Async request: id > 0, or 0 when not sent (no v3 peer).  take_response: 1 = decoded,
 * 0 = not yet, -1 = protocol error (request finished).  cancel_request drops interest. */
int64_t aiml_mirror_session_send_request(void* session, int32_t target, const char* method, int32_t arg_i32,
                                         const void* obj, const AimlTypeNode* nodes, int32_t n_nodes);
int32_t aiml_mirror_session_take_response(void* session, int64_t id, const char* expect_method,
                                          void* out, int64_t out_size,
                                          const AimlTypeNode* nodes, int32_t n_nodes);
void    aiml_mirror_session_cancel_request(void* session, int64_t id);

/* Client convenience: send a typed-RPC request to the server, then wait up to `timeout_ms`
 * for the reply of THIS request (see spec/44_5 for the correlation rules).  On a match the
 * payload is decoded into `out` and 1 is returned; 0 on timeout / error.  Unrelated RPCs
 * stay queued in their order.
 *
 * Used by the `session.send_and_await(...)` compiler-builtin so AIML
 * code doesn't have to hand-roll the send + poll-loop + decode triple
 * for every request type. */
int32_t aiml_mirror_session_send_and_await(
    void* session, int32_t net_id,
    const char* method, int32_t arg_i32,
    const void* req_obj, const AimlTypeNode* req_nodes, int32_t req_n_nodes,
    const char* expect_method,
    void* out_obj, int64_t out_size, const AimlTypeNode* out_nodes, int32_t out_n_nodes,
    int32_t timeout_ms);

/* ── mirror_event field accessors ─────────────────────────────────────────── */

int32_t     aiml_mirror_type_enum   (const char* type);  /* type-string → MirrorEventType enum (0=none) */
const char* aiml_mirror_event_type  (void* ev);  /* "peer_connected" | "peer_disconnected" |
                                                     "object_registered" | "object_unregistered" */
void*       aiml_mirror_event_peer  (void* ev);  /* AimlMirrorPeer* (null for object_* events) */
int32_t     aiml_mirror_event_net_id(void* ev);  /* net_id (0 for peer_* events) */

#ifdef __cplusplus
}
#endif
