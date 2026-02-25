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
