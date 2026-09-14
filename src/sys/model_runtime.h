#pragma once
#include <cstdint>
#include <cstddef>  // size_t

// ── AIML v2 reference-counted MODEL heap objects ─────────────────────────────
// See doc/aiml_v2_memory_model.md. A value-model with heap fields becomes a
// shared, reference-counted heap object: binding / passing / returning / element
// extraction SHARE it (incref), mutation is in-place and visible to every holder,
// and it is freed EXACTLY when its refcount hits 0 — never "who dropped first".
// This removes the value-model double-free/UAF class by construction.
//
// The scheme mirrors refcounted strings (string_runtime.*): an 8-byte header is
// prefixed to the allocation and the RETURNED pointer skips it, so compiled code
// sees a normal struct pointer. incref/decref magic-guard the header at `obj-8`
// and no-op on any pointer that isn't a MODL allocation (a stack model, a
// borrowed view, etc.) — false-positive risk ~2^-32.
//
// decref takes the model's serNodes (the flat AimlTypeNode array the compiler
// already threads to aiml_ser_free_decoded at every free site) so that, at
// refcount 0, it can deep-free the inner heap (strings decref, nested models
// decref, lists/maps free) before releasing the shell.

extern "C" {

// Magic marking a MODL allocation header ("MODL" in little-endian memory).
// (0x4D='M' 0x4F='O' 0x44='D' 0x4C='L')
// Exposed for tests / assertions; the value is an implementation detail.
#define AIML_MODEL_MAGIC 0x4C444F4Du

// Allocate a reference-counted model shell of `structSize` bytes (the struct's
// own size, WITHOUT the header). The struct bytes are zero-initialised (all heap
// pointer fields NULL — the aiml_ser_* precondition). refcount starts at 1.
// Returns the struct pointer (header hidden at obj-8), or nullptr on OOM.
void* aiml_model_alloc(size_t structSize);

// refcount++ (magic-guarded, null-safe, no-op on non-MODL pointers).
void  aiml_model_incref(void* obj);

// refcount-- ; at 1 it deep-frees the inner heap via aiml_ser_free_decoded(obj,
// nodes, n_nodes), poisons the header, and frees the shell. Magic-guarded and
// null-safe; a no-op on non-MODL pointers (a stack model / borrowed view passed
// here by a conservative call site simply isn't freed by refcount).
void  aiml_model_decref(void* obj, const void* nodes, int32_t n_nodes);

// Current refcount of a MODL object, or 0 if `obj` is null / not a MODL
// allocation. Diagnostics + sanitizer probes only.
uint32_t aiml_model_refcount(const void* obj);

} // extern "C"
