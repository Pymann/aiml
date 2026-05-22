# Hot-Reload — Concrete Implementation Plan

## 1. Scope

| Feature | In this plan | Deferred |
|---------|-------------|----------|
| Code Hot-Reload (functions, methods) | ✓ fptable (2D) | Trampolines |
| Classes / Models | ✓ Append-Only + Padding | Version tags, migration |
| Enums | — | fully deferred |
| Interfaces | — | fully deferred |
| Persistence (journal) | — | Phase 2 |
| Multi-thread atomicity | ✓ (8-byte store, fptable_current) | Thread suspend |

---

## Terminology

| Term | Meaning |
|------|---------|
| **fptable** | Global 2D array `__aiml_fptable[func][version]` — stores all function pointers by function and version |
| **fptable_current** | 1D array `__aiml_fptable_current[func]` — always points to the latest version; updated atomically |
| **func_idx** | Compile-time index of a function in the fptable (0-based, stable over program lifetime) |
| **version_idx** | `version - 1` — 0-based index into the second fptable dimension |
| **Hot-Reload** | Replacing a running function/schema version with a newer one without restarting the program |
| **context=false** | Shared-context mode: hot-reload active, fptable is updated, host symbols reachable |
| **context=true** | Isolated mode: fresh engine, no fptable update, full sandboxing |
| **JIT mode** | `aiml -r file.aiml` — program runs in MCJIT, `g_active_ee` valid |
| **AOT mode** | `exodus build` → compiled binary (`aiml src.aiml -o src.o` + linking). Standalone `aiml file.aiml` without flags only produces `.ll`. No LLVM at runtime, `g_active_ee = nullptr` |
| **compileYamlChunk** | Runtime function: compiles a YAML string to machine code and returns the function address |
| **Schema-Registry** | Runtime table of all class/model layouts (`AimlClassSchema`) — exclusively for `padded_size` (allocation via `calloc`) and append-only validation; **no field offset table** (GEP indices are compile-time stable) |
| **Append-Only** | Constraint: new fields only at the end of a class/model — existing GEP indices remain unchanged, baked-in code stays correct |
| **padded_size** | Allocation size of a struct: next power of two of `packed_size`; zero-initialized via `calloc` |
| **packed_size** | Actual size of all declared fields (without padding) |
| **g_aot_engines** | List of freshly created MCJIT engines in AOT mode — must stay alive to keep JIT pages valid |
| **-rdynamic** | Linker flag: exports all symbols of the binary dynamically → findable via `dlsym(RTLD_DEFAULT, ...)` |
| **AIML_MAX_VERSIONS** | Maximum number of hot-reloadable versions per function (default: 16) |

---

## 2. 2D fptable — Data Structure

### 2.1 Concept

```
__aiml_fptable[func_idx][version_idx]
  Dimension 1: Function  (fixed index per name, assigned at compile time)
  Dimension 2: Version   (version-1, 0-based; max AIML_MAX_VERSIONS = 16)

__aiml_fptable_current[func_idx]
  Always: pointer to the latest version — for normal call sites (1 load, atomic)

__aiml_fptable_versions[func_idx]
  Currently highest version (1-based) — for exec_impl validation

__aiml_func_names[func_idx]
  Function name as C string — for exec_impl name lookup

__aiml_func_count
  Number of registered functions — limit for lookup loop
```

### 2.2 LLVM Types and Initialization

```cpp
// Constants (in fptable.h):
static constexpr int AIML_MAX_VERSIONS = 16;

// Global LLVM variables (emitted in registerTypes / emitFptable):
//
// @__aiml_fptable         = global [N x [16 x i8*]] zeroinitializer
// @__aiml_fptable_current = global [N x i8*]        zeroinitializer
// @__aiml_fptable_versions= global [N x i32]        zeroinitializer
// @__aiml_func_names      = global [N x i8*]        [ptr-to-strings]
// @__aiml_func_count      = constant i32 N
```

### 2.3 Memory Layout (x86-64, N=3 functions)

```
__aiml_fptable (3 × 16 × 8 bytes = 384 bytes):
  [0][0] = worker_task_v1   [0][1] = null … [0][15] = null
  [1][0] = log_msg_v1       [1][1] = null … [1][15] = null
  [2][0] = compute_v1       [2][1] = null … [2][15] = null

__aiml_fptable_current (3 × 8 bytes = 24 bytes):
  [0] = worker_task_v1    ← always points to latest
  [1] = log_msg_v1
  [2] = compute_v1

__aiml_fptable_versions (3 × 4 bytes = 12 bytes):
  [0] = 1    [1] = 1    [2] = 1
```

