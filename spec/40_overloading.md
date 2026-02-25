# Operator Overloading

**Operator overloading is not supported in AIML v1.**

User-defined classes cannot override the behaviour of arithmetic operators (`+`, `-`, `*`, `/`),
comparison operators (`eq`, `lt`, etc.), or any other built-in operator.

If you need custom comparison or arithmetic logic for a class, define explicit methods:

```yaml
- class:
    name: Vector2
    members:
      - [x, f64]
      - [y, f64]
    methods:
      - def:
          name: add
          args: [[other, Vector2]]
          return: Vector2
          body:
            - decl: [result, Vector2]
            - set: [result.x, { +: [<.x, other.x] }]
            - set: [result.y, { +: [<.y, other.y] }]
            - return: [result]

      - def:
          name: equals
          args: [[other, Vector2]]
          return: bool
          body:
            - return: { and: [{ eq: [<.x, other.x] }, { eq: [<.y, other.y] }] }

# Usage:
- decl: [a, Vector2]
- set: [a.x, 1.0]
- set: [a.y, 2.0]
- decl: [b, Vector2]
- set: [b.x, 3.0]
- set: [b.y, 4.0]
- decl: [c, Vector2]
- set: [c, { call: [a.add, b] }]  # c = a + b (explicit method)
```

Operator overloading may be considered for a future version of AIML.
