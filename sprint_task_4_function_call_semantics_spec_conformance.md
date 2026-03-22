# sprint_task_4

## Titel

Funktionsaufrufe an die Spec angleichen

## Ziel

Die Call-Semantik des AIML-Compilers soll die in [07_functions.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/spec/07_functions.md) beschriebenen Regeln deutlich vollstaendiger abbilden.

## Hintergrund

Die Spec beschreibt mehr als heute robust gesichert ist:

- Default-Argumente
- benannte Trailing-Overrides
- `map<string:any>`-basierte Argumentuebergabe mit Validierung
- klares Fehlerverhalten bei Typmismatch

Der aktuelle Call-Pfad ist an mehreren Stellen noch zu weich oder uneinheitlich.

## Scope

- Argumentvalidierung fuer User-Funktionen
- Default-Argumente konsequent anwenden
- benannte Argumente bzw. Trailing-Map-Regeln pruefen und vervollstaendigen
- strukturiertes Fehlerverhalten fuer Typmismatches
- Abgrenzung zwischen Sprachregel und Builtin-Sonderfall

## Betroffene Stellen

- [07_functions.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/spec/07_functions.md)
- [ast_compiler_call.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_call.cpp)
- [call_dispatcher.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/call_dispatcher.cpp)
- [ir_generator.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ir_generator.cpp)
- optional: [builtin_registry.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/builtin_registry.cpp)

## Umsetzungsrichtung

1. Bestehende harte Typpruefung fuer Calls als Basis nehmen.

2. Dann pruefen, welche Spec-Features bereits teilweise vorhanden sind und welche komplett fehlen:

- Defaultwerte in `args`
- benannte Overrides ueber Trailing-Map
- Map-zu-Signatur-Validierung

3. User-Funktionen zuerst sauber machen, Builtins danach gezielt nachziehen.

4. Fehlerformate vereinheitlichen, damit klar ist:

- welcher Parameter falsch war
- welcher Typ erwartet wurde
- welche Sprachregel verletzt ist

## Akzeptanzkriterien

- Default-Argumente funktionieren gemaess Spec oder werden explizit als nicht unterstuetzt dokumentiert.
- Benannte Argumente haben konsistente Regeln.
- Typmismatches enden nicht mehr in spaeten LLVM- oder Runtime-Fehlern.
- Es gibt passende Positiv- und Negativtests fuer diese Semantik.

## Ergebnis

Nach Abschluss ist der Funktionsaufrufpfad deutlich naeher an der Spec und als Sprachfeature wesentlich vorhersehbarer.
