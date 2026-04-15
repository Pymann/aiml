# sprint_task_16

Titel: Native Crash-Stacktraces und AIML-Stacktraces als gemeinsame Diagnostic-Architektur

## Status

Dieses Dokument ersetzt:

- `architecture_sprint_plan.md`
- `sprint_task_15_source_locations_and_diagnostics.md`

Beide Dokumente waren fuer den aktuellen Stand zu grob oder zu eng geschnitten:

- das Architekturpapier war zu breit und nicht diagnostic-zentriert
- der Source-Location-Task war zu schmal und hat Runtime-/Crash-Pfade nicht mitgedacht

Ziel dieses Dokuments ist ein einziger umsetzbarer Plan fuer:

- native Stacktraces auf `stderr`
- AIML-Stacktraces auf `stderr`
- zentrale Source-Locations und Compiler-Diagnostics
- die dafuer noetigen Compiler-/Runtime-/JIT-/AOT-Schnittstellen

## Zielbild

Wenn AIML abstuerzt oder mit einem schweren Compiler-/Runtime-Fehler endet, soll die Ausgabe auf `stderr` deutlich besser sein.

Zielbild 1: nativer Crash

```text
=== AIML NATIVE CRASH (SIGSEGV) ===
Fault address: 0x...
Thread: main
Native stacktrace:
  0: aiml(+0x...)
  1: AIMLCompiler::jitRun()
  2: ...
```

Zielbild 2: AIML-Runtime-Crash waehrend Codeausfuehrung

```text
=== AIML NATIVE CRASH (SIGSEGV) ===
...
AIML stacktrace:
  0: Player.update at game/player.aiml:42:7
  1: Engine.tick at sys/engine.aiml:118:3
  2: main at main.aiml:7:1
```

Zielbild 3: Compiler-/Semantikfehler ohne Prozesscrash

```text
frontend/session.aiml:413:15 Undefined property 'hp_maxx' on model 'Player'
```

## Warum das ein Architekturthema ist

Das Thema ist nicht nur ein Signal-Handler oder nur ein Compiler-Polish.

Es greift in mehrere Ebenen ein:

- Prozessstart und Fatal-Signal-Behandlung
- zentrale Logging-/Diagnostic-Schnittstellen
- AST-Source-Locations
- CompilerContext und Error-Propagation
- IR-Emission fuer AIML-Callframes
- Runtime-Storage fuer thread-lokale AIML-Frames
- JIT- und AOT-Paritaet

Wenn diese Teile separat geloest werden, entstehen wieder Punktloesungen:

- Mirror hat eigenen Crash-Handler
- Compilerfehler kennen nur C++-Dateien
- JIT hat keine AIML-Frames
- AOT und JIT verhalten sich unterschiedlich

Genau das soll diese Sprint-Planung vermeiden.

## Aktueller Stand

### Bereits vorhanden

- In `src/sys/mirror_runtime.cpp` existiert bereits ein lokaler POSIX-Crash-Handler mit `sigaction`, `backtrace()` und `backtrace_symbols_fd()`.
- AST-Knoten tragen bereits `YAML::Node sourceNode` und teilweise `sourceLine`.
- Fuer `log.*` gibt es schon Quellkontext-Injektion.
- Top-Level-Decls werden beim Parse/Compile teilweise mit YAML-Nodes verknuepft.

### Noch nicht vorhanden

- ein globaler, offizieller Crash-Handler fuer den ganzen Prozess
- `std::set_terminate(...)`-Handling fuer ungefangene C++-Exceptions
- normalisierte `SourceLoc`-Abstraktion mit Datei/Zeile/Spalte
- zentrale Diagnostic-API im CompilerContext
- thread-local AIML-Frame-Stack
- automatische Push/Pop-Instrumentierung bei AIML-Funktionsaufrufen
- formatierter AIML-Stacktrace auf `stderr`
- klare JIT/AOT-Paritaet fuer Diagnostics

## Betroffene C++-Stellen

Prozess / Einstieg:

- `main.cpp`
- `logger.h`
- optional neuer Datei-Block wie `src/core/crash_diagnostics.{h,cpp}`

Compiler / Source-Locations / Diagnostics:

- `compiler.cpp`
- `compiler.h`
- `src/core/ast_nodes.h`
- `src/core/ast_builder.cpp`
- `src/core/context.h`
- `src/core/ast_compiler.cpp`
- `src/core/ast_compiler_expr.cpp`
- `src/core/ast_compiler_stmt.cpp`
- `src/core/ast_compiler_call.cpp`

