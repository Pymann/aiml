# sprint_task_17

## Titel

Compiler-Diagnostics auf verlässliche AIML-Datei/Zeile/Spalte zentralisieren

## Status

Neu. Weiterhin offen und kurzfristig sinnvoll.

## Ziel

Compiler- und Semantikfehler sollen verlässlich als AIML-Diagnostics erscheinen, also im Format `aiml_file:line[:column] message`, statt nur als rohe `LOG_ERROR(...)`-Ausgabe mit C++-Datei oder ganz ohne AIML-Quellort.

## Warum dieser Task noetig ist

Der aktuelle Compiler hat bereits mehrere Vorarbeiten:

- [ast_nodes.h](../../src/core/ast_nodes.h) haengt `sourceNode` und `sourceLine` an AST-Knoten.
- [ast_builder.cpp](../../src/core/ast_builder.cpp) aktualisiert `currentLine` aus `YAML::Mark()`.
- [context.h](../../src/core/context.h) hat mit `currentSourceFile`, `currentLine`, `currentFunctionName` sowie `fmtLoc()` bereits eine primitive Location-Surface.

Trotzdem ist die Fehleroberflaeche nicht konsistent zentralisiert.

Im aktuellen Stand gibt es viele Compilerpfade, die weiterhin direkt `LOG_ERROR(...)` verwenden, ohne `fmtLoc(...)` oder node-basierte Diagnostics. Eine Stichprobe ueber den aktuellen Code zeigt:

- 142 `LOG_ERROR(...)`-Stellen im Compiler-/Core-Code
- davon nur 12 mit `fmtLoc(...)`
- also 130 Stellen ohne expliziten AIML-Quellort

Das erklaert den beobachteten Effekt: Manche Fehler zeigen AIML-Datei/Zeile, andere enden nur als generische Compiler- oder C++-interne Meldung.

## Aktueller Stand

### Bereits vorhanden

- Primitive Location-Tracking-Felder im [context.h](../../src/core/context.h)
- AST-Knoten mit YAML-Ursprung in [ast_nodes.h](../../src/core/ast_nodes.h)
- Teilweise lokalisierte Fehlerpfade, z. B. in [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)

### Noch nicht sauber geloest

- Viele Semantikfehler in [ast_compiler.cpp](../../src/core/ast_compiler.cpp), [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp), [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp), [utils.cpp](../../src/core/utils.cpp) und [builtin_registry.cpp](../../src/core/builtin_registry.cpp) laufen noch ohne AIML-Quellort.
- Parse-/Builder-Fehler wie in [expression_builder.cpp](../../src/core/ast/expression_builder.cpp) nutzen rohe `LOG_ERROR(...)`-Meldungen.
- Top-Level-Fehler in [compiler.cpp](../../compiler.cpp) fangen YAML-/AST-/Import-Fehler nicht durchgaengig in einem einheitlichen AIML-Diagnostic-Format ab.
- Es existiert sogar noch mindestens ein roher `throw std::runtime_error(...)`-Pfad in [context.h](../../src/core/context.h), der die zentrale Diagnostic-Schicht komplett umgeht.

## Was an der bisherigen Planung fehlte

Das Thema war bisher teilweise in [sprint_task_16_stacktraces_and_crash_diagnostics.md](../../sprint_task_16_stacktraces_and_crash_diagnostics.md) enthalten, dort aber als Teil einer viel breiteren Crash-/Runtime-Architektur.

Fuer die aktuelle Compiler-Qualitaet ist ein eigener Task sinnvoller, weil das Problem enger ist:

- keine Runtime-Frames
- keine Signal-Handler
- kein nativer Stacktrace
- sondern die Vollstaendigkeit der AIML-Compilerdiagnostics

Task 17 ist damit kein Ersatz fuer Task 16, sondern eine gezielte Vorstufe: Compilerfehler muessen erst verlässlich lokalisiert sein, bevor AIML-Stacktraces und groessere Diagnostic-Pipelines wirklich sauber wirken.

## Fokus

1. Eine kleine zentrale Compiler-Diagnostic-API einfuehren.
2. Rohe `LOG_ERROR(...)`-Pfade mit AST-/Node-/Location-kontext auf diese API umstellen.
3. Parse-, Builder-, Semantik- und Top-Level-Compilefehler in ein einheitliches AIML-Diagnostic-Format bringen.
4. Negative Repros hinzufuegen, die nicht nur auf Fehlerschlag pruefen, sondern explizit auf Datei/Zeile/Spalte im Fehlertext.

## Betroffene Stellen

- [compiler.cpp](../../compiler.cpp)
- [src/core/context.h](../../src/core/context.h)
- [src/core/ast_nodes.h](../../src/core/ast_nodes.h)
- [src/core/ast_builder.cpp](../../src/core/ast_builder.cpp)
- [src/core/ast_compiler.cpp](../../src/core/ast_compiler.cpp)
- [src/core/ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)
- [src/core/ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)
- [src/core/ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)
- [src/core/utils.cpp](../../src/core/utils.cpp)
- [src/core/string_utils.h](../../src/core/string_utils.h)
- [src/core/builtin_registry.cpp](../../src/core/builtin_registry.cpp)
- [logger.h](../../logger.h)

