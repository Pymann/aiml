# Chapter 45 — Heap Serialization

> **Core idea:** When AIML data leaves the current process memory — into a WAL file,
> over a network socket, or into a message channel — heap pointers must be replaced by
> byte streams. This conversion happens fully automatically, transparently
> for the AIML code, and is recursive for arbitrarily deeply nested types.

---

## 1. The Problem

Many AIML types contain **heap pointers**: addresses that are only valid in the current process.

```aiml
- class:
    name: Card
    members:
      - [name,     string]      # char* → heap address
      - [mkp,      i32]         # plain value, no pointer
      - [tags,     list<string>] # AimlList* → heap address
      - [metadata, map<string:string>] # i8* → heap address
```

When a `Card` struct is serialized via `memcpy` (as the existing
`Kv<string:Card>` or `mirror.register` does internally), the raw pointer values
are stored:

```
WAL file: [..., 0x7fff12345678, 42, 0x7fff87654321, ...]
                 ↑ name pointer     ↑ tags pointer
```

After a restart (or in a different process), these addresses are **invalid**.
The result is a segfault or garbage data.

Affected are all systems that move structs across process boundaries or lifetime boundaries:

| System | Boundary |
|--------|----------|
| `Kv<string:T>` with persist | WAL → restart |
| `mirror` namespace | Process A → process B (network) |
| `chan<T>` (future) | Thread handoff with ownership |

---

## 2. Solution: Automatic Heap Serialization

The compiler knows the complete field structure of every type. It emits
a **type descriptor** as a static constant array in the LLVM module.
The runtime uses this descriptor to:

- **Encode**: struct → byte blob (all heap pointers replaced by their data)
- **Decode**: byte blob → struct (heap memory reallocated, pointers restored)

The AIML code changes **nothing**:

```aiml
# Identical code — whether with or without string fields:
- decl: [kv, Kv<string:Card>]
- set:  [kv, { call: [db.open_kv, "cards", {persist: true, path: "./db/"}] }]
- call: [kv.set, "abc", my_card]     # → internally: encode(my_card)
- decl: [c, Card]
- set:  [c, { call: [kv.get, "abc"] }]   # → internally: decode(blob)
```

---

## 3. The Type Descriptor

### 3.1 Design Principle: Flat Node Array

A recursive C struct with pointers would be difficult to emit as an LLVM constant
global. Instead, a **flat array of `AimlTypeNode`** is used,
where children are referenced by their integer index.

```c
typedef enum {
    AIML_TD_I8       = 0,   // 1 byte
    AIML_TD_I16      = 1,   // 2 bytes
    AIML_TD_I32      = 2,   // 4 bytes
    AIML_TD_I64      = 3,   // 8 bytes
    AIML_TD_F32      = 4,   // 4 bytes
    AIML_TD_F64      = 5,   // 8 bytes
    AIML_TD_STRING   = 6,   // char*  → [i32 len][bytes]
    AIML_TD_STRUCT   = 7,   // param1=n_fields, param2=index_of_first_field_node
    AIML_TD_LIST     = 8,   // param1=child_index (element type)
    AIML_TD_MAP      = 9,   // param1=key_child, param2=value_child
    AIML_TD_BUFFER   = 10,  // param1=child_index (element type)
    AIML_TD_RAWBYTES = 11,  // param1=byte_count (for vec3, mat4, embedded value structs)
} AimlTDKind;

typedef struct {
    int32_t kind;     // AimlTDKind
    int32_t param1;   // meaning depends on kind (see above)
    int32_t param2;   // meaning depends on kind (see above)
    int32_t offset;   // byte offset of this field in the parent struct
                      // (only relevant for fields within a STRUCT)
} AimlTypeNode;
```

### 3.2 Encoding as a Flat Array

Each type is encoded as a contiguous node sequence. The root node is always
at index 0. Children follow in depth-first order (DFS order).

