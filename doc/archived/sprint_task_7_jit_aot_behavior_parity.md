# sprint_task_7

## Title

Establish semantic JIT/AOT parity

## Status

Still open and to be clearly distinguished from task 12.

## Goal

The same AIML content should have the same visible semantics in JIT and AOT.

## Current state

- [compiler.cpp](../../compiler.cpp) still shows separate entry points for `compile`, `compileFromString`, `jitRun` and `compileChunkAndGetAddress`.
- Module verification already exists in both main paths; so today the problem is less "the verifier is missing" than "semantics and init behaviour drift apart".

## What was outdated in the old version

- The task mixed semantic parity with pipeline architecture.
- Verification as the core gap is no longer the right focus today.

## Focus

1. Import and symbol visibility.
2. Init behaviour including `__aiml_init`.
3. Context and side effects between the file path and the string path.
4. Small repros that hold both paths against each other.

## Affected places

- [jit_vs_aot.md](../../jit_vs_aot.md)
- [compiler.cpp](../../compiler.cpp)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp)
- [exec_impl.cpp](../../exec_impl.cpp)
- [fptable.cpp](../../src/core/fptable.cpp)

## Acceptance criteria

- Core cases behave the same in JIT and AOT.
- Import and init rules are no longer surprisingly path-dependent.
- Remaining differences are small and deliberately documented.
