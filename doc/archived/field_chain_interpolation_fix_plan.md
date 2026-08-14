# AIML: Fixplan fuer Field-Chains in Interpolation und Call-Argumenten

## Problem

Der neue Chain-Support funktioniert inzwischen fuer:

- tiefe Field-Reads/Writes
- List-Index-Ketten wie `a.items-0.hp`
- Method-Chains wie `a.items-0.cost.get("1")`

Aber ein wichtiger Restbruch bleibt:

- Field-Chains in String-Interpolation wie `${hc.iid}`
- dieselben Chain-Ausdruecke in bestimmten Call-Argument-Kontexten

Im aktuellen Stand fuehrt das dazu, dass eigentlich string- oder ptr-basierte
Ausdruecke intern als falscher Typ (`i32`) behandelt werden koennen.

Praktischer Effekt in `9rooms`:

- `${hc.iid}`
- `${g_client_info.player_name}`
- `${g_client_info.deckset_id}`

haben Frontend-Buildfehler ausgeloest, obwohl dieselben Zugriffe als normale
Expressions ausserhalb der Interpolation funktionieren.

## Kernbefund

Das ist kein allgemeiner Field-Chain-Bug mehr, sondern ein Integrationsloch
zwischen:

- AST/Expression-Builder fuer Interpolation
- `visitStringInterpolation(...)`
- und den Typ-/Value-Konventionen von `visitVariable(...)`

Mit anderen Worten:

- `hc.iid` als normale Expression: ok
- `${hc.iid}` als Interpolationsteil: noch nicht robust

## Wahrscheinlichste Ursache

Die String-Interpolation arbeitet heute typsensitiv ueber `visitExpression(...)`
und entscheidet dann anhand des LLVM-Typs, wie ein Teil in Stringform gebracht
wird.

Der Bruch sitzt wahrscheinlich in einem dieser Punkte:

1. ein Interpolationsteil wird nicht als normaler `VariableExpr("hc.iid")`
   gebaut
2. oder `visitVariable(...)` liefert in diesem Pfad nicht den finalen Feldwert,
   sondern einen Zwischenwert mit falscher Typinformation
3. oder `visitStringInterpolation(...)` behandelt den Rueckgabewert eines
   geketteten Feldzugriffs nicht gleich wie den eines einfachen Variablenreads

## Relevante C++-Stellen

Die heissesten Stellen fuer den Fix sind:

- [ast_compiler_expr.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_expr.cpp)
  - `visitStringInterpolation(...)`
  - `visitVariable(...)`
  - `visitExpression(...)`
- [ast/expression_builder.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast/expression_builder.cpp)
  - falls `${...}`-Inhalte dort nicht als normale Expressions gebaut werden
- [string_utils.h](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/string_utils.h)
- [expression.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/expression.cpp)
  - falls dort Sonderbehandlung fuer Variablen-/Pfadauflosung steckt
- optional zur Diagnose:
  - [compiler.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/compiler.cpp)
  - [context.h](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/context.h)

## Zielverhalten

Diese Faelle muessen gleichwertig funktionieren:

```aiml
- decl: [iid, string, hc.iid]
- print: ["iid=${iid}"]
```

und

```aiml
- print: ["iid=${hc.iid}"]
```

ebenso:

```aiml
- call: [log.info, "player=${g_client_info.player_name} deckset=${g_client_info.deckset_id}"]
```

und analog fuer:

- `ptr<T>.field`
- `list<T>-idx.field`
- `obj.deep.field`
- `obj.deep.map.get("x")` innerhalb von `${...}` sobald dort ein String oder
  numerischer Wert entsteht

## Konkreter Implementierungsplan

### sprint_task_1: Repro-Tests fuer Interpolation und direkte String-Kontexte

Neue Tests zuerst anlegen.

Pfad:

- `tests/test_field_chain_interpolation.aiml`
- optional `tests/test_field_chain_call_args.aiml`

