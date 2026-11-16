# Implementierungsplan: Verkettete Field-/Method-Syntax in AIML

## Ziel

Der AIML-Compiler soll echte verkettete Zugriffe und Methoden konsistent
unterstuetzen, z. B.:

- `a.b.c`
- `a.b.c = 7`
- `a.b.len()`
- `a.items-0.name`
- `a.b.map.get("x")`
- `this.player.hand-2.mana_cost.get("1")`

Aktuell funktionieren nur Teilformen oder Sonderfaelle. Das fuehrt zu
Hilfsvariablen, Workarounds und inkonsistentem Verhalten zwischen Read, Write
und Method-Call.

## Kernproblem

Der Compiler behandelt Zugriffs-Strings heute nicht als echte Segmentkette,
sondern meist als:

- `baseName`
- plus ein Reststring ab dem ersten Punkt

Dadurch brechen echte Ketten frueh weg.

## Aktueller Befund im C++-Code

### 1. Read-Pfad zerlegt nur einmal

In [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp#L184) wird in `visitVariable(...)` nur der erste Trenner (`.` oder `-`) verarbeitet.

Folge:

- `a.b.c` wird zu:
  - `baseName = "a"`
  - `propName = "b.c"`

Das ist keine echte rekursive Pfadauflösung.

### 2. Write-Pfad zerlegt ebenfalls nur einmal

In [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp) wird `set` strukturell genauso behandelt.

Folge:

- tiefe Ziele wie `root.match.round` oder `card.data.cost` sind nicht robust

### 3. Method-Call-Pfad splittet nur am ersten Punkt

In [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L62) wird `call->functionName` mit `find('.')` nur einmal zerlegt.

Beispiel:

- `a.b.map.get`
  - `baseName = "a"`
  - `methodName = "b.map.get"`

Dann versucht `visitObjectMethodCall(...)` auf `a` zu arbeiten, obwohl die
eigentliche Receiver-Instanz erst nach mehreren Segmenten erreicht wird.

### 4. Es gibt bereits Teil-Sonderfaelle, aber keinen allgemeinen Kettenpfad

In [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L459) und [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L1215) existiert bereits "chained method dispatch", aber nur als lokaler Sonderfall:

- Feldname isolieren
- dann Unter-Methode ausfuehren

Das ist nuetzlich fuer einfache Builtin-Sonderfaelle wie:

- `resp.headers.put(...)`
- `c.mana_cost.put(...)`

aber kein allgemeines Modell fuer beliebig tiefe Ketten.

## Zielbild

Der Compiler soll jeden Zugriff intern in **Segmente** zerlegen und dann
einheitlich aufloesen.

Beispiel:

`this.player.hand-2.mana_cost.get("1")`

soll intern werden zu:

1. `base(this)`
2. `member(player)`
3. `member(hand)`
4. `index(2)`
5. `member(mana_cost)`
6. `method(get)`

Erst mit so einem Modell lassen sich:

- tiefe Reads
- tiefe Writes
- tiefe Method-Calls
- gemischte Ketten aus Membern, Indizes und Methoden

sauber vereinheitlichen.

## Empfohlene Architektur

### Architektonische Leitplanken

Wenn dieser Umbau nur punktuell in `visitVariable(...)` und `visitCall(...)`
geflickt wird, entsteht sehr wahrscheinlich der naechste Parallelpfad.

Darum sollten diese Regeln gelten:

1. Es gibt genau **einen** Parser fuer Access-Chains.
2. Es gibt genau **einen** Resolver fuer Read/Write/Call-Receiver.
3. `visitVariable(...)`, `visitSetStmt(...)` und `visitCall(...)` duplizieren
   keine Pfadlogik mehr.
4. Builtin-Methoden bekommen einen bereits korrekt aufgeloesten Receiver und
   sollen nicht selbst wieder String-Ketten zerlegen.
5. Property-Sonderfaelle wie `len` werden in denselben Kettenpfad integriert
   und nicht als zweites System weitergefuehrt.

Wenn man diese 5 Punkte einhaelt, richtet der Umbau nicht nur die Syntax
gerade, sondern reduziert gleichzeitig Architektur-Schulden im Compiler.

### Stufe 1: Access-Chain-Datenmodell

Neue zentrale Typen, z. B. in `utils.h/.cpp`:

- `enum class AccessSegmentKind`
  - `Base`
  - `Member`
  - `Index`
  - `Method`
- `struct AccessSegment`
- `struct AccessChain`

Der Parser soll aus einem AIML-Pfad eine normierte Kette erzeugen.

Beispiele:

- `a.b.c`
- `a.items-0.hp`
- `a.b.map.get`
- `this.player.cards-1.len`

### Stufe 2: Einheitlicher Chain-Parser

Neue Funktion:

- `parseAccessChain(const std::string&)`

Anforderungen:

- `.` und `-` gemeinsam verstehen
- `this.` und `<.` vorher normalisieren
- nicht nur String-Splitting, sondern echte Segmenttypen erzeugen
- Methodenname am Ende sauber von Field-Segmenten trennen

Diese Logik darf nicht mehr verteilt sein ueber:

- `visitVariable(...)`
- `visitSetStmt(...)`
- `visitCall(...)`

## Zentrale Auflösung

### Stufe 3: Gemeinsamer Resolver

Neue Kernfunktion:

- `resolveAccessChain(ctx, chain, mode)`

`mode` etwa:

- `Read`
- `Write`
- `CallReceiver`

Der Resolver laeuft Segment fuer Segment:

- Startsymbol laden
- Struct-Member absteigen
- Pointer/Referenz beachten
- List/Array-Index aufloesen
- Container-Properties behandeln
- finalen Receiver fuer Method-Call liefern

### Stufe 4: Read/Write/Call auf denselben Resolver umstellen

#### `visitVariable(...)`

In [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp#L179)

soll:

- Namespace-/Enum-Sonderfall frueh bleiben
- normaler Variablen-/Field-Read aber komplett an `resolveAccessChain(..., Read)` gehen

#### `visitSetStmt(...)`

In [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)

soll:

- das Ziel als Chain geparst werden
- finaler LValue ueber `resolveAccessChain(..., Write)` geliefert werden

#### `visitCall(...)`

In [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp#L62)

soll:

- `call->functionName` nicht mehr nur in `baseName` + `methodName` gesplittet werden
- stattdessen:
  - Chain parsen
  - finalen Receiver aufloesen
  - letztes Segment als Method-Name behandeln

## Konkreter Method-Call-Umbau

### Stufe 5: Receiver-Resolution fuer Ketten

Heute:

- `a.b.map.get(...)` wird als Methode auf `a` gestartet

Ziel:

- Receiver ist `a.b.map`
- Methode ist `get`

Neue Hilfsfunktion:

- `splitChainForMethodCall(chain) -> receiverChain + methodSegment`

Dann:

1. ReceiverChain aufloesen
2. finalen Receiver-Typ bestimmen
3. Method-Dispatch fuer genau diesen Typ ausfuehren

### Stufe 6: Builtin-Methoden nicht als Sonderlogikstring behandeln

Der bisherige "fieldName + subMethod"-Sonderfall kann danach stark vereinfacht werden.

Ziel:

- Builtin-Method-Dispatch bekommt bereits den korrekten Receiver
- keine improvisierte Teilauflösung ueber String-Reste

Das betrifft besonders:

- `list<T>`
- `map<K:V>`
- `Kv<K:V>`
- `Store<T>`
- `TimeSeries<T>`
- Builtin-Klassen mit Property-Handles

## Properties vs. Methoden

### Stufe 7: Container-Properties sauber unterscheiden

Es braucht eine saubere Regel fuer:

- `a.cards.len`
- `a.cards.len()`

Empfehlung:

- intern beide Formen auf dieselbe Property-/Builtin-Logik abbilden
- aber ueber denselben Chain-Resolver

Wichtig:

- Property-Sonderfaelle duerfen nicht wieder ein zweiter Parallelpfad werden

## Dateiaufteilung

### `utils.h` / `utils.cpp`

Neu hinein:

- `AccessSegmentKind`
- `AccessSegment`
- `AccessChain`
- `parseAccessChain(...)`
- `resolveAccessChain(...)`
- `splitChainForMethodCall(...)`

### `ast_compiler_expr.cpp`

Umbau:

- `visitVariable(...)` auf Chain-Parser + Resolver

### `ast_compiler_stmt.cpp`

Umbau:

- `visitSetStmt(...)` auf Chain-Parser + Resolver

### `ast_compiler_call.cpp`

Umbau:

- `visitCall(...)` auf Receiver-Chain-Modell
- `visitObjectMethodCall(...)` soll nicht mehr mit halb-rohen Method-Namen arbeiten

## Reihenfolge der Umsetzung

### sprint_task_1

Titel: Access-Chain-Strukturen und Parser einführen

Ergebnis:

- Parser fuer `a.b`, `a.b.c`, `a.x-0.y`, `a.b.get`

Betroffene Stellen:

- [utils.h](../../src/core/utils.h)
- [utils.cpp](../../src/core/utils.cpp)

### sprint_task_2

Titel: Gemeinsamen Resolver fuer Reads bauen

Ergebnis:

- `visitVariable(...)` liest tiefe Ketten sauber

Betroffene Stellen:

- [utils.h](../../src/core/utils.h)
- [utils.cpp](../../src/core/utils.cpp)
- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)

### sprint_task_3

Titel: Gemeinsamen Resolver fuer Writes bauen

Ergebnis:

- `set: [a.b.c, ...]` funktioniert ueber denselben Pfad

Betroffene Stellen:

- [utils.h](../../src/core/utils.h)
- [utils.cpp](../../src/core/utils.cpp)
- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)

### sprint_task_4

Titel: Method-Call-Receiver auf Chain-Modell umstellen

Ergebnis:

- `a.b.map.get(...)`
- `this.player.hand-1.mana_cost.get(...)`
- `obj.nested.list.len()`

laufen ueber echte Receiver-Aufloesung

Betroffene Stellen:

- [utils.h](../../src/core/utils.h)
- [utils.cpp](../../src/core/utils.cpp)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)

