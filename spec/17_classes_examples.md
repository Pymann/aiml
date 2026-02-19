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
            # 'this' is implicit or explicit? Let's say explicit for clarity here.
            - set: [this.x, { add: [this.x, dx] }]
            - set: [this.y, { add: [this.y, dy] }]

# Class Composition (Mixin Models)
# Access fields defined in a model directly in the class.
# - model:
#     name: Entity
#     fields: [[id, i32]]
- class:
    name: GamePoint
    extends: Point
    models: [Entity] # Mixin 'id' field
    members:
      - [color, string]

# Packed Class (No Padding)
# Useful for binary formats / hardware mapping
# Maps to LLVM 'packed' struct type
- class:
    name: Header
    packed: true
    members:
      - [id, u8]
      - [count, i32] # Normally would align to 4 bytes, here it follows u8 immediately

# Instantiation (Stack)
- decl: [p1, Point] 

# Instantiation (Heap) - Full Control
- decl: [p2, ref<Point>]       # Declare pointer
- set: [p2, { new: Point }]    # Allocate on Heap
- set: [p2.x, 99]              # Access (Arrow-like)
- free: [p2]                   # Manual Free

# Member Access
# Syntax: object.member
- set: [p1.x, 10]
- set: [p1.y, 20]

# Method Call
# Syntax: object.method(args) -> { call: [object.method, args...] }
- call: [p1.move, 5, 5]

- print: [p1.x] # Should be 15
```
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
            # 'this' is implicit or explicit? Let's say explicit for clarity here.
            - set: [this.x, { add: [this.x, dx] }]
            - set: [this.y, { add: [this.y, dy] }]

# Class Composition (Mixin Models)
# Access fields defined in a model directly in the class.
# - model:
#     name: Entity
#     fields: [[id, i32]]
- class:
    name: GamePoint
    extends: Point
    models: [Entity] # Mixin 'id' field
    members:
      - [color, string]

# Packed Class (No Padding)
# Useful for binary formats / hardware mapping
# Maps to LLVM 'packed' struct type
- class:
    name: Header
    packed: true
    members:
      - [id, u8]
      - [count, i32] # Normally would align to 4 bytes, here it follows u8 immediately

# Instantiation (Stack)
- decl: [p1, Point] 

# Instantiation (Heap) - Full Control
- decl: [p2, ref<Point>]       # Declare pointer
- set: [p2, { new: Point }]    # Allocate on Heap
- set: [p2.x, 99]              # Access (Arrow-like)
- free: [p2]                   # Manual Free

# Member Access
# Syntax: object.member
- set: [p1.x, 10]
- set: [p1.y, 20]

# Method Call
# Syntax: object.method(args) -> { call: [object.method, args...] }
- call: [p1.move, 5, 5]

- print: [p1.x] # Should be 15
