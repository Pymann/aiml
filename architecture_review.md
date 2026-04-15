# AIML C++ Architektur-Review

## Kurzfazit

Die AIML-C++-Implementierung hat ein tragfaehiges Grundgeruest:

- klar erkennbare Schichten aus Parser, AST, Compiler und Runtime
- gute subsystemische Trennung in `src/sys/*`
- eine bereits modularisierte AST-Builderschicht

Die groessten Probleme sind heute nicht "falsche Grundidee", sondern **architektonische Ueberlagerung**:

- zu viel mutable Zustandslogik in einem zentralen `CompilerContext`
- mehrere nebeneinander existierende Dispatch- und Compilerpfade
- zu grosse Monolithen in den Schluesseldateien
- inkonsistente Semantik zwischen JIT/AOT und library/non-library
- zu schwache Guardrails gegen invalide IR

Der Code wirkt wie ein System, das erfolgreich gewachsen ist, aber mehrere Evolutionsstufen gleichzeitig mittraegt.

## Was heute gut ist

### 1. Die grobe Schichtung ist richtig

Die Kernaufteilung ist sinnvoll:

- Frontend-Treiber in `compiler.cpp`
- AST-Parsing in `src/core/ast/*`
- AST-zu-LLVM-Codegen in `src/core/ast_compiler*.cpp`
- Builtin-/Methodenverdrahtung in `src/core/builtin_registry.cpp`
- Laufzeitfunktionalitaet in `src/sys/*`

Das ist konzeptionell deutlich besser als ein komplett vermischter Parser-/Runtime-Blob.

### 2. Die AST-Builderschicht ist bereits relativ modular

Positiv auffaellig:

- `src/core/ast_builder.h`
- `src/core/ast/*_builder.cpp`

Hier ist die Sprache in zustaendige Builder zerlegt:

- `function_builder`
- `expression_builder`
- `class_builder`
- `model_builder`
- `extern_builder`
- `control_flow_builder`

Das ist einer der staerkeren Teile der Architektur und ein gutes Vorbild fuer den Rest des Systems.

### 3. Die Runtime ist sinnvoll nach Domaenen geteilt

`src/sys/*` ist trotz teils grosser Dateien inhaltlich sauberer getrennt als der Compilerkern:

- DB in `db_runtime.cpp`
- Mirror in `mirror_runtime.cpp`
- Netzwerk in `net_runtime.cpp`, `ws_runtime.cpp`, `tls_runtime.cpp`
- Rendering/UI/Engine in `engine_runtime.cpp`, `ui_runtime.cpp`, `prefab_runtime.cpp`
- Serialisierung in `ser_runtime.cpp`

Das ist eine gute Basis fuer spaetere Stabilisierung.

## Hauptprobleme

### 1. `CompilerContext` ist ein God Object

Datei:
- `src/core/context.h`

Der `CompilerContext` traegt gleichzeitig:

- LLVM-Kontext und Builder
- Symboltabellen
- Funktionen, Klassen, Enums, Namespaces
- Import- und Header-Only-Zustand
- Hot-Reload-Zustand
- Bundle-/Schema-Zustand
- Deserialisierungszustand
- DB-Open-Options
- Exception-State
- Runtime-/Builtin-Dispatch-Metadaten

Das fuehrt zu drei Problemen:

- Features koppeln sich implizit ueber Ambient State
- Fehler lassen sich schwer isolieren
- JIT/AOT/libraryMode/headerOnlyMode beeinflussen sich indirekt

Ein Symptom dafuer ist z. B. `dbOpenPersist`, `dbOpenColumnLayout` und `dbOpenBasePathVal` als globale Kontextflags fuer einen einzelnen Call-Pfad.

### 2. `builtin_registry.cpp` ist zu gross und zu zentral

Datei:
- `src/core/builtin_registry.cpp`

Groesse:
- ca. `7250` Zeilen

Diese Datei ist aktuell:

- Builtin-Katalog
- Namespace-Dispatch
- Klassen-/Methodenregistrierung
- Typkonvertierungslogik
- Spezialfall-Sammlung fuer Runtime-APIs

Das ist einer der groessten strukturellen Risikofaktoren im Projekt.

Folgen:

- aenderungsanfällig
- schwer testbar
- schlecht navigierbar
- hohe Wahrscheinlichkeit fuer inkonsistente Spezialfaelle

### 3. Es gibt mehrere uebereinander liegende Dispatch-Systeme

Es existieren mindestens diese Pfade:

