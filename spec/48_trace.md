# Spec 48 — AIML_TRACE: Function Tracing

## Overview

`AIML_TRACE` is an environment variable that enables automatic function-call
tracing at runtime without modifying AIML source code.  When active, every
call to a traced function emits one line to `stderr` showing the function name
and its argument values.

## Usage

```bash
# Trace specific functions by name (comma-separated):
AIML_TRACE="resolve_owner_spec,fire_trigger" aiml -r game.aiml

# Trace all user-defined functions:
AIML_TRACE="*" aiml -r game.aiml

# No tracing (default):
aiml -r game.aiml
```

## Output Format

```
[TRACE] => fname(arg0=val0, arg1=val1, ...)
```

Output goes to `stderr`.  The program's normal stdout output is unaffected.

### Example

```
[TRACE] => add(x=3, y=7)
[TRACE] => resolve_owner_spec(g=<ptr>, otype=2, ozone=0, ctx=<ptr>)
[TRACE] => fire_trigger(g=<ptr>, code=10, att_id=32, target=45)
```

## Value Representation

| Argument type | Printed as                   |
|---------------|------------------------------|
| `i32 / i64`   | decimal integer              |
| `f32 / f64`   | `%.6g` / `%.15g` float       |
| `bool / i8`   | decimal integer (0 or 1)     |
| `string`      | string value                 |
| `i8*` handle  | string value (best-effort)   |
| struct / list / map | `<?>` (opaque)        |

## Hot-Path Cost

When `AIML_TRACE` is **not** set, the only overhead per function call is one
`aiml_trace_active()` lookup (O(1) hash) and one conditional branch.  The
trace side-block is never entered.

## Implementation Notes

- `src/sys/trace_runtime.h/cpp` — `TraceRuntime` class + 2 extern C symbols:
  `aiml_trace_active(fname) → i32`, `aiml_trace(fname, dir, args) → void`
- `compiler.cpp` `jitRun()` — calls `aiml_trace_init()` before JIT execution;
  registers `aiml_trace_active` and `aiml_trace` as JIT symbols via `AddSymbol`
- `src/core/ast_compiler.cpp` `visitFunction()` — emits trace prologue IR into
  every user-defined function body: conditional branch → trace side-block →
  string-build with existing `StringUtils` helpers → `aiml_trace` call
