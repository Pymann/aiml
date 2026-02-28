# Trampoline-Patching — In-Place Hot-Reload ohne Call-Site-Änderung

## 1. Übersicht

Trampoline-Patching überschreibt die ersten Bytes einer bestehenden Funktion mit
einem Sprung (`jmp`) zu neu kompiliertem Code. Alle `call old_func`-Instruktionen
bleiben unverändert — sie landen beim nächsten Aufruf im neuen Code, weil der
Eintrittspunkt umgeleitet wird.

```
Vor dem Patch:                        Nach dem Patch:
0x401000  push rbp      ← Eintritt    0x401000  jmp 0x7f3c110000  ← 5 Bytes
0x401001  mov rbp, rsp               0x401005  (überschrieben)
0x401003  ...                        0x401007  ...   (wird nie erreicht)
0x40102F  ret                        0x40102F  ret

                                      0x7f3c110000:  ← neue Funktion im JIT-Buffer
                                        push rbp
                                        mov rbp, rsp
                                        ...
                                        ret
```

**Kein Compiler-Umbau nötig** — direkte Call-Instruktionen bleiben direkt.
Der Overhead im ungepatchten Zustand ist null.

---

## 2. Sprung-Stubs

### 2.1 Relativer 5-Byte-Sprung (Standard)

```
E9 <rel32>
```

`rel32 = new_addr - (old_func + 5)` — Offset relativ zum Ende des Sprungbefehls.

Reichweite: ±2 GB vom Sprungbefehl. Ausreichend, wenn der JIT-Buffer in der Nähe
des Binaries allokiert wird (Abschnitt 4).

```c
int32_t rel32 = (int32_t)((intptr_t)new_addr - ((intptr_t)old_func + 5));
uint8_t stub[5] = {
    0xE9,
    (rel32 >>  0) & 0xFF,
    (rel32 >>  8) & 0xFF,
    (rel32 >> 16) & 0xFF,
    (rel32 >> 24) & 0xFF,
};
```

### 2.2 Absoluter 14-Byte-Sprung (Fallback)

Wenn `|new_addr - old_func| > 2 GB` (z.B. bei sehr großen Binaries oder weit
entfernten mmap-Regionen):

```
FF 25 00 00 00 00   ; jmp [rip+0]  — liest 8 Bytes direkt nach dem Befehl
<8 Bytes absoluter Zieladresse, little-endian>
```

```c
uint64_t abs_addr = (uint64_t)new_addr;
uint8_t stub[14] = {
    0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,   // jmp [rip+0]
    (abs_addr >>  0) & 0xFF,
    (abs_addr >>  8) & 0xFF,
    (abs_addr >> 16) & 0xFF,
    (abs_addr >> 24) & 0xFF,
    (abs_addr >> 32) & 0xFF,
    (abs_addr >> 40) & 0xFF,
    (abs_addr >> 48) & 0xFF,
    (abs_addr >> 56) & 0xFF,
};
```

Voraussetzung: die alte Funktion hat mindestens 14 Bytes. Praktisch immer gegeben
(Funktions-Prologue + erster Befehl = mind. 3–7 Bytes; mit NOP-Padding ≥ 14).

---

## 3. Code-Seiten beschreibbar machen (mprotect)

Maschinencode liegt in `.text`-Seiten mit `r-x` (Read + Execute, kein Write).
Vor dem Patchen muss die betroffene Seite temporär schreibbar gemacht werden:

```c
#include <sys/mman.h>
#include <unistd.h>

void patch_function(void* old_func, const uint8_t* stub, size_t stub_len) {
    long   page_size = sysconf(_SC_PAGESIZE);              // typisch 4096
    void*  page      = (void*)((uintptr_t)old_func & ~(page_size - 1));

    // 1. Schreibbar machen
    mprotect(page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC);

    // 2. Stub schreiben
    memcpy(old_func, stub, stub_len);

    // 3. Schutz wiederherstellen
    mprotect(page, page_size, PROT_READ | PROT_EXEC);

    // 4. Instruction-Cache invalidieren (x86-64: Speicher-Barrier genügt)
    __builtin___clear_cache(old_func, (char*)old_func + stub_len);
}
```

**PROT_WRITE + PROT_EXEC gleichzeitig** ist auf modernen Systemen mit W^X-Policy
(z.B. SELinux, OpenBSD) möglicherweise verboten. Sichere Alternative:
Write → `mprotect(PROT_READ|PROT_WRITE)`, dann `mprotect(PROT_READ|PROT_EXEC)`.
Die kurze Zwischenphase ohne Execute ist auf Single-Thread unkritisch.

