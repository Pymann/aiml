# Plan: list builtins for AIML

As of: 2026-08-21

Status: **Implemented (2026-08-21).** All nine methods (M1: contains/index_of/
count/extend/remove_value; M2: reverse/sort/sort_desc/shuffle) are implemented in `ListImpl`
(`src/core/list_impl.{h,cpp}`) + dispatch (`src/core/ast_compiler_call.cpp`)
and run identically native + WASM (`tests/test_list_builtins.aiml`).
Type diagnostics (struct search, extend mismatch, sort element type) with source location.
Spec: `spec/10_collections.md`, `spec/aiml_reference_guide.md`.

## 1. Goal

`list<T>` currently only has the basic mutating operations `push`,
`pop`, `get`, `insert`, `remove`, `clear`, `free` as well as the properties `len` and
`empty`. In larger AIML projects, local loops for search, membership, copying,
removal by value, reversing, sorting and shuffling therefore keep arising.

This project introduces a small, type-safe set of list builtins:

| Method | Signature | Priority | Mutation |
|---|---|---:|---|
| `contains` | `(value: T) -> i32` | M1 | no |
| `index_of` | `(value: T) -> i32` | M1 | no |
| `count` | `(value: T) -> i32` | M1 | no |
| `extend` | `(other: list<T>) -> void` | M1 | yes |
| `remove_value` | `(value: T) -> i32` | M1 | yes |
| `reverse` | `() -> void` | M2 | yes |
| `sort` | `() -> void` | M2 | yes |
| `sort_desc` | `() -> void` | M2 | yes |
| `shuffle` | `() -> void` | M2 | yes |

`map`, `filter`, `find(predicate)` and comparator-based sorting do not belong
in this first scope. AIML does not yet support user-defined
generics; universal callback signatures would therefore need a separate
language extension.

## 2. Existing integration points

The line numbers refer to the repository state of 2026-08-21. They
are navigation aids; before the implementation, the respective function must
be searched for by symbol again.

| Area | Current place | Meaning |
|---|---|---|
| Language reference | `spec/10_collections.md:16-30` | documents the current list methods |
| Short reference | `spec/aiml_reference_guide.md:155-166` | compact builtin list |
| Call dispatch | `src/core/ast_compiler_call.cpp:1223-1321` | recognises `list<T>` and dispatches the methods |
| List API | `src/core/list_impl.h:21-32` | declarations of the IR emitters |
| List layout | `src/core/list_impl.cpp:6-20` | `{i32 size, i32 capacity, ptr data}` |
| Growth | `src/core/list_impl.cpp:56-111` | `push` including `realloc` |
| Element access | `src/core/list_impl.cpp:129-146` | `get` and `getElementPtr` |
| Loop example | `src/core/list_impl.cpp:148-187` | IR generation for `slice` |
| Mutation | `src/core/list_impl.cpp:200-324` | `clear`, `insert`, `remove` |
| Properties | `src/core/utils.cpp:158-159` | `.len` and `.empty` |
| String comparison | `src/core/ast_compiler_expr.cpp:437-457` | null-safe `strcmp` path |
| Primitive comparisons | `src/core/ast_compiler_expr.cpp:571-737` | `fcmp`, `icmp`, pointer/string semantics |
| RNG builtin | `src/core/builtin_registry.cpp:3846-3858` | emit for `math.random_int` |
| RNG runtime | `src/sys/math_runtime.cpp:406-422` | thread-local `mt19937` |
| Base test | `tests/test_list.aiml:1-30` | previous list coverage |
| WASM test path | `tools/run_wasm_test.sh` | single test under WASM |

Important: list methods are currently not an entry in `builtin_registry.cpp`.
`Compiler::visitCallExpr` handles them directly in
`src/core/ast_compiler_call.cpp`. The new methods must be added to
the same dispatch for M1/M2, as long as the existing architecture is not
refactored separately.

