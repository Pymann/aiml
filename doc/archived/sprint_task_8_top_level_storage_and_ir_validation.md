# sprint_task_8

## Titel

Top-Level-Storage-Semantik bereinigen und IR-Invarianten absichern

## Status

Teilweise ueberholt, aber weiterhin relevant.

## Ziel

Top-Level-`decl` soll semantisch konsistent bleiben, und invalide IR-Situationen sollen nicht mehr erst tief in spaeten LLVM-Pfaden auffallen.

## Aktueller Stand

- `compiler.cpp` verifiziert Module bereits vor JIT- bzw. Ausfuehrungspfaden.
- Gleichzeitig existieren weiterhin `libraryMode`, `inTopLevelInit`, `__aiml_init` und globale Symbolpromotion im realen Compilerzustand.

## Was an der alten Fassung veraltet war

- „Modulverifikation vor jedem Pfad einfuehren“ ist nicht mehr der Hauptgewinn; Verifikation existiert bereits.
- Der Kern ist heute eher die Speicher- und Initialisierungssemantik von top-level State als der blanke Verifier selbst.

## Fokus

1. Top-Level-Storage-Regel klarziehen: Global, Init-Code und Zugriffspfad.
2. problematische Fremd-Frame-/Alloca-Nutzung weiter einschränken.
3. Verifier-Fehler als klare AIML-Diagnosen nutzbar machen.

## Betroffene Stellen

- [ast_compiler.cpp](../../src/core/ast_compiler.cpp)
- [compiler.cpp](../../compiler.cpp)
- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)
- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)
- [context.h](../../src/core/context.h)

## Akzeptanzkriterien

- Top-Level-State verhaelt sich ueber relevante Pfade konsistent.
- Invalides IR fuehrt zu einem AIML-Fehler statt zu spaeten LLVM-Abstuerzen.
- Verifier-Meldungen sind fuer Repros praktisch nutzbar.