---

## 3. 2D fptable — LLVM IR Emission

### 3.1 New file: `src/core/fptable.h`

```cpp
#pragma once
#include "context.h"

namespace AIML {

static constexpr int AIML_MAX_VERSIONS = 16;

// Emits all fptable globals.
// Must be called after visitFunction() for all functions.
void emitFptable(CompilerContext& ctx);

} // namespace AIML
```

### 3.2 New file: `src/core/fptable.cpp`

```cpp
#include "fptable.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <vector>
#include <string>

namespace AIML {

void emitFptable(CompilerContext& ctx) {
    auto& M    = ctx.module;
    auto& C    = ctx.llvmCtx;
    auto* i8p  = llvm::Type::getInt8PtrTy(C);
    auto* i32  = llvm::Type::getInt32Ty(C);

    // Collect functions in index order
    // (ctx.functions is a map → stable order via fptable_index):
    int N = 0;
    for (auto& [_, fi] : ctx.functions) N = std::max(N, fi.fptable_index + 1);

    std::vector<std::pair<int,llvm::Function*>> sorted(N);
    std::vector<std::string> names(N);
    for (auto& [name, fi] : ctx.functions) {
        sorted[fi.fptable_index] = {fi.fptable_index, fi.func};
        names[fi.fptable_index]  = name;
    }

    // --- 1. @__aiml_fptable [N x [MAX_VERSIONS x i8*]] ---
    auto* row_ty   = llvm::ArrayType::get(i8p, AIML_MAX_VERSIONS);
    auto* table_ty = llvm::ArrayType::get(row_ty, N);

    std::vector<llvm::Constant*> rows;
    for (auto& [idx, fn] : sorted) {
        std::vector<llvm::Constant*> slots(AIML_MAX_VERSIONS,
                                           llvm::ConstantPointerNull::get(i8p));
        slots[0] = fn
            ? llvm::ConstantExpr::getBitCast(fn, i8p)
            : llvm::ConstantPointerNull::get(i8p);
        rows.push_back(llvm::ConstantArray::get(row_ty, slots));
    }
    new llvm::GlobalVariable(M, table_ty, false,
        llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantArray::get(table_ty, rows),
        "__aiml_fptable");

    // --- 2. @__aiml_fptable_current [N x i8*] ---
    auto* cur_ty = llvm::ArrayType::get(i8p, N);
    std::vector<llvm::Constant*> cur_ptrs;
    for (auto& [idx, fn] : sorted)
        cur_ptrs.push_back(fn
            ? llvm::ConstantExpr::getBitCast(fn, i8p)
            : llvm::ConstantPointerNull::get(i8p));
    new llvm::GlobalVariable(M, cur_ty, false,
        llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantArray::get(cur_ty, cur_ptrs),
        "__aiml_fptable_current");

    // --- 3. @__aiml_fptable_versions [N x i32] ---
    auto* ver_ty = llvm::ArrayType::get(i32, N);
    std::vector<llvm::Constant*> vers(N, llvm::ConstantInt::get(i32, 1));
    new llvm::GlobalVariable(M, ver_ty, false,
        llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantArray::get(ver_ty, vers),
        "__aiml_fptable_versions");

    // --- 4. @__aiml_func_names [N x i8*] ---
    auto* names_ty = llvm::ArrayType::get(i8p, N);
    std::vector<llvm::Constant*> name_ptrs;
    for (auto& nm : names) {
        auto* str = llvm::ConstantDataArray::getString(C, nm, true);
        auto* gv  = new llvm::GlobalVariable(M, str->getType(), true,
            llvm::GlobalValue::PrivateLinkage, str);
        name_ptrs.push_back(llvm::ConstantExpr::getBitCast(gv, i8p));
    }
    new llvm::GlobalVariable(M, names_ty, true,
        llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantArray::get(names_ty, name_ptrs),
        "__aiml_func_names");

    // --- 5. @__aiml_func_count ---
    new llvm::GlobalVariable(M, i32, true,
        llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantInt::get(i32, N),
        "__aiml_func_count");
}

} // namespace AIML
```

---

## 4. 2D fptable — Call-Site Emission

### 4.1 `src/core/context.h` — Extend FunctionInfo

