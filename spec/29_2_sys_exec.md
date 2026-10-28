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

**Achtung bei fremdem Code (spec/65) — seit 17.10.2026 UMGESETZT, nicht nur vermerkt:** beide
Zweige sind an einer Vertrauensgrenze nicht zulaessig, wie sie oben stehen, und `tryHotReload`
in `exec_impl.cpp` prueft das jetzt in dieser Reihenfolge:

1. `replace_level` der Sitzung — die Version stammt von dem, der das Bundle geschrieben hat,
   ist also **Eingabe** und keine Berechtigung (spec/65 F65-29).  Fehlt das Recht:
   `clearance-replace`, und der Lauf bricht mit `-3` ab.
2. erst danach der Versionsvergleich.  Der SKIP-Zweig ist in einer eingeschraenkten Sitzung ein
   **Fehler** (`clearance-collision`, spec/65 F65-30), kein Ueberspringen: ein Skript, das sein
   eigenes `helper` definiert, bekaeme sonst stillschweigend das `helper` des Wirts.

Fuer Code aus derselben Quelle — also ohne gesetzte Freigabe — bleibt die Logik oben
unveraendert gueltig; die Pruefungen haengen an `restricted()`.

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

**Zwei Auflagen aus spec/65 fuer den, der §4.3 wirklich baut** (Befund 17.10.2026): heute kann
ein Buendel keinen FREMDEN Namen tragen — `sys.bundle` nimmt den Namen einer Funktion, die der
Wirt selbst uebersetzt hat (§16) —, und darum ist der Pfad „neues Symbol" unerreichbar.  Sobald
er erreichbar wird, gilt:

- Das Anlegen braucht `create_level` (spec/65 F65-26).  Der Waechter `clearance-create` steht
  schon in `tryHotReload` und ist fail closed; er wartet nur auf diesen Pfad.
- Das Level eines angelegten Symbols IST die Freigabe der Sitzung (spec/65 F65-27) — nicht 0
  und nicht ein Wert, den die ankommende Quelle waehlt.  Dieses Kapitel ist die Stelle, an der
  das Level gesetzt wird, also muss es hier passieren; spec/65 kann es nicht nachtraeglich
  anbringen.  Die zugehoerigen Proben sind `tests/clearance/11_create_allowed.aiml` und
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

# Der Kontext haengt am BUENDEL, nicht an der Ausfuehrung: er beschreibt, WIE uebersetzt wird.
- decl: [pkg, Code, { call: [sys.bundle, "user_script", { context: true }] }]

# Die Argumente 2..4 sind die RICHTLINIE der Sitzung (spec/65 F65-26/F65-36), in dieser
# Reihenfolge: clearance, create_level, replace_level.  Sie beschreibt, WER ausfuehrt, und
# gehoert darum hierher und nicht an das Buendel.  Fehlende hintere Werte sind 0 = niemand.
- set: [rc, { call: [sys.exec, pkg, 7] }]         # Freigabe 7, darf nichts schreiben
- set: [rc, { call: [sys.exec, pkg, 7, 7, 0] }]   # ... darf anlegen, nicht ersetzen
```

(Ein frueherer Entwurf dieses Abschnitts stellte ein zweites Argument `true` fuer den
ISOLIERTEN Kontext in Aussicht.  Das ist ueberholt: `context` ist eine Eigenschaft des Codes
und steht an `sys.bundle`; der zweite Platz traegt die Freigabe.)

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

## 12. Isolierter Kontext in einem AOT-Binary — funktioniert

**Richtigstellung.**  §3.1 und §11 lesen sich so, als sei `sys.exec` in einem AOT-Binary nicht
benutzbar, und §3.2 fuehrt den isolierten Kontext als "Phase 2".  Beides ist ueberholt:
`aiml_isolated_exec` ist implementiert und haengt **nicht** an `g_active_context`.

Nachgewiesen mit einem `exodus build`-Binary (kein JIT):

```yaml
- decl: [pkg, Code, { call: [sys.bundle, "worker_task", { context: true }] }]
- decl: [rcx, i32, { call: [sys.exec, pkg] }]      # rc=0, worker_task laeuft
```

```
aiml_isolated_exec: compiled source (160 bytes)
   worker_task laeuft im isolierten Kontext
