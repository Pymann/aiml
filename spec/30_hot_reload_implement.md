# Hot-Reload — Konkreter Implementierungsplan

## 1. Scope

| Feature | In diesem Plan | Zurückgestellt |
|---------|---------------|----------------|
| Code Hot-Reload (Funktionen, Methoden) | ✓ fptable (2D) | Trampoline |
| Klassen / Models | ✓ Append-Only + Padding | Version-Tags, Migration |
| Enums | — | komplett zurückgestellt |
| Interfaces | — | komplett zurückgestellt |
| Persistenz (Journal) | — | Phase 2 |
| Multi-Thread-Atomizität | ✓ (8-Byte-Store, fptable_current) | Thread-Suspend |

---

## Begriffe

| Begriff | Bedeutung |
|---------|-----------|
| **fptable** | Globales 2D-Array `__aiml_fptable[func][version]` — speichert alle Funktionszeiger nach Funktion und Version |
| **fptable_current** | 1D-Array `__aiml_fptable_current[func]` — zeigt immer auf die aktuellste Version; wird atomar aktualisiert |
| **func_idx** | Compile-Zeit-Index einer Funktion im fptable (0-basiert, stabil über Programmlebensdauer) |
| **version_idx** | `version - 1` — 0-basierter Index der zweiten fptable-Dimension |
| **Hot-Reload** | Ersetzen einer laufenden Funktions-/Schema-Version durch eine neuere ohne Programmneustart |
| **context=false** | Shared-Context-Modus: Hot-Reload aktiv, fptable wird aktualisiert, Host-Symbole erreichbar |
| **context=true** | Isolierter Modus: frische Engine, kein fptable-Update, vollständiges Sandboxing |
| **JIT-Modus** | `aiml -r file.aiml` — Programm läuft in MCJIT, `g_active_ee` valide |
| **AOT-Modus** | `exodus build` → kompiliertes Binary (`aiml src.aiml -o src.o` + Linking). Standalone `aiml file.aiml` ohne Flags erzeugt nur `.ll`. Kein LLVM zur Laufzeit, `g_active_ee = nullptr` |
| **compileYamlChunk** | Laufzeit-Funktion: kompiliert einen YAML-String zu Maschinencode und gibt die Funktionsadresse zurück |
| **Schema-Registry** | Laufzeit-Tabelle aller Klassen/Model-Layouts (`AimlClassSchema`) — ausschließlich für `padded_size` (Allokierung via `calloc`) und Append-Only-Validierung; **keine Feld-Offset-Tabelle** (GEP-Indices sind compile-time-stabil) |
| **Append-Only** | Constraint: neue Felder nur am Ende einer Klasse/Model — bestehende GEP-Indices bleiben unverändert, baked-in Code bleibt korrekt |
| **padded_size** | Allokierungsgröße eines Structs: nächste Zweierpotenz von `packed_size`; Zero-Initialisiert via `calloc` |
| **packed_size** | Tatsächliche Größe aller deklarierten Felder (ohne Padding) |
| **g_aot_engines** | Liste frisch erstellter MCJIT-Engines im AOT-Modus — müssen am Leben bleiben, damit JIT-Pages gültig bleiben |
| **-rdynamic** | Linker-Flag: exportiert alle Symbole des Binaries dynamisch → `dlsym(RTLD_DEFAULT, ...)` findbar |
| **AIML_MAX_VERSIONS** | Maximale Anzahl hot-reloadbarer Versionen pro Funktion (Default: 16) |

---

## 2. 2D fptable — Datenstruktur

### 2.1 Konzept

```
__aiml_fptable[func_idx][version_idx]
  Dimension 1: Funktion  (fester Index pro Name, vergeben zur Compile-Zeit)
  Dimension 2: Version   (version-1, 0-basiert; max AIML_MAX_VERSIONS = 16)

__aiml_fptable_current[func_idx]
  Immer: Zeiger auf aktuellste Version — für normale Call-Sites (1 Load, atomar)

__aiml_fptable_versions[func_idx]
  Aktuell höchste Version (1-basiert) — für exec_impl zur Validierung

__aiml_func_names[func_idx]
  Funktionsname als C-String — für exec_impl Namens-Lookup

__aiml_func_count
  Anzahl registrierter Funktionen — Grenze für Lookup-Schleife
```

