# Compiler Dispatch / Lookup Cleanup

**Status**: backlog (2026-06-07)
**Scope**: AIML compiler hot paths (`src/core/context.h`, `ast_compiler_expr.cpp`, AST dispatch)
**Motivation**: the compiler contains several large string and RTTI dispatch paths that are hit very often during a compile run. Some of them are only hard to read; some are also a real performance candidate.

## Goal

Systematically switch the largest repeated linear dispatch paths in the compiler over to more suitable lookup mechanisms:

- string `if` chains -> table lookup / `unordered_map`
- operator string comparisons -> one-time normalisation to an `enum` + `switch`
- large `dynamic_cast` chains -> `NodeKind` / tag dispatch instead of RTTI cascades

Not every rework is meant to happen for performance reasons. Priority goes to the paths that are called often in a normal compile run.

## Priority 1 — `CompilerContext::getLLVMType`

**File**: `src/core/context.h`

Currently there is a big string dispatch graveyard here for:

- primitive types (`i32`, `f32`, `bool`, ...)
- opaque handle types (`gl_texture`, `prefab_instance`, `audio_sound`, ...)
- some special structures (`vec2`, `mat4`, `mirror_event`, `ui_style`, ...)

### Problem

`getLLVMType(typeName)` is called in very many places. The current form is a long linear sequence of `if (typeName == "...")` comparisons.

### Target picture

1. Keep the special cases at the start:
   - `ptr<T>`
   - `ref<T>`
   - `list<T>` / `map<K:V>` / `fn<...>` / `iterator<T>`
2. After that a table lookup:
   - `unordered_map<string, llvm::Type*>` for trivial cached types
   - or `unordered_map<string, std::function<llvm::Type*()>>` for lazy creation
3. Move more complex structure types (`mat4`, `mirror_event`, `ui_style`) out into small dedicated builder functions

### Expected benefit

- fewer repeated string comparisons
- a smaller, readable hot path
- less risk of accidental double definitions / forgotten alias paths

## Priority 2 — binary operator dispatch

**File**: `src/core/ast_compiler_expr.cpp`

Currently `bin->op` is evaluated several times via string comparisons:

- mathematical struct ops
- float arithmetic / comparisons
- integer fallback
- pointer equality special cases

### Problem

A single operator (`+`, `eq`, `gte`, `pneq`, ...) is checked as a string several times in the same compile path.

### Target picture

1. Normalise the operator early to an `enum class BinOpKind`
2. Unify synonyms right there:
   - `+` / `add`
   - `-` / `sub`
   - `le` / `lte`
   - `ge` / `gte`
3. After that dispatch via `switch(opKind)`

### Expected benefit

- less string work in the hot path
- clearer control flow
- less scattered alias handling

## Priority 3 — AST node dispatch without RTTI cascades

**Files**:

- `src/core/ast_compiler.cpp`
- `src/core/ast_compiler_expr.cpp`
- `src/core/ast_compiler_stmt.cpp`
- partly `compiler.cpp`

### Problem

Many paths work with long `dynamic_cast` chains:

- top-level decl dispatch
- expr dispatch
- stmt dispatch
- various registration and header passes

### Target picture

- `NodeKind` or an equivalent tag directly on the AST base
- central `switch(node->kind())` dispatches
- `dynamic_cast` only where concrete data fields are really needed

### Expected benefit

- less RTTI noise
- clearer dispatch
- potentially lower overhead for large ASTs

## What is **not** primarily meant

### `builtin_registry.cpp`

The file is large, but the main dispatch there is already largely organised as tables (`builtinFuncs`, `builtinMethods`). That is more a volume/maintainability topic than a classic `if/else` performance problem.

### Small, rare if chains

Import/error/one-shot logic in `compiler.cpp` is not the first lever. Only rework it if it is touched structurally anyway.

## Concrete work plan

1. Split up `getLLVMType`
   - special parsers at the front
   - primitive/opaque lookup table
   - structured helpers for complex types
2. Switch `visitBinary` to `BinOpKind`
3. Prepare expr/stmt dispatch for `NodeKind`
4. After that tackle the top-level decl dispatch in `registerTypeDecl` / related paths

## Measurement idea

Measure before/after on:

- a complete build of the AIML repo
- a complete build of the DataCreatures client/server
- optionally instrumented counters:
  - number of `getLLVMType` calls
  - frequency of the most important operators

## Success criterion

- No semantic change in the generated IR / behaviour
- noticeably less string/RTTI dispatch in the hot paths
- the compiler code becomes clearer at the same time, not just "more clever"
