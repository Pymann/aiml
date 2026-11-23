# sprint_task_11

## Title

Clean up dispatch responsibility for calls and builtins

## Status

Needs heavy reframing; the old version contained several outdated paths.

## Goal

It should be clear which places really decide call semantics today: the AST call compiler, the builtin registry and runtime-bound registration logic.

## Current state

- `call_dispatcher.cpp` does not exist in the current state.
- `expression.cpp` as a legacy reference is not a real active core path either.
- The relevant actual layout today lies mainly in [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp) and [builtin_registry.cpp](../../src/core/builtin_registry.cpp).

## What was outdated in the old version

- Several of the files/paths named are outdated as active parallel truths.
- Today the task must be less "unify the dispatcher" than "clarify ownership in the current call stack".

## Focus

1. Document which call class is decided where.
2. Do not let builtin semantics diffuse between the AST compiler and the registry.
3. Remove outdated references from docs and code comments.

## Affected places

- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)
- [builtin_registry.cpp](../../src/core/builtin_registry.cpp)
- [builtin_registry.h](../../src/core/builtin_registry.h)
- for legacy hints: [context.h](../../src/core/context.h)

## Acceptance criteria

- For every call class it is clear which component is responsible.
- Outdated dispatcher assumptions no longer live on in the sprint plan and code comments.
- New fixes do not have to be followed up in several supposed parallel paths.