### 2.2 LLVM-Typen und -Initialisierung

```cpp
// Konstanten (in fptable.h):
static constexpr int AIML_MAX_VERSIONS = 16;

// Globale LLVM-Variablen (emittiert in registerTypes / emitFptable):
//
// @__aiml_fptable         = global [N x [16 x i8*]] zeroinitializer
// @__aiml_fptable_current = global [N x i8*]        zeroinitializer
// @__aiml_fptable_versions= global [N x i32]        zeroinitializer
// @__aiml_func_names      = global [N x i8*]        [ptr-zu-strings]
// @__aiml_func_count      = constant i32 N
```

### 2.3 Speicherlayout (x86-64, N=3 Funktionen)

```
__aiml_fptable (3 × 16 × 8 Bytes = 384 Bytes):
  [0][0] = worker_task_v1   [0][1] = null … [0][15] = null
  [1][0] = log_msg_v1       [1][1] = null … [1][15] = null
  [2][0] = compute_v1       [2][1] = null … [2][15] = null

__aiml_fptable_current (3 × 8 Bytes = 24 Bytes):
  [0] = worker_task_v1    ← immer auf aktuellste
  [1] = log_msg_v1
  [2] = compute_v1

__aiml_fptable_versions (3 × 4 Bytes = 12 Bytes):
  [0] = 1    [1] = 1    [2] = 1
```

---

## 3. 2D fptable — LLVM IR Emission

### 3.1 Neue Datei: `src/core/fptable.h`

```cpp
#pragma once
#include "context.h"

namespace AIML {

static constexpr int AIML_MAX_VERSIONS = 16;

// Emittiert alle fptable-Globals.
// Muss nach visitFunction() aller Funktionen aufgerufen werden.
void emitFptable(CompilerContext& ctx);

} // namespace AIML
```

### 3.2 Neue Datei: `src/core/fptable.cpp`

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

    // Funktionen in Index-Reihenfolge sammeln
    // (ctx.functions ist map → stabile Ordnung via fptable_index):
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

## 4. 2D fptable — Call-Site-Emission

### 4.1 `src/core/context.h` — FunctionInfo erweitern

```cpp
struct FunctionInfo {
    llvm::Function* func    = nullptr;
    std::vector<std::string> argNames;
    std::string     typeName;
    int             version  = 1;
    std::string     sourceYaml;
    std::set<std::string> referencedTypes;

    int  fptable_index = -1;   // ← NEU: Index in __aiml_fptable / fptable_current
};
```

### 4.2 `src/core/ast_compiler.cpp` — Indices vergeben

In `registerTypes()`, vor `visitFunction()`:

```cpp
// Alle Funktions-Deklarationen sammeln und Indizes vergeben:
void Compiler::registerTypeDecl(Node* node) {
    if (auto* fn = dynamic_cast<FunctionDecl*>(node)) {
        FunctionInfo fi;
        fi.fptable_index = (int)ctx.functions.size();  // nächster freier Index
        ctx.functions[fn->name] = fi;                   // Platzhalter
    }
    // ... Klassen, Enums wie bisher
}
```

Am Ende von `compile()`, nach allen `visitFunction()`-Aufrufen:

```cpp
#include "fptable.h"
// ...
void Compiler::compile(Program* prog) {
    registerTypes(prog);
    for (auto* node : prog->nodes)
        visitTopLevel(node);

    emitFptable(ctx);           // ← NEU: nach allen Funktionen
}
```

### 4.3 `src/core/ast_compiler_call.cpp` — Indirekter Call

In `visitCall()`, wenn eine bekannte AIML-Funktion aufgerufen wird:

```cpp
IRValue* Compiler::visitCall(CallExpr* call) {
    // ... sys.* intercepts wie bisher ...

    auto* fi = resolveFunction(funcName);
    if (!fi) { LOG_ERROR(...); return nullptr; }

    if (fi->fptable_index >= 0) {
        // Indirekter Call via __aiml_fptable_current[idx]
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

        // Argumente evaluieren und Call emittieren
        std::vector<llvm::Value*> argVals;
        for (auto& arg : call->args)
            argVals.push_back(ctx.ir_gen->unwrap(visitExpression(arg.get())));

        return ctx.ir_gen->wrap(
            ctx.builder.CreateCall(fi->func->getFunctionType(), fp, argVals));
    }

    // Fallback: direkter Call (für Builtins / externe Funktionen)
    // ...
}
```

