# Modifiers and Access Control

```yaml
# 1. Variable Qualifiers
# Syntax: decl: [name, type, value, { modifiers: [...] }]

- decl: [PI, f64, 3.14159, { const: true }]
# - set: [PI, 3.0] -> Compiler Error!

- decl: [hw_register, i32, 0, { volatile: true }]
# Prevents optimization of reads/writes (essential for hardware drivers)

# 2. Access Modifiers (Classes)
# Default access for class members is 'private' (Encapsulation).
# Namespace-level symbols (top-level functions, classes, models) are public by default.

- class:
    name: BankAccount
    members:
      - [balance, i32, { access: private }]              # Only visible inside class
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
      - [x, i32, { access: protected }] # Visible to subclasses
```
