# sprint_task_4

## Title

Bring function call semantics closer to the current spec

## Status

Still relevant, but to be cut narrower than in the first version.

## Goal

The call rules described in [07_functions.md](../../spec/07_functions.md) should be mapped step by step onto the real compiler path.

## Current state

- Today the operational core lives in [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp).
- `sprint_task_1` covers the hard type error path; this task is broader and more spec-oriented.

## What was outdated in the old version

- Too much overlap with `sprint_task_1`.
- References to dispatcher structures that no longer form the main path today.

## Focus

1. Default arguments.
2. Named or trailing map arguments.
3. A clear error model for unsupported parts of the spec.
4. User function rules before builtin special rules.

## Affected places

- [spec/07_functions.md](../../spec/07_functions.md)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)
- [compiler.cpp](../../compiler.cpp)
- for builtin deviations: [builtin_registry.cpp](../../src/core/builtin_registry.cpp)

## Acceptance criteria

- Supported call features behave consistently with the spec.
- Unsupported features do not end in implicit chaos but in a clear limitation.
- Positive and negative tests separate user function rules from builtin exceptions.
