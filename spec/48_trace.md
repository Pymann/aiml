# Spec 48 — AIML_TRACE / AIML_SIZETRACE / AIML_TRACE_SYMBOL: Function, Size & Value Tracing

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
| opaque handle / model pointer | pointer address (`%p`), never read as text |
| interface fat pointer | object and vtable addresses |
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
For builds, configure the selection in the JSON `env` object. Exodus merges
`{**os.environ, **config.env}` (after expanding config paths), so **JSON values win**
when a key is present in both places. A shell prefix only supplies keys absent
from the JSON configuration.

For AOT/WASM, a nonempty build-time `AIML_TRACE` is baked into the binary and
initialized without rereading the runtime environment. With no baked selection,
native execution can read `AIML_TRACE` at startup. Changing a baked selection
requires rebuilding; runtime gating does not imply runtime override of baked values.

> **Important:** JSON does not allow duplicate keys.  A file with two `"env"`
> entries will silently use only the second one.  Always merge all environment
> variables into a single `"env"` object.

## Implementation Notes

- `src/sys/trace_runtime.h/cpp` — `TraceRuntime` class and C runtime entry points:
  `aiml_trace_active(fname) → i32`, `aiml_trace_format(fname, format, ...) → void`
- `compiler.cpp` `jitRun()` — calls `aiml_trace_init()` before JIT execution;
  registers `aiml_trace_active`, `aiml_trace_format` and the legacy `aiml_trace` via `AddSymbol`
- `src/core/ast_compiler.cpp` `visitFunction()` — emits trace prologue IR into
  every user-defined function body: conditional branch → trace side-block →
  source-typed varargs → `aiml_trace_format` (no temporary AIML strings)

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
> Function trace instrumentation is runtime-gated, but AOT/WASM may bake the
> selection at build time; use JSON configuration for reliable build invalidation.

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

---

# AIML_TRACE_SYMBOL: Per-Symbol Value Lifetime Tracing

## Overview

`AIML_TRACE_SYMBOL` observes selected heap locals at **DECL**, **MOVE**, and
**DROP**, plus selected local/model-field assignments at **BEFORE_SET** and **SET**.
MOVE means a pending local cleanup was consumed at an ownership handoff.
BEFORE_SET is before RHS evaluation, SET is after the assignment; these events
observe values, not allocator frees. Missing DROP alone cannot distinguish a move,
a borrow/static value, disabled ownership cleanup, or a missing cleanup.

Like `AIML_SIZETRACE` (and unlike the runtime-gated `AIML_TRACE`), it is
**compile-time gated**: the compiler reads the variable and emits the trace only for
matching symbols. Unset → zero code emitted, no overhead.

## Usage — `env` field of `exodus*.json` (compile-time; same cache caveat as AIML_SIZETRACE)

Comma-separated local-variable names and assignment targets, or `1` / `all` for
every supported symbol. A field matches its exact target (e.g. `<.library`) or its
final field name (`library`):

```json
{ "env": { "AIML_TRACE_SYMBOL": "artdbg,card_name" } }
```

## Output Format

To **stdout** (so it appears in the browser console for WASM builds):

```
[aiml-trace] DECL text (string | fn | file:line: decl=file:line: cleanup=registered) slot@0x... data@0x... = "value"
[aiml-trace] MOVE items (list<string> | fn | ...) len=3 slot@0x... data@0x...
[aiml-trace] SET <.library (list<Card> | fn | ...) len=3 slot@0x... data@0x...
[aiml-trace] DROP base (ptr<CardBase> | fn | ...) iface_obj@0x...
```

- Strings: slot/data addresses and up to 256 content bytes; null-safe, but reading
  a corrupt non-null pointer is still unsafe. A byte limit can split UTF-8 text.
- Lists: length, slot and buffer addresses. Models/maps: storage address.
- Boxed classes: heap address; interfaces: underlying object address.
- Local declarations retain their source location in subsequent events. Event
  locations and the function name disambiguate repeated local names.
- `cleanup=registered` indicates a pending scope cleanup; `unregistered` does not
  prove a leak or a borrow. Ownership-enabled and disabled runs can differ.

## What is traced

DECL fires after initialization of a matching local string/list/map/model/box.
MOVE fires when `consumeLocalDrop` removes a registered local cleanup. DROP fires
immediately before its cleanup code. Early returns use the same cleanup hooks.

