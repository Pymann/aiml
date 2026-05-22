# Modifiers and Access Control

```yaml
# 1. Variable Qualifiers
# Syntax: decl: [name, type, value, { const: true, volatile: true, extern: true }]

- decl: [PI, f64, 3.14159, { const: true }]
# - set: [PI, 3.0] -> Compiler Error!

- decl: [hw_register, i32, 0, { volatile: true }]
# Prevents optimization of reads/writes (essential for hardware drivers)
```

**Rule:** declaration modifiers for `decl` always belong in the **4th** list position.
`decl: [x, i32, { const: true }]` is invalid because the 3rd position is the initializer slot.
Use `decl: [x, i32, 0, { const: true }]` instead.

```yaml
# 2. Access Modifiers (Classes)
# Default access for class members is 'private' (Encapsulation).
# Namespace-level symbols (top-level functions, classes, models) are public by default.

- class:
    name: BankAccount
    members:
      - [balance, i32, 0, { access: private }]              # Only visible inside class
      - [id, i32, 12345, { access: public, const: true }] # Publicly readable, immutable

    methods:
      - def:
          name: deposit
          access: public
          args: [[amount, i32]]
          return: void
          body: ...

      - def:
          name: update_internal
          access: private
          args: []
          return: void
          body: ...

# 'protected' for inheritance
- class:
    name: Base
    members:
      - [x, i32, 0, { access: protected }] # Visible to subclasses
```
