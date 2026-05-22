# Logic and Control Flow

## if

### Canonical Form

```yaml
- if:
    condition: { and: [ { lt: [x, y] }, { gt: [x, z] } ] }
    then:
      - print: [x]
      - set: [result, 1]
    else:
      - print: [z]
      - set: [result, 0]
```

### Short Form

The short form is equivalent to the canonical form and is an official part of the language:

```yaml
- if: [{ lt: [x, y] }, true]
  then:
    - print: [x]
  else:
    - print: [z]
```

The first element of the list is the condition (any expression that yields a truth value).
The second element (`true`) is a mandatory placeholder with no semantic meaning.

`else` is optional in both forms.

Both forms are equivalent and may be mixed freely.

## while

### Canonical Form

```yaml
- while:
    condition: { lt: [counter, limit] }
    body:
      - set: [counter, { +: [counter, 1] }]
```

### Short Form

```yaml
- while: [{ lt: [counter, limit] }]
  body:
    - set: [counter, { +: [counter, 1] }]
```

The first (and only) element of the list is the condition.

## Operator Arity

Logical and comparison operators have fixed arity.

- Unary operators: `!`, `?`, `&` — exactly 1 operand
- Binary logical operators: `and`, `&&`, `or`, `||` — exactly 2 operands
- Comparison operators: `eq`, `neq`, `lt`, `gt`, `le`, `lte`, `ge`, `gte`, `peq`, `pneq` — exactly 2 operands

```yaml
# valid
- if:
    condition: { or: [{ eq: [a, b] }, { gt: [a, c] }] }
    then:
      - print: ["ok"]

# Compiler error: 'eq' is binary
- if:
    condition: { eq: [a, b, c] }
    then:
      - print: ["never compiles"]

# Compiler error: '||' is binary
- if:
    condition: { "||": [{ eq: [a, b] }, { eq: [b, c] }, { eq: [c, d] }] }
    then:
      - print: ["never compiles"]
```

Longer logical expressions must be nested explicitly:

```yaml
- if:
    condition: { "||": [{ eq: [a, b] }, { "||": [{ eq: [b, c] }, { eq: [c, d] }] }] }
    then:
      - print: ["ok"]
```

## tolerance — Float Comparison with Epsilon

`tolerance` is a ternary operator for approximate equality of floating-point numbers.

**Syntax:**

```yaml
{ tolerance: [val1, val2, epsilon] }
```

**Semantics:** Returns `1` (true) if `|val1 - val2| <= epsilon`, otherwise `0` (false).

All three arguments are automatically converted to `f32` (i32 → SIToFP, f64 → FPTrunc).

**Implementation:** Pure LLVM IR inline (no function call): `fsub → fneg → select(abs) → fcmple`.

```yaml
- decl: [a, f32, 1.0]
- decl: [b, f32, 1.0005]

# Within tolerance
- if:
    condition: { tolerance: [a, b, 0.001] }
    then:
      - print: ["equal (difference < 0.001)"]

# Outside tolerance
- if:
    condition: { tolerance: [a, b, 0.0001] }
    then:
      - print: ["never reached"]
    else:
      - print: ["not equal (difference > 0.0001)"]

# Exact equality (eps = 0)
- if:
    condition: { tolerance: [5.0, 5.0, 0.0] }
    then:
      - print: ["exactly equal"]

# Negative values work correctly
- if:
    condition: { tolerance: [-3.5, -3.501, 0.01] }
    then:
      - print: ["close enough"]
```

> **Note:** For exact integer/pointer equality use `eq`. `tolerance` is
> intended exclusively for f32 comparisons where rounding errors are expected.