Runtime / Callstack / Crash-Dump:

- `exec_impl.cpp`
- `src/sys/mirror_runtime.cpp`
- ggf. weitere Runtime-Einstiegspunkte fuer Host-Callbacks / Threads

Tests / Repros:

- `tests/`
- neue negative Crash-/Diagnostic-Repros

## Leitplanken

### 1. Native Crash-Ausgabe muss signal-safe bleiben

Im Fatal-Signal-Handler darf nur sehr defensiver Code laufen.

Erlaubt:

- `write(2, ...)`
- `backtrace()` / `backtrace_symbols_fd()` als pragmatischer POSIX-Weg
- einfache statische Buffer
- Re-Raise des Signals nach Reset auf Default

Nicht im Signal-Handler:

- `std::string`
- `new`
- iostreams
- komplexe Locks
- YAML / LLVM / sonstige grosse Libraries

### 2. AIML-Frames muessen thread-local und robust sein

Der AIML-Frame-Stack darf nicht an einem globalen Singleton haengen.

Vorgabe:

- `thread_local` Storage
- einfacher POD-aehnlicher Frame-Eintrag
- Push/Pop ueber RAII oder klar symmetrische Runtime-Calls

### 3. JIT und AOT muessen dieselbe Semantik sehen

Es darf nicht passieren, dass:

- AOT native Stacktraces hat, JIT aber keine AIML-Frames
- JIT bessere AIML-Frames hat als AOT
- Compilerdiagnostics in einem Modus mehr Source-Info liefern als im anderen

### 4. Diagnostics muessen zentral werden

Kein weiteres Wachstum von rohen `LOG_ERROR(...)`-Pfaden ohne AIML-Kontext.

Stattdessen:

- `ctx.error(node, msg)`
- `ctx.error(loc, msg)`
- `ctx.runtimeError(loc, msg)`
- optional `ctx.note(...)`

## Architekturentscheidung

Es gibt zwei unterschiedliche, aber kombinierbare Stacktraces:

### A. Nativer Stacktrace

Quelle:

- C++ Frames
- LLVM/JIT/AOT Runtime
- Prozesssignal / `terminate`

Nutzen:

- native Crashes finden
- schlechte Pointer / LLVM / Runtime-Fehler eingrenzen
- symbolisierte Backtraces fuer C++-Debugging

### B. AIML-Stacktrace

Quelle:

- explizit gepflegte AIML-Callframes
- basiert auf Compiler-Metadaten und Runtime-Push/Pop

Nutzen:

- Nutzer sieht AIML-Funktionskette statt C++-Interna
- Diagnosen bei JIT-Runtime-Problemen werden praktisch nutzbar
- Compiler-/Runtime-Fehler koennen auf AIML-Datei/Zeile rueckbezogen werden

Beide sollen nebeneinander existieren.

## Sprint-Struktur

### Phase 0: Foundation und Format festlegen

Ziel:

- offizielles Diagnostic-Format definieren
- keine parallelen Ad-hoc-Formate mehr

Arbeit:

- Ausgabeformat fuer native Crash-Header definieren
- Ausgabeformat fuer AIML-Frames definieren
- `SourceLoc`-Format definieren: `file`, `line`, `column`, optional `function`, `kind`
- Decide on relative paths statt absolute Pfade auf `stderr`

Deliverables:

- kleine Diagnostic-Spec im Code oder in diesem Dokument umgesetzt
- benoetigte Zielstrings fuer Tests fixiert

### Phase 1: Globalen nativen Crash-Handler zentralisieren

Ziel:

- vorhandene lokale Crash-Logik aus `mirror_runtime.cpp` in einen offiziellen globalen Pfad ueberfuehren

Arbeit:

- neue zentrale Komponente z. B. `src/core/crash_diagnostics.cpp`
- Install-Funktion frueh in `main.cpp`
- Signale mindestens:
  - `SIGSEGV`
  - `SIGABRT`
  - `SIGBUS`
  - optional `SIGILL`
  - optional `SIGFPE`
- `std::set_terminate(...)` fuer ungefangene C++-Exceptions
- Header auf `stderr` mit Signalname / Thread-Kontext / Hinweis auf Symbolisierung
- vorhandene Mirror-spezifische Installation entfernen oder auf die zentrale API umbiegen

