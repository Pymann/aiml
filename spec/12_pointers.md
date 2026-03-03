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

## Auto-Dereferenzierung bei Member-Zugriff

Für `ptr<T>` wo `T` eine Klasse oder ein Model ist, wird der Pointer bei Punkt-Notation
automatisch dereferenziert — explizites `{ ?: [...] }` ist nicht nötig:

```yaml
- decl: [p, ptr<Point>, { new: Point }]

# Schreiben — automatisch (*p).x = 10
- set: [p.x, 10]

# Lesen — automatisch x = (*p).y
- decl: [y, i32]
- set: [y, p.y]

# Methoden — automatisch (*p).move(dx, dy)
- call: [p.move, 5, 3]
```

Gilt für beliebige Tiefe: `p.nested.field` dereferenziert `p` und greift dann auf das
Feld `nested` des resultierenden Structs zu.

Für primitive Typen (`ptr<i32>` etc.) ist weiterhin `{ ?: [p] }` zum Lesen und
`store: [p, val]` zum Schreiben erforderlich.