aiml_isolated_exec: finished with rc=0
```

Der Ablauf: `sys.bundle(name, { context: true })` setzt `new_context = 1`, `aiml_exec_code`
verzweigt sofort nach `aiml_isolated_exec`, das aus den `code_chunks` YAML baut, einen frischen
`AIMLCompiler` anlegt, `compileAndRun` ruft, die Globals des Aufrufers wiederherstellt und beim
Verlassen des Scopes LLVMContext, Module und CompilerContext freigibt.

Die Voraussetzung dafuer ist, dass der Compiler in der Laufzeitbibliothek steckt — er tut es:
`libaiml.so` enthaelt LLVM und exportiert `aiml_exec_code` sowie `aiml_set_active_context`.

**Was weiterhin NICHT geht:** `context = false` in AOT.  Dort sucht `sys.exec` den geteilten
JIT-Kontext, findet keinen und liefert `-2` (§11).  Das ist richtig so — Hot-Reload in die
laufende Sitzung setzt eine Sitzung voraus.

**Was zu messen bleibt:** die Lebensdauer.  Jeder isolierte Lauf baut einen vollstaendigen
LLVM-Kontext auf und wieder ab.  Dass dabei nichts zurueckbleibt, ist plausibel (der Destruktor
raeumt), aber bei einem Agenten, der tausende Schritte ausfuehrt, gehoert es gemessen und nicht
angenommen.

### Befund 16.10.2026: der Objekt-Cache hat im AOT-Fall keinen Versionsschluessel

`aiml_isolated_exec` uebernimmt den Compilerpfad nur, wenn es einen aktiven Kontext gibt:

```cpp
if (g_active_context) iso.compilerPath = g_active_context->compilerPath;
```

Im AOT-Binary ist `g_active_context` immer `nullptr` (§11), der Subcompiler bekommt also **keinen**
`compilerPath`.  Dieser Pfad ist aber die Quelle des Cache-Versionsschluessels:
`compilerBinaryRev()` liest Groesse und mtime der Compiler-Datei und liefert bei leerem Pfad
`"unknown"`, woraufhin `cacheRevDir()` in `rev_unknown` landet.

**Folge:** uebersetzte Objekte eines isolierten Laufs im AOT-Binary liegen unter einem Schluessel,
der **Compiler-Versionen nicht unterscheidet**.  Nach einem `libaiml`-Update kann veralteter Code
wiederverwendet werden — genau die Klasse von Fehler, die in diesem Projekt schon zweimal teuer
war (stale KV-Blobs, stale `__aiml_cache`).

Aufgefallen ist es bisher niemandem, weil `context = true` im AOT praktisch nicht benutzt wird.
Fuer eine Skript-Konsole in einem AOT-Wirt (spec/65) ist es ein Stolperstein, denn dort ist der
isolierte Kontext der **einzige** verfuegbare Pfad.

**Behoben am 17.10.2026.**  Der Schluessel haengt nicht mehr am Compilerpfad, sondern an der
Datei, die den Uebersetzer **tatsaechlich enthaelt**: `dladdr` auf eine Funktion der
betreffenden Uebersetzungseinheit liefert im JIT-Lauf das `aiml`-Binary und im AOT-Binary die
`libaiml.so`; daraus wird wie bisher `<groesse>_<mtime>`.  Gibt auch das nichts her, ist die
Rueckgabe **leer** statt `"unknown"` — `computeImportCacheKey` liefert dann keinen Schluessel,
`checkCache` behandelt das als „kein Treffer", und der Cache **schaltet sich ab** statt einen
gemeinsamen Eimer fuer alle Versionen aufzumachen (fail closed, mit Warnung).

Gemessen im JIT-Lauf: nach einem Neubau heisst das Cache-Verzeichnis `rev_83002216_…` und
entspricht damit Groesse und mtime des neuen Binaries; das Verzeichnis der Vorversion wird
von `sweepCache` geraeumt.  **Noch nicht gemessen** ist der AOT-Zweig: dafuer muesste ein
AOT-Binary ein isoliert uebersetztes Buendel mit einem `import` ausfuehren, denn ohne Import
wird der Cache gar nicht befragt — `tests/bundle_socket` (der vorhandene AOT-Pfad) buendelt
ohne Imports und hat den Fehler deshalb nie ausgeloest.  Der Fehler war also **latent**: kein
`rev_unknown` ist im Baum je entstanden.


## 13. `sys.code_from` — ein Bundle aus Quelltext bauen

`pkg.toString()` verkettet `code_chunks` + `data_chunks` zu **aiml-Quelltext**; `start_call` und
`new_context` sind darin **nicht** enthalten.  Das ist die Drahtform, in der ein Bundle über eine
Verbindung reist (cosmos `doc/spec/96`: der Controller schickt den Schritt an den Agenten).  Bis
hierher fehlte die Gegenrichtung: ein Empfänger konnte aus dem Text kein `Code` mehr machen.

```
sys.code_from(src: string [, { start: "<fn>", context: true }]) → Code
```

| Option | Vorgabe | Bedeutung |
|---|---|---|
| `start` | `""` | Funktion, die nach dem Übersetzen gerufen wird (wird `start_call`) |
| `context` | `false` | `true` = isolierter Kontext — **das einzige, was in einem AOT-Binary geht** (§11) |

```yaml
# Sender
- decl: [pkg, Code, { call: [sys.bundle, "worker_task"] }]
- decl: [wire, string, { call: [pkg.toString] }]

# Empfaenger
- decl: [job, Code, { call: [sys.code_from, wire, { start: "worker_task", context: true }] }]
- decl: [rc, i32, { call: [sys.exec, job] }]
```

Die Optionen spiegeln `sys.bundle` absichtlich: `context` heisst hier dasselbe wie dort, und ein
Empfaenger, der in einem AOT-Binary laeuft, muss `context: true` setzen — sonst sucht `sys.exec`
einen aktiven JIT-Kontext, den es dort nicht gibt, und liefert `-2`.

**Was NICHT mitreist:** der Absender-Zustand.  Der Text enthaelt Definitionen und
Daten-Initialisierung, nicht den Heap des Senders.  Ein Bundle ist Code, kein Prozessabbild.

## 14. Eine Bibliothek laden und Pipelines dagegen fahren

Nachgewiesen mit `tests/bundle_socket/` (zwei **AOT**-Binaries, Bundle über TCP):

```
2) Bibliothek: 110 Bytes   Pipeline: 230 Bytes
   lib_greet aus der Bibliothek
   run_pipeline rief lib_greet, bekam 7     → rc=0
