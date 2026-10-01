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

- `throw: e` throws an existing exception object (a `ptr<E>` local or the catch `var`);
  inside a `catch` body, `throw: <var>` rethrows the caught object to the next handler.
  Ownership of thrown/caught objects: spec/61 §9.3.
- Unhandled exceptions propagate up the call stack to the caller.
- If no `catch` block matches at any level, the program calls `sys.exit(2)` and prints
  the exception message to stderr.

## Implementation invariant — calls inside `try` must be `invoke`

For a `throw` in a *called* function/method to reach an enclosing `try`, **every
call emitted inside a try block must be an LLVM `invoke`** whose unwind edge is
the catch landingpad (tracked via `ctx.catchBlock`). A plain `call` lets the
exception unwind straight past the handler → uncaught: AOT `std::terminate`
("terminate called without an active exception"), JIT top-level "Unhandled AIML
exception". Every call-emission path in `ast_compiler_call.cpp` (free functions,
the fptable branch's not-yet-emitted `cur_gv==null` fallback, **and regular
method calls**) must honor `ctx.catchBlock`. Symptom of a missed path in the IR:
`catch_block: ; No predecessors!`. Regression test: `tests/aot_trycatch_repro.aiml`
(free-function *and* method throw, caught by-type; expects `CAUGHT …` lines),
built AOT via `exodus-aot-repro.json` — must be checked in AOT, not only JIT.

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
