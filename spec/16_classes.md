# AIML Class Specification

## Overview
Classes in AIML map directly to LLVM Struct Types. They provide encapsulation (fields) and behavior (methods).

## Syntax

### Class Definition
```yaml
- class:
    name: Point
    members:
      - [x, i32, { access: public }]
      - [y, i32, { access: public }]
      - [id, i32, { access: private }]
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

#### Tiefe Member-Ketten

Beliebig tiefe Pfade sind für Reads und Writes unterstützt:

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

# Tiefer Write (drei Ebenen):
- set: [o.mid.inner.value, 42]

# Tiefer Read:
- decl: [v, i32, o.mid.inner.value]

# ptr<T> — automatische Dereferenz an jeder Ebene:
- decl: [p, ptr<Outer>, { new: Outer }]
- set: [p.mid.inner.value, 99]
- decl: [r, i32, p.mid.inner.value]
```

Das Segment-Modell wird intern iterativ aufgelöst (kein syntaktischer Sonderfall).
Jedes Segment darf entweder ein struct-Member oder ein list-Index (`field-0`) sein:

```yaml
# Gemischte Kette: list-Element, dann Member
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

**Zugriffskontrolle** wird an jedem Segment der Kette geprüft — nicht nur am letzten.

### Method Call
```yaml
- call: [p1.move, 10, 20]
```

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
