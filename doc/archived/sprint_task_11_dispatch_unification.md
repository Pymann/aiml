# sprint_task_11

## Titel

Dispatch-Verantwortung fuer Calls und Builtins bereinigen

## Status

Stark zu reframen; die alte Fassung enthielt mehrere veraltete Pfade.

## Ziel

Es soll klar sein, welche Stellen heute wirklich Aufrufsemantik entscheiden: AST-Call-Compiler, Builtin-Registry und Runtime-gebundene Registrierungslogik.

## Aktueller Stand

- `call_dispatcher.cpp` existiert im aktuellen Stand nicht.
- `expression.cpp` als Legacy-Referenz ist ebenfalls kein realer aktiver Kernpfad.
- Der relevante Ist-Zuschnitt liegt heute vor allem in [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp) und [builtin_registry.cpp](../../src/core/builtin_registry.cpp).

## Was an der alten Fassung veraltet war

- Mehrere genannte Dateien/Pfade sind als aktive Parallelwahrheiten ueberholt.
- Die Task muss heute weniger „Dispatcher vereinheitlichen“ als „Besitzverhaeltnisse im aktuellen Call-Stack klaeren“.

## Fokus

1. dokumentieren, welche Aufrufklasse wo entschieden wird.
2. Builtin-Semantik nicht zwischen AST-Compiler und Registry diffundieren lassen.
3. veraltete Verweise aus Doku und Codekommentaren entfernen.

## Betroffene Stellen

- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)
- [builtin_registry.cpp](../../src/core/builtin_registry.cpp)
- [builtin_registry.h](../../src/core/builtin_registry.h)
- bei Legacy-Hinweisen: [context.h](../../src/core/context.h)

## Akzeptanzkriterien

- Es ist fuer jede Call-Klasse klar, welche Komponente zustaendig ist.
- Veraltete Dispatcher-Annahmen leben nicht weiter in Sprintplan und Codekommentaren.
- Neue Fixes muessen nicht in mehreren vermeintlichen Parallelpfaden nachgezogen werden.
