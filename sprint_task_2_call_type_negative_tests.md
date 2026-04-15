# sprint_task_2

## Titel

Negativtests fuer Call-Typfehler und Arity-Fehler stabilisieren

## Status

Weiterhin offen und direkt an `sprint_task_1` gekoppelt.

## Ziel

Ein kleines, belastbares Negativtestpaket soll sicherstellen, dass falsche Aufrufe reproduzierbar als Compilerfehler enden.

## Aktueller Stand

- Der Call-Pfad ist heute staerker in [ast_compiler_call.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_call.cpp) konzentriert.
- Die alte Fassung verwies noch auf einen Dispatcher-Zuschnitt, der im aktuellen Code so nicht mehr existiert.

## Was an der alten Fassung veraltet war

- `call_dispatcher.cpp` als zentrale Testreferenz ist ueberholt.
- Der Task war als reine Sammlung beschrieben, ohne die Fehlerklassen zu priorisieren.

## Fokus

1. Arity-Fehler.
2. elementare Typmismatches bei User-Funktionen.
3. Methoden- und `fn<...>`-Faelle.
4. nur danach Pointer-/Struct-Randfaelle.

## Betroffene Stellen

- [tests](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/tests)
- [ast_compiler_call.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_call.cpp)
- bei Builtin-Ausnahmen: [builtin_registry.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/builtin_registry.cpp)

## Akzeptanzkriterien

- Fuer jede priorisierte Fehlerklasse existiert mindestens ein Negativtest.
- Die Tests scheitern am Compiler statt erst zur Laufzeit.
- Fehlertexte enthalten mindestens Zielsymbol, Argumentposition und Typursache.