## 3. Language contract

### 3.1 `contains`

```aiml
- decl: [ids, list<string>]
- call: [ids.push, "a"]
- decl: [has_a, i32, { call: [ids.contains, "a"] }]
# has_a == 1
```

- Returns exactly `1` or `0`.
- Does not change the list.
- An empty list yields `0`.
- Uses the same equality semantics as AIML `eq`.

### 3.2 `index_of`

```aiml
- decl: [idx, i32, { call: [ids.index_of, "a"] }]
# first hit; -1 if not present
```

- Returns the first matching index.
- Returns `-1` for an empty list or a missing value.
- No exception for "not found".

### 3.3 `count`

```aiml
- decl: [hits, i32, { call: [ids.count, "a"] }]
```

- Counts all values with AIML `eq` semantics.
- The result is `i32`, analogous to `.len`.

### 3.4 `extend`

```aiml
- decl: [left, list<i32>]
- decl: [right, list<i32>]
- call: [right.push, 2]
- call: [right.push, 3]
- call: [left.extend, right]
# left == [2, 3], right unchanged
```

- Appends value copies in order.
- `left.extend(left)` must work in a defined way. Before the first mutation,
  the source length is therefore recorded; with a possible `realloc`, no old
  data pointer may be kept across a `push`.
- `list<Model>` copies every model as a value, like `push`.
- `list<ptr<T>>` copies pointer identities.
- An incompatible element type is a compile error.

### 3.5 `remove_value`

```aiml
- decl: [removed, i32, { call: [ids.remove_value, "a"] }]
```

- Removes only the first hit.
- Returns `1` on removal, otherwise `0`.
- Builds internally on `index_of` plus the existing `ListImpl::remove`.
- Deliberately not `remove`, because `remove(index)` already has different semantics.

### 3.6 `reverse`

```aiml
- call: [ids.reverse]
```

- In place, O(n), O(1) additional memory.
- Allowed for all element types, since only whole LLVM values are swapped.

### 3.7 `sort` and `sort_desc`

```aiml
- call: [scores.sort]
- call: [scores.sort_desc]
```

M2 initially supports:

- Integers and enums: signed comparison according to the AIML value type.
- Unsigned integers: unsigned comparison.
- `f32`/`f64`: `fcmp olt` or `ogt` respectively; NaN stays stably at the end.
- `string`: lexicographic via `strcmp`, null is treated like an empty string.

Not in M2:

- Models/structs, maps, nested lists, `any` and interface fat pointers.
- Pointers are not sorted by address; such a call is a
  compile error with the type and method name.
- No comparator-based variant until callback types are cleanly specified.

The first implementation should emit a stable insertion sort in IR.
It is sufficient for typical AIML gameplay/UI lists, avoids a
type-erased `qsort` callback ABI and works identically in AOT and WASM. A
later runtime sort can follow after profiling.

### 3.8 `shuffle`

```aiml
- call: [deck.shuffle]
```

- Fisher-Yates in place from `len - 1` down to `1`.
- Allowed for all element types.
- Uses the same RNG as `math.random_int(0, hi)`.
- The result order is deliberately non-deterministic.
- Seedable/replayable random sources are a separate project. Gameplay
  that needs deterministic replays must not use the builtin until then.

## 4. Concrete compiler changes

### 4.1 Extract shared comparison emitters

The existing `eq` logic in `src/core/ast_compiler_expr.cpp:437-457` and
`src/core/ast_compiler_expr.cpp:571-737` must not be copied into `ListImpl` as a second, slightly
diverging implementation.

Proposal: centralise it in `Compiler` or a new small helper
`src/core/value_compare.{h,cpp}`:

```cpp
enum class AimlCompareOp { Eq, Lt, Gt };

llvm::Value* emitAimlCompare(
    CompilerContext& ctx,
    llvm::Value* lhs,
    llvm::Value* rhs,
    llvm::Type* valueType,
    const std::string& typeName,
    AimlCompareOp op);
```