```

Der Weg ist `Code.merge`: Bibliothek und Pipeline werden zu **einem** Bundle verschmolzen und in
**einem** `sys.exec` ausgeführt.  Die Reihenfolge zählt — `start_call` erbt vom **ersten**
Operanden, also muss die Pipeline links stehen (`pipe.merge(lib)`), sonst wird die Bibliothek
gestartet.

**Was damit NICHT geht, und das ist die eigentliche Einschränkung:** eine Bibliothek *einmal*
laden und danach mehrere Pipelines dagegen fahren.  Jeder `sys.exec` mit `context: true` legt
einen **frischen** `AIMLCompiler` an, übersetzt, läuft und räumt ihn beim Verlassen des Scopes
wieder ab — zwischen zwei Aufrufen bleibt nichts stehen.  `sys.load_bundle`, das genau dafür
gedacht wäre, ist nach §9 zurückgestellt.

Folge: ein Agent, der 500 Schritte gegen dieselbe Bibliothek fährt, übersetzt sie **500-mal mit**.
Funktional richtig, aber bei einer großen Bibliothek der dominierende Posten.

### 14a `ctx` — der Kontext als eigener Namensraum (Entscheid 25.10.2026)

Vorgabe des Nutzers: *„Wir legen lieber einen neuen Namensraum an, den wir `ctx` nennen.  Und aus
diesem Raum können wir quasi eine Compiler-Instanz beziehen, darin Code ausführen und auch auf
die Symbole dieser Ausführung zugreifen."*

```yaml
- decl: [cx, ctx_handle, { call: [ctx.new] }]          # eine Compiler-Instanz
- decl: [n_lib, i32, { call: [cx.exec, lib_bundle] }]  # Bibliothek EINMAL
- decl: [r_a,   i32, { call: [cx.exec, pipe_a] }]      # sieht die Bibliothek
- decl: [r_b,   i32, { call: [cx.exec, pipe_b] }]      # auch
- decl: [zahl,  i32, { call: [cx.symbol, "zaehler", "i32"] }]       # Wert, MIT Typ
- decl: [adr,   i64, { call: [cx.symbol, "lib_greet", "fn(string) -> i32"] }]  # Funktion: MIT Signatur
```

- **F29_2-…a: `ctx` ist ein NAMENSRAUM, kein zweites `sys`.**  `sys.exec` bleibt, was es ist: ein
  Bundle einmal ausführen.  `ctx.*` ist der Fall, in dem der Kontext **länger lebt als der
  Aufruf** — und das ist ein anderer Umstand, kein Zusatzargument.  Wer beides in `sys` legt,
  bekommt eine Funktion mit einem Schalter, der die Lebensdauer ändert; das ist die Art Schalter,
  die man später nicht mehr los wird.
- **F29_2-…b: Der Kontext ist eine EINDEUTIGE RESSOURCE.**  Derselbe Typ Wert wie `process` oder
  `thread_handle` (spec/61 §2.1): move-only, und beim Drop des Besitzers wird die Engine
  freigegeben.  Nicht kopierbar — zwei Besitzer einer JIT-Engine wären zwei, die sie freigeben.
- **F29_2-…c: Der Grund ist die Grundlast, nicht die Eleganz.**  Gemessen (§15): **~96 ms** je
  isoliertem Lauf — LLVMContext, Modul, Einstiegspunkt, Engine —, unabhängig davon, wie wenig er
  tut.  Für einen Agenten mit 500 Schritten sind das ~50 Sekunden, bevor ein einziger Befehl
  läuft.  `ctx` macht daraus **einmal** Aufbau und 500-mal Ausführen.  Das ist der ganze Zweck;
  wäre die Grundlast 1 ms, bräuchte es `ctx` nicht.
- **F29_2-…d: Beim Abgreifen wird der Typ GENANNT — und zur Laufzeit geprüft.**
  (Vorgabe des Nutzers, 25.10.2026.)  `cx.symbol(name, typ)` nimmt den erwarteten Typ als
  zweites Argument: `"i32"`, `"string"`, ein Modellname, oder — bei einer Funktion — ihre
  **volle Signatur** (F29_2-…d3).  Geprüft wird **immer zur Laufzeit**: der Kontext hat das Symbol selbst
  übersetzt und kennt seinen Typ; stimmt er nicht mit dem genannten überein, **wird geworfen**
  (spec/22).  Nur dann — ein passendes Symbol kommt wortlos zurück.
  Damit fällt die frühere Einschränkung „Werte nicht": ein Wert hinter einer Adresse hat ohne
  Typ keine Bedeutung, aber mit genanntem **und geprüftem** Typ hat er eine.  Der Leser sagt,
  was er erwartet, und erfährt es, wenn er sich irrt — statt Bytes falsch zu deuten.
- **F29_2-…d4: Wo der Übersetzer es SCHON WEISS, sagt er es sofort.**  (Nachtrag des Nutzers:
  *„der Compiler müsste ja den Datentyp kennen und kann vergleichen, ob das, was programmiert
  wurde, und das, was gerade kompiliert wurde, übereinstimmt."*)  Richtig — aber nur in einem
  der beiden Fälle, und der Unterschied ist, WANN übersetzt wird:

  | Wer liest das Symbol | Was der Übersetzer in dem Moment weiß | Prüfung |
  |---|---|---|
  | Ein **Bundle im selben Kontext**, übersetzt **nachdem** das definierende Bundle lief | beides: den genannten Typ und den des Symbols in seiner Tabelle | **zur Übersetzungszeit** — Fehler sofort, kein Lauf |
  | Das **Wirtsprogramm** (AOT gebaut, z. B. cosmosd) | nur den genannten Typ; der Kontext existiert zu diesem Zeitpunkt noch nicht | **erst zur Laufzeit** |

  Also: der Kontext prüft beim Übersetzen eines Bundles, was er prüfen kann, und wirft sonst
  beim Zugriff.  Die Laufzeitprüfung bleibt **Pflicht** — sie ist die einzige, die es in beiden
  Fällen gibt; die frühe ist der Gewinn und nicht der Ersatz.  Eine Zusage, die nur manchmal
  greift, darf nicht die sein, auf die man sich verlässt.
- **F29_2-…d2: Zwei Fehler, zwei Ausnahmen.**  `SymbolMissingException` — der Kontext kennt
  diesen Namen nicht (nichts hat ihn definiert, oder ein Tippfehler).  `SymbolTypeException` —
  der Name ist da, der Typ passt nicht; die Meldung nennt **beide** Typen (erwartet / gefunden),
  denn ein „Typfehler" ohne die zwei Namen zwingt zum Suchen.  Beide erben von `Exception` und
  sind mit `catch` fangbar wie jede andere.
- **F29_2-…d3: Bei einer Funktion wird die VOLLE SIGNATUR genannt.**  (Vorgabe des Nutzers,
  25.10.2026: *„Auch wenn man Funktionssymbole abruft, muss man bitte die vollständige Signatur
  angeben."*)  Nicht `"fn"`, sondern `"fn(i32,string) -> i32"`: die Argumenttypen in ihrer
  Reihenfolge, dann der Rückgabetyp — `void`, wenn es keinen gibt, `fn() -> i32` ohne Argumente.
  Der Kontext hat die Funktion selbst übersetzt und kennt beides (`FunctionInfo::argTypeNames`
  und `returnTypeName`), also wird beides verglichen; Leerzeichen zählen dabei nicht, so dass
  `"fn(i32, string) -> i32"` und `"fn(i32,string)->i32"` dasselbe sagen.

  Der Grund ist, dass hier das Gegenteil der sonstigen Lage gilt: ein Wert mit falsch genanntem
  Typ wird falsch gedeutet, aber eine **Adresse** mit falscher Signatur zu rufen gibt überhaupt
  keinen Fehler — sie gibt einen kaputten Stapel, und zwar irgendwo später.  Ein Typname, der
  genau an der gefährlichsten Stelle nichts prüft, ist schlimmer als keiner, weil er beruhigt.
  Ein blankes `"fn"` ist deshalb ein Typfehler wie jeder andere (`SymbolTypeException`), und
  die Meldung sagt ausdrücklich, dass die Signatur fehlt.

  Was die Signatur **nicht** nennt: die Lebensdauer-Qualifizierer der Argumente
  (`consumed`/`persistent`, spec/61 §3.1).  Sie stehen in derselben Tabelle und wären
  prüfbar, gehören aber zum Aufruf-**Vertrag** und nicht zum Typ; da fast jedes Argument das
  stillschweigende `scope` trägt, würde jede Nennung einer Signatur um Rauschen wachsen, das
  in fast allen Fällen nichts unterscheidet.  Wird ein `consumed`-Argument über eine
  `ctx`-Adresse gerufen, bleibt der Besitz-Übergang also Sache des Aufrufers.
- **F29_2-…e: `ctx` ist KEINE Sicherheitsgrenze.**  Der Kontext teilt Prozess, Adressraum und die
  dynamische Symboltabelle mit seinem Wirt; `DynamicLibrary::AddSymbol` hat keine Sicht je
  Kontext.  Für fremden Code bleibt die Grenze ein **eigener Prozess** (spec/65 F65-15,
  `doc/analysis/symbol_clearance_2026-10-16.md`).  `ctx` trennt **Namen**, nicht **Rechte** — wer
  das verwechselt, hält eine Bequemlichkeit für einen Schutz.
- **F29_2-…f: Was der Kontext sieht, ist die Reihenfolge seiner Aufrufe.**  Bundle 2 sieht, was
  Bundle 1 definiert hat; mehr nicht.  Keine Rückwärts-Sicht, kein impliziter Neuaufbau.  Damit
  ist `ctx.exec` nachvollziehbar wie eine Liste von Anweisungen — und nicht wie ein Zustand, der
  irgendwann irgendwie entstanden ist.

- **F29_2-…g: Der Kontext führt seine EIGENE Typtabelle — weil der Übersetzer seine wegwirft.**
  `compileFromString` zerstört und baut den `CompilerContext` bei **jedem** Aufruf neu; die lebende
  Tabelle kennt also nur, was das **letzte** Bundle übersetzt oder deklariert hat.  Gemessen
  (25.10.2026): nach drei Bundles war `lib_summe` „kennt dieser Kontext nicht", obwohl sein Bundle
  mit 0 gelaufen war — sichtbar blieb nur, was das dritte Bundle zufällig mitdeklarierte.  Das
  widerspricht F29_2-…f, und zwar nicht sichtbar, sondern **zufällig**: dieselbe Abfrage trägt
  oder trägt nicht, je nachdem, was danach lief.  Deshalb schreibt `aiml_ctx_record_symbols` nach
  **jedem** `ctx.exec` Namen → Typ in eine Tabelle AM KONTEXT fort (später definiert gewinnt,
  §4 Neuladen), und `cx.symbol` liest zuerst dort.  Die Adresse holt weiterhin die Engine
  (`symbolAddress`, neueste zuerst) — ein Name in der Tabelle ohne Adresse heißt „deklariert, nie
  definiert" und wird als **fehlendes** Symbol gemeldet, nicht als Typfehler.
- **F29_2-…h: Ein vom Übersetzer gelegter Wurf notiert seine Meldung wie ein `throw` in aiml.**
  Die oberste Ebene (`jitRun`) berichtet die Begründung einer ungefangenen Ausnahme aus
  `aiml_note_thrown_message`; `emitThrowMsg` rief das nicht, also stand dort „(no message)" —
  für **jede** eingebaute Ausnahme, nicht nur die von `ctx`.  Behoben; `tests/test_ctx_unhandled.aiml`
  hält es fest (Lauf endet mit 2 **und** nennt erwartet/gefunden).

**Stand der Umsetzung** (25.10.2026 — umgesetzt und durch `tests/test_ctx.aiml` belegt):

| Stück | Lage |
|---|---|
| `AIMLCompiler` mehrfach benutzbar | **da** — `execInContext` (`compiler.cpp`): Modul neu binden, frühere Quellen als Deklarationen voranstellen, Engine behalten (`ctx_engines_`), Funktionsadressen per `AddSymbol` veröffentlichen |
| Kontext-Registry + Wert-Typ | **da** — `src/sys/ctx_runtime.cpp` hält die Instanzen, `ctx_handle` ist der Griff |
| `ctx.*` im Builtin-Register | **da** — `ctx.new`, `cx.exec`, `cx.symbol`, `cx.close` (`src/core/builtin_registry.cpp`) |
| `cx.symbol` | **da** — Typtabelle je Kontext (F29_2-…g), Prüfung samt `SymbolMissingException`/`SymbolTypeException`, Funktionen mit **voller Signatur** (F29_2-…d3) |
| `ctx_handle` als eindeutige Ressource | **offen** — move-only und Freigabe beim Drop des Besitzers (F29_2-…b) fehlen noch; heute gibt nur `cx.close` die Engine frei |
| Frühe Prüfung beim Übersetzen (F29_2-…d4) | **offen** — heute prüft nur die Laufzeit.  Das ist die Pflichtprüfung und deckt beide Fälle; die frühe bleibt der Gewinn, nicht der Ersatz, und wird hier als fehlend geführt statt als zugesagt |
| Aufruf einer `fn`-Adresse aus aiml | **offen** — `cx.symbol` liefert die Adresse, aiml hat noch keinen Weg, sie zu rufen; mit F29_2-…d3 ist die Signatur dafür jetzt geprüft vorhanden |

**Wie cosmos es benutzen würde:** ein Kontext **je Agent**, beim Start die Bibliothek hinein,
danach jeder Schritt als Bundle dagegen.  Das ist genau der Satz, der heute schon als Kommentar
in `exec_impl.cpp` steht — „ein Agent lädt beim Start seine Bibliothek und fährt danach Schritte"
— nur dass es dafür bisher keinen Wert gibt, an dem der Kontext hängt.

### Offen: wiederverwendbarer Kontext als aiml-Wert  *(überholt durch §14a)*

Der Entwurf, aus dem §14a wurde — hier belassen, weil er die Begründung trägt.  Der Ausweg wäre,
die Compiler-Instanz nicht mehr lokal zu halten, sondern als **eindeutige Ressource**
sichtbar zu machen — derselbe Typ Wert wie `process` oder `thread_handle` (spec/61
§2.1): move-only, beim Besitzer-Drop freigegeben.

```yaml
- decl: [ectx, exec_context, { call: [sys.context_new] }]
- decl: [r_lib,  i32, { call: [ectx.exec, lib_bundle] }]   # einmal
- decl: [r_one,  i32, { call: [ectx.exec, pipe_a] }]       # sieht die Bibliothek
- decl: [r_two,  i32, { call: [ectx.exec, pipe_b] }]
```

Das Hindernis ist nicht die Sichtbarkeit, sondern dass `AIMLCompiler` heute **einmalig** ist:
`compileAndRun` ruft `jitRun`, und das macht `EngineBuilder eb(std::move(module_))` — das Modul
ist danach weg, ein zweiter Lauf hätte nichts mehr, worin er übersetzen könnte.  Zwei Entwürfe:

1. **Eine Engine, frisches Modul je Bundle**, `addModule` hinein; der `CompilerContext` bleibt
   stehen, damit Bundle 2 die Symbole von Bundle 1 sieht.  Das ist, was "Bibliothek laden" meint.
2. **Je Bundle eine eigene Engine**, Adressen behalten und über die fptable erreichbar machen —
   im Ansatz vorhanden (`compileChunkAndGetAddress` plus der Zeiger-Tausch des Hot-Reload-Pfads).

Dazu ein Befund an §9: die Zurückstellung von `sys.load_bundle` ist dort mit "braucht ORC JIT"
begründet.  Der vorhandene geteilte Pfad benutzt aber gar kein `addModule`, sondern übersetzt den
Chunk in eine **eigene** Engine und tauscht einen Zeiger.  Für Entwurf 2 trifft die Begründung
also nicht zu; für Entwurf 1 wäre sie an MCJIT zu prüfen statt aus der Spec zu übernehmen.

### Entwurf 3 (vermutlich der richtige): Objekt-Cache statt Übersetzen

Die Frage "lazy oder eager" ist für einen Build-Agenten die falsche Achse.  Beide Antworten
übersetzen dieselbe Bibliothek wieder und wieder; sie verschieben nur, **wann** es wehtut.

LLVM hat dafür `llvm/ExecutionEngine/ObjectCache.h` — von MCJIT (`setObjectCache`) wie von ORC
nutzbar: das übersetzte **Objekt** wird behalten und beim nächsten Mal **geladen statt
übersetzt**, auch über Prozessgrenzen hinweg.

Der Schlüssel dafür liegt bereits vor: der Hash des Bundles (`AimlCode.id`, FNV-1a über alle
`code_chunks`).  Gleiches Bundle → gleiches Objekt.  Das ist dieselbe Mechanik wie der
Schritt-Cache einer Pipeline (cosmos `doc/spec/96`), nur eine Ebene tiefer, und sie unterliegt
derselben Regel: der Schlüssel muss **alles** erfassen, was das Ergebnis beeinflusst — hier also
auch Zielarchitektur und LLVM-Version, sonst lädt ein Knoten ein Objekt, das nicht zu ihm passt.

Damit wird aus "500 Schritte, 500-mal die Bibliothek übersetzen" ein einmaliges Übersetzen und
499-mal Laden — **ohne** dass ein wiederverwendbarer Kontext (Entwurf 1/2) dafür nötig wäre.
Die beiden Entwürfe bleiben trotzdem sinnvoll, aber für einen anderen Zweck: sie sparen das
Laden **und** erlauben, dass Bundle 2 die Symbole von Bundle 1 sieht.

### Zur Einordnung von "lazy"

ORCs Faulheit ist eine **eigene Schicht**, keine Eigenschaft: `CompileOnDemandLayer` und
`LazyReexports` muss man einbauen.  Ohne sie übersetzt `IRCompileLayer` bei der **Symbolsuche**,
und ein `lookup()` direkt nach dem Hinzufügen erzwingt das sofort.  "Erst beim ersten Aufruf" ist
also wählbar, nicht aufgezwungen — und MCJIT, das immer alles sofort übersetzt und nichts behält,
ist für wiederholte Läufe die teurere Variante.

### 14b Was ein Wirt mit einem Kontext tun kann: nennen, rufen, Fehler lesen (25.10.2026)

§14a gibt einem Wirt einen Kontext, einen Weg Code hineinzugeben und eine geprüfte Adresse heraus.
Für den Fall, um den es geht — ein **Kontroller** nimmt fremden Quelltext an, lässt ihn übersetzen
und arbeitet mit dem Ergebnis (cosmos `doc/spec/96`) — fehlen drei Stücke, und jedes fehlende
zwingt den Wirt zurück zum Selberlesen des Quelltexts:

```yaml
- decl: [kontext, ctx_handle, { call: [ctx.new] }]
- decl: [code, Code, { call: [sys.code_from, eingereicht] }]
- decl: [rcx, i32, { call: [kontext.exec, code] }]
- if:                                         # ging nicht: WAS war falsch?
    condition: { lt: [rcx, 0] }
    then:
      - decl: [why, list<string>, { call: [kontext.errors] }]
      - return: { at: [why, 0] }
