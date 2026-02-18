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
