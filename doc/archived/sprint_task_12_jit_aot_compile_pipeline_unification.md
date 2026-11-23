# sprint_task_12

## Title

Unify the compile pipeline for file, string, JIT and chunk compilation

## Status

Still open and clearly separate from task 7.

## Goal

The different compile entry points should be reduced to a small shared core.

## Current state

- [compiler.cpp](../../compiler.cpp) still has separate paths for `compile()`, `compileFromString()`, `jitRun()` and `compileChunkAndGetAddress()`.
- Exactly that keeps the task current.

## What was outdated in the old version

- Little in substance; the task is still accurate.
- The only important thing is the clearer separation from task 7: task 12 is pipeline architecture, not semantic parity.

## Focus

1. Expose the shared compile core.
2. Layer source frontend, JIT, chunk and object emit on top as strategies.
3. Anchor symbol/library preload and verification in a few clear places.

## Affected places

- [compiler.cpp](../../compiler.cpp)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp)

## Acceptance criteria

- New features do not have to be retrofitted twice into several compile entry points.
- Compile paths differ only where it is technically necessary.
- Verification, import setup and init hooks are anchored in a few fixed places.