- `registerBuiltins(ctx)` in `src/core/builtin_registry.cpp`
- `SystemRegistry` in `src/sys/registry.h`
- `CallDispatcher` in `src/core/call_dispatcher.cpp`
- dazu noch Altlasten wie `src/core/expression.cpp`

Das ist architektonisch ungesund.

Besonders auffaellig:

- `ast_compiler.cpp` nutzt den neuen Builtin-Registry-Pfad
- `call_dispatcher.cpp` nutzt noch `SystemRegistry`
- `expression.cpp` ist ein alter YAML-Ausdruckspfad

Das bedeutet:

- mehrere "Wahrheiten" fuer Aufrufsemantik
- erschwerte Feature-Erweiterung
- hoehere Gefahr, dass ein Fix nur in einem Pfad landet

### 4. `compiler.cpp` mischt zu viele Rollen

Datei:
- `compiler.cpp`

Groesse:
- ca. `1826` Zeilen

Diese Datei macht gleichzeitig:

- YAML-Datei laden
- CompilerContext initialisieren
- Importauflösung
- Header-Only-Propagation
- Deferred-Import-Kompilation
- JIT-Setup
- AOT-Objekterzeugung
- Entry-Point-Generierung
- Linking
- Hot-Reload-Chunk-Kompilation
- Runtime-Symbol-Registrierung fuer JIT

Das ist faktisch ein Orchestrator, Linker-Glue, JIT-Launcher und Compiler-Frontend in einer Datei.

Das fuehrt direkt zu den bereits sichtbaren JIT/AOT-Unterschieden.

### 5. Der Code traegt gleichzeitig altes und neues IR-Abstraktionsmodell

In `Symbol` und breiter im Compiler sieht man:

- rohe LLVM-Zeiger: `alloca_inst`, `type`, `element_type`
- neue Wrapper: `ir_alloca_inst`, `ir_type`, `ir_element_type`

Datei:
- `src/core/context.h`

Das ist ein klassischer Migrationszustand, aber langfristig teuer:

- doppelte Invarianten
- doppelte Fehlerquellen
- schwerer lesbarer Code

Dasselbe sieht man auch im Compilerfluss:

- manche Pfade arbeiten direkt mit LLVM
- andere ueber `IRGenerator`
- wieder andere mischen beides im selben Block

### 6. Top-Level-Semantik ist nicht sauber vereinheitlicht

Ein konkretes Architekturproblem, das bereits echte Bugs erzeugt:

- im `libraryMode` werden top-level `DeclStmt` als `GlobalVariable` materialisiert
- im normalen Pfad werden top-level Statements in `__aiml_init` emittiert

Datei:
- `src/core/ast_compiler.cpp`

Dadurch divergiert die Speichersemantik von top-level Variablen.

Das ist kein kleiner Detailfehler, sondern ein Architekturbruch:

- dieselbe Sprachkonstruktion hat je nach Modus andere Storage-Semantik
- das hat bereits zum Pointer-Return-Repro mit invalidem IR beigetragen

### 7. Guardrails gegen invalide IR sind zu schwach

Ein wiederkehrendes Muster in den bisherigen Bugs:

- AIML erzeugt kein klares Diagnoseergebnis
- stattdessen scheitert LLVM spaet oder die Runtime crasht

Beispiele:

- Pointer-Return-Repro endet in LLVM `LiveVariables`
- Mirror-Bugs kippten ueber Debug-Prints oder spaete Runtime-Symptome

`compileToObject(...)` verifiziert das Modul, aber der gesamte JIT-/Chunk-/Run-Pfad ist architektonisch nicht gleich stark abgesichert.

Das ist gefaehrlich, weil gerade bei einer experimentellen Sprache der Compiler aggressiv frueh scheitern sollte.

### 8. Viele Features sind ueber Stringtypen statt ueber explizite Semantik verdrahtet

Im Code tauchen viele Entscheidungen ueber `typeName`-Stringvergleiche auf:

- `list<...>`
- `map<...>`
- `ptr<...>`
- `buffer<...>`
- `"path"`
- `"ui_style"`

Das ist fuer einen Prototyp normal, skaliert aber schlecht.

Folgen:

- viele verstreute Spezialfaelle
- hoher Pflegeaufwand
- schwerere Refactorings
- mehr inkonsistente Eckfaelle

### 9. Diagnostics und Source-Locations sind noch kein echter Compiler-Subsystem

Die meisten Compilerfehler zeigen heute vor allem:

- C++-Quelldatei
- C++-Zeile
- Fehltext

