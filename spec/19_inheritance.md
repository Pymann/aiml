# Inheritance and Interfaces

```yaml
# 1. Inheritance
# Syntax: extends: ParentClass
# Single inheritance only. Memory layout: parent fields come first (C++ style).

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
    # Circle memory layout: [x, y, radius]

# Method overriding
- def:
    name: Shape.draw
    args: []
    return: void
    body: ...

# 2. Interfaces (Contracts)
# Defines a set of method signatures that implementing classes must provide.
# All interface methods MUST be implemented by the class, or a CompilationError is raised.

- interface:
    name: Drawable
    methods:
      - [draw, [], void]  # [name, args, return_type]

# 3. Implementation
- class:
    name: Button
    extends: Shape
    implements: [Drawable]
    members:
      - [label, string]
    methods:
      - def:
          name: draw    # Must match Interface signature exactly
          args: []
          return: void
          body:
            - print: ["Drawing Button"]

# Polymorphism
# - decl: [d, ptr<Drawable>]
# - set: [d, { new: Button }]
# - call: [d.draw]  # Virtual call via VTable

# 4. Composition with models:
# 'models: [ModelName]' injects fields from a Model into the class.
# This is structural composition — fields only, no method inheritance.
# See 17_classes_examples.md for usage.
```
