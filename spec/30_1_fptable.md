# fptable — Funktionszeiger-Tabelle für Hot-Reload

## 1. Übersicht

Der fptable-Ansatz ersetzt direkte `call func`-Instruktionen durch indirekte Aufrufe
über eine globale Zeigertabelle `__aiml_fptable`. Hot-Reload aktualisiert nur einen
Zeiger in der Tabelle — alle bestehenden Call-Sites sehen die neue Implementierung
beim nächsten Aufruf, ohne selbst verändert zu werden.

```
Ohne fptable (AOT, direkt):          Mit fptable (AOT, indirekt):

call worker_task                      load rax, [__aiml_fptable + 0*8]
  → Adresse zur Link-Zeit fix          call rax
  → kein Hot-Reload möglich             → Adresse zur Laufzeit austauschbar
```

---

## 2. Tabellenstruktur

Der AOT-Compiler emittiert eine globale Array-Variable mit je einem `i8*`-Eintrag
pro bekannter Funktion:

```llvm
; LLVM IR (AOT-generiert):
@__aiml_fptable = global [3 x i8*] [
  i8* bitcast (i32 ()* @worker_task to i8*),   ; Index 0
  i8* bitcast (void (i8*)* @log_msg to i8*),   ; Index 1
  i8* bitcast (i32 (i32)* @compute to i8*)     ; Index 2
]

; Symbolische Index-Konstanten (eine pro Funktion):
@__aiml_idx_worker_task = constant i32 0
@__aiml_idx_log_msg     = constant i32 1
@__aiml_idx_compute     = constant i32 2
```

Die Tabelle liegt im `.data`-Segment (les- und schreibbar). Jeder Eintrag ist
genau 8 Bytes (Zeiger auf x86-64) → atomisch aktualisierbar (siehe Abschnitt 5).

---

## 3. Call-Emission im AOT-Compiler

Statt `call i32 @worker_task()` emittiert der Compiler:

```llvm
; Indirekter Aufruf über Tabellenzeiger:
%slot    = getelementptr [3 x i8*], [3 x i8*]* @__aiml_fptable, i32 0, i32 0
%fp_raw  = load i8*, i8** %slot
%fp      = bitcast i8* %fp_raw to i32 ()*
%result  = call i32 %fp()
```

**Einschränkung**: Der Compiler muss den Funktions-Index (`0`, `1`, ...) bereits zur
Compile-Zeit kennen. Alle Definitionen einer Compilation-Unit sind bekannt → kein
Problem. Externe Definitionen (fremde Module) erhalten einen Eintrag in einer
erweiterter Tabelle (Abschnitt 6).

---

## 4. Hot-Reload: Tabellenzeiger aktualisieren

Wenn eine neue Version von `worker_task` in einen JIT-Buffer kompiliert wurde:

```cpp
// C-Seite (exec_impl.cpp oder hot-reload-Runtime):
void aiml_fptable_update(int idx, void* new_fn_ptr) {
    __atomic_store_n(
        &__aiml_fptable[idx],
        new_fn_ptr,
        __ATOMIC_SEQ_CST   // volle Speicher-Barrier
    );
}

// Aufruf:
uint64_t new_addr = jit_engine->getFunctionAddress("worker_task");
aiml_fptable_update(0, (void*)new_addr);
```

Ab dem nächsten `call rax` laden alle Aufrufer die neue Adresse.
**Kein mprotect, kein Patchen von Call-Sites, kein Cache-Flush nötig.**

---

## 5. Atomizität

