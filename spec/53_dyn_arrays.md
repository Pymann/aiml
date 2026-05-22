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

## 4. Free — `arr.free`

```yaml
- call: [arr.free]
```

Calls `free(ptr)`. Optional — memory is reclaimed on process exit regardless.
Recommended for long-running programs that allocate large arrays repeatedly.

## 5. Comparison: `array<T:N>` vs `dyn_array<T>`

| Property              | `array<T:N>`            | `dyn_array<T>`          |
|-----------------------|-------------------------|-------------------------|
| Size known at         | Compile time            | Runtime                 |
| Allocation            | Stack (LLVM alloca)     | Heap (malloc)           |
| Per-access overhead   | 0 (GEP only)            | 0 (GEP only, after load)|
| Per-alloc cost        | 0 (hoisted to entry)    | malloc call             |
| Max size              | ~8 MB (thread stack)    | Limited by heap/RAM     |
| Lifetime              | Function scope          | Until `.free` or exit   |
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
- `.free` emits a `free(ptr)` call.
- No bounds checking — same raw-pointer semantics as `array<T:N>`.
- Element types must be LLVM primitive types; structs and pointer elements are not yet supported.