aber nicht die eigentlich betroffene AIML-Stelle.

Das ist architektonisch unguenstig, weil die Grundlage dafuer bereits teilweise existiert:

- AST-Knoten tragen `sourceNode`
- `YAML::Mark().line` wird fuer `log.*` schon verwendet
- Top-Level-Declarations werden beim Parse bereits mit YAML-Nodes verknuepft

Der Bruch ist also nicht fehlende Rohinformation, sondern fehlende Zentralisierung:

- keine offizielle `SourceLoc`-Abstraktion
- keine zentrale Diagnostic-API
- viele rohe `LOG_ERROR(...)`-Pfade ohne AST-/Kontextbezug

Folgen:

- hohe Debug-Kosten
- schlechte Fehlermeldungen fuer Sprachbenutzer
- langsamere Bearbeitung genau der Architektur- und Typfehler, die das System heute haeufig erzeugt

## Konkrete Architekturspannungen

### A. Gute Frontend-Modularisierung vs. schwacher Midend/Backend-Zuschnitt

Der Parser ist besser geschnitten als der Compiler.

Vergleich:

- Parser: viele kleine Builder
- Compiler: grosse Dateien wie
  - `ast_compiler.cpp` (`1312` Zeilen)
  - `ast_compiler_call.cpp` (`1735` Zeilen)
  - `builtin_registry.cpp` (`7250` Zeilen)

Das deutet darauf hin, dass die Frontend-Seite bereits in Richtung guter Architektur gegangen ist, der Backend-Teil aber noch nicht nachgezogen wurde.

### B. Runtime sauber getrennt, aber ABI-/Dispatch-Schicht nicht

Die Domaenenruntimes sind getrennt, aber die Bindung dorthin ist zentral und dicht gekoppelt.

Heute haengt viel davon ab, dass:

- Builtin-Registrierung
- Kontextflags
- Typnamen
- Methodennamen
- spezielle Compiler-Sonderregeln

genau zusammenpassen.

Das macht Featurearbeit teuer und Regressionen wahrscheinlich.

### C. JIT und AOT sind keine zwei Backends, sondern zwei verschiedene Systeme

Die Architektur suggeriert "gleiche Sprache, zwei Ausgabemodi".

Tatsaechlich sind es heute eher:

- ein Datei-/AOT-Pfad mit vollem Import- und Objektmodell
- ein reduzierter String-/JIT-Pfad

Das ist einer der groessten systemischen Komplexitaetstreiber.

## Konkrete Verbesserungsvorschlaege

### sprint_task_1: `CompilerContext` in echte Teilkontexte aufspalten

Empfohlene Teilung:

- `SemanticContext`
  - Symboltabellen
  - Funktionen/Klassen/Enums/Namespaces
  - aktuelle Namespace-/Scope-Infos

- `CodegenContext`
  - LLVMContext
  - Module
  - Builder
  - Insert-Point-/Exception-State
  - IRGenerator

- `CompileSession`
  - libraryMode
  - headerOnlyMode
  - currentSourceFile
  - import/object tracking
  - bundle/hot-reload state

- `CallContext`
  - kurzlebige Call-Optionen wie DB-Open-Flags

Damit verschwinden viele implizite Seiteneffekte.

### sprint_task_2: Builtin-Registry in Module aufspalten

`builtin_registry.cpp` sollte in registrierende Teilmodule zerlegt werden, z. B.:

- `builtin_registry_core.cpp`
- `builtin_registry_io.cpp`
- `builtin_registry_db.cpp`
- `builtin_registry_net.cpp`
- `builtin_registry_ui.cpp`
- `builtin_registry_mirror.cpp`
- `builtin_registry_math.cpp`

Ziel:

- eine gemeinsame Registrierungsoberflaeche behalten
- aber keine 7000-Zeilen-Sammeldatei mehr

### sprint_task_3: Einen einzigen offiziellen Call-/Builtin-Dispatch festlegen

Entscheidung noetig:

- entweder `builtin_registry` ist der offizielle Weg
- oder `CallDispatcher` bleibt zentral

Aber:

- `SystemRegistry`
- `CallDispatcher`
- `builtin_registry`

sollten nicht dauerhaft parallel weiterleben.

Empfehlung:

- `builtin_registry` als Hauptpfad
- `SystemRegistry`/`CallDispatcher` als Legacy markieren und schrittweise entfernen
- `expression.cpp` ganz klar als Legacy dokumentieren oder loeschen