```cpp
struct FunctionInfo {
    llvm::Function* func    = nullptr;
    std::vector<std::string> argNames;
    std::string     typeName;
    int             version  = 1;
    std::string     sourceYaml;
    std::set<std::string> referencedTypes;

    int  fptable_index = -1;   // ← NEW: index in __aiml_fptable / fptable_current
};
```

### 4.2 `src/core/ast_compiler.cpp` — Assign indices

In `registerTypes()`, before `visitFunction()`:

```cpp
// Collect all function declarations and assign indices:
void Compiler::registerTypeDecl(Node* node) {
    if (auto* fn = dynamic_cast<FunctionDecl*>(node)) {
        FunctionInfo fi;
        fi.fptable_index = (int)ctx.functions.size();  // next free index
        ctx.functions[fn->name] = fi;                   // placeholder
    }
    // ... classes, enums as before
}
```

At the end of `compile()`, after all `visitFunction()` calls:

```cpp
#include "fptable.h"
// ...
void Compiler::compile(Program* prog) {
    registerTypes(prog);
    for (auto* node : prog->nodes)
        visitTopLevel(node);

    emitFptable(ctx);           // ← NEW: after all functions
}
```

### 4.3 `src/core/ast_compiler_call.cpp` — Indirect Call

In `visitCall()`, when a known AIML function is called:

```cpp
IRValue* Compiler::visitCall(CallExpr* call) {
    // ... sys.* intercepts as before ...

    auto* fi = resolveFunction(funcName);
    if (!fi) { LOG_ERROR(...); return nullptr; }

    if (fi->fptable_index >= 0) {
        // Indirect call via __aiml_fptable_current[idx]
        auto* i8p    = llvm::Type::getInt8PtrTy(ctx.llvmCtx);
        auto* cur_gv = ctx.module.getNamedGlobal("__aiml_fptable_current");

        llvm::Value* slot = ctx.builder.CreateGEP(
            cur_gv->getValueType(), cur_gv,
            {ctx.ir_gen->getInt32(0),
             ctx.ir_gen->getInt32(fi->fptable_index)},
            "fptable_slot");
        llvm::Value* fp_raw = ctx.builder.CreateLoad(i8p, slot, "fp_raw");
        llvm::Value* fp     = ctx.builder.CreateBitCast(
            fp_raw, fi->func->getType(), "fp");

        // Evaluate arguments and emit call
        std::vector<llvm::Value*> argVals;
        for (auto& arg : call->args)
            argVals.push_back(ctx.ir_gen->unwrap(visitExpression(arg.get())));

        return ctx.ir_gen->wrap(
            ctx.builder.CreateCall(fi->func->getFunctionType(), fp, argVals));
    }

    // Fallback: direct call (for builtins / external functions)
    // ...
}
```

---

## 5. 2D fptable — Hot-Reload Runtime

### 5.1 `exec_impl.h` — New exports

```cpp
#pragma once
extern "C" void aiml_set_active_context(void* ctx, void* ee);
extern "C" int  aiml_exec_code(void* code_obj);

// Fptable update (also usable from future libaiml):
extern "C" int  aiml_fptable_find(const char* name);
extern "C" void aiml_fptable_update(int func_idx, int version, void* fn_ptr);
```

### 5.2 `exec_impl.cpp` — Helper functions

```cpp
// Read fptable globals from the engine (cached after first call):
static void**   g_fptable_current  = nullptr;   // __aiml_fptable_current[0]
static void***  g_fptable          = nullptr;   // __aiml_fptable[0][0] (row-major)
static int32_t* g_fptable_versions = nullptr;
static const char** g_func_names   = nullptr;
static int32_t  g_func_count       = 0;

static void cache_fptable_globals() {
    if (!g_active_ee || g_fptable_current) return;

    auto ga = [](const char* nm) -> uint64_t {
        return g_active_ee->getGlobalValueAddress(nm);
    };
    g_fptable_current  = (void**)    ga("__aiml_fptable_current");
    g_fptable          = (void***)   ga("__aiml_fptable");
    g_fptable_versions = (int32_t*)  ga("__aiml_fptable_versions");
    g_func_names       = (const char**) ga("__aiml_func_names");
    auto cnt_addr      = ga("__aiml_func_count");
    g_func_count       = cnt_addr ? *(int32_t*)cnt_addr : 0;
}

extern "C" int aiml_fptable_find(const char* name) {
    cache_fptable_globals();
    for (int i = 0; i < g_func_count; i++)
        if (g_func_names[i] && strcmp(g_func_names[i], name) == 0)
            return i;
    return -1;
}

extern "C" void aiml_fptable_update(int func_idx, int version, void* fn_ptr) {
    // version is 1-based; array index = version - 1
    int vi = version - 1;
    if (vi < 0 || vi >= AIML_MAX_VERSIONS) {
        LOG_ERROR("aiml_fptable_update: version out of range: " + std::to_string(version));
        return;
    }
    // 2D table: row-major, stride = AIML_MAX_VERSIONS
    g_fptable[func_idx][vi] = fn_ptr;

    // Update current pointer atomically (8 bytes aligned → guaranteed atomic on x86-64):
    __atomic_store_n(&g_fptable_current[func_idx], fn_ptr, __ATOMIC_SEQ_CST);

    // Update version counter:
    if (version > g_fptable_versions[func_idx])
        g_fptable_versions[func_idx] = version;

    LOG_INFO("fptable: " + std::string(g_func_names[func_idx]) +
             " updated to version " + std::to_string(version));
}
```

