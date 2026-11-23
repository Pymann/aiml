# sprint_task_5

## Title

Pull member access and model validation onto a shared path

## Status

Still highly relevant.

## Goal

Deep member access, lvalue resolution and model validation should become consistent instead of running through several half-separate compiler paths.

## Current state

- Member access is still spread over [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp), [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp) and [utils.cpp](../../src/core/utils.cpp) today.
- [member_chain_plan.md](../../member_chain_plan.md) is still the obvious groundwork.

## What was outdated in the old version

- Too much focus on merely documented spec promises, too little on the real central access path.
- Structural typing and deep access were mixed up, although they carry different risks.

## Focus

1. A unified resolver for read/write/lvalue.
2. After that deep chains and auto-dot deref.
3. Only after that map-to-model validation and structural compatibility.

## Affected places

- [spec/15_models.md](../../spec/15_models.md)
- [member_chain_plan.md](../../member_chain_plan.md)
- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)
- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)
- [utils.cpp](../../src/core/utils.cpp)

## Acceptance criteria

- Deep member chains behave consistently for read and write.
- `ptr<Model>` access follows one clear rule.
- Model validation is traceable and no longer just a side effect of individual code paths.
