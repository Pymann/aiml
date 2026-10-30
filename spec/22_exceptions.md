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

Bis 18.10.2026 war davon nichts umgesetzt, und zwar still: der Parser behielt **nur den
letzten** Zweig (jeder `type:`/`var:`/`body:` ueberschrieb den vorherigen), und die
Codegenerierung prueefte den Typ gar nicht — `__cxa_throw` bekam `nullptr` als Typinfo, der
Handler fing alles.  Zwei Zweige zu schreiben hiess also: der erste verschwand wortlos, und
der zweite bekam auch das, was er nie fangen wollte.  Dieselbe Fehlerklasse wie das frueher
verschluckte `free:` (spec/61) und `exit:` (spec/23).  Das Ausnahme-Record traegt seitdem
den Klassennamen, und der Handler vergleicht ihn gegen die statisch bekannte Menge
"dieser Typ und alles, was von ihm erbt".

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

Werfen und Fangen gilt auf wasm32 unverändert. Der Link muss die Ausnahmen aber
mitnehmen: emcc übersetzt ohne `-fexceptions` gegen `libc++-noexcept`, und
`__cxa_throw` endet dann in `RuntimeError: unreachable` statt zu werfen. Das Flag
steht in `tools/run_wasm_test.sh` und in den wasm-Configs der Projekte.

Im Codegen ist dabei zweierlei zeigerbreitenabhängig (`CompilerContext`,
`src/core/context.h`): die `size_t`-Argumente der libc-Aufrufe
(`getSizeType`/`getSizeConst`/`toSizeType`) und der Ausnahme-Record
`{Objekt, Free-Thunk, Typ-Tag}` (`getExcRecordType`/`excRecordField`) — auf wasm32
ist ein Zeiger 4 Byte, feste Abstände 8/16 schreiben dort ins Leere. `__cxa_throw`
bekommt ein echtes `__class_type_info` (`getExcTypeInfo`); mit `null` greift
Emscriptens `__cxa_throw` beim Bauen der Meldung aus dem Speicher. Wächter:
`tests/test_builtin_throw_after_list_alloc.aiml` (in `tests/wasm_tests.list`).