### 5.3 `exec_impl.cpp` — Hot-Reload in `aiml_exec_code`

In the code_chunks processing block, replacing the previous SKIP + WARNING:

```cpp
// For each declaration in code_chunks:
auto tryHotReload = [&](const std::string& key,
                         auto& table,
                         int bundle_version) {
    if (!decl[key]) return;
    std::string nm = decl[key]["name"].as<std::string>();

    if (table.count(nm)) {
        int cur_ver = table.at(nm).version;
        if (bundle_version <= cur_ver) {
            LOG_INFO("sys.exec: skip " + nm + " (v" +
                     std::to_string(bundle_version) + " <= current v" +
                     std::to_string(cur_ver) + ")");
            return;
        }
        // Hot-Reload: recompile + update fptable
        LOG_INFO("sys.exec: hot-reload " + nm + " → v" + std::to_string(bundle_version));

        // Step 1: JIT-compile chunk as AIML source
        // (compileYamlChunk — section 5.4)
        uint64_t new_addr = compileYamlChunk(cc->data[i], nm);
        if (!new_addr) {
            LOG_ERROR("sys.exec: compilation failed for " + nm);
            return;
        }

        // Step 2: update fptable
        int fidx = aiml_fptable_find(nm.c_str());
        if (fidx < 0) {
            LOG_WARNING("sys.exec: " + nm + " not in fptable — new function, skipping");
            return;
        }
        aiml_fptable_update(fidx, bundle_version, (void*)new_addr);

        // Step 3: update symbol table
        table[nm].version = bundle_version;
    } else {
        LOG_WARNING("sys.exec: new " + key + " '" + nm +
                    "' — dynamic loading not yet supported");
    }
};

tryHotReload("def",   g_active_context->functions, bundle_version);
tryHotReload("class", g_active_context->classes,   bundle_version);
```

### 5.4 `compiler.cpp` — `compileYamlChunk` (new function)

```cpp
// Compiles a single YAML string (def/class) in a fresh LLVM module,
// links it into the active ExecutionEngine.
// Returns the address of the named function (0 = error).
static uint64_t compileYamlChunk(const char* yaml, const std::string& funcName) {
    // 1. YAML → AST
    YAML::Node node = YAML::Load(std::string(yaml));
    auto* prog = AIML::AST::Builder::buildChunk(node);  // parses single declaration
    if (!prog) return 0;

    // 2. Fresh LLVM module, same LLVMContext as g_active_context
    auto new_mod = std::make_unique<llvm::Module>("hr_chunk", g_active_context->llvmCtx);

    // 3. Fresh compiler with shared context
    llvm::IRBuilder<> builder(g_active_context->llvmCtx);
    CompilerContext chunk_ctx(g_active_context->llvmCtx, *new_mod, builder);
    // Adopt types from main context (so structs are compatible):
    chunk_ctx.classes = g_active_context->classes;
    chunk_ctx.enums   = g_active_context->enums;

    AIML::AST::Compiler compiler(chunk_ctx);
    compiler.compile(prog);

    // 4. Link module into active engine
    g_active_ee->addModule(std::move(new_mod));
    g_active_ee->finalizeObject();

    return g_active_ee->getFunctionAddress(funcName);
}
```

---

## 6. Schema Registry — Classes & Models (Append-Only)

> **Design decision**: Field offsets are **not** stored in a runtime table.
> Rationale: The append-only rule guarantees that existing fields never change
> their GEP position. Baked-in GEP indices in compiled code therefore remain
> permanently correct — without any indirection. The schema registry is only
> needed for two things: `padded_size` (allocation size) and field name comparison
> (append-only validation during hot-reload).