# Wie heisst die Pipeline darin?  Der Kontext weiss es — der Wirt muss nicht suchen.
- decl: [found, list<string>, { call: [kontext.names, "fn() -> pipeline"] }]
- decl: [pipe, pipeline, { call: [kontext.call, { at: [found, 0] }, "fn() -> pipeline"] }]
```

- **F29_2-…i: `cx.names(typ)` — nach Typ fragen, nicht nach Namen raten.**  Gibt die Namen, die
  dieser Kontext mit genau diesem Typ bzw. dieser Signatur trägt, in der Reihenfolge der Tabelle
  (Leerzeichen zählen nicht, §14a F29_2-…d3); keiner passt → leere Liste, kein Fehler.  Der Grund
  ist der Kontroller: er nimmt eine **eingereichte** Pipeline an und kennt ihren Namen nicht.  Ihn
  aus dem Quelltext zu lesen heisst, den Quelltext zu lesen — also genau das, was der Kontext
  übernehmen sollte.  Ein Wirt, der nur `cx.symbol` hat, muss raten oder parsen; mit `cx.names`
  fragt er.
- **F29_2-…j: `cx.call(name, signatur)` ruft, was der Kontext übersetzt hat.**  Erst wird die
  Signatur geprüft (dieselbe Prüfung wie `cx.symbol`, F29_2-…d3), dann gerufen; der Rückgabewert
  hat den Typ, der in der Signatur steht.  **Heute ohne Argumente** (`fn() -> T`) mit
  `T ∈ {void, i32, i64, string, eingebaute Griffklasse}`: das ist der Fall, der gebraucht wird —
  eine deklarierte Pipeline ist `fn() -> pipeline` (spec/62 F62-108) —, und jeder weitere
  Parameter braucht die Argumentumwandlung, die ohne Nutzer nur Gewicht wäre.  Mehr Argumente
  kommen, wenn ein Fall sie verlangt; die Signatur steht dann schon da.
- **F29_2-…k: `cx.symbol` gibt den WERT, wenn der genannte Typ ein Wert ist.**  `i32`, `i64`,
  `string` und die eingebauten Griffklassen kommen als Wert zurück, eine Funktion als Adresse
  (`i64`).  Der genannte Typ entscheidet also nicht nur, ob der Zugriff erlaubt ist, sondern auch,
  was man in der Hand hält — anders herum müsste der Leser aus einer Adresse selbst einen Wert
  machen, und das kann er in aiml nicht, ohne das Typsystem zu umgehen.
- **F29_2-…l: `cx.errors` gehört dem KONTEXT, nicht dem Prozess.**  Die Meldungen des **letzten**
  `cx.exec`, in ihrer Reihenfolge; ein neues `cx.exec` setzt sie zurück.  Zwei Gründe: `-3` heisst
  „ging nicht" und ist für einen Wirt wertlos, der einem Menschen sagen muss, was falsch war —
  und zwei Kontexte dürfen ihre Meldungen nicht vermischen, sonst erklärt die eine Einreichung
  den Fehler der anderen.  Zurückgesetzt wird, weil sonst der dritte Lauf die Fehler des ersten
  als seine eigenen berichtet.
- **F29_2-…n: `cx.load` übersetzt und hängt ein, OHNE zu laufen.**  `cx.exec` ruft den
  Einstiegspunkt des Bundles — bei **eingereichtem** Quelltext wäre das fremder Code im
  Controller-Prozess, und genau das verbietet cosmos F96-28.  `cx.load` tut alles außer dem
  Aufruf: übersetzen, Meldungen sammeln, Symbole bekannt machen.  Danach ruft der Wirt **genau
  die eine** Funktion, die er wollte (`cx.call`), und nichts sonst.  Der Unterschied ist nicht
  Vorsicht, sondern Zuständigkeit: ein Rumpf gehört zum Agenten, nicht zum Kontroller.
- **F29_2-…m: Was ein Wirt NICHT bekommt.**  Keine AST-Knoten, keine LLVM-Typen, keinen Zeiger in
  die Tabellen des Übersetzers — nur Namen, Werte, Adressen und Meldungen.  Alles andere bindet
  den Kontroller an den Innenbau des Übersetzers, und dann ist jede Änderung am Übersetzer eine
  Änderung am Kontroller.  Die Grenze ist absichtlich eng: `ctx` ist eine **Schnittstelle**, kein
  Fenster.

**Wofür das zusammen reicht:** ein Kontroller kann eine eingereichte Pipeline übersetzen lassen,
die Meldungen des Übersetzers weitergeben, die Pipeline bauen und sie lesen (spec/62 §20) — ohne
eine einzige Zeile YAML selbst anzufassen.  Das ist der Punkt: die Sprache hat **einen** Leser,
und der steht im Übersetzer.

## 15. `sys.lib_load` — eine Bibliothek einmal übersetzen

```
sys.lib_load(pkg: Code) → i32      # Zahl der registrierten Funktionen, -1 Fehler
```

Der Unterschied zu `merge` (§14) ist nicht die Sichtbarkeit, sondern die **Kosten**: `merge`
übersetzt die Bibliothek bei **jedem** Lauf mit, `lib_load` übersetzt sie **einmal**.

Ablauf:

1. **Laden** (`compileLibraryAndRegister`): Bundle übersetzen, eine Engine erzeugen, für jede
   Funktion ihre Adresse über `llvm::sys::DynamicLibrary::AddSymbol` bekannt machen.  Die Engine
   wird am Compiler **festgehalten** — gibt man sie frei, wird der Code freigegeben und die eben
   registrierten Adressen zeigen ins Leere.
2. **Ausführen** (`sys.exec`, isolierter Kontext): vor dem Bundle werden die gemerkten
   Bibliotheks-Quelltexte im **headerOnlyMode** eingespielt — `visitFunction` trägt die Signatur
   in `ctx.functions` ein und legt die `llvm::Function` an, **überspringt aber den Rumpf**.  Die
   Aufrufe der Pipeline werden damit zu externen Symbolen, die MCJIT über die registrierten
   Adressen auflöst.

Je Lauf bleibt also das Parsen und Deklarieren der Bibliothek; die Codeerzeugung für ihre Rümpfe
entfällt.

### Gemessen: der Vorteil ist nicht nachweisbar — und warum

`tests/test_lib_load.aiml`, Bibliothek aus vier Funktionen, fünf Läufe:

| | Läufe (ms) | Mittel |
|---|---|---|
| `merge` (Bibliothek jedes Mal mit) | 98, 97, 104, 99, 101 | ~100 |
| `lib_load` (einmal geladen) | 93, 94, 97, 95, 104 | ~97 |

**~3 %, und in einem Lauf war `lib_load` langsamer.** Das ist Streuung, kein Vorteil.  Die
Behauptung "spart die teure Übersetzung" gilt in der engen Lesart (die Rümpfe werden nicht erneut
erzeugt), ist aber ohne praktische Wirkung — **bei dieser Bibliotheksgröße**.

Der Grund ist die Zahl, die dabei sichtbar wurde und die wichtiger ist als der Vergleich: **ein
isolierter Lauf kostet rund 96 ms Grundlast** — LLVMContext, Modul, Einstiegspunkt, Engine —
unabhängig davon, wie wenig er tut.  Vier Bibliotheksfunktionen gehen darin unter.

**Folge für cosmos:** 500 Schritte in isolierten Kontexten kosten ~50 Sekunden reine Grundlast,
bevor ein einziger Build-Befehl läuft.  Der Hebel ist damit nicht, die Bibliothek seltener zu
übersetzen, sondern den **Kontext seltener aufzubauen** — also der wiederverwendbare Kontext
(§14, Entwurf 1), für den `CompilerContext::module` jetzt rebindbar ist.  `sys.lib_load` bleibt
nützlich für die **Sichtbarkeit** (eine Pipeline kann eine Bibliothek benutzen, ohne sie
mitzubringen), nicht für die Geschwindigkeit.

### Bekannte Grenze: genau eine Version

`AddSymbol` schreibt in eine **flache, prozessweite** Tabelle und laut LLVM-Header *"permanently
adds the symbol"*.  Daraus folgt beides:

- **Zwei Versionen derselben Bibliothek gehen nicht.**  Ein Name, ein Eintrag — eine Version
  gewinnt, die andere verschwindet **ohne Meldung**.  Dieselbe Fehlerklasse wie ein zu grober
  Cache-Schlüssel: es läuft, und es läuft falsch.
- **Entladen geht nicht.**  "Permanently" ist wörtlich gemeint.

Dazu kommt, dass `compileDeclarationsOnly` **alle** gemerkten Quelltexte in jeden neuen Compiler
einspielt: zwei Versionen deklarieren denselben Namen, die zweite überschreibt
`ctx.functions[name]` — womöglich mit anderer Signatur.

`sys.lib_load` ist deshalb ausdrücklich der **Ein-Versionen-Fall**: ein Agent lädt beim Start
seine Bibliothek und fährt danach Schritte.  Sobald zwei Versionen nebeneinander gebraucht werden
(aiml `spec/62` §16, `runtime: own`), reicht es nicht mehr, und es braucht einen **Symbolraum je
Kontext** — entweder einen eigenen Resolver je Engine (bleibt bei MCJIT) oder ORCs JITDylibs.

### Voraussetzung, die dafür geschaffen wurde

`CompilerContext::module` war eine **Referenz** und ist jetzt ein Zeiger mit `module()` und
`setModule()` (602 Zugriffsstellen umgestellt).  Eine Referenz lässt sich nicht neu binden; damit
konnte ein Kontext nie ein zweites Modul bedienen.  Das ist die Grundlage für mehrere Module in
einem Kontext — benutzt wird sie von `sys.lib_load` noch **nicht**.

### Was in aiml NICHT sichtbar ist

Es gibt weiterhin **keinen** aiml-Datentyp für einen Kontext oder ein Modul.  `sys.lib_load` wirkt
auf eine prozessweite Registry; ein `exec_context` als eindeutige Ressource (wie `process`) wäre
der nächste Schritt und setzt den Symbolraum je Kontext voraus — sonst wäre er ein Besitz ohne
Wirkung.

## 16. Befund: `sys.bundle` ist NICHT transitiv über aufgerufene Funktionen

`spec/30_3` §297 schreibt "(sys.bundle includes dependencies transitively)".  Das gilt für
**Typen**, nicht für **Funktionen**.  `dfsCollect` (`ast_compiler_call.cpp`) folgt ausschließlich
`referencedTypes`; eine Funktion, die eine andere **aufruft**, nimmt sie nicht mit.

```yaml
- def: { name: helper,  return: i32, body: [{ return: 7 }] }
- def: { name: worker,  return: i32, body: [ { decl: [got, i32, { call: [helper] }] }, ... ] }

