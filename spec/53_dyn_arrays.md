# 53 — Dynamic Arrays (Heap-Allocated, Runtime-Sized)

Heap-allocated arrays whose element count is determined at runtime. Complement to
`array<T:N>` (stack, compile-time size). Element access compiles to the same GEP+load/store
as fixed arrays — no overhead per element access.

## 1. Declaration & Allocation

```yaml
- decl: [arr, "dyn_array<i32>", { new_array: [i32, n] }]
```

`new_array: [ElemType, CountExpr]` allocates `count * sizeof(ElemType)` bytes via `malloc`.
The variable `arr` holds an `i8*` internally (heap pointer).

### Supported element types

All primitive scalars: `i32`, `i64`, `f32`, `f64`, `u8`, `i8`, `bool`.

Note: `string`, `list<T>`, `tensor`, `map<K:V>` are NOT supported as element types —
use `list<T>` for those.

## 2. Read — `{ at: [arr, i] }`

```yaml
- decl: [x, i32, { at: [arr, i] }]
- decl: [y, f32, { at: [farr, j] }]
```

Compiles to: load `i8*` pointer → bitcast to `T*` → GEP with index → load.

## 3. Write — `idx` statement

```yaml
- idx: [arr, i, 42]
- idx: [farr, j, 3.14]
```

Compiles to: load `i8*` pointer → bitcast to `T*` → GEP with index → store.

## 4. Free — scope-default, with `arr.free` as an optional early release

`dyn_array<T>` is a **Class-4 unique resource** (spec/61 §5.6, §2.1): a single-owner heap buffer,
**not** refcounted or aliased. Its lifetime follows the `ptr<T>` discipline (spec/61 §6, §9.1):

- **Released automatically at owner drop** (scope exit) — you rarely write `free`. (Under the
  memory model this replaces the older "optional `.free`, leaks otherwise".)
- **`arr.free` is an optional *early* release** — it `free(ptr)`s now and nulls the slot, so the
  scope-default drop becomes a null no-op (no double free). Use it to reclaim a large buffer
  before scope end (e.g. per loop iteration).
- **Move-on-assign:** assigning / passing / returning a `dyn_array` **moves** it (unique owner);
  a shallow copy is forbidden (double-free). Duplicate with `clone` (fresh `malloc` + byte-copy).

```yaml
- call: [arr.free]     # optional early release; otherwise freed at scope exit
```

## 5. Comparison: `array<T:N>` vs `dyn_array<T>`

| Property              | `array<T:N>`            | `dyn_array<T>`          |
|-----------------------|-------------------------|-------------------------|
| Size known at         | Compile time            | Runtime                 |
| Allocation            | Stack (LLVM alloca)     | Heap (malloc)           |
| Per-access overhead   | 0 (GEP only)            | 0 (GEP only, after load)|
| Per-alloc cost        | 0 (hoisted to entry)    | malloc call             |
| Max size              | ~8 MB (thread stack)    | Limited by heap/RAM     |
| Lifetime              | Function scope          | Owner drop (scope); `.free`=early |
| Use case              | Fixed inner-loop buffers| Runtime-sized buffers   |

## 6. Example

```yaml
- def:
    name: example
    args: [[n, i32]]
    return: i32
    body:
      - decl: [arr, "dyn_array<i32>", { new_array: [i32, n] }]
      - decl: [i, i32, 0]
      - while:
          condition: { "<": [i, n] }
          body:
            - idx: [arr, i, i]          # arr[i] = i
            - set: [i, { +: [i, 1] }]
      - decl: [last, i32, { at: [arr, { -: [n, 1] }] }]
      - call: [arr.free]
      - return: [last]
```

## 7. Implementation Notes

- `dyn_array<T>` is stored as `i8*` at the LLVM level (same as all other pointer types).
- `new_array` emits `malloc(count * sizeof(elemTy))`.
- `at` / `idx` load the `i8*`, bitcast to `T*`, then emit GEP + load/store.
- `.free` emits a `free(ptr)` call and nulls the slot (early release; spec/61 §5.6).
- Under the memory model (spec/61 §5.6) a `dyn_array` local registers a **scope-default free**
  (Class-4 unique resource, freed once at owner drop) — gated on the memory-model build flag; the
  legacy/OFF path keeps the manual-`.free`-or-leak behavior byte-for-byte.
- No bounds checking — same raw-pointer semantics as `array<T:N>`.
- Element types must be LLVM primitive types; structs and pointer elements are not yet supported.
