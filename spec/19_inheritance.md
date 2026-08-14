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
# Every interface method must be DECLARED by the class (name + matching
# signature), or a CompilationError is raised.  A declaration may however be
# BODYLESS (see §5) to satisfy the contract as a no-op / default.

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

# 5. Bodyless (no-op / default) methods
# A class method may be declared WITHOUT a `body:`.  This still satisfies an
# interface contract (the method exists with the correct signature), but the
# compiler synthesizes a default return based on the return type:
#     void          → returns nothing (no-op)
#     i8/i16/i32/i64 → 0
#     f32/f64       → 0.0
#     string / ptr<T> / any → null
#     list<T> / map<K:V> / struct → empty value
# Use this for polymorphic interfaces with many methods that most implementing
# types do not need: each type spells out ONLY the methods it actually
# implements and leaves the rest bodyless.  This keeps the contract explicit
# (the method is visibly declared) while avoiding hand-written empty bodies.

- interface:
    name: CardBehaviorLike
    methods:
      - [apply_combat_stats, [[ze, ptr<ZoneEntry>]], void]
      - [get_produces,       [], map<string:i32>]

- class:
    name: ManaCard
    models: [ManaData]
    implements: [CardBehaviorLike]
    methods:
      # ManaCard has no combat — bodyless no-op:
      - def: { name: apply_combat_stats, args: [[ze, ptr<ZoneEntry>]], return: void }
      # ManaCard DOES produce mana — real body:
      - def: { name: get_produces, args: [], return: map<string:i32>, body: [{ return: <.produces }] }
```
