# sprint_task_9

## Title

Split `CompilerContext` along real responsibilities

## Status

Still open and structurally important.

## Goal

Slim down the large ambient state container [context.h](../../src/core/context.h) so that new features are no longer coupled to a god context.

## Current state

- `CompilerContext` is still large and carries semantic, codegen, import, init, serialisation and mirror-related state fields.
- Today the task is even more urgent than in the first version, because many newer topics hang off it.

## What was outdated in the old version

- The proposed target cut was phrased as a rigid set of classes.
- Today a step-by-step cut along read/write patterns makes more sense than an immediate big split.

## Focus

1. Inventory the fields and group them into clusters.
2. Pull out short-lived call/compile state first.
3. Then separate import/session/codegen state.
4. Deliberately plan for an adapter phase.

## Affected places

- [context.h](../../src/core/context.h)
- [compiler.cpp](../../compiler.cpp)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp)

## Acceptance criteria

- New features no longer automatically hang off a global god context.
- Short-lived compile/call state is separated from long-lived module/semantic state.
- Migration can happen step by step, without breaking the whole compiler in one go.
