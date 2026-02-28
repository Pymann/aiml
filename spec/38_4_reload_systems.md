# Hot-Reload Systems — Gesamtarchitektur

Dieses Dokument fasst alle Hot-Reload-Mechanismen zusammen und zeigt wie sie
zusammenspielen. Einzelheiten zu den Mechanismen: `38_1_fptable.md`,
`38_2_trampoline_patching.md`, `38_3_symbolism.md`.

---

## 1. Drei unabhängige Hot-Reload-Probleme

```
┌─────────────────────────────────────────────────────────────────┐
│  Problem 1: CODE                                                │
│  Funktionen / Methoden — purer Maschinencode                   │
│  Lösung: fptable oder Trampoline-Patching (38_1 / 38_2)        │
├─────────────────────────────────────────────────────────────────┤
│  Problem 2: TYPEN                                               │
│  Klassen / Models / Enums / Interfaces — Schemata               │
│  Lösung: Schema-Registry + Offset-Tabelle                       │
├─────────────────────────────────────────────────────────────────┤
│  Problem 3: LEBENDE DATEN                                       │
│  Heap-Instanzen mit altem Layout                               │
│  Lösung: Member-Padding (Fast-Path) + Lazy-Toleranz (Fallback) │
└─────────────────────────────────────────────────────────────────┘
```

Alle drei Probleme sind orthogonal und haben unabhängige Lösungen.
Code-Hot-Reload funktioniert ohne Schema-Reload und umgekehrt.

---

## 2. Code Hot-Reload (Funktionen & Methoden)

Zusammenfassung aus 38_1 und 38_2:

### 2.1 fptable (Compile-Time-Umbau nötig, triviale Atomizität)

```llvm
; Statt direktem Call:
call i32 @worker_task()

; Über Tabelle:
%fp = load i8*, i8** getelementptr(@__aiml_fptable, 0, <idx>)
call i32 %fp()
```

Hot-Reload = atomischer 8-Byte-Pointer-Store. Kein mprotect.

### 2.2 Trampoline-Patching (kein Compile-Time-Umbau, null Overhead)

```
Erster Byte der alten Funktion → E9 <rel32> (jmp zu neuer Implementierung)
Neue Implementierung → JIT-Buffer (mmap near binary)
```

Hot-Reload = mprotect + memcpy(5 Bytes) + mprotect zurück.

### 2.3 Empfehlung

| Szenario | Empfehlung |
|----------|-----------|
| Single-Thread JIT | Trampoline (null Overhead, einfach) |
| Multi-Thread AOT | fptable (triviale Atomizität) |
| Performance-kritisch, kein Reload nötig | `@noreload` → direkter Call bleibt |

---

## 3. Schema-Registry — Typ-Hot-Reload

### 3.1 Aufbau

Der AOT-Compiler emittiert neben dem Binary eine globale **Schema-Registry**
(In-Memory, kein Disk-I/O) und eine **Offset-Tabelle** (analog zu fptable):

```
__aiml_schema["Payload"] = {
    current_version: 1,
    versions: {
        1: {size: 8,  fields: {id: 0, value: 4}},
    }
}

__aiml_offsets = [
    /* [0] Payload.id    */ 0,
    /* [1] Payload.value */ 4,
    /* [2] Status.Ok     */ 0,
    ...
]
```

### 3.2 Feldzugriff über Offset-Tabelle

```llvm
; Statt GEP mit baked-in Index:
%ptr = getelementptr %Payload, %Payload* %p, i32 0, i32 1   ; alt: baked

; Über Offset-Tabelle (1 Load, ~1 ns, L1-Cache):
%off = load i64, i64* getelementptr(@__aiml_offsets, 0, 1)
%base = bitcast %Payload* %p to i8*
%ptr  = getelementptr i8, i8* %base, i64 %off
%v    = load i32, i32* %ptr
```

**Schema-Hoisting**: Compiler holt Offsets einmal im Funktions-Prologue in
lokale Variablen → Inner-Loops ohne Tabellen-Zugriff:

