# Object-Oriented Programming Proposal

```yaml
# Class Definition
# Maps to LLVM Struct Type
- class:
    name: Point
    members:
      - [x, i32]
      - [y, i32]
    methods:
      - def:
          name: move
          args: [[dx, i32], [dy, i32]]
          return: void
          body:
            # '<.' is the self-reference operator inside a method body.
            - set: [<.x, { +: [<.x, dx] }]
            - set: [<.y, { +: [<.y, dy] }]

# Class Composition
# 'extends:' = single inheritance (inherits fields and methods from parent).
# 'models:' = structural composition (injects fields from a model; no method inheritance).
- class:
    name: GamePoint
    extends: Point        # Inherits x, y, move()
    models: [Entity]      # Injects 'id' field from the Entity model
    members:
      - [color, string]

# Packed Class (No Padding)
# Default alignment follows target platform rules (typically 8-byte for i64/f64).
# packed: true uses 1-byte alignment — no padding between fields.
- class:
    name: Header
    packed: true
    members:
      - [id, u8]
      - [count, i32] # Follows u8 immediately without alignment padding

# Instantiation (Stack)
# Direct declaration -> stack allocation (alloca).
- decl: [p1, Point]

# Instantiation (Heap)
# { new: ClassName } -> heap allocation (malloc).
- decl: [p2, ptr<Point>]
- set: [p2, { new: Point }]
- set: [p2.x, 99]
- free: [p2]

# Member Access
- set: [p1.x, 10]
- set: [p1.y, 20]

# Method Call
- call: [p1.move, 5, 5]

- print: [p1.x] # Should be 15
```
