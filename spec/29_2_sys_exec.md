# sys.exec — Runtime-Ausführung von Code-Objekten

## 1. Übersicht

`sys.exec` führt ein `Code`-Objekt (produziert von `sys.bundle`) zur Laufzeit aus.
Das `context`-Feld des Code-Objekts steuert, ob ein gemeinsamer oder isolierter
Ausführungskontext verwendet wird.

```aiml
- decl: [pkg, Code]
- set: [pkg, { call: [sys.bundle, "worker_task"] }]

- decl: [rc, i32]
- set: [rc, { call: [sys.exec, pkg] }]
- print: ["exit code: ${rc}"]
```

## 2. Code-Struct — Erweiterung um `context`-Feld

Das `__aiml_code_t`-Struct bekommt ein fünftes Feld:

```
__aiml_code_t (5 Felder, alle auf 64-Bit-Grenzen ausgerichtet):
  [0] i8*  id           — FNV-1a Hex-Hash aller code_chunks
  [1] i8*  start_call   — Serialisierter YAML-Call-Node (JSON-String)
  [2] i8*  code_chunks  — Zeiger auf GenericList<i8*> (AIML-Definitionen)
  [3] i8*  data_chunks  — Zeiger auf GenericList<i8*> (AIML-State-Init)
  [4] i8   new_context  — 0 = shared context (default), 1 = isolierter Context
```

`sys.bundle` setzt `new_context = 0` per Default.
Der Nutzer kann es nachträglich setzen (zukünftig via `pkg.set_context(true)`).

## 3. Ausführungsmodi

### 3.1 `context = false` — Shared Context (Standard, Hot-Reload-fähig)

**Nur im JIT-Modus verfügbar.** Im AOT-Modus gibt sys.exec immer -2 zurück
(kein aktiver JIT-Context — siehe Abschnitt 11).

Das Bundle wird in die **laufende** JIT-Session eingeladen:

```
sys.exec(pkg)  [context=false, JIT-Modus]
  → C-Runtime: aiml_exec_code(void* code_obj)
      ↓
  1. g_active_context / g_active_ee prüfen (nullptr → -2)
  2. Für jedes code_chunk (YAML-String):
       Parse → AST-Nodes
       Für jede Declaration:
         a. Name bereits in ctx.functions/ctx.classes?
            → Versions-Check:
              bundle.version <= ctx.version  → SKIP (bereits aktuell)
              bundle.version >  ctx.version  → HOT-RELOAD (Neukompilierung)
         b. Nicht vorhanden → Kompilieren in neues llvm::Module
  3. Neues Module in dieselbe ExecutionEngine einlinken:
       ee->addModule(std::move(newModule))
       ee->finalizeObject()
  4. data_chunks als Statements kompilieren + ausführen
  5. start_call als Statement ausführen (falls nicht leer):
       getFunctionAddress(funcName) → direkt als Funktionszeiger aufrufen
       (kein neues llvm::Module nötig für bereits kompilierte Funktionen)
  6. Rückgabe: i32 Exit-Code
```

**Warum neues Module, aber gleicher LLVMContext?**

MCJIT finalisiert Module bei erster Ausführung. Neue Funktionen müssen in ein
separates `llvm::Module` kompiliert werden, können aber denselben `LLVMContext`
und `ExecutionEngine` teilen — damit sind Host-Funktionen direkt aufrufbar
und Struct-Typen kompatibel.

**Symboltabellen-Prüfung:**

| Lookup-Tabelle | Prüfbedingung |
|----------------|---------------|
| `ctx.functions` | Name + version |
| `ctx.classes` | Name + version |
| `ctx.enums` | Name (Enums haben keine Version) |
| `ctx.interfaceSources` | Name (Interfaces haben keine Version) |

### 3.2 `context = true` — Isolierter Context

**Im JIT-Modus und im AOT-Modus verfügbar** (spätere Phase).

Neuer `LLVMContext` + `Module` + `Compiler` + eigener `ExecutionEngine`.
Keine Verbindung zum Host-Programm. Immer sauber, keine Konflikte.
Nützlich für untrusted Code oder vollständige Sandboxing.

```
sys.exec(pkg)  [context=true, JIT oder AOT]
  → aiml_exec_code():
      Erstellt neuen Compiler (frischer LLVMContext + Module)
      Kompiliert code_chunks vollständig (keine Skip-Logik nötig)
      Kompiliert + führt data_chunks + start_call aus
      Gibt exit code zurück
```

