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

Das Bundle wird in den **laufenden** Compiler-Context eingeladen:

```
sys.exec(pkg)  [context=false]
  → C-Runtime: aiml_exec_code(void* code_obj)
      ↓
  1. Zugriff auf g_active_context (globaler CompilerContext-Zeiger)
  2. Für jedes code_chunk (YAML-String):
       Parse → AST-Nodes
       Für jede Declaration:
         a. Name bereits in ctx.functions/ctx.classes?
            → Versions-Check:
              bundle.version <= ctx.version  → SKIP (bereits aktuell)
              bundle.version >  ctx.version  → HOT-RELOAD (Neukompilierung)
         b. Nicht vorhanden → Normal kompilieren in neues llvm::Module
  3. Neues Module in denselben ExecutionEngine einlinken:
       ee->addModule(std::move(newModule))
       ee->finalizeObject()
  4. data_chunks als Statements kompilieren + ausführen
  5. start_call als Statement ausführen (falls nicht leer)
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

Neuer `LLVMContext` + `Module` + `Compiler` + eigener `ExecutionEngine`.
Keine Verbindung zum Host-Programm. Immer sauber, keine Konflikte.
Nützlich für untrusted Code oder vollständige Sandboxing.

```
sys.exec(pkg)  [context=true]
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

- Neue Funktion in frischem Module kompilieren
- Alten Entry-Point atomar überschreiben (Function-Pointer-Umlenkung)
- `ctx.functions[name]` mit neuer `FunctionInfo` aktualisieren
- Struct-Layout-Änderungen von Klassen: Fehler (Breaking Change)

Für die initiale Implementierung:
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
- AOT-Modus: noch nicht implementiert
- `context = true` (isolierter Modus): zweite Phase
- Struct-Layout-Änderungen bei Hot-Reload: nicht unterstützt (Fehler)
- `sys.load_bundle`: zurückgestellt (braucht ORC JIT)
