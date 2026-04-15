# 52 — Arrays (Stack-Allocated Fixed-Size)

Native fixed-size arrays that compile to LLVM `alloca [N x T]`. Zero overhead element access
via GEP instructions — no function calls, no bounds checks, no heap allocation.

## 1. Declaration

```yaml
# 1D array: 64 floats on the stack
- decl: [v, "array<f32:64>"]

# 2D array: 4×4 matrix (stored flat as 16 floats)
- decl: [mat, "array<f32:4:4>"]

# 3D array
- decl: [cube, "array<i32:3:4:5>"]

# With initializer (zero-filled by default)
- decl: [counts, "array<i32:256>"]
```

### Supported element types

`i32`, `i64`, `f32`, `f64`, `u8`, `i8`

No pointer/string/struct elements — use `list<T>` for those.

### LLVM mapping

```
array<f32:64>    → alloca [64 x float], zeroinitializer
array<f32:4:4>   → alloca [16 x float], zeroinitializer   (4×4 = 16, flat)
array<i32:3:4:5> → alloca [60 x i32], zeroinitializer     (3×4×5 = 60, flat)
```

Multi-dimensional arrays are stored in **row-major** order (C layout).

## 2. Element Access — `{ at: [...] }`

The `at` expression is the universal indexing operator for arrays, lists, and tensors.

### Read

```yaml
# 1D
- decl: [x, f32, { at: [v, i] }]

# 2D
- decl: [x, f32, { at: [mat, row, col] }]

# 3D
- decl: [x, i32, { at: [cube, a, b, c] }]

# Nested (array of indices)
- decl: [idx, i32, { at: [indices, k] }]
- decl: [x, f32, { at: [data, idx] }]

# Computed index
- decl: [x, f32, { at: [v, { "+": [i, 1] }] }]
```

### Write — `idx` statement

Writing uses the `idx` statement (not `set`). The **last argument is the value**, all others are container + indices.

```yaml
# 1D
- idx: [v, i, 42.0]

# 2D
- idx: [mat, 2, 3, 1.5]

# 3D
- idx: [cube, a, b, c, 99]

# Computed index
- idx: [v, { "+": [i, 1] }, 42.0]

# Also works for lists
- idx: [my_list, k, 99]
```

### LLVM codegen

```
# Read: { at: [v, i] }
%ptr = getelementptr [64 x float], [64 x float]* %v, i32 0, i32 %i
%val = load float, float* %ptr

# Write: idx: [v, i, 42.0]
%ptr = getelementptr [64 x float], [64 x float]* %v, i32 0, i32 %i
store float 42.0, float* %ptr

# 2D read: { at: [mat, r, c] }  where mat is array<f32:4:4>
%flat_idx = mul i32 %r, 4        ; row * cols
%flat_idx2 = add i32 %flat_idx, %c  ; + col
%ptr = getelementptr [16 x float], [16 x float]* %mat, i32 0, i32 %flat_idx2
%val = load float, float* %ptr

# 2D write: idx: [mat, r, c, 1.5]
# Same GEP, then store instead of load
```

## 3. `at` for Other Container Types

The `at` expression works polymorphically based on the container type:

| Container | `{ at: [x, i] }` compiles to | Overhead |
|-----------|------------------------------|----------|
| `array<T:N>` | GEP + load | **1 instruction** |
| `list<T>` | `aiml_list_get(x, i)` | Function call + bounds check |
| `tensor` | `aiml_tensor_get(x, i)` | Function call |

This replaces the `-` indexing syntax (which is deprecated but still supported for backward compatibility).

### Migrating from `-` syntax

```yaml
# Old (deprecated)
- decl: [x, i32, my_list-i]

# New (recommended)
- decl: [x, i32, { at: [my_list, i] }]
```

### Slice (lists only)

Slicing remains a separate operation:

```yaml
- decl: [sub, list<i32>, { slice: [my_list, 2, 5] }]
```

Arrays do not support slicing (fixed size, stack-allocated).

## 4. Properties

