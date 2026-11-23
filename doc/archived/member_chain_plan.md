# Implementation plan for deep member access

## Problem

In the current AIML compiler, nested member access like `a.b.c` or `root.match.round` is not implemented consistently.

In practice this leads to two symptoms:

- deep reads only work partly or only in certain forms
- deep writes to `set` targets break early, so that you have to create helper variables or reassign whole sub-objects

That matches the observed user behaviour in `9rooms` and the mirror repros.

## Main finding in the C++ code

### 1. `visitVariable(...)` only splits at the first separator

In [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp#L184) to [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp#L190) a variable name is split with `find('.')` or `find('-')`.

That means:

- `a.b.c` first only becomes `baseName = "a"` and `propName = "b.c"`
- the further handling is not built as a generic segment walk

So the expression side is structurally not designed for really arbitrarily deep paths.

### 2. `visitSetStmt(...)` also splits only once

In [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L219) to [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L230) the `set` target is likewise only split into `baseName` and `propName`.

Consequence:

- `root.match.round` is treated as `baseName = "root"` and `propName = "match.round"`
- after that the code only looks for a direct member with exactly this name
- no deeper descending takes place

That is the direct cause of deep `set` targets practically not working cleanly today.

### 3. `Utils::resolveLValue(...)` is closer to the right direction, but not deep enough yet

In [utils.cpp](../../src/core/utils.cpp#L117) to [utils.cpp](../../src/core/utils.cpp#L129) `rfind('.')` is used.

That is better than `find('.')`, because at least the last member is isolated. But:

- `resolveLValueMemberAccess(...)` in [utils.cpp](../../src/core/utils.cpp#L8) expects `objName` to be directly in the `symbol_table` already
- for `a.b.c` this becomes `objName = "a.b"`
- `"a.b"`, however, is not a real symbol in the `symbol_table`

So this path is not really recursive either.

### 4. Reads and writes follow different resolution paths

Today there is no single central path resolution for:

- simple variable access
- member read
- member write
- array/list access
- mixed chains like `a.items-0.hp`

Instead the logic is spread over:

- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)
- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)
- [utils.cpp](../../src/core/utils.cpp)

That is the main reason why deep dot logic is inconsistent.

## Target picture

The compiler should treat every path as a segment chain, not as a string with a special case.

Examples:

- `a.b.c`
- `a.items-0.hp`
- `this.player.hand-2.name`
- `ptr_to_root.match.round`

All these forms should be normalised internally into a uniform sequence of path segments.

## Recommended implementation

### Stage 1: introduce a path parser

First new component:

- `parseAccessPath("a.items-0.hp") -> [var(a), member(items), index(0), member(hp)]`

This logic should no longer sit ad hoc in `visitVariable` and `visitSetStmt`.

Primary C++ location:

- new in [utils.cpp](../../src/core/utils.cpp)

Helpful API:

- `struct AccessSegment`
- `enum class AccessKind { BaseVar, Member, Index }`
- `parseAccessPath(const std::string&)`

Important:

- `.` and `-` must be supported together
- `this.` and `<.` should be normalised before parsing

### Stage 2: build a central recursive/iterative resolution

After that there should be exactly one core function, e.g.:

- `resolveAccessPath(ctx, path, forWrite)`

This function walks through segment by segment and returns:

- either the final pointer/lvalue
- or additionally metadata about the final type

Primary C++ locations:

- [utils.cpp](../../src/core/utils.cpp)
- optionally declarations in [utils.h](../../src/core/utils.h)

This function must handle the following cases uniformly:

- struct members
- pointer/ref dereference to structs
- list elements
- array elements
- mixed chains

### Stage 3: switch `visitVariable(...)` to the new path resolution

`visitVariable(...)` in [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp#L179) should no longer work with `find('.')` itself.

Instead:

- handle namespace/enum special cases early
- after that delegate the rest to the new path resolution

Goal:

- deep reads like `a.b.c`
- mixed reads like `a.items-0.hp`
- treat deep `.len`-like accesses as a property special case only where they really are container properties

### Stage 4: switch `visitSetStmt(...)` completely to the same path core

That is the most important step for the perceived usability.

In [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L213) `set` should no longer split `baseName/propName` manually.

Instead:

- parse the target path
- resolve the final lvalue with `forWrite = true`
- check access protection and `const` at the final segment
- write the value there

If this step is missing, the actual user problem remains.

### Stage 5: integrate property special cases into the path resolution

Currently there are special cases like:

- `list.len`
- `list.empty`
- `map.len`

in [utils.cpp](../../src/core/utils.cpp#L134).

These should not live as a completely separate side branch, but be integrated cleanly into the segment model:

- normal members
- container properties
- index access

Otherwise the path logic stays split in two again.

### Stage 6: delimit builtin handle types deliberately

There are builtins with specially wired setters/getters, e.g. in [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L2421).

Important:

- normal struct chains should run via the new generic path resolution
- builtin handle types may keep special property setters
- but only as explicit special cases at the end of the segment chain

That avoids the builtin API and normal dot logic breaking each other.

## Test plan

I would not rebuild this without new targeted tests.

### New mandatory tests

1. Deep read:
   `a.b.c`

2. Deep write:
   `set: [a.b.c, 7]`

3. Mixed path:
   `a.items-0.hp`

4. Pointer/ref chain:
   `p.nested.value`

5. Container property after a chain:
   `a.cards.len`

6. Negative test:
   access to a non-existent intermediate member

7. Negative test:
   write access to a `const` intermediate path or a final `const` member

### Good additional tests

- `this.` and `<.` with a deep member path
- `set` on nested mirror models
- a combination of `list.get(...)` and dot access, so that it stays clear what is deliberately a method and what a direct path

## Recommended order

1. Path parser in `utils.*`
2. Central `resolveAccessPath(...)` function in `utils.cpp`
3. Switch expression reads to the new API
4. Switch `set` writes to the new API
5. Follow up cleanly with property/builtin special cases
6. Check in new regression tests

## Practical assessment

This is not a small cosmetic fix. It is a small piece of infrastructure work in the compiler.

But:

- the benefit is high
- the language becomes considerably more consistent
- `9rooms` and similar projects lose a lot of helper variable boilerplate
- it removes a real language limit that gets in the way today when modelling larger data structures

## Recommendation

I would treat this as a targeted compiler refactor, not as a point bug fix.

The right lever is:

- a uniform path representation
- a central resolution
- only after that switch reads and writes over to it

Everything else would only put new special cases next to the existing special cases.