**Example: `Card { name:string, mkp:i32, tags:list<string> }`**

```
Index | kind     | param1 | param2 | offset
------+----------+--------+--------+-------
  0   | STRUCT   |   3    |   1    |   0     ← root: 3 fields, first from index 1
  1   | STRING   |   0    |   0    |   0     ← field "name" at offset 0
  2   | I32      |   0    |   0    |   8     ← field "mkp" at offset 8
  3   | LIST     |   4    |   0    |  16     ← field "tags" at offset 16, elem type from index 4
  4   | STRING   |   0    |   0    |   0     ← element type of tags
```

**Example: `Stats { scores:list<list<i32>>, label:string }`**

```
Index | kind     | param1 | param2 | offset
------+----------+--------+--------+-------
  0   | STRUCT   |   2    |   1    |   0
  1   | LIST     |   3    |   0    |   0     ← field "scores", elem type from index 3
  2   | STRING   |   0    |   0    |   8     ← field "label"
  3   | LIST     |   4    |   0    |   0     ← outer list → elem from index 4
  4   | I32      |   0    |   0    |   0     ← innermost element
```

**Example: `Config { data:map<string:list<i32>>, count:i64 }`**

```
Index | kind   | param1 | param2 | offset
------+--------+--------+--------+-------
  0   | STRUCT |   2    |   1    |   0
  1   | MAP    |   3    |   4    |   0     ← field "data", key type=3, val type=4
  2   | I64    |   0    |   0    |   8     ← field "count"
  3   | STRING |   0    |   0    |   0     ← key type of map
  4   | LIST   |   5    |   0    |   0     ← value type of map
  5   | I32    |   0    |   0    |   0     ← element type of list
```

### 3.3 Recursion over Nesting

The serializer and deserializer work **recursively** over the node array:

```
serialize(node_idx, data_ptr):
  node = nodes[node_idx]
  switch node.kind:
    STRUCT:
      for i in range(node.param1):
        field = nodes[node.param2 + i]
        serialize(node.param2 + i, data_ptr + field.offset)
    LIST:
      list = *(AimlList*)(data_ptr)
      write i32: list.len
      elem_size = size_of(nodes[node.param1])
      for i in range(list.len):
        serialize(node.param1, list.data + i * elem_size)
    STRING:
      str = *(char**)(data_ptr)
      len = strlen(str)
      write i32: len
      write bytes: str[0..len]
    I32:   write 4 bytes
    I64:   write 8 bytes
    ...
    MAP:
      m = *(AimlMap*)(data_ptr)
      write i32: m.count
      for each (k, v) in m:
        serialize(node.param1, &k)
        serialize(node.param2, &v)
    BUFFER:
      buf = *(AimlBuffer*)(data_ptr)
      write i32: buf.size
      write bytes: buf.data[0..buf.size]
    RAWBYTES:
      write node.param1 bytes raw
```

---

## 4. Binary Wire Format

The generated blob has the following structure:

```
[i32 total_data_len]   ← length of the data part (excluding these 4 bytes)
[field_data...]        ← recursively encoded fields
```

Encoding rules per node type:

```
STRING          →  [i32 len][len bytes]              (no null terminator)
I8 / U8         →  [1 byte]
I16 / U16       →  [2 bytes little-endian]
I32 / U32 / F32 →  [4 bytes little-endian]
I64 / U64 / F64 →  [8 bytes little-endian]
STRUCT          →  fields in declaration order, recursively
LIST            →  [i32 count] [element_0] [element_1] ...
MAP             →  [i32 count] ([key] [value]) × count
BUFFER          →  [i32 byte_count] [raw bytes]
RAWBYTES(N)     →  [N raw bytes]     (no length — N is fixed in the descriptor)
```

All multi-byte integers: **little-endian**.

---

## 5. C API: `ser_runtime.h`