---

## 4. JIT-Buffer-Allokation

Neuer Code muss in einem ausführbaren Speicherbereich liegen. Für den ±2-GB-Bereich
(5-Byte-Sprung) sollte der Buffer **nah am Binary** allokiert werden:

```c
// mmap mit Adress-Hint nah am Binary:
void* alloc_near(void* hint, size_t size) {
    // Adresse des Binaries als Hint → Kernel versucht, nahe zu allokieren
    void* buf = mmap(
        (void*)((uintptr_t)hint & ~0xFFFFF),  // 1 MB unterhalb als Hint
        size,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1, 0
    );
    if (buf == MAP_FAILED) return nullptr;
    return buf;
}
```

Für AIML: ein statischer JIT-Buffer (z.B. 4 MB) beim ersten Hot-Reload-Aufruf
allokiert, danach sequentiell befüllt (Bump-Allocator). Alte Versionen bleiben
im Buffer (kein Free — bis `sys.free_version` aufgerufen wird, Abschnitt 7).

---

## 5. Variante: Pre-Padded Buffers

Wie in `38_hot_reload.md` beschrieben: der AOT-Compiler emittiert jede Funktion
mit NOP-Padding bis zu einer Mindestgröße (z.B. 64 oder 256 Bytes):

```asm
worker_task:
    push rbp
    mov rbp, rsp
    ...
    ret
    nop; nop; nop; ...    ← Padding bis 64 Bytes
.Lworker_task_end:
```

**Vorteil**: Neuer Code ≤ Padding-Größe → direktes In-Place-Überschreiben, kein
JIT-Buffer, kein externer Zeiger nötig.

**Nachteil**:
- Padding-Größe ist willkürlich; Funktionen wachsen und brauchen trotzdem Trampoline
- Binary wird größer (jede Funktion mindestens N Bytes)
- Kein echter Gewinn gegenüber reinem Trampoline, der immer funktioniert

**Empfehlung**: Padding nur für die ersten 14 Bytes (genug für den absoluten Sprung),
nicht für den gesamten Funktionskörper. Damit ist der Trampoline-Stub immer
einschreibbar, ohne die Funktion selbst zu zerstören.

---

## 6. Backup des alten Codes

Damit laufende Threads den alten Code sicher beenden können:

```
Vor dem Patch:
  1. Neuen Code in JIT-Buffer kompilieren (Zieladresse bekannt)
  2. Alten Code (erste N Bytes) in separaten Puffer kopieren (Backup)
     → laufende Threads, die gerade im Prologue sind, lesen aus dem Original;
        nach dem Patch springen sie zum neuen Code
  3. Stub schreiben (5 oder 14 Bytes)
```

Das Backup wird für `sys.free_version` benötigt: erst wenn sichergestellt ist,
dass kein Thread mehr im alten Code ist, darf der Backup-Buffer freigegeben werden.

Für Single-Thread-AIML-Programme ist kein Backup nötig.

---

## 7. Speicher-Rückgewinnung (`sys.free_version`)

```aiml
# Alte Version von worker_task freigeben (Version 1):
- call: [sys.free_version, "worker_task", 1]
```

In `exec_impl.cpp` / Runtime:
- JIT-Buffer-Bereich der alten Version als frei markieren
- Backup-Puffer freigeben
- `ctx.functions["worker_task"].old_versions` bereinigen

**Sicherheit**: Nur aufrufen, wenn garantiert kein Thread mehr die alte Version
ausführt (Quiescence-Point, z.B. nach Barrier oder Join).

---

## 8. Atomizität

### Das Problem

Ein 5-Byte-Write ist **nicht atomar**:

```
Thread A: liest Byte 0–3 der Instruktion (altes push rbp)
Thread B: schreibt Bytes 0–4 (E9 xx xx xx xx)
Thread A: liest Byte 4 — liest bereits neues Byte → ungültige Instruktion
```

### Lösung 1: Single-Thread (einfachste Lösung)

Für AIML-Programme ohne Parallelität: kein Problem. `sys.exec` wird zwischen
Threads koordiniert (Channel-Sync), nicht während einer Funktion aufgerufen.

### Lösung 2: 2-Byte-Patch mit vorbereiteter Funktion

Microsoft Windows Hot-Patching nutzt einen 2-Byte-Sprung (`EB <rel8>`, Reichweite
±127 Bytes) als atomischen ersten Schritt zu einem Trampoline im NOP-Padding:

