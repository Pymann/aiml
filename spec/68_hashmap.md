# 68 — `hashmap<K:V>`: hash table

Status: implemented (native targets, JIT and AOT; the runtime is also listed in the WASM runtime).
Compiler: `src/core/hashmap_impl.{h,cpp}`. Runtime: `src/sys/hashmap_runtime.cpp`.
Tests: `tests/test_hashmap.aiml`, `tests/test_hashmap_lifetime.aiml`.

## 1. Purpose

`map<K:V>` (spec/01, `src/core/map_impl.cpp`) stores keys and values in two parallel arrays and
finds a key by **linear search**. Every `get`, `put` and `contains` is O(n), so filling a map
is O(n²). At 20,000 string keys one put plus one get per key takes about 1.1 s, while 2,000,000
keys with `hashmap` take about 2.1 s.

`hashmap<K:V>` is a real hash table (`std::unordered_map` in the runtime) with an expected
O(1) per access. It has the same interface as `map`, plus `add`, `clear` and `keys`. `map`
stays as it is: it is a value container with an inline layout that persistence and
serialisation use.

| | `map<K:V>` | `hashmap<K:V>` |
|---|---|---|
| Lookup | linear, O(n) | hashed, O(1) expected |
| Storage | inline struct (value container) | opaque refcounted handle |
| Copy `decl: [b, T, a]` | own copy | **shares** the table (like `chan<T>`) |
| Serialisation / `persistent` | yes | no (in-process only) |
| Iteration order | insertion | none; `keys` returns keys **sorted** |

## 2. Types

```yaml
- decl: [counts, "hashmap<string:i32>"]
- decl: [names,  "hashmap<i64:string>"]
- decl: [table,  "hashmap<string:i32>", { eins: 1, zwei: 2 }]   # literal
```

- **K:** `string`, `i8`…`i64`, `u8`…`u64`, or an enum. Integer keys are stored as i64.
- **V:** integer types, `bool`, enum, `f32`, `f64` or `string`.
- Other types (models, lists, `any`) are a compile error. To store a model, keep it in a
  `list<T>` and put its index into the hashmap.

Quote the type in YAML (`"hashmap<string:i32>"`), as with `map`.

## 3. Methods

| Method | Result | Notes |
|---|---|---|
| `put(k, v)` / `set(k, v)` | — | inserts or overwrites |
| `get(k)` | `V` | **missing key → `0` / `0.0` / `false` / `""`**, never null or a crash |
| `add(k, d)` | `V` | integer values only: `v += d`, missing key counts as 0; returns the new value (a counter in one call) |
| `contains(k)` / `has(k)` | `i32` 0/1 | |
| `remove(k)` | `i32` 0/1 | 1 if the key was there |
| `len()` / `size()`, property `.len` | `i32` | |
| property `.empty` | `i32` 0/1 | |
| `clear()` | — | removes all entries |
| `keys()` | `list<K>` | sorted, so the result is deterministic whatever the hash order |

String values: `put` stores its own RC copy. `get` returns a **borrow**, and a local bound to it
(`decl: [s, string, { call: [t.get, k] }]`) increfs it, so `s` stays valid after a later
`put` or `remove` (spec/61 §7).

## 4. Lifetime (spec/61, Class 5 like `chan<T>`)

A `hashmap` slot holds a pointer to a refcounted table.

- **Creation:** a local, member or global without an initialiser gets a new empty table.
- **Binding from a named source** (`decl: [b, T, a]`, `set: [b, a]`, argument, member): retain.
  Both names then see the **same** table.
- **Scope drop / overwrite / owner drop:** release. The last owner frees the table and its
  string values. Local scope-drop kind is 23.
- **Return:** moved out, no retain.
- **`spawn` argument:** retained for the thread and released after it ends. The table is
  **not** thread-safe. Concurrent writes need a `mutex`, or pass values through a `chan`.
- **Model field:** type descriptor `AIML_TD_HASHMAP` (27). Free is release, share is retain,
  and **clone is a deep copy** (`aiml_hashmap_clone`). Encode and decode carry only the
  in-process pointer. A model with a hashmap field is not wire-serialisable
  (`serialize_impl.cpp`).

## 5. Runtime ABI

```c
void*    aiml_hashmap_new(int32 key_kind, int32 val_kind);   // key 0=string 1=int; val 0=int 1=float 2=string
void     aiml_hashmap_retain(void*); void aiml_hashmap_release(void*); void* aiml_hashmap_clone(void*);
void     aiml_hashmap_put_bits(void*, const char* sk, int64 ik, uint64 bits);
void     aiml_hashmap_put_str (void*, const char* sk, int64 ik, const char* v);
uint64   aiml_hashmap_get_bits(void*, const char* sk, int64 ik);
const char* aiml_hashmap_get_str(void*, const char* sk, int64 ik);  // borrow; "" if missing
int64    aiml_hashmap_add(void*, const char* sk, int64 ik, int64 delta);
int32    aiml_hashmap_contains(void*, ...); int32 aiml_hashmap_remove(void*, ...);
int32    aiml_hashmap_len(void*); void aiml_hashmap_clear(void*);
int32    aiml_hashmap_keys_fill(void*, void* list, int32 elem_size);
```

The key is passed in either `sk` (string keys) or `ik` (integer keys), and the other one is
ignored. Values are 64-bit patterns: integers are sign- or zero-extended, and floats are
`double` bits.

## 6. Measured (29.10.2026, AOT, -O1, string keys, one put plus one get per key)

| n | `map` | `hashmap` |
|---|---|---|
| 20,000 | 1,082 ms | 27 ms |
| 200,000 | — (~2 min, extrapolated) | 123 ms |
| 2,000,000 | — | 2,103 ms |

Valgrind shows 0 bytes definitely lost and 0 errors for both tests.

## 7. Open points

- No `for`-iteration over a hashmap. Use `keys()` and then `get`.
- No `persistent` or serialisation. For persistent key-value data use AionDB `Kv` (spec/AionDB).
- Concurrent access is not synchronised (§4).
