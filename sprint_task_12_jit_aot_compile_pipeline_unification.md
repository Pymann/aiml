# sprint_task_12

## Titel

Compile-Pipeline fuer Datei, String, JIT und Chunk-Kompilation vereinheitlichen

## Status

Weiterhin offen und klar von Task 7 getrennt.

## Ziel

Die unterschiedlichen Compile-Entry-Points sollen auf einen kleinen gemeinsamen Kern reduziert werden.

## Aktueller Stand

- [compiler.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/compiler.cpp) besitzt weiterhin getrennte Pfade fuer `compile()`, `compileFromString()`, `jitRun()` und `compileChunkAndGetAddress()`.
- Genau das macht die Task weiterhin aktuell.

## Was an der alten Fassung veraltet war

- Inhaltlich wenig; die Aufgabe ist weiterhin treffend.
- Wichtig ist nur die klarere Abgrenzung zu Task 7: Task 12 ist Pipeline-Architektur, nicht Semantik-Paritaet.

## Fokus

1. gemeinsamen Compile-Kern freilegen.
2. Source-Frontend, JIT, Chunk und Object-Emit als Strategien darueberlegen.
3. Symbol-/Library-Preload und Verifikation an wenigen klaren Stellen verankern.

## Betroffene Stellen

- [compiler.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/compiler.cpp)
- [ast_compiler.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler.cpp)

## Akzeptanzkriterien

- Neue Features muessen nicht doppelt in mehreren Compile-Entry-Points nachgeruestet werden.
- Compile-Pfade unterscheiden sich nur noch dort, wo es technisch notwendig ist.
- Verifikation, Import-Setup und Init-Hooks sind an wenigen festen Stellen verankert.
