# `sys.clone` — Generic Deep-Copy

## 1. Purpose

`sys.clone(x)` returns a deep copy of `x` — an independent value whose heap
allocations (strings, list buffers, map entries, nested structs/lists/maps)
are *not* shared with the source.  Mutating the source after `sys.clone` does
not affect the clone, and vice-versa.

Used when AIML's default pass-by-value of list/map structs would otherwise
leak shared backing buffers, or when a function needs to keep a snapshot of
a value that the caller will continue to mutate.

## 2. Signature

```aiml
- decl: [y, T, { call: [sys.clone, x] }]
```

- The argument (the same applies to the shallow `{ copy: x }`, `61_memory_management.md`
  §5.1) may be a **variable**, an **`at` element** (`{ at: [outer, i] }`), a container accessor
  (`.get` / `.at` / `.first` / `.last`), a **member access** (`obj.field`) or a **call** result.
  For anything but a plain variable the compiler takes the type from the typed target
  (`decl: [y, T, { clone: {at: …} }]` / `set: [y, …]`) and checks it against the operand.
- The operand is evaluated **exactly once**; the source stays alive while it is copied.
- An unsupported operand form, or a missing target type, is a **compile error** — never a silent
  empty or null result. (A genuinely empty source still yields an empty copy.)
- The result type matches the argument type.
- Returns a fresh value, suitable as init-value for `decl: [y, T, ...]` or
  as RHS of `set: [y, ...]`.

## 3. Supported Types

`sys.clone` works on every type that the binary serializer understands:

| Type                       | Cloned how                                        |
|----------------------------|---------------------------------------------------|
| Scalars (`i32`, `f64`, …)  | Value copy (trivially "deep")                     |
| `string`                   | Fresh `strdup` of payload                         |
| `list<T>`                  | Fresh data buffer + recursive clone of elements   |
| `map<K:V>`                 | Fresh hash table + recursive clone of values      |
| `buffer<T>`                | Fresh malloc + bytewise copy                      |
| User classes / models      | Fresh struct + recursive clone of heap fields     |
| Nested combinations        | Fully recursive                                   |

## 4. Unsupported Types

The same types that are not serializable cannot be cloned:

- `any`
- `chan<T>`
- `lock`, `thread_handle`
- `Code` (AIML bytecode object)
- `file_handle`, `process`, `socket_handle`, `tls_socket_handle`, `ws_handle`
- `tensor` — use the dedicated `tensor.clone(t)` (different storage model)
- `Kv<K:V>`, `Store<T>`, `VecStore` — AionDB opaque handles, not values

Cloning an unsupported type is a compile error.

## 5. Examples

### 5.1 List of integers

```aiml
- decl: [a, list<i32>]
- call: [a.push, 10]
- call: [a.push, 20]
- decl: [b, list<i32>, { call: [sys.clone, a] }]
- call: [b.push, 30]
# a.len = 2,  b.len = 3,  a[0] = 10,  b[0] = 10
```

### 5.2 Map of strings

```aiml
- decl: [m, map<string:string>]
- call: [m.set, "k1", "v1"]
- decl: [m2, map<string:string>, { call: [sys.clone, m] }]
- call: [m2.set, "k2", "v2"]
# m.len  = 1   (only "k1")
# m2.len = 2   ("k1" + "k2")
```

### 5.3 List of strings — independent heaps

```aiml
- decl: [s1, list<string>]
- call: [s1.push, "alpha"]
- call: [s1.push, "beta"]
- decl: [s2, list<string>, { call: [sys.clone, s1] }]
- call: [s1.push, "gamma"]
# s1 = ["alpha", "beta", "gamma"]  (3 entries)
# s2 = ["alpha", "beta"]            (2 entries, independent strdup'd buffers)
```

### 5.4 User struct with nested list field

```aiml
- class:
    name: Inventory
    members:
      - [items, list<string>]
      - [gold,  i32, 0]

- decl: [a, Inventory]
- call: [a.items.push, "sword"]
- set: [a.gold, 100]

- decl: [b, Inventory, { call: [sys.clone, a] }]
- call: [a.items.push, "shield"]
- set: [a.gold, 200]
# b.items = ["sword"]   (length 1, independent backing buffer)
# b.gold  = 100          (independent of a.gold)
```

## 6. Implementation Notes

`sys.clone` is implemented on top of `aiml_ser_deep_copy(dst, src, nodes, n_nodes)`
in `src/sys/ser_runtime.cpp`, which performs an encode-then-decode round-trip
over the binary serializer.  Type descriptors (`AimlTypeNode[]`) are emitted
once per cloned type as `__aiml_sernodes_clone_<typeName>` with
`LinkOnceODRLinkage`, so duplicate clones across modules merge to a single
table at link time.

The destination alloca is zero-initialized before the call (precondition of
`aiml_ser_deep_copy`), then receives fresh heap allocations for every
non-trivial field.

The implementation lives in `Compiler::visitSysCloneCall` in
`src/core/ast_compiler.cpp`.  It is intercepted before eager argument
evaluation (analogous to `sys.serialize` / `sys.bundle`) so the compiler can
inspect the argument's declared type.

## 7. Comparison to `tensor.clone`

`tensor.clone(t)` exists separately because tensors use their own heap-
managed storage model (data buffer + shape + grad) and don't pass through
the generic serializer.  Use `tensor.clone` for tensors, `sys.clone` for
everything else.

## 8. Comparison to `kv.get_copy`

`kv.get_copy(key)` returns a deep-copied value out of an AionDB Kv-store.
It uses the same `aiml_ser_deep_copy` underneath, scoped to KV reads
(needed to avoid sharing the decoded-cache's heap with the caller; see
`doc/issue/kv_get_mutate_set_same_key_crash.md`).  `sys.clone` is the
generic, non-KV variant of that mechanism.
