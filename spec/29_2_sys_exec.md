# sys.exec — Runtime Execution of Code Objects

## 1. Overview

`sys.exec` executes a `Code` object (produced by `sys.bundle`) at runtime.
The `context` field of the code object controls whether a shared or isolated
execution context is used.

```aiml
- decl: [pkg, Code]
- set: [pkg, { call: [sys.bundle, "worker_task"] }]

- decl: [rc, i32]
- set: [rc, { call: [sys.exec, pkg] }]
- print: ["exit code: ${rc}"]
```

## 2. Code Struct — Extension with `context` Field

The `__aiml_code_t` struct receives a fifth field:

```
__aiml_code_t (5 fields, all aligned to 64-bit boundaries):
  [0] i8*  id           — FNV-1a hex hash of all code_chunks
  [1] i8*  start_call   — serialized YAML call node (JSON string)
  [2] i8*  code_chunks  — pointer to GenericList<i8*> (AIML definitions)
  [3] i8*  data_chunks  — pointer to GenericList<i8*> (AIML state init)
  [4] i8   new_context  — 0 = shared context (default), 1 = isolated context
```

`sys.bundle` sets `new_context = 0` by default.
The user can set it afterwards (in the future via `pkg.set_context(true)`).

## 3. Execution Modes

### 3.1 `context = false` — Shared Context (default, hot-reload capable)

**Only available in JIT mode.** In AOT mode, sys.exec always returns -2
(no active JIT context — see section 11).

The bundle is loaded into the **running** JIT session:

```
sys.exec(pkg)  [context=false, JIT mode]
  → C runtime: aiml_exec_code(void* code_obj)
      ↓
  1. Check g_active_context / g_active_ee (nullptr → -2)
  2. For each code_chunk (YAML string):
       Parse → AST nodes
       For each declaration:
         a. Name already in ctx.functions/ctx.classes?
            → Version check:
              bundle.version <= ctx.version  → SKIP (already up to date)
              bundle.version >  ctx.version  → HOT-RELOAD (recompile)
         b. Not present → compile into new llvm::Module
  3. Link new module into the same ExecutionEngine:
       ee->addModule(std::move(newModule))
       ee->finalizeObject()
  4. Compile + execute data_chunks as statements
  5. Execute start_call as statement (if not empty):
       getFunctionAddress(funcName) → call directly as function pointer
       (no new llvm::Module needed for already compiled functions)
  6. Return: i32 exit code
```

**Why a new module, but the same LLVMContext?**

MCJIT finalizes modules on first execution. New functions must be compiled into a
separate `llvm::Module`, but can share the same `LLVMContext`
and `ExecutionEngine` — this makes host functions directly callable
and struct types compatible.

**Symbol table lookup:**

| Lookup table | Check condition |
|--------------|-----------------|
| `ctx.functions` | name + version |
| `ctx.classes` | name + version |
| `ctx.enums` | name (enums have no version) |
| `ctx.interfaceSources` | name (interfaces have no version) |

### 3.2 `context = true` — Isolated Context

**Available in JIT mode and AOT mode** (later phase).

New `LLVMContext` + `Module` + `Compiler` + own `ExecutionEngine`.
No connection to the host's symbol table or module. Always clean, no name conflicts.
Useful for code that must not disturb the host's symbol table — a fresh name space, not a
security boundary. **It is NOT a sandbox**, and the earlier wording ("complete sandboxing") was
wrong: the isolated context shares the process, the address space and the dynamic symbol table
with its host, so raw pointer arithmetic, `extern "C"` declarations and `sys.*` reach everything
the host can reach. `DynamicLibrary::AddSymbol` has no per-context view, and
`LoadLibraryPermanently(nullptr)` resolves against the whole process. For untrusted code the
boundary is a separate PROCESS; see spec/65 F65-15 and `doc/analysis/symbol_clearance_2026-10-16.md`.

```
sys.exec(pkg)  [context=true, JIT or AOT]
  → aiml_exec_code():
      Create new compiler (fresh LLVMContext + Module)
      Compile code_chunks completely (no skip logic needed)
      Compile + execute data_chunks + start_call
      Return exit code
```

## 4. Version Check and Hot-Reload

### 4.1 Version Sources

Classes and functions in AIML have optional version fields:

```aiml
- class:
    name: Payload
    version: 2        # default: 1
    members:
      - [id, i32]

- def:
    name: worker_task
    version: 3        # default: 1
    args: [[p, Payload]]
    body: ...
```

`FunctionInfo::version` and `ClassInfo::version` in `context.h` already exist.
In `visitFunction`, `funcInfo.version = func->version` must be set (currently
always 0).

### 4.2 Skip Logic

```
bundle_version := from parsed YAML node
current_version := ctx.functions[name].version  (or ctx.classes[name].version)

if bundle_version <= current_version:
    SKIP — definition already present and up to date
elif bundle_version > current_version:
    HOT-RELOAD — recompile (initially: skip + warning; later: real reload)
```

**Caution with foreign code (spec/65) — IMPLEMENTED since 2026-10-17, not just noted:** at a
trust boundary neither branch is admissible as written above, and `tryHotReload`
in `exec_impl.cpp` now checks in this order:

1. the session's `replace_level` — the version comes from whoever wrote the bundle,
   so it is **input**, not an authorisation (spec/65 F65-29).  If the right is missing:
   `clearance-replace`, and the run aborts with `-3`.
2. only then the version comparison.  In a restricted session the SKIP branch is an
   **error** (`clearance-collision`, spec/65 F65-30), not a skip: a script that defines its
   own `helper` would otherwise silently get the host's `helper`.

For code from the same source — i.e. without a clearance set — the logic above
remains valid unchanged; the checks hinge on `restricted()`.

### 4.3 Hot-Reload (Implementation — later phase)

**In JIT mode:**
- Compile new function into a fresh LLVM module
- `ee->addModule(newModule)` + `ee->finalizeObject()`
- Update old symbol entry in `ctx.functions[name]`
- `getFunctionAddress(name)` now returns the new version
- Struct layout changes of classes: error (breaking change)

**In AOT mode** (see section 11.3):
Natively compiled code is hot-reloadable in principle — it is just RAM bytes.
The prerequisite is a **call table indirection**: instead of `call foo`, the AOT
compiler emits `call [__aiml_fptable + foo_idx]`. A hot-reload then only patches
the table pointer, and all callers automatically see the new implementation.
Currently AIML AOT emits direct call instructions (no indirection layer),
so AOT hot-reload is not yet supported.

For the initial implementation (JIT and AOT):
- Same or older version → SKIP
- Newer version → SKIP + `LOG_WARNING("hot-reload not yet supported: " + name)`