```c
// Encode: produces a malloc'd blob = [i32 total_len][data...]
// Caller is responsible for free(blob).
char* aiml_ser_encode(const void* obj, const AimlTypeNode* nodes, int32_t n_nodes);

// Decode: fills a pre-allocated out buffer.
// Strings are strdup'd, lists/maps/buffers are malloc'd.
// Must be cleaned up with aiml_ser_free_decoded.
void aiml_ser_decode(const char* blob, int32_t blob_len,
                     void* out,
                     const AimlTypeNode* nodes, int32_t n_nodes);

// Frees all heap resources allocated by aiml_ser_decode.
// Does not free the out buffer itself (it belongs to the caller).
void aiml_ser_free_decoded(void* obj, const AimlTypeNode* nodes, int32_t n_nodes);

// Helper: computes the size of the encoded blob without allocating.
int32_t aiml_ser_encoded_size(const void* obj, const AimlTypeNode* nodes, int32_t n_nodes);
```

---

## 6. Compiler Integration

### 6.1 Descriptor Emission in `registerTypeDecl`

For every `class:` or `model:` with at least one heap field, the compiler emits
a private constant global:

```llvm
@__aiml_sernodes_Card = private constant [5 x { i32, i32, i32, i32 }] [
  { i32 7, i32 3, i32 1, i32 0 },   ; STRUCT, 3 fields, from index 1
  { i32 6, i32 0, i32 0, i32 0 },   ; STRING, offset=0
  { i32 2, i32 0, i32 0, i32 8 },   ; I32, offset=8
  { i32 8, i32 4, i32 0, i32 16 },  ; LIST, elem=4, offset=16
  { i32 6, i32 0, i32 0, i32 0 },   ; STRING (element type)
]
```

Stored in `ClassInfo`:
```cpp
llvm::GlobalVariable* serNodesVar   = nullptr;  // @__aiml_sernodes_TypeName
int32_t               serNodesCount = 0;
bool                  hasHeapFields = false;
```

`hasHeapFields` is `true` if at least one field (or a transitively nested
field) contains a heap pointer: `string`, `list<T>`, `map<K:V>`, `buffer<T>`.

### 6.2 Field Type → Node Kind Mapping

| AIML Type         | AimlTDKind    | Subtypes (param1/param2)          |
|-------------------|---------------|-----------------------------------|
| `string`          | TD_STRING     | —                                 |
| `i8`, `u8`        | TD_I8         | —                                 |
| `i16`, `u16`      | TD_I16        | —                                 |
| `i32`, `u32`      | TD_I32        | —                                 |
| `i64`, `u64`      | TD_I64        | —                                 |
| `f32`             | TD_F32        | —                                 |
| `f64`             | TD_F64        | —                                 |
| `list<T>`         | TD_LIST       | param1 = child_index(T)           |
| `map<K:V>`        | TD_MAP        | param1 = child(K), param2 = child(V) |
| `buffer<T>`       | TD_BUFFER     | param1 = child_index(T)           |
| `vec2/vec3/vec4`  | TD_RAWBYTES   | param1 = sizeof (8/12/16)         |
| `mat4`            | TD_RAWBYTES   | param1 = 64                       |
| User class T      | TD_STRUCT     | param1=n, param2=first_field_idx  |

Nested user classes are embedded inline as STRUCT nodes (DFS).

### 6.3 Usage by `db.open_kv`

When the value type has `hasHeapFields == true`:

```aiml
# AIML code (unchanged):
- decl: [kv, Kv<string:Card>]
- set:  [kv, { call: [db.open_kv, "cards", {persist: true, path: "./db/"}] }]

# Emitted LLVM IR (simplified):
%kv = call i8* @aiml_db_kv_open_typed_persist(
    i8* "cards",
    i64 sizeof(Card),
    i8* "./db/",
    i8* bitcast(@__aiml_sernodes_Card to i8*),
    i32 5   ← n_nodes
)
```

### 6.4 Usage by `mirror.register`

