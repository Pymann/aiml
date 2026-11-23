# sprint_task_8

## Title

Clean up top-level storage semantics and secure IR invariants

## Status

Partly outdated, but still relevant.

## Goal

Top-level `decl` should stay semantically consistent, and invalid IR situations should no longer only surface deep in late LLVM paths.

## Current state

- `compiler.cpp` already verifies modules before the JIT and execution paths.
- At the same time `libraryMode`, `inTopLevelInit`, `__aiml_init` and global symbol promotion still exist in the real compiler state.

## What was outdated in the old version

- "Introduce module verification before every path" is no longer the main gain; verification already exists.
- Today the core is rather the storage and initialisation semantics of top-level state than the bare verifier itself.

## Focus

1. Clarify the top-level storage rule: global, init code and access path.
2. Further restrict problematic use of foreign frames/allocas.
3. Make verifier errors usable as clear AIML diagnostics.

## Affected places

- [ast_compiler.cpp](../../src/core/ast_compiler.cpp)
- [compiler.cpp](../../compiler.cpp)
- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)
- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)
- [context.h](../../src/core/context.h)

## Acceptance criteria

- Top-level state behaves consistently across the relevant paths.
- Invalid IR leads to an AIML error instead of late LLVM crashes.
- Verifier messages are practically usable for repros.