```llvm
entry:
  %off_value = load i64, i64* getelementptr(@__aiml_offsets, 0, 1)  ; 1× beim Eintritt
loop:
  %ptr = getelementptr i8, i8* %base, i64 %off_value   ; Register — kein Load mehr
  %v   = load i32, i32* %ptr
  br loop
```

### 3.3 Hot-Reload eines Schemas

```
Neues Feld am Ende (Payload.priority, Offset 8):

1. Schema-Registry updaten:
   __aiml_schema["Payload"].versions[2] = {size: 12, fields: {id:0, value:4, priority:8}}
   __aiml_schema["Payload"].current_version = 2

2. Offset-Tabelle erweitern:
   __aiml_offsets[3] = 8    ← neuer Slot für Payload.priority

3. Neuen Slot-Index in Compiler-Metadaten registrieren
   (für zukünftige Kompilierungen die das Feld referenzieren)
```

Bestehende Funktionen, die `value` lesen, laden weiterhin `__aiml_offsets[1] = 4`
→ korrekt. Neue Funktionen, die `priority` lesen, laden `__aiml_offsets[3] = 8`.

---

## 4. Lebende Daten — das Heap-Problem

Das schwierigste Problem: Instanzen, die vor dem Schema-Hot-Reload allokiert wurden,
haben das alte Byte-Layout im Heap. Zwei Mechanismen lösen das gemeinsam:

### 4.1 Member-Padding (Fast-Path — kein Overhead)

Jeder Struct wird von vornerein auf die nächste Zweierpotenz **aufgerundet**
und mit Nullen initialisiert (`calloc` oder `mmap`):

```
Payload v1 (gepackt):   [id: 0-3][value: 4-7]          = 8 Bytes
Payload v1 (gepaddet):  [id: 0-3][value: 4-7][_: 8-15] = 16 Bytes (nächste 2er-Potenz)
```

Wenn `priority` (i32) als drittes Feld hinzukommt:
- Neuer Offset: 8 — **liegt im bereits allokierten Padding**
- Padding-Bytes waren `calloc`-initialisiert → **priority = 0 by default**
- Alte Instanzen lesen `priority` → korrekt 0 (sinnvoller Default)
- **Kein Version-Check, kein Warning, kein mprotect — null Overhead**

```
Padding-Strategie:
  packed size ≤  8 Bytes  → allokiere  16 Bytes
  packed size ≤ 16 Bytes  → allokiere  32 Bytes
  packed size ≤ 32 Bytes  → allokiere  64 Bytes
  packed size ≤ 64 Bytes  → allokiere 128 Bytes
  ...
  packed size >  N Bytes  → allokiere  2 × packed size
```

**Wann reicht Padding nicht?** Wenn nach dem Hot-Reload die Gesamtgröße aller
Felder die ursprüngliche Padding-Größe übersteigt. Beispiel: Payload startet mit
8 Bytes gepackt → 16 Bytes padded. Nach 3 Hot-Reloads mit je 4 Bytes neuem Feld:
8 + 4 + 4 + 4 = 20 Bytes > 16 Bytes → Padding erschöpft → **Fallback**.

### 4.2 Lazy-Toleranz mit Schema-Version-Tag (Fallback)

Wenn Padding erschöpft ist, muss eine neue **Major-Version** des Schemas angelegt
werden. Neue Instanzen werden mit der neuen Größe allokiert. Bestehende Instanzen
erhalten einen **Schema-Version-Tag** im Allokierungs-Header:

```
Heap-Layout (mit 4-Byte-Header):
  [schema_version: u32][id: 4B][value: 4B][priority: 4B][...]
   ↑ versteckt, 4 Bytes overhead pro Instanz
```

Bei jedem Feldzugriff prüft die Runtime:

```
Payload.value auf Instanz p lesen:
  p.schema_version  = 1  (alt)
  current_version   = 2  (neu, mehr Platz)

  → Suche "value" in Version 1 der Schema-History:
      v1.fields["value"].offset = 4  ✓ (existiert, korrekter Offset)
  → Lese Byte 4-7 der Instanz (korrekt)
  → [WARNING] "Payload.value: Instanz ist schema v1, aktuell v2
               — Feld noch gültig, aber Instanz outdated"
```

```
Payload.new_field auf Instanz p lesen:
  p.schema_version  = 1  (existiert vor new_field)
  "new_field" in v1 → nicht gefunden

  → [WARNING] "Payload.new_field: Feld existiert nicht in schema v1
               — Defaultwert 0 zurückgegeben"
  → Gibt 0 zurück (kein Crash)
```

**Schlüsseleigenschaft**: Der Zugriff auf Felder, die in der Instanz-Version
existierten, ist **immer korrekt** (Append-Only garantiert gleiche Offsets).
Nur der Zugriff auf neue Felder gibt 0 + Warning. Die Instanz lebt ihren
natürlichen Lebenszyklus aus und wird irgendwann freigegeben — kein Forced-Migration.

### 4.3 Kompatibilität der beiden Mechanismen

```
Neues Feld hinzugefügt
        │
        ▼
Passt in Padding?
   ├── Ja → Nullen lesen, kein Warning, kein Tag    ← Fast-Path (häufig)
   └── Nein → Major-Version bump
                  │
                  ▼
           Neue Instanzen: größere Allokierung, Version-Tag = 2
           Alte Instanzen: Version-Tag = 1, laufen weiter
                  │
                  ▼
           Feldzugriff auf alte Instanz:
             Feld existierte in v1 → korrekt + WARNING
             Feld neu in v2        → 0 + WARNING
                  │
                  ▼
           Instanz geht out-of-scope → keine Warnings mehr
```

---

## 5. Enums — Sonderfall

Enum-Werte sind derzeit Compile-Time-Immediates (baked in). Hot-Reload ist
**nicht möglich** ohne eine Lookup-Tabelle (analog zu `__aiml_offsets`):

```llvm
; Aktuell (nicht reloadable):
%cmp = icmp eq i32 %s, 1   ; 1 = Error, baked

; Mit Enum-Lookup-Tabelle (zukünftig):
%ev  = load i32, i32* getelementptr(@__aiml_enum_Status, 0, 1)  ; 1 = Error-Index
%cmp = icmp eq i32 %s, %ev
```

**Sichere Enum-Änderungen** (ohne Lookup-Tabelle):
- Neues Member am Ende hinzufügen → alter Code kennt es nicht, kein Problem
- Wert eines bestehenden Members ändern → Breaking (Immediates falsch)

**Schema-Version-Tag für Enums**: Enum-Werte werden als `i32` gespeichert.
Ein Instanz-Tag hilft hier nicht — es gibt keine "Instanz", nur den Wert.
Deshalb gilt: Enum-Werte nie ändern, nur erweitern.

---

## 6. Performance-Modell

### Pro Feldzugriff (Laufzeit-Overhead)

| Mechanismus | Overhead | Bedingung |
|-------------|---------|-----------|
| Baked GEP (heute) | **0 ns** | kein Hot-Reload |
| Offset-Tabelle, L1-Hit | ~1 ns | Schema in Tabelle |
| Schema-Hoisting | ~0 ns (amortisiert) | Offset einmal pro Funktion |
| Version-Check (Padding ok) | **0 ns** | Fast-Path: kein Tag nötig |
| Version-Check (Tag vorhanden) | ~2–5 ns | Vergleich + Schema-Lookup |
| Warning-Emission | ~50–200 ns | nur bei outdated Instanzen |

### Memory-Overhead

| Mechanismus | Overhead |
|-------------|---------|
| Member-Padding | 1× packed size (16B statt 8B, 64B statt 48B, ...) |
| Schema-Version-Tag | 4 Bytes pro Instanz (nur wenn Major-Version bump) |
| Schema-Registry | ~1 KB pro Typ (Name, Felder, Versionshistorie) |
| Offset-Tabelle | 8 Bytes pro Feld × N Felder (typisch < 10 KB gesamt) |