**Two requirements from spec/65 for whoever actually builds §4.3** (finding 2026-10-17): today
a bundle cannot carry a FOREIGN name — `sys.bundle` takes the name of a function the
host compiled itself (§16) — and so the "new symbol" path is unreachable.  As soon as
it becomes reachable, the following applies:

- Creating needs `create_level` (spec/65 F65-26).  The guard `clearance-create` is
  already in `tryHotReload` and fails closed; it is only waiting for this path.
- The level of a created symbol IS the session's clearance (spec/65 F65-27) — not 0
  and not a value chosen by the incoming source.  This chapter is the place where
  the level is set, so it has to happen here; spec/65 cannot add it
  afterwards.  The corresponding probes are `tests/clearance/11_create_allowed.aiml` and
  `neg/n20_create_without_level.aiml` (`# NEEDS: dynload`).

## 5. Global Context Pointer

`aiml_exec_code` is an `extern "C"` function and cannot directly access
`CompilerContext`. Solution: static pointer in `exec_impl.cpp`:

```cpp
// exec_impl.cpp
static CompilerContext* g_active_context = nullptr;
static llvm::ExecutionEngine* g_active_ee = nullptr;

extern "C" void aiml_set_active_context(void* ctx, void* ee) {
    g_active_context = (CompilerContext*)ctx;
    g_active_ee = (llvm::ExecutionEngine*)ee;
}

extern "C" int aiml_exec_code(void* code_obj) { ... }
```

In `compiler.cpp`, directly before JIT execution:
```cpp
// Before ee->runFunction(mainFn, ...):
aiml_set_active_context(&ctx, ee.get());
// After execution:
aiml_set_active_context(nullptr, nullptr);
```

## 6. Return Value

`sys.exec` returns `i32`:

| Value | Meaning |
|-------|---------|
| `0` | Success (or no start_call) |
| `-1` | Parse/compile error |
| `-2` | No active context (context=false, but not in JIT mode) |
| `n > 0` | Exit code of the executed bundle |

## 7. Syntax Overview

```aiml
# Default: shared context, no start_call
- set: [rc, { call: [sys.exec, pkg] }]

# The context belongs to the BUNDLE, not to the execution: it describes HOW it is compiled.
- decl: [pkg, Code, { call: [sys.bundle, "user_script", { context: true }] }]

# Arguments 2..4 are the session's POLICY (spec/65 F65-26/F65-36), in this
# order: clearance, create_level, replace_level.  It describes WHO executes, and
# therefore belongs here and not on the bundle.  Missing trailing values are 0 = nobody.
- set: [rc, { call: [sys.exec, pkg, 7] }]         # clearance 7, may not write anything
- set: [rc, { call: [sys.exec, pkg, 7, 7, 0] }]   # ... may create, not replace
```

(An earlier draft of this section promised a second argument `true` for the
ISOLATED context.  That is outdated: `context` is a property of the code
and is set on `sys.bundle`; the second position carries the clearance.)

## 8. Implementation Plan

### Phase 1: Code Struct Extension

**`code_impl.h/cpp`:**
- Field enum: add `NEW_CONTEXT = 4`
- `getStructType()`: add 5th field `i8`
- `emitNewFn()`: add 5th parameter `i8 new_context`
- `create()`: 5th parameter + `setContext()` helper function
- `emitExecFn()`: emits call to `aiml_exec_code(i8*) → i32`

**`code_runtime.cpp`:**
- `AimlCode` struct: add `int8_t new_context` field

**`ast_compiler_call.cpp`:**
- `sys.bundle` calls: set `new_context = 0` in `CodeImpl::create()`

### Phase 2: exec_impl.cpp

New file with:
- `g_active_context`, `g_active_ee` (static)
- `aiml_set_active_context(void*, void*)`
- `aiml_exec_code(void*)` — main implementation:
  - context=false: shared context path (with skip logic)
  - context=true: isolated path

### Phase 3: Compiler Integration

**`compiler.cpp`:**
- `aiml_set_active_context(&ctx, ee.get())` before JIT execution
- `aiml_set_active_context(nullptr, nullptr)` afterwards
- New function `compileYamlNode(YAML::Node, CompilerContext&)` for exec_impl.cpp

**`ast_compiler_call.cpp`:**
- `sys.exec` intercept (before eager eval)
- `visitSysExecCall()`: validate argument (Code type), call `CodeImpl::exec(ctx, ptr)`

**`ast_compiler.h`:**
- Declare `visitSysExecCall`

### Phase 4: Tests

**`tests/test_exec.aiml`:**

```aiml
# Basic sys.exec test

- class:
    name: ExecMsg
    members:
      - [value, i32]

- def:
    name: exec_worker
    args: [[m, ExecMsg]]
    body:
      - print: ["exec_worker: value=${m.value}"]

# Function bundle + exec
- decl: [pkg, Code]
- set: [pkg, { call: [sys.bundle, "exec_worker"] }]

- decl: [rc, i32]
- set: [rc, { call: [sys.exec, pkg] }]
- print: ["exec (no start_call) rc: ${rc}"]

# Instance bundle + exec
- decl: [msg, ExecMsg]
- set: [msg.value, 42]
- decl: [inst, Code]
- set: [inst, { call: [sys.bundle, msg] }]
- set: [inst, { call: [inst.merge, pkg] }]
- set: [rc, { call: [sys.exec, inst] }]
- print: ["exec (with instance) rc: ${rc}"]
```

## 9. Distinction: sys.exec vs. sys.load_bundle

| | `sys.exec` | `sys.load_bundle` (future) |
|--|-----------|----------------------------|
| Context | Shared or isolated | Always shared |
| start_call | Is executed | Is ignored |
| data_chunks | Are executed | Are ignored |
| Purpose | Execute code | Load definitions |
| JIT-only | No (AOT via libaiml) | Yes (ORC JIT required) |

`sys.load_bundle` requires ORC JIT (incrementally adding to a running module)
and is deferred for now.

## 10. Known Limitations (initial implementation)

- Hot-reload: SKIP + warning (no real redirection)
- AOT mode: `context=false` returns -2; `context=true` not yet implemented
- `context = true` (isolated mode): second phase
- `data_chunks` execution: not yet implemented (LOG_WARNING)
- Dynamic loading of new definitions (not in symbol table): not yet implemented
- start_call: only 0-argument functions supported; functions with arguments are skipped
- Struct layout changes during hot-reload: not supported (breaking change)
- `sys.load_bundle`: deferred (needs ORC JIT)

