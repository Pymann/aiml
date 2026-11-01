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
- set: [res, { *: [ { +: [a, b] }, 2 ] }]
- print: [res]

# Modulo
- set: [m, { %: [10, 3] }]
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
- set: [prod, { *: [2, 3, 4] }]

# invalid: compiler error, because '-' stays binary
- set: [x, { -: [10, 3, 1] }]
```

This restriction is intentional. Only associative arithmetic operators with clear fold
semantics are variadic. Non-associative operators stay binary to avoid ambiguous code.