- decl: [pkg, Code, { call: [sys.bundle, "worker"] }]   # enthaelt helper NICHT
```

Beim Ausführen meldet der isolierte Lauf dann `Unknown function: helper` — erst zur Laufzeit, in
einem Bundle, das längst unterwegs war.

**Folge für cosmos:** ein Pipeline-Schritt, der eine Hilfsfunktion ruft, trägt sie nicht mit.  Wer
ein vollständiges Bundle will, muss die Teile ausdrücklich zusammenführen (`Code.merge`, §14) oder
die Bibliothek getrennt laden (§15).  Das ist heute Handarbeit des Autors und damit eine
Fehlerquelle, die nach einer Prüfung verlangt.

**Entschieden: `dfsCollect` folgt auch Aufrufen.**  Ein Bundle ist damit von sich aus
vollständig — das ist die Eigenschaft, auf der alles andere aufbaut (ein Schritt, der beim
Empfänger `Unknown function` meldet, ist über die Leitung gegangen, ohne benutzbar zu sein).

Erfasst wird dasselbe wie bei Typen: während ein Funktionsrumpf übersetzt wird, trägt
`bundleTrackingTarget` die gerade übersetzte Funktion, und jeder aufgelöste Aufruf einer
**benutzerdefinierten** Funktion wird dort vermerkt (`referencedFuncs`).  Builtins bleiben außen
vor — sie stecken in der Laufzeitbibliothek, nicht im Bundle.

Der Preis ist die Kehrseite der Vollständigkeit: ein Bundle kann groß werden, wenn die gerufene
Funktion ihrerseits weit verzweigt.  Das ist hinnehmbar, weil es **sichtbar** ist (`pkg.size()`)
und weil die Alternative — ein unvollständiges Bundle, das erst beim Empfänger auffällt — die
schlechtere Sorte Überraschung ist.

## 17. Befund: Übersetzungsfehler im Bundle kamen als Erfolg zurück

`aiml_isolated_exec` rief `compileAndRun`, und das übersetzte, erzeugte den Einstiegspunkt und
lief los — **ohne zu prüfen, ob das Übersetzen überhaupt gelungen war**.  `main.cpp` macht diese
Prüfung (`Logger::instance().hasErrors()`), `compileAndRun` nicht.

Beobachtet beim Schreiben von `tests/test_bundle_callees.aiml`:

```
[ERROR] ... Unknown function: mid_level
sys.exec rc=0
```

Das ist die gefährlichste Sorte Fehlverhalten für cosmos: ein Agent hätte einen Schritt als
**erfolgreich** gemeldet, dessen Code nie übersetzt wurde — und der Layer-Cache hätte dieses
„Ergebnis" aufgezeichnet und beim nächsten Lauf ausgeliefert.

**Behoben:** `compileAndRun` merkt sich den Fehlerstand vor dem Übersetzen, vergleicht danach und
gibt `-3` zurück, statt zu laufen.  Der Stand wird *verglichen*, nicht auf null geprüft — ein
langlebiger Agent hat womöglich schon frühere Fehler gezählt, und uns interessieren nur die aus
diesem Bundle.

**Nebenbefund:** `Code.contains(name)` ist eine reine Textsuche (`strstr` über die code_chunks).
Eine Funktion, die `mid_level` nur **aufruft**, lässt `contains("mid_level")` wahr werden, obwohl
die Funktion gar nicht im Bundle ist.  Als Prüfung auf Vollständigkeit taugt `contains` deshalb
nicht — nur ein Lauf taugt dafür.

## 18. Vollständigkeitsprüfung beim Bündeln

Ein Bundle, dem eine gerufene Funktion fehlt, fällt erst beim **Empfänger** auf — als
`Unknown function`, nachdem es über die Leitung gegangen ist.  Für cosmos heißt das: der Schritt
ist beim Agenten, der Agent kann ihn nicht ausführen, und die Ursache liegt beim Absender.

Drei Stellen kämen für eine Prüfung in Frage, und zwei taugen nicht:

| Stelle | Warum sie nicht taugt |
|---|---|
| `Code.contains(name)` | reine Textsuche (`strstr`).  Eine Funktion, die `helper` nur **aufruft**, lässt `contains("helper")` wahr werden, obwohl `helper` nicht im Bundle ist (§17) |
| beim Empfänger | zu spät: das Bundle ist bereits unterwegs, und der Fehler sieht aus wie ein Fehler des Empfängers |
| **beim Bündeln** | der letzte Punkt, an dem der **Autor** es erfährt |

Umgesetzt an der dritten: `dfsCollect` sammelt jeden gerufenen Namen, für den **keine Quelle
hinterlegt** ist, und `sys.bundle` meldet sie:

```
sys.bundle('worker'): unvollstaendig — gerufene Funktion(en) ohne hinterlegte Quelle:
helper.  Der Empfaenger meldet dafuer 'Unknown function'.
```

Zwei Eigenschaften, die dabei beabsichtigt sind:

- Es ist eine **Warnung**, kein Fehler.  Ein Bundle kann absichtlich unvollständig sein — wenn der
  Empfänger die Funktion über eine geladene Bibliothek (§15) schon kennt.  Ein Fehler würde diesen
  gültigen Fall verbieten.
- `--check` sammelt Warnungen ein (`spec/49`).  Die Prüfung ist damit **maschinenlesbar** und
  landet ohne Zusatzarbeit in der Befundliste einer Pipeline-Oberfläche (cosmos `doc/spec/98`).

Was sie **nicht** erfasst: einen Namen, den der Compiler gar nicht kennt — der ist schon vorher ein
Fehler (`Unknown function` beim Übersetzen des Rumpfes).  Die Prüfung greift genau für den Fall
dazwischen: bekannt, aber ohne hinterlegte Quelle.

## 19. Die Zitierung ist Bedeutung, nicht Formatierung

Ein Bundle traegt **Quelltext**.  Dieser Quelltext wird aus dem geparsten
YAML-Knoten wiederhergestellt — und genau dabei ging bis 2026-10-11 die
Zitierung verloren: `YAML::Dump` wirft das yaml-cpp-Tag `!` weg, das nur
zitierte Skalare tragen.

Das ist in aiml kein Schoenheitsfehler, sondern ein Bedeutungswechsel:
`nodeToExpr` (`src/core/ast_builder.cpp`) unterscheidet ein **String-Literal**
von einem **Namen** ausschliesslich an diesem Tag.  Aus

```yaml
      - call: [parts.push, "eins"]
