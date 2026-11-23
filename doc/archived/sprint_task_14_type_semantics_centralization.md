# sprint_task_14

## Title

Pull type semantics out of scattered special cases into a central API

## Status

Still open and strategically important.

## Goal

Type rules for pointers, containers, heap types and serialisation should no longer be spread across the compiler as scattered string and special-case logic.

## Current state

- An explicit `TypeSemantics` API does not exist in the current code.
- Type decisions are still spread over [context.h](../../src/core/context.h), [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp), [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp), [serialize_impl.cpp](../../src/core/serialize_impl.cpp) and [builtin_registry.cpp](../../src/core/builtin_registry.cpp).

## What was outdated in the old version

- Little in substance; the task was already close to the actual problem.
- Today only the clearer connection to serialisation, mirror and container layout needs adding.

## Focus

1. Define a small central type semantics API.
2. Encapsulate the most important questions there.
3. Switch call, decl, serialize and builtin paths over to it step by step.

## Affected places

- [context.h](../../src/core/context.h)
- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)
- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)
- [serialize_impl.cpp](../../src/core/serialize_impl.cpp)
- [builtin_registry.cpp](../../src/core/builtin_registry.cpp)

## Acceptance criteria

- New type rules no longer arise as scattered string special cases.
- Several compiler paths use the same type decision.
- Container, pointer and heap edge cases become more consistent.
