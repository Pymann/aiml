# AIML Pointer And Struct Array Implementation Plan - 2026-04-16

## Goal

Support `array<ptr<T>:N>` and `array<MyStruct:N>` as well as multidimensional `array<...:N:M:...>` in AIML.

Example:

```aiml
- decl: [nodes, "array<ptr<MyNode>:16>"]
- idx: [nodes, 0, some_node_ptr]
- decl: [n0, "ptr<MyNode>", { at: [nodes, 0] }]

- decl: [points, "array<Point:16>"]
- idx: [points, 0, p0]
- decl: [p1, Point, { at: [points, 0] }]
```

## Why this is currently not cleanly specified

The current array implementation is geared towards primitive values.

Relevant places:

- type resolution: [context.h](../../src/core/context.h#L480)
- array read via `at`: [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp#L882)
- array write via `idx`: [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L854)
- array properties `.len/.data_ptr/.dimX`: [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L1212)
- current tests: [test_array.aiml](../../tests/test_array.aiml#L1)

The implementation already builds LLVM arrays generically from `elemType + count`. That is good:

- `ptr<T>` is just a pointer type at LLVM level and therefore array-capable
- `MyStruct` is a real struct type at LLVM level and therefore array-capable too

So the actual topic is not LLVM, but:

1. language rule
2. type validation
3. initialisation/null semantics
4. copy/store semantics
5. cast rules for `idx`
6. tests and docs

## Basic decision

### Recommendation

Explicitly allow:

1. `array<ptr<T>:N>`
2. `array<MyStruct:N>` with C-like inline semantics

### Staging

Allow immediately:

- `array<ptr<T>:N>`
- `array<MyStruct:N>` for structs with clear value semantics

Do not allow yet:

- `array<string:N>`
- `array<list<i32>:N>`
- `array<any:N>`
- `array<map<K:V>:N>`

Reason:

- pointer arrays are simple and clear.
- struct arrays are quite feasible with C-like value semantics.
- heap containers as direct array elements immediately open questions about ownership, serialize and copy rules.

## Expected semantic model

### 1. Pointer arrays

Allowed:

- `ptr<T>`
- `ref<T>` only if `ref<T>` internally has pointer semantics anyway and is not treated differently in the array
- raw `ptr`

Not part of this first step:

- pointers plus automatic lifetime management

### 2. Default value

`array<ptr<T>:N>` is null-initialised.

That means:

- every element is initially `null`
- reading an unset element returns `null`

### 3. Access

Reading:

```aiml
- decl: [p, "ptr<MyNode>", { at: [nodes, 3] }]
```

Writing:

```aiml
- idx: [nodes, 3, p]
- idx: [nodes, 4, null]
```

### 4. `.data_ptr`

`.data_ptr` stays allowed as a raw `i8*`.

That is sufficient for C interop and consistent with the current array API.

### 2. Struct arrays

`array<MyStruct:N>` follows C-like value semantics:

1. The array is a contiguous block of `N` struct values.
2. `at` reads the struct value at the index.
3. `idx` writes the struct value at the index.
4. No implicit heap management.
5. No pointer indirection, unless it is already in the fields of the struct.

Important:

- `array<MyStruct:N>` is **not** the same as `array<ptr<MyStruct>:N>`
- the first stores values inline
- the second only stores addresses

### 3. C-like copy semantics

For `array<MyStruct:N>`, initially:

- reading via `at` is a value load
- writing via `idx` is a value store
- semantically like a C array of structs

That is particularly clean for trivial/POD-like structs.

### 4. Non-trivial structs

If `MyStruct` contains fields such as these:

- `string`
- `list<T>`
- `map<K:V>`
- `buffer`
- `ptr<U>`

then the semantics nevertheless initially stays **value copy at field level**, i.e. C-like and not automatically deep.

That means:

- pointer fields are copied as pointer values
- heap container fields behave according to their existing representation

These semantics should not be "improved" in hiding. If deep copy rules are wanted later, that is a feature of its own.

## Implementation steps

## P1 - define the type system cleanly

### Goal

`array<ptr<T>:N>` and `array<MyStruct:N>` must no longer just work by accident, but must count explicitly as allowed cases.

### Places

- [context.h](../../src/core/context.h#L480)
- optionally diagnostic/validation code in the AST builders or type checker places

### Change

1. When parsing `array<...>`, do not just delegate the element type to `getLLVMType(parts[0])`.
2. Additionally introduce a language rule:
   - primitive types allowed
   - `ptr<...>` allowed
   - `ref<...>` optionally allowed
   - struct/model types allowed
   - heap container types such as `string`, `list<T>`, `map<K:V>`, `buffer` still not allowed as direct array element types for now

### Why

Without this rule, the current situation is unstable:

- LLVM can build pointer arrays
- but the language has so far claimed the opposite in the docs

### Desired error message

Example:

`array element type 'string' is not supported; allowed: primitive scalars, ptr<T>, struct/model value types`

## P2 - secure `at` for pointer and struct arrays

### Goal

Reading from `array<ptr<T>:N>` and `array<MyStruct:N>` must come back correctly typed.

### Place

- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp#L882)

### Current state

The code does:

1. `elemTy = getLLVMType(parts[0])`
2. `gep`
3. `load elemTy`

That is in principle already right for pointers and structs.

### Change

1. No special handling needed in the GEP path.
2. But:
   - make sure that `sym->typeName` really carries `array<ptr<T>:...>` or `array<MyStruct:...>`
   - possibly improve the type check for the target expression/assignment
3. Tests for:
   - loading a set pointer
   - loading a `null` slot
   - comparison with `null`
   - loading a struct value
   - loading from a 2D struct array

## P3 - correct `idx` for pointer and struct arrays

### Goal

Writing into `array<ptr<T>:N>` must cleanly accept pointer values and `null`.
Writing into `array<MyStruct:N>` must cleanly accept value stores for structs.

### Place

- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L854)

### Problem

The current cast logic is strongly numerically oriented:

- int -> float
- float -> int
- int width casts

An explicit rule for pointers is missing.
For struct values it must be ensured that no unnecessary numeric cast logic runs in between.

### Change

Add these cases in the array `idx` path:

1. `storeVal` is a pointer and `elemTy` is the same pointer type:
   - store directly

2. `storeVal` is `null` and `elemTy` is a pointer type:
   - `ConstantPointerNull` or a matching null pointer cast

3. `storeVal` is a different pointer type and `elemTy` is a pointer type:
   - at first only allow if exactly the same type
   - later optionally `bitcast` for compatible pointer types

4. Pointer into a non-pointer array:
   - clear error message

5. `storeVal` is a struct and `elemTy` is exactly the same struct type:
   - store directly

6. Struct into a non-struct array or a struct type mismatch:
   - clear error message

### Recommendation

At the start, no implicit pointer bitcasts between `ptr<A>` and `ptr<B>`.

Why:

- simpler
- more type-safe
- less debugging horror

### Struct rule

No implicit type adjustments for struct arrays.

Only allow:

- an identical struct type

Do not allow:

- "similar" structs
- automatic field-by-field conversion
- hidden reinterpretation

## P4 - test initialisation and copy semantics explicitly

### Goal

Pointer arrays must be guaranteed to be null-initialised.
Struct arrays must be guaranteed to be null/zero-initialised by value, according to the existing alloca initialisation.

### Places

- compiler alloca initialisation for arrays
- tests in [test_array.aiml](../../tests/test_array.aiml#L1)

### Change

New test cases:

1. declare `array<ptr<T>:4>`
2. read the first element
3. check against `null`
4. store a pointer
5. read the same pointer again
6. set the slot back to `null`

Additional struct tests:

1. declare `array<MyStruct:4>`
2. read the first entry
3. check the fields for default/zero
4. store a struct value
5. read the same struct value again
6. write/read in a 2D variant

## P5 - check properties for pointer and struct arrays

### Goal

`.len`, `.dimX` and `.data_ptr` must also stay correct with pointer and struct elements.

### Place

- [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L1212)

### Assessment

`.len` and `.dimX` are already correct independently of the element type.

`data_ptr` is in principle also correct, because it only points to the first element and is then cast to `i8*`.

### What is additionally necessary

Only tests:

1. `arr.len`
2. `arr.dim0`
3. compile `arr.data_ptr` for a pointer array
4. compile `arr.data_ptr` for a struct array

Not necessarily as a runtime test, but at least a compile test.

## P6 - update the documentation

### Places

- [52_arrays.md](../../spec/52_arrays.md#L1)
- possibly [aiml_reference_guide.md](../../spec/aiml_reference_guide.md)

### Change

The docs currently say:

- no pointer/string/struct elements

For this extension, that must be replaced by:

- primitive types allowed
- `ptr<T>` allowed
- struct/model value types allowed
- `string`, `list<T>`, `map<K:V>`, `buffer` still not allowed

### Add examples

```aiml
- model:
    name: Node
    fields:
      - [value, i32]

- decl: [nodes, "array<ptr<Node>:8>"]
- decl: [n, "ptr<Node>", { new: Node }]
- set: [n.value, 42]
- idx: [nodes, 0, n]
- decl: [r, "ptr<Node>", { at: [nodes, 0] }]
```

And a struct example:

```aiml
- model:
    name: Point
    fields:
      - [x, f32]
      - [y, f32]

- decl: [pts, "array<Point:8>"]
- decl: [p, Point]
- set: [p.x, 1.0]
- set: [p.y, 2.0]
- idx: [pts, 0, p]
- decl: [r, Point, { at: [pts, 0] }]
```

## P7 - extend the tests

### File

- [test_array.aiml](../../tests/test_array.aiml#L1)

### New test group

#### Phase F: pointer arrays

1. declaration + null init
2. store/load the same pointer
3. store `null`
4. 2D pointer array
5. optionally a `.data_ptr` compile smoke test

#### Phase G: struct arrays

1. declaration + zero init
2. store/load the same struct value
3. 2D struct array
4. optionally struct array `.data_ptr`

### Concrete test cases

```aiml
- model:
    name: PtrBox
    fields:
      - [x, i32]

- decl: [boxes, "array<ptr<PtrBox>:4>"]
- decl: [p0, "ptr<PtrBox>", { at: [boxes, 0] }]
# expect null

- decl: [b, "ptr<PtrBox>", { new: PtrBox }]
- set: [b.x, 7]
- idx: [boxes, 0, b]
- decl: [rb, "ptr<PtrBox>", { at: [boxes, 0] }]
# expect rb != null and rb.x == 7

- idx: [boxes, 0, null]
- decl: [p1, "ptr<PtrBox>", { at: [boxes, 0] }]
# expect null
```

Struct example:

```aiml
- model:
    name: Point
    fields:
      - [x, i32]
      - [y, i32]

- decl: [pts, "array<Point:4>"]
- decl: [p, Point]
- set: [p.x, 3]
- set: [p.y, 7]
- idx: [pts, 0, p]
- decl: [r, Point, { at: [pts, 0] }]
# expect r.x == 3 and r.y == 7
```

## Do not do in phase 1

Deliberately postpone these points:

1. `array<string:N>`
2. `array<MyStruct:N>`
3. implicit pointer bitcasts
4. serialize/deserialize for pointer or struct arrays as a new special feature
5. arrays of `any`
6. lists/maps/strings/buffers as direct array elements

Reason:

That immediately pulls in ownership, ABI and persistence questions.

## Risks

### 1. Type inconsistencies with `null`

If `null` is not cleanly materialised in the compiler as a pointer null for the target type, `idx` or comparisons break.

### 2. Pointer type mismatch

If implicit pointer casts are allowed too early, debugging becomes hard later.

### 3. Structs with heap fields

If a struct contains heap fields, a value copy is still deliberately only C-like and not deep.
That must be documented, otherwise users may automatically expect "smart" semantics.

### 4. Docs and implementation drift apart

If pointer arrays are implemented but the specification claims primitive-only, that again turns into "it works by accident" behaviour.

## Recommended order

1. introduce the type rule for array element types
2. make `idx` pointer casts + struct value store clean
3. write pointer and struct array tests
4. update the docs
5. optionally 2D pointer/struct array tests

## Minimal first sprint

### Files

- `src/core/context.h`
- `src/core/ast_compiler_stmt.cpp`
- `src/core/ast_compiler_expr.cpp`
- `tests/test_array.aiml`
- `spec/52_arrays.md`

### Goal

After sprint 1, this should work stably:

```aiml
- decl: [nodes, "array<ptr<MyNode>:8>"]
- idx: [nodes, 0, null]
- idx: [nodes, 1, some_ptr]
- decl: [n, "ptr<MyNode>", { at: [nodes, 1] }]
```

and:

```aiml
- decl: [pts, "array<Point:8>"]
- idx: [pts, 0, some_point]
- decl: [p, Point, { at: [pts, 0] }]
```

## Conclusion

`array<ptr<T>:N>` and `array<MyStruct:N>` are technically quite feasible in AIML.

The LLVM foundation is already close. The actual effort lies in:

1. explicit language approval
2. pointer/`null` rules in the `idx` path
3. clear struct value semantics
4. tests
5. docs

That is a sensible, narrowly bounded expansion. What is critical is not pointer or struct arrays as such, but only direct arrays of heap containers.
