# AIML Operator Arity Report

Datum: 2026-04-02

## Kurzfazit

Das relevante Restproblem ist ein Binary-Arity-Problem.

- `UnaryExpr` ist im AST strikt einoperandig ausgelegt: [ast_nodes.h](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_nodes.h#L60)
- `BinaryExpr` ist im AST strikt zweioperandig ausgelegt: [ast_nodes.h](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_nodes.h#L49)
- Der Builder erzeugt Unary nur bei `size()==1` und Binary nur bei `size()==2`: [ast_builder.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_builder.cpp#L80), [expression_builder.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast/expression_builder.cpp#L143)
- Der Compiler behandelt `||` und `&&` explizit binaer: [ast_compiler_expr.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_expr.cpp#L580)

Unary-only-Operatoren mit mehr als einem Operand werden jetzt bereits sauber als Compilerfehler abgewiesen. Bei Binary gilt jetzt:

- `+` und `*` duerfen mehr als 2 Operanden haben und werden links gefaltet
- alle anderen bekannten binaeren Operatoren erzeugen bei mehr als 2 Operanden einen Compilerfehler

## Betroffene Operatoren

### Binary

Sichtbar binaer ausgewertete Operatoren:

- Arithmetik: `+`, `-`, `*`, `/`, `%`
- Vergleich: `eq`, `neq`, `lt`, `gt`, `le`, `lte`, `ge`, `gte`, `peq`, `pneq`
- Logik: `and`, `&&`, `or`, `||`

Referenz: [ast_compiler_expr.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_compiler_expr.cpp#L459)

Bewertung:

- Diese Operatoren sind im aktuellen AST/Compiler-Modell alle binär.
- `+` und `*` sind jetzt der einzige bewusst variadische Sonderfall.
- Alle anderen bekannten Binary-Operatoren bleiben strikt 2-stellig.
- Das verhindert stille Fehlkompilierung bei Operatoren wie `eq`, `||`, `lt` oder `pneq`.

## Aufwand nach Option

### Umgesetzte Policy

- Unary-only-Operatoren: exakt 1 Operand
- `+`, `*`, `add`, `mul`: 2 oder mehr Operanden, links gefaltet
- alle anderen bekannten Binary-Operatoren: exakt 2 Operanden

Referenzen:

- [expression_builder.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast/expression_builder.cpp#L23)
- [expression_builder.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast/expression_builder.cpp#L190)
- [ast_builder.cpp](/media/nikolai/95a208ef-ffa2-478b-abdd-ff394baa6a76/projects/python/test/aiml/src/core/ast_builder.cpp#L121)

Verifiziert:

- `{ +: [1, 2, 3] }` kompiliert und liefert `6`
- `{ eq: [1, 1, 1] }` bricht jetzt mit `Binary operator 'eq' expects exactly 2 operands, got 3.` ab

## Aufwand nach heutigem Stand

### Bereits erledigt

- Unary-Arity-Fehler
- Binary-Arity-Fehler fuer alle bekannten Operatoren ausser `+` und `*`
- Linksfaltung fuer `+` und `*`

Geschaetzter Aufwand:

- erledigt

### Optional spaeter: weitere variadische Operatoren

Wenn spaeter noch `eq`, `&&`, `||` oder Vergleichsketten variadisch werden sollen, ist das ein bewusster Sprachdesign-Schritt.

Geschaetzter Aufwand:

- etwa 0.5 bis 1.5 Tage je nach Umfang und Testtiefe

## Konkrete Aussage zur Frage

- Der Unary-Teil ist jetzt erledigt: Unary-only-Operatoren mit mehr als einem Operand erzeugen einen Compilerfehler.
- Binary ist jetzt bewusst eingeschraenkt: nur `+` und `*` duerfen mehr als 2 Operanden haben.
- Fuer alle anderen bekannten binaeren Operatoren gibt es jetzt bei `>2` einen Compilerfehler.
