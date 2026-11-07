# Math Operations

```yaml
# Operations are Dictionaries: { op: [args] }
# Lists are Lists: [item1, item2]
# Using [op, arg1, arg2] for operations is NOT allowed — it creates a plain list of strings.

# Basic Statements (Imperative)
- set: [a, 10]
- set: [b, 5]

# Expressions (Nested)
# Use { op: [args] } dict syntax for all operations.

# Example: res = (a + b) * 2
- set: [res, { "*": [ { +: [a, b] }, 2 ] }]
- print: [res]

# Modulo
- set: [m, { "%": [10, 3] }]
- print: [m]

# Division by Zero:
# - Integer division by zero raises DivisionByZeroException.
# - Float division by zero returns Inf or NaN per IEEE 754.
```

## Operator Arity

Arithmetic operators in AIML are not uniformly variadic.

- `+`, `add`, `*`, `mul` accept 2 or more operands.
- When more than 2 operands are provided, they are folded from left to right.
- `/`, `div`, `%` are strictly binary and require exactly 2 operands.
- `-`, `sub` take exactly 2 operands as subtraction, and exactly 1 as **negation**
  (`{ -: [a] }` → `-a`).  There is no 3-or-more form, and no `neg` operator — see
  spec/04_1 §"`-` — Unary Negation" for the scalar types and spec/43_0 / spec/39 for
  vectors, where the unary form is defined the same way.  Unary `/` or `%` is rejected
  ("Unknown unary operator", measured 31.10.2026).

Examples:

```yaml
# valid: left-folded as ((1 + 2) + 3)
- set: [sum, { +: [1, 2, 3] }]

# valid: left-folded as ((2 * 3) * 4)
- set: [prod, { "*": [2, 3, 4] }]

# invalid: compiler error, because '-' stays binary
- set: [x, { -: [10, 3, 1] }]
```

This restriction is intentional. Only associative arithmetic operators with clear fold
semantics are variadic. Non-associative operators stay binary to avoid ambiguous code.

## Operator-Schluessel zitieren (07.11.2026)

`{ op: [args] }` ist eine YAML-Flow-Map, und `op` ist ein YAML-**Schluessel**.  Dort gelten
die Indikatorzeichen von YAML, nicht die Operatortabelle von AIML.  Von den symbolischen
Operatoren, die AIML kennt, laesst sich nur ein Teil blank hinschreiben — gemessen an
yaml-cpp 0.8 am 07.11.2026:

| Schluessel | Stelligkeit | blank | was YAML aus der blanken Form macht |
|---|---|---|---|
| `+` `-` | binaer, `-` auch unaer | ja | Schluessel wie geschrieben |
| `/` | binaer | ja | Schluessel wie geschrieben |
| `<` `<=` | binaer | ja | Schluessel wie geschrieben |
| `*` | binaer | **nein** | Alias-Indikator → Wurf `the referenced anchor is not defined` |
| `%` | binaer | **nein** | Wurf `unknown token` |
| `>` `>=` | binaer | **nein** | Wurf `unknown token` |
| `\|\|` | binaer | **nein** | Wurf `unknown token` |
| `?` | unaer (Dereferenz) | **nein** | Wurf `unknown token` |
| `&&` | binaer | **nein** | Anker-Indikator → Schluessel wird **leer, kein Wurf** |
| `&` | unaer (Adresse) | **nein** | Anker-Indikator → Schluessel wird **leer, kein Wurf** |
| `!` | unaer | **nein** | Tag-Indikator → Schluessel wird **leer, kein Wurf** |
| alle Wortformen (`div`, `mod`, `and`, `or`, `not`, `eq`, `neq`, `lt`, `gt`, `le`, `ge`, `band`, `bor`, `bxor`, `shl`, `shr`, `ushr`) | | ja | Schluessel wie geschrieben |

Darum gilt fuer **jeden symbolischen** Operatorschluessel: **zitieren**.  `{ "%": [10, 3] }`
und `{ '%': [10, 3] }` sind beide richtig, die Wortformen brauchen nie Anfuehrungszeichen.
Die zitierte Form ist fuer jeden Schluessel der Tabelle gemessen (`tests/_opkey/quoted_all_ops.aiml`),
einschliesslich `{ "&": [zahl] }` und `{ "?": [zgr] }`.

Das ist keine Stilfrage, und zwar aus zwei Gruenden:

1. Die Wurf-Zeilen der Tabelle melden YAML-Grammatik, nicht den Operator, der gemeint war —
   `unknown token` an der Spalte des Zeichens.
2. Die Zeilen mit **kein Wurf** sind schlimmer: der Operator verschwindet ganz.  Am
   Ereignisstrom von yaml-cpp gemessen (07.11.2026):

   ```
   { &: [12, 10] }    MapStart, SeqStart(anchor=1), 12, 10, SeqEnd, Null, MapEnd
   { !=: [1, 2] }     MapStart, SeqStart(tag='!=:'), 1, 2, SeqEnd, Null, MapEnd
   { ~: [12, 10] }    MapStart, Null, SeqStart, 12, 10, SeqEnd, MapEnd
   ```

   Bei den ersten zwei wird die **Argumentsequenz zum Schluessel** der Map und der Wert
   `null` — `&` ist als Anker gelesen, `!=:` als Tag.  Der Compiler bekommt also eine Map
   ohne jeden Operator und meldete vor 07.11.2026 weiter unten etwas anderes (`Failed to
   evaluate value expression`, bei `~` `Unknown binary operator: null`).

Seit 07.11.2026 nennt der Compiler in beiden Faellen Zeile, Ursache und Abhilfe.  Gemessene
Meldungen:

```
# Wurf-Klasse -- der Hinweis haengt am YAML-Fehler und holt sich die Zeile aus dessen Marke
stmt_percent.aiml: YAML parse error: yaml-cpp: error at line 6, column 23: unknown token
 — line 6 writes the operator key '%' bare: `- set: [rest, { %: [10, 3] }]`.  A bare '%'
is a YAML indicator character, not a key.  Quote it: { "%": [...] }
(spec/04 "Operator-Schluessel zitieren").

# Schluck-Klasse -- erkannt daran, dass in Schluesselposition eine Sequenz bzw. ein
# Null-Skalar ankommt
stmt_anchor.aiml:6: operator key swallowed by YAML here: `- set: [bits, { &: [12, 10] }]`
 -- the operator key '&' stands bare in key position, so YAML read it as an anchor/tag/null
and the operator never reached the compiler.  Quote it: { "&": [...] }
(spec/04 "Operator-Schluessel zitieren").
```

`==`, `!=` und `<>` sind ausserdem veraltet; Vergleiche werden in Wortform geschrieben.
Blank geschrieben nennt die Schluck-Meldung darum gleich die Wortform
("Write { neq: [...] } -- '!=' is not allowed either"), zitiert greift die eigene
Veraltet-Pruefung ("Operator '!=' is not allowed. Use 'neq' instead.").

Beide Pruefungen haengen **am schon feststehenden Fehlschlag**, nicht am Quelltext: sie
erklaeren einen Fehler, den es ohne sie auch gab, und koennen darum keinen Code brechen,
der heute laeuft.  Das ist nicht nur Absicht, sondern noetig — die Suche ist rein
lexikalisch und trifft `{ *: ... }` auch in Daten in Anfuehrungszeichen (gemessen: drei
Stellen in 12.024 .aiml-Dateien, alle Testeingaben eines Tokenizers).

Fuer Operatoren **innerhalb einer Interpolation** gilt dasselbe; dort war die blanke Form
bis 07.11.2026 **still** — siehe spec/09_1 §3.5.