### 6.1 New file: `src/core/schema_registry.h`

```cpp
#pragma once
#include <cstdint>

// Field offsets are NOT stored — they are embedded as GEP indices at compile time
// and remain stable due to the append-only rule (existing fields never shift).
// The schema registry is used only for:
//   1. padded_size → calloc size during allocation
//   2. field_names + field_count → append-only validation in exec_impl

static constexpr int AIML_MAX_FIELDS = 32;

struct AimlFieldInfo {
    const char* name;   // field name for append-only comparison
    // no offset/size — GEP index is compile-time stable (append-only guarantee)
};

struct AimlClassSchema {
    const char*   name;
    int32_t       version;
    int32_t       field_count;
    int32_t       packed_size;   // actual field size (LLVM DataLayout)
    int32_t       padded_size;   // next power of two → allocation size for calloc
    AimlFieldInfo fields[AIML_MAX_FIELDS];
};

// Emits @__aiml_schemas and @__aiml_schema_count after all class definitions:
namespace AIML { void emitSchemaRegistry(CompilerContext& ctx); }
```

### 6.2 `src/core/context.h` — Extend ClassInfo

```cpp
struct ClassInfo {
    llvm::StructType* type = nullptr;
    std::vector<std::string> memberNames;
    std::vector<std::string> memberTypes;
    // ... existing fields ...
    int     version      = 1;
    std::string sourceYaml;
    std::set<std::string> referencedTypes;

    int     packed_size  = 0;   // ← NEW
    int     padded_size  = 0;   // ← NEW (next power of two of packed_size)
};
```

### 6.3 Padding Calculation

In `ast_compiler.cpp`, after computing the LLVM struct size:

```cpp
// In visitFunction / class registration:
int packed = (int)ctx.module.getDataLayout().getTypeAllocSize(ci.type);
ci.packed_size = packed;
// Next power of two:
int padded = 1;
while (padded < packed) padded <<= 1;
ci.padded_size = padded;
```

### 6.4 Padded Allocation in Generated Code

In `ast_compiler_expr.cpp` / `visitNewExpr`:

```cpp
// Instead of: call i8* @malloc(i64 sizeof(Payload))
// New:        call i8* @calloc(i64 1, i64 padded_sizeof(Payload))

llvm::Value* padded_sz = ctx.ir_gen->getInt64(ci.padded_size);
llvm::Function* callocFn = getOrEmitCalloc(ctx);
llvm::Value* raw = ctx.builder.CreateCall(callocFn, {ctx.ir_gen->getInt64(1), padded_sz});
```

`calloc` guarantees zero-initialization → new fields in old instances are 0.

### 6.5 Append-Only Validation in `exec_impl.cpp`

When a class chunk in the bundle is found with a higher version:

```cpp
bool validate_append_only(AimlClassSchema* cur, YAML::Node new_decl) {
    // Check that all existing fields are unchanged and in the same order:
    int new_fc = (int)new_decl["members"].size();
    if (new_fc < cur->field_count) {
        LOG_ERROR("hot-reload: class " + std::string(cur->name) +
                  " — fields removed (breaking change)");
        return false;
    }
    for (int i = 0; i < cur->field_count; i++) {
        std::string new_name = new_decl["members"][i][0].as<std::string>();
        if (new_name != cur->fields[i].name) {
            LOG_ERROR("hot-reload: class " + std::string(cur->name) +
                      " — field " + std::to_string(i) +
                      " changed (breaking change, only append-only allowed)");
            return false;
        }
    }
    return true;  // new fields at the end are ok
}
```

Update schema registry after successful validation:

```cpp
// Register new AimlClassSchema in g_schema_registry:
// (used for padded_size on future allocations and for
//  the next append-only validation round — NO field offsets)
update_schema_registry(cur->name, new_decl, new_version);
```

---

## 7. Files to Modify

### New files

| File | Content |
|------|---------|
| `src/core/fptable.h` | `emitFptable()` declaration + `AIML_MAX_VERSIONS` |
| `src/core/fptable.cpp` | Emit fptable globals |
| `src/core/schema_registry.h` | `AimlFieldInfo`, `AimlClassSchema`, `emitSchemaRegistry()` |
| `src/core/schema_registry.cpp` | Emit schema registry globals |

### Modified files