## Konkrete Problemklassen

### 1. AST-/Semantikfehler ohne Node-Lokalisierung

Beispiele:

- `Unknown parent class`
- `Unknown model`
- `Duplicate member`
- `Undefined property`
- `Variable shadowing`
- `Unknown class for new`

Diese Fehler entstehen oft an AST-Knoten, loggen aber heute noch ohne deren `sourceNode`.

### 2. Builder-/Parsefehler ohne sauberen AIML-Ort

Beispiele:

- veraltete Operatoren in [expression_builder.cpp](../../src/core/ast/expression_builder.cpp)
- String-Expansion-Parsefehler in [string_utils.h](../../src/core/string_utils.h)
- YAML-Ausnahmen in [compiler.cpp](../../compiler.cpp)

Hier muss klar getrennt werden zwischen:

- echter Parser-/YAML-Lokation
- aktueller Builder-Zeile
- importierter Datei

### 3. Top-Level-Compilefehler ohne einheitliches Diagnostic-Objekt

Beispiele:

- Import-Aufloesung
- `compileFromString`
- Objektdatei-/IR-Emission
- `verifyModule`-Fehler

Nicht jeder dieser Fehler hat eine AIML-Node. Der Task muss daher eine saubere Staffelung definieren:

- `loc from node`
- sonst `loc from current compiler state`
- sonst `file only`
- sonst bewusst als nicht-AIML-internen Toolchainfehler kennzeichnen

### 4. Rohe Exceptions statt Diagnostics

`throw std::runtime_error(...)` in Compiler-Core ist mit diesem Zielbild unvertraeglich, wenn der Fehler eigentlich ein lokalisierbarer AIML-Semantikfehler ist.

## Zielarchitektur

Es braucht eine kleine zentrale Surface, zum Beispiel im oder neben [context.h](../../src/core/context.h):

- `DiagnosticLoc`
- `ctx.error(node, message)`
- `ctx.error(loc, message)`
- `ctx.errorCurrent(message)`
- optional `ctx.note(...)`

Diese API sollte:

- Datei, Zeile, Spalte und optional Funktion normalisieren
- den finalen Text formatieren
- einen `hadError`-Zustand zentral setzen koennen
- spaeter mit Task 16 kompatibel bleiben

Wichtig ist, dass neue Compilerfehler nicht mehr direkt `LOG_ERROR(...)` aufrufen, wenn sie semantisch an AIML-Quellcode haengen.

## Abgrenzung zu Task 16

Task 16:

- native Crashdiagnostics
- AIML-Runtime-Stacktraces
- Crash-Handler
- thread-local AIML-Frames

Task 17:

- Compiler- und Semantikdiagnostics
- Vollstaendigkeit und Konsistenz der AIML-Datei/Zeile/Spalte
- Builder-/Parser-/Semantikfehler im normalen Fehlerrueckkanal

Empfohlene Reihenfolge:

1. Task 17 zuerst fuer saubere Compilerfehler
2. danach Task 16 fuer Runtime- und Crashdiagnostics

## Akzeptanzkriterien

- Ein typischer Semantikfehler wie unbekanntes Feld, unbekannter Typ, falscher Memberzugriff oder Shadowing erscheint verlässlich mit AIML-Datei und Zeile.
- Parse-/Builder-Fehler verlieren die Datei nicht, auch bei Imports oder `compileFromString`.
- Die ueblichen Compilerpfade nutzen keine rohen `LOG_ERROR(...)`-Meldungen mehr fuer lokalisierbare AIML-Semantikfehler.
- Der verbleibende Einsatz roher `LOG_ERROR(...)` ist auf echte Toolchain-/Runtime-/Hostfehler begrenzt.
- Es gibt Regressionstests fuer mindestens:
  - unbekanntes Feld
  - unbekannter Typ
  - veralteter Operator
  - Shadowing
  - YAML-Parsefehler
  - Fehler in importierter Datei
  - Fehler ueber `compileFromString`
- Fehlerformat ist fuer diese Faelle konsistent lesbar, bevorzugt `file:line[:column] message`.

## Empfohlene erste Umsetzung

1. `DiagnosticLoc` und `ctx.error(...)` definieren.
2. Ein enges Paket besonders haeufiger AST-/Semantikfehler auf die neue API umstellen:
   - [ast_compiler.cpp](../../src/core/ast_compiler.cpp)
   - [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)
   - [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)
3. Builder-/Parsefehler nachziehen.
4. Top-Level-Compilerpfade in [compiler.cpp](../../compiler.cpp) vereinheitlichen.
5. Negative Tests auf Diagnoseinhalt ergaenzen.
