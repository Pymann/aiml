# Store Multiopen Issue

## Kurzfassung

Ja, `Store_multiopen_issue.md` trifft den Kern.

Es gibt aktuell mehrere reproduzierbare Fehler im persistenten AionDB-Pfad. Der erste sichtbare und eindeutigste Fehler sitzt bei `Store<T>`, daher passt der Dateiname weiterhin.

Aktueller Stand:

- Zwei `db.open(..., {persist: true, path: ...})`-Aufrufe mit demselben `name` und `path`
- im selben Prozess
- liefern offenbar keinen konsistent gemeinsam sichtbaren Live-Zustand

Zusätzlich zeigen neue Reopen-Tests:

- `Store<T>` scheitert bereits beim ersten persistenten `insert`
- `Kv` und `TimeSeries` bestehen den Same-Process-Reuse-Test
- `Kv` und `TimeSeries` verlieren ihren Zustand aber über getrennte Läufe hinweg

Damit ist der Befund:

- `Store<T>` ist aktuell der sichtbar kaputteste Persistenzpfad
- `Kv` und `TimeSeries` sind lokal im selben Prozess besser
- echte Persistenz über mehrere Runs ist aber auch dort nicht verlässlich

## Repro

Neuer Test:

- [test_aiondb_persist_reuse.aiml](../../tests/test_aiondb_persist_reuse.aiml)

Ablauf:

1. `players_a = db.open("persist_reuse_players", {persist: true, path: db_path})`
2. `players_b = db.open("persist_reuse_players", {persist: true, path: db_path})`
3. Insert über `players_a`
4. `players_b.len()` und `players_b.get(eid)` prüfen

Ist-Ergebnis:

```text
FAIL store_reuse_len base=0 got=0
```

Erwartung:

- Nach dem Insert über `players_a` müsste `players_b.len()` denselben neuen Stand sehen.

## Gegenprobe

Neuer Test:

- [test_aiondb_persist_reuse_kv_ts.aiml](../../tests/test_aiondb_persist_reuse_kv_ts.aiml)

Ergebnis:

```text
AionDB kv/ts persist reuse PASS
```

Das grenzt den Fehler sauber ein:

- `Kv`-Multiopen im selben Prozess: ok
- `TimeSeries`-Multiopen im selben Prozess: ok
- `Store<T>`-Multiopen im selben Prozess: kaputt

## Neue Reopen-Tests

Neue Tests:

- [test_aiondb_store_reopen_write.aiml](../../tests/test_aiondb_store_reopen_write.aiml)
- [test_aiondb_store_reopen_read.aiml](../../tests/test_aiondb_store_reopen_read.aiml)
- [test_aiondb_kv_reopen_write.aiml](../../tests/test_aiondb_kv_reopen_write.aiml)
- [test_aiondb_kv_reopen_read.aiml](../../tests/test_aiondb_kv_reopen_read.aiml)
- [test_aiondb_ts_reopen_write.aiml](../../tests/test_aiondb_ts_reopen_write.aiml)
- [test_aiondb_ts_reopen_read.aiml](../../tests/test_aiondb_ts_reopen_read.aiml)

Ergebnisse:

```text
FAIL store_reopen_write_id=-1
AionDB kv reopen write PASS
AionDB ts reopen write PASS
FAIL kv_reopen_read_mode=
FAIL ts_reopen_read_len=0
```

Interpretation:

- `Store<T>` scheitert schon beim ersten persistenten Schreibversuch
- `Kv` und `TimeSeries` schreiben innerhalb eines Laufs erfolgreich
- beim zweiten getrennten Lauf kommt der Zustand aber nicht mehr zurück

## Zusätzlicher Befund: `path`-Option aus Variablen ist sehr verdächtig

Die neuen Reopen- und Reuse-Tests verwenden:

- `decl: [db_path, path, "/tmp/..."]`
- danach `path: db_path` in `db.open` / `db.open_kv` / `db.open_timeseries`

Nach den Write-Tests existieren die erwarteten Verzeichnisse in `/tmp/` aber gar nicht:

