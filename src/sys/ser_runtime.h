#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// ── Type Descriptor Kinds ─────────────────────────────────────────────────────
//
// A type is described as a flat array of AimlTypeNode (DFS order).
// Children are referenced by their index in the array, not by pointer,
// so the whole descriptor can be emitted as a single LLVM constant global.
//
// Root node is always at index 0.

typedef enum {
    AIML_TD_I8       =  0,   // 1 byte
    AIML_TD_I16      =  1,   // 2 bytes
    AIML_TD_I32      =  2,   // 4 bytes  (also u32, f32)
    AIML_TD_I64      =  3,   // 8 bytes  (also u64, f64)
    AIML_TD_STRING   =  4,   // char*    → [i32 len][bytes]  (no null terminator in blob)
    AIML_TD_STRUCT   =  5,   // param1=n_fields  param2=index_of_first_field_node
    AIML_TD_LIST     =  6,   // param1=child_index (element type node)
    AIML_TD_MAP      =  7,   // param1=key_child   param2=value_child
    AIML_TD_BUFFER   =  8,   // param1=child_index (element type node)
    AIML_TD_RAWBYTES =  9,   // param1=byte_count  (vec2/vec3/mat4/value-only structs)
    AIML_TD_PTR_STRUCT = 10, // ptr<T>/ref<T> to a struct.
                             //   param1=child_index (the inner struct subtree)
                             //   elem_size=sizeof(inner struct) — used by decoder for calloc.
                             // Wire format: 1-byte marker (0=null, 1=non-null) + struct bytes.
} AimlTDKind;

// Each node is 24 bytes.  The whole descriptor for a type is an array of these.
typedef struct {
    int32_t kind;      // AimlTDKind
    int32_t param1;    // RAWBYTES: byte count
                       // STRUCT:   number of field child-nodes
                       // LIST/BUFFER: child index (element type)
                       // MAP:      key child index
    int32_t param2;    // STRUCT:   index of the first field node in this array
                       // MAP:      value child index
                       // (unused for all other kinds — set to 0)
    int32_t offset;    // byte offset of this field inside its parent struct
                       // (meaningful only for nodes that are direct children of STRUCT)
    int32_t elem_size; // LIST:   stride in bytes for one element in list->data
                       // MAP:    stride in bytes for one entry in map->values
                       // BUFFER: stride in bytes for one element in buf->data
                       // STRUCT: total byte size of the struct
                       //         (needed when struct appears as list/map element)
                       // Primitives / RAWBYTES: 0 (size implied by kind or param1)
    int32_t _pad;      // padding — must be 0
} AimlTypeNode;

// ── Encode ────────────────────────────────────────────────────────────────────
//
// Serialise *obj* into a heap-allocated blob:
//   blob[0..3]  = int32_t data_len  (little-endian, does NOT include these 4 bytes)
//   blob[4..]   = payload
//
// The caller owns the returned pointer and must free() it.

char*   aiml_ser_encode(const void*          obj,
                        const AimlTypeNode*  nodes,
                        int32_t              n_nodes);

// ── Decode ────────────────────────────────────────────────────────────────────
//
// Deserialise from *blob* (the full blob as returned by aiml_ser_encode,
// including the 4-byte length prefix) into the pre-allocated buffer *out*
// (must be at least sizeof(T) bytes, zero-initialised by the caller).
//
// Heap fields (strings, lists, maps, buffers) are newly allocated:
//   strings  → malloc'd + null-terminated
//   lists    → malloc'd SerList struct + malloc'd data array
//   maps     → malloc'd SerMap struct + malloc'd occupied/keys/values arrays
//   buffers  → malloc'd SerBuffer struct + malloc'd data array
//
// Use aiml_ser_free_decoded() to release these allocations before
// discarding *out*.  Does NOT free *out* itself.

// Returns 0 on success, -1 if a structural decode error was encountered
// (e.g. absurd list count, schema mismatch).  On error the caller MUST NOT
// use *out and MUST NOT call shadow-copy / memcpy from it.
int     aiml_ser_decode(const char*          blob,
                        void*                out,
                        const AimlTypeNode*  nodes,
                        int32_t              n_nodes);

// ── Free decoded heap fields ──────────────────────────────────────────────────
//
// Frees every heap allocation that aiml_ser_decode() created inside *obj*.
// Does NOT free *out* itself.  Only call on objects produced by aiml_ser_decode.

void    aiml_ser_free_decoded(void*                obj,
                              const AimlTypeNode*  nodes,
                              int32_t              n_nodes);

// ── Deep copy ────────────────────────────────────────────────────────────────
//
// Produces a deep copy of *src* into *dst*.  After this call, *dst* owns its
// own heap allocations (strings/lists/maps/buffers) — independent of *src*.
//
// **Precondition:** *dst* must be zero-initialised (all heap pointer fields
// NULL).  *dst* must be at least the same byte-size as the struct described
// by *nodes*.
//
// Used by the AIML compiler at `decl: [p, T, kv.get(...)]` sites to avoid
// shared heap ownership between the user's struct and the kv decoded cache —
// see doc/issue/kv_get_mutate_set_same_key_crash.md.
//
// Implemented as encode-then-decode roundtrip; cost is one extra
// serialise+deserialise pass.  Strings/list-buffers/etc. become fresh malloc'd
// allocations owned by *dst*.

void    aiml_ser_deep_copy(void*                dst,
                           const void*          src,
                           const AimlTypeNode*  nodes,
                           int32_t              n_nodes);

// ── Helpers ───────────────────────────────────────────────────────────────────

// Returns the byte length of the payload (the value stored in the 4-byte prefix),
// without allocating anything.
int32_t aiml_ser_encoded_size(const void*          obj,
                              const AimlTypeNode*  nodes,
                              int32_t              n_nodes);

// Schema-format guard toggle (thread-local).  Enabled ONLY by the persistent-KV
// layer around its encode/decode so stale on-disk blobs (written by an
// incompatible struct layout) are refused with a clear diagnostic instead of
// decoded as garbage.  OFF everywhere else (e.g. WS live messages).  Returns the
// previous value so callers can save/restore (nesting-safe).  See ser_runtime.cpp.
int     aiml_ser_set_guard(int on);

#ifdef __cplusplus
}
#endif