| File | Change |
|------|--------|
| `src/core/context.h` | `FunctionInfo::fptable_index`, `ClassInfo::packed_size`, `ClassInfo::padded_size` |
| `src/core/ast_compiler.cpp` | `registerTypeDecl`: assign fptable_index; end of `compile()`: call `emitFptable()` + `emitSchemaRegistry()`; padding calculation in class registration |
| `src/core/ast_compiler_call.cpp` | `visitCall`: indirect call via `__aiml_fptable_current[idx]` instead of direct `CreateCall(fi->func, ...)` |
| `src/core/ast_compiler_expr.cpp` | `visitNewExpr`: `calloc(1, padded_size)` instead of `malloc(packed_size)` |
| `exec_impl.h` | Export `aiml_fptable_find`, `aiml_fptable_update` |
| `exec_impl.cpp` | `cache_fptable_globals()`, `aiml_fptable_find()`, `aiml_fptable_update()`, `compileYamlChunk()`, hot-reload logic in `aiml_exec_code` |
| `compiler.cpp` | `jitRun()`: after `aiml_set_active_context` also call `cache_fptable_globals()` via `aiml_set_active_context` (or directly) |
| `CMakeLists.txt` | Add `fptable.cpp`, `schema_registry.cpp` to sources |

---

## 8. Hot-Reload Flow (Overview)

```
sys.exec(pkg) called
         │
         ▼
aiml_exec_code(code_obj)
         │
         ├── cache_fptable_globals()   [once]
         │
         ├── for each code_chunk:
         │     parse YAML → read name + version
         │     │
         │     ├── "def":
         │     │    bundle_version ≤ ctx.functions[name].version?
         │     │      ├── Yes → LOG_INFO("skip") → next chunk
         │     │      └── No  → compileYamlChunk(chunk, name)
         │     │                  → aiml_fptable_find(name) → idx
         │     │                  → aiml_fptable_update(idx, version, addr)
         │     │                  → ctx.functions[name].version = version
         │     │
         │     └── "class" / "model":
         │          bundle_version ≤ ctx.classes[name].version?
         │            ├── Yes → LOG_INFO("skip")
         │            └── No  → validate_append_only(cur_schema, new_decl)
         │                        ├── Error → LOG_ERROR → return -1
         │                        └── OK    → update_schema_registry(...)
         │                                    ctx.classes[name].version = version
         │
         ├── data_chunks: LOG_WARNING (deferred)
         │
         └── execute start_call (as before: getFunctionAddress + function pointer)
```

---

## 9. Tests

### `tests/test_hot_reload.aiml`

```aiml
# Define worker v1:
- def:
    name: hr_worker
    version: 1
    return: i32
    body:
      - return: 1

# Bundle + execute v1:
- decl: [pkg1, Code]
- set: [pkg1, {call: [sys.bundle, "hr_worker"]}]
- decl: [rc, i32]
- set: [rc, {call: [sys.exec, pkg1]}]
- print: ["v1 rc: ${rc}"]    # expected: v1 rc: 1

# Define worker v2 (hot-reload):
- def:
    name: hr_worker
    version: 2
    return: i32
    body:
      - return: 2

# Bundle with v2 + exec → Hot-Reload:
- decl: [pkg2, Code]
- set: [pkg2, {call: [sys.bundle, "hr_worker"]}]
- set: [rc, {call: [sys.exec, pkg2]}]
- print: ["v2 rc: ${rc}"]    # expected: v2 rc: 2

# Once more with v1 bundle → SKIP (version ≤ current):
- set: [rc, {call: [sys.exec, pkg1]}]
- print: ["v1 skip rc: ${rc}"]  # expected: v1 skip rc: 2 (v2 still active)
```

---

## 10. Implementation Order

```
Phase 1 — Data structures (no build break)
  1. context.h: FunctionInfo::fptable_index, ClassInfo::packed/padded_size
  2. fptable.h / fptable.cpp: emitFptable() — stub for now

Phase 2 — fptable Emission
  3. ast_compiler.cpp: registerTypeDecl → assign fptable_index
  4. fptable.cpp: implement full emission
  5. ast_compiler.cpp: compile() → emitFptable() at the end
  → Build + all existing tests must still pass (call sites still direct)

Phase 3 — Indirect Calls
  6. ast_compiler_call.cpp: visitCall → indirect call via fptable_current
  → Build + tests: function behavior identical, just indirect

Phase 4 — Schema Registry + Padding
  7. schema_registry.h / .cpp: AimlClassSchema, emitSchemaRegistry()
  8. ast_compiler.cpp: padding calculation + emitSchemaRegistry()
  9. ast_compiler_expr.cpp: calloc instead of malloc
  → Build + tests: identical behavior, structs larger but correct

Phase 5 — Hot-Reload in exec_impl.cpp
  10. exec_impl.h/.cpp: cache_fptable_globals, aiml_fptable_find/update, compileYamlChunk
  11. exec_impl.cpp: hot-reload logic in aiml_exec_code (replacing SKIP + WARNING)
  12. write tests/test_hot_reload.aiml
  → exodus build + bash run_all_tests.sh → ≥33/33 ✓
```