### `@noreload`-Annotation

Für Performance-kritische Typen: Padding deaktivieren, GEP baked lassen:

```aiml
- class:
    name: Vec3
    noreload: true        # kein Padding, kein Version-Tag, kein Schema-Eintrag
    members:
      - [x, f32]          # Offset 0 — für immer
      - [y, f32]          # Offset 4 — für immer
      - [z, f32]          # Offset 8 — für immer
```

---

## 7. Gesamtbild: Was wann greift

```
sys.exec empfängt Code-Bundle
        │
        ├── code_chunks (Funktionen/Methoden):
        │       version > ctx.version?
        │         ├── Ja → Trampoline/fptable Hot-Reload
        │         └── Nein → SKIP
        │
        ├── code_chunks (Klassen/Models):
        │       version > ctx.version?
        │         ├── Ja, Append-Only → Schema-Registry + Offset-Tabelle updaten
        │         │       Padding ausreichend?
        │         │         ├── Ja → Neue Felder in bestehenden Instanzen = 0
        │         │         └── Nein → Major-Version bump, Version-Tags aktivieren
        │         ├── Ja, Breaking Change → ERROR (nicht unterstützt)
        │         └── Nein → SKIP
        │
        └── data_chunks (Instanz-Initialisierungen):
                Ausführung → neue Instanzen mit aktuellem Schema allokiert
```

---

## 8. Implementierungsreihenfolge (Phasen)

### Phase 1 — Code Hot-Reload (Trampoline, Single-Thread)
Voraussetzung: sys.exec (bereits implementiert, Phase 2/3 aus 29_2)
- JIT-Buffer via `mmap`
- `write_trampoline(old_func, new_addr)`
- Version-Check in `aiml_exec_code` → Trampoline statt SKIP

### Phase 2 — Member-Padding
- AOT-Compiler: Struct-Allokierung auf nächste 2er-Potenz runden
- `calloc` statt `malloc` für alle Struct-Allokierungen (Nullen garantiert)
- Padding-Größe in Schema-Registry ablegen

### Phase 3 — Schema-Registry + Offset-Tabelle
- `__aiml_offsets`-Array emittieren
- Feldzugriffe über Offset-Tabelle (mit Schema-Hoisting im Prologue)
- Schema-Registry initialisieren (In-Memory, bei Programmstart)

### Phase 4 — Lazy-Toleranz (Version-Tags)
- Allokierungs-Header mit `schema_version: u32`
- Feldzugriff-Runtime: Version prüfen, korrekte Offset-Version wählen, Warning emittieren
- `sys.free_version` für Schema-History-Cleanup

### Phase 5 — fptable (Multi-Thread)
- `__aiml_fptable` emittieren
- Indirekte Call-Emission im AOT-Compiler
- `aiml_fptable_update` für atomische Updates

---

## 9. Persistenz von Hot-Reload-State

### 9.1 Das Problem

Alle Hot-Reload-Änderungen (Trampoline-JIT-Buffer, fptable-Einträge,
Schema-Registry-Updates) leben im RAM. Beim Beenden des Prozesses sind sie
verloren — vor allem kritisch wenn die Code-Objekte über das Netzwerk kamen
und nicht erneut verfügbar sind.

### 9.2 Hot-Reload-Journal (Write-Ahead Log)

Jedes `sys.exec`, das zu einem Hot-Reload führt, schreibt vor der Anwendung
die `code_chunks` in ein Journal. Der Inhalt ist YAML — bereits im Code-Objekt
enthalten, kein extra Aufwand.

```
sys.exec(pkg)
    │
    ├── 1. Journal-Eintrag schreiben:
    │        hot_reload.journal ←
    │          {base_hash: "sha256:4a7f...",
    │           bundle_id: "abc123",
    │           timestamp: "2026-02-28T14:30:00Z",
    │           code_chunks: ["- def:\n    name: worker_task\n    version: 2\n..."]}
    │
    └── 2. Hot-Reload anwenden (Trampoline / Schema-Update)
```

