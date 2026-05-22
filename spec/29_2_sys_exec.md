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
No connection to the host program. Always clean, no conflicts.
Useful for untrusted code or complete sandboxing.

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

# Future: isolated context
# - set: [rc, { call: [sys.exec, pkg, true] }]
# or: pkg.new_context = true; sys.exec(pkg)
```

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
