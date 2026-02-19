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
      - Green      # Auto-assigned: 1
      - Blue: 10   # Explicit value: 10
      - Yellow     # Auto-assigned: 11 (previous + 1)
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