## 11. JIT Mode vs. AOT Mode

### JIT Mode (`aiml -r file.aiml`)

The AIML program is executed via MCJIT through `AIMLCompiler::jitRun()`.
Directly before `engine->runFunction(mainFn, args)`, the global pointers are set:

```cpp
// compiler.cpp, jitRun():
aiml_set_active_context(compiler_ctx_, engine);
engine->runFunction(mainFn, args);
aiml_set_active_context(nullptr, nullptr);
```

This makes both `g_active_context` (symbol table, LLVMContext) and `g_active_ee`
(compiled code, getFunctionAddress) available inside `aiml_exec_code`.

| sys.exec call | JIT mode | Result |
|---|---|---|
| `context=false` | ✓ | g_active_context + g_active_ee available |
| `context=true` | ✓ (Phase 2) | Fresh isolated JIT |

**start_call (context=false):**
Functions already compiled in the main module are directly callable via
`getFunctionAddress(name)` — no recompilation needed. This is the typical case
when a bundle from the same file is executed.

### AOT Mode (`exodus build` → Binary)

The AIML program is compiled to native machine code via the exodus build system:
`aiml source.aiml -o source.o` produces object files, exodus links
them with `libaiml.so` and all libraries from `exodus.json`.
**Standalone `aiml file.aiml` (without `-r` or `-o`) only produces LLVM IR
(`.ll`) — no binary.** AOT binaries require `exodus build`.

At runtime there is neither a `jitRun()` phase nor an `ExecutionEngine`.
`aiml_set_active_context` is never called:

```
g_active_context = nullptr
g_active_ee      = nullptr
```

| sys.exec call | AOT mode | Result |
|---|---|---|
| `context=false` | ✗ | returns -2 (no active JIT context) |
| `context=true` | ✓ (Phase 2) | start fresh JIT internally via libaiml |

**context=false is fundamentally not useful in AOT mode:** There is no running
LLVM ExecutionEngine to share. New definitions from the bundle would have to be
JIT-compiled from scratch — which is then effectively identical to `context=true`.

**Recommendation for AOT:** Instead of `context=false` (default), explicitly set
`context=true` when sys.exec is used in an AOT-compiled application.

### 11.3 Hot-Reload in AOT Mode

> **"Is AOT hot-reload fundamentally impossible?"**
> No. A compiled binary is just machine code in RAM — and RAM bytes
> can be replaced at runtime. The current limitation is **not**
> the nature of native code, but the absence of an indirection layer.

**Why it currently does not work:**

AIML AOT emits direct call instructions:

```
; AOT-generated LLVM IR:
call i32 @worker_task()   ; direct link — address fixed at link time
```

The linker resolves `worker_task` to a fixed address. After startup there is
no location that could be swapped without patching all call sites.

**How it could work — call table (PLT principle):**

```cpp
// At compile time the AOT compiler emits a global pointer table:
void* __aiml_fptable[] = {
    (void*)worker_task,   // index 0
    (void*)other_func,    // index 1
    ...
};

// Every function call goes through the table entry:
; LLVM IR with indirection:
%fp = load i8*, i8** getelementptr([__aiml_fptable, 0, 0])
call i32 %fp()
```

A hot-reload then only patches a single pointer in the table:
```cpp
__aiml_fptable[0] = (void*)worker_task_v2;  // atomic; all callers see v2
```

No patching of call sites, no `mprotect`, no trampolines needed.

**What also does not work with a call table:**
- Struct layout changes: already allocated instances have the old layout
  on the heap; new code with a different offset reads wrong bytes → breaking change

**Current status:**
AIML AOT does not emit `__aiml_fptable`. Hot-reload is therefore not available
in AOT mode. The call table indirection is a known future feature.

### Why `context=false` is still the default

In JIT mode (the most common development case), shared context is the most
capable option: already defined types and functions are not compiled twice,
hot-reload is possible, and struct types are compatible.
An AOT binary that needs sys.exec must explicitly set `context=true`
or is specifically built for JIT operation via `libaiml`.

### `libaiml` (future)

A shared library `libaiml.so` could provide an embedded AIML JIT
that AOT binaries use at runtime.
This would make `context=false` work in AOT mode as well:
`libaiml` provides a persistent `CompilerContext` + `ExecutionEngine`
into which sys.exec can load new modules.

## 12. Isolated context in an AOT binary — it works

**Correction.**  §3.1 and §11 read as if `sys.exec` could not be used in an AOT binary,
and §3.2 lists the isolated context as "Phase 2".  Both are outdated:
`aiml_isolated_exec` is implemented and does **not** depend on `g_active_context`.

Verified with an `exodus build` binary (no JIT):

```yaml
- decl: [pkg, Code, { call: [sys.bundle, "worker_task", { context: true }] }]
- decl: [rcx, i32, { call: [sys.exec, pkg] }]      # rc=0, worker_task runs
```

```
aiml_isolated_exec: compiled source (160 bytes)
   worker_task laeuft im isolierten Kontext
aiml_isolated_exec: finished with rc=0
```

The sequence: `sys.bundle(name, { context: true })` sets `new_context = 1`, `aiml_exec_code`
branches straight to `aiml_isolated_exec`, which builds YAML from the `code_chunks`, creates a fresh
`AIMLCompiler`, calls `compileAndRun`, restores the caller's globals and, on leaving the
scope, frees the LLVMContext, module and CompilerContext.

The prerequisite is that the compiler is part of the runtime library — and it is:
`libaiml.so` contains LLVM and exports `aiml_exec_code` as well as `aiml_set_active_context`.

**What still does NOT work:** `context = false` in AOT.  There `sys.exec` looks for the shared
JIT context, finds none and returns `-2` (§11).  That is correct — hot reload into the
running session presupposes a session.

**What remains to be measured:** the lifetime.  Every isolated run builds up and tears down a complete
LLVM context.  That nothing is left behind is plausible (the destructor
cleans up), but for an agent that executes thousands of steps it should be measured, not
assumed.

### Finding 2026-10-16: the object cache has no version key in the AOT case

`aiml_isolated_exec` takes over the compiler path only if there is an active context:

```cpp
if (g_active_context) iso.compilerPath = g_active_context->compilerPath;
```

In the AOT binary `g_active_context` is always `nullptr` (§11), so the sub-compiler gets **no**
`compilerPath`.  But this path is the source of the cache version key:
`compilerBinaryRev()` reads size and mtime of the compiler file and returns
`"unknown"` for an empty path, whereupon `cacheRevDir()` ends up in `rev_unknown`.