Requirements:

- `string` uses the existing null-safe `strcmp` path.
- Enums use their underlying integer value.
- Float eq stays `oeq`; sorting defines the NaN behaviour explicitly.
- Opaque handles and normal pointers use identity for `contains/index_of/count`,
  unless it is a `string`.
- Struct value eq stays inadmissible in M1 until a canonical model equality
  exists. `extend`, `reverse` and `shuffle` work nevertheless.

If the extraction is too risky for M1, M1 may locally implement only primitive,
enum, string and pointer-capable comparisons. In that case, however, a
follow-up for merging them must be documented firmly in the same change.

### 4.2 Extend `ListImpl`

Add the following declarations in `src/core/list_impl.h:21-32`:

```cpp
llvm::Value* indexOf(CompilerContext&, llvm::Value* listPtr,
                     llvm::Value* needle, llvm::Type* elemType,
                     const std::string& elemTypeName);
llvm::Value* count(CompilerContext&, llvm::Value* listPtr,
                   llvm::Value* needle, llvm::Type* elemType,
                   const std::string& elemTypeName);
void extend(CompilerContext&, llvm::Value* dstPtr, llvm::Value* srcPtr,
            llvm::Type* elemType);
void reverse(CompilerContext&, llvm::Value* listPtr, llvm::Type* elemType);
void sort(CompilerContext&, llvm::Value* listPtr, llvm::Type* elemType,
          const std::string& elemTypeName, bool descending);
void shuffle(CompilerContext&, llvm::Value* listPtr, llvm::Type* elemType);
```

`contains` is generated in the dispatch as `indexOf(...) >= 0`. `remove_value`
likewise uses `indexOf` and only calls `ListImpl::remove(...)` on a hit.
That way only one search loop exists.

The new implementations go into `src/core/list_impl.cpp` after
`ListImpl::remove`, currently from line 325. Shared private IR helpers for
loop blocks and swap should be placed in the anonymous namespace above `namespace
ListImpl`.

Example for the `indexOf` emitter:

```cpp
llvm::Value* indexOf(CompilerContext& ctx, llvm::Value* listPtr,
                     llvm::Value* needle, llvm::Type* elemType,
                     const std::string& elemTypeName) {
    // entry: result = -1, i = 0
    // head:  i < size && result == -1
    // body:  value = load data[i]; eq = emitAimlCompare(..., Eq)
    // hit:   result = i
    // next:  i++
    // exit:  load result
}
```

### 4.3 Extend the call dispatch

In `src/core/ast_compiler_call.cpp:1230-1319`, new branches are inserted after `get` and before the
mutating methods. Concrete form:

```cpp
} else if (methodName == "contains" && argVals.size() == 1) {
    llvm::Value* idx = ListImpl::indexOf(ctx, listPtr, arg0,
                                         llvmListElemType, listElemName);
    return ctx.ir_gen->wrap(ctx.builder.CreateZExt(
        ctx.builder.CreateICmpSGE(idx, ctx.builder.getInt32(0)),
        ctx.builder.getInt32Ty()));
} else if (methodName == "index_of" && argVals.size() == 1) {
    return ctx.ir_gen->wrap(ListImpl::indexOf(
        ctx, listPtr, arg0, llvmListElemType, listElemName));
} else if (methodName == "count" && argVals.size() == 1) {
    return ctx.ir_gen->wrap(ListImpl::count(
        ctx, listPtr, arg0, llvmListElemType, listElemName));
} else if (methodName == "extend" && argVals.size() == 1) {
    ListImpl::extend(ctx, listPtr, otherListPtr, llvmListElemType);
    return nullptr;
} else if (methodName == "remove_value" && argVals.size() == 1) {
    // indexOf + conditional remove; return i32 0/1
} else if (methodName == "reverse" && argVals.empty()) {
    ListImpl::reverse(ctx, listPtr, llvmListElemType);
    return nullptr;
} else if (methodName == "sort" && argVals.empty()) {
    ListImpl::sort(ctx, listPtr, llvmListElemType, listElemName, false);
    return nullptr;
} else if (methodName == "sort_desc" && argVals.empty()) {
    ListImpl::sort(ctx, listPtr, llvmListElemType, listElemName, true);
    return nullptr;
} else if (methodName == "shuffle" && argVals.empty()) {
    ListImpl::shuffle(ctx, listPtr, llvmListElemType);
    return nullptr;
}
```