Architekturentscheidung:

- ein einziger Installer im Prozess
- keine subsystem-spezifischen Constructor-Hooks mehr fuer Crash-Handling

Akzeptanzkriterien:

- absichtlicher `SIGSEGV` druckt nativen Stacktrace auf `stderr`
- ungefangene C++-Exception druckt `terminate`-Hinweis auf `stderr`
- Mirror nutzt keinen separaten Sonderweg mehr

### Phase 2: `SourceLoc` als offizielle Compiler-Abstraktion einfuehren

Ziel:

- AST-/Compiler-/Runtime-Pfade sprechen ueber eine normale Quellort-Struktur statt implizit ueber `YAML::Node`

Arbeit:

- `SourceLoc`-Struct einfuehren
- mindestens:
  - `file`
  - `line`
  - `column`
  - optional `function_name`
  - optional `context_kind`
- `Node` in `ast_nodes.h` erweitert oder begleitet durch `SourceLoc`
- Helper zur Konvertierung von `YAML::Node` / `YAML::Mark`
- relative Dateipfade zur Projektwurzel normalisieren

Wichtig:

- `YAML::Node sourceNode` kann vorerst bleiben
- `SourceLoc` wird die offizielle Surface fuer Diagnostics

Akzeptanzkriterien:

- neue AST-Knoten koennen `SourceLoc` tragen
- `sourceLine` als Sonderfall ist nicht mehr die Haupt-API

### Phase 3: Builder und AST-Propagation vervollstaendigen

Ziel:

- relevante Expressions, Statements und Top-Level-Declarations tragen verlässlich `SourceLoc`

Arbeit:

- `ast_builder.cpp` auf systematische Loc-Propagation umstellen
- Call-, Decl-, Set-, If-, While-, Return-, Throw-, Try- und Top-Level-Knoten absichern
- bei aus YAML generierten Hilfsknoten Fallback-Regeln festlegen

Risiko:

- heute werden manche Nodes nur punktuell annotiert; das fuehrt spaeter zu halbleeren AIML-Frames

Akzeptanzkriterien:

- Kernknoten der Sprache haben Locations
- `log.*` bleibt kompatibel und nutzt idealerweise dieselbe Loc-Quelle

### Phase 4: Compiler-Diagnostics zentralisieren

Ziel:

- semantische Fehler nicht mehr roh ueber verstreute `LOG_ERROR(...)`-Aufrufe melden

Arbeit:

- zentrale Helpers in `CompilerContext`
- Fehlerformat: `aiml_file:line:column message`
- haeufige Fehler zuerst umstellen:
  - unknown type
  - undefined variable
  - undefined property
  - falsche Argumentzahl
  - falscher Argumenttyp
  - ungueltiger Memberzugriff
- Parser-/Importfehler mit importierter Datei verbessern
- IR-/Verifier-Fehler mit letztem bekannten AIML-Kontext anreichern

Architekturentscheidung:

- Diagnostic-Ausgabe kommt aus einer Stelle
- Logging ist nur Transport, nicht Ort der Formatentscheidung

Akzeptanzkriterien:

- typische Semantikfehler zeigen AIML-Datei/Zeile/Spalte
- spaete Compilerfehler zeigen mindestens den letzten verantwortlichen AIML-Kontext

### Phase 5: AIML-Frame-Stack in der Runtime einfuehren

Ziel:

- eigene AIML-Callframes zur Laufzeit fuehren, unabhaengig vom nativen Stack

Arbeit:

- Runtime-Struct `AimlFrame`
- `thread_local` Stack oder einfacher thread-local Frame-Vector
- Felder mindestens:
  - AIML-Funktionsname
  - `SourceLoc`
  - optional Namespace / Datei / Call-Kind
- Runtime-API:
  - `aiml_push_frame(...)`
  - `aiml_pop_frame()`
  - `aiml_dump_stacktrace_fd(int fd)`

Wichtige Designfragen:

- Push/Pop als echte Runtime-Funktionen oder Inline-Helfer
- Max-Frame-Limit gegen Rekursions-/Corruption-Faelle
- Verhalten bei Stack-Imbalance in Exceptions / early returns

Akzeptanzkriterien:

- Runtime kann den aktuellen AIML-Frame-Stack unabhaengig vom nativen Stack dumpen
- Multi-Threading verschmiert die Frames nicht zwischen Threads