---

## 5. 2D fptable — Hot-Reload-Runtime

### 5.1 `exec_impl.h` — neue Exports

```cpp
#pragma once
extern "C" void aiml_set_active_context(void* ctx, void* ee);
extern "C" int  aiml_exec_code(void* code_obj);

// Fptable-Update (auch von zukünftiger libaiml nutzbar):
extern "C" int  aiml_fptable_find(const char* name);
extern "C" void aiml_fptable_update(int func_idx, int version, void* fn_ptr);
```

### 5.2 `exec_impl.cpp` — Hilfsfunktionen

```cpp
// Fptable-Globals aus der Engine lesen (gecacht nach erstem Aufruf):
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
    // version ist 1-basiert; Array-Index = version - 1
    int vi = version - 1;
    if (vi < 0 || vi >= AIML_MAX_VERSIONS) {
        LOG_ERROR("aiml_fptable_update: version out of range: " + std::to_string(version));
        return;
    }
    // 2D-Table: row-major, Schrittweite = AIML_MAX_VERSIONS
    g_fptable[func_idx][vi] = fn_ptr;

    // current-Pointer atomar updaten (8 Bytes aligned → garantiert atomar auf x86-64):
    __atomic_store_n(&g_fptable_current[func_idx], fn_ptr, __ATOMIC_SEQ_CST);

    // Versionszähler aktualisieren:
    if (version > g_fptable_versions[func_idx])
        g_fptable_versions[func_idx] = version;

    LOG_INFO("fptable: " + std::string(g_func_names[func_idx]) +
             " updated to version " + std::to_string(version));
}
```

### 5.3 `exec_impl.cpp` — Hot-Reload in `aiml_exec_code`

Im code_chunks-Verarbeitungsblock, statt dem bisherigen SKIP + WARNING:

```cpp
// Für jede Deklaration in code_chunks:
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
        // Hot-Reload: neu kompilieren + fptable updaten
        LOG_INFO("sys.exec: hot-reload " + nm + " → v" + std::to_string(bundle_version));

        // Schritt 1: chunk als AIML-Quelldatei JIT-kompilieren
        // (compileYamlChunk — Abschnitt 5.4)
        uint64_t new_addr = compileYamlChunk(cc->data[i], nm);
        if (!new_addr) {
            LOG_ERROR("sys.exec: compilation failed for " + nm);
            return;
        }

        // Schritt 2: fptable updaten
        int fidx = aiml_fptable_find(nm.c_str());
        if (fidx < 0) {
            LOG_WARNING("sys.exec: " + nm + " not in fptable — new function, skipping");
            return;
        }
        aiml_fptable_update(fidx, bundle_version, (void*)new_addr);

        // Schritt 3: Symboltabelle updaten
        table[nm].version = bundle_version;
    } else {
        LOG_WARNING("sys.exec: new " + key + " '" + nm +
                    "' — dynamic loading not yet supported");
    }
};

tryHotReload("def",   g_active_context->functions, bundle_version);
tryHotReload("class", g_active_context->classes,   bundle_version);
```

### 5.4 `compiler.cpp` — `compileYamlChunk` (neue Funktion)