```aiml
# AIML code (unchanged):
- call: [sess.register, my_player]

# With hasHeapFields == true → emits:
call void @aiml_mirror_session_register_typed(
    i8* sess,
    i8* player_ptr,
    i64 sizeof(Player),
    i8* bitcast(@__aiml_sernodes_Player to i8*),
    i32 n_nodes
)
```

---

## 7. Runtime Integration

### 7.1 `Kv<string:T>` with Heap Fields

`AimlKv` stores the descriptor:

```cpp
struct AimlKv {
    int64_t             val_size;
    const AimlTypeNode* ser_nodes  = nullptr;   // null → previous path
    int32_t             ser_n      = 0;
    // Decoded cache: deserialized structs for stable kv_get pointer
    std::unordered_map<std::string, std::vector<uint8_t>> decoded_cache;
    // ...
};
```

**`kv_set(key, val)`**:
- `ser_nodes != null` → `aiml_ser_encode(val, nodes, n)` → store blob in map
- invalidate decoded_cache[key]
- WAL: write blob as byte sequence as before (format unchanged)

**`kv_get(key)`**:
- `ser_nodes != null` → blob from map → check decoded_cache → on miss: `aiml_ser_decode` → store in decoded_cache → return pointer
- Pointer in decoded_cache remains stable until next `kv_set` / `kv_delete`

**WAL replay**:
- Blobs are read as raw bytes (format already correct: `[val_bytes:u32][data]`)
- Decode happens lazily on first `kv_get`

### 7.2 `mirror` Network Sync

`AimlMirrorRegistered` stores the descriptor:

```cpp
struct AimlMirrorRegistered {
    void*               obj_ptr   = nullptr;
    int64_t             obj_size  = 0;
    uint8_t*            shadow    = nullptr;
    int32_t             net_id    = 0;
    bool                is_owner  = false;
    float               sync_rate = 0.0f;
    bool                manual_sync = false;
    const AimlTypeNode* ser_nodes = nullptr;   // null → raw memcpy (previous behavior)
    int32_t             ser_n     = 0;
};
```

**`send_delta_pkt`** (with `ser_nodes != null`):
- `aiml_ser_encode(obj_ptr, nodes, n)` → blob
- Packet: `[header 6 bytes][i32 blob_len][blob_data]`
- Shadow update: `aiml_ser_encode` of the same object → store in shadow

**`process_packet` PKT_DELTA receive** (with `ser_nodes != null`):
- Read blob from packet: `[i32 blob_len][blob_data]`
- `aiml_ser_decode(blob_data, blob_len, obj_ptr, nodes, n)`

**Dirty detection** (`memcmp` in `update()`):
- Remains raw-bytes based for now (acceptable false-positives for pointer changes)
- v2: store shadow as blob, blob comparison

---

## 8. Scope

### In scope (v1)
- All primitive types: `i8`, `i16`, `i32`, `i64`, `f32`, `f64`, `string`
- `list<T>` for all primitive T + `string` + user-defined structs
- `map<string:string>`, `map<string:T>` (T primitive or struct)
- `buffer<u8>` (and `buffer<i8>`)
- Pure-value structs as fields (`vec2`, `vec3`, `vec4`, `mat4`) → RAWBYTES
- User-defined structs as fields (embedded STRUCT nodes)
- Arbitrary nesting depth (`list<list<list<string>>>` etc.)
- `Kv<string:T>` as a field in a `sync:` model (Kv contents are fully
  serialized / deserialized; the owner must call `sess.sync_now` after Kv mutation —
  automatic dirty detection for Kv pointers only in v2, see below)

### Not in scope (v2)
- `Store<T>` persistence (uses mmap, different mechanism)
- `chan<T>` thread handoff
- Mirror dirty detection as blob comparison (fixes automatic detection of
  Kv / map / list in-place mutations without explicit `sync_now`)

### `ptr<T>` fields and recursive / back-reference types

