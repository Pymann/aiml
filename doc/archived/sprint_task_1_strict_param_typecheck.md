# sprint_task_1

## Title

Consolidate strict parameter type checking in the call path

## Status

Partly prepared, but not yet secured as a consistently hard language rule.

## Goal

Function and method calls with wrong argument types should fail early and unambiguously in the compiler, instead of implicitly casting on or running late into LLVM/runtime errors.

## Current state

- The relevant main path today lies in [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp).
- The old assumption of a separate `call_dispatcher.cpp` as the core path is outdated; that file does not exist in the current state.
- Builtin semantics still live largely in [builtin_registry.cpp](../../src/core/builtin_registry.cpp).

## What was outdated in the old version

- `call_dispatcher.cpp` as the central dependency is no longer correct.
- The task was too broad for "all call kinds at once".
- The separation between language rule and builtin special case was not sharp enough.

## Focus

1. Harden direct user function calls.
2. Then pull method calls and `fn<...>` calls onto the same validation logic.
3. Isolate builtin special cases explicitly, instead of silently dragging them along in the general call rules.

## Affected places

- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)
- [compiler.cpp](../../compiler.cpp)
- [context.h](../../src/core/context.h)
- for builtin special cases: [builtin_registry.cpp](../../src/core/builtin_registry.cpp)

## Acceptance criteria

- A wrong parameter type for user functions produces a clear compiler error.
- Too many and too few arguments are handled consistently.
- Methods and `fn<...>` follow the same basic rule or are explicitly marked as follow-up work.
- Builtin exceptions are visible and no longer an unintended side effect of the general call path.
