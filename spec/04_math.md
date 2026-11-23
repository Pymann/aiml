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

## Quoting operator keys (2026-11-07)

`{ op: [args] }` is a YAML flow map, and `op` is a YAML **key**.  There the
indicator characters of YAML apply, not the operator table of AIML.  Of the symbolic
operators AIML knows, only some can be written bare — measured with
yaml-cpp 0.8 on 2026-11-07:

| Key | Arity | bare | what YAML makes of the bare form |
|---|---|---|---|
| `+` `-` | binary, `-` also unary | yes | key as written |
| `/` | binary | yes | key as written |
| `<` `<=` | binary | yes | key as written |
| `*` | binary | **no** | alias indicator → throws `the referenced anchor is not defined` |
| `%` | binary | **no** | throws `unknown token` |
| `>` `>=` | binary | **no** | throws `unknown token` |
| `\|\|` | binary | **no** | throws `unknown token` |
| `?` | unary (dereference) | **no** | throws `unknown token` |
| `&&` | binary | **no** | anchor indicator → key becomes **empty, no throw** |
| `&` | unary (address) | **no** | anchor indicator → key becomes **empty, no throw** |
| `!` | unary | **no** | tag indicator → key becomes **empty, no throw** |
| all word forms (`div`, `mod`, `and`, `or`, `not`, `eq`, `neq`, `lt`, `gt`, `le`, `ge`, `band`, `bor`, `bxor`, `shl`, `shr`, `ushr`) | | yes | key as written |

Therefore, for **every symbolic** operator key: **quote it**.  `{ "%": [10, 3] }`
and `{ '%': [10, 3] }` are both correct; the word forms never need quotation marks.
The quoted form is measured for every key in the table (`tests/_opkey/quoted_all_ops.aiml`),
including `{ "&": [zahl] }` and `{ "?": [zgr] }`.

This is not a matter of style, for two reasons:

1. The throwing rows of the table report YAML grammar, not the operator that was meant —
   `unknown token` at the column of the character.
2. The rows with **no throw** are worse: the operator disappears entirely.  Measured on
   the event stream of yaml-cpp (2026-11-07):

   ```
   { &: [12, 10] }    MapStart, SeqStart(anchor=1), 12, 10, SeqEnd, Null, MapEnd
   { !=: [1, 2] }     MapStart, SeqStart(tag='!=:'), 1, 2, SeqEnd, Null, MapEnd
   { ~: [12, 10] }    MapStart, Null, SeqStart, 12, 10, SeqEnd, MapEnd
   ```

   In the first two, the **argument sequence becomes the key** of the map and the value
   `null` — `&` is read as an anchor, `!=:` as a tag.  The compiler therefore gets a map
   without any operator and, before 2026-11-07, reported something else further down (`Failed to
   evaluate value expression`, for `~` `Unknown binary operator: null`).

Since 2026-11-07 the compiler names line, cause and remedy in both cases.  Measured
messages:

```
# throwing class -- the hint hangs on the YAML error and takes the line from its mark
stmt_percent.aiml: YAML parse error: yaml-cpp: error at line 6, column 23: unknown token
 — line 6 writes the operator key '%' bare: `- set: [rest, { %: [10, 3] }]`.  A bare '%'
is a YAML indicator character, not a key.  Quote it: { "%": [...] }
(spec/04 "Quoting operator keys").

# swallowing class -- recognised by a sequence or a null scalar arriving
# in key position
stmt_anchor.aiml:6: operator key swallowed by YAML here: `- set: [bits, { &: [12, 10] }]`
 -- the operator key '&' stands bare in key position, so YAML read it as an anchor/tag/null
and the operator never reached the compiler.  Quote it: { "&": [...] }
(spec/04 "Quoting operator keys").
```

`==`, `!=` and `<>` are also deprecated; comparisons are written in word form.
Written bare, the swallow message therefore names the word form right away
("Write { neq: [...] } -- '!=' is not allowed either"); quoted, the separate
deprecation check applies ("Operator '!=' is not allowed. Use 'neq' instead.").

Both checks hang **on the failure that is already certain**, not on the source text: they
explain an error that existed without them too, and therefore cannot break any code
that runs today.  That is not only intent but necessary — the search is purely
lexical and also hits `{ *: ... }` in quoted data (measured: three
places in 12,024 .aiml files, all test inputs of a tokenizer).

The same applies to operators **within an interpolation**; there the bare form was
**silent** until 2026-11-07 — see spec/09_1 §3.5.