Before the dispatch, `listPtr` must be computed once locally instead of repeating the long
`static_cast`/`unwrap` expression per branch:

```cpp
llvm::Value* listPtr =
    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
```

Argument count errors must not fall into the generic message `Unknown list method`.
For every known method a location-based diagnostic should arise:

```text
file.aiml:42: list<i32>.contains expects 1 argument, got 0
```

For that, the existing diagnostic at `src/core/ast_compiler_call.cpp:1319` should
get `ctx.fmtLoc(call->sourceNode)`.

### 4.4 Check the element type of the `extend` argument

The receiver supplies `listElemName`. For the argument, the static
`Symbol::typeName` or the expression type must be normalised to exactly the same
`list<T>`. As in the existing code at
`src/core/ast_compiler_call.cpp:1223-1227`, `cascade<X>` may only count as `list<i32>` if
both sides have this layout.

Error example:

```text
list<i32>.extend expects list<i32>, got list<string>
```

No implicit numeric conversion in M1. That prevents partially
converted lists and unclear ownership for struct/pointer elements.

### 4.5 RNG for `shuffle`

`shuffle` should not generate an AIML namespace dispatch for every element.
`ListImpl::shuffle` directly declares the same external runtime function as
`math.random_int` in `src/core/builtin_registry.cpp:3846-3858`:

```cpp
llvm::Function* randomInt = ctx.getFunction(
    "aiml_math_random_int", ctx.builder.getInt32Ty(),
    {ctx.builder.getInt32Ty(), ctx.builder.getInt32Ty()});
```

The runtime code in `src/sys/math_runtime.cpp:406-422` stays unchanged.
That way there is no second RNG source, and native/WASM use the same
exported ABI name.

## 5. Safety and edge cases

### 5.1 Bounds and empty lists

- `contains`, `index_of`, `count`, `extend`, `reverse`, `sort` and `shuffle`
  must not form element pointers when `len == 0`.
- `reverse` and `shuffle` must return immediately when `len < 2`.
- The new methods must not extend the existing bounds problems of `pop`,
  `insert` and `remove`. Hardening those is a separate patch.

### 5.2 Reallocation and aliasing

- `extend` must not cache a source data pointer across `push`.
- `self.extend(self)` must be tested explicitly.
- Two list structs can contain the same data pointer because of a shallow value copy.
  The plan only guarantees correct behaviour for an identical
  list struct pointer. General alias tracking stays out of scope.

### 5.3 Strings

- String equality must compare content, not pointer identity.
- `null` and `""` follow exactly the existing `eq` semantics.
- `sort` uses `strcmp`; pointers are only reordered, strings are not copied.

### 5.4 Structs and fat pointers

- `extend`, `reverse`, `shuffle`: allowed, because whole LLVM values are moved.
- `contains`, `index_of`, `count`, `remove_value`: allowed in M1 only if a
  canonical eq semantics exists; otherwise a compile error.
- `sort`: reject structs, normal pointers and interface fat pointers in M2.

## 6. Tests

New file: `tests/test_list_builtins.aiml`.

### 6.1 M1 functional tests

