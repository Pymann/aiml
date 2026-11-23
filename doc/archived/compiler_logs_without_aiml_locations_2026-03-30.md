# Compiler Logs Without AIML Source Locations

Date: 2026-03-30

## Goal

This list identifies log/diagnostic sites in the AIML project that currently carry no AIML file/line in the message. The focus is above all on compiler and semantic errors where an AIML source is expected.

## Methodology

- Searched were `compiler.cpp`, `src/core`, `src/sys`.
- Searched for `LOG_ERROR`, `LOG_WARNING`, `LOG_WARN`, `LOG_INFO`.
- "With AIML location" here primarily counts sites that prepend `ctx.fmtLoc(...)` or `fmtLoc()` directly to the message.
- Infrastructure/runtime logs without an AIML source are marked separately; they are not necessarily wrong, but not sufficient for compiler diagnostics.

## Rough figures

- Log sites found in total: `152`
- Of those directly with `fmtLoc(...)`/`fmtLoc()`: `12`

The delta is larger than the real need for action, because quite a few logs are deliberately LLVM/runtime/cache logs. The gaps relevant for `sprint_task_17` lie above all in the AST/semantic files.

## Priority A: real compiler diagnostics without an AIML file/line

These sites should preferably be switched to `ctx.fmtLoc(node)` or at least `ctx.fmtLoc()`.

### `src/core/ast_compiler_expr.cpp`

- `112`: `Null LLVM value evaluated for initialization of member: ...`
  Fix: `ctx.fmtLoc(<expr/member node>)` prepend.
- `115`: `Failed to compile AST expression for initialization of member: ...`
  Fix: `ctx.fmtLoc(<expr/member node>)`.
- `122`: `Unknown class for new: ...`
  Fix: `ctx.fmtLoc(newExpr->sourceNode)`.
- `168`: `Invalid unknown literal (expected bool or number): ...`
  Fix: `ctx.fmtLoc(expr->sourceNode)`.
- `296`: `Array Slicing not supported on raw arrays yet.`
  Fix: `ctx.fmtLoc(call/expr source node)`.
- `322`: `Invalid list index: ...`
  Fix: `ctx.fmtLoc(expr->sourceNode)`.
- `553`: `Cannot apply eq/neq to struct type without __eq__: ...`
  Fix: `ctx.fmtLoc(bin->sourceNode)`.
- `583`: `Unknown binary operator: ...`
  Fix: `ctx.fmtLoc(bin->sourceNode)`.
- `597`: `Unary '&': operand must be a variable`
  Fix: `ctx.fmtLoc(unary->sourceNode)`.
- `599`: `Unary '&': undefined variable: ...`
  Fix: `ctx.fmtLoc(unary->sourceNode)`.
- `675`: `Unknown unary operator: ...`
  Fix: `ctx.fmtLoc(unary->sourceNode)`.
- `682`: `spawn: unknown function: ...`
  Fix: `ctx.fmtLoc(spawn->sourceNode)`.
- `758`: `spawn: thread_handle type not found`
  Fix: `ctx.fmtLoc(spawn->sourceNode)`.

### `src/core/ast_compiler_stmt.cpp`

- `43`: `Variable shadowing is not allowed: ...`
  Fix: `ctx.fmtLoc(decl->sourceNode)` or `ctx.fmtLoc()`.
- `268`: `Failed to evaluate value expression for assignment to: ...`
  Fix: `ctx.fmtLoc(stmt->sourceNode)`.
- `274`: `Cannot assign to const variable ...`
  Fix: `ctx.fmtLoc(stmt->sourceNode)`.
- `385`: `Slice assignment is not supported yet: ...`
  Fix: `ctx.fmtLoc(stmt->sourceNode)`.
- `400`: `Invalid list index for set: ...`
  Fix: `ctx.fmtLoc(stmt->sourceNode)`.
- `476`: `Failed to evaluate return expression`
  Fix: `ctx.fmtLoc(stmt->sourceNode)`.
- `480`: `Empty return statement in non-void function!`
  Fix: `ctx.fmtLoc(stmt->sourceNode)`.
