# Logic and Control Flow

## if

### Kanonische Form

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

### Kurzform

Die Kurzform ist gleichwertig zur kanonischen Form und offizieller Teil der Sprache:

```yaml
- if: [{ lt: [x, y] }, true]
  then:
    - print: [x]
  else:
    - print: [z]
```

Das erste Element der Liste ist die Condition (beliebiger Ausdruck, der einen Wahrheitswert liefert).
Das zweite Element (`true`) ist ein Pflicht-Platzhalter ohne semantische Bedeutung.

`else` ist in beiden Formen optional.

Beide Formen sind äquivalent und können gemischt verwendet werden.

## while

### Kanonische Form

```yaml
- while:
    condition: { lt: [counter, limit] }
    body:
      - set: [counter, { +: [counter, 1] }]
```

### Kurzform

```yaml
- while: [{ lt: [counter, limit] }]
  body:
    - set: [counter, { +: [counter, 1] }]
```

Das erste (und einzige) Element der Liste ist die Condition.

## Operator Arity

Logische und Vergleichsoperatoren haben feste Stelligkeit.

- Unäre Operatoren: `!`, `?`, `&` — genau 1 Operand
- Binäre Logikoperatoren: `and`, `&&`, `or`, `||` — genau 2 Operanden
- Vergleichsoperatoren: `eq`, `neq`, `lt`, `gt`, `le`, `lte`, `ge`, `gte`, `peq`, `pneq` — genau 2 Operanden

```yaml
# gültig
- if:
    condition: { or: [{ eq: [a, b] }, { gt: [a, c] }] }
    then:
      - print: ["ok"]

# Compilerfehler: 'eq' ist binär
- if:
    condition: { eq: [a, b, c] }
    then:
      - print: ["nie kompiliert"]

# Compilerfehler: '||' ist binär
- if:
    condition: { "||": [{ eq: [a, b] }, { eq: [b, c] }, { eq: [c, d] }] }
    then:
      - print: ["nie kompiliert"]
```

Längere logische Ausdrücke müssen explizit verschachtelt werden:

```yaml
- if:
    condition: { "||": [{ eq: [a, b] }, { "||": [{ eq: [b, c] }, { eq: [c, d] }] }] }
    then:
      - print: ["ok"]
```

## tolerance — Float-Vergleich mit Epsilon

`tolerance` ist ein ternärer Operator für approximative Gleichheit von Fließkommazahlen.

**Syntax:**

```yaml
{ tolerance: [val1, val2, epsilon] }
```

**Semantik:** Gibt `1` (true) zurück wenn `|val1 - val2| <= epsilon`, sonst `0` (false).

Alle drei Argumente werden automatisch zu `f32` konvertiert (i32 → SIToFP, f64 → FPTrunc).

**Implementierung:** Reines LLVM IR inline (kein Funktionsaufruf): `fsub → fneg → select(abs) → fcmple`.

```yaml
- decl: [a, f32, 1.0]
- decl: [b, f32, 1.0005]

# Innerhalb Toleranz
- if:
    condition: { tolerance: [a, b, 0.001] }
    then:
      - print: ["gleich (Differenz < 0.001)"]

# Außerhalb Toleranz
- if:
    condition: { tolerance: [a, b, 0.0001] }
    then:
      - print: ["nie erreicht"]
    else:
      - print: ["ungleich (Differenz > 0.0001)"]

# Exakte Gleichheit (eps = 0)
- if:
    condition: { tolerance: [5.0, 5.0, 0.0] }
    then:
      - print: ["exakt gleich"]

# Negative Werte funktionieren korrekt
- if:
    condition: { tolerance: [-3.5, -3.501, 0.01] }
    then:
      - print: ["nah genug"]
```

> **Hinweis:** Für exakte Integer/Pointer-Gleichheit `eq` verwenden. `tolerance` ist
> ausschließlich für f32-Vergleiche gedacht, bei denen Rundungsfehler erwartet werden.