- `list<i32>`: hit at the start, in the middle, at the end, and a miss.
- `list<string>`: content equality with separately created strings.
- `list<Enum>`: `contains`, `index_of`, `count`.
- Duplicates: `count == n`, `remove_value` removes only the first hit.
- An empty list for every search method.
- `extend` preserves order and leaves the source unchanged.
- `extend` across the capacity limit, so that `realloc` is executed.
- `self.extend(self)` yields exactly the doubled original sequence.
- `list<Model>.extend` copies whole values.
- Negative test: `list<i32>.extend(list<string>)` does not compile.
- Negative test: struct search reports the unsupported element type.

### 6.2 M2 functional tests

- `reverse` for 0, 1, 2 and odd/even element counts.
- `sort` and `sort_desc` for integer, enum, float and string.
- Sorting with duplicates is stable.
- Float sorting documents and tests NaN.
- `shuffle` preserves the length and the multiset of values.
- `shuffle` on 0/1 elements does not crash.
- Negative test: `list<Model>.sort` produces a clear compile diagnostic.

### 6.3 Backends

Native:

```bash
./out/aiml/aiml tests/test_list_builtins.aiml
```

WASM:

```bash
tools/run_wasm_test.sh tests/test_list_builtins.aiml
```

Existing regressions:

```bash
./out/aiml/aiml tests/test_list.aiml
./out/aiml/aiml tests/test_list_list_inner_values.aiml
./out/aiml/aiml tests/test_list_of_iface_ptr.aiml
./out/aiml/aiml tests/test_return_list_from_call.aiml
```

If the regular runner expects output-based tests, the new file should
print `PASS test_list_builtins` on success and abort via
`sys.exit(1)` on the first deviation.

## 7. Documentation

After M1:

- `spec/10_collections.md:16-30`: add methods, returns and examples.
- `spec/aiml_reference_guide.md:155-166`: synchronise the short reference.
- `spec/54_iterators.md:40-50`: keep marking the entry `list.iter()` as future;
  search builtins are eager and do not replace an iterator.
- `spec/49_static_analysis.md`: document new type diagnostics for `extend`, struct search and
  `sort`, if they are implemented as always-on static checks.

After M2, additionally link an RNG/determinism note for `shuffle` in
`spec/43_0_math.md`.

## 8. Implementation steps

1. Fix the API contract for M1 in `spec/10_collections.md`.
2. Provide comparison emitters for primitive, enum, string and pointer-capable values
   or extract them from `ast_compiler_expr.cpp`.
3. Implement `indexOf`, `count` and `extend` in `ListImpl`.
4. Compose `contains` and `remove_value` from `indexOf`.
5. Add the dispatch, argument count and type diagnostics.
6. Run the M1 native tests and then the WASM test.
7. Update the reference documentation.
8. Start M2 with `reverse`; this method needs no comparison semantics.
9. Implement `sort`/`sort_desc` with a stable insertion sort.
10. Connect `shuffle` to `aiml_math_random_int`.
11. Run the M2 native/WASM tests and the existing list regressions.

## 9. Acceptance criteria

- All nine methods match the signatures from section 1.
- Native and WASM deliver the same results, apart from the random
  order of `shuffle`.
- No new runtime list ABI; the layout stays `{i32, i32, ptr}`.
- No implicit conversion between different `list<T>`.
- String search compares content.
- Unsupported types produce compile errors with a source location.
- `self.extend(self)` and reallocation are tested.
- Existing list, serialisation, interface list and return list tests
  stay green.

## 10. Later extensions

Evaluate only after stable callback/generic semantics:

- `find(predicate) -> i32` or `T`
- `find_index(predicate) -> i32`
- `any(predicate)`, `all(predicate)`
- `map<U>(fn) -> list<U>`
- `filter(predicate) -> list<T>`
- `sort_by(comparator)`
- `unique`/`dedupe`
- a seedable `shuffle(rng)` for deterministic replays

These functions must not be simulated via `any` boxing, because that would worsen
type information, performance and AOT/WASM parity.
1