BEFORE_SET/SET cover plain locals and dot-separated model/class fields (including
`<.field`), resolved without evaluating extra AIML expressions. By-reference
container parameters are dereferenced to their actual storage. These events do
not cover indexed writes, standalone calls, `list.push`, `decode_rpc`,
`get_copy_at`, native handle destruction or automatic recursive field cleanup.
Those require separate runtime/allocator evidence. Field aliases do not observe
the complete lifetime of the original field.

## Implementation Notes

- `src/core/ast_compiler_stmt.cpp` — `aimlTraceSymbol()` parses `AIML_TRACE_SYMBOL`;
  `emitSymbolValueTrace()` emits the `printf`; DECL hook at `visitDeclStmt` end, DROP
  hook in `emitScopeDrop`, MOVE in `consumeLocalDrop`, assignment hooks around
  `visitSetStmt`. `ScopeDrop` carries `debugName`/`debugType`, set at each
  drop-registration site.
- Pure diagnostic, **independent of spec/61 ownership**: it observes drops/moves but
  changes no semantics.

---

# AIML_MEM_DIFF: Live-Allocation Diff by Call-Stack

## Overview

`AIML_MEM_DIFF` answers **"what is accumulating?"** at runtime, without a manual `AIML_TRACE_SYMBOL`
DECL-vs-DROP tally or a multi-hundred-MB trace. Every AIML heap **leaf-allocation** — currently every
refcounted string (`aiml_str_alloc`) — is recorded in a live map keyed by the **C++ call-stack** at the
allocation; the matching free (`aiml_str_decref` on refcount→0) forgets it. `sys.mem_diff()` (or the
automatic process-exit dump) prints, per allocation site, the **live bytes + block count** and the
**delta since the previous dump** — so a site that keeps growing between two dumps is the leak, in the
function that made it (symbolized via `backtrace_symbols`; on the AOT build with debug info that reads as
e.g. `CardService_get_base`).

**Recursive models / nested class-members are captured cleanly and for free:** tracking is at the leaf
(each string), and deep-free (`aiml_ser_free_decoded`) recurses to those same leaves, so a
`list<Model>`-of-models-with-strings, a nested member, or a recursive `ptr<>` structure is exactly one
balanced record/forget per nested string — no per-type logic. A container whose elements carry heap
(strings/models) therefore shows its leak through those elements. (Not yet tracked: the `list`/`map`
**data buffers** themselves and `{new:}` box shells — they are allocated as inline IR, not through a
runtime choke point; a pure-scalar `list<i32>` buffer leak is thus invisible in v1. Planned follow-up.)

## Gating — COMPILE-TIME via baked activation (the AIML_TRACE model, no defines)

`AIML_MEM_DIFF` is **compile-time gated**, consistent with the other trace features and with no `#ifdef`
defines. The leaf-allocation hooks live in `libaiml`, but the tracker is **inert** — a single
never-taken branch on a global `g_on` — unless the **compiler** emits one `aiml_memdiff_init_baked(file,
interval)` call into the program prologue. The compiler emits it **iff `AIML_MEM_DIFF` is set in the build
environment** (`exodus.json` `env`), reading `AIML_MEM_DIFF_FILE` / `AIML_MEM_DIFF_INTERVAL` at compile
time and **baking** them in. There is **no runtime `getenv`**: a binary built without the feature ignores
a runtime `AIML_MEM_DIFF`, and one built with it needs no runtime env. Changing the selection requires a
rebuild (same cache-invalidation reasoning as `AIML_SIZETRACE`). This keeps the leaf-level accuracy and
move-correctness of an allocator hook (it catches deep `aiml_ser_decode` strings and recursive
model/class-member strings, each one balanced record/forget) while emitting **zero activation** when off.

## Usage — set in `exodus.json` `env` (build-time), like the other trace features

```json
{ "env": {
    "AIML_MEM_DIFF": "1",
    "AIML_MEM_DIFF_FILE": "logs/mem_diff_server.txt",
    "AIML_MEM_DIFF_INTERVAL": "60"
} }
```

Then `exodus build`. The dumps then run automatically (activity-driven periodic + a final exit dump) with
no source change; `sys.mem_diff()` (returns total live bytes) forces an immediate dump if wanted.

## Output Format (to `AIML_MEM_DIFF_FILE`, appended; else `stderr`)

Each dump is one timestamped block, appended to the file so dumps accumulate over the run and the file is
readable live:

```
[mem-diff 2026-09-12 17:39:12] total live: 239575 bytes in 9010 blocks across 11 sites (top 30 by growth since last dump)
  [+21390 B]  live 64170 B / 1500 blocks (ever 1500)
        ./game(CardService_get_base+0x1c4) [0x...]
        ./game(...) [0x...]
  [+0 B]      live 4096 B / 64 blocks (ever 128000)
        ...
```

