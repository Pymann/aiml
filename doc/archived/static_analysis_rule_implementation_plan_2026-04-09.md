# Static Analysis Rule Implementation Plan

Datum: 2026-04-09

## Ziel

Weitere statische Strukturregeln fuer AIML einfuehren, ohne die laufende
9rooms-Entwicklung unnötig zu stören.

Der Plan priorisiert:

- minimale Compiler-Eingriffe
- klare Regeldefinitionen
- testbare, lokale AST-Analysen
- kontrollierte Einfuehrung mit moeglichen Escape-Hatches ueber `static_off`

---

## Ausgangslage

Bereits implementiert:

- Dateigroesse Warning ab `1000` Zeilen
- Dateigroesse Hard Error ab `1500` Zeilen
- Funktionsgroesse Hard Error ab `301` Zeilen
- `static_off: ["lines"]` zum lokalen Abschalten der Funktionslinienregel

Relevante Implementierungsorte:

- statische Strukturdiagnostik in [compiler.cpp](../../compiler.cpp)
- AST-Struktur in [ast_nodes.h](../../src/core/ast_nodes.h)
- Statement-Dispatch in [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)

Wichtiger Architekturpunkt:

- Die neue Static Analysis sollte weiterhin vor oder parallel zum eigentlichen
  Codegen in `compiler.cpp` stattfinden.
- Neue Regeln sollten nicht in `visitDeclStmt`, `visitIfStmt`, `visitWhileStmt`
  oder andere IR-nahe Codepfade gemischt werden.

---

## Kandidatenregeln

### 1. Maximale lokale Variablenzahl pro Funktion

Technische Schwierigkeit: niedrig bis mittel

Was gezaehlt werden kann:

- alle `DeclStmt` im Funktionskoerper
- inklusive verschachtelter Bloecke
- exklusive Funktionsargumente
- exklusive Klassenmember
- Catch-Variablen optional, aber besser zunaechst exklusive

Warum die Regel sinnvoll ist:

- viele lokale Variablen sind fuer KI-gestuetzte Bearbeitung unguenstig
- die Regel misst echte kognitive Last besser als reine Dateigroesse

Empfohlene Erstsemantik:

- zaehle nur `DeclStmt`
- keine Sonderfaelle fuer Typen
- kein Scope-Gewicht, reine Anzahl

### 2. Maximale Verschachtelungstiefe

Technische Schwierigkeit: niedrig

Was gezaehlt werden kann:

- `if`
- `while`
- `try`

Optional spaeter:

- `catch` als eigener Level
- tiefere Ausdrucksverschachtelung separat

Warum die Regel sinnvoll ist:

- hohe Nesting-Tiefe ist eine direkte Lesbarkeits- und Wartungsbarriere
- die AST-Struktur dafuer ist bereits vorhanden

Empfohlene Erstsemantik:

- Starttiefe `0`
- beim Betreten von `if`, `while`, `try` `+1`
- Maximum ueber den gesamten Funktionskoerper bilden

### 3. Maximale Statement-Anzahl pro Funktion

Technische Schwierigkeit: niedrig bis mittel

Status:

- nicht erste Prioritaet
- kann spaeter als robustere Strukturmetrik neben der Zeilenregel dienen

Warum spaeter:

- Zeilenregel existiert bereits
- Variablenzahl und Nesting-Tiefe liefern erst einmal mehr neuen Nutzen

---

## Empfohlene Einfuehrungsreihenfolge

### Phase 1

Nur Plan, Spezifikation und gezielte Tests vorbereiten.

Keine produktive Aktivierung neuer Regeln.

Ziel:

- 9rooms nicht blockieren
- Semantik praezise festlegen
- Repro-Tests vorab haben

### Phase 2

Nesting-Tiefe implementieren.

Warum zuerst:

- kleinster Eingriff
- klarste Semantik
- einfachster AST-Walk
- niedrigstes Risiko fuer unerwartete Seiteneffekte

### Phase 3

Lokale Variablenzahl implementieren.

Warum danach:

- ebenfalls lokal im AST bestimmbar
- etwas mehr Designklaerung noetig als bei Nesting

### Phase 4

Optional:

- Statement-Anzahl
- weitere Strukturmetriken
- ggf. Warning- und Error-Stufen differenzieren

---

## Konkretes Zieldesign

## A. Neue Analysehelfer in `compiler.cpp`

Analoge Struktur wie heute bei:

- `maxStmtSourceLine(...)`
- `maxBlockSourceLine(...)`
- `logAimlFunctionSizeDiagnostic(...)`

Neu hinzufuegen:

- `countDeclsInStmt(...)`
- `countDeclsInBlock(...)`
- `maxNestingDepthStmt(...)`
- `maxNestingDepthBlock(...)`
- `logAimlFunctionVarCountDiagnostic(...)`
- `logAimlFunctionNestingDiagnostic(...)`

Wichtig:

- reine AST-Funktionen
- keine Abhaengigkeit vom IR-Generator
- keine Seiteneffekte

## B. `static_off` erweitern

Aktuell existiert:

- `"lines"`

Empfohlene neue Keys:

- `"vars"`
- `"nesting"`

Semantik:

- `"vars"` deaktiviert nur die Variablenanzahl-Regel
- `"nesting"` deaktiviert nur die Verschachtelungstiefen-Regel

Nicht empfehlen:

- ein globales `"all"`
- dateiweite Abschaltungen
- projektweite Abschaltungen

## C. Compilerdiagnostik

Format analog zu bestehenden Meldungen:

Beispiele:

```text
...:42: AIML function 'foo' has too many local declarations (37). Maximum is 32.
...:42: AIML function 'foo' is nested too deeply (6). Maximum is 4.
```

Wichtig:

- Meldung immer mit Datei und Funktionsstartzeile
- Regelname in Text klar benennen

---

## Empfohlene Schwellwerte fuer die erste Version

Noch nicht sofort aktivieren, sondern zuerst gegen reale Codebasis messen.

Vorschlag fuer Startwerte:

- lokale Variablenzahl Warning ab `24`, Hard Error ab `32`
- Nesting-Tiefe Warning ab `4`, Hard Error ab `6`

Warum konservativ:

- 9rooms und bestehende AIML-Tests sollen nicht sofort kollabieren
- erst reale Verteilung messen, dann haerter drehen

Wenn maximale Schonung fuer 9rooms priorisiert wird:

- zunaechst nur Warnings
- Hard Errors erst nach einer Aufraeumphase

---

## Empfohlene Einfuehrungsstrategie ohne 9rooms-Störung

### Stufe 1: Nur Analysefunktionen + Tests

Implementieren:

- AST-Walker fuer `decl`-Anzahl
- AST-Walker fuer Nesting-Tiefe
- noch keine aktive Compilerdiagnostik

Zweck:

- Logik isoliert verifizieren
- Null Risiko fuer 9rooms

### Stufe 2: Opt-in Logging hinter Debug-Schalter

Moeglichkeit:

- bei gesetztem Debug-Flag nur `LOG_DEBUG` oder `LOG_WARNING`
- noch keine Build-Blockade

Zweck:

- reale Messwerte aus 9rooms und AIML-Suite sammeln

### Stufe 3: Warnings aktivieren

Nach Datensichtung:

- zuerst nur Warning-Stufen aktivieren
- kein Hard Error

Zweck:

- Problemstellen sichtbar machen
- 9rooms weiter entwickeln lassen

### Stufe 4: Hard Errors aktivieren

Erst wenn:

- groesste Ausreisser bereinigt sind
- `static_off`-Ausnahmen gezielt gesetzt wurden
- Tests stabil bleiben

---

## Testplan

### 1. Unit-nahe Compiler-Repros

Neue kleine AIML-Testfaelle:

- Funktion mit vielen `decl` innerhalb flacher Struktur
- Funktion mit `decl` in verschachtelten `if`/`while`
- Funktion mit tiefer Nesting-Kette
- Funktion knapp unter und knapp ueber dem Grenzwert
- Funktion mit `static_off: ["vars"]`
- Funktion mit `static_off: ["nesting"]`

### 2. Negativ-/Positiv-Paare

Beispiele:

- `test_static_analysis_vars_ok.aiml`
- `test_static_analysis_vars_fail.aiml`
- `test_static_analysis_nesting_ok.aiml`
- `test_static_analysis_nesting_fail.aiml`

### 3. Suite-Schutz

Nach jeder Aktivierungsstufe:

- `./run_all_tests.sh --no-build`

Und fuer reale Auswirkungen:

- gezielte Builds der betroffenen 9rooms-Ziele

---

## Offene Designfragen

Vor Implementierung final klaeren:

1. Zaehlen Funktionsargumente zur Variablenzahl?
Empfehlung: nein

2. Zaehlt `catchVar` als lokale Variable?
Empfehlung: zunaechst nein

3. Zaehlt `else` als eigener Nesting-Level?
Empfehlung: nein, nur das umschliessende `if`

4. Zaehlt `try` und `catch` zusammen als ein Level oder getrennt?
Empfehlung: zunaechst ein Level fuer `try`

5. Sollen neue Regeln sofort Hard Errors sein?
Empfehlung: nein

---

## Minimaler Implementierungsschnitt

Wenn nur ein kleiner, risikoarmer erster Schritt gewuenscht ist:

1. Nesting-Tiefen-Analyse implementieren
2. `static_off: ["nesting"]` vorsehen
3. noch keine aktive Error-Regel
4. nur Tests und optionales Debug-Logging

Das ist der kleinste sinnvolle Einstieg mit minimaler Gefahr fuer 9rooms.

---

## Empfehlung

Konkrete Empfehlung fuer das naechste Arbeitspaket:

1. Spec fuer neue Regelkeys in `49_static_analysis.md` noch nicht erweitern
2. zuerst Mess- und Analysefunktionen implementieren
3. nur Nesting-Tiefe als ersten Pilot bauen
4. Variablenzahl direkt danach
5. neue Regeln anfangs nur als Warning oder Debug-Metrik laufen lassen
6. Hard Errors erst nach Sicht auf reale 9rooms-Ausreisser aktivieren

Damit bleibt die 9rooms-Entwicklung beweglich, waehrend die Static-Analysis
schrittweise ausgebaut wird.
