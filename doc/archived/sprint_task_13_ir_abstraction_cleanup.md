# sprint_task_13

## Title

Reduce the IR abstraction to one clear ownership path

## Status

Still open.

## Goal

The parallel existence of raw LLVM paths and a wrapper abstraction should be reduced to a reliable target architecture.

## Current state

- [ir_generator.cpp](../../src/core/ir_generator.cpp) still exists, but is small.
- At the same time, most of the real compiler work still lives very close to raw LLVM in the `ast_compiler_*` files.

## What was outdated in the old version

- The task sounded like a pure `Symbol` cleanup; the problem is broader and concerns the official ownership path for IR generation as a whole.

## Focus

1. Decide the target architecture explicitly.
2. Then mark mixed paths.
3. Only allow new compiler logic through the target path.

## Affected places

- [ir_generator.cpp](../../src/core/ir_generator.cpp)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)
- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)
- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)
- [context.h](../../src/core/context.h)

## Acceptance criteria

- New compiler work no longer has to jump between two equal IR worlds.
- Mixed paths are visible and get smaller step by step.
- The target architecture is binding enough for follow-up work.
