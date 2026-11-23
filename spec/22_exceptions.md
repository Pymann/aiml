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

### Which clause catches

A `catch:` is a **list** of clauses.  They are tried **in written order**, and the first
one whose `type:` the thrown object satisfies wins — satisfies meaning: the thrown class
is that type, or inherits from it (`extends`).  A clause of the base type therefore
belongs LAST; a clause after it is dead.  Matches nothing in this `try`?  Then the
exception keeps unwinding to the enclosing `try`, exactly as if this `try` were not there.

Until 2026-10-18 none of this was implemented, and silently so: the parser kept **only the
last** branch (every `type:`/`var:`/`body:` overwrote the previous one), and
code generation did not check the type at all — `__cxa_throw` got `nullptr` as type info, the
handler caught everything.  Writing two branches therefore meant: the first disappeared without a word, and
the second also got what it never wanted to catch.  The same class of error as the formerly
swallowed `free:` (spec/61) and `exit:` (spec/23).  Since then the exception record carries
the class name, and the handler compares it against the statically known set
"this type and everything that inherits from it".

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

## wasm32

Throwing and catching apply unchanged on wasm32. But the link has to bring the exceptions
along: without `-fexceptions` emcc compiles against `libc++-noexcept`, and
`__cxa_throw` then ends in `RuntimeError: unreachable` instead of throwing. The flag
is in `tools/run_wasm_test.sh` and in the projects' wasm configs.

Two things in codegen depend on the pointer width (`CompilerContext`,
`src/core/context.h`): the `size_t` arguments of the libc calls
(`getSizeType`/`getSizeConst`/`toSizeType`) and the exception record
`{object, free thunk, type tag}` (`getExcRecordType`/`excRecordField`) — on wasm32
a pointer is 4 bytes, fixed offsets 8/16 write into nothing there. `__cxa_throw`
gets a real `__class_type_info` (`getExcTypeInfo`); with `null`, Emscripten's
`__cxa_throw` reaches out of memory when building the message. Guard:
`tests/test_builtin_throw_after_list_alloc.aiml` (in `tests/wasm_tests.list`).
