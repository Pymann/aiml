# sprint_task_9

## Titel

`CompilerContext` entlang echter Verantwortungen aufspalten

## Status

Weiterhin offen und strukturell wichtig.

## Ziel

Den grossen Ambient-State-Container [context.h](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/context.h) so zu entschlacken, dass neue Features nicht weiter an einen God-Context gekoppelt werden.

## Aktueller Stand

- `CompilerContext` ist weiterhin gross und traegt Semantik-, Codegen-, Import-, Init-, Serialisierungs- und Mirror-bezogene Zustandsfelder.
- Der Task ist heute eher noch dringlicher als in der Erstfassung, weil viele neuere Themen daran anhaengen.

## Was an der alten Fassung veraltet war

- Der vorgeschlagene Zielschnitt war als starres Klassenset formuliert.
- Sinnvoller ist heute ein schrittweiser Schnitt nach Lese-/Schreibmustern statt sofortiger Grossaufteilung.

## Fokus

1. Felder inventarisieren und in Cluster gruppieren.
2. Kurzlebigen Call-/Compile-State zuerst herausziehen.
3. dann Import-/Session-/Codegen-Zustand trennen.
4. Adapterphase bewusst einplanen.

## Betroffene Stellen

- [context.h](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/context.h)
- [compiler.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/compiler.cpp)
- [ast_compiler.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler.cpp)

## Akzeptanzkriterien

- Neue Features haengen nicht mehr automatisch an einem globalen God-Context.
- Kurzlebiger Compile-/Call-State ist von langlebigem Modul-/Semantik-State getrennt.
- Migration kann schrittweise erfolgen, ohne den gesamten Compiler in einem Zug zu brechen.
