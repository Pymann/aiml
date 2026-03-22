# sprint_task_5

## Titel

Model-Validierung und Member-Zugriffe an die Spec angleichen

## Ziel

Die Regeln aus [15_models.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/spec/15_models.md) sollen fuer Modellzuweisung, strukturelle Typisierung und tiefe Member-Zugriffe belastbar umgesetzt werden.

## Hintergrund

Die Spec verspricht:

- Zuweisung aus Maps mit Laufzeitvalidierung
- strukturelle Typisierung in Funktionsaufrufen
- `ptr<Model>` mit Auto-Dot-Dereferenzierung
- alltagstaugliche verschachtelte Feldzugriffe

Praktisch ist vor allem der tiefe Zugriffspfad noch unvollstaendig. Das fuehrt zu vielen Hilfsvariablen und Workarounds.

## Scope

- zentralen Access-Path fuer Reads und Writes vereinheitlichen
- tiefe Member-Ketten `a.b.c` konsistent aufloesen
- `set` auf tiefe Ziele unterstuetzen
- Modellzuweisung aus Map gegen Schema klar validieren
- strukturelle Typisierung nicht nur dokumentieren, sondern gezielt absichern

## Betroffene Stellen

- [15_models.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/spec/15_models.md)
- [member_chain_plan.md](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/member_chain_plan.md)
- [ast_compiler_expr.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_expr.cpp)
- [ast_compiler_stmt.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_stmt.cpp)
- [utils.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/utils.cpp)

## Umsetzungsrichtung

1. Einen zentralen Access-Path-Resolver als gemeinsame Basis bauen.

2. Reads, Writes und LValues darauf aufsetzen statt drei halbgetrennte Pfade zu pflegen.

3. Danach Modellvalidierung fuer Map-Zuweisung und strukturelle Argumentuebergabe gezielt haerten.

4. Die wichtigsten Alltagsfaelle mit Regressionstests abdecken:

- `a.b.c`
- `a.items-0.hp`
- `ptr<Model>.field`
- `set` auf verschachtelte Ziele

## Akzeptanzkriterien

- Tiefe Member-Ketten funktionieren fuer Read und Write konsistent.
- `ptr<Model>` verhaelt sich wie in der Spec beschrieben.
- Modellzuweisungen aus Maps validieren nachvollziehbar gegen das Schema.
- Strukturkompatible Aufrufe sind entweder sauber unterstuetzt oder explizit begrenzt.

## Ergebnis

Nach Abschluss ist die Modell- und Member-Semantik deutlich naeher an der dokumentierten Sprache und alltagstauglicher fuer groessere Projekte.