### sprint_task_4: Top-Level-Storage-Semantik vereinheitlichen

Top-level `decl` sollte nicht vom Modus abhaengen.

Empfehlung:

- top-level Storage immer als echte Modul-Globals modellieren
- `__aiml_init` nur fuer Initialisierung, nicht fuer Ownership von Variablen

Das wuerde:

- JIT/AOT angleichen
- library/non-library angleichen
- Pointer-/Global-Bugs reduzieren

### sprint_task_5: JIT/AOT auf gemeinsamen Compile-Kern zwingen

`compile()` und `compileFromString()` sollten denselben Kern benutzen:

- gleicher Context-Aufbau
- gleiche Importstrategie
- gleiche Verifikation
- gleiche Runtime-Symbolvorbereitung, soweit moeglich

Wenn Unterschiede noetig sind, sollten sie explizit in einer kleinen Strategy-Schicht leben, nicht verteilt in `compiler.cpp`.

### sprint_task_6: IR-Validierung vor jedem Codegen-Pfad erzwingen

Empfehlung:

- Modulverifikation immer vor JIT finalization
- bei Chunk-Kompilation ebenfalls
- optional zusaetzlich Funktionsverifikation nach `visitFunction`

Ziel:

- frueher AIML-Compilerfehler
- keine spaeten LLVM-Pass-Crashes

### sprint_task_7: Legacy-IR-/LLVM-Doppelmodell abbauen

Entscheidung:

- entweder `IRGenerator` ist die offizielle Abstraktion
- oder der Compiler arbeitet direkt mit LLVM

Aber nicht dauerhaft beides parallel.

Praktisch:

- `Symbol` schrittweise auf eine Repräsentation reduzieren
- neue Codepfade nur noch ueber die Zielabstraktion bauen
- Alt-Felder markieren und danach entfernen

### sprint_task_8: Typsystem-Regeln zentralisieren

Heute sind viele Sonderregeln ueber Stringvergleiche im Code verteilt.

Empfehlung:

- eine zentrale `TypeSemantics`-Schicht
- dort Antworten wie:
  - ist Heap-Typ?
  - ist Pointer-Typ?
  - ist trivially copyable?
  - braucht Init?
  - braucht Serialize-Descriptor?

Dann muessen `visitDeclStmt`, Builtin-Dispatch und Serializer nicht jeweils eigene Typ-String-Logik nachbauen.

### sprint_task_9: Source-Locations und Diagnostics zentralisieren

Empfehlung:

- offizielle `SourceLoc`-Struktur einfuehren
- Builder soll Positionsdaten konsequent auf Nodes propagieren
- zentrale Compiler-API wie `ctx.error(node, msg)` statt roher `LOG_ERROR(...)`
- zunaechst die haeufigsten Semantikfehler umstellen:
  - unbekannter Typ
  - undefined property
  - undefined variable
  - falsche Argumentzahl/-typen

Nutzen:

- deutlich bessere AIML-Fehlermeldungen
- geringere Debug-Kosten
- schnelleres Arbeiten an allen anderen Architekturbaustellen

## Priorisierung

Wenn nur die wichtigsten Hebel umgesetzt werden sollen, waere meine Reihenfolge:

1. `CompilerContext` aufteilen
2. Top-level-Storage vereinheitlichen
3. Source-Locations und Diagnostics zentralisieren
4. einen einzigen offiziellen Builtin-/Call-Dispatch festlegen
5. `builtin_registry.cpp` zerlegen
6. JIT/AOT ueber einen gemeinsamen Compile-Kern normalisieren
7. harte IR-Verifikation vor jedem Codegen-Pfad

## Gesamtbewertung

Die Architektur ist **nicht kaputt**, aber sie ist deutlich ueber den Punkt hinaus, an dem weitere Features noch billig auf die bestehende Struktur gesetzt werden koennen.

Der Code zeigt zwei sehr unterschiedliche Reifestufen:

- Frontend/AST-Building: schon relativ sauber
- Compiler- und Bindungsschicht: funktional stark, aber strukturell ueberladen

Die wichtigste Einsicht ist:

Das System braucht jetzt weniger neue Einzel-Features und mehr **Entflechtung der zentralen Schichten**.

Wenn diese Entflechtung gelingt, werden auch viele der aktuellen Fehlerklassen seltener:

- JIT/AOT-Divergenzen
- Global-/Pointer-Semantikfehler
- Builtin-Sonderfallregressionen
- schwer erklaerbare Sync-/Serializer-/Call-Bugs