```cpp
// Kompiliert einen einzelnen YAML-String (def/class) in einem frischen
// LLVM-Module, linkt es in die aktive ExecutionEngine ein.
// Gibt die Adresse der benannten Funktion zurück (0 = Fehler).
static uint64_t compileYamlChunk(const char* yaml, const std::string& funcName) {
    // 1. YAML → AST
    YAML::Node node = YAML::Load(std::string(yaml));
    auto* prog = AIML::AST::Builder::buildChunk(node);  // parst einzelne Deklaration
    if (!prog) return 0;

    // 2. Frisches LLVM-Module, gleicher LLVMContext wie g_active_context
    auto new_mod = std::make_unique<llvm::Module>("hr_chunk", g_active_context->llvmCtx);

    // 3. Frischer Compiler mit geteiltem Context
    llvm::IRBuilder<> builder(g_active_context->llvmCtx);
    CompilerContext chunk_ctx(g_active_context->llvmCtx, *new_mod, builder);
    // Typen aus Haupt-Context übernehmen (damit Structs kompatibel):
    chunk_ctx.classes = g_active_context->classes;
    chunk_ctx.enums   = g_active_context->enums;

    AIML::AST::Compiler compiler(chunk_ctx);
    compiler.compile(prog);

    // 4. Module in aktive Engine einlinken
    g_active_ee->addModule(std::move(new_mod));
    g_active_ee->finalizeObject();

    return g_active_ee->getFunctionAddress(funcName);
}
```

---

## 6. Schema-Registry — Klassen & Models (Append-Only)

> **Design-Entscheidung**: Feldoffsets werden **nicht** in einer Laufzeit-Tabelle
> gespeichert. Begründung: Die Append-Only-Regel garantiert, dass bestehende Felder
> ihre GEP-Position nie ändern. Baked-in GEP-Indices im kompilierten Code bleiben
> daher dauerhaft korrekt — ohne jede Indirektion. Die Schema-Registry ist nur für
> zwei Dinge nötig: `padded_size` (Allokierungsgröße) und Feld-Namensvergleich
> (Append-Only-Validierung beim Hot-Reload).


### 6.1 Neue Datei: `src/core/schema_registry.h`

```cpp
#pragma once
#include <cstdint>

// Feldoffsets werden NICHT gespeichert — sie sind als GEP-Indices zur Compile-Zeit
// eingebettet und bleiben durch die Append-Only-Regel stabil (bestehende Felder
// verschieben sich nie). Die Schema-Registry dient nur für:
//   1. padded_size → calloc-Größe beim Allokieren
//   2. field_names + field_count → Append-Only-Validierung in exec_impl

static constexpr int AIML_MAX_FIELDS = 32;

struct AimlFieldInfo {
    const char* name;   // Feldname für Append-Only-Vergleich
    // kein offset/size — GEP-Index ist compile-time-stabil (Append-Only-Garantie)
};

struct AimlClassSchema {
    const char*   name;
    int32_t       version;
    int32_t       field_count;
    int32_t       packed_size;   // tatsächliche Feldgröße (LLVM DataLayout)
    int32_t       padded_size;   // nächste 2er-Potenz → Allokierungsgröße für calloc
    AimlFieldInfo fields[AIML_MAX_FIELDS];
};

// Emittiert @__aiml_schemas und @__aiml_schema_count nach allen Klassen-Definitionen:
namespace AIML { void emitSchemaRegistry(CompilerContext& ctx); }
```

### 6.2 `src/core/context.h` — ClassInfo erweitern

```cpp
struct ClassInfo {
    llvm::StructType* type = nullptr;
    std::vector<std::string> memberNames;
    std::vector<std::string> memberTypes;
    // ... bestehende Felder ...
    int     version      = 1;
    std::string sourceYaml;
    std::set<std::string> referencedTypes;

    int     packed_size  = 0;   // ← NEU
    int     padded_size  = 0;   // ← NEU (nächste 2er-Potenz von packed_size)
};
```

### 6.3 Padding-Berechnung

In `ast_compiler.cpp`, nach dem Berechnen der LLVM-Struct-Größe:

```cpp
// In visitFunction / Klassen-Registrierung:
int packed = (int)ctx.module.getDataLayout().getTypeAllocSize(ci.type);
ci.packed_size = packed;
// Nächste 2er-Potenz:
int padded = 1;
while (padded < packed) padded <<= 1;
ci.padded_size = padded;
```

### 6.4 Padded Allokation im generierten Code

In `ast_compiler_expr.cpp` / `visitNewExpr`:

```cpp
// Statt: call i8* @malloc(i64 sizeof(Payload))
// Neu:   call i8* @calloc(i64 1, i64 padded_sizeof(Payload))

llvm::Value* padded_sz = ctx.ir_gen->getInt64(ci.padded_size);
llvm::Function* callocFn = getOrEmitCalloc(ctx);
llvm::Value* raw = ctx.builder.CreateCall(callocFn, {ctx.ir_gen->getInt64(1), padded_sz});
```

