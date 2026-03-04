# Kapitel 39 — Operator Overloading

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

# Vergleich (ergibt i8 / bool)
- decl: [eq, i8, { ==: [a, b] }]           # komponentenweise Gleichheit (alle müssen gleich sein)
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
| `==` | `vec2/3/4` | gleich | `i8` | Exakte Gleichheit aller Komponenten |
| `!=` | `vec2/3/4` | gleich | `i8` | Ungleichheit |

---

## 2. User-definiertes Operator Overloading

### 2.1 In Klassen — `operators:`-Sektion

```aiml
- class:
    name: Complex
    members:
      - [re, f32]
      - [im, f32]
    operators:
      "+":
        args: [[other, Complex]]
        return: Complex
        body:
          - decl: [r, Complex]
          - set: [r.re, { +: [<.re, other.re] }]
          - set: [r.im, { +: [<.im, other.im] }]
          - return: r
      "-":
        args: [[other, Complex]]
        return: Complex
        body:
          - decl: [r, Complex]
          - set: [r.re, { -: [<.re, other.re] }]
          - set: [r.im, { -: [<.im, other.im] }]
          - return: r
      "*":
        args: [[other, Complex]]
        return: Complex
        body:
          # (a+bi)(c+di) = (ac−bd) + (ad+bc)i
          - decl: [r, Complex]
          - set: [r.re, { -: [{ *: [<.re, other.re] }, { *: [<.im, other.im] }] }]
          - set: [r.im, { +: [{ *: [<.re, other.im] }, { *: [<.im, other.re] }] }]
          - return: r
      "==":
        args: [[other, Complex]]
        return: i8
        body:
          - return: { and: [{ ==: [<.re, other.re] }, { ==: [<.im, other.im] }] }

# Verwendung — exakt gleiche Syntax wie für primitive Typen
- decl: [a, Complex]
- set: [a.re, 1.0]
- set: [a.im, 2.0]
- decl: [b, Complex]
- set: [b.re, 3.0]
- set: [b.im, 4.0]

- decl: [c, Complex, { +: [a, b] }]        # → (4.0, 6.0i)
- decl: [d, Complex, { *: [a, b] }]        # → (-5.0, 10.0i)
- decl: [eq, i8,     { ==: [a, b] }]       # → 0
```

### 2.2 Als Top-Level-Funktionen — `operator:`-Schlüssel in `def`

Für Typen die man nicht selbst definiert hat (z.B. externe Structs via FFI),
oder für Operatoren zwischen zwei verschiedenen Typen:

```aiml
- def:
    name: vec3_add_ivec3
    operator: "+"
    args: [[a, vec3], [b, ivec3]]
    return: vec3
    body:
      - decl: [r, vec3]
      - set: [r.x, { +: [a.x, { f32: [b.x] }] }]
      - set: [r.y, { +: [a.y, { f32: [b.y] }] }]
      - set: [r.z, { +: [a.z, { f32: [b.z] }] }]
      - return: r

# Jetzt funktioniert:
- decl: [v, vec3, { call: [math.vec3, 1.0, 2.0, 3.0] }]
- decl: [iv, ivec3, { call: [math.ivec3, 1, 0, -1] }]
- decl: [result, vec3, { +: [v, iv] }]
```

---

## 3. Unterstützte Operatoren

| Symbol | Unär | Binär | Beschreibung |
|--------|------|-------|--------------|
| `+` | — | ✓ | Addition |
| `-` | ✓ | ✓ | Subtraktion / Negation |
| `*` | — | ✓ | Multiplikation |
| `/` | — | ✓ | Division |
| `%` | — | ✓ | Modulo |
| `==` | — | ✓ | Gleichheit (muss `i8` zurückgeben) |
| `!=` | — | ✓ | Ungleichheit (muss `i8` zurückgeben) |
| `<` | — | ✓ | Kleiner (muss `i8` zurückgeben) |
| `<=` | — | ✓ | Kleiner-gleich |
| `>` | — | ✓ | Größer |
| `>=` | — | ✓ | Größer-gleich |

Nicht overloadbar: `and`, `or`, `not`, `cast`, alle Kontrollfluss-Operatoren.

---

## 4. Auflösungsreihenfolge (Dispatch)

Wenn `{ +: [a, b] }` kompiliert wird:

1. Sind beide Operanden primitive Typen (`i32`, `f32`, ...)? → Primitive Addition
2. Ist der linke Typ ein eingebauter math-Typ (`vec3`, `mat4`, ...)? → Eingebaute Overloads (§1)
3. Hat der linke Typ einen `operators:"+"`-Eintrag? → Class-Operator-Methode
4. Gibt es eine Top-Level-`def` mit `operator: "+"` und passenden Argumenttypen? → Free-Function-Operator
5. Kein Match → Compilerfehler

Bei mehreren passenden Top-Level-Operatoren gewinnt die spezifischste Typ-Signatur.

---

## 5. Einschränkungen

- Operatoren können keine neuen Typen als Rückgabe haben, die noch nicht deklariert sind
- Keine impliziten Konversionen — `{ +: [vec3, f32] }` ist nur gültig wenn explizit definiert
- `==` und Vergleichsoperatoren **müssen** `i8` zurückgeben (damit `if` und `while` sie nutzen können)
- Keine Kurzschluss-Auswertung für user-definierte `and`/`or` (nicht overloadbar)
