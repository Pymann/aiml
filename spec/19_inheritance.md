# Inheritance and Interfaces

```yaml
# 1. Inheritance
# Syntax: extends: ParentClass
# Memory Layout: Parent fields come first (C++ style).

- class:
    name: Shape
    members:
      - [x, i32]
      - [y, i32]

- class:
    name: Circle
    extends: Shape
    members:
      - [radius, i32]
    # Circle memory: [x, y, radius]

# method overriding?
- def:
    name: Shape.draw
    args: []
    return: void
    body: ...

# 2. Interfaces (Contracts)
# Defines a set of methods that strict classes must implement.
# Maps to VTable / Fat Pointers in implementation.

- interface:
    name: Drawable
    methods:
      - [draw, [], void]  # name, args, return

# 3. Implementation
- class:
    name: Button
    extends: Shape
    implements: [Drawable]
    members:
      - [label, string]
    methods:
      - def:
          name: draw    # Must match Interface
          args: []
          return: void
          body:
            - print: ["Drawing Button"]

# Polymorphism
# - decl: [d, ref<Drawable>]
# - set: [d, { new: Button }]
# - call: [d.draw]  # Virtual call via VTable
```
