# sprint_task_7

## Titel

Semantische JIT-/AOT-Paritaet herstellen

## Status

Weiterhin offen und klar von Task 12 zu unterscheiden.

## Ziel

Derselbe AIML-Inhalt soll in JIT und AOT dieselbe sichtbare Semantik haben.

## Aktueller Stand

- [compiler.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/compiler.cpp) zeigt weiterhin getrennte Entry-Points fuer `compile`, `compileFromString`, `jitRun` und `compileChunkAndGetAddress`.
- Modulverifikation existiert bereits in beiden Hauptpfaden; das Problem ist heute also weniger „Verifier fehlt“ als „Semantik und Init-Verhalten driften“. 

## Was an der alten Fassung veraltet war

- Der Task vermischte semantische Paritaet mit Pipeline-Architektur.
- Verifikation als Kernluecke ist heute nicht mehr der richtige Schwerpunkt.

## Fokus

1. Import- und Symbolsichtbarkeit.
2. Init-Verhalten inklusive `__aiml_init`.
3. Kontext- und Seiteneffekte zwischen Datei- und Stringpfad.
4. kleine Repros, die beide Pfade gegeneinander halten.

## Betroffene Stellen

- [jit_vs_aot.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/jit_vs_aot.md)
- [compiler.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/compiler.cpp)
- [ast_compiler.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler.cpp)
- [exec_impl.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/exec_impl.cpp)
- [fptable.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/fptable.cpp)

## Akzeptanzkriterien

- Kernfaelle verhalten sich in JIT und AOT gleich.
- Import- und Init-Regeln sind nicht mehr pfadabhaengig ueberraschend.
- Verbleibende Unterschiede sind klein und bewusst dokumentiert.