---

## 11. JIT Mode vs. AOT Mode — Implementation Differences

### 11.1 Overview Table: context × execution mode

| | **JIT** (`aiml -r file.aiml`) | **AOT** (`exodus build` → binary) |
|---|---|---|
| `g_active_ee` | ✓ valid | ✗ `nullptr` |
| **context=false** | Shared LLVM engine: `addModule` to running engine; fptable via `getGlobalValueAddress` | No shared LLVM state possible; fptable via `dlsym`; new code in fresh engine |
| **context=true** | Fresh isolated LLVMContext + engine; no fptable connection to host | Identical to JIT context=true; no difference |
| fptable access | `g_active_ee->getGlobalValueAddress(name)` | `dlsym(RTLD_DEFAULT, name)` + `-rdynamic` |
| Compiling new code | `addModule(new_mod)` → to running engine | Fresh `EngineBuilder` engine (LLVM must be linked) |
| Existing functions callable? | ✓ (same ExecutionEngine) | ✓ via `LoadLibraryPermanently(nullptr)` + `-rdynamic` |
| Hot-reload into fptable | ✓ `aiml_fptable_update` via engine address | ✓ `aiml_fptable_update` via `dlsym` address |
| LLVM runtime dependency | already present | binary must link LLVM (or `libaiml`) |

### 11.2 `cache_fptable_globals()` — two paths

```cpp
static void cache_fptable_globals() {
    if (g_fptable_current) return;  // already cached

    if (g_active_ee) {
        // ── JIT mode ─────────────────────────────────────────────────────────
        auto ga = [](const char* nm) -> uint64_t {
            return g_active_ee->getGlobalValueAddress(nm);
        };
        g_fptable_current  = (void**)      ga("__aiml_fptable_current");
        g_fptable          = (void***)     ga("__aiml_fptable");
        g_fptable_versions = (int32_t*)    ga("__aiml_fptable_versions");
        g_func_names       = (const char**)ga("__aiml_func_names");
        g_func_count       = *(int32_t*)   ga("__aiml_func_count");
    } else {
        // ── AOT mode ─────────────────────────────────────────────────────────
        // Binary must be linked with -rdynamic so that its own globals are visible.
        g_fptable_current  = (void**)      dlsym(RTLD_DEFAULT, "__aiml_fptable_current");
        g_fptable          = (void***)     dlsym(RTLD_DEFAULT, "__aiml_fptable");
        g_fptable_versions = (int32_t*)    dlsym(RTLD_DEFAULT, "__aiml_fptable_versions");
        g_func_names       = (const char**)dlsym(RTLD_DEFAULT, "__aiml_func_names");
        auto* cnt          = (int32_t*)    dlsym(RTLD_DEFAULT, "__aiml_func_count");
        g_func_count       = cnt ? *cnt : 0;
    }
}
```

### 11.3 `compileYamlChunk()` — two paths

```cpp
static uint64_t compileYamlChunk(const char* yaml, const std::string& funcName) {
    // ── Common: YAML → AST → IR ───────────────────────────────────────────────
    YAML::Node node = YAML::Load(std::string(yaml));
    auto* prog = AIML::AST::Builder::buildChunk(node);
    if (!prog) return 0;

    auto new_mod = std::make_unique<llvm::Module>("hr_chunk", g_active_context->llvmCtx);
    llvm::IRBuilder<> builder(g_active_context->llvmCtx);
    CompilerContext chunk_ctx(g_active_context->llvmCtx, *new_mod, builder);
    chunk_ctx.classes = g_active_context->classes;  // adopt types
    chunk_ctx.enums   = g_active_context->enums;
    AIML::AST::Compiler compiler(chunk_ctx);
    compiler.compile(prog);

    if (g_active_ee) {
        // ── JIT mode: add module to running engine ────────────────────────────
        // Same LLVMContext → struct types compatible, symbols directly callable.
        g_active_ee->addModule(std::move(new_mod));
        g_active_ee->finalizeObject();
        return g_active_ee->getFunctionAddress(funcName);

    } else {
        // ── AOT mode: fresh ExecutionEngine ──────────────────────────────────
        // Make process symbols available (malloc, aiml_print_string etc.):
        llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);

        std::string err;
        auto* engine = llvm::EngineBuilder(std::move(new_mod))
            .setErrorStr(&err)
            .setEngineKind(llvm::EngineKind::JIT)
            .create();
        if (!engine) { LOG_ERROR("compileYamlChunk: " + err); return 0; }
        engine->finalizeObject();

        uint64_t addr = engine->getFunctionAddress(funcName);

        // Keep engine alive (otherwise JIT pages are freed):
        g_aot_engines.push_back(engine);   // static std::vector<ExecutionEngine*>

        return addr;
    }
}
```