**Consequence:** compiled objects of an isolated run in the AOT binary are stored under a key
that **does not distinguish compiler versions**.  After a `libaiml` update, stale code can
be reused — exactly the class of bug that has already been expensive twice in this project
(stale KV blobs, stale `__aiml_cache`).

Nobody has noticed so far, because `context = true` is practically unused in AOT.
For a script console in an AOT host (spec/65) it is a stumbling block, because there the
isolated context is the **only** available path.

**Fixed on 2026-10-17.**  The key no longer depends on the compiler path but on the
file that **actually contains** the compiler: `dladdr` on a function of the
translation unit in question yields the `aiml` binary in a JIT run and
`libaiml.so` in an AOT binary; from that, `<size>_<mtime>` is formed as before.  If that yields nothing
either, the return value is **empty** instead of `"unknown"` — `computeImportCacheKey` then returns no key,
`checkCache` treats that as "no hit", and the cache **switches itself off** instead of opening a
shared bucket for all versions (fail closed, with a warning).

Measured in a JIT run: after a rebuild the cache directory is called `rev_83002216_…` and
so matches size and mtime of the new binary; the previous version's directory is
cleaned up by `sweepCache`.  The AOT branch is **not measured yet**: that would need an
AOT binary to execute an isolated-compiled bundle with an `import`, because without an import
the cache is not consulted at all — `tests/bundle_socket` (the existing AOT path) bundles
without imports and therefore never triggered the bug.  The bug was thus **latent**: no
`rev_unknown` ever came into being in the tree.


## 13. `sys.code_from` — building a bundle from source text

`pkg.toString()` concatenates `code_chunks` + `data_chunks` into **aiml source text**; `start_call` and
`new_context` are **not** included.  That is the wire form in which a bundle travels over a
connection (cosmos `doc/spec/96`: the controller sends the step to the agent).  Until
now the opposite direction was missing: a receiver could not turn the text back into a `Code`.

```
sys.code_from(src: string [, { start: "<fn>", context: true }]) → Code
```

| Option | Default | Meaning |
|---|---|---|
| `start` | `""` | function called after compilation (becomes `start_call`) |
| `context` | `false` | `true` = isolated context — **the only thing that works in an AOT binary** (§11) |

```yaml
# Sender
- decl: [pkg, Code, { call: [sys.bundle, "worker_task"] }]
- decl: [wire, string, { call: [pkg.toString] }]

# Receiver
- decl: [job, Code, { call: [sys.code_from, wire, { start: "worker_task", context: true }] }]
- decl: [rc, i32, { call: [sys.exec, job] }]
```

The options deliberately mirror `sys.bundle`: `context` means the same here as there, and a
receiver running in an AOT binary must set `context: true` — otherwise `sys.exec` looks for
an active JIT context that does not exist there and returns `-2`.

**What does NOT travel along:** the sender's state.  The text contains definitions and
data initialisation, not the sender's heap.  A bundle is code, not a process image.

## 14. Loading a library and running pipelines against it

Verified with `tests/bundle_socket/` (two **AOT** binaries, bundle over TCP):

```
2) library: 110 bytes   pipeline: 230 bytes
   lib_greet from the library
   run_pipeline called lib_greet, got 7     → rc=0
```

The way to do it is `Code.merge`: library and pipeline are merged into **one** bundle and executed in
**one** `sys.exec`.  The order matters — `start_call` is inherited from the **first**
operand, so the pipeline must be on the left (`pipe.merge(lib)`), otherwise the library
is started.

**What does NOT work this way, and this is the real limitation:** loading a library *once*
and then running several pipelines against it.  Every `sys.exec` with `context: true` creates
a **fresh** `AIMLCompiler`, compiles, runs and tears it down again on leaving the scope
— nothing remains between two calls.  `sys.load_bundle`, which would be meant for exactly
this, is deferred according to §9.

Consequence: an agent that runs 500 steps against the same library compiles it **500 times along with them**.
Functionally correct, but for a large library the dominant cost.

### 14a `ctx` — the context as a namespace of its own (decision 2026-10-25)

User requirement: *"We would rather create a new namespace that we call `ctx`.  And from
this space we can, so to speak, obtain a compiler instance, execute code in it and also access
the symbols of that execution."*

```yaml
- decl: [cx, ctx_handle, { call: [ctx.new] }]          # one compiler instance
- decl: [n_lib, i32, { call: [cx.exec, lib_bundle] }]  # library ONCE
- decl: [r_a,   i32, { call: [cx.exec, pipe_a] }]      # sees the library
- decl: [r_b,   i32, { call: [cx.exec, pipe_b] }]      # so does this one
- decl: [zahl,  i32, { call: [cx.symbol, "zaehler", "i32"] }]       # value, WITH type
- decl: [adr,   i64, { call: [cx.symbol, "lib_greet", "fn(string) -> i32"] }]  # function: WITH signature
```

- **F29_2-…a: `ctx` is a NAMESPACE, not a second `sys`.**  `sys.exec` stays what it is: execute a
  bundle once.  `ctx.*` is the case in which the context **lives longer than the
  call** — and that is a different circumstance, not an extra argument.  Putting both into `sys`
  produces one function with a switch that changes the lifetime; that is the kind of switch
  one can never get rid of later.
- **F29_2-…b: The context is a UNIQUE RESOURCE.**  The same kind of value as `process` or
  `thread_handle` (spec/61 §2.1): move-only, and when its owner is dropped the engine is
  released.  Not copyable — two owners of a JIT engine would be two that free it.
- **F29_2-…c: The reason is the base cost, not elegance.**  Measured (§15): **~96 ms** per
  isolated run — LLVMContext, module, entry point, engine — regardless of how little it
  does.  For an agent with 500 steps that is ~50 seconds before a single command
  runs.  `ctx` turns this into **one** setup and 500 executions.  That is the whole purpose;
  if the base cost were 1 ms, `ctx` would not be needed.
- **F29_2-…d: When reading a symbol, the type is NAMED — and checked at runtime.**
  (User requirement, 2026-10-25.)  `cx.symbol(name, type)` takes the expected type as the
  second argument: `"i32"`, `"string"`, a model name, or — for a function — its
  **full signature** (F29_2-…d3).  The check is **always done at runtime**: the context compiled the symbol itself
  and knows its type; if it does not match the named one, **an exception is thrown**
  (spec/22).  Only then — a matching symbol comes back without comment.
  This removes the earlier restriction "no values": a value behind an address has no
  meaning without a type, but with a named **and checked** type it has one.  The reader says
  what they expect and finds out when they are wrong — instead of misreading bytes.