Ein 64-Bit-Zeiger (8 Bytes) auf einem 8-Byte-ausgerichteten Tabellenplatz ist auf
x86-64 **atomar les- und schreibbar** — garantiert durch die Architektur (Intel SDM
Vol. 3A §8.1.1: "The processor guarantees that a single QWORD memory access is
atomic if it is naturally aligned").

Voraussetzung: `__aiml_fptable` ist 8-Byte-aligned (Default für globale Arrays in LLVM).

```
Thread A ruft worker_task auf:        Thread B aktualisiert Zeiger:
  load rax, [fptable + 0*8]            store [fptable + 0*8], new_addr (atomar)
  call rax
```

Thread A sieht entweder den alten oder den neuen Zeiger — nie einen halbgeschriebenen.
**Kein Mutex, kein Thread-Suspend nötig.**

---

## 6. Neue Funktionen zur Laufzeit

Die Tabelle wird zur Compile-Zeit mit fixer Größe emittiert. Zur Laufzeit neu
geladene Funktionen (aus einem Code-Bundle), die vorher nicht existierten, haben
keinen Tabellenplatz.

**Optionen:**

### 6.1 Reservierte Slots

Der AOT-Compiler reserviert N Leer-Slots am Ende der Tabelle:
```llvm
@__aiml_fptable = global [103 x i8*] [
  ; 3 bekannte Funktionen
  i8* bitcast (@worker_task to i8*),
  i8* bitcast (@log_msg     to i8*),
  i8* bitcast (@compute     to i8*),
  ; 100 leere Slots für dynamische Erweiterung
  i8* null, i8* null, ..., i8* null
]
```

### 6.2 Dynamische Erweiterung via Indirektion

Statt direktem Array: globaler Zeiger auf einen heap-allokierten Block, der
bei Bedarf neu allokiert wird:
```cpp
void** __aiml_fptable_ptr = initial_table;
int    __aiml_fptable_size = 3;

// Erweiterung (Lock nötig — seltener Pfad):
void aiml_fptable_grow(void* new_fn) {
    void** old = __aiml_fptable_ptr;
    void** neu = malloc((size + 1) * 8);
    memcpy(neu, old, size * 8);
    neu[size] = new_fn;
    __atomic_store_n(&__aiml_fptable_ptr, neu, __ATOMIC_SEQ_CST);
    size++;
    // old nicht sofort freigeben — ggf. noch in Benutzung
}
```

Für die initiale Implementierung genügen **reservierte Slots** (Abschnitt 6.1).

---

## 7. Performance

| Situation | direkter Call | fptable-Call |
|-----------|--------------|-------------|
| L1-Cache-Hit | 1 ns | ~2 ns (+1 Load) |
| L1-Cache-Miss | 1 ns | ~4–10 ns (Pointer im Cache-Line) |
| Branch-Misprediction | selten | möglich (indirekter Sprung) |

Die Tabelle ist typisch klein → passt vollständig in L1/L2. Praktischer Overhead:
**1–2 ns pro Call** — in den meisten Anwendungen vernachlässigbar.

Kritische Inner-Loops: dort `@inline`-Markierung nutzen → Funktion wird eingebettet,
kein indirekter Call.

---

## 8. Vergleich mit Trampoline-Patching

| Kriterium | fptable | Trampoline |
|-----------|---------|------------|
| Compiler-Änderung | ja (indirekte Calls) | nein (direkte Calls bleiben) |
| Call-Overhead | +1 Load (immer) | 0 (ungepatcht), +1 jmp (gepatcht) |
| Atomizität | trivial (8-Byte-Store) | komplex (5-Byte-Write) |
| Größenlimit | keins | keins (JIT-Buffer) |
| mprotect nötig | nein | ja |
| Neue Funktionen | Slot-Konzept nötig | kein Problem (neuer Buffer) |
| `__rdynamic` nötig | nein (Tabelle intern) | ja (für Symbol-Lookup) |

**fptable** ist einfacher zu implementieren und sicherer bzgl. Atomizität,
hat aber immer einen kleinen Call-Overhead.
**Trampoline** hat null Overhead im Normalfall, erfordert aber mprotect und
komplexere Atomizitäts-Behandlung.

---

## 9. Implementierungsplan

### Phase 1 — AOT-Compiler emittiert Tabelle

**`src/core/ast_compiler.cpp` / `code_gen`:**
- Nach `registerTypes`: alle bekannten Funktionsnamen sammeln → Index vergeben
- `@__aiml_fptable` als globale Variable emittieren (+ N Leer-Slots)
- `@__aiml_idx_<name>` als globale Konstante pro Funktion

### Phase 2 — Indirekte Call-Emission

**`src/core/ast_compiler_call.cpp`:**
- `visitCall`: statt `builder.CreateCall(llvmFn, ...)` → GEP + Load + indirectCall
- Nur wenn `ctx.useFptable` aktiv (Flag, default: false während Transition)

### Phase 3 — Hot-Reload-Runtime

**`exec_impl.cpp`:**
- `aiml_fptable_update(int idx, void* fn)` — atomischer Store
- `aiml_fptable_register(const char* name, void* fn)` — für neue Funktionen (Leer-Slot)
- In `aiml_exec_code`: nach JIT-Kompilierung → `aiml_fptable_update` aufrufen

### Phase 4 — Linker-Flags

**`CMakeLists.txt` / Build:**
- AOT-Binary: `-Wl,--export-dynamic` für Runtime-Zugriff auf Tabelle aus `libaiml`

---

## 10. Bekannte Einschränkungen

- fptable muss zur Compile-Zeit fixer Größe sein (oder Erweiterungskonzept nötig)
- Inline-Funktionen umgehen die Tabelle → kein Hot-Reload für inlined Code
- Struct-Layout-Änderungen bleiben Breaking Change (unabhängig von fptable)
- Erste Phase: `ctx.useFptable = false` → fptable noch nicht aktiv
