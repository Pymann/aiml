# Pointer-Return Repro: C++-Fehleranalyse

## Repro

Datei:
- `tests/test_return_types_ptr_repro.aiml`

Inhaltlich minimal:
- top-level `decl g_i32`
- Funktion `ret_ptr_i32() -> ptr<i32>`
- `return [&g_i32]`
- Aufrufer liest den Rueckgabepointer

Lauf:
- `./out/aiml/aiml -r tests/test_return_types_ptr_repro.aiml`

Beobachtung:
- kein normaler Compilerfehler
- harter Absturz mit `exit code -1`

`gdb`-Backtrace:
- SIGSEGV in LLVM `LiveVariables::HandleVirtRegUse`
- Stack ueber `MCJIT::emitObject`, `MCJIT::generateCodeForModule`, `MCJIT::runFunction`
- also kein normaler AIML-Runtime-Fehler, sondern kaputtes oder inkonsistentes erzeugtes LLVM-IR / Machine-IR

## Staerkster Verdacht

Der Repro wirkt aktuell **nicht** wie ein allgemeiner Fehler "Pointer-Returns gehen nie", sondern wie ein spezifischer Fehler:

- `&g_i32` nimmt die Adresse eines **top-level `decl`**
- im normalen Nicht-Library-Pfad werden top-level `decl`s **nicht** als echte Globals angelegt
- stattdessen landen sie als lokale Allocas in `__aiml_init`
- spaeter kompilierte Funktionen sehen diese Symbole aber weiter in `ctx.symbol_table`
- dadurch kann Funktionscode eine Alloca aus einer **anderen Funktion** referenzieren

Das ergibt invalides IR und passt sehr gut zu dem spaeten LLVM-Absturz im Codegen.

## Relevante C++-Stellen

### 1. Top-level `decl` wird im normalen Programm nicht als Global erzeugt

In `src/core/ast_compiler.cpp` wird nur im `libraryMode` der sichere Pfad benutzt:

- top-level `DeclStmt` -> `llvm::GlobalVariable`
- Kommentar dort sagt das selbst explizit

Im normalen Pfad:
- es wird `__aiml_init` erzeugt
- danach wird jedes top-level Node einfach mit `visitTopLevel(...)` besucht

Betroffene Stellen:
- `src/core/ast_compiler.cpp`
  - `compile(...)`, Library-Pfad mit `GlobalVariable`
  - `compile(...)`, Nicht-Library-Pfad mit `__aiml_init`

Konsequenz:
- top-level `decl` im Run-/JIT-Modus lebt wie eine lokale Variable in `__aiml_init`
- nicht wie ein echtes Modulglobal

### 2. `visitDeclStmt(...)` erzeugt Allocas am aktuellen Insert-Point

In `src/core/ast_compiler_stmt.cpp`:
- `visitDeclStmt(...)` ruft `ctx.ir_gen->createAlloca(...)`
- im Nicht-Library-Fall ist der aktuelle Insert-Point fuer top-level Code eben `__aiml_init`

Konsequenz:
- `g_i32` aus dem Repro wird als Stack-/Entry-Alloca von `__aiml_init` angelegt

### 3. `visitFunction(...)` uebernimmt vorhandene Symbole in die Funktionskompilierung

In `src/core/ast_compiler.cpp`:
- `visitFunction(...)` speichert `auto savedSymbols = ctx.symbol_table`
- damit sind top-level Symbole in der Funktionskompilierung weiterhin sichtbar
- es gibt dort keinen Schutz, dass ein Symbol aus einer anderen Funktion stammt

Konsequenz:
- `ret_ptr_i32()` kann `g_i32` erfolgreich aufloesen
- obwohl `g_i32` physisch in `__aiml_init` allokiert wurde

### 4. Unary `&` gibt fuer Variablen blind deren Alloca zurueck

In `src/core/ast_compiler_expr.cpp`:
- `visitUnary(...)`
- bei `&x` wird direkt `sym->ir_alloca_inst` zurueckgegeben

Wichtig:
- dort gibt es keinen Check, ob diese Alloca zum aktuellen `llvm::Function` gehoert
- es wird schlicht die Adresse des Symbols zurueckgegeben

Konsequenz:
- `&g_i32` in `ret_ptr_i32()` gibt sehr wahrscheinlich eine Alloca aus `__aiml_init` zurueck
- das ist fuer die aktuelle Funktion kein gueltiger lokaler Wertursprung