`calloc` garantiert Zero-Initialisierung → neue Felder in alten Instanzen sind 0.

### 6.5 Append-Only-Validierung in `exec_impl.cpp`

Wenn ein class-Chunk im Bundle mit höherer Version gefunden wird:

```cpp
bool validate_append_only(AimlClassSchema* cur, YAML::Node new_decl) {
    // Prüfe, dass alle bestehenden Felder unverändert und in gleicher Reihenfolge:
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
    return true;  // neue Felder am Ende sind ok
}
```

Schema-Registry nach erfolgreicher Validierung updaten:

```cpp
// Neue AimlClassSchema in g_schema_registry eintragen:
// (wird für padded_size bei zukünftigen Allokierungen und für
//  die nächste Append-Only-Validierungsrunde genutzt — KEINE Feldoffsets)
update_schema_registry(cur->name, new_decl, new_version);
```

---

## 7. Zu ändernde Dateien

### Neue Dateien

| Datei | Inhalt |
|-------|--------|
| `src/core/fptable.h` | `emitFptable()` Deklaration + `AIML_MAX_VERSIONS` |
| `src/core/fptable.cpp` | fptable-Globals emittieren |
| `src/core/schema_registry.h` | `AimlFieldInfo`, `AimlClassSchema`, `emitSchemaRegistry()` |
| `src/core/schema_registry.cpp` | Schema-Registry-Globals emittieren |

### Geänderte Dateien

| Datei | Änderung |
|-------|---------|
| `src/core/context.h` | `FunctionInfo::fptable_index`, `ClassInfo::packed_size`, `ClassInfo::padded_size` |
| `src/core/ast_compiler.cpp` | `registerTypeDecl`: fptable_index vergeben; Ende von `compile()`: `emitFptable()` + `emitSchemaRegistry()` aufrufen; Padding-Berechnung in Klassen-Registrierung |
| `src/core/ast_compiler_call.cpp` | `visitCall`: indirekter Call via `__aiml_fptable_current[idx]` statt direktem `CreateCall(fi->func, ...)` |
| `src/core/ast_compiler_expr.cpp` | `visitNewExpr`: `calloc(1, padded_size)` statt `malloc(packed_size)` |
| `exec_impl.h` | `aiml_fptable_find`, `aiml_fptable_update` exportieren |
| `exec_impl.cpp` | `cache_fptable_globals()`, `aiml_fptable_find()`, `aiml_fptable_update()`, `compileYamlChunk()`, Hot-Reload-Logik in `aiml_exec_code` |
| `compiler.cpp` | `jitRun()`: nach `aiml_set_active_context` auch `cache_fptable_globals()` via `aiml_set_active_context` (oder direkt) |
| `CMakeLists.txt` | `fptable.cpp`, `schema_registry.cpp` zu Sources hinzufügen |

---

## 8. Hot-Reload-Flow (Gesamtbild)

```
sys.exec(pkg) aufgerufen
         │
         ▼
aiml_exec_code(code_obj)
         │
         ├── cache_fptable_globals()   [einmalig]
         │
         ├── für jeden code_chunk:
         │     YAML parsen → Name + Version lesen
         │     │
         │     ├── "def":
         │     │    bundle_version ≤ ctx.functions[name].version?
         │     │      ├── Ja  → LOG_INFO("skip") → nächster Chunk
         │     │      └── Nein → compileYamlChunk(chunk, name)
         │     │                  → aiml_fptable_find(name) → idx
         │     │                  → aiml_fptable_update(idx, version, addr)
         │     │                  → ctx.functions[name].version = version
         │     │
         │     └── "class" / "model":
         │          bundle_version ≤ ctx.classes[name].version?
         │            ├── Ja  → LOG_INFO("skip")
         │            └── Nein → validate_append_only(cur_schema, new_decl)
         │                        ├── Fehler → LOG_ERROR → return -1
         │                        └── OK    → update_schema_registry(...)
         │                                    ctx.classes[name].version = version
         │
         ├── data_chunks: LOG_WARNING (zurückgestellt)
         │
         └── start_call ausführen (wie bisher: getFunctionAddress + Funktionszeiger)
```