```
AOT-emittierte Funktion (vorbereitetes Layout):
  0x401000: EB 09            ; jmp +9 (zur 14-Byte-Stub-Region)  ← 2 Bytes atomar patchbar
  0x401002: nop nop nop nop  ; 4 Bytes Padding
  0x401006: nop nop nop nop  ; 4 Bytes Padding (14-Byte-Stub passt ab hier)
  0x40100E: push rbp          ; eigentlicher Funktionskörper
  ...

Patch-Prozess:
  1. Absoluten 14-Byte-Stub in 0x401002–0x40100F schreiben (harmlos — nie erreicht)
  2. 2-Byte `EB 09` atomar auf `EB 00` (oder entsprechendes Offset) umschreiben
     → jetzt springt Funktion zum Stub
```

Ein 16-Bit-Write (`mov word ptr [addr], val`) ist auf x86-64 atomar wenn 2-Byte-aligned.

### Lösung 3: Thread-Suspend (maximale Sicherheit)

```c
// Alle Threads kurz suspendieren (plattformspezifisch):
// Linux: SIGSTOP aller Threads, patch, SIGCONT
// Oder: pthread_barrier_wait an definierten Checkpoint-Punkten
```

Aufwändig, aber korrekt für Multi-Thread + beliebige Funktionen.

---

## 9. Instruction-Cache auf x86-64

x86-64 hat **cache-kohärente** I-Caches (MESI-Protokoll): ein `mov [addr], val`
macht den I-Cache-Line automatisch ungültig auf allen Cores. Expliziter Cache-Flush
(`CLFLUSH`) oder `__builtin___clear_cache` sind technisch nicht nötig, aber
als Portable-Code-Praxis empfohlen (ARM benötigt es z.B. zwingend).

---

## 10. Vergleich mit fptable

Siehe `38_1_fptable.md` §8 für die vollständige Tabelle. Kurzfassung:

| | fptable | Trampoline |
|---|---|---|
| Compile-Zeit | indirekte Calls emittieren | keine Änderung |
| Runtime-Overhead (ungepatcht) | +1 Load pro Call | **null** |
| Runtime-Overhead (gepatcht) | +1 Load pro Call | +1 jmp |
| Atomizität | trivial (8-Byte-Store) | komplex |
| mprotect | nein | ja |

**Empfehlung**: Für AIML im Single-Thread-JIT-Modus → Trampoline (null Overhead,
einfache Atomizität). Für Multi-Thread AOT mit häufigem Hot-Reload → fptable.

---

## 11. Implementierungsplan

### Phase 1 — JIT-Buffer

**`exec_impl.cpp`:**
- `static void* g_jit_buffer = nullptr` — beim ersten Aufruf via `alloc_near` allokiert
- `static size_t g_jit_offset = 0` — Bump-Allocator
- `void* jit_alloc(size_t n)` — gibt Zeiger zurück, erhöht Offset

### Phase 2 — Patch-Funktion

**`exec_impl.cpp`:**
- `bool write_trampoline(void* target, void* dest)`:
  - Reichweite prüfen (±2 GB → 5-Byte, sonst → 14-Byte)
  - `mprotect` → `memcpy` → `mprotect` → `__builtin___clear_cache`
  - Gibt `true` bei Erfolg zurück

### Phase 3 — Integration in `aiml_exec_code`

Bei Hot-Reload (bundle_version > current_version):
```
1. Neuen Code in JIT-Buffer kompilieren → new_addr
2. Backup von old_func (erste 14 Bytes) speichern
3. write_trampoline(old_func, new_addr)
4. ctx.functions[name].version aktualisieren
5. ctx.functions[name].func = neue llvm::Function* (für Metadaten)
```

### Phase 4 — `sys.free_version`

**`ast_compiler_call.cpp`:**
- Intercept `sys.free_version` → `aiml_free_version(const char* name, int version)` aufrufen
- In Runtime: JIT-Buffer-Bereich als wiederverwendbar markieren

---

## 12. Bekannte Einschränkungen

- Multi-Thread-Atomizität: initiale Implementierung nur für Single-Thread korrekt
- Funktionen < 5 Bytes: theoretisch möglich (z.B. nackte `ret`-Wrapper) → brauchen Pre-Padding
- W^X-Systeme (OpenBSD, hardened Linux): `PROT_WRITE|PROT_EXEC` verboten → alternativer mprotect-Ablauf nötig
- ARM: expliziter I-Cache-Flush via `__builtin___clear_cache` zwingend
- Inlined Funktionen: kein Patch möglich (Code ist in Aufrufer eingebettet)
- Struct-Layout-Änderungen: bleiben Breaking Change (Abschnitt 38_3)
