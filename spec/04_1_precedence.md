# Operator Reference & Precedence

In AIML, all expressions use explicit nested dict syntax `{ op: [args] }`.
Evaluation order is therefore always determined by **nesting depth**, not by implicit
precedence rules. This file documents all operators, their types, and the equivalent
precedence level (for mental model and documentation purposes).

---

## 1. Precedence Table (Highest to Lowest)

| Level | Category | Operators | Arity | Associativity |
|-------|----------|-----------|-------|---------------|
| 1 | Unary / Dereference | `not`, `?`, `??`, `neg` | 1 | Right |
| 2 | Cast | `cast` | 2 | Right |
| 3 | Multiplicative | `*`, `/`, `//`, `%` | 2 | Left |
| 4 | Additive | `+`, `-` | 2 | Left |
| 5 | Bitwise Shift | `shl`, `shr` | 2 | Left |
| 6 | Bitwise AND | `band` | 2 | Left |
| 7 | Bitwise XOR | `bxor` | 2 | Left |
| 8 | Bitwise OR | `bor` | 2 | Left |
| 9 | Comparison | `eq`, `neq`, `lt`, `gt`, `le`, `ge` | 2 | None |
| 10 | Logical AND | `and` | 2+ | Left |
| 11 | Logical OR | `or` | 2+ | Left |
| 12 | Conditional | `select` | 3 | Right |
| 13 | Assignment | `set` | — | — |

> **Note**: Because AIML uses explicit `{ op: [...] }` syntax, nesting always
> takes precedence over the table above. The table is informational — it reflects
> the expected evaluation order when expressions are composed.

---

## 2. Arithmetic Operators

### `+` — Addition
```yaml
- set: [res, { +: [a, b] }]
```
- Operands: `i32 | i64 | f32 | f64`
- Result: same type as operands
- Both operands must have the same type. Use `cast` for mixed types.

### `-` — Subtraction
```yaml
- set: [res, { -: [a, b] }]
```
- Operands: `i32 | i64 | f32 | f64`
- Result: same type as operands

### `neg` — Unary Negation
```yaml
- set: [res, { neg: [a] }]
```
- Operands: `i32 | i64 | f32 | f64`
- Result: same type as operand

### `*` — Multiplication
```yaml
- set: [res, { *: [a, b] }]
```
- Operands: `i32 | i64 | f32 | f64`
- Result: same type as operands

### `/` — Division
```yaml
- set: [res, { /: [a, b] }]
```
- Integer operands: **integer division** (truncates toward zero)
- Float operands: **IEEE 754 floating-point division**
- Division by zero (integer): raises `DivisionByZeroException`
- Division by zero (float): returns `Inf` or `NaN` per IEEE 754

### `//` — Floor Division
```yaml
- set: [res, { //: [a, b] }]
```
- Operands: `i32 | i64 | f32 | f64`
- Result: always truncates toward negative infinity (unlike `/` which truncates toward zero)

### `%` — Modulo
```yaml
- set: [res, { %: [10, 3] }]   # -> 1
```
- Operands: `i32 | i64`
- Result: `i32 | i64`
- Division by zero raises `DivisionByZeroException`
- Sign of result matches the **dividend** (left operand)

---

## 3. Comparison Operators

All comparison operators return `bool`.

| Operator | Meaning | Example |
|----------|---------|---------|
| `eq` | Equal (`==`) | `{ eq: [a, b] }` |
| `neq` | Not equal (`!=`) | `{ neq: [a, b] }` |
| `lt` | Less than (`<`) | `{ lt: [a, b] }` |
| `gt` | Greater than (`>`) | `{ gt: [a, b] }` |
| `le` | Less than or equal (`<=`) | `{ le: [a, b] }` |
| `ge` | Greater than or equal (`>=`) | `{ ge: [a, b] }` |

```yaml
- set: [ok, { and: [ { lt: [x, 100] }, { ge: [x, 0] } ] }]
```

- Operands: `i32 | i64 | f32 | f64 | string | bool`
- String comparison: lexicographic (UTF-8 byte order)
- Cross-type comparison (e.g. `i32` vs `f64`): **compilation error** — use `cast` explicitly

---

## 4. Logical Operators

### `and` — Logical AND
```yaml
- set: [res, { and: [cond_a, cond_b] }]
```
- Operands: `bool`
- Result: `bool`
- Accepts 2 or more arguments: `{ and: [a, b, c] }` = `a && b && c`
- **Short-circuit evaluation**: if first operand is `false`, remaining operands are not evaluated