### 11.4 context=false vs. context=true in `aiml_exec_code`

`context=false` (default): Hot-reload — existing fptable entries are replaced.
`context=true` (isolated): no hot-reload — new code object runs completely
isolated, host program's fptable remains untouched.

```cpp
extern "C" int aiml_exec_code(void* raw) {
    ExecCode* code = static_cast<ExecCode*>(raw);

    if (code->new_context) {
        // ── context=true: isolated path ───────────────────────────────────────
        // Works the same in JIT and AOT (always fresh engine):
        llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
        // ... build fresh LLVMContext + module + engine ...
        // ... compile code_chunks + execute start_call ...
        // fptable: do NOT update
        return isolated_exec(code);
    }

    // ── context=false: shared / hot-reload path ───────────────────────────────
    // AOT mode: fptable reachable via dlsym, even without g_active_ee:
    cache_fptable_globals();
    if (!g_fptable_current) return -2;  // neither JIT nor AOT fptable found

    // Process code_chunks: validate → compileYamlChunk → aiml_fptable_update
    // ...

    // Execute start_call:
    if (g_active_ee) {
        // JIT: getFunctionAddress from running engine
        uint64_t addr = g_active_ee->getFunctionAddress(funcName);
        return ((int32_t(*)())addr)();
    } else {
        // AOT: address from fptable_current (contains latest pointer)
        int fidx = aiml_fptable_find(funcName.c_str());
        if (fidx < 0) { LOG_ERROR("function not found: " + funcName); return -1; }
        return ((int32_t(*)())g_fptable_current[fidx])();
    }
}
```

### 11.5 Complete Mechanism Matrix

| Aspect | JIT + context=false | JIT + context=true | AOT + context=false | AOT + context=true |
|--------|--------------------|--------------------|--------------------|--------------------|
| fptable access | `getGlobalValueAddress` | — (no fptable update) | `dlsym(RTLD_DEFAULT,...)` | — (no fptable update) |
| Compile new code | `addModule` to `g_active_ee` | fresh engine | fresh engine + `g_aot_engines` | fresh engine |
| Struct type compatibility | ✓ (same LLVMContext) | ✗ (isolated) | △ (types copied from `g_active_context`) | ✗ (isolated) |
| Existing functions | ✓ (same engine) | ✓ via `LoadLibraryPermanently` | ✓ via `LoadLibraryPermanently` + `-rdynamic` | ✓ via `LoadLibraryPermanently` + `-rdynamic` |
| fptable update after reload | ✓ | ✗ | ✓ | ✗ |
| Binary prerequisite | — | — | `-rdynamic` | `-rdynamic` |
| LLVM at runtime | already present | already present | must be linked | must be linked |
| `g_active_ee` | ✓ valid | ✓ valid | `nullptr` | `nullptr` |
| Useful? | ✓ main case | ✓ sandboxing | ✓ with `-rdynamic` + LLVM | ✓ simplest AOT case |

---

## 12. Known Limitations of this Plan

- New functions (not yet in fptable): `LOG_WARNING` + skip (no dynamic addition)
- Classes with breaking changes: `LOG_ERROR` + return -1
- `compileYamlChunk`: classes in the main module must be known in `chunk_ctx`
  (copied from `g_active_context->classes` — types compatible since same LLVMContext)
- Multi-thread: `fptable_current` update is atomic; `fptable[idx][vi]` write is not
  → for multi-thread: `fptable` write before `fptable_current` update with memory barrier
- `AIML_MAX_VERSIONS = 16`: more than 16 versions of a function → overflow error
- Padding overhead: all structs at least 2× as large as packed
  → `@noreload` annotation for performance-critical types (no padding, no schema)