## 4. Versions-Check und Hot-Reload

### 4.1 Versions-Quellen

Klassen und Funktionen in AIML haben optionale Versionsfelder:

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

`FunctionInfo::version` und `ClassInfo::version` in `context.h` existieren bereits.
Bei `visitFunction` muss `funcInfo.version = func->version` gesetzt werden (aktuell
immer 0).

### 4.2 Skip-Logik

```
bundle_version := aus geparster YAML-Node
current_version := ctx.functions[name].version  (oder ctx.classes[name].version)

if bundle_version <= current_version:
    SKIP — Definition bereits vorhanden und aktuell
elif bundle_version > current_version:
    HOT-RELOAD — Neukompilierung (zunächst: Skip + Warnung; später: echtes Reload)
```

### 4.3 Hot-Reload (Implementierung — spätere Phase)

**Im JIT-Modus:**
- Neue Funktion in frischem LLVM-Module kompilieren
- `ee->addModule(newModule)` + `ee->finalizeObject()`
- Alten Symbol-Eintrag in `ctx.functions[name]` aktualisieren
- `getFunctionAddress(name)` liefert fortan die neue Version
- Struct-Layout-Änderungen von Klassen: Fehler (Breaking Change)

**Im AOT-Modus** (siehe Abschnitt 11.3):
Nativ kompilierter Code ist prinzipiell hot-reloadbar — er ist nur RAM-Bytes.
Die Voraussetzung ist eine **Call-Table-Indirektion**: statt `call foo` emittiert
der AOT-Compiler `call [__aiml_fptable + foo_idx]`. Ein Hot-Reload patcht dann
nur den Tabellenzeiger, alle Aufrufer sehen automatisch die neue Implementierung.
Aktuell emittiert AIML AOT direkte Call-Instruktionen (keine Indirektionsschicht),
daher ist AOT-Hot-Reload noch nicht unterstützt.

Für die initiale Implementierung (JIT und AOT):
- Gleiche oder ältere Version → SKIP
- Neuere Version → SKIP + `LOG_WARNING("hot-reload not yet supported: " + name)`

## 5. Globaler Context-Pointer

`aiml_exec_code` ist eine `extern "C"` Funktion und kann nicht direkt auf
`CompilerContext` zugreifen. Lösung: statischer Zeiger in `exec_impl.cpp`:

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

In `compiler.cpp`, direkt vor der JIT-Ausführung:
```cpp
// Vor ee->runFunction(mainFn, ...):
aiml_set_active_context(&ctx, ee.get());
// Nach Ausführung:
aiml_set_active_context(nullptr, nullptr);
```

## 6. Rückgabewert

`sys.exec` gibt `i32` zurück:

| Wert | Bedeutung |
|------|-----------|
| `0` | Erfolg (oder kein start_call) |
| `-1` | Parse-/Kompilierfehler |
| `-2` | Kein aktiver Context (context=false, aber kein JIT-Modus) |
| `n > 0` | Exit-Code des ausgeführten Bundles |

## 7. Syntaxüberblick

```aiml
# Standard: shared context, kein start_call
- set: [rc, { call: [sys.exec, pkg] }]

# Zukünftig: isolierter Context
# - set: [rc, { call: [sys.exec, pkg, true] }]
# oder: pkg.new_context = true; sys.exec(pkg)
```

## 8. Implementierungsplan

### Phase 1: Code-Struct-Erweiterung

**`code_impl.h/cpp`:**
- Field enum: `NEW_CONTEXT = 4` hinzufügen
- `getStructType()`: 5. Feld `i8` hinzufügen
- `emitNewFn()`: 5. Parameter `i8 new_context`
- `create()`: 5. Parameter + `setContext()` Hilfsfunktion
- `emitExecFn()`: emittiert Call zu `aiml_exec_code(i8*) → i32`

**`code_runtime.cpp`:**
- `AimlCode` Struct: `int8_t new_context` Feld hinzufügen

**`ast_compiler_call.cpp`:**
- `sys.bundle` Aufrufe: `new_context = 0` beim `CodeImpl::create()`

### Phase 2: exec_impl.cpp

Neue Datei mit:
- `g_active_context`, `g_active_ee` (static)
- `aiml_set_active_context(void*, void*)`
- `aiml_exec_code(void*)` — Haupt-Implementierung:
  - context=false: shared-context Pfad (mit Skip-Logik)
  - context=true: isolierter Pfad