---

## 9. Tests

### `tests/test_hot_reload.aiml`

```aiml
# Definiere worker v1:
- def:
    name: hr_worker
    version: 1
    return: i32
    body:
      - return: 1

# Paketiere + führe v1 aus:
- decl: [pkg1, Code]
- set: [pkg1, {call: [sys.bundle, "hr_worker"]}]
- decl: [rc, i32]
- set: [rc, {call: [sys.exec, pkg1]}]
- print: ["v1 rc: ${rc}"]    # erwartet: v1 rc: 1

# Definiere worker v2 (hot-reload):
- def:
    name: hr_worker
    version: 2
    return: i32
    body:
      - return: 2

# Bundle mit v2 + exec → Hot-Reload:
- decl: [pkg2, Code]
- set: [pkg2, {call: [sys.bundle, "hr_worker"]}]
- set: [rc, {call: [sys.exec, pkg2]}]
- print: ["v2 rc: ${rc}"]    # erwartet: v2 rc: 2

# Noch einmal mit v1-Bundle → SKIP (version ≤ current):
- set: [rc, {call: [sys.exec, pkg1]}]
- print: ["v1 skip rc: ${rc}"]  # erwartet: v1 skip rc: 2 (weiterhin v2 aktiv)
```

---

## 10. Implementierungsreihenfolge

```
Phase 1 — Datenstrukturen (kein Build-Break)
  1. context.h: FunctionInfo::fptable_index, ClassInfo::packed/padded_size
  2. fptable.h / fptable.cpp: emitFptable() — erst mal leer/stub

Phase 2 — fptable Emission
  3. ast_compiler.cpp: registerTypeDecl → fptable_index vergeben
  4. fptable.cpp: vollständige Emission implementieren
  5. ast_compiler.cpp: compile() → emitFptable() am Ende
  → Build + alle bestehenden Tests müssen noch laufen (Call-Sites noch direkt)

Phase 3 — Indirekte Calls
  6. ast_compiler_call.cpp: visitCall → indirekter Call via fptable_current
  → Build + Tests: Funktionsverhalten identisch, nur indirekt

Phase 4 — Schema-Registry + Padding
  7. schema_registry.h / .cpp: AimlClassSchema, emitSchemaRegistry()
  8. ast_compiler.cpp: Padding-Berechnung + emitSchemaRegistry()
  9. ast_compiler_expr.cpp: calloc statt malloc
  → Build + Tests: identisches Verhalten, Structs größer aber korrekt

Phase 5 — Hot-Reload in exec_impl.cpp
  10. exec_impl.h/.cpp: cache_fptable_globals, aiml_fptable_find/update, compileYamlChunk
  11. exec_impl.cpp: Hot-Reload-Logik in aiml_exec_code (statt SKIP + WARNING)
  12. tests/test_hot_reload.aiml schreiben
  → exodus build + bash run_all_tests.sh → ≥33/33 ✓
```

---

## 11. JIT-Modus vs. AOT-Modus — Implementierungsunterschiede

### 11.1 Übersichtstabelle: context × Ausführungsart

| | **JIT** (`aiml -r file.aiml`) | **AOT** (`exodus build` → Binary) |
|---|---|---|
| `g_active_ee` | ✓ valide | ✗ `nullptr` |
| **context=false** | Shared LLVM-Engine: `addModule` zu laufender Engine; fptable via `getGlobalValueAddress` | Kein shared LLVM-State möglich; fptable via `dlsym`; neuer Code in frischer Engine |
| **context=true** | Frischer isolierter LLVMContext + Engine; keine fptable-Verbindung zum Host | Identisch zu JIT context=true; kein Unterschied |
| fptable-Zugriff | `g_active_ee->getGlobalValueAddress(name)` | `dlsym(RTLD_DEFAULT, name)` + `-rdynamic` |
| Neuen Code kompilieren | `addModule(new_mod)` → zu laufender Engine | Frische `EngineBuilder`-Engine (LLVM muss gelinkt sein) |
| Existierende Funktionen aufrufbar? | ✓ (gleiche ExecutionEngine) | ✓ via `LoadLibraryPermanently(nullptr)` + `-rdynamic` |
| Hot-Reload in fptable | ✓ `aiml_fptable_update` über Engine-Adresse | ✓ `aiml_fptable_update` über `dlsym`-Adresse |
| LLVM-Laufzeit-Abhängigkeit | bereits vorhanden | Binary muss LLVM linken (oder `libaiml`) |