### Phase 6: Compiler-Instrumentierung fuer AIML-Frames

Ziel:

- JIT- und AOT-generierter Code pflegt die AIML-Frames automatisch

Arbeit:

- bei Eintritt in AIML-Funktionen `push_frame`
- bei allen Rueckkehrpfaden `pop_frame`
- bei Throws / exceptional exits die Symmetrie sichern
- nach Moeglichkeit RAII-artiges Muster auf IR-Ebene oder zentralisierte Epilog-Pfade
- Builtins / Host-Callbacks pruefen:
  - ob sie selbst AIML-Frames brauchen
  - oder nur den aufrufenden AIML-Frame sichtbar lassen

Betroffene Stellen:

- `ast_compiler.cpp`
- `ast_compiler_call.cpp`
- `ast_compiler_stmt.cpp`
- ggf. Entry-Point-Generator

Risiken:

- mehrere Return-Blocks
- Exception-Pfade
- Tail-call-artige Optimierungen
- JIT/AOT-Unterschiede bei Wrapper-Funktionen

Akzeptanzkriterien:

- bei normalem Lauf bleibt der Frame-Stack konsistent leer nach Funktionsende
- bei Crash mitten in AIML-Code ist die letzte AIML-Kette sichtbar

### Phase 7: Native Crash-Handler und AIML-Frames verbinden

Ziel:

- ein Fatal-Signal druckt beide Perspektiven

Arbeit:

- Signal-Handler druckt erst nativen Header und nativen Stacktrace
- danach, wenn robust moeglich, AIML-Frames ueber signal-sicheren Dump-Pfad ausgeben
- fuer `std::terminate(...)` ebenfalls nativen + AIML-Stack dumpen

Wichtige Einschränkung:

- Nicht alles am AIML-Dump darf signal-unsafe sein.
- Falls `aiml_dump_stacktrace_fd(...)` nur mit rohen C-Strings/POD-Daten arbeitet, ist das realistisch.
- Falls doch zu viele `std::string`-Abhaengigkeiten benoetigt werden, muss fuer Fatal-Signale ein vorbereiteter, signal-armer Snapshot-Ansatz gebaut werden.

Pragmatische Entscheidung:

- lieber ein reduzierter, robuster AIML-Frame-Dump als ein "schicker" aber unzuverlaessiger Handler

Akzeptanzkriterien:

- bei absichtlichem Crash in AIML-Lauf erscheint nativer plus AIML-Stacktrace auf `stderr`

### Phase 8: Symbolisierung und Build-Integration

Ziel:

- native Backtraces sind lesbar genug, um praktisch nuetzlich zu sein

Arbeit:

- Debug-Builds standardisieren: `-g`
- fuer AOT moeglichst `-rdynamic` oder aequivalente Symbol-Export-Strategie
- dokumentieren, was im Release-Build sichtbar bleibt
- optional Umgebungsflag fuer erweiterten Crash-Output

Nicht Ziel dieser Phase:

- vollautomatische externe Symbolizer-Integration wie `addr2line` im Signal-Handler

Akzeptanzkriterien:

- Debug-Build liefert brauchbare C++-Symbolnamen
- Verhalten in Release ist dokumentiert

## Testplan

### Native Crash-Tests

- absichtlicher `SIGSEGV` in kleinem Repro
- `abort()`-Repro
- ungefangene `throw`-Exception

### Compiler-Diagnostic-Tests

- unknown type
- undefined property
- wrong argument type
- import failure
- verifier error with last known AIML context

### AIML-Stacktrace-Tests

- einfache Call-Kette `main -> a -> b -> c`
- Crash in `c`
- pruefen, dass `stderr` AIML-Frames in richtiger Reihenfolge zeigt
- Rekursions-/Mehrfachreturn-Faelle
- Throw/Catch-Pfade

### Paritaets-Tests

- JIT-Lauf
- AOT-Lauf
- identischer AIML-Stacktrace in beiden Modi fuer denselben Repro

### Negativtests

- Crash ausserhalb von AIML-Code: nur nativer Stacktrace, AIML-Stack evtl. leer
- leerer Frame-Stack darf den Crash-Handler nicht selbst kaputt machen

## Risiken und offene Entscheidungen

### 1. Signal-Safety vs. Komfort