```

wurde beim Buendeln

```yaml
      - call: [parts.push, eins]
```

und der Empfaenger meldete `Unknown Type: 'eins'`.  Betroffen war **jedes**
String-Literal in **jedem** Bundle; aufgefallen ist es erst, als ein
Pipeline-Schritt (`spec/62` §5.6) ein `sleep` an `sys.exe` uebergeben wollte,
weil die vorherigen Buendel-Tests zufaellig keine String-Literale enthielten.

- **F29_2-19** Bundle-Quelltext wird ausschliesslich mit
  `aiml::dumpPreservingQuotes` (`src/core/yaml_dump.h`) erzeugt, nie mit
  `YAML::Dump`.  Der Emitter laesst zitierte Skalare zitiert.  `YAML::Dump`
  bleibt fuer den **Benutzer**-Aufruf `yaml.dump` richtig, wo es um Daten und
  nicht um Quelltext geht — die beiden Faelle sehen gleich aus und sind es
  nicht.
- **F29_2-19a** Ein Test, der Buendelung prueft, enthaelt mindestens ein
  String-Literal **im Rumpf**.  Ohne eines faellt diese ganze Fehlerklasse
  durch jede Pruefung (so geschehen).

## 20. Was ein Bundle einsammelt: auch `new X`

- **F29_2-20** Eine Klasse, die im Rumpf nur in einem `new` vorkommt, gehoert
  ins Bundle.  `visitNewExpr` schlaegt sie direkt in `ctx.classes` nach und
  umgeht damit `resolveType`, wo das Einsammeln haengt (`context.h`) — der
  haeufigste Fall dafuer ist `throw: { new: MeinFehler, message: ... }`, also
  ausgerechnet der Pfad, auf dem ein Schritt scheitert (`spec/62` §5.6).  Der
  Empfaenger meldete `Unknown class for new`.  Aufgezeichnet wird deshalb in
  `visitNewExpr` selbst.

## 21. `toString` serialisiert eine Sequenz

- **F29_2-21** `Code.toString` liefert eine YAML-**Sequenz** (`- def:` …), nicht
  die aneinandergeklebten Chunks.  Jeder Chunk ist ein Mapping auf oberster
  Ebene; zwei aneinandergeklebte ergeben ein Mapping mit zweimal dem Schluessel
  `def:`, und beim Wiedereinlesen ueberlebt nur der letzte.  Ein aiml-Programm
  ist selbst eine Sequenz, also ist das auch die richtige Serialisierung.
  `exec_chunks_to_yaml` baut dieselbe Sequenz — die beiden Wege muessen
  dasselbe liefern, sonst verhaelt sich ein Bundle ueber die Leitung anders
  als eines im Prozess.
- **F29_2-21a** Aufgefallen ist das erst, als §16 (transitive Einsammlung)
  Buendel mit **mehr als einem** Chunk zur Regel machte: der Socket-Test
  schickte `run_pipeline` samt `lib_greet` und verlor `run_pipeline`.  Mit
  einem Chunk war der Fehler unsichtbar — ein Test fuer Serialisierung
  braucht daher mindestens **zwei** Chunks.

## 22. Was ein Bundle vom Wirt sieht — und was nicht

Die Frage stellt sich sofort, wenn eine Pipeline **im Controller** laeuft: kommt ein Schritt-Rumpf
an die Symbole und den Zustand des Wirts?  Gemessen in
`tests/test_bundle_state_boundary.aiml`, drei getrennte Antworten:

| Was | Erreichbar? | Wie |
|---|---|---|
| Eingebaute Funktionen (`io`, `net`, `db`, `sys`, `step`, …) | **ja** | im isolierten Kontext frisch registriert |
| Eine **Funktion** des Wirts | **ja, als Kopie** | ihr Quelltext wird transitiv mitgebuendelt (§16) und neu uebersetzt |
| Globale **Variablen** des Wirts | **nein** | sie werden mitgebuendelt, aber **frisch initialisiert** (§22 unten) |
| Laufender Zustand des Wirts (offene Handles, Caches) | **nein** | ein Bundle traegt Code, kein Prozessabbild (§13) |
| Dateien, Sockets, Datenbank | **ja, geteilt** | derselbe Prozess, dieselbe Maschine |

- **F29_2-22** Eine globale Variable, die eine mitgebuendelte Funktion anfasst, gehoert ins
  Bundle.  Vorher brach genau der haeufigste Fall — eine Hilfsfunktion des Wirts, die einen
  Zaehler oder eine Konfiguration liest — mit `Undefined variable (set target)` ab, und zwar erst
  beim **Empfaenger**.  Aufgezeichnet wird in `resolveVariable` (die einzige Stelle, die Lesen,
  Schreiben und Interpolation gleich behandelt) ueber `Symbol::is_global`; mitgeschickt wird der
  Quelltext des Top-Level-`decl`.
- **F29_2-22a** Mitgeschickt wird die **Deklaration**, nicht der Wert: im Bundle beginnt die
  Globale bei ihrem Initialwert.  Gemessen: der Wirt zaehlte auf 2, im Bundle lieferte dieselbe
  Funktion `1`, und der Zaehler des Wirts blieb unberuehrt.  Das ist die richtige Bedeutung — ein
  Bundle kann auf einem anderen Rechner laufen (`spec/62` §5.8), und ein dorthin kopierter
  Momentanwert waere eine stille Luege ueber geteilten Zustand.
- **F29_2-22b** **Die Trennung ist lexikalisch, keine Sandbox.**  Der Rumpf laeuft im **selben
  Prozess**: `io` schreibt in dieselben Verzeichnisse, `db` in dieselbe Datenbank, `sys.exe`
  startet Prozesse mit denselben Rechten, und MCJIT loest unbekannte Symbole ueber
  `DynamicLibrary::LoadLibraryPermanently(nullptr)` gegen die exportierten Symbole des Prozesses
  auf.  Wer fremden Pipeline-Quelltext ausfuehrt, fuehrt ihn also mit den Rechten des Wirts aus —
  dieselbe Fehlerklasse wie eine Remote-Shell, nicht eine abgeschwaechte.  Isolation im Sinne von
  **Rechten** braucht einen eigenen Prozess (und darunter rootless/userns, Kata oder gVisor), nicht
  einen eigenen `LLVMContext`.