- `577`: `AST Error: Try statement is missing a body. ...`
  Fix: `ctx.fmtLoc(stmt->sourceNode)`.

### `src/core/ast_compiler.cpp`

- `471`: `resolveImportCallback not bound! Cannot process import: ...`
  Fix: `ctx.fmtLoc(imp->sourceNode)`.
- `496`: `Unknown parent class: ...`
  Fix: `ctx.fmtLoc(node/class source)`.
- `510`: `Unknown model: ...`
  Fix: `ctx.fmtLoc(node/model source)`.
- `524`: `Duplicate member ... in class ...`
  Fix: `ctx.fmtLoc(memberNode)`.
- `530`: `Unknown type ... for member ...`
  Fix: `ctx.fmtLoc(memberNode)`.
- `681`: `Unknown type ... for field ... in model ...`
  Fix: `ctx.fmtLoc(fieldNode)`.
- `999`: `Variable shadowing is not allowed: function argument ...`
  Fix: `ctx.fmtLoc(fn->sourceNode)` or the arg node.
- `1085`: `Undefined enum variant: ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1147`: `Dash-index on non-list field: ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1168`: `Invalid list index: ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1228`: `Undefined property: ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1262`: `Cannot access private member ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1265`: `Cannot access protected member ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1279`: `Dash-index on non-list field: ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1305`: `Invalid list index: ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1337`: `Cannot access private member ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1340`: `Cannot access protected member ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1344`: `Cannot assign to const member ...`
  Fix: `ctx.fmtLoc(expr/source node)`.

### `src/core/ast_compiler_call.cpp`

