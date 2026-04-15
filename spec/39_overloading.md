# Kapitel 39 — Operator Overloading

## 0. Kanonische Operatornamen

AIML verwendet **ausschließlich Wortnamen** für alle Vergleichs- und Gleichheitsoperatoren.
Symbolschreibweisen (`==`, `!=`, `<`, usw.) werden beim Parsen automatisch normalisiert:

| Symbol | Kanonischer Name | Bedeutung |
|--------|-----------------|-----------|
| `==`   | `eq`            | Gleichheit (Inhalt für Strings/Pointer, Wert für Integer) |
| `!=`   | `neq`           | Ungleichheit |
| `<`    | `lt`            | Kleiner |
| `>`    | `gt`            | Größer |
| `<=`   | `lte`           | Kleiner-gleich |
| `>=`   | `gte`           | Größer-gleich |
| `peq`  | `peq`           | Pointer-Identität (Adressvergleich) |
| `pneq` | `pneq`          | Pointer-Nichtidentität |

**`eq` / `neq` Semantik:**
- Für `string`-Pointer: Byte-für-Byte Inhaltsvergleich (wie `strcmp`)
- Für `i32`, `i64`, etc.: Integer-Gleichheit
- Für Klassen/Modelle mit `__eq__`-Methode: Dispatch auf `__eq__`

**`peq` / `pneq`:** Vergleicht immer die rohe Pointer-Adresse, nie den Inhalt.
Nützlich für Null-Checks und Identitätsvergleiche.

```aiml
- decl: [a, string, "hello"]
- decl: [b, string, "hello"]

- decl: [same_content, i32, { eq:  [a, b] }]   # 1 (Inhalt gleich)
- decl: [same_ptr,     i32, { peq: [a, b] }]   # 0 (verschiedene Allocations)
- decl: [self_ptr,     i32, { peq: [a, a] }]   # 1 (selber Pointer)
```

---

## 1. Eingebaute Overloads (math-Typen)

Für die eingebauten Typen `vec2`, `vec3`, `vec4`, `quat`, `mat4` überlastet
der Compiler die Standardoperatoren automatisch — kein zusätzlicher Syntax.

```aiml
- decl: [a, vec3, { call: [math.vec3, 1.0, 2.0, 3.0] }]
- decl: [b, vec3, { call: [math.vec3, 4.0, 5.0, 6.0] }]

- decl: [c, vec3, { +: [a, b] }]           # komponentenweise Addition
- decl: [d, vec3, { -: [a, b] }]           # komponentenweise Subtraktion
- decl: [e, vec3, { *: [a, 2.0] }]         # Skalierung (vec * f32)
- decl: [f, vec3, { /: [a, 2.0] }]         # Division durch Skalar
- decl: [g, vec3, { *: [a, b] }]           # komponentenweises Produkt (Hadamard)
- decl: [h, vec3, { -: [a] }]              # Negation (unär)

# mat4
- decl: [m1, mat4, { call: [math.mat4_identity] }]
- decl: [m2, mat4, { call: [math.mat4_translate, { call: [math.vec3, 1.0, 0.0, 0.0] }] }]
- decl: [m3, mat4, { *: [m1, m2] }]        # Matrizenmultiplikation
- decl: [v,  vec4, { call: [math.vec4, 1.0, 0.0, 0.0, 1.0] }]
- decl: [tv, vec4, { *: [m2, v] }]         # mat4 * vec4 (Transform)

# Vergleich (ergibt i32 / bool)
- decl: [vecs_eq, i32, { eq: [a, b] }]     # komponentenweise Gleichheit (alle müssen gleich sein)
```

### Dispatch-Tabelle für eingebaute Typen

| Operator | Linker Typ | Rechter Typ | Ergebnis | Bedeutung |
|----------|-----------|------------|---------|-----------|
| `+` | `vec2/3/4` | gleich | gleich | Komponentenweise Addition |
| `-` | `vec2/3/4` | gleich | gleich | Komponentenweise Subtraktion |
| `-` | `vec2/3/4` | *(unär)* | gleich | Negation |
| `*` | `vec2/3/4` | `f32` | gleich | Skalierung |
| `*` | `f32` | `vec2/3/4` | gleich | Skalierung (kommutativ) |
| `*` | `vec2/3/4` | gleich | gleich | Hadamard-Produkt |
| `/` | `vec2/3/4` | `f32` | gleich | Division durch Skalar |
| `*` | `mat4` | `mat4` | `mat4` | Matrizenmultiplikation |
| `*` | `mat4` | `vec4` | `vec4` | Matrix-Vektor-Transform |
| `+` | `mat4` | `mat4` | `mat4` | Komponentenweise Addition |
| `eq` | `vec2/3/4` | gleich | `i32` | Exakte Gleichheit aller Komponenten |

---

## 2. User-definiertes Gleichheits-Overloading — `__eq__`

Für eigene Klassen und Modelle kann `__eq__` als normale Methode definiert werden.
Sie wird automatisch aufgerufen wenn `eq` / `neq` auf Instanzen des Typs angewendet wird.

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

- decl: [equal, i32, { eq:  [p1, p2] }]    # 1 — ruft p1.__eq__(p2)
- decl: [diff,  i32, { neq: [p1, p2] }]    # 0
```

**Signatur:** `__eq__(other: ptr<ClassName>) → i32`
- Gibt `1` zurück wenn gleich, `0` wenn ungleich
- `neq` negiert automatisch das Ergebnis
- Kein `__neq__` nötig

---

## 3. Unterstützte Operatoren

| Operator | Unär | Binär | Beschreibung |
|----------|------|-------|--------------|
| `+` | — | ✓ | Addition |
| `-` | ✓ | ✓ | Subtraktion / Negation |
| `*` | — | ✓ | Multiplikation |
| `/` | — | ✓ | Division |
| `%` | — | ✓ | Modulo |
| `eq` | — | ✓ | Gleichheit (Inhalt / `__eq__` dispatch) |
| `neq` | — | ✓ | Ungleichheit |
| `lt` | — | ✓ | Kleiner |
| `lte` | — | ✓ | Kleiner-gleich |
| `gt` | — | ✓ | Größer |
| `gte` | — | ✓ | Größer-gleich |
| `peq` | — | ✓ | Pointer-Identität (Adressvergleich) |
| `pneq` | — | ✓ | Pointer-Nichtidentität |

Nicht overloadbar: `and`, `or`, `not`, `cast`, alle Kontrollfluss-Operatoren.
Kein overloading für `lt`/`gt`/`lte`/`gte` — nur `__eq__` ist definierbar.

---

## 4. Auflösungsreihenfolge für `eq` / `neq`

1. Beide Operanden sind float-Typen (`f32`, `f64`) → Float-Vergleich
2. Linker Typ ist ein eingebauter math-Struct (`vec3`, ...) → eingebauter `aiml_math_X_eq`
3. Beide Operanden sind Pointer → prüfe ob LHS-Typ `__eq__`-Methode hat → Dispatch
4. Beide Operanden sind Pointer ohne `__eq__` → String-Inhaltsvergleich (`strcmp`)
5. Integer-Operanden → direkte ICmpEQ / ICmpNE

---

## 5. Einschränkungen

- `__eq__` muss `i32` zurückgeben
- Nur der linke Operand wird für den `__eq__`-Dispatch untersucht (kein Rechts-Dispatch)
- `peq`/`pneq` umgeht `__eq__` immer — reiner Adressvergleich