### Phase 3: Compiler-Integration

**`compiler.cpp`:**
- `aiml_set_active_context(&ctx, ee.get())` vor JIT-Ausführung
- `aiml_set_active_context(nullptr, nullptr)` danach
- Neue Funktion `compileYamlNode(YAML::Node, CompilerContext&)` für exec_impl.cpp

**`ast_compiler_call.cpp`:**
- `sys.exec` Intercept (vor eager eval)
- `visitSysExecCall()`: Argument validieren (Code-Typ), `CodeImpl::exec(ctx, ptr)` aufrufen

**`ast_compiler.h`:**
- `visitSysExecCall` deklarieren

### Phase 4: Tests

**`tests/test_exec.aiml`:**

```aiml
# Grundlegender sys.exec Test

- class:
    name: ExecMsg
    members:
      - [value, i32]

- def:
    name: exec_worker
    args: [[m, ExecMsg]]
    body:
      - print: ["exec_worker: value=${m.value}"]

# Function-Bundle + exec
- decl: [pkg, Code]
- set: [pkg, { call: [sys.bundle, "exec_worker"] }]

- decl: [rc, i32]
- set: [rc, { call: [sys.exec, pkg] }]
- print: ["exec (no start_call) rc: ${rc}"]

# Instance-Bundle + exec
- decl: [msg, ExecMsg]
- set: [msg.value, 42]
- decl: [inst, Code]
- set: [inst, { call: [sys.bundle, msg] }]
- set: [inst, { call: [inst.merge, pkg] }]
- set: [rc, { call: [sys.exec, inst] }]
- print: ["exec (with instance) rc: ${rc}"]
```

## 9. Abgrenzung: sys.exec vs. sys.load_bundle

| | `sys.exec` | `sys.load_bundle` (zukünftig) |
|--|-----------|-------------------------------|
| Context | Shared oder isoliert | Immer shared |
| start_call | Wird ausgeführt | Wird ignoriert |
| data_chunks | Werden ausgeführt | Werden ignoriert |
| Zweck | Code ausführen | Definitionen nachladen |
| JIT-only | Nein (AOT via libaiml) | Ja (ORC JIT nötig) |

`sys.load_bundle` erfordert ORC JIT (inkrementelles Hinzufügen zu laufendem Modul)
und ist zunächst zurückgestellt.

## 10. Bekannte Einschränkungen (initiale Implementierung)

- Hot-Reload: SKIP + Warning (keine echte Umlenkung)
- AOT-Modus: `context=false` gibt -2; `context=true` noch nicht implementiert
- `context = true` (isolierter Modus): zweite Phase
- `data_chunks`-Ausführung: noch nicht implementiert (LOG_WARNING)
- Dynamisches Laden neuer Definitionen (nicht im Symboltabelle): noch nicht implementiert
- start_call: nur 0-Argument-Funktionen unterstützt; Funktionen mit Argumenten werden übersprungen
- Struct-Layout-Änderungen bei Hot-Reload: nicht unterstützt (Breaking Change)
- `sys.load_bundle`: zurückgestellt (braucht ORC JIT)

## 11. JIT-Modus vs. AOT-Modus

### JIT-Modus (`aiml -r file.aiml`)

Das AIML-Programm wird durch `AIMLCompiler::jitRun()` per MCJIT ausgeführt.
Direkt vor `engine->runFunction(mainFn, args)` werden die globalen Zeiger gesetzt:

```cpp
// compiler.cpp, jitRun():
aiml_set_active_context(compiler_ctx_, engine);
engine->runFunction(mainFn, args);
aiml_set_active_context(nullptr, nullptr);
```

Damit stehen in `aiml_exec_code` sowohl `g_active_context` (Symboltabelle,
LLVMContext) als auch `g_active_ee` (kompilierter Code, getFunctionAddress)
zur Verfügung.

| sys.exec-Aufruf | JIT-Modus | Ergebnis |
|---|---|---|
| `context=false` | ✓ | g_active_context + g_active_ee vorhanden |
| `context=true` | ✓ (Phase 2) | Frischer isolierter JIT |

**start_call (context=false):**
Bereits im Haupt-Modul kompilierte Funktionen sind über `getFunctionAddress(name)`
direkt aufrufbar — kein erneutes Kompilieren nötig. Das ist der typische Fall,
wenn ein Bundle derselben Datei ausgeführt wird.

