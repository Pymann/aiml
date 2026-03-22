# sprint_task_2

## Titel

Negativtests fuer fehlerhafte Funktionsparameter und Call-Typmismatches

## Ziel

Es soll ein belastbares Testset geben, das sicherstellt, dass der Compiler bei falschen Argumenttypen und ungueltigen Call-Signaturen mit klaren Fehlern abbricht.

## Hintergrund

Sobald `sprint_task_1` die Typpruefung schaerft, braucht der Compiler dafuer stabile Regressionstests. Ohne Negativtests ist die Gefahr hoch, dass spaetere Lockern oder Sonderfaelle die strikte Pruefung wieder aushebeln.

## Scope

Tests fuer:

- normale Funktionen
- Methodenaufrufe
- `fn<...>`-Funktionspointer
- Argumentanzahl
- Struct-/Pointer-Byref-Faelle

## Empfohlene Testfaelle

### Basisfaelle

- `i32` erwartet, `string` uebergeben
- `string` erwartet, `i32` uebergeben
- `bool` erwartet, `string` uebergeben
- `f32` erwartet, ungeeigneten nichtnumerischen Typ uebergeben

### Argumentanzahl

- zu wenige Argumente
- zu viele Argumente

### Methoden

- Objektmethode mit falschem Parametertyp
- Objektmethode mit zu wenigen Argumenten
- Objektmethode mit zu vielen Argumenten

### Struct / Pointer

- Struct erwartet, falscher Struct-Typ uebergeben
- Pointer-/Byref-Fall mit unpassendem Typ
- keine stille Annahme, dass ein beliebiger Struct-Name als gueltige Referenz passt

### fn<...>

- `fn<i32:i32>` mit `string`
- `fn<string:void>` mit Zahl
- falsche Argumentanzahl bei `fn<...>`-Aufruf

## Betroffene Stellen

- [tests](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests)
- indirekt:
  - [ast_compiler_call.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_call.cpp)
  - [call_dispatcher.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/call_dispatcher.cpp)

## Teststrategie

1. Kleine isolierte `.aiml`-Dateien je Fehlerklasse anlegen.

2. Die Tests sollten nicht auf Runtime-Verhalten pruefen, sondern auf Compiler-Fehler.

3. Pro Test muss klar sein:

- welcher Aufruf illegal ist
- welche Fehlermeldung erwartet wird

4. Wenn das aktuelle Testsystem Compiler-Fehler nicht direkt als Negativtest ausdruecken kann, zunaechst ein minimales Harness dafuer schaffen.

## Akzeptanzkriterien

- Fuer jede wichtige Fehlerklasse gibt es mindestens einen Negativtest.
- Die Tests scheitern am Compiler und nicht erst zur Laufzeit.
- Die Fehlertexte sind ausreichend spezifisch, um Fehlursachen schnell zu erkennen.
- Die Tests decken direkte Calls, Methoden und `fn<...>`-Calls ab.

## Ergebnis

Nach Abschluss existiert ein kleines, aber gezieltes Negativtestpaket, das die neue strikte Parametertyppruefung absichert.