### 11.2 `cache_fptable_globals()` — zwei Pfade

```cpp
static void cache_fptable_globals() {
    if (g_fptable_current) return;  // bereits gecacht

    if (g_active_ee) {
        // ── JIT-Modus ────────────────────────────────────────────────────────
        auto ga = [](const char* nm) -> uint64_t {
            return g_active_ee->getGlobalValueAddress(nm);
        };
        g_fptable_current  = (void**)      ga("__aiml_fptable_current");
        g_fptable          = (void***)     ga("__aiml_fptable");
        g_fptable_versions = (int32_t*)    ga("__aiml_fptable_versions");
        g_func_names       = (const char**)ga("__aiml_func_names");
        g_func_count       = *(int32_t*)   ga("__aiml_func_count");
    } else {
        // ── AOT-Modus ────────────────────────────────────────────────────────
        // Binary muss mit -rdynamic gelinkt sein, damit eigene Globals sichtbar sind.
        g_fptable_current  = (void**)      dlsym(RTLD_DEFAULT, "__aiml_fptable_current");
        g_fptable          = (void***)     dlsym(RTLD_DEFAULT, "__aiml_fptable");
        g_fptable_versions = (int32_t*)    dlsym(RTLD_DEFAULT, "__aiml_fptable_versions");
        g_func_names       = (const char**)dlsym(RTLD_DEFAULT, "__aiml_func_names");
        auto* cnt          = (int32_t*)    dlsym(RTLD_DEFAULT, "__aiml_func_count");
        g_func_count       = cnt ? *cnt : 0;
    }
}
```

### 11.3 `compileYamlChunk()` — zwei Pfade

```cpp
static uint64_t compileYamlChunk(const char* yaml, const std::string& funcName) {
    // ── Gemeinsam: YAML → AST → IR ───────────────────────────────────────────
    YAML::Node node = YAML::Load(std::string(yaml));
    auto* prog = AIML::AST::Builder::buildChunk(node);
    if (!prog) return 0;

    auto new_mod = std::make_unique<llvm::Module>("hr_chunk", g_active_context->llvmCtx);
    llvm::IRBuilder<> builder(g_active_context->llvmCtx);
    CompilerContext chunk_ctx(g_active_context->llvmCtx, *new_mod, builder);
    chunk_ctx.classes = g_active_context->classes;  // Typen übernehmen
    chunk_ctx.enums   = g_active_context->enums;
    AIML::AST::Compiler compiler(chunk_ctx);
    compiler.compile(prog);

    if (g_active_ee) {
        // ── JIT-Modus: Modul zu laufender Engine hinzufügen ──────────────────
        // Gleicher LLVMContext → Struct-Typen kompatibel, Symbole direkt aufrufbar.
        g_active_ee->addModule(std::move(new_mod));
        g_active_ee->finalizeObject();
        return g_active_ee->getFunctionAddress(funcName);

    } else {
        // ── AOT-Modus: frische ExecutionEngine ───────────────────────────────
        // Prozesssymbole verfügbar machen (malloc, aiml_print_string etc.):
        llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);

        std::string err;
        auto* engine = llvm::EngineBuilder(std::move(new_mod))
            .setErrorStr(&err)
            .setEngineKind(llvm::EngineKind::JIT)
            .create();
        if (!engine) { LOG_ERROR("compileYamlChunk: " + err); return 0; }
        engine->finalizeObject();

        uint64_t addr = engine->getFunctionAddress(funcName);

        // Engine am Leben halten (sonst werden JIT-Pages freigegeben):
        g_aot_engines.push_back(engine);   // static std::vector<ExecutionEngine*>

        return addr;
    }
}
```

### 11.4 context=false vs. context=true in `aiml_exec_code`

`context=false` (default): Hot-Reload — bestehende fptable-Einträge werden ersetzt.
`context=true` (isoliert): kein Hot-Reload — neues Code-Objekt läuft vollständig
isoliert, fptable des Host-Programms bleibt unberührt.

