# Implementierungsplan für tiefe Member-Zugriffe

## Problem

Im aktuellen AIML-Compiler sind verschachtelte Member-Zugriffe wie `a.b.c` oder `root.match.round` nicht konsistent implementiert.

In der Praxis führt das zu zwei Symptomen:

- tiefe Reads funktionieren nur teilweise oder nur in bestimmten Formen
- tiefe Writes auf `set`-Ziele brechen früh, sodass man Hilfsvariablen anlegen oder ganze Teilobjekte neu zuweisen muss

Das deckt sich mit dem beobachteten Nutzerverhalten in `9rooms` und mit den Mirror-Repros.

## Hauptbefund im C++-Code

### 1. `visitVariable(...)` splittet nur am ersten Separator

In [ast_compiler_expr.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_expr.cpp#L184) bis [ast_compiler_expr.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_expr.cpp#L190) wird ein Variablenname mit `find('.')` bzw. `find('-')` zerlegt.

Das heißt:

- `a.b.c` wird zuerst nur zu `baseName = "a"` und `propName = "b.c"`
- die weitere Behandlung ist nicht als generischer Segment-Walk aufgebaut

Damit ist die Expression-Seite strukturell nicht auf echte beliebig tiefe Pfade ausgelegt.

### 2. `visitSetStmt(...)` splittet ebenfalls nur einmal

In [ast_compiler_stmt.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_stmt.cpp#L219) bis [ast_compiler_stmt.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_stmt.cpp#L230) wird das `set`-Ziel genauso nur in `baseName` und `propName` zerlegt.

Folge:

- `root.match.round` wird als `baseName = "root"` und `propName = "match.round"` behandelt
- danach sucht der Code nur noch ein direktes Member mit genau diesem Namen
- tieferes Descending findet nicht statt

Das ist die direkte Ursache dafür, dass tiefe `set`-Ziele heute praktisch nicht sauber funktionieren.

### 3. `Utils::resolveLValue(...)` ist näher an der richtigen Richtung, aber noch nicht tief genug

In [utils.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/utils.cpp#L117) bis [utils.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/utils.cpp#L129) wird mit `rfind('.')` gearbeitet.

Das ist besser als `find('.')`, weil wenigstens der letzte Member isoliert wird. Aber:

- `resolveLValueMemberAccess(...)` in [utils.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/utils.cpp#L8) erwartet, dass `objName` bereits direkt im `symbol_table` liegt
- bei `a.b.c` wird daraus `objName = "a.b"`
- `"a.b"` ist aber kein echtes Symbol im `symbol_table`

Damit ist auch dieser Pfad nicht wirklich rekursiv.

### 4. Reads und Writes folgen unterschiedlichen Auflösungspfaden

Es gibt heute keine einzige zentrale Pfadauflösung für:

- einfacher Variablenzugriff
- Member-Read
- Member-Write
- Array-/Listenzugriff
- gemischte Ketten wie `a.items-0.hp`

Stattdessen ist die Logik verteilt über:

- [ast_compiler_expr.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_expr.cpp)
- [ast_compiler_stmt.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_stmt.cpp)
- [utils.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/utils.cpp)

Das ist der Hauptgrund, warum tiefe Dot-Logik inkonsistent ist.

## Zielbild

Der Compiler sollte jeden Pfad wie eine Segmentkette behandeln, nicht wie einen String mit einem Sonderfall.

Beispiele:

- `a.b.c`
- `a.items-0.hp`
- `this.player.hand-2.name`
- `ptr_to_root.match.round`

Alle diese Formen sollten intern zu einer einheitlichen Folge von Pfadsegmenten normalisiert werden.

## Empfohlene Implementierung

### Stufe 1: Pfadparser einführen

Erste neue Komponente:

- `parseAccessPath("a.items-0.hp") -> [var(a), member(items), index(0), member(hp)]`

Diese Logik sollte nicht mehr ad hoc in `visitVariable` und `visitSetStmt` stecken.

Primäre C++-Stelle:

- neu in [utils.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/utils.cpp)

Hilfreiche API:

- `struct AccessSegment`
- `enum class AccessKind { BaseVar, Member, Index }`
- `parseAccessPath(const std::string&)`

Wichtig:

- `.` und `-` müssen gemeinsam unterstützt werden
- `this.` und `<.` sollten vor dem Parsen normalisiert werden

### Stufe 2: Zentrale rekursive/iterative Auflösung bauen

Danach sollte es genau eine Kernfunktion geben, z. B.:

- `resolveAccessPath(ctx, path, forWrite)`

Diese Funktion läuft Segment für Segment durch und liefert:

- entweder den finalen Pointer/LValue
- oder zusätzlich Metadaten über den finalen Typ

Primäre C++-Stellen:

- [utils.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/utils.cpp)
- optional Deklarationen in [utils.h](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/utils.h)

Diese Funktion muss folgende Fälle einheitlich beherrschen:

- Struct-Member
- Pointer-/Ref-Dereferenz auf Structs
- Listenelemente
- Arrayelemente
- gemischte Ketten

### Stufe 3: `visitVariable(...)` auf die neue Pfadauflösung umstellen

`visitVariable(...)` in [ast_compiler_expr.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_expr.cpp#L179) sollte nicht mehr selbst mit `find('.')` arbeiten.

Stattdessen:

- Namespace-/Enum-Sonderfälle früh behandeln
- danach den Rest an die neue Pfadauflösung delegieren

Ziel:

- tiefe Reads wie `a.b.c`
- gemischte Reads wie `a.items-0.hp`
- tiefe `.len`-artige Zugriffe nur dort als Property-Sonderfall behandeln, wo sie wirklich Container-Properties sind

### Stufe 4: `visitSetStmt(...)` vollständig auf denselben Pfadkern umstellen

Das ist der wichtigste Schritt für die wahrgenommene Nutzbarkeit.

In [ast_compiler_stmt.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_stmt.cpp#L213) sollte `set` nicht mehr `baseName/propName` manuell zerlegen.

Stattdessen:

- Zielpfad parsen
- finalen LValue mit `forWrite = true` auflösen
- Zugriffsschutz und `const` am finalen Segment prüfen
- Wert dorthin schreiben

Wenn dieser Schritt fehlt, bleibt das eigentliche Nutzerproblem bestehen.

### Stufe 5: Property-Sonderfälle in die Pfadauflösung integrieren

Aktuell gibt es Spezialfälle wie:

- `list.len`
- `list.empty`
- `map.len`

in [utils.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/utils.cpp#L134).

Diese sollten nicht als völlig separater Seitentrieb leben, sondern sauber in das Segmentmodell integriert werden:

- normale Member
- Container-Property
- Indexzugriff

Sonst bleibt die Pfadlogik wieder zweigeteilt.

### Stufe 6: Builtin-Handle-Typen bewusst abgrenzen

Es gibt Builtins mit eigens verdrahteten Settern/Gettern, z. B. in [builtin_registry.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/builtin_registry.cpp#L2421).

Wichtig:

- normale Struct-Ketten sollten über die neue generische Pfadauflösung laufen
- Builtin-Handle-Typen dürfen weiter spezielle Property-Setzer haben
- aber nur als explizite Sonderfälle am Segmentende

So vermeidet man, dass Builtin-API und normale Dot-Logik sich gegenseitig kaputtmachen.

## Testplan

Ohne neue gezielte Tests würde ich das nicht umbauen.

### Neue Pflicht-Tests

1. Tiefer Read:
   `a.b.c`

2. Tiefer Write:
   `set: [a.b.c, 7]`

3. Gemischter Pfad:
   `a.items-0.hp`

4. Pointer/Ref-Kette:
   `p.nested.value`

5. Container-Property nach Kette:
   `a.cards.len`

6. Negativtest:
   Zugriff auf nicht existentes Zwischenmember

7. Negativtest:
   Schreibzugriff auf `const`-Zwischenpfad bzw. finales `const`-Member

### Gute Zusatztests

- `this.` und `<.` mit tiefem Memberpfad
- `set` auf verschachtelte Mirror-Modelle
- Kombination aus `list.get(...)` und Dot-Zugriff, damit klar bleibt, was bewusst Methode und was direkter Pfad ist

## Empfohlene Reihenfolge

1. Pfadparser in `utils.*`
2. zentrale `resolveAccessPath(...)`-Funktion in `utils.cpp`
3. Expression-Read auf neue API umstellen
4. `set`-Write auf neue API umstellen
5. Property-/Builtin-Sonderfälle sauber nachziehen
6. neue Regressionstests einchecken

## Praktische Bewertung

Das ist kein kleiner kosmetischer Fix. Es ist eine kleine Infrastrukturarbeit im Compiler.

Aber:

- der Nutzen ist hoch
- die Sprache wird deutlich konsistenter
- `9rooms` und ähnliche Projekte verlieren viel Hilfsvariablen-Boilerplate
- es beseitigt eine reale Sprachgrenze, die heute beim Modellieren größerer Datenstrukturen stört

## Empfehlung

Ich würde das als gezielten Compiler-Refactor behandeln, nicht als punktuellen Bugfix.

Der richtige Hebel ist:

- eine einheitliche Pfadrepräsentation
- eine zentrale Auflösung
- danach erst Reads und Writes darauf umstellen

Alles andere würde nur neue Sonderfälle neben die bestehenden Sonderfälle stellen.