### sprint_task_5

Titel: Builtin-Dispatch und Property-Zugriffe bereinigen

Ergebnis:

- kein Sonderpfad-Wildwuchs mehr
- Container-Properties und Methoden laufen konsistent

Betroffene Stellen:

- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)
- [builtin_registry.cpp](../../src/core/builtin_registry.cpp)
- [utils.cpp](../../src/core/utils.cpp)

### sprint_task_6

Titel: Negative Diagnostics für tiefe Chains verbessern

Ergebnis:

- Fehler wie:
  - unbekanntes Zwischenmember
  - Methode auf falschem Endtyp
  - Index auf Nicht-Container

mit klarer Meldung statt spaetem IR- oder Runtime-Fehler

Betroffene Stellen:

- [context.h](../../src/core/context.h)
- [utils.cpp](../../src/core/utils.cpp)
- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)
- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)
- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)

## Testplan

Pflichttests:

1. `a.b.c` lesen
2. `set: [a.b.c, 7]`
3. `a.items-0.hp`
4. `a.map.get("x")`
5. `a.b.map.get("x")`
6. `this.player.hand-1.name`
7. `this.player.hand-1.mana_cost.get("1")`
8. `a.cards.len`
9. `a.cards.len()`

Negativtests:

1. unbekanntes Zwischenmember
2. Methode auf Skalar
3. Index auf Nicht-Liste
4. Write auf nicht schreibbares Ziel

## Akzeptanzkriterium

Die Änderung ist erst dann erfolgreich, wenn Nutzer nicht mehr gezwungen sind,
fuer einfache tiefe Ketten wie:

- `a.b.c`
- `this.player.hand-1.name`
- `card.data.cost.get("1")`

Hilfsvariablen oder Sonderworkarounds anzulegen.