```cpp
extern "C" int aiml_exec_code(void* raw) {
    ExecCode* code = static_cast<ExecCode*>(raw);

    if (code->new_context) {
        // ── context=true: isolierter Pfad ────────────────────────────────────
        // Funktioniert gleich in JIT und AOT (immer frische Engine):
        llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
        // ... frischen LLVMContext + Module + Engine aufbauen ...
        // ... code_chunks kompilieren + start_call ausführen ...
        // fptable: NICHT aktualisieren
        return isolated_exec(code);
    }

    // ── context=false: shared / hot-reload Pfad ──────────────────────────────
    // AOT-Modus: fptable via dlsym erreichbar, auch ohne g_active_ee:
    cache_fptable_globals();
    if (!g_fptable_current) return -2;  // weder JIT noch AOT-fptable gefunden

    // code_chunks verarbeiten: validate → compileYamlChunk → aiml_fptable_update
    // ...

    // start_call ausführen:
    if (g_active_ee) {
        // JIT: getFunctionAddress aus laufender Engine
        uint64_t addr = g_active_ee->getFunctionAddress(funcName);
        return ((int32_t(*)())addr)();
    } else {
        // AOT: Adresse aus fptable_current (enthält aktuellsten Zeiger)
        int fidx = aiml_fptable_find(funcName.c_str());
        if (fidx < 0) { LOG_ERROR("function not found: " + funcName); return -1; }
        return ((int32_t(*)())g_fptable_current[fidx])();
    }
}
```

### 11.5 Vollständige Mechanismen-Matrix

| Aspekt | JIT + context=false | JIT + context=true | AOT + context=false | AOT + context=true |
|--------|--------------------|--------------------|--------------------|--------------------|
| fptable-Zugriff | `getGlobalValueAddress` | — (kein fptable-Update) | `dlsym(RTLD_DEFAULT,...)` | — (kein fptable-Update) |
| Neuen Code kompilieren | `addModule` zu `g_active_ee` | frische Engine | frische Engine + `g_aot_engines` | frische Engine |
| Struct-Typ-Kompatibilität | ✓ (gleicher LLVMContext) | ✗ (isoliert) | △ (Typen aus `g_active_context` kopiert) | ✗ (isoliert) |
| Existierende Funktionen | ✓ (gleiche Engine) | ✓ via `LoadLibraryPermanently` | ✓ via `LoadLibraryPermanently` + `-rdynamic` | ✓ via `LoadLibraryPermanently` + `-rdynamic` |
| fptable-Update nach Reload | ✓ | ✗ | ✓ | ✗ |
| Voraussetzung Binary | — | — | `-rdynamic` | `-rdynamic` |
| LLVM zur Laufzeit | bereits vorhanden | bereits vorhanden | muss gelinkt sein | muss gelinkt sein |
| `g_active_ee` | ✓ valide | ✓ valide | `nullptr` | `nullptr` |
| Sinnvoll? | ✓ Hauptfall | ✓ Sandboxing | ✓ mit `-rdynamic` + LLVM | ✓ einfachster AOT-Fall |

---

## 12. Bekannte Einschränkungen dieses Plans

- Neue Funktionen (noch nicht im fptable): `LOG_WARNING` + skip (kein dynamisches Hinzufügen)
- Klassen mit Breaking Changes: `LOG_ERROR` + return -1
- `compileYamlChunk`: Klassen im Haupt-Modul müssen in `chunk_ctx` bekannt sein
  (werden aus `g_active_context->classes` kopiert — Typen kompatibel da gleicher LLVMContext)
- Multi-Thread: `fptable_current`-Update ist atomar; `fptable[idx][vi]`-Write ist es nicht
  → für Multi-Thread: `fptable`-Write vor `fptable_current`-Update mit Memory-Barrier
- `AIML_MAX_VERSIONS = 16`: mehr als 16 Versionen einer Funktion → Overflow-Fehler
- Padding-Overhead: alle Structs mindestens 2× so groß wie gepackt
  → `@noreload`-Annotation für performance-kritische Typen (kein Padding, kein Schema)