Mindestens diese Faelle:

- `${obj.field}`
- `${obj.deep.field}`
- `${obj.items-0.name}`
- `${ptrObj.name}`
- `log.info("x=${obj.field}")`
- Zuweisung in `string`-Variable via Interpolation

Akzeptanz:

- alle laufen gruen
- keine LLVM-Verifier-Fehler
- keine Typdegradierung auf `i32`

Betroffene Stellen:

- [ast_compiler_expr.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_expr.cpp)
- `tests/`

### sprint_task_2: AST-Baum fuer `${...}` normieren

Pruefen, wie `${hc.iid}` intern gebaut wird.

Wenn der Inhalt nicht als normale Expression landet, muss das vereinheitlicht
werden:

- `${...}` soll denselben AST-Expression-Typ bekommen wie ausserhalb der
  Interpolation
- kein eigener vereinfachter „nur Variablenname“-Pfad fuer Chain-Strings

Betroffene Stellen:

- [ast/expression_builder.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast/expression_builder.cpp)
- ggf. [ast_builder.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_builder.cpp)

### sprint_task_3: `visitStringInterpolation(...)` gegen Chain-Ergebnisse haerten

`visitStringInterpolation(...)` darf nicht implizit davon ausgehen, dass ein
Teil nur:

- simpler int
- simpler string ptr
- float

ist.

Stattdessen:

- Ergebnis von `visitExpression(part)` als autoritative Quelle nehmen
- gekettete Feldreads genau wie normale VariableExprs behandeln
- falls noetig: pointer-to-string / loaded string sauber unterscheiden

Betroffene Stellen:

- [ast_compiler_expr.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_expr.cpp)

### sprint_task_4: gemeinsame „value-to-string“-Konvertierung extrahieren

Aktuell steckt die Typbehandlung fuer Interpolation lokal in
`visitStringInterpolation(...)`.

Besser:

- zentralen Helfer einfuehren, z. B. `stringifyValue(IRValue*)`
- denselben Helfer auch fuer `print`, `log.*` oder spaetere Template-Pfade
  nutzbar machen

Das verhindert, dass Chain-Expressions an einer Stelle funktionieren und an
einer anderen nicht.

Betroffene Stellen:

- [ast_compiler_expr.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_expr.cpp)
- [string_utils.h](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/string_utils.h)
- ggf. neue Implementierungsdatei in `src/core/`

### sprint_task_5: Regressionen fuer reale 9rooms-Muster

Nach dem generischen Fix genau die bisher problematischen Muster als Tests
abdecken, aber hermetisch:

- `${hc.iid}`
- `${client_info.player_name}`
- `${obj.items-0.name}`
- Interpolation innerhalb von `log.info(...)`
- zusammengesetzte Strings mit mehreren Chain-Teilen

Akzeptanz:

- 9rooms-nahe Muster kompilieren
- kein Rueckfall auf temporaere Alias-Variablen mehr noetig

## Architektonische Leitlinie

Der Fix sollte **nicht** als Sonderfall „wenn StringInterpolation und Name
enthaelt Punkt“ gebaut werden.

Die richtige Richtung ist:

- Interpolation benutzt dieselbe Expression-Logik wie alle anderen Kontexte
- Stringisierung ist ein nachgelagerter Schritt
- kein separater Mini-Resolver nur fuer `${...}`

Sonst bekommt AIML erneut parallele Semantik:

- normaler Field-Read
- Interpolation-Field-Read
- Call-Arg-Field-Read

Genau das sollte vermieden werden.

## Empfehlung

Wenn man das pragmatisch schneidet:

1. zuerst Testpaket
2. dann AST-Pruefung fuer `${...}`
3. dann `visitStringInterpolation(...)` zentral haerten
4. danach 9rooms-nahe Regressionen

Das ist klein genug fuer einen fokussierten Umbau und gross genug, um den
jetzigen Rueckfall in Zwischenvariablen wieder abzubauen.