- `[+N B]` — growth in that site's live bytes since the previous dump. Sites are sorted by this delta:
  the top entries with a large positive delta are what is actively accumulating.
- `live … / … blocks` — current live bytes and block count for the site; `ever` is the total ever
  recorded there (churn context — a high `ever` with `[+0 B]` is hot but balanced, not a leak).
- Frames are the call-stack (top `kPrintFrames`), symbolized on native builds with debug info.

## Implementation Notes

- `src/sys/mem_diff_runtime.h/.cpp` — `aiml_memdiff_init_baked(file, interval)` (the compile-baked
  activation: sets `g_on`, `g_file`, `g_interval` — NO getenv), `aiml_memdiff_record/forget/dump`,
  `aiml_sys_mem_diff`; live map `ptr → {size, site}`, aggregate `site-hash → {frames, live_count,
  live_bytes, last_dump_bytes}`; one mutex; a thread-local recursion guard; opportunistic activity-driven
  periodic dump (checks elapsed every ~2048 records) + `atexit`. `backtrace`/`backtrace_symbols` are
  `#ifndef __EMSCRIPTEN__` (WASM tracks totals without frames).
- `compiler.cpp` prologue — emits `aiml_memdiff_init_baked(fileStr, interval)` iff `getenv("AIML_MEM_DIFF")`
  is truthy at compile time (mirrors the `aiml_trace_init_baked` bake); `jitRun()` `AddSymbol`s it for JIT.
- `src/sys/string_runtime.cpp` — `aiml_str_alloc` → `record`, `aiml_str_decref`(on free) → `forget`
  (both a single `g_on` branch when off).
- `src/core/builtin_registry.cpp` — `sys.mem_diff()` → `aiml_sys_mem_diff` (dump + total live bytes).
- Pure diagnostic; changes no semantics. Independent of `AIML_LIFETIME_DROPS` (but most useful with drops
  ON, where a still-growing site is a real leak rather than the drops-OFF leak-everything baseline).

## Diagnostic updates (2026-09-12)

`AIML_TRACE` formats source-typed arguments through `aiml_trace_format` directly
using stdio, without allocating AIML strings. Only source `string` arguments
are read as text (null prints `<null>`). Other pointers print addresses; interface
fat pointers print object/vtable addresses. Integer varargs use the required
promotions, including signed i16; f32 promotes to double. Existing `aiml_trace`
remains available for previously generated code. Rebuild the compiler and native /
WASM runtime libraries together before rebuilding applications.

`AIML_TRACE_SYMBOL` additionally emits `MOVE`, `BEFORE_SET` and `SET`. `MOVE`
records removal of a registered local cleanup at an ownership handoff; it is not
a destructor event. `BEFORE_SET` observes the target before RHS evaluation;
`SET` observes it after assignment. Neither proves that old heap storage was freed.
Selections now also cover model fields on `set`, either by exact target spelling
(e.g. `<.library`) or final field name (`library`). Direct runtime mutation
(e.g. `get_copy_at`, `decode_rpc`, `list.push`) does not produce SET events.

Events include the function/event location and, for local declarations, their
declaration location. DECL reports `cleanup=registered` or `cleanup=unregistered`;
unregistered may mean borrowed, static, moved ownership elsewhere, or disabled
lifetime drops, and is not proof of a leak. Strings print slot and data addresses
plus at most 256 content bytes (null-safe); lists print slot and buffer addresses.
A corrupt non-null string can still fail during content inspection.

A missing DROP alone does not prove a move: use MOVE events, cleanup registration,
and the receiving owner. Field replacement and native-handle cleanup still need
allocator/runtime evidence to establish a complete lifetime balance.

## Regression checks

After activating `exvenv` and running `exodus build --all`, run
`python3 tests/run_trace_tests.py --wasm`. It checks native and WASM execution
with lifetime drops on/off, disabled tracing, null/object/interface arguments,
signed numeric formatting, dynamic string preservation, field SET events,
box-to-interface MOVE/DROP address continuity, and 20,000 traced calls.
For allocator evidence, compare `tests/test_trace_stress.aiml` under Valgrind
with `AIML_TRACE=trace_probe` versus an empty selection; compiler/JIT baseline
allocations must not be attributed to generated trace code.

The regression `test_trace_string_len_first.aiml` exercises string length before
any concatenation, with tracing on/off. Removing trace string concatenation must
not expose declaration-order-dependent libc ABI errors: `strlen` returns target
`size_t` (i32 on wasm32, i64 on x86_64), independently of which builtin declares it.
