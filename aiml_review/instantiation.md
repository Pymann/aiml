# Instantiation & Memory Control

AIML gives you full control over where your objects live.

## 1. Stack Allocation (Automatic)
Use `decl` with the direct Struct Type. The object is allocated on the stack (via `alloca`).
It is automatically destroyed when the scope ends.

### Syntax
```yaml
- decl: [p_stack, Point]
# p_stack is a "Value" (internally a pointer to the stack slot).
# Access: p_stack.x
```

## 2. Heap Allocation (Manual)
Use `new` to allocate on the heap. You must declare a **Reference/Pointer** first.
You are responsible for freeing the memory.

### Syntax
```yaml
# 1. Declare a Reference (Pointer) to a Point
- decl: [p_heap, ref<Point>]

# 2. Allocate on Heap
- set: [p_heap, { new: Point }]

# 3. Access (Same syntax due to auto-deref provided by compiler?)
# OR explicit dereference? 
# Let's say '.' operator handles pointers automatically (arrow operator -> in C++)
- set: [p_heap.x, 100]

# 4. Free Memory
- free: [p_heap]
```

## 3. Advanced: Placement / Custom Allocators (Future)
We could support `new: [Type, Allocator]` later.