Ein schoener formattierter AIML-Stacktrace ist wertlos, wenn er im Crash-Handler selbst wieder crasht.

Entscheidung:

- Robustheit vor Schoenheit

### 2. Frame-Lebensdauer bei Exceptions

Wenn Push/Pop nicht ueber alle Exit-Pfade symmetrisch ist, ist der AIML-Stacktrace unbrauchbar.

Entscheidung:

- keine verstreuten ad-hoc Pops
- zentralisierte Epilog-/Cleanup-Pfade bevorzugen

### 3. Host-Threads und Callbacks

Falls AIML-Funktionen spaeter auf Worker-Threads oder ueber Callbacks laufen, muss die Frame-Fuehrung das respektieren.

Entscheidung:

- von Anfang an `thread_local`

### 4. Source-Loc-Vollstaendigkeit

AIML-Frames sind nur so gut wie ihre `SourceLoc`-Daten.

Entscheidung:

- zuerst AST-Propagation wirklich sauber machen, bevor viel Frame-Logik gebaut wird

## Empfohlene Reihenfolge

1. Phase 1: globaler nativer Crash-Handler
2. Phase 2: `SourceLoc`
3. Phase 3: AST-Propagation
4. Phase 4: Compiler-Diagnostics zentralisieren
5. Phase 5: Runtime-Frame-Stack
6. Phase 6: Compiler-Instrumentierung
7. Phase 7: kombinierter Crash-Dump
8. Phase 8: Symbolisierung / Build-Integration

## Minimaler erster vertikaler Slice

Wenn nur der kleinste nuetzliche Block zuerst gebaut werden soll:

1. globalen nativen Crash-Handler zentralisieren
2. `SourceLoc` einfuehren
3. einen einzigen Semantikfehlerpfad ueber `ctx.error(node, msg)` umbauen
4. AIML-Frame-Stack fuer normale Funktionscalls instrumentieren
5. bei `SIGSEGV` AIML-Frames mit ausgeben

Damit haette AIML schon frueh:

- brauchbare native `stderr`-Crash-Ausgabe
- erste echte AIML-Frames
- den Beweis, dass die Architektur durchgaengig funktioniert

## Umsetzungszuschnitt als Subtasks

Damit `sprint_task_16` nicht als ein einziger Riesenblock endet, wird die Arbeit als sequenzielle Subtasks geschnitten.

### sprint_task_16_1

Titel: Globalen nativen Crash-Handler zentralisieren

Scope:

- zentrale Crash-Diagnostics-Komponente einfuehren
- Signal-Handler aus `mirror_runtime.cpp` auf offiziellen Prozesspfad ueberfuehren
- `std::set_terminate(...)` anbinden

Betroffene Dateien:

- `main.cpp`
- `src/sys/mirror_runtime.cpp`
- neuer Block wie `src/core/crash_diagnostics.{h,cpp}`

Akzeptanzkriterien:

- `SIGSEGV` und `SIGABRT` drucken nativen Stacktrace auf `stderr`
- Mirror installiert keinen separaten Sonderweg mehr

### sprint_task_16_2

Titel: `SourceLoc` als offizielle Quellort-Abstraktion einfuehren

Scope:

- `SourceLoc`-Struct definieren
- AST-/Compiler-Helfer fuer Datei/Zeile/Spalte bauen
- relative Pfade zur Projektwurzel normalisieren

Betroffene Dateien:

- `src/core/ast_nodes.h`
- `src/core/context.h`
- `compiler.cpp`

Akzeptanzkriterien:

- Kern-AST-Knoten koennen eine normalisierte Source-Location tragen
- `sourceLine` ist nicht mehr die einzige Quellort-API

### sprint_task_16_3

Titel: AST-Builder und Parse-Pfade auf vollstaendige Source-Locations umstellen

Scope:

- Expressions, Statements und Top-Level-Declarations konsequent annotieren
- Fallback-Regeln fuer generierte Hilfsknoten definieren

Betroffene Dateien:

- `src/core/ast_builder.cpp`
- Builder-Unterdateien in `src/core/ast/`

Akzeptanzkriterien:

- Funktionsaufrufe, Memberzugriffe, Returns, Throws und Top-Level-Decls tragen brauchbare Locations

### sprint_task_16_4

Titel: Compiler-Diagnostics zentralisieren

Scope:

- `ctx.error(node, msg)` / `ctx.error(loc, msg)` einfuehren
- haeufige Semantikfehler auf die neue API umstellen
- Parser-/Import-/Verifier-Kontext verbessern

Betroffene Dateien:

- `src/core/context.h`
- `src/core/ast_compiler.cpp`
- `src/core/ast_compiler_expr.cpp`
- `src/core/ast_compiler_stmt.cpp`
- `src/core/ast_compiler_call.cpp`
- `compiler.cpp`

Akzeptanzkriterien:

- typische Compilerfehler zeigen AIML-Datei/Zeile/Spalte statt nur C++-Logstellen

### sprint_task_16_5

Titel: AIML-Frame-Stack in der Runtime einfuehren

Scope:

- `thread_local` AIML-Frame-Storage
- Runtime-API fuer `push_frame`, `pop_frame`, `dump_stacktrace_fd`
- robuste, signal-arme Frame-Datenstruktur

Betroffene Dateien:

- `exec_impl.cpp`
- neuer Runtime-Block wie `src/core/aiml_stacktrace.{h,cpp}`

Akzeptanzkriterien:

- Runtime kann den aktuellen AIML-Frame-Stack unabhaengig vom nativen Stack dumpen

### sprint_task_16_6

Titel: Compiler-Instrumentierung fuer AIML-Frames in JIT und AOT

Scope:

- Push/Pop bei AIML-Funktionsaufrufen emittieren
- Return-/Throw-/Exceptional-Paths symmetrisch machen
- Wrapper und Entry-Points angleichen

Betroffene Dateien:

- `src/core/ast_compiler.cpp`
- `src/core/ast_compiler_call.cpp`
- `src/core/ast_compiler_stmt.cpp`
- `compiler.cpp`

Akzeptanzkriterien:

- JIT und AOT pflegen denselben AIML-Frame-Stack-Stil
- nach normalem Lauf bleibt kein Frame-Leck zurueck

### sprint_task_16_7

Titel: Kombinierten nativen plus AIML-Crashdump ausgeben

Scope:

- Crash-Handler und AIML-Frame-Dump zusammenfuehren
- `stderr`-Format finalisieren
- Verhalten bei leerem AIML-Stack absichern

Betroffene Dateien:

- `src/core/crash_diagnostics.{h,cpp}`
- `src/core/aiml_stacktrace.{h,cpp}`
- `main.cpp`

Akzeptanzkriterien:

- Crash in AIML-Code zeigt nativen und AIML-Stacktrace auf `stderr`

### sprint_task_16_8

Titel: Symbolisierung, Build-Integration und Repro-Tests abschliessen

Scope:

- Debug-Symbolik fuer sinnvolle native Backtraces sichern
- JIT/AOT-Repros und Crash-Tests ergänzen
- Dokumentation fuer Debug- vs Release-Verhalten abschliessen

Betroffene Dateien:

- Build-/Run-Skripte
- `tests/`
- ggf. Doku in `aiml.md` oder separatem Runtime-Doc

Akzeptanzkriterien:

- native Backtraces sind im Debug-Build lesbar
- gezielte Repros decken `SIGSEGV`, `terminate`, Compiler-Diagnostics und AIML-Frames ab

## Empfohlene Reihenfolge der Subtasks

1. `sprint_task_16_1`
2. `sprint_task_16_2`
3. `sprint_task_16_3`
4. `sprint_task_16_4`
5. `sprint_task_16_5`
6. `sprint_task_16_6`
7. `sprint_task_16_7`
8. `sprint_task_16_8`

Diese Reihenfolge ist absichtlich streng:

- erst nativer Crashpfad
- dann Source-Locations
- dann Compiler-Diagnostics
- erst danach AIML-Frames und deren Instrumentierung

## Done-Definition

Die Arbeit ist erst dann wirklich fertig, wenn alle folgenden Punkte gelten:

- native Fatal-Signale drucken zentral einen Backtrace auf `stderr`
- ungefangene C++-Exceptions drucken einen `terminate`-Trace
- typische Compilerdiagnostics zeigen AIML-Datei/Zeile/Spalte
- JIT und AOT liefern fuer denselben Repro denselben AIML-Stacktrace-Stil
- Mirror und andere Runtime-Teile nutzen keinen Sonderweg mehr fuer Crash-Handling
- die Diagnostic-/Stacktrace-Architektur ist dokumentiert und ersetzt alte Planungsdokumente