### 5. `visitRetStmt(...)` selbst macht keine Zusatzvalidierung

In `src/core/ast_compiler_stmt.cpp`:
- `visitRetStmt(...)` evaluiert nur den Ausdruck
- dann `ctx.ir_gen->createRet(val)`

Das ist fuer sich genommen nicht der Hauptfehler, aber:
- hier wird der problematische Pointer einfach ungeprueft als Return-Wert emittiert

## Warum das den LLVM-Crash plausibel erklaert

Wenn eine Funktion IR verwendet, das effektiv auf Allocas / Definitionen aus einer anderen Funktion zeigt, ist das IR semantisch kaputt.

Typische Folgen:
- Verifier-Fehler, falls verifiziert wird
- oder spaetere Abstuerze in LLVM-Passes

Genau hier sehen wir letzteres:
- nicht sauberer AIML-Compile-Fehler
- sondern SIGSEGV tief in LLVM LiveVariables waehrend MCJIT-Codegen

Das passt deutlich besser zu **invalidem funktionsuebergreifendem IR** als zu einem einfachen Runtime-Bug.

## Warum `test_return_types.aiml` trotzdem durchlaeuft

Der stabile Test nutzt aktuell nur:
- `i32`-Return
- `string`-Return

Diese Funktionen greifen nicht auf top-level Alloca-Adressen zu.

Deshalb:
- der Basis-Return-Pfad funktioniert
- der spezifische Repro mit `&g_i32` kippt

## Weitere moegliche Auswirkungen

Der Fehler ist wahrscheinlich groesser als nur `ptr<i32>`-Return:

- jede Funktion, die auf ein top-level `decl` im Nicht-Library-Pfad zugreift, koennte betroffen sein
- besonders kritisch:
  - `&global`
  - Rueckgabe von `ptr<T>`
  - Weitergabe solcher Pointer an andere Funktionen
  - moeglicherweise auch direkte Loads aus top-level `decl`, falls sie aus fremdem Funktionskontext kommen

## Wahrscheinlich sinnvollste Fixrichtungen

### Fixrichtung 1: top-level `decl` auch im Nicht-Library-Pfad als echte Globals materialisieren

Das ist die robusteste Richtung.

Statt:
- top-level `decl` als lokale Allocas in `__aiml_init`

besser:
- top-level `decl` immer als `llvm::GlobalVariable`
- `__aiml_init` macht nur noch Initialisierung und Builtin-Init

Das waere konzeptionell konsistent mit dem bereits vorhandenen Library-Pfad.

### Fixrichtung 2: Bis dahin harte Schutzregel im Compiler

Falls der groessere Umbau nicht sofort gemacht wird:

- `visitUnary('&')` sollte ablehnen, wenn das Zielsymbol aus einem anderen `llvm::Function` stammt
- allgemeiner: jede Nutzung eines Symbols mit Alloca aus fremdem Funktionskontext sollte einen Compilerfehler werfen

Dann gaebe es wenigstens:
- klaren AIML-Fehler
- keinen LLVM-SIGSEGV

### Fixrichtung 3: Modulverifikation vor JIT erzwingen

Unabhaengig vom eigentlichen Fix sehr sinnvoll:

- erzeugtes LLVM-Modul verifizieren
- bei Verifier-Fehler sauber abbrechen
- nicht erst LLVM-Passes abstuerzen lassen

## Kurzfazit

Der staerkste aktuelle Befund ist:

- `ptr<i32>`-Return selbst ist nicht zwingend der eigentliche Grundfehler
- der Repro bricht sehr wahrscheinlich, weil `&g_i32` auf ein top-level `decl` zeigt, das im Nicht-Library/JIT-Pfad nur als Alloca in `__aiml_init` existiert
- diese Adresse wird dann aus einer anderen Funktion heraus verwendet und zurueckgegeben
- das erzeugt sehr wahrscheinlich invalides LLVM-IR und fuehrt spaet zum Crash in `LiveVariables`

Der beste naechste technische Schritt waere:
- top-level `decl`-Storage zwischen Library- und Nicht-Library-Pfad vereinheitlichen
- oder mindestens die fremde-Funktion-Alloca-Nutzung hart verbieten
