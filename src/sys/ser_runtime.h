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
    AIML_TD_MODEL      = 11, // AIML v2: a refcounted MODEL reference (%M*). Same wire format as
                             //   PTR_STRUCT (marker + struct bytes) so on-disk/wire is unchanged,
                             //   but decode allocates the shell via aiml_model_alloc (MODL header +
                             //   refcount 1) and free decrefs it (frees at 0). param1=inner struct
                             //   subtree; elem_size=sizeof(inner struct).
    AIML_TD_LIST_PTR   = 12, // spec/61 §5 Option B (memV3 only): a list<T> FIELD stored as a
                             //   POINTER to a refcounted handle ([rc][pad][{size,cap,data}]).
                             //   param1=the inner AIML_TD_LIST node index. The slot is 8 bytes.
                             //   Emitted only under memV3 → OFF/deployed descriptors never carry it.
    AIML_TD_MAP_PTR    = 13, // spec/61 §5 Option B (memV3 only): a map<K:V> FIELD stored as a
                             //   POINTER to a refcounted handle ([rc][pad][{size,cap,occ,keys,vals}]).
                             //   param1=the inner AIML_TD_MAP node index. Emitted only under memV3.
    AIML_TD_BUFFER_PTR = 14, // spec/61 §5 Option B (memV3 only): a buffer FIELD stored as a
                             //   POINTER to a refcounted handle ([rc][pad][{size,cap,r,w,data}]).
                             //   param1=the inner AIML_TD_BUFFER node index. Emitted only under memV3.
    AIML_TD_PTR_REF    = 15, // spec/61 §5.1 (memV3 only): an owning ptr<T> to a RECURSIVE type (T
                             //   is mid-expansion on the DFS path, so its subtree cannot be inlined
                             //   in the static descriptor). param1 = T's runtime TYPE-ID (registered
                             //   via aiml_ser_register_type); param2 = 1 if T is a refcount MODEL
                             //   (%M* shell), else 0 (plain heap box); elem_size = sizeof(T). The
                             //   clone/free walkers look T's descriptor up at runtime and recurse
                             //   (memoized), so a finite recursive chain is deep-cloned/freed.
    AIML_TD_WEAK_REF   = 16, // round4 §2 (memV3 CLONE descriptors only): a ref<T> weak edge. A leaf
                             //   (pointer slot, no subtree). The clone walker copies the pointer,
                             //   then in a second phase redirects it to the clone of an internal
                             //   target (via CloneContext.relocated); no incref, not freed at drop.
    AIML_TD_TENSOR     = 17, // round4 §3 (memV3 CLONE descriptors only): an owned `tensor` handle. A
                             //   leaf; the clone walker deep-copies it via aiml_tensor_clone.
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

// Returns 0 on success, -1 on a structural decode error (truncated/overlong field,
// absurd list/map count, corrupt blob), -2 on a schema-fingerprint mismatch (stale
// data from a different struct layout).  On ANY non-zero return the caller MUST NOT
// use *out as a valid value and MUST NOT shadow-copy / memcpy from it; *out may hold
// a PARTIALLY-decoded object, so the caller is responsible for releasing it with
// aiml_ser_free_decoded() (then zero it) before reuse.
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

// Retain (incref) every rc-bearing field of *obj* (strings, containers, shared/model refs) — the
// mirror of aiml_ser_free_decoded. Used after a shallow bit-copy so the copy co-owns the referents
// with a balanced refcount (spec/61 §7). Does NOT allocate.
void    aiml_ser_retain(void*                obj,
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

// ── spec/61 §5.1 memoized deep-clone (in-memory, no blob) ─────────────────────
// Like aiml_ser_deep_copy but walks src→dst directly (no encode/decode roundtrip)
// and MEMOIZES AIML_TD_MODEL (shared<T>/refcount) references by source pointer:
// a sub-object reached through two owning edges is cloned ONCE and the second
// edge increfs the already-cloned object (DAG-preserving, cycle-terminating).
// Strings are shared (incref, immutable per §4).  ptr<T> (PTR_STRUCT) stays
// unique — each is cloned independently.  dst must be zero-initialised.
// Kept separate from aiml_ser_deep_copy so the deployed mirror/KV wire format
// (encode/decode) is untouched.
void    aiml_clone_deep(void*                dst,
                        const void*          src,
                        const AimlTypeNode*  nodes,
                        int32_t              n_nodes);

// spec/61 §5.1: register a model type's descriptor under a runtime TYPE-ID, so the clone/free
// walkers can follow an AIML_TD_PTR_REF (a recursive ptr<T> edge that the static descriptor could
// not inline). Called once per referenced recursive type from the module-init prologue.
void    aiml_ser_register_type(int32_t              type_id,
                               const AimlTypeNode*  nodes,
                               int32_t              n_nodes,
                               int32_t              struct_size);

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

// Debug: walk a live struct via its type descriptor and print every field
// (i-values, string content+len with an AimlStrHeader-magic sanity check, list
// counts + element size, nested structs) to stderr.  String pointers whose
// header magic doesn't match are reported as **INVALID** instead of being
// strlen'd — so a corrupt/garbage pointer from a bad decode is surfaced rather
// than crashing the dump.  Bound to the `sys.dump(model)` builtin.
void    aiml_ser_dump(const void*          obj,
                      const AimlTypeNode*  nodes,
                      int32_t              n_nodes);

#ifdef __cplusplus
}
#endif
