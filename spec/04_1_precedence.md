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
- Both operands must have the same type. Use `cast` for mixed types — mixing two *typed* values
  is a compile error, while a literal adopts the other operand's type (§7a).

### `-` — Subtraction
```yaml
- set: [res, { -: [a, b] }]
```
- Operands: `i32 | i64 | f32 | f64`
- Result: same type as operands

### `-` — Unary Negation
```yaml
- set: [res, { -: [a] }]
```
- `neg` is **not** an operator name: the compiler reports `Unknown unary operator: neg`
  (measured 29.10.2026).  Unary negation is `-` with one operand.
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
- Cross-type comparison (e.g. `i32` vs `f64`, or `f32` vs `f64`): **compilation error** — use
  `cast` explicitly. The error names both types; it is never silently converted.
- **A numeric literal is not a type.** `{ neq: [d, 1280.0] }` with `d: f64` is well-formed: the
  literal is materialized as `f64`, because a written-out constant has no type of its own until
  it is used. Nothing is converted at run time, so this is literal typing, not an implicit cast
  (see §7a).

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

> **Umgesetzt erst 2026-10-15 — vorher war diese Regel nur Text.**  `and`/`&&` und
> `or`/`||` erzeugten `CreateAnd` / `CreateOr` auf **beiden bereits ausgewerteten**
> Operanden; eine Kurzauswertung gab es nicht.  Damit war das häufigste Schutzmuster der
> Sprache wirkungslos:
>
> ```yaml
> - if:
>     condition: { '&&': [{ pneq: [<.live, null] }, { gt: [{ call: [<.live.has] }, 0] }] }
> ```
>
> Die Wache bestand, der zweite Operand lief trotzdem — gemessen im cosmos-Client als
> `SIGSEGV at addr=0x8`.  Jeder Null-Test vor einem Zugriff, jede Bereichsprüfung vor einem
> Index war auf dieselbe Weise blind.  Seither emittiert `visitBinary` echten Kontrollfluss
> (bedingter Sprung + `phi`), **bevor** die Operanden ausgewertet werden.  `equiv` und `xor`
> brauchen beide Seiten und bleiben bitweise.  Regressionstest:
> `tests/test_and_short_circuit_guard.aiml`.

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

`cast` converts between **all** numeric scalar types — `bool`, `i32`, `i64`, `f32`, `f64` — in
every direction. The table below is complete: every source/target pair is defined, including the
identity cast (which is a no-op). An earlier version listed only the pairs around `i32`/`f64`,
which made ordinary conversions such as `i32` → `f32` a compile error.

| From → To | `bool` | `i32` / `i64` | `f32` / `f64` |
|---|---|---|---|
| `bool` | identity | zero-extend: `false`→0, `true`→1 | `false`→`0.0`, `true`→`1.0` |
| `i32` / `i64` | `0`→`false`, else `true` | sign-extend / truncate | signed integer → float (see rounding) |
| `f32` / `f64` | `0.0`→`false`, else `true` (NaN → `false`) | truncate toward zero (see range) | extend / round to nearest |

**Rounding.** `i64` → `f32` and `i64` → `f64` round to nearest when the value exceeds the mantissa
(2^24 and 2^53); `i32` → `f64` is always exact. `f64` → `f32` may lose precision; `f32` → `f64` is
always exact.

**Range (float → integer).** The value is truncated toward zero. Two inputs have no integer
image and are therefore **defined** rather than left to the hardware: `NaN` yields `0`, and a
value outside the target's range is **clamped** to that range (`i32`: −2147483648 … 2147483647).
Without this, an out-of-range conversion is undefined at IR level and a build could legitimately
produce any value at all.

**NaN as a truth value** is `false`, the same answer `if` gives for it — `cast` to `bool` and a
condition must not disagree about the same value.

| From | To | Behavior |
|------|----|---------|
| `ptr<T>` | `any` | Implicit (no explicit cast needed) |
| `any` | `ptr<T>` | Implicit (no runtime check in v1) |

Unsupported conversions (compile error): `string` → `i32`, `i32` → `string` (use `sys.to_string` / `sys.parse_int`)

---

## 7a. Literal typing vs. implicit conversion

These two look alike and are not:

```yaml
- decl: [ratio, f64, 0.5]
- decl: [other, f32, 0.25]

- if: { condition: { gt: [ratio, 0.4] },         then: [...] }   # ok — literal becomes f64
- if: { condition: { gt: [ratio, other] },       then: [...] }   # ERROR — f64 vs f32
- if: { condition: { gt: [ratio, { cast: [other, f64] }] }, then: [...] }   # ok — asked for
```

A **literal** has no type until its context gives it one, so materializing `0.4` as `f64`
converts nothing; the constant is simply built in that type. A **value** already has a type, and
changing it loses information (`f64` → `f32` loses precision, `f32` → `i32` loses the fraction) —
that has to be written down.

The rule applies to both comparison (§3) and arithmetic (§2), which share one operand-reconciling
step in the compiler. Before this was specified, a mixed pair fell through that step unchanged and
reached LLVM as an instruction with two different operand types: the module then failed
verification (`Both operands to FCmp instruction are not of the same type!`) instead of producing
the error promised here — a diagnostic that pointed at the compiler rather than at the source
line.

Integer width mixing (`i32` vs `i64`) is still reconciled silently by sign-extension; it is part
of the implicit-conversion set listed in `/spec/13_cast.md` §6 and kept for now.

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
- set: [abs_x, { select: [{ ge: [x, 0] }, x, { -: [x] }] }]
```

> **Not implemented yet (measured 29.10.2026).**  The compiler has no `select`; worse, the
> expression is accepted without a diagnostic and evaluates to `0` — `{ select: [false, 10, 20] }`
> yields `0`, the example above yields `0` for `x = -3`.  Until it exists, write the branch as
> an `if` with two `set`s.  (The silent `0` is a compiler bug to fix, not a feature.)

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