- **F29_2-…d4: Where the compiler ALREADY KNOWS, it says so immediately.**  (User addendum:
  *"the compiler should know the data type and can compare whether what was
  programmed and what was just compiled match."*)  Right — but only in one
  of the two cases, and the difference is WHEN compilation happens:

  | Who reads the symbol | What the compiler knows at that moment | Check |
  |---|---|---|
  | A **bundle in the same context**, compiled **after** the defining bundle ran | both: the named type and the symbol's type in its table | **at compile time** — error immediately, no run |
  | The **host program** (built AOT, e.g. cosmosd) | only the named type; the context does not exist yet at this point | **only at runtime** |

  So: the context checks what it can when compiling a bundle, and otherwise throws
  on access.  The runtime check remains **mandatory** — it is the only one that exists in both
  cases; the early one is the gain, not the replacement.  A guarantee that only applies
  sometimes must not be the one people rely on.
- **F29_2-…d2: Two errors, two exceptions.**  `SymbolMissingException` — the context does not know
  this name (nothing defined it, or a typo).  `SymbolTypeException` —
  the name exists, the type does not match; the message names **both** types (expected / found),
  because a "type error" without the two names forces a search.  Both inherit from `Exception` and
  can be caught with `catch` like any other.
- **F29_2-…d3: For a function, the FULL SIGNATURE is named.**  (User requirement,
  2026-10-25: *"Even when retrieving function symbols, please give the complete
  signature."*)  Not `"fn"` but `"fn(i32,string) -> i32"`: the argument types in their
  order, then the return type — `void` if there is none, `fn() -> i32` without arguments.
  The context compiled the function itself and knows both (`FunctionInfo::argTypeNames`
  and `returnTypeName`), so both are compared; whitespace does not count, so that
  `"fn(i32, string) -> i32"` and `"fn(i32,string)->i32"` say the same thing.

  The reason is that the opposite of the usual situation applies here: a value with a wrongly named
  type is misread, but calling an **address** with the wrong signature produces no
  error at all — it produces a broken stack, somewhere later.  A type name that
  checks nothing at exactly the most dangerous spot is worse than none, because it reassures.
  A bare `"fn"` is therefore a type error like any other (`SymbolTypeException`), and
  the message says explicitly that the signature is missing.

  What the signature does **not** name: the lifetime qualifiers of the arguments
  (`consumed`/`persistent`, spec/61 §3.1).  They are in the same table and would be
  checkable, but they belong to the call **contract** and not to the type; since almost every argument carries the
  implicit `scope`, every signature would grow by noise that
  distinguishes nothing in almost all cases.  If a `consumed` argument is called through a
  `ctx` address, the ownership transfer therefore remains the caller's responsibility.
- **F29_2-…e: `ctx` is NOT a security boundary.**  The context shares process, address space and the
  dynamic symbol table with its host; `DynamicLibrary::AddSymbol` has no per-context
  view.  For foreign code the boundary remains a **separate process** (spec/65 F65-15,
  `doc/analysis/symbol_clearance_2026-10-16.md`).  `ctx` separates **names**, not **rights** — whoever
  confuses the two mistakes a convenience for a protection.
- **F29_2-…f: What the context sees is the order of its calls.**  Bundle 2 sees what
  bundle 1 defined; nothing more.  No backward view, no implicit rebuild.  This makes
  `ctx.exec` traceable like a list of statements — and not like a state that
  came about somehow at some point.

- **F29_2-…g: The context keeps its OWN type table — because the compiler throws its own away.**
  `compileFromString` destroys and rebuilds the `CompilerContext` on **every** call; the live
  table therefore only knows what the **last** bundle compiled or declared.  Measured
  (2026-10-25): after three bundles, `lib_summe` was "unknown to this context", although its bundle
  had run with 0 — only what the third bundle happened to declare as well remained visible.  That
  contradicts F29_2-…f, and not visibly but **randomly**: the same query works
  or does not, depending on what ran afterwards.  Therefore `aiml_ctx_record_symbols` writes
  name → type into a table ON THE CONTEXT after **every** `ctx.exec` (later definitions win,
  §4 reloading), and `cx.symbol` reads from there first.  The address is still fetched from the engine
  (`symbolAddress`, newest first) — a name in the table without an address means "declared, never
  defined" and is reported as a **missing** symbol, not as a type error.
- **F29_2-…h: A throw raised by the compiler records its message like a `throw` in aiml.**
  The top level (`jitRun`) reports the reason of an uncaught exception from
  `aiml_note_thrown_message`; `emitThrowMsg` did not call it, so it said "(no message)" —
  for **every** built-in exception, not only the ones from `ctx`.  Fixed; `tests/test_ctx_unhandled.aiml`
  pins it down (the run ends with 2 **and** names expected/found).

**Implementation status** (2026-10-25 — implemented and covered by `tests/test_ctx.aiml`):

| Piece | Status |
|---|---|
| `AIMLCompiler` reusable | **done** — `execInContext` (`compiler.cpp`): rebind the module, prepend earlier sources as declarations, keep the engine (`ctx_engines_`), publish function addresses via `AddSymbol` |
| Context registry + value type | **done** — `src/sys/ctx_runtime.cpp` holds the instances, `ctx_handle` is the handle |
| `ctx.*` in the builtin registry | **done** — `ctx.new`, `cx.exec`, `cx.symbol`, `cx.close` (`src/core/builtin_registry.cpp`) |
| `cx.symbol` | **done** — type table per context (F29_2-…g), check including `SymbolMissingException`/`SymbolTypeException`, functions with **full signature** (F29_2-…d3) |
| `ctx_handle` as a unique resource | **open** — move-only and release on the owner's drop (F29_2-…b) are still missing; today only `cx.close` releases the engine |
| Early check at compile time (F29_2-…d4) | **open** — today only the runtime checks.  That is the mandatory check and covers both cases; the early one remains the gain, not the replacement, and is listed here as missing instead of promised |
| Calling a `fn` address from aiml | **open** — `cx.symbol` returns the address, aiml has no way to call it yet; with F29_2-…d3 the signature for doing so is now available and checked |

**How cosmos would use it:** one context **per agent**, the library loaded into it at start,
then every step as a bundle against it.  That is exactly the sentence that already stands as a comment
in `exec_impl.cpp` — "an agent loads its library at start and then runs steps"
— except that until now there was no value for the context to hang on.

### Open: reusable context as an aiml value  *(superseded by §14a)*

The draft that became §14a — left here because it carries the reasoning.  The way out would be
to no longer keep the compiler instance local, but to make it visible as a **unique resource**
— the same kind of value as `process` or `thread_handle` (spec/61
§2.1): move-only, released on the owner's drop.

```yaml
- decl: [ectx, exec_context, { call: [sys.context_new] }]
- decl: [r_lib,  i32, { call: [ectx.exec, lib_bundle] }]   # once
- decl: [r_one,  i32, { call: [ectx.exec, pipe_a] }]       # sees the library
- decl: [r_two,  i32, { call: [ectx.exec, pipe_b] }]
```

The obstacle is not visibility but that `AIMLCompiler` is **single-use** today:
`compileAndRun` calls `jitRun`, and that does `EngineBuilder eb(std::move(module_))` — the module
is gone afterwards; a second run would have nothing left to compile into.  Two designs:

1. **One engine, a fresh module per bundle**, `addModule` into it; the `CompilerContext` stays
   in place, so that bundle 2 sees the symbols of bundle 1.  That is what "loading a library" means.
2. **A separate engine per bundle**, keep the addresses and make them reachable through the fptable —
   present in outline (`compileChunkAndGetAddress` plus the pointer swap of the hot-reload path).

In addition, a finding regarding §9: the deferral of `sys.load_bundle` is justified there with "needs ORC JIT".
But the existing shared path does not use `addModule` at all; it compiles the
chunk into a **separate** engine and swaps a pointer.  For design 2 the justification
therefore does not apply; for design 1 it would have to be checked against MCJIT instead of being taken over from the spec.

### Design 3 (probably the right one): object cache instead of compiling

The question "lazy or eager" is the wrong axis for a build agent.  Both answers
compile the same library again and again; they only shift **when** it hurts.

LLVM has `llvm/ExecutionEngine/ObjectCache.h` for this — usable from MCJIT (`setObjectCache`) as well as from ORC:
the compiled **object** is kept and next time **loaded instead of
compiled**, even across process boundaries.

The key for it already exists: the bundle's hash (`AimlCode.id`, FNV-1a over all
`code_chunks`).  Same bundle → same object.  This is the same mechanism as the
step cache of a pipeline (cosmos `doc/spec/96`), only one level deeper, and it is subject to
the same rule: the key must capture **everything** that influences the result — here that means
target architecture and LLVM version too, otherwise a node loads an object that does not fit it.

This turns "500 steps, compiling the library 500 times" into compiling once and
loading 499 times — **without** needing a reusable context (design 1/2) for it.
The two designs still make sense, but for a different purpose: they save the
loading **and** allow bundle 2 to see the symbols of bundle 1.

### On classifying "lazy"

ORC's laziness is a **separate layer**, not a property: `CompileOnDemandLayer` and
`LazyReexports` have to be added.  Without them `IRCompileLayer` compiles at **symbol lookup**,
and a `lookup()` right after adding forces this immediately.  "Only on first call" is
therefore a choice, not imposed — and MCJIT, which always compiles everything immediately and keeps nothing,
is the more expensive variant for repeated runs.

### 14b What a host can do with a context: list, call, read errors (2026-10-25)

§14a gives a host a context, a way to put code into it and a checked address out of it.
For the case at stake — a **controller** accepts foreign source text, has it compiled
and works with the result (cosmos `doc/spec/96`) — three pieces are missing, and each missing one
forces the host back to reading the source text itself:

```yaml
- decl: [kontext, ctx_handle, { call: [ctx.new] }]
- decl: [code, Code, { call: [sys.code_from, eingereicht] }]
- decl: [rcx, i32, { call: [kontext.exec, code] }]
- if:                                         # failed: WHAT was wrong?
    condition: { lt: [rcx, 0] }
    then:
      - decl: [why, list<string>, { call: [kontext.errors] }]
      - return: { at: [why, 0] }
# What is the pipeline in it called?  The context knows — the host does not have to search.
- decl: [found, list<string>, { call: [kontext.names, "fn() -> pipeline"] }]
- decl: [pipe, pipeline, { call: [kontext.call, { at: [found, 0] }, "fn() -> pipeline"] }]
```

- **F29_2-…i: `cx.names(type)` — ask by type, do not guess by name.**  Returns the names that
  this context carries with exactly this type or signature, in table order
  (whitespace does not count, §14a F29_2-…d3); none matches → empty list, no error.  The reason
  is the controller: it accepts a **submitted** pipeline and does not know its name.  Reading it
  from the source text means reading the source text — exactly what the context
  was supposed to take over.  A host that only has `cx.symbol` has to guess or parse; with `cx.names`
  it asks.
- **F29_2-…j: `cx.call(name, signature)` calls what the context compiled.**  First the
  signature is checked (the same check as `cx.symbol`, F29_2-…d3), then the call is made; the return value
  has the type stated in the signature.  **Currently without arguments** (`fn() -> T`) with
  `T ∈ {void, i32, i64, string, built-in handle class}`: that is the case that is needed —
  a declared pipeline is `fn() -> pipeline` (spec/62 F62-108) — and every further
  parameter needs argument conversion, which without users would only be dead weight.  More arguments
  come when a case demands them; the signature will already be in place then.
- **F29_2-…k: `cx.symbol` returns the VALUE if the named type is a value.**  `i32`, `i64`,
  `string` and the built-in handle classes come back as a value, a function as an address
  (`i64`).  So the named type decides not only whether access is allowed, but also
  what one holds in hand — the other way round, the reader would have to make a value out of an address
  themselves, and that is impossible in aiml without bypassing the type system.
- **F29_2-…l: `cx.errors` belongs to the CONTEXT, not to the process.**  The messages of the **last**
  `cx.exec`, in their order; a new `cx.exec` resets them.  Two reasons: `-3` means
  "failed" and is worthless to a host that has to tell a human what was wrong —
  and two contexts must not mix their messages, otherwise one submission explains
  the error of the other.  They are reset because otherwise the third run would report the errors of the first
  as its own.
- **F29_2-…n: `cx.load` compiles and links in, WITHOUT running.**  `cx.exec` calls the
  bundle's entry point — with **submitted** source text that would be foreign code in the
  controller process, and that is exactly what cosmos F96-28 forbids.  `cx.load` does everything except the
  call: compile, collect messages, make symbols known.  Afterwards the host calls **exactly
  the one** function it wanted (`cx.call`), and nothing else.  The difference is not
  caution but responsibility: a body belongs to the agent, not to the controller.
- **F29_2-…m: What a host does NOT get.**  No AST nodes, no LLVM types, no pointer into
  the compiler's tables — only names, values, addresses and messages.  Anything else binds
  the controller to the compiler's internals, and then every change to the compiler is a
  change to the controller.  The boundary is deliberately narrow: `ctx` is an **interface**, not a
  window.

**What all this together is enough for:** a controller can have a submitted pipeline compiled,
pass on the compiler's messages, build the pipeline and read it (spec/62 §20) — without
touching a single line of YAML itself.  That is the point: the language has **one** reader,
and it lives in the compiler.

## 15. `sys.lib_load` — compiling a library once

```
sys.lib_load(pkg: Code) → i32      # number of registered functions, -1 error
```

The difference from `merge` (§14) is not visibility but **cost**: `merge`
compiles the library along with **every** run, `lib_load` compiles it **once**.

Sequence:

1. **Load** (`compileLibraryAndRegister`): compile the bundle, create an engine, for every
   function make its address known via `llvm::sys::DynamicLibrary::AddSymbol`.  The engine
   is **held on to** by the compiler — if it were released, the code would be freed and the addresses
   just registered would point into nothing.
2. **Execute** (`sys.exec`, isolated context): before the bundle, the remembered
   library source texts are fed in **headerOnlyMode** — `visitFunction` enters the signature
   in `ctx.functions` and creates the `llvm::Function`, **but skips the body**.  The
   pipeline's calls thus become external symbols, which MCJIT resolves via the registered
   addresses.

Per run, parsing and declaring the library therefore remain; code generation for its bodies
is dropped.

### Measured: the advantage cannot be demonstrated — and why

`tests/test_lib_load.aiml`, library of four functions, five runs:

| | Runs (ms) | Mean |
|---|---|---|
| `merge` (library included every time) | 98, 97, 104, 99, 101 | ~100 |
| `lib_load` (loaded once) | 93, 94, 97, 95, 104 | ~97 |

**~3 %, and in one run `lib_load` was slower.** That is noise, not an advantage.  The
claim "saves the expensive compilation" holds in the narrow reading (the bodies are not generated
again), but has no practical effect — **at this library size**.

The reason is the number that became visible along the way and that matters more than the comparison: **an
isolated run costs about 96 ms of base cost** — LLVMContext, module, entry point, engine —
regardless of how little it does.  Four library functions disappear in that.

**Consequence for cosmos:** 500 steps in isolated contexts cost ~50 seconds of pure base cost
before a single build command runs.  The lever is therefore not to compile the library less often,
but to **build the context less often** — i.e. the reusable context
(§14, design 1), for which `CompilerContext::module` is now rebindable.  `sys.lib_load` remains
useful for **visibility** (a pipeline can use a library without bringing it
along), not for speed.

### Known limit: exactly one version

`AddSymbol` writes into a **flat, process-wide** table and, according to the LLVM header, *"permanently
adds the symbol"*.  Both of the following follow from that:

- **Two versions of the same library do not work.**  One name, one entry — one version
  wins, the other disappears **without a message**.  The same class of bug as a too coarse
  cache key: it runs, and it runs wrongly.
- **Unloading does not work.**  "Permanently" is meant literally.

On top of that, `compileDeclarationsOnly` feeds **all** remembered source texts into every new compiler:
two versions declare the same name, the second overwrites
`ctx.functions[name]` — possibly with a different signature.

`sys.lib_load` is therefore explicitly the **single-version case**: an agent loads its library
at start and then runs steps.  As soon as two versions are needed side by side
(aiml `spec/62` §16, `runtime: own`), it is no longer enough, and a **symbol space per
context** is needed — either a resolver of its own per engine (staying with MCJIT) or ORC's JITDylibs.

### Prerequisite created for this

`CompilerContext::module` was a **reference** and is now a pointer with `module()` and
`setModule()` (602 access sites converted).  A reference cannot be rebound; so
a context could never serve a second module.  This is the basis for several modules in
one context — `sys.lib_load` does **not** use it yet.

### What is NOT visible in aiml

There is still **no** aiml data type for a context or a module.  `sys.lib_load` acts
on a process-wide registry; an `exec_context` as a unique resource (like `process`) would be
the next step and presupposes a symbol space per context — otherwise it would be ownership without
effect.

## 16. Finding: `sys.bundle` is NOT transitive over called functions

`spec/30_3` §297 says "(sys.bundle includes dependencies transitively)".  That holds for
**types**, not for **functions**.  `dfsCollect` (`ast_compiler_call.cpp`) follows only
`referencedTypes`; a function that **calls** another does not take it along.

```yaml
- def: { name: helper,  return: i32, body: [{ return: 7 }] }
- def: { name: worker,  return: i32, body: [ { decl: [got, i32, { call: [helper] }] }, ... ] }

- decl: [pkg, Code, { call: [sys.bundle, "worker"] }]   # does NOT contain helper
```

When executed, the isolated run then reports `Unknown function: helper` — only at runtime, in
a bundle that has long since been on its way.

**Consequence for cosmos:** a pipeline step that calls a helper function does not carry it along.  Whoever
wants a complete bundle must merge the parts explicitly (`Code.merge`, §14) or
load the library separately (§15).  Today that is manual work for the author and therefore a
source of errors that calls for a check.

**Decided: `dfsCollect` also follows calls.**  A bundle is therefore complete
by itself — that is the property everything else builds on (a step that reports `Unknown function` at the
receiver has gone over the wire without being usable).

The same is captured as for types: while a function body is being compiled,
`bundleTrackingTarget` carries the function currently being compiled, and every resolved call to a
**user-defined** function is recorded there (`referencedFuncs`).  Builtins stay
out — they live in the runtime library, not in the bundle.

The price is the flip side of completeness: a bundle can grow large if the called
function in turn branches widely.  That is acceptable because it is **visible** (`pkg.size()`)
and because the alternative — an incomplete bundle that is only noticed at the receiver — is the
worse kind of surprise.

## 17. Finding: compile errors in the bundle came back as success

`aiml_isolated_exec` called `compileAndRun`, and that compiled, generated the entry point and
started running — **without checking whether compilation had succeeded at all**.  `main.cpp` does this
check (`Logger::instance().hasErrors()`), `compileAndRun` did not.

Observed while writing `tests/test_bundle_callees.aiml`:

```
[ERROR] ... Unknown function: mid_level
sys.exec rc=0
```

That is the most dangerous kind of misbehaviour for cosmos: an agent would have reported a step as
**successful** whose code was never compiled — and the layer cache would have recorded this
"result" and delivered it on the next run.

**Fixed:** `compileAndRun` remembers the error count before compiling, compares afterwards and
returns `-3` instead of running.  The count is *compared*, not checked for zero — a
long-lived agent may already have counted earlier errors, and we are only interested in the ones from
this bundle.

**Side finding:** `Code.contains(name)` is a plain text search (`strstr` over the code_chunks).
A function that merely **calls** `mid_level` makes `contains("mid_level")` true, although
the function is not in the bundle at all.  `contains` is therefore no good as a completeness check
— only a run is.

## 18. Completeness check at bundling time

A bundle that lacks a called function is only noticed at the **receiver** — as
`Unknown function`, after it has gone over the wire.  For cosmos this means: the step
is at the agent, the agent cannot execute it, and the cause lies with the sender.

Three places would be candidates for a check, and two are no good:

| Place | Why it is no good |
|---|---|
| `Code.contains(name)` | plain text search (`strstr`).  A function that merely **calls** `helper` makes `contains("helper")` true, although `helper` is not in the bundle (§17) |
| at the receiver | too late: the bundle is already on its way, and the error looks like an error of the receiver |
| **at bundling time** | the last point at which the **author** finds out |

Implemented at the third: `dfsCollect` collects every called name for which **no source
is stored**, and `sys.bundle` reports them:

```
sys.bundle('worker'): unvollstaendig — gerufene Funktion(en) ohne hinterlegte Quelle:
helper.  Der Empfaenger meldet dafuer 'Unknown function'.
```

Two properties that are intended here:

- It is a **warning**, not an error.  A bundle can be deliberately incomplete — when the
  receiver already knows the function through a loaded library (§15).  An error would forbid this
  valid case.
- `--check` collects warnings (`spec/49`).  The check is therefore **machine-readable** and
  ends up in the findings list of a pipeline UI without extra work (cosmos `doc/spec/98`).

What it does **not** capture: a name the compiler does not know at all — that is already an
error before (`Unknown function` when compiling the body).  The check applies exactly to the case
in between: known, but without a stored source.

## 19. Quoting is meaning, not formatting

A bundle carries **source text**.  This source text is reconstructed from the parsed
YAML node — and exactly there, until 2026-10-11, the
quoting was lost: `YAML::Dump` throws away the yaml-cpp tag `!` that only
quoted scalars carry.

In aiml this is not a cosmetic flaw but a change of meaning:
`nodeToExpr` (`src/core/ast_builder.cpp`) distinguishes a **string literal**
from a **name** solely by this tag.  So

```yaml
      - call: [parts.push, "eins"]
```

became, after bundling,

```yaml
      - call: [parts.push, eins]
```

and the receiver reported `Unknown Type: 'eins'`.  **Every**
string literal in **every** bundle was affected; it was only noticed when a
pipeline step (`spec/62` §5.6) wanted to pass a `sleep` to `sys.exe`,
because the earlier bundling tests happened to contain no string literals.

- **F29_2-19** Bundle source text is generated exclusively with
  `aiml::dumpPreservingQuotes` (`src/core/yaml_dump.h`), never with
  `YAML::Dump`.  The emitter keeps quoted scalars quoted.  `YAML::Dump`
  remains correct for the **user** call `yaml.dump`, where it is about data and
  not about source text — the two cases look the same and are
  not.
- **F29_2-19a** A test that checks bundling contains at least one
  string literal **in the body**.  Without one, this entire class of errors slips
  through every check (as happened).

## 20. What a bundle collects: `new X` as well

- **F29_2-20** A class that occurs in the body only in a `new` belongs
  in the bundle.  `visitNewExpr` looks it up directly in `ctx.classes` and
  thereby bypasses `resolveType`, where the collecting is hooked in (`context.h`) — the
  most common case for this is `throw: { new: MeinFehler, message: ... }`, i.e.
  precisely the path on which a step fails (`spec/62` §5.6).  The
  receiver reported `Unknown class for new`.  Recording therefore happens in
  `visitNewExpr` itself.

## 21. `toString` serialises a sequence

- **F29_2-21** `Code.toString` returns a YAML **sequence** (`- def:` …), not
  the chunks glued together.  Every chunk is a top-level
  mapping; two glued together produce a mapping with the key
  `def:` twice, and on re-reading only the last one survives.  An aiml program
  is itself a sequence, so this is also the right serialisation.
  `exec_chunks_to_yaml` builds the same sequence — the two paths must
  deliver the same, otherwise a bundle over the wire behaves differently
  from one in the process.
- **F29_2-21a** This was only noticed when §16 (transitive collection)
  made bundles with **more than one** chunk the rule: the socket test
  sent `run_pipeline` together with `lib_greet` and lost `run_pipeline`.  With
  one chunk the bug was invisible — a test for serialisation
  therefore needs at least **two** chunks.

## 22. What a bundle sees of the host — and what it does not

The question arises immediately when a pipeline runs **in the controller**: does a step body get
at the host's symbols and state?  Measured in
`tests/test_bundle_state_boundary.aiml`, three separate answers:

| What | Reachable? | How |
|---|---|---|
| Built-in functions (`io`, `net`, `db`, `sys`, `step`, …) | **yes** | freshly registered in the isolated context |
| A **function** of the host | **yes, as a copy** | its source text is bundled transitively (§16) and recompiled |
| Global **variables** of the host | **no** | they are bundled along, but **freshly initialised** (§22 below) |
| Running state of the host (open handles, caches) | **no** | a bundle carries code, not a process image (§13) |
| Files, sockets, database | **yes, shared** | the same process, the same machine |

- **F29_2-22** A global variable touched by a bundled function belongs in the
  bundle.  Before, exactly the most common case — a helper function of the host that reads a
  counter or a configuration — broke with `Undefined variable (set target)`, and only
  at the **receiver**.  Recording happens in `resolveVariable` (the only place that treats reading,
  writing and interpolation alike) via `Symbol::is_global`; what is sent along is the
  source text of the top-level `decl`.
- **F29_2-22a** What is sent along is the **declaration**, not the value: in the bundle the
  global starts at its initial value.  Measured: the host counted to 2, in the bundle the same
  function returned `1`, and the host's counter remained untouched.  That is the right meaning — a
  bundle can run on another machine (`spec/62` §5.8), and a snapshot value copied there
  would be a silent lie about shared state.
- **F29_2-22b** **The separation is lexical, not a sandbox.**  The body runs in the **same
  process**: `io` writes into the same directories, `db` into the same database, `sys.exe`
  starts processes with the same rights, and MCJIT resolves unknown symbols via
  `DynamicLibrary::LoadLibraryPermanently(nullptr)` against the process's exported
  symbols.  Whoever executes foreign pipeline source text therefore executes it with the host's rights —
  the same class of error as a remote shell, not a weakened one.  Isolation in the sense of
  **rights** needs a separate process (and underneath it rootless/userns, Kata or gVisor), not
  a separate `LLVMContext`.

