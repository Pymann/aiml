# Pointer Syntax

```yaml
# Declaration
- decl: [x, i32, 42]

# Pointer to x
# Syntax: ptr<Type>
- decl: [p, ptr<i32>, { '&': [x] }]

# Address of Address
- decl: [pp_temp, ptr<ptr<i32>>, { '&&': [x] }]

# Dereference (Read)
# Syntax: {'?': [ptr]}
# Raises NullPointerException if ptr is null (null checks are ON by default).
# Disable null checks with an unsafe block.
- print: [["Value of X via P: ${ { '?': [p] } }"]]

# Store (Write to address)
# Syntax: store: [ptr, value, (optional)depth]
# depth 1 (default): *ptr = value
# depth N: *(*...*ptr) = value (N dereferences)
- store: [p, 100]
- print: [["New value of X: ${x}"]]

# Pointer to Pointer
- decl: [pp, ptr<ptr<i32>>, { '&': [p] }]

# Double Dereference
# Syntax: '??'
# Equivalent to two consecutive '?' dereferences.
# Raises NullPointerException if either pointer in the chain is null.
- print: [["Value of X via PP: ${ { '??': [pp] } }"]]

# Deep Store (Depth 1)
# Writes to *(*pp) which is x
- store: [pp, 99, 1]
- print: [["Modified X via PP deep store: ${x}"]]
```

## Auto-Dereferencing on Member Access

For `ptr<T>` where `T` is a class or model, the pointer is automatically dereferenced
on dot-notation access — explicit `{ "?": [...] }` is not required:

```yaml
- decl: [p, ptr<Point>, { new: Point }]

# Write — automatically (*p).x = 10
- set: [p.x, 10]

# Read — automatically x = (*p).y
- decl: [y, i32]
- set: [y, p.y]

# Methods — automatically (*p).move(dx, dy)
- call: [p.move, 5, 3]
```

Applies at arbitrary depth: `p.nested.field` dereferences `p` and then accesses the
field `nested` of the resulting struct.

For primitive types (`ptr<i32>` etc.) `{ "?": [p] }` is still required for reading and
`store: [p, val]` for writing.  Semantik analog C:

| C            | AIML                       |
|--------------|----------------------------|
| `p = 5`      | `set: [p, 5]`              | (rebind pointer slot)
| `*p = 5`     | `store: [p, 5]`            | (deref-write, depth=1)
| `**pp = 5`   | `store: [pp, 5, 2]`        | (deref-write, depth=2)
| `int v = *p` | `set: [v, { '?': [p] }]`   | (read via deref)

`store:` ist das natürliche out-param-Pattern:

```yaml
- def:
    name: write_out
    args: [[out, ptr<i32>]]
    body:
      - store: [out, 5]      # *out = 5

- decl: [val, i32, 0]
- call: [write_out, { '&': [val] }]
# nach Call: val == 5
```
