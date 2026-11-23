# sprint_task_2

## Title

Stabilise negative tests for call type errors and arity errors

## Status

Still open and directly coupled to `sprint_task_1`.

## Goal

A small, reliable package of negative tests should ensure that wrong calls reproducibly end as compiler errors.

## Current state

- Today the call path is more concentrated in [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp).
- The old version still referred to a dispatcher layout that no longer exists like that in the current code.

## What was outdated in the old version

- `call_dispatcher.cpp` as the central test reference is outdated.
- The task was described as a mere collection, without prioritising the error classes.

## Focus

1. Arity errors.
2. Elementary type mismatches for user functions.
3. Method and `fn<...>` cases.
4. Only after that pointer/struct edge cases.

## Affected places

- [tests](../../tests)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)
- for builtin exceptions: [builtin_registry.cpp](../../src/core/builtin_registry.cpp)

## Acceptance criteria

- For every prioritised error class there is at least one negative test.
- The tests fail in the compiler instead of only at runtime.
- Error texts contain at least the target symbol, the argument position and the type cause.