- `/tmp/aiml_test_aiondb_kv_reopen_20260319/` fehlt
- `/tmp/aiml_test_aiondb_ts_reopen_20260319/` fehlt
- `/tmp/aiml_test_aiondb_store_reopen_20260319/` fehlt

Gleichzeitig funktionieren ältere Tests mit Literal-Pfaden weiterhin und erzeugen Dateien in `/tmp/`, z. B.:

- `/tmp/aiml_test_deck_persist_ops/deck_persist_ops.kvwal`
- `/tmp/aiml_test_card_heap_persist/card_heap_persist.kvwal`

Das macht einen zusätzlichen Compiler-/Builtin-Verdacht sehr plausibel:

- Die `path`-Option wird aus Variablen (`path: db_path`) nicht korrekt an die Runtime durchgereicht.

Dann erklären sich die Symptome sauber:

- `Store<T>` schlägt sichtbar fehl, weil `aiml_db_store_open_persist(...)` bei Open-/Mmap-Problemen `nullptr` zurückgeben kann.
- `Kv` und `TimeSeries` wirken zunächst erfolgreich, weil sie selbst bei fehlgeschlagenem `open()` trotzdem einen Handle zurückgeben und Fehler still maskieren.

## Erwartete Semantik laut Runtime

In [db_runtime.cpp](../../src/sys/db_runtime.cpp#L279) bis [db_runtime.cpp](../../src/sys/db_runtime.cpp#L282) steht explizit:

- gleicher `name + path`
- gleicher Prozess
- vorhandenen persistenten Handle wiederverwenden

Relevant:

- `g_persistent_stores_by_name`
- `registry_key = bp + nm`
- `if (it != g_persistent_stores_by_name.end()) return it->second;`

Der Test zeigt aber, dass diese erwartete Reuse-Semantik beim `Store<T>` aus Sicht des AIML-Codes nicht ankommt.

## Wahrscheinlichste Interpretation

Der Fehler liegt nicht nur im WAL-/Checkpoint-Verhalten, sondern allgemeiner im persistenten Open-/Handle-Pfad.

Denn:

- Der Repro scheitert schon im selben Prozess.
- Er scheitert sofort nach `insert`, also vor jedem echten Reopen in einem neuen Prozess.
- `db.checkpoint` ist dafür nicht nötig.

## Mögliche C++-Ursachen

### 1. `Store<T>`-Open schlägt fehl und wird im AIML-Code nur indirekt sichtbar

In [db_runtime.cpp](../../src/sys/db_runtime.cpp#L289) bis [db_runtime.cpp](../../src/sys/db_runtime.cpp#L350) kann `aiml_db_store_open_persist(...)` bei Fehlern `nullptr` zurückgeben:

- `open(db_path, ...) < 0`
- `mmap(...) == MAP_FAILED`
- initiales `mmap` nach `ftruncate` schlägt fehl

Das passt gut zu den beobachteten Symptomen:

- `players.len()` auf `nullptr` ergibt in [db_runtime.cpp](../../src/sys/db_runtime.cpp#L497) bis [db_runtime.cpp](../../src/sys/db_runtime.cpp#L500) einfach `0`
- `players.insert(...)` auf `nullptr` ergibt in [db_runtime.cpp](../../src/sys/db_runtime.cpp#L378) bis [db_runtime.cpp](../../src/sys/db_runtime.cpp#L381) `-1`

Genau dieses Muster sehen die neuen Tests:

- `len == 0`
- `insert == -1`

### 2. `Store<T>` könnte zusätzlich ein `elem_size`-Mismatch haben

In [db_runtime.cpp](../../src/sys/db_runtime.cpp#L378) bis [db_runtime.cpp](../../src/sys/db_runtime.cpp#L382) liefert `aiml_db_store_insert(...)` auch dann `-1`, wenn `elem_size != s->elem_size`.

Das ist die zweite plausible Erklärung für `FAIL store_reopen_write_id=-1`.

Relevant ist dabei:

- `db.open` berechnet `elemSize` in [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L1773) bis [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L1843)
- `Store.insert` berechnet `elemSize` separat in [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L803) bis [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L823)

Wenn diese beiden Pfade nicht exakt dieselbe Größe emittieren, ist `insert == -1` die direkte Folge.

### 3. `Kv` und `TimeSeries` maskieren `open()`-Fehler stillschweigend

In [db_runtime.cpp](../../src/sys/db_runtime.cpp#L618) bis [db_runtime.cpp](../../src/sys/db_runtime.cpp#L643) und [db_runtime.cpp](../../src/sys/db_runtime.cpp#L908) bis [db_runtime.cpp](../../src/sys/db_runtime.cpp#L933) gilt:

- `open(...)` kann fehlschlagen
- trotzdem wird ein `AimlKv*` bzw. `AimlTimeSeries*` zurückgegeben
- der Handle wird sogar in die globalen Persistent-Registries eingetragen

Das ist gefährlich, weil der Fehler damit wie ein leerer Store aussieht statt wie ein echter Open-Fehler.

Das passt sehr gut zu den neuen Reopen-Ergebnissen:

- Write-Test innerhalb eines Laufs kann gegen den in-memory Zustand funktionieren
- `db.checkpoint` ist bei `wal_fd < 0` bzw. `log_fd < 0` in [db_runtime.cpp](../../src/sys/db_runtime.cpp#L645) bis [db_runtime.cpp](../../src/sys/db_runtime.cpp#L663) und [db_runtime.cpp](../../src/sys/db_runtime.cpp#L937) bis [db_runtime.cpp](../../src/sys/db_runtime.cpp#L944) ein stilles `0`
- der zweite Prozess sieht dann einfach nichts mehr

### 4. Die `path`-Option aus Variablen könnte im Compiler/Builtin verloren gehen

Der relevante Übergabepfad ist:

- Vorverarbeitung der Option Bags in [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L95) bis [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L136)
- Nutzung von `ctx.dbOpenBasePathVal` in [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L1829) bis [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L1953)

Verdacht:

- `path: <VariableExpr>` wird zwar formal erkannt,
- aber der zur Laufzeit übergebene Wert ist offenbar nicht der erwartete `/tmp/...`-String.

Indizien:

- neue Tests mit `path`-Variable erzeugen keine Dateien am Zielort
- ältere Tests mit Literal-Pfad funktionieren
- `path` ist ein eigener Typ mit Interning in [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L113) bis [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L121) und [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L296) bis [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp#L304), also genau dort liegt ein plausibler Integrationsfehler

### 5. `Kv`-/`TimeSeries`-Replay hat keinerlei harte Fehlerdiagnostik

Auch wenn die Datei geöffnet wird, fehlen an kritischen Stellen belastbare Fehlerchecks:

- `write(...)`-Rückgaben werden ignoriert
- `read(...)`-Abbrüche führen nur zu stillem `break`
- `fsync(...)`-Rückgaben werden nicht ausgewertet
- `mkdir(...)`-Fehler werden ignoriert

Relevant:

- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L538) bis [db_runtime.cpp](../../src/sys/db_runtime.cpp#L574)
- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L644) bis [db_runtime.cpp](../../src/sys/db_runtime.cpp#L663)
- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L920) bis [db_runtime.cpp](../../src/sys/db_runtime.cpp#L944)

## Noch nicht abgedeckt

Weiterhin offen:

- `Store<T>`-Cross-Run-Reopen nach funktionierendem Erstschreibfall
- `Store<T>` `update`/`delete` nach erfolgreichem persistenten Open
- persistenter Column-Layout-Store
- typed `Kv`-Cross-Run-Reopen
- Diagnose, ob `Kv`/`TimeSeries` wirklich nicht schreiben oder nur still mit ungültigem FD laufen

Diese Folgefälle sind nachrangig, solange:

- `Store<T>` schon beim ersten persistenten Insert scheitert
- und `Kv`/`TimeSeries` Reopen über getrennte Läufe den geschriebenen Zustand nicht wiederfinden

## Konkreter Fixplan

### 1. `path`-Übergabe zuerst verifizieren und härten

Betroffene Stellen:

- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L95)
- [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L1829)

Vorgehen:

- `path: <VariableExpr>` im `db.*open`-Option-Bag gezielt instrumentieren
- im Runtime-Open temporär den tatsächlich empfangenen `base_path` loggen
- prüfen, ob bei `path: db_path` wirklich `/tmp/...` ankommt oder leer/default

Wahrscheinlich nötige Änderung:

- Option-Bag-Parsing für `path` robuster machen
- sicherstellen, dass `visitExpression(u->operand)` für `path`-Variablen stabil ein `i8*` liefert
- gegebenenfalls den `path`-Wert vor Übergabe explizit als `char*`/interned path normalisieren

### 2. Persistente `open()`-Fehler in der Runtime nicht mehr still maskieren

Betroffene Stellen:

- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L618)
- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L908)

Vorgehen:

- Wenn `open()` fehlschlägt, nicht trotzdem einen scheinbar gültigen `Kv`-/`TimeSeries`-Handle zurückgeben
- Fehler sichtbar machen:
  - `nullptr` zurückgeben, oder
  - zumindest einen klaren `stderr`-Fehler loggen und den Handle nicht registrieren

Nutzen:

- Der Unterschied zwischen “leer” und “Persistenz-Open kaputt” wird sofort sichtbar
- Folgefehler wie `checkpoint == 0` trotz ungültigem FD verschwinden

### 3. `Store<T>`-Open gegen `insert`-Pfad auf `elem_size`-Konsistenz prüfen

Betroffene Stellen:

- [builtin_registry.cpp](../../src/core/builtin_registry.cpp#L1773)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L803)
- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L378)

Vorgehen:

- für denselben Modelltyp den berechneten `elemSize` in `db.open` und `Store.insert` gegeneinander loggen
- prüfen, ob `packed_size` vs. `DataLayout` an irgendeiner Stelle auseinanderläuft
- bei Mismatch entweder:
  - beide Pfade auf dieselbe Größenquelle zwingen, oder
  - im Fehlerfall eine explizite Diagnose ausgeben statt nur `-1`

### 4. `Store<T>`-Persistenzpfad bei Open/Mmap sauber diagnostizieren

Betroffene Stelle:

- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L289)

Vorgehen:

- bei jedem frühen Fehlerpfad temporär Logging ergänzen:
  - `open(db_fd)` fehlgeschlagen
  - `mmap(existing)` fehlgeschlagen
  - `mmap(initial)` fehlgeschlagen
  - Header ungültig / `elem_size` stimmt nicht

Nutzen:

- trennt sofort `nullptr`-Open von `elem_size`-Mismatch
- erklärt den aktuellen `len == 0` / `insert == -1`-Befund eindeutig

### 5. `Kv`-/`TimeSeries`-Replay und Checkpoint mit echten Fehlercodes versehen

Betroffene Stellen:

- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L538)
- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L644)
- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L920)
- [db_runtime.cpp](../../src/sys/db_runtime.cpp#L937)

Vorgehen:

- `write`, `read`, `fsync`, `mkdir`, `ftruncate`, `lseek` auf Rückgabewerte prüfen
- bei Fehlern:
  - klar loggen
  - `checkpoint` nicht mehr blind `0` zurückgeben

### 6. Danach Tests in dieser Reihenfolge erneut fahren

1. [test_aiondb_store_reopen_write.aiml](../../tests/test_aiondb_store_reopen_write.aiml)
2. [test_aiondb_persist_reuse.aiml](../../tests/test_aiondb_persist_reuse.aiml)
3. [test_aiondb_kv_reopen_write.aiml](../../tests/test_aiondb_kv_reopen_write.aiml)
4. [test_aiondb_kv_reopen_read.aiml](../../tests/test_aiondb_kv_reopen_read.aiml)
5. [test_aiondb_ts_reopen_write.aiml](../../tests/test_aiondb_ts_reopen_write.aiml)
6. [test_aiondb_ts_reopen_read.aiml](../../tests/test_aiondb_ts_reopen_read.aiml)

Empfohlene Priorität:

- zuerst `path`-Übergabe und `Store<T>`-Open/Insert diagnostizieren
- danach `Kv`/`TimeSeries`-Open-Fehlermaskierung beseitigen
- erst dann Replay-/Checkpoint-Details nachziehen