Beim nächsten Programmstart:

```
Binary startet
    │
    ▼
Journal vorhanden + base_hash stimmt überein?
    ├── Nein → normal starten
    └── Ja  → Einträge in Reihenfolge replayed:
                code_chunks re-kompilieren
                Trampoline / fptable wieder anwenden
                Schema-Registry wieder aufbauen
                → vollständiger Zustand rekonstruiert
```

### 9.3 JIT-Modus: Journal = mehr AIML am Ende der Quelldatei

Im JIT-Modus (`aiml -r main.aiml`) gibt es kein Binary. Das Journal wird direkt
an die Quelldatei angehängt — AIML's Version-Check erledigt die Deduplication
beim nächsten Compilerlauf automatisch:

```aiml
# main.aiml — original
- def:
    name: worker_task
    version: 1
    body:
      - print: ["v1"]

# === HOT-RELOAD JOURNAL — 2026-02-28T14:30:00Z (bundle: abc123) ===
- def:
    name: worker_task
    version: 2          # höhere Version → überschreibt v1 automatisch
    body:
      - print: ["v2"]
```

Kein separater Replay-Mechanismus nötig — der Compiler sieht beide Definitionen,
Version-Check greift, v2 gewinnt.

### 9.4 Snapshot: Journal → neue Baseline

Das Journal wächst mit der Zeit. `sys.snapshot` komprimiert: alle Einträge
werden in den Quellcode gemergt, neu kompiliert, Journal geleert.

```aiml
- call: [sys.snapshot, "program_v2.aiml"]   # Quelldatei mit allen Updates gemergt
- call: [sys.snapshot]                       # überschreibt aktuelle Quelldatei
```

```
Base-Source + Journal → merge → program_v2.aiml → neu kompilieren
                                                 → program_v2 (kein Journal nötig)
```

Analog zu `git commit` — akkumulierte Patches werden zur neuen Baseline.

### 9.5 Basis-Konflikt-Erkennung

Wenn das Binary/die Quelldatei selbst aktualisiert wird (neue Deployment),
könnte das Journal gegen eine inkompatible Basis appliziert werden.
Das Journal enthält einen `base_hash`:

```json
{
  "base_hash": "sha256:4a7f...",
  "entries": [...]
}
```

Stimmt `base_hash` beim Start nicht überein → Journal verwerfen + Warning.
Der Nutzer entscheidet über manuelle Portierung oder verwirft den Journal-State.

### 9.6 Persistenz-Modi

| Mechanismus | Wann aktiv | Eignung |
|---|---|---|
| Auto-Journal (default) | bei jedem Hot-Reload-`sys.exec` | immer recoverable |
| `sys.checkpoint()` | explizit im Code | kontrollierte Persistenz |
| `sys.snapshot(path)` | explizit, mergt + kompiliert | Release-Artefakte |
| SIGTERM-Handler | bei sauberem Beenden | Fallback für ungeplante Stops |
| `pkg.persist = false` | opt-out pro Bundle | ephemere / temporäre Patches |

---

## 10. Bekannte Einschränkungen (alle Phasen)

- Breaking Changes (Feld-Reorder, Typ-Änderung, Feld-Entfernung): nie unterstützt → Fehler
- Enum-Wert-Änderungen: erst ab Lookup-Tabelle (Phase 3+) unterstützt
- Inlined Funktionen (`@noreload`): kein Code-Hot-Reload
- `@noreload`-Typen: kein Schema-Reload, kein Padding-Overhead
- AOT Multi-Thread ohne fptable: Trampoline-Atomizität nur Single-Thread sicher
- Struct-Größe > Padding-Limit: Major-Version-Bump nötig (selten, aber möglich)
- Warning-Regen: viele outdated Instanzen → viele Warnings (Throttling empfohlen)