- `258`: `Failed to evaluate argument ... for call to ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `324`: `Null LLVM value passed as argument ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `546`: `Unknown chained sub-method: ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `587`: `buffer<T>.get: unknown elem type ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `793`: `Unknown list method: ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `842`: `Unknown map method: ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `869`: `Unknown channel method: ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `960`: `get_by_key: no primary_key defined for ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1004`: `Unknown Store method: ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1085`: `Unknown Kv method: ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1151`: `Unknown VecStore method: ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1240`: `Unknown TimeSeries method: ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1345`: `sys.serialize: 'any' type is not serialisable`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1353`: `sys.serialize: ptr<...> inner type is not serialisable`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1368`: `sys.serialize: type ... is not serialisable`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1395`: `sys.deserialize: requires at least one argument ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1406`: `sys.deserialize: second argument must be a string literal type name`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1414`: `sys.deserialize: type cannot be inferred ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1424`: `sys.deserialize: 'any' type is not serialisable`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1432`: `sys.deserialize: ptr<...> inner type is not serialisable`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1440`: `sys.deserialize: type ... is not serialisable`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1512`: `sys.bundle: requires 1 argument ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1539`: `sys.bundle: no YAML source found for ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1579`: `sys.bundle: unknown variable ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1586`: `sys.bundle: no YAML source found for type ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1631`: `sys.bundle: argument must be a string literal ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1640`: `sys.exec: requires exactly 1 argument ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1648`: `sys.exec: argument must be a Code variable ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1657`: `sys.exec: failed to evaluate Code argument`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1680`: `db.run_system: requires at least one argument ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1689`: `db.run_system: first arg must be a function name`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1695`: `db.run_system: function ... not found in module`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1706`: `db.run_system: need at least one store argument`
  Fix: `ctx.fmtLoc(call->sourceNode)`.

### `src/core/utils.cpp`

This file produces many semantic errors, but currently has no explicit node context in the log.

- `13`, `41`, `44`, `49`, `64`, `67`, `76`, `82`, `98`, `134`, `146`, `226`, `257`
  Fix: extend the function signatures so that a `CompilerContext&` plus optionally a `const YAML::Node*` or `sourceLine` can be passed along. Then prepend `ctx.fmtLoc(...)`.

Note:
- A string prefix alone is not enough here. The call stack must pass the source context down into the utility helpers.

### `src/core/context.h`

- `345`: `CRITICAL ERROR: getLLVMType called with invalid typeName ...`
- `432`: `Failed to resolve key/val types for map`
- `452`: `Separator ':' not found in map type content: ...`

Fix:
- If it can be triggered from user AIML: prepend `fmtLoc()`/`fmtLoc(node)`.
- If it is an internal invariant: mark it explicitly as an internal compiler error, e.g. `internal compiler error`.

### `src/core/expression_builder.cpp`

- `50`: `Operator '...' is not allowed. Use '...' instead.`

Fix:
- The builder already carries line information; here the current builder context with file/line should flow into the error message.

### `src/core/string_utils.h`

- `238`: `YAML Parse Error in String Expansion: ...`
- `246`: `String Expansion requires evaluator: ...`
- `280`: `Unmatched brace in expression: ...`

Fix:
- If these paths come from AIML source, pass the SourceLoc down into the string expansion context.
- Otherwise at least pass along the surrounding selector/function name.

### `src/core/builtin_registry.cpp`

- `2010`: `db.query: predicate function '...' not found`

Fix:
- Pass the `CompilerContext` or the call site node down into the builtin resolution and use `ctx.fmtLoc(call->sourceNode)`.

## Priority B: frontend compiler/driver logs without an AIML location

These sites are partly justified without an AIML line, but should carry the affected AIML file name or import path where possible.

### `compiler.cpp`

Sites without an AIML file/line:

- `227`: `Failed to parse import signatures: ...`
- `239`: `Could not open file: ...`
- `405`: `AST Builder failed to produce a valid program.`
- `408`: `YAML Parsing Error during AST Build: ...`
- `422`: `Could not open file: ...`
- `478`: `compileFromString: AST builder produced no program`
- `481`: `compileFromString: YAML parse error: ...`
- `1743`: `Import source not found: ...`

Fix:
- Always put the file path of the affected AIML file explicitly into the diagnostic.
- Where a YAML node is present, switch to `DiagnosticLoc` later.

Not necessarily needed with an AIML line, but mark clearly as infrastructure:

- `72`, `511`, `666`, `684`, `692`, `704`, `711`, `719`, `725`, `733`
- `1599`, `1610`, `1617`, `1624`, `1639`, `1646`, `1655`, `1662`
- `1749`, `1793`, `1801`, `1803`

Recommendation:
- Do not mix these logs with `fmtLoc()`, but distinguish them cleanly in the message:
  - `AIML compile error: ...`
  - `LLVM/JIT error: ...`
  - `Import cache info: ...`

## Priority C: runtime/system logs without an AIML location

These logs are currently not the main problem of `sprint_task_17`, because they are not direct compiler diagnostics.

### `src/sys/process_runtime.cpp`

- `25`, `41`, `68`, `79`

### `src/sys/process_commands.h`

- `39`

### `src/sys/sys_commands.h`

- `24`

### `src/core/buffer_impl.h`

- `29`, `33`, `98`, `163`, `266`

Assessment:
- Not a bug in the narrower sense, as long as these messages are understood as runtime/system errors.
- If AIML stack traces/diagnostics are expanded in the long term, these paths should at least name the function context or the caller.

## Concrete bug fix strategy

### Stage 1: quick compiler diagnostics

Switch directly to `ctx.fmtLoc(...)` in:

- `src/core/ast_compiler_expr.cpp`
- `src/core/ast_compiler_stmt.cpp`
- `src/core/ast_compiler.cpp`
- `src/core/ast_compiler_call.cpp`

That brings the biggest gain immediately.

### Stage 2: follow up with utility/builtin context

Refactor for:

- `src/core/utils.cpp`
- `src/core/builtin_registry.cpp`
- `src/core/string_utils.h`

A local string fix is not enough here. The source context must be passed along architecturally.

### Stage 3: separate driver vs. diagnostics

In `compiler.cpp` logs should be split into two classes:

- real user diagnostics with an AIML file/line
- infrastructure/LLVM/cache logs without an AIML line, but clearly marked

## Most important first candidates for direct implementation

If only the worst places are to be fixed first:

1. `src/core/ast_compiler_expr.cpp`
2. `src/core/ast_compiler_stmt.cpp`
3. `src/core/ast_compiler_call.cpp`
4. `src/core/ast_compiler.cpp`
5. `src/core/utils.cpp`

These five areas explain most of today's "location-less" AIML compiler errors.
