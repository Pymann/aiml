# AIML Array Fast-Path Plan - 2026-04-16

## Decision

We take the performance-oriented route:

1. use `array<f32:N>` in AIML for small fixed numeric hot paths
2. introduce new C++ builtins that work directly on `array.data_ptr`
3. no conversion back via `list<f32>` in the hot path

The goal is not just more elegant AIML code, but a really measurable fast path for small feature vectors in TUL.

## Target area

First target:

- `tul/math/scoring.aiml`

Later targets:

- `tul/model/candidate_ranker.aiml`

Not the first target:

- the large transformer training path
- lists with dynamic length
- persisted or string-heavy data

## Technical findings from the C++ foundation

The necessary foundations are already in place:

1. Arrays are built directly as LLVM `[N x T]`:
   [context.h](../../src/core/context.h#L480)

2. Array reads are a direct `GEP + load`:
   [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp#L882)

3. Array writes are a direct `GEP + store`:
   [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L854)

4. Arrays already have `.data_ptr`:
   [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L1212)

5. The builtin side already knows pointer-based stack array helpers:
   [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L8068)

That means:

- the language side is almost ready for `array<f32:N>`
- the missing part is a set of small numeric builtins that accept `i8*`/raw pointers directly

## Core problem today

TUL currently builds small feature vectors via `list<f32>` and `push`.

Examples:

- `test/tul/math/scoring.aiml:34`
- `test/tul/math/vector.aiml:12`

In the compiler and runtime system, `list<f32>` is a heap container with the layout `{size, capacity, data*}`:

- list methods: [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L911)
- `ListImpl` API: [list_impl.h](../../src/core/list_impl.h#L21)

That is flexible, but unnecessarily expensive for fixed 10-element features.

## Target architecture

### Phase 1 type model

We do **not** introduce a completely new universal vector type.

Instead:

1. small hot path functions in AIML use local `array<f32:10>`
2. new builtins read directly from `array.data_ptr`
3. general `Vector`/`list<f32>` paths remain for the rest of the system

That minimises risk and keeps the first fast path narrow and controlled.

## P1 - define new native builtins

### Goal

Direct numeric operations on raw data pointers, without `list<f32>`.

### New builtins

Recommended first set:

1. `ai.dot_ptr(ptr_a, ptr_b, n) -> f32`
2. `ai.affine_score_ptr(w_ptr, x_ptr, n, bias) -> f32`
3. `ai.argmax_ptr(x_ptr, n) -> i32`
4. optionally `ai.copy_f32_ptr(src_ptr, dst_ptr, n) -> void`

### Why exactly these

They cover the first fast path almost completely:

- dot product
- affine score
- argmax over a few scores

### C++ files

- `src/sys/ai_runtime.h`
- `src/sys/ai_runtime.cpp`
- `src/core/builtin_registry.cpp`

### API proposal

```cpp
float aiml_dot_ptr(const float* a, const float* b, int32_t n);
float aiml_affine_score_ptr(const float* w, const float* x, int32_t n, float bias);
int32_t aiml_argmax_ptr(const float* x, int32_t n);
void aiml_copy_f32_ptr(const float* src, float* dst, int32_t n);
```

### AIML signature

Register on the compiler side as `i8*`-based builtins:

- `ai.dot_ptr(i8*, i8*, i32) -> f32`
- `ai.affine_score_ptr(i8*, i8*, i32, f32) -> f32`
- `ai.argmax_ptr(i8*, i32) -> i32`

## P2 - wire up the builtin registry

### Goal

Make the new runtime functions callable from AIML.

### File

- [builtin_registry.cpp](../../src/core/builtin_registry.cpp)

### Approach

Follow the existing `ai.*` builtins.

Model:

- `tensor.from_data`: [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L8325)

### Implementation

1. accept `i8*` pointer arguments directly
2. `i32` for `n`
3. `CreateCall(...)` to the new runtime functions

### Advantage

`array.data_ptr` already fits naturally into this model.

## P3 - switch AIML scoring to the array fast path

### Goal

`tul/math/scoring.aiml` should no longer build a `list<f32>` container for fixed 10 features.

### File

- `test/tul/math/scoring.aiml:1`

### Implementation

#### 1. New local builders

Instead of `Vector` directly:

```aiml
- decl: [feat, "array<f32:10>"]
- idx: [feat, 0, token_count]
- idx: [feat, 1, unique_token_count]
...
```

#### 2. New scoring helpers

Additional functions:

- `affine_score_fixed10(w: Vector, x: array<f32:10>, bias: f32) -> f32`
- `compute_mode_scores_fixed10(x: array<f32:10>, mode_weights: list<Vector>, mode_biases: list<f32>) -> array<f32:K>` or at first `list<f32>`

#### 3. First compromise

In phase 1:

- features as `array<f32:10>`
- weights stay `Vector` / `list<f32>`
- `ai.affine_score_ptr(w.data_as_ptr?, x.data_ptr, 10, bias)` is not directly possible yet as long as `Vector` only has `list<f32>`

That is why there are two sub-options.

## P4 - clarify access to weight vectors

This is the only real design knot.

### Problem

`WeightVector.weights` and `Vector.data` are `list<f32>` today.

`ai.affine_score_ptr(...)`, however, needs raw pointers on both sides.

### Option A - an additional list builtin for raw data

A new property or builtin for `list<f32>`:

- `list.data_ptr`

Or a builtin:

- `ai.list_data_ptr(list<f32>) -> i8*`

### Affected places

- the `ListImpl` layout is known: [list_impl.h](../../src/core/list_impl.h#L21)
- `tensor.from_int_list` already extracts the data field from a list value:
  [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L8337)

### Recommendation

For phase 1 this is the best solution.

Why:

1. a minimally invasive extension
2. no new general `Vector` representation
3. immediate benefit for `scoring.aiml`

### Result

Then this is possible:

```aiml
- decl: [score, f32, { call: [ai.affine_score_ptr, w.weights.data_ptr, feat.data_ptr, 10, bias] }]
```

## P5 - introduce list `data_ptr` for primitive lists

### Goal

Primitive `list<T>` should optionally be able to expose a raw data pointer.

### File

- `src/core/ast_compiler.cpp`
- possibly `src/core/ast_compiler_call.cpp`

### Model

The array property `.data_ptr`:

- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L1231)

### Rule

Allow for:

- `list<i32>`
- `list<i64>`
- `list<f32>`
- `list<f64>`
- possibly `list<u8>`

Do not enable for:

- `list<string>`
- `list<Model>`
- `list<ptr<T>>`

### Property proposal

`my_list.data_ptr`

Returns:

- `i8*`

### Benefit

That is helpful not only for TUL, but for numeric builtins in general.

## P6 - really switch TUL scoring

### Goal

The fixed 10-element feature vectors go from:

`push -> list<f32> -> Vector -> ai.dot`

to:

`idx -> array<f32:10> -> data_ptr -> ai.affine_score_ptr`

### Targets

- `build_shared_core_features`
- `build_router_features`
- `compute_mode_scores`
- `argmax`

### Phase 1 pragmatics

`compute_mode_scores` may still return `list<f32>` in phase 1 if only the input side is already array-based.

The biggest gain lies in:

1. avoiding building the feature list
2. direct dot/affine calls

## P7 - candidate ranker afterwards

### File

- `tul/model/candidate_ranker.aiml`

### Goal

Also switch small fixed feature vectors for candidate scoring to arrays plus pointer builtins.

### Order

Only after a successful `math/scoring.aiml` rebuild.

## P8 - tests

### AIML compiler/runtime tests

New tests in the AIML project:

1. `array<f32:10>` + `.data_ptr` to a native builtin
2. `list<f32>.data_ptr` for primitive lists
3. comparison of array vs. list results for dot/affine

### TUL tests

1. `build_shared_core_features` returns numerically identical values as before
2. `compute_mode_scores` returns identical scores
3. router/SharedCore training does not change numerically in an uncontrolled way

## P9 - benchmark

### Goal

Not just "should be faster", but prove it.

### Benchmark idea

Mini benchmark for 1 million calls:

1. old path:
   - `build_router_features` via `list.push`
   - `affine_score`

2. new path:
   - `array<f32:10>`
   - `ai.affine_score_ptr`

### Expectation

The gain should come mainly from these points:

1. no heap list for features
2. no `push` chain
3. direct pointer access in native code

## Open design points

These points still have to be decided finally, but are small:

### 1. `list.data_ptr` as a property or builtin?

Recommendation:

- property, because it matches `array.data_ptr`

### 2. `argmax_ptr` only for `f32`?

Recommendation:

- yes, phase 1 only `f32`

### 3. a dedicated `FixedVector10` type right away or later?

Recommendation:

- later
- phase 1 only local `array<f32:10>`

## Recommended implementation order

1. new runtime functions in `ai_runtime.*`
2. new `ai.*_ptr` builtins in `builtin_registry.cpp`
3. `list<f32>.data_ptr` for primitive lists
4. switch `tul/math/scoring.aiml` to the array fast path
5. tests
6. benchmark
7. afterwards `candidate_ranker`

## Minimal first sprint

### AIML/C++

1. `src/sys/ai_runtime.h/.cpp`
   - `aiml_dot_ptr`
   - `aiml_affine_score_ptr`
   - `aiml_argmax_ptr`

2. `src/core/builtin_registry.cpp`
   - `ai.dot_ptr`
   - `ai.affine_score_ptr`
   - `ai.argmax_ptr`

3. `src/core/ast_compiler.cpp`
   - `list<f32>.data_ptr` for primitive lists

### TUL

4. `tul/math/scoring.aiml`
   - `array<f32:10>` for features
   - pointer builtins for scoring

## Conclusion

This can be implemented without rebuilding the whole system.

The critical point is not `array<>` itself. The critical point is decoupling the numeric hot path from `list<f32>` without immediately redesigning the entire AIML container stack.

The cleanest first step is therefore:

1. pointer builtins
2. `list.data_ptr` for primitive lists
3. `tul/math/scoring.aiml`

That gives TUL a real small array fast path that is technically narrowly bounded and measurable.
