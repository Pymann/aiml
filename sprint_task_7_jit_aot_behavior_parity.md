# sprint_task_7

## Titel

JIT- und AOT-Verhalten angleichen

## Ziel

Der Compiler soll fuer denselben AIML-Inhalt moeglichst dieselbe Semantik liefern, egal ob ueber Datei-/AOT-Pfad oder String-/Chunk-/JIT-Pfad kompiliert wird.

## Hintergrund

Die Analyse in [jit_vs_aot.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/jit_vs_aot.md) zeigt mehrere echte Unterschiede:

- anderer Compilerkontext
- andere Import- und Header-Propagation
- andere Objektdatei- und Linkbehandlung
- andere Initialisierung und Symbolregistrierung

Das ist nicht nur Tooling-Unterschied, sondern kann Sprachverhalten veraendern.

## Scope

- Compilerkontext zwischen `compile()` und `compileFromString()` angleichen
- Import- und Linksemantik vereinheitlichen
- Objektartefakte und `objectFilesToLink` konsistent behandeln
- Initialisierungspfade und aktive Kontexte fuer JIT-Lauf pflegen

## Betroffene Stellen

- [jit_vs_aot.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/jit_vs_aot.md)
- [compiler.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/compiler.cpp)
- [ast_compiler.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler.cpp)
- [fptable.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/fptable.cpp)
- [exec_impl.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/exec_impl.cpp)

## Umsetzungsrichtung

1. Die derzeitigen Unterschiede nach Auswirkung sortieren:

- semantisch kritisch
- toolseitig stoerend
- nur intern technisch

2. Zuerst alle semantisch kritischen Punkte angleichen:

- Importauflosung
- `libraryMode`
- `currentSourceFile`
- `objectFilesToLink`

3. Danach die Initialisierungs- und Runtimekontext-Unterschiede zwischen `jitRun()` und Chunk-Pfaden bereinigen.

4. Abschliessend kleine Repros anlegen, die identische Quellen in beiden Pfaden pruefen.

## Akzeptanzkriterien

- Derselbe Quellinhalt verhaelt sich in JIT und AOT fuer Kernfaelle gleich.
- Importierte Typen und Symbole sind nicht mehr pfadabhaengig sichtbar oder unsichtbar.
- Abweichungen sind entweder beseitigt oder bewusst dokumentiert.

## Ergebnis

Nach Abschluss ist JIT gegen AOT kein eigener Sprachdialekt mehr, sondern im Wesentlichen nur noch ein anderer Ausfuehrungspfad.