### AOT-Modus (`aiml file.aiml` → Binary)

Das AIML-Programm wird zu nativem Maschinencode kompiliert und als Binary
gelinkt. Es gibt weder eine `jitRun()`-Phase noch eine `ExecutionEngine` zur
Laufzeit. `aiml_set_active_context` wird nie aufgerufen:

```
g_active_context = nullptr
g_active_ee      = nullptr
```

| sys.exec-Aufruf | AOT-Modus | Ergebnis |
|---|---|---|
| `context=false` | ✗ | gibt -2 zurück (kein aktiver JIT-Context) |
| `context=true` | ✓ (Phase 2) | frischen JIT intern starten via libaiml |

**context=false ist im AOT-Modus prinzipiell nicht sinnvoll:** Es gibt keine
laufende LLVM ExecutionEngine, die man teilen könnte. Neue Definitionen aus dem
Bundle müssten von Grund auf JIT-kompiliert werden — das ist dann de facto
identisch mit `context=true`.

**Empfehlung für AOT:** Statt `context=false` (default) explizit `context=true`
setzen, wenn sys.exec in einer AOT-kompilierten Anwendung genutzt wird.

### 11.3 Hot-Reload im AOT-Modus

> **"Ist AOT-Hot-Reload prinzipiell unmöglich?"**
> Nein. Ein kompiliertes Binary ist nur Maschinencode im RAM — und RAM-Bytes
> lassen sich zur Laufzeit ersetzen. Die aktuelle Einschränkung ist **nicht**
> die Natur von nativem Code, sondern das Fehlen einer Indirektionsschicht.

**Warum es aktuell nicht geht:**

AIML AOT emittiert direkte Call-Instruktionen:

```
; AOT-generierter LLVM IR:
call i32 @worker_task()   ; direkte Verknüpfung — Adresse zur Link-Zeit fixiert
```

Der Linker löst `worker_task` zu einer festen Adresse auf. Nach dem Start gibt
es keine Stelle, die man tauschen könnte, ohne alle Call-Sites zu patchen.

**Wie es gehen könnte — Call-Table (PLT-Prinzip):**

```cpp
// Zur Compile-Zeit emittiert der AOT-Compiler eine globale Zeigertabelle:
void* __aiml_fptable[] = {
    (void*)worker_task,   // Index 0
    (void*)other_func,    // Index 1
    ...
};

// Jeder Funktionsaufruf geht durch den Tabelleneintrag:
; LLVM IR mit Indirektion:
%fp = load i8*, i8** getelementptr([__aiml_fptable, 0, 0])
call i32 %fp()
```

Ein Hot-Reload patcht dann nur einen Zeiger in der Tabelle:
```cpp
__aiml_fptable[0] = (void*)worker_task_v2;  // atomar; alle Aufrufer sehen v2
```

Kein Patchen von Call-Sites, kein `mprotect`, keine Trampoline nötig.

**Was auch mit Call-Table nicht geht:**
- Struct-Layout-Änderungen: bereits allokierte Instanzen haben das alte Layout
  im Heap; neuer Code mit anderem Offset liest falsche Bytes → Breaking Change

**Aktueller Status:**
AIML AOT emittiert keine `__aiml_fptable`. Hot-Reload ist daher im AOT-Modus
nicht verfügbar. Die Call-Table-Indirektion ist ein bekanntes Zukünftig-Feature.

### Warum `context=false` trotzdem der Default ist

Im JIT-Modus (der häufigste Entwicklungsfall) ist Shared Context die
leistungsfähigste Option: bereits definierte Typen und Funktionen werden
nicht doppelt kompiliert, Hot-Reload ist möglich, Struct-Typen sind kompatibel.
Ein AOT-Binary, das sys.exec benötigt, muss `context=true` explizit setzen
oder ist speziell für den JIT-Betrieb via `libaiml` gebaut.

### `libaiml` (zukünftig)

Eine Shared-Library `libaiml.so` könnte einen eingebetteten AIML-JIT
bereitstellen, der von AOT-Binaries zur Laufzeit verwendet wird.
Damit würde `context=false` auch im AOT-Modus funktionieren:
`libaiml` stellt einen persistenten `CompilerContext` + `ExecutionEngine` bereit,
in den sys.exec neue Module laden kann.
