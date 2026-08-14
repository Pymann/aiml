# AIML Class Specification

## Overview
Classes in AIML map directly to LLVM Struct Types. They provide encapsulation (fields) and behavior (methods).

## Syntax

### Class Definition
```yaml
- class:
    name: Point
    members:
      - [x, i32, 0, { access: public }]
      - [y, i32, 0, { access: public }]
      - [id, i32, 0, { access: private }]
    methods:
      - def:
          name: move
          access: public
          args: [[dx, i32], [dy, i32]]
          return: void
          body:
            - set: [<.x, { +: [<.x, dx] }]
            - set: [<.y, { +: [<.y, dy] }]
```

### Instantiation
**Stack Allocation:**
```yaml
- decl: [p1, Point] # Allocates sizeof(Point) on stack
```

**Heap Allocation:**
```yaml
- decl: [p2, ptr<Point>, { new: Point }] # Allocates on heap, returns pointer
# ...
- free: [p2]
```

### Member Access
Dot notation is used for both stack instances and pointers (syntactic sugar).
```yaml
- set: [p1.x, 10]
- set: [p2.y, 20] # Equivalent to (*p2).y
```

#### Deep Member Chains

Arbitrarily deep paths are supported for both reads and writes:

```yaml
- class:
    name: Inner
    members: [[value, i32]]

- class:
    name: Middle
    members: [[inner, Inner], [count, i32]]

- class:
    name: Outer
    members: [[mid, Middle], [tag, i32]]

- decl: [o, Outer]

# Deep write (three levels):
- set: [o.mid.inner.value, 42]

# Deep read:
- decl: [v, i32, o.mid.inner.value]

# ptr<T> — automatic dereference at every level:
- decl: [p, ptr<Outer>, { new: Outer }]
- set: [p.mid.inner.value, 99]
- decl: [r, i32, p.mid.inner.value]
```

The segment model is resolved iteratively internally (no special syntactic case).
Each segment may be either a struct member or a list index (`field-0`):

```yaml
# Mixed chain: list element, then member
- class:
    name: Container
    members: [[items, list<Inner>]]

- decl: [c, Container]
- decl: [elem, Inner]
- set: [elem.value, 7]
- call: [c.items.push, elem]

# Read: items-0.value
- decl: [hp, i32, c.items-0.value]

# Write: items-0.value
- set: [c.items-0.value, 13]
```

**Access control** is enforced at every segment of the chain — not just the last one.

### Method Call
```yaml
- call: [p1.move, 10, 20]
```

### `<` — Implicit `this` Shortcut

Inside a method body, the compiler injects an implicit first argument
`this: ref<ClassName>` (see [18_refs_and_pointers.md](18_refs_and_pointers.md)).
The `<` token is a syntactic shortcut that desugars to `this`:

| Source            | Desugars to     | Use case                              |
|-------------------|-----------------|---------------------------------------|
| `<.field`         | `this.field`    | Member read/write inside a method     |
| `<.method(...)`   | `this.method(...)` | Method call inside a method        |
| `<` (standalone)  | `this`          | Pass the whole instance as argument   |

```yaml
- class:
    name: Counter
    members: [[value, i32, 0]]
    methods:
      - def:
          name: bump
          args: []
          body:
            - set: [<.value, { +: [<.value, 1] }]      # member write
            - decl: [v, i32, <.value]                  # member read
            - call: [log.info, "Counter v=${<.value}"] # in string interp

      - def:
          name: copy_self
          args: []
          return: Counter
          body:
            # Pass the whole instance to a free function (or sys.* builtin).
            - return: { call: [sys.clone, <] }
```

#### When to use `<` standalone

The `<`-shortcut for the whole instance is useful for:
- `sys.clone(<)` — deep-copy of `this`
- `sys.serialize(<)` — serialise `this` to JSON
- `sess.decode_rpc(rpc, <)` — decode a mirror RPC payload into the
  class's own model-mixin fields (when the class `models: [SomeReq]`
  the wire payload lines up with the class's own member layout)
- Passing `this` to a free helper function that takes `ptr<Class>` /
  `ref<Class>`

#### What `<` is **not**

- Not a pointer literal — you can't write `set: [<, other_instance]`
- Not an L-value root — `set: [<.field, …]` works because `<.` desugars
  to `this.`, but bare `<` on the LHS of a set has no defined meaning
- Not usable outside a class method body — there's no `this` to refer to
- Not the YAML merge-key operator `<<:` — single `<` is plain scalar
  text and never needs quoting

### YAML Note: `<` and `>` in arg lists

The `<` token is plain scalar in YAML — it does **not** need quoting in
flow sequences (`[<]`) or block sequences (`- <`).  The only conflict
would be `<<:` (YAML merge-key) which is two `<` characters; a single
`<` is unambiguous.

## Internal Representation
1. **Context Storage**: `CompilerContext` maintains a `ClassRegistry` mapping class names to `ClassInfo`.
2. **ClassInfo Struct**:
   - `llvm::StructType* llvmType`
   - `std::map<string, int> memberOffsets`
   - `std::map<string, FunctionInfo> methods`
   - `std::map<string, AccessModifier> accessModifiers`

3. **Method Name Mangling**:
   Methods are compiled as global LLVM functions with mangled names: `Point_move`.
   The first argument is always `<` (pointer to the struct).

## Implementation Steps
1. **Class Parsing**: Implement `ClassCommand` to parse definition and register struct type.
2. **Member Layout**: Calculate offsets and generate `llvm::StructType`.
3. **Method Compilation**: Compile methods as functions, injecting `this` argument.
4. **Member Access**: Update `resolveLValue` to handle `obj.member`.
5. **Method Dispatch**: Update `CallDispatcher` to handle `obj.method()`.
