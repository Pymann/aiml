# Spec 48 — AIML_TRACE / AIML_SIZETRACE: Function & Size Tracing

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

## Usage with exodus

When a program is built via `exodus build`, the `env` field in the
`exodus*.json` config is merged into the environment of every compiler and
linker subprocess.  Set `AIML_TRACE` there so that all invocations inherit it
automatically — including JIT execution triggered inside the `aiml` compiler
subprocess.

```json
{
    "env": {
        "AIML_TRACE": "*"
    }
}
```

To enable tracing for specific functions, set the value to a comma-separated
function list or `"*"` for all.  An empty string `""` disables tracing.
Override from the command line if needed:

```bash
AIML_TRACE="pf_worker_loop,pf_dispatch_jobs" exodus build -c exodus-train.json
```

Note: the `env` field in the JSON is the canonical way to persist the setting.
A command-line override takes precedence over the JSON value (shell env wins
because exodus merges `{**os.environ, **config.env}`).

> **Important:** JSON does not allow duplicate keys.  A file with two `"env"`
> entries will silently use only the second one.  Always merge all environment
> variables into a single `"env"` object.

## Implementation Notes

- `src/sys/trace_runtime.h/cpp` — `TraceRuntime` class + 2 extern C symbols:
  `aiml_trace_active(fname) → i32`, `aiml_trace(fname, dir, args) → void`
- `compiler.cpp` `jitRun()` — calls `aiml_trace_init()` before JIT execution;
  registers `aiml_trace_active` and `aiml_trace` as JIT symbols via `AddSymbol`
- `src/core/ast_compiler.cpp` `visitFunction()` — emits trace prologue IR into
  every user-defined function body: conditional branch → trace side-block →
  string-build with existing `StringUtils` helpers → `aiml_trace` call

---

# AIML_SIZETRACE: Per-Symbol Size / Address / Type Tracing

## Overview

`AIML_SIZETRACE` is the size/layout sibling of `AIML_TRACE`.  For the functions
you name, it prints — for every variable **declaration**, **assignment**, and
**call result** — the symbol's **size** (bytes), **address** (runtime storage
pointer), and **type**.  It is built for diagnosing struct-size / layout
mismatches (e.g. a 16-byte struct written into an 8-byte slot, or a model whose
serialized size disagrees with its native `sizeof`).

Unlike `AIML_TRACE` (runtime-gated: instrumentation is emitted into every
function and a runtime check decides), `AIML_SIZETRACE` is **compile-time
gated**: the **compiler** reads the variable and instruments only the listed
functions.  There is therefore no per-call cost anywhere else, and changing the
target set requires a rebuild.

## Usage — put it in the `env` field of `exodus*.json`

`AIML_SIZETRACE` must be set in the **`env` field of the `exodus*.json` config**,
**not** as a shell prefix:

```json
{
    "env": {
        "AIML_SIZETRACE": "collect_owned,create_bundle"
    }
}
```

Then `exodus build`. Use a comma-separated token list, or `"*"` / `"all"` for
every function. Matching is **substring-based**, so a token like `collect_owned`
also matches a mangled method symbol such as `CardService_collect_owned`.

> **Why not a shell prefix?** `AIML_SIZETRACE=… exodus build` is unreliable: the
> feature is compile-time, but exodus's incremental build cache keys on source
> content + config — **not** on the ambient shell environment. A shell-only
> change does not invalidate the cache, so already-compiled objects keep their
> old (or no) instrumentation and you silently trace the wrong set. Changing the
> value **inside `exodus*.json`** changes the config → forces a full recompile →
> the instrumentation is always correct. This is the canonical, reliable way.
> (`AIML_TRACE` tolerates a shell prefix because it is runtime-gated — the code
> is emitted unconditionally and the env is read when the program runs. That
> does not apply to the compile-time `AIML_SIZETRACE`.)

## Output Format

To `stderr`:

```
[SIZETRACE] <fname> | <sym> : <type>  size=<bytes>  addr=<ptr>
```

### Example

```
[SIZETRACE] CardService_create_bundle | out      : CardBundleResp  size=128  addr=0x7ffd1f8f1298
[SIZETRACE] CardService_create_bundle | type_i   : i32             size=4    addr=0x7ffd1f8f1290
[SIZETRACE] CardService_create_bundle | card_id  : string          size=8    addr=0x7ffd1f8f1328
[SIZETRACE] CardService_collect_owned | cdt      : CreatureData    size=136  addr=0x7ffd1f8f0de0
```

- `size` — compile-time `DataLayout::getTypeAllocSize` of the symbol's element type.
- `addr` — runtime pointer of the symbol's storage (its `alloca`), printed on each
  execution so aliasing / storage reuse is visible.
- `type` — the original AIML type string (`Symbol::typeName`).

## What is traced

The hook lives in `Compiler::visitStatement`: after each `DeclStmt` and `SetStmt`
it emits an `aiml_sizetrace(...)` call for the affected symbol.  Because a call
result is normally bound via `decl:` / `set:` (`decl: [x, { call: [f, …] }]`),
this also covers **call results** — `x`'s size after the call.  A standalone
`call:` with no binding has no symbol and is not traced.

**Absence is a signal:** the trace call sits *after* the store, so if a symbol's
line never prints, its declaration's initializer aborted before completing (e.g.
a crash inside a `get_copy` / persist initializer).

## Implementation Notes

- `src/sys/trace_runtime.h/cpp` — `aiml_sizetrace(fname, sym, type, size, addr)`
  (linked into every runtime lib via the `src/**/*.cpp` glob; compile-time gated,
  so no runtime active-check).
- `src/core/ast_compiler_stmt.cpp` — `sizetrace_active()` parses `AIML_SIZETRACE`
  once; `emitSizeTrace()` builds the call from the symbol's `alloca` + element
  type + `DataLayout` size; the hook is in `visitStatement` after decl/set.
