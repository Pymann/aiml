# sprint_task_17

## Title

Centralise compiler diagnostics on reliable AIML file/line/column

## Status

New. Still open and sensible in the short term.

## Goal

Compiler and semantic errors should reliably appear as AIML diagnostics, i.e. in the format `aiml_file:line[:column] message`, instead of only as raw `LOG_ERROR(...)` output with a C++ file or entirely without an AIML source location.

## Why this task is necessary

The current compiler already has several pieces of groundwork:

- [ast_nodes.h](../../src/core/ast_nodes.h) attaches `sourceNode` and `sourceLine` to AST nodes.
- [ast_builder.cpp](../../src/core/ast_builder.cpp) updates `currentLine` from `YAML::Mark()`.
- [context.h](../../src/core/context.h) already has a primitive location surface with `currentSourceFile`, `currentLine`, `currentFunctionName` as well as `fmtLoc()`.

Still, the error surface is not consistently centralised.

In the current state there are many compiler paths that still use `LOG_ERROR(...)` directly, without `fmtLoc(...)` or node-based diagnostics. A sample over the current code shows:

- 142 `LOG_ERROR(...)` sites in the compiler/core code
- of which only 12 with `fmtLoc(...)`
- so 130 sites without an explicit AIML source location

That explains the observed effect: some errors show the AIML file/line, others end only as a generic compiler or C++-internal message.

## Current state

### Already present

- Primitive location tracking fields in [context.h](../../src/core/context.h)
- AST nodes with YAML origin in [ast_nodes.h](../../src/core/ast_nodes.h)
- Partly localised error paths, e.g. in [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)

### Not yet solved cleanly

- Many semantic errors in [ast_compiler.cpp](../../src/core/ast_compiler.cpp), [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp), [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp), [utils.cpp](../../src/core/utils.cpp) and [builtin_registry.cpp](../../src/core/builtin_registry.cpp) still run without an AIML source location.
- Parse/builder errors such as in [expression_builder.cpp](../../src/core/ast/expression_builder.cpp) use raw `LOG_ERROR(...)` messages.
- Top-level errors in [compiler.cpp](../../compiler.cpp) do not catch YAML/AST/import errors consistently in a uniform AIML diagnostic format.
- There is even at least one raw `throw std::runtime_error(...)` path in [context.h](../../src/core/context.h) that bypasses the central diagnostic layer completely.

## What the previous planning was missing

The topic was so far partly contained in [sprint_task_16_stacktraces_and_crash_diagnostics.md](../../sprint_task_16_stacktraces_and_crash_diagnostics.md), but there as part of a much broader crash/runtime architecture.

For the current compiler quality a separate task makes more sense, because the problem is narrower:

- no runtime frames
- no signal handlers
- no native stack trace
- but the completeness of the AIML compiler diagnostics

Task 17 is thus not a replacement for task 16, but a targeted preliminary stage: compiler errors first have to be reliably localised before AIML stack traces and larger diagnostic pipelines really work cleanly.

## Focus

1. Introduce a small central compiler diagnostic API.
2. Switch raw `LOG_ERROR(...)` paths with AST/node/location context over to this API.
3. Bring parse, builder, semantic and top-level compile errors into a uniform AIML diagnostic format.
4. Add negative repros that do not just check for failure, but explicitly for file/line/column in the error text.

## Affected places

- [compiler.cpp](../../compiler.cpp)
- [src/core/context.h](../../src/core/context.h)
- [src/core/ast_nodes.h](../../src/core/ast_nodes.h)
- [src/core/ast_builder.cpp](../../src/core/ast_builder.cpp)
- [src/core/ast_compiler.cpp](../../src/core/ast_compiler.cpp)
- [src/core/ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)
- [src/core/ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)
- [src/core/ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)
- [src/core/utils.cpp](../../src/core/utils.cpp)
- [src/core/string_utils.h](../../src/core/string_utils.h)
- [src/core/builtin_registry.cpp](../../src/core/builtin_registry.cpp)
- [logger.h](../../logger.h)

## Concrete problem classes

### 1. AST/semantic errors without node localisation

Examples:

- `Unknown parent class`
- `Unknown model`
- `Duplicate member`
- `Undefined property`
- `Variable shadowing`
- `Unknown class for new`

These errors often arise at AST nodes, but today still log without their `sourceNode`.

### 2. Builder/parse errors without a clean AIML location

Examples:

- outdated operators in [expression_builder.cpp](../../src/core/ast/expression_builder.cpp)
- string expansion parse errors in [string_utils.h](../../src/core/string_utils.h)
- YAML exceptions in [compiler.cpp](../../compiler.cpp)

Here a clear distinction must be made between:

- a real parser/YAML location
- the current builder line
- an imported file

### 3. Top-level compile errors without a uniform diagnostic object

Examples:

- import resolution
- `compileFromString`
- object file/IR emission
- `verifyModule` errors

Not every one of these errors has an AIML node. The task must therefore define a clean staggering:

- `loc from node`
- otherwise `loc from current compiler state`
- otherwise `file only`
- otherwise mark it deliberately as a non-AIML internal toolchain error

### 4. Raw exceptions instead of diagnostics

`throw std::runtime_error(...)` in the compiler core is incompatible with this target picture when the error is actually a localisable AIML semantic error.

## Target architecture

A small central surface is needed, for example in or next to [context.h](../../src/core/context.h):

- `DiagnosticLoc`
- `ctx.error(node, message)`
- `ctx.error(loc, message)`
- `ctx.errorCurrent(message)`
- optionally `ctx.note(...)`

This API should:

- normalise file, line, column and optionally the function
- format the final text
- be able to set a `hadError` state centrally
- stay compatible with task 16 later

It is important that new compiler errors no longer call `LOG_ERROR(...)` directly when they semantically hang off AIML source code.

## Delimitation from task 16

Task 16:

- native crash diagnostics
- AIML runtime stack traces
- crash handler
- thread-local AIML frames

Task 17:

- compiler and semantic diagnostics
- completeness and consistency of the AIML file/line/column
- builder/parser/semantic errors in the normal error return channel

Recommended order:

1. Task 17 first, for clean compiler errors
2. after that task 16, for runtime and crash diagnostics

## Acceptance criteria

- A typical semantic error such as an unknown field, unknown type, wrong member access or shadowing reliably appears with the AIML file and line.
- Parse/builder errors do not lose the file, even with imports or `compileFromString`.
- The usual compiler paths no longer use raw `LOG_ERROR(...)` messages for localisable AIML semantic errors.
- The remaining use of raw `LOG_ERROR(...)` is limited to real toolchain/runtime/host errors.
- There are regression tests for at least:
  - unknown field
  - unknown type
  - outdated operator
  - shadowing
  - YAML parse error
  - error in an imported file
  - error via `compileFromString`
- The error format is consistently readable for these cases, preferably `file:line[:column] message`.

## Recommended first implementation

1. Define `DiagnosticLoc` and `ctx.error(...)`.
2. Switch a narrow package of particularly frequent AST/semantic errors over to the new API:
   - [ast_compiler.cpp](../../src/core/ast_compiler.cpp)
   - [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)
   - [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)
3. Follow up with builder/parse errors.
4. Unify the top-level compiler paths in [compiler.cpp](../../compiler.cpp).
5. Add negative tests on diagnostic content.