| Property | Type | Description |
|----------|------|-------------|
| `.len` | `i32` | Total element count (compile-time constant) |
| `.data_ptr` | `i8*` | Pointer to first element (for C interop) |

```yaml
- decl: [v, "array<f32:64>"]
- decl: [n, i32, v.len]           # → 64 (constant, no runtime cost)
- decl: [p, "i8*", v.data_ptr]    # → bitcast [64 x float]* to i8*
```

For multi-dimensional arrays, `.len` returns the **total** flat element count:

```yaml
- decl: [mat, "array<f32:4:4>"]
- decl: [n, i32, mat.len]         # → 16
```

### Dimension query

```yaml
- decl: [mat, "array<f32:4:4>"]
- decl: [rows, i32, mat.dim0]     # → 4 (compile-time constant)
- decl: [cols, i32, mat.dim1]     # → 4 (compile-time constant)
```

## 5. Passing to Functions

Arrays are passed **by pointer** (like C arrays):

```yaml
- def:
    name: sum_array
    args:
      - [data, "array<f32:64>"]   # receives pointer to [64 x float]
    return: f32
    body:
      - decl: [total, f32, 0.0]
      - decl: [i, i32, 0]
      - while:
          condition: { "<": [i, 64] }
          body:
            - set: [total, { "+": [total, { at: [data, i] }] }]
            - set: [i, { "+": [i, 1] }]
      - return: [total]
```

### C Interop via `data_ptr`

```yaml
# Pass array data to C function expecting float*
- decl: [v, "array<f32:64>"]
- call: [some_c_function, v.data_ptr, 64]
```

## 6. Initialization

Arrays are **zero-initialized** by default:

```yaml
- decl: [v, "array<f32:64>"]      # all 64 floats = 0.0
- decl: [m, "array<i32:4:4>"]     # all 16 ints = 0
```

Fill with a value:

```yaml
# Fill loop (compiler may optimize to memset)
- decl: [v, "array<f32:64>"]
- decl: [i, i32, 0]
- while:
    condition: { "<": [i, 64] }
    body:
      - idx: [v, i, 1.0]
      - set: [i, { "+": [i, 1] }]
```

## 7. Stack Size Limits

Arrays live on the stack. Recommended maximum:

| Max elements | Approx. stack usage |
|-------------|-------------------|
| 16,384 f32 | 64 KB |
| 65,536 f32 | 256 KB |

For larger data, use `tensor` (heap-allocated) or `list<T>`.

## 8. Use Cases

### ML: Embedding vector operations (replaces Vector/list<f32>)

```yaml
- decl: [emb, "array<f32:64>"]
- decl: [sum, "array<f32:64>"]
- decl: [i, i32, 0]
- while:
    condition: { "<": [i, 64] }
    body:
      - idx: [sum, i, { "+": [{ at: [sum, i] }, { at: [emb, i] }] }]
      - set: [i, { "+": [i, 1] }]
```

This compiles to a tight loop of GEP+load+add+store — same speed as C.

### Lookup tables

```yaml
- decl: [ascii_table, "array<i32:128>"]
- idx: [ascii_table, 65, 1]    # 'A' = 1
- idx: [ascii_table, 66, 2]    # 'B' = 2
```

### 2D matrices

```yaml
- decl: [transform, "array<f32:4:4>"]
# Identity matrix
- idx: [transform, 0, 0, 1.0]
- idx: [transform, 1, 1, 1.0]
- idx: [transform, 2, 2, 1.0]
- idx: [transform, 3, 3, 1.0]
```

## 9. Implementation Notes

- Arrays compile to LLVM `alloca` with `zeroinitializer` in the function entry block
- Element access compiles to `getelementptr` + `load`/`store` (single instruction each)
- No runtime bounds checking (same as C arrays) — use `list<T>` if bounds checks needed
- Multi-dim indices are flattened at compile time when constant, or with `mul`+`add` when dynamic
- `.len`, `.dim0`, `.dim1` etc. are compile-time constants (emitted as LLVM `i32` constants)
- Arrays cannot be returned from functions (stack-allocated) — return via out-parameter or use `tensor`
