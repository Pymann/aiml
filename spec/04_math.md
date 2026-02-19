# Math Operations Proposal (Revised for Safety)

```yaml
# Solution for Ambiguity:
# Operations are Dictionaries: { op: [args] }
# Lists are Lists: [item1, item2]

# Basic Statements (Imperative)
# These are top-level list items, which differ from "Expressions"
- set: [a, 10]
- set: [b, 5]

# Expressions (Nested)
# Instead of [+, a, b] which looks like a list of strings,
# we use { +: [a, b] } to denote an operation.

# Example: res = (a + b) * 2
- set: [res, { *: [ { +: [a, b] }, 2 ] }]
- print: [res]

# Modulo
- set: [m, { %: [10, 3] }]
- print: [m]

# Comparison with Lists (Data)
# This is a list with symbols/strings, NOT an operation:
- set: [my_list, [+, a, b]] 
# my_list is now ["+", 10, 5] (resolved) or ["+", "a", "b"] depending on resolution rules.
```
# Lists are Lists: [item1, item2]

# Basic Statements (Imperative)
# These are top-level list items, which differ from "Expressions"
- set: [a, 10]
- set: [b, 5]

# Expressions (Nested)
# Instead of [+, a, b] which looks like a list of strings,
# we use { +: [a, b] } to denote an operation.

# Example: res = (a + b) * 2
- set: [res, { *: [ { +: [a, b] }, 2 ] }]
- print: [res]

# Modulo
- set: [m, { %: [10, 3] }]
- print: [m]

# Comparison with Lists (Data)
# This is a list with symbols/strings, NOT an operation:
- set: [my_list, [+, a, b]] 
# my_list is now ["+", 10, 5] (resolved) or ["+", "a", "b"] depending on resolution rules.
