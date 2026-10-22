# Enum Specification for AIML

## Definition
Enums are defined using the `enum` command. They map named constants to integer values (i32).

```yaml
- enum:
    name: Color
    values:
      - Red        # Auto-assigned: 0
      - Green      # Auto-assigned: 1
      - Blue: 10   # Explicit value: 10
      - Yellow     # Auto-assigned: 11 (previous + 1)
```

## `wire: true` — Vokabular einer Schnittstelle

Ein Enum, dessen Mitglieder über ein Protokoll, eine Datei oder eine Datenbank
gehen, trägt `wire: true`.  Die Angabe schaltet die Unused-Enumerator-Warnung
für dieses Enum ab (spec/49 §18.2a) — denn ein Mitglied, das nur der andere
Build benennt, ist kein totes Mitglied, sondern der Vertrag.  Sie ändert nichts
an Kodierung oder Werten.

```aiml
- enum:
    name: AgentStatus
    wire: true
    values: [disconnected, connected, busy]
```

## Usage
Enum values are accessed using `EnumName.ValueName`.

```yaml
- decl: [c, i32, Color.Red]
- print: ["Color is: ", c]

- if:
    condition: {eq: [c, Color.Red]}
    then:
      - print: ["It is Red"]
```

## Compilation
- Enums are stored in `CompilerContext` as a map of `name -> map<string, int>`.
- `Expr::evaluate` or `resolveValue` will handle `Color.Red` by looking up the enum registry.
- Enums are essentially compilation-time constants (i32).

## Argument Type Checking
An enum-typed function/method parameter is lowered to `i32`.  Passing a
**string literal (or any pointer value)** where an enum- or numeric-typed
argument (`i32`/`bool`/`f32`/…) is expected is a **compile error**, reported at
the call site with a source location — for both free functions and class/method
calls:

```text
foo.aiml:22: Call to 'Painter_paint': argument 1 ('col') type mismatch —
passing a string/pointer where an integer- or enum-typed argument is expected;
use EnumType.<value> or an int literal
```

Rationale: without this check the mismatch (`i8*` argument for an `i32`
parameter) slips past the frontend and only surfaces much later as an opaque
LLVM IR verifier failure (`'__aiml_user_main' verification FAILED: Call
parameter type does not match function signature!`) with no line number.  The
guard lives in `ast_compiler_call.cpp` (the `coerceArgs` lambda for free
functions and the concrete-method dispatch path) and mirrors the existing
enum-`set:` assignment check.  Fix a flagged call by using the enum symbol
(`EnumType.member`) or an `i32` literal instead of a quoted string.

---

## Reflection (`enums` namespace)

Enum members are compile-time `i32` constants (see above).  For the reverse
direction — turning a runtime value back into its member **name**, or resolving a
name to its value — use the reserved `enums` namespace.  This replaces
hand-written `*_to_string` converters (which must be kept in sync with the enum
by hand) and makes value-based data domains (mana colours, zones, bytecode codes)
printable/serialisable by name.

```yaml
- decl: [nm, string, { call: [enums.name, "ManaColor", color_i] }]   # 1 → "fire"
- decl: [v,  i32,    { call: [enums.value, "ManaColor", "fire"] }]    # "fire" → 1
- log.info: ["played ${ { call: [enums.name, \"EffectCode\", code] } }"]
```

### `enums.name(type, value) → string`
- `type` **must be a string literal** naming a declared enum (resolved at
  compile time).  `value` is any `i32` expression.
- Returns the name of the member whose value equals `value`; the empty string
  `""` if no member matches (values need not be contiguous).
- Lowered entirely at compile time to a `select`-chain over the enum's
  members — no runtime table, no runtime type string.

### `enums.value(type, member) → i32`
- Both `type` and `member` **must be string literals**.  Resolved to the
  member's constant `i32` at compile time (identical to writing `type.member`,
  but with the member name as data).
- Unknown enum or unknown member is a **compile error** (loud fail, with source
  location) — never a silent 0.

### Errors
A non-literal `type`/`member`, an unknown enum, or (for `enums.value`) an unknown
member is reported at the call site as a compile error, e.g.:

```text
foo.aiml:31: enums.name: unknown enum type 'ManColor' (typo?)
foo.aiml:44: enums.name: 'type' argument must be a string literal
```

### Static analysis (unused-enum, spec/49 §18)
- `enums.name(T, …)` counts as a **reflective whole-enum use**: every member of
  `T` is marked referenced.  Rationale — reflecting a value back to its name is a
  legitimate use of the *entire* member vocabulary, even though no single member
  appears by name.  This is how value-domain enums (ManaColor, GameZone, …) stop
  being false "unused" once a log/UI/serialiser adopts `enums.name`.
- `enums.value(T, "m")` marks member `m` used, exactly like the constant `T.m`.