A `ptr<T>` (or `ref<T>`) field pointing at a **registered user struct** is followed:
the descriptor emits a `PTR_STRUCT` node wrapping `T`'s subtree, and the runtime
allocates/follows through the pointer at encode/decode time (the raw pointer value
is meaningless on the receiving side).

Following pointers makes the type graph **potentially cyclic**. A component that
holds a back-reference to its owner is the common case:

```aiml
- class: { name: Owner,  members: [[child, ptr<Child>]] }
- class: { name: Child,  members: [[owner, ptr<Owner>]] }   # back-ref
```

The descriptor builder (`serBuildSubtree`) keeps a **DFS active-path set** of the
struct types currently being expanded. When a `ptr<T>` points back at a type
already on that path, the pointer is **not** followed — it is emitted as an opaque
pointer-sized `RAWBYTES` leaf and left out of the serialized data. Rationale: a
back-reference is a *wired reference*, not owned data, so cutting it there both
terminates the walk and avoids duplicating the owner. (Same principle as the
classgroup-collaborator skip.) A type is still followed normally when it recurs in
a *sibling* subtree — only genuine ancestor cycles are cut. This makes
back-reference components (e.g. a screen ↔ its extracted sub-view) compile; before
the guard the builder recursed forever and crashed. Regression:
`tests/test_recursive_ptr_ser.aiml`.

---

## 9. Example: 9Rooms Backend

```aiml
- class:
    name: Card
    members:
      - [name,        string]
      - [card_type,   string]
      - [mkp,         i32]
      - [description, string]
      - [abilities,   string]
      - [lp,          i32]
      ...

# Emitted descriptor (simplified):
# @__aiml_sernodes_Card = [
#   {STRUCT, 15, 1, 0},     ← 15 fields, from index 1
#   {STRING,  0, 0, 0},     ← name
#   {STRING,  0, 0, 8},     ← card_type
#   {I32,     0, 0, 16},    ← mkp
#   {STRING,  0, 0, 20},    ← description
#   {STRING,  0, 0, 28},    ← abilities
#   {I32,     0, 0, 36},    ← lp
#   ...
# ]

# AIML code unchanged — persistence works correctly across restarts:
- decl: [kv, Kv<string:Card>]
- set:  [kv, { call: [db.open_kv, "9r_cards", {persist: true, path: "./aiondb/"}] }]
- call: [kv.set, card_id, my_card]   # serialized automatically
```

---

## 10. Implementation Order

| # | File | Contents |
|---|------|----------|
| 1 | `src/sys/ser_runtime.h` | `AimlTypeNode`, `AimlTDKind`, C API |
| 2 | `src/sys/ser_runtime.cpp` | `aiml_ser_encode`, `aiml_ser_decode`, `aiml_ser_free_decoded` |
| 3 | `src/sys/db_runtime.h` | New functions `aiml_db_kv_open_typed[_persist]` |
| 4 | `src/sys/db_runtime.cpp` | Typed Kv + decoded_cache + WAL integration |
| 5 | `src/sys/mirror_runtime.h` | `aiml_mirror_session_register_typed`, `register_id_typed` |
| 6 | `src/sys/mirror_runtime.cpp` | Typed send/recv |
| 7 | `src/core/context.h` | `ClassInfo`: `serNodesVar`, `serNodesCount`, `hasHeapFields` |
| 8 | `src/core/ast_compiler.cpp` | Emit descriptor global in `registerTypeDecl` |
| 9 | `src/core/builtin_registry.cpp` | `db.open_kv` → typed variant when `hasHeapFields` |
| 10 | `src/core/ast_compiler_call.cpp` | `sess.register` → typed variant when `hasHeapFields` |
| 11 | `compiler.cpp` | `#include ser_runtime.h` + 5 new `AddSymbol` calls |
| 12 | `tests/test_heap_ser.aiml` | Unit tests for all types |
| 13 | `tests/test_mirror.aiml` | Extend: phase with string fields |
