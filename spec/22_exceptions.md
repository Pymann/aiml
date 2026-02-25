# Exception Handling

```yaml
# 1. Exception Hierarchy
- class:
    name: Exception
    members:
      - [message, string]

- class:
    name: DivisionByZeroException
    extends: Exception

# 2. Throwing an Exception
- def:
    name: divide
    args: [[a, i32], [b, i32]]
    return: i32
    body:
      - if:
          condition: { eq: [b, 0] }
          then:
            - throw: { new: DivisionByZeroException, message: "Division by zero" }
      - return: { div: [a, b] }

# 3. Try-Catch Block
- def:
    name: main
    args: []
    return: i32
    body:
      - try:
          body:
            - call: [divide, 10, 0]
            - print: ["Success"] # Skipped if throw happens
          catch:
            - type: DivisionByZeroException
              var: e
              body:
                - print: ["Caught: ${e.message}"]

            - type: Exception
              var: e
              body:
                - print: ["Caught generic error: ${e.message}"]

      - print: ["Program continues..."]
      - return: [0]
```

## Exception Propagation

- Unhandled exceptions propagate up the call stack to the caller.
- If no `catch` block matches at any level, the program calls `sys.exit(2)` and prints
  the exception message to stderr.

## Built-in System Exceptions

| Exception | Trigger |
|-----------|---------|
| `NullPointerException` | Dereferencing a null pointer |
| `TypeMismatchException` | Dynamic type cast failure (`any` vs concrete type) |
| `IndexOutOfBoundsException` | Array/list access out of range |
| `DivisionByZeroException` | Integer division or modulo by zero |
| `StackOverflowException` | Recursion depth exceeded |
| `OutOfMemoryException` | Heap allocation failed |
| `TimeoutException` | Remote call or agent task timeout |
| `SerializationException` | Serialization error |
| `DeserializationException` | Deserialization error |
| `BufferOverflowException` | `write()` when `size + sizeof(value) > capacity` |
| `BufferUnderflowException` | `read()` past written data |
| `InvalidThreadStateException` | e.g. `join()` on a detached thread |
| `MissingKeyError` | `map.remove()` on a non-existent key |
| `NamespaceCollisionError` | Two imports define the same FQN |
