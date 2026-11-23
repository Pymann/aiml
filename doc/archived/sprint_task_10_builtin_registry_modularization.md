# sprint_task_10

## Title

Modularise `builtin_registry.cpp` by function

## Status

Still relevant, unchanged.

## Goal

Cut the still very large builtin monolith into domain modules without breaking the existing builtin surface.

## Current state

- [builtin_registry.cpp](../../src/core/builtin_registry.cpp) is still at around 7250 lines.
- [builtin_registry.h](../../src/core/builtin_registry.h) already exists, but there is no real domain separation yet.

## What was outdated in the old version

- The problem statement is still right; in practice only the current state needs adding today.

## Focus

1. Keep the registration interface small.
2. Introduce domain modules.
3. Move shared helpers out separately.
4. No semantic change as a precondition.

## Affected places

- [builtin_registry.cpp](../../src/core/builtin_registry.cpp)
- [builtin_registry.h](../../src/core/builtin_registry.h)

## Acceptance criteria

- New builtins land in domain modules.
- The central registration surface stays small.
- The split does not lead to silent language changes.
