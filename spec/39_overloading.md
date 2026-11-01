# Chapter 39 — Operator Overloading

## 0. Canonical Operator Names

AIML uses **word names exclusively** for all comparison and equality operators.
Symbol forms (`==`, `!=`, `<`, etc.) are automatically normalized during parsing:

| Symbol | Canonical Name | Meaning |
|--------|----------------|---------|
| `==`   | `eq`           | Equality (content for strings/pointers, value for integers) |
| `!=`   | `neq`          | Inequality |
| `<`    | `lt`           | Less than |
| `>`    | `gt`           | Greater than |
| `<=`   | `lte`          | Less than or equal |
| `>=`   | `gte`          | Greater than or equal |
| `peq`  | `peq`          | Pointer identity (address comparison) |
| `pneq` | `pneq`         | Pointer non-identity |

**`eq` / `neq` semantics:**
- For `string` pointers: byte-by-byte content comparison (like `strcmp`)
- For `i32`, `i64`, etc.: integer equality
- For classes/models with an `__eq__` method: dispatch to `__eq__`

**`peq` / `pneq`:** Always compares the raw pointer address, never the content.
Useful for null checks and identity comparisons.

```aiml
- decl: [a, string, "hello"]
- decl: [b, string, "hello"]

- decl: [same_content, i32, { eq:  [a, b] }]   # 1 (content equal)
- decl: [same_ptr,     i32, { peq: [a, b] }]   # 0 (different allocations)
- decl: [self_ptr,     i32, { peq: [a, a] }]   # 1 (same pointer)
```

---

## 1. Built-in Overloads (math types)

For the built-in types `vec2`, `vec3`, `vec4`, `quat`, `mat4`, the compiler
automatically overloads the standard operators — no additional syntax required.

```aiml
- decl: [a, vec3, { call: [math.vec3, 1.0, 2.0, 3.0] }]
- decl: [b, vec3, { call: [math.vec3, 4.0, 5.0, 6.0] }]

- decl: [c, vec3, { +: [a, b] }]           # component-wise addition
- decl: [d, vec3, { -: [a, b] }]           # component-wise subtraction
- decl: [e, vec3, { *: [a, 2.0] }]         # scaling (vec * f32)
- decl: [f, vec3, { /: [a, 2.0] }]         # division by scalar
- decl: [g, vec3, { *: [a, b] }]           # component-wise product (Hadamard)
- decl: [h, vec3, { -: [a] }]              # negation (unary)

# mat4
- decl: [m1, mat4, { call: [math.mat4_identity] }]
- decl: [m2, mat4, { call: [math.mat4_translate, { call: [math.vec3, 1.0, 0.0, 0.0] }] }]
- decl: [m3, mat4, { *: [m1, m2] }]        # matrix multiplication
- decl: [v,  vec4, { call: [math.vec4, 1.0, 0.0, 0.0, 1.0] }]
- decl: [tv, vec4, { *: [m2, v] }]         # mat4 * vec4 (transform)

# Comparison (yields i32 / bool)
- decl: [vecs_eq, i32, { eq: [a, b] }]     # component-wise equality (all must be equal)
```

### Dispatch Table for Built-in Types

| Operator | Left Type  | Right Type | Result | Meaning |
|----------|-----------|------------|--------|---------|
| `+` | `vec2/3/4` | same | same | Component-wise addition |
| `-` | `vec2/3/4` | same | same | Component-wise subtraction |
| `-` | `vec2/3/4` | *(unary)* | same | Negation |
| `*` | `vec2/3/4` | `f32` | same | Scaling |
| `*` | `f32` | `vec2/3/4` | same | Scaling (commutative) |
| `*` | `vec2/3/4` | same | same | Hadamard product |
| `/` | `vec2/3/4` | `f32` | same | Division by scalar |
| `*` | `mat4` | `mat4` | `mat4` | Matrix multiplication |
| `*` | `mat4` | `vec4` | `vec4` | Matrix-vector transform |
| `+` | `mat4` | `mat4` | `mat4` | Component-wise addition |
| `eq` | `vec2/3/4` | same | `i32` | Exact equality of all components |
| `neq` | `vec2/3/4` | same | `i32` | Negation von `eq` (spec/43_0) |

---

## 2. User-Defined Equality Overloading — `__eq__`

For custom classes and models, `__eq__` can be defined as a regular method.
It is automatically called when `eq` / `neq` is applied to instances of the type.

```aiml
- class:
    name: Point
    members:
      - [x, i32]
      - [y, i32]
    methods:
      - def:
          name: __eq__
          args: [[other, ptr<Point>]]
          return: i32
          body:
            - decl: [sx, i32, { eq: [x, other.x] }]
            - decl: [sy, i32, { eq: [y, other.y] }]
            - return: { and: [sx, sy] }

- decl: [p1, Point]
- set: [p1.x, 3]
- set: [p1.y, 4]
- decl: [p2, Point]
- set: [p2.x, 3]
- set: [p2.y, 4]

- decl: [equal, i32, { eq:  [p1, p2] }]    # 1 — calls p1.__eq__(p2)
- decl: [diff,  i32, { neq: [p1, p2] }]    # 0
```

**Signature:** `__eq__(other: ptr<ClassName>) → i32`
- Returns `1` if equal, `0` if unequal
- `neq` automatically negates the result
- No `__neq__` needed

---

## 3. Supported Operators

| Operator | Unary | Binary | Description |
|----------|-------|--------|-------------|
| `+` | — | ✓ | Addition |
| `-` | ✓ | ✓ | Subtraction / Negation |
| `*` | — | ✓ | Multiplication |
| `/` | — | ✓ | Division |
| `%` | — | ✓ | Modulo |
| `eq` | — | ✓ | Equality (content / `__eq__` dispatch) |
| `neq` | — | ✓ | Inequality |
| `lt` | — | ✓ | Less than |
| `lte` | — | ✓ | Less than or equal |
| `gt` | — | ✓ | Greater than |
| `gte` | — | ✓ | Greater than or equal |
| `peq` | — | ✓ | Pointer identity (address comparison) |
| `pneq` | — | ✓ | Pointer non-identity |

Not overloadable: `and`, `or`, `not`, `cast`, all control-flow operators.
No overloading for `lt`/`gt`/`lte`/`gte` — only `__eq__` is definable.

---

## 4. Resolution Order for `eq` / `neq`

1. Both operands are float types (`f32`, `f64`) → float comparison
2. Left type is a built-in math struct (`vec3`, ...) → built-in `aiml_math_X_eq`
3. Both operands are pointers → check whether the LHS type has an `__eq__` method → dispatch
4. Both operands are pointers without `__eq__` → string content comparison (`strcmp`)
5. Integer operands → direct ICmpEQ / ICmpNE

---

## 5. Limitations

- `__eq__` must return `i32`
- Only the left operand is examined for `__eq__` dispatch (no right-side dispatch)
- `peq`/`pneq` always bypasses `__eq__` — pure address comparison