### `or` — Logical OR
```yaml
- set: [res, { or: [cond_a, cond_b] }]
```
- Operands: `bool`
- Result: `bool`
- Accepts 2 or more arguments
- **Short-circuit evaluation**: if first operand is `true`, remaining operands are not evaluated

### `not` — Logical NOT
```yaml
- set: [res, { not: [cond] }]
```
- Operand: `bool`
- Result: `bool`

---

## 5. Bitwise Operators

All bitwise operators work on integer types only (`i32`, `i64`, `u32`, `u64`).

| Operator | Meaning | Example |
|----------|---------|---------|
| `band` | Bitwise AND | `{ band: [a, b] }` |
| `bor` | Bitwise OR | `{ bor: [a, b] }` |
| `bxor` | Bitwise XOR | `{ bxor: [a, b] }` |
| `bnot` | Bitwise NOT (unary) | `{ bnot: [a] }` |
| `shl` | Left shift | `{ shl: [a, n] }` |
| `shr` | Right shift (arithmetic) | `{ shr: [a, n] }` |
| `ushr` | Right shift (logical/unsigned) | `{ ushr: [a, n] }` |

```yaml
- set: [flags, { bor: [0x01, 0x04] }]   # -> 0x05
- set: [masked, { band: [flags, 0xFF] }]
- set: [shifted, { shl: [1, 8] }]        # -> 256
```

- `shr` on signed integers preserves the sign bit (arithmetic shift)
- `ushr` fills with zeros regardless of sign (logical shift)
- Shift amount must be in range `[0, bit_width - 1]`; out-of-range is undefined behavior

---

## 6. Dereference Operators

### `?` — Single Dereference
```yaml
- set: [val, { '?': [p] }]   # Reads value from pointer p
```
- Operand: `ptr<T>`
- Result: `T`
- Raises `NullPointerException` if `p` is null (when null checks are enabled)

### `??` — Double Dereference
```yaml
- set: [val, { '??': [pp] }]  # Reads from pointer-to-pointer
```
- Operand: `ptr<ptr<T>>`
- Result: `T`
- Equivalent to `{ '?': [{ '?': [pp] }] }`

---

## 7. Cast Operator

### `cast` — Explicit Type Conversion
```yaml
- set: [n, { cast: [f_val, i32] }]   # f64 -> i32 (truncates)
- set: [f, { cast: [n, f64] }]        # i32 -> f64
```

Supported conversions:

| From | To | Behavior |
|------|----|---------|
| `i32` | `i64` | Zero/sign-extends |
| `i64` | `i32` | Truncates high bits |
| `i32` | `f64` | Exact for values ≤ 2^53 |
| `f64` | `i32` | Truncates toward zero; NaN → 0 |
| `f32` | `f64` | Widening, always exact |
| `f64` | `f32` | Narrowing, may lose precision |
| `bool` | `i32` | `false` → 0, `true` → 1 |
| `i32` | `bool` | 0 → `false`, non-zero → `true` |
| `ptr<T>` | `any` | Implicit (no explicit cast needed) |
| `any` | `ptr<T>` | Implicit (no runtime check in v1) |

Unsupported conversions (compile error): `string` → `i32`, `i32` → `string` (use `sys.to_string` / `sys.parse_int`)

---

## 8. Conditional Expression

### `select` — Ternary
```yaml
- set: [res, { select: [condition, value_if_true, value_if_false] }]
```
- Condition: `bool`
- Both branches must have compatible types
- Only the selected branch is evaluated (short-circuit)

```yaml
- set: [abs_x, { select: [{ ge: [x, 0] }, x, { neg: [x] }] }]
```

---

## 9. Compound Expressions

Since nesting defines order, precedence is explicit. Example for `res = (a + b) * (c - d)`:

```yaml
- set: [res, { *: [
    { +: [a, b] },
    { -: [c, d] }
  ]
}]
```

Example for `(x > 0) and (x < 100) or flag`:

```yaml
- set: [in_range, { or: [
    { and: [{ gt: [x, 0] }, { lt: [x, 100] }] },
    flag
  ]
}]
```

---

## 10. Type Promotion Rules

AIML does **not** perform implicit numeric promotion. Both operands of a binary operator
must have the same type. Use `cast` explicitly when types differ.

```yaml
# ERROR: i32 + f64 is not allowed
- set: [bad, { +: [my_i32, my_f64] }]

# CORRECT: cast first
- set: [ok, { +: [{ cast: [my_i32, f64] }, my_f64] }]
```

The only implicit conversions allowed are pointer-related (see `13_cast.md`):
- `ptr<T>` → `any` (implicit)
- `any` → `ptr<T>` (implicit, unsafe in v1)
