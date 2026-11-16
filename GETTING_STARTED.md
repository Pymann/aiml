# Getting started with AIML

Every snippet below is taken from a test in this repository that passes, or from
the normative spec chapter it cites. The file is named after each one so you can
read it in full.

## 0. Get a compiler

There are no prebuilt binaries yet; build it from source, which takes one
dependency fetch and one build:

```bash
exodus pkg install              # pinned system libraries into __exodus_cache/
exodus build -c exodus.json     # the compiler  → out/aiml/aiml
exodus build -c exodus2.json    # the runtime   → out/libaiml/libaiml.so
```

[BUILDING.md](BUILDING.md) has the prerequisites, the container recipe and what
to expect from the test suite.

## 1. Run something

```bash
out/aiml/aiml tests/test.aiml
```

That compiles in memory (JIT) and runs immediately. The process exit code is the
program's own, which is why most tests end in `sys.exit`.

A whole program can be a bare list of statements — there is no mandatory
wrapper (`tests/test.aiml`):

```yaml
- call: [io.write, io.stdout, "Hello from io.write!\n"]
- decl: [name, string]
- set: [name, "User"]
- print: ["Hello ${name}."]
- call: [sys.exit, 42]
```

If a `main` function exists, it is the entry point instead
(`spec/08_entrypoint.md`), and its return value is the exit code:

```yaml
- def:
    name: main
    return: i32
    body:
      - return: 0
```

## 2. Statements, expressions, types

* A **statement** is a list item whose key names it: `decl`, `set`, `call`,
  `if`, `while`, `return`, `idx`, `import`, `def`, `class`, `model`, `free`.
* An **expression** is a mapping whose key is the operator or the operation:
  `{ +: [a, b] }`, `{ at: [xs, i] }`, `{ call: [f, arg] }`, `{ new: Widget }`,
  `{ cast: [value, i64] }`.
* A **type** is written as a string: `i32`, `f64`, `bool`, `string`,
  `ptr<Node>`, `ref<Node>`, `shared<Node>`, `list<string>`, `map<string:i32>`,
  `array<f32:4:4>`, `buffer<u8>`, `vec3`, `mat4`, `path`.

Quote a type when YAML would otherwise mangle it (`"array<i32:3>"`), and quote
operator keys that YAML reads as something else (`"+"`, `"<"`, `"&&"`).

## 3. Variables

`decl` declares, optionally with an initialiser; `set` assigns
(`tests/test.aiml`, `tests/test_collections.aiml`):

```yaml
- decl: [age, i32]
- set: [age, 25]
- decl: [flags, string, "-O2 -Wall"]
- decl: [parts, list<string>, ["a", "b", "c"]]
- decl: [ages, map<string:i32>, {zoe: 3, adam: 1, mia: 2}]
```

Strings interpolate with `${...}`, and an expression may stand inside it
(`tests/test_collections.aiml`):

```yaml
- print: ["arr-2 = ${ { at: [arr, 2] } }"]
```

Fixed-size arrays are written with `idx` and read with `at`:

```yaml
- decl: [arr, "array<i32:3>"]
- idx: [arr, 0, 10]
- idx: [arr, 2, { +: [{ at: [arr, 0] }, { at: [arr, 1] }] }]
```

## 4. Control flow

`if` takes `condition`, `then` and optionally `else`
(`tests/test_ai_dense.aiml`):

```yaml
- if:
    condition: { "&&": [{ ">": [var_e0, 0.49] }, { "<": [var_e0, 0.51] }] }
    then:
      - set: [pass, { "+": [pass, 1] }]
    else:
      - set: [fail, { "+": [fail, 1] }]
```

Logical operators take exactly **two** operands; longer chains nest.

The only loop is `while` (`spec/06_loops.md`), in a canonical and a short form:

```yaml
- while:
    condition: { lt: [idx, 20000] }
    body:
      - set: [idx, { +: [idx, 1] }]
```

There is no `for:`. The compiler rejects that key by name and points at
`spec/06`.

To walk a collection lazily, use `iterator<T>` with `while`
(`spec/54_iterators.md`):

```yaml
- decl: [it, "iterator<string>", { call: [kv.iter] }]
- while:
    condition: { call: [it.has_next] }
    body:
      - decl: [key, string, { call: [it.next] }]
      - print: ["key: ${key}"]
- call: [free, it]
```

## 5. Functions

`def` names the function, its `args` (name/type pairs), its `return` type and
its `body` (`tests/test_functions.aiml`):

```yaml
- def:
    name: add_two
    args: [[var_a, i32], [var_b, i32]]
    return: i32
    body:
      - return: { +: [var_a, var_b] }

- decl: [res, i32, { call: [add_two, 10, 20] }]
```

A `void` function is called as a statement: `- call: [greet]`.

## 6. Models and classes

A `model` is a plain data structure, a `class` has methods
(`tests/test_classgroup_ctor.aiml`):

```yaml
- class:
    name: Widget
    members:
      - [cfg_val, i32]
      - [ready, i32]
    methods:
      - def:
          name: setup
          args: [[cfg, i32]]
          body:
            - set: [<.cfg_val, cfg]
            - set: [<.ready, 1]
      - def: { name: is_ready, return: i32, body: [{ return: <.ready }] }
```

`<.member` is a member of the current object — not `this.member`, which no
resolver understands. Instantiate on the heap with `new` and call through the
handle:

```yaml
- decl: [w, ptr<Widget>, { new: Widget }]
- call: [w.setup, 42]
- decl: [rdy, i32, { call: [w.is_ready] }]
- free: [w]
```

Interfaces, inheritance and overloading are `spec/16`, `spec/19`, `spec/39`.
Class *groups* (`spec/58`) wire several instances together statically and run
their constructors in a defined phase.

## 7. Memory

The heap belongs to you: `new` allocates, `free` releases, `ptr<T>` holds the
handle. `ref<T>` borrows without owning, `shared<T>` is reference-counted, and a
plain `decl: [x, Model]` is a stack value.

Dropping a `ptr<T>` box at the end of its scope is the **default** — `scope`
semantics, which moving the value out suppresses — and `persistent` is the
opt-out (`spec/61` §11.3). `consumed` on a parameter and `producer` on a return
are explicit affirmations of the transfer: they put "moved, not copied" in the
signature. `shared<T>` is opt-in co-ownership. None of it replaces `new`/`free`
— the heap stays yours. The model is `spec/61_memory_management.md`; the leak
gates under `tests/lifetime/` and `tests/_mem_sanitize/` are its acceptance
tests.

Wiring a member of a class group to a base instance requires `ref<T>`; `ptr<T>`
is rejected there, because the group frees its instances itself.

## 8. Errors

`try` takes a `body` and typed `catch` clauses
(`tests/test_builtin_throw_after_list_alloc.aiml`):

```yaml
- try:
    body:
      - decl: [got, i32, { at: [items, last] }]
    catch:
      - type: IndexOutOfBoundsException
        var: err
        body:
          - call: [log.info, "caught: ${err.message}"]
```

See `spec/22_exceptions.md`. Compile-time diagnostics name the file, the line
and the spec rule they enforce; a rejected program is a *finding*, not a
formality — `AIML_LINT_GLOBALS=0` and the `static_off` switches
(`spec/49_static_off.md`) exist, but switching a check off is a decision, not a
fix.

## 9. Output and logging

`log` (`spec/46_log.md`) and `io.write` are the way forward:

```yaml
- call: [log.info, "sorted ${count} entries"]
- call: [io.write, io.stdout, "done\n"]
```

`print` still exists and appears throughout the older tests, but it is on its
way out. Do not use it in new code.

## 10. Modules

`import` pulls in another file; a file sees only its own imports
(`spec/21_namespaces.md` §2.1), so every file imports what it uses
(`tests/test_lang_basics.aiml`):

```yaml
- import: "tests/_cross/check.aiml"
```

Namespaced types must be written out in full inside generic types —
`list<x.y.T>`, not `list<T>` — otherwise the type is reported as unknown.
Module-level globals are linted: state belongs to a class or a class group.

## 11. Ahead of time, and the web

```bash
out/aiml/aiml prog.aiml -o prog.o          # object file + prog.o.deps
out/aiml/aiml prog.aiml --check            # compile only, no run
out/aiml/aiml prog.aiml -- arg1 arg2       # pass argv to the program
```

Linking an object file is shown in [BUILDING.md](BUILDING.md) §5; the `wasm32`
target is §6 there.

## 12. Where to look next

* [spec/aiml_reference_guide.md](spec/aiml_reference_guide.md) — types,
  operators, built-in methods, namespaces, in one file.
* [spec/00_reference_guide.md](spec/00_reference_guide.md) — index of all 115
  chapters.
* [tests/](tests/) — 1238 programs. For "how do I …", searching the tests is
  usually faster than reading a chapter: every rule has at least one.
* [README.md](README.md) — what is implemented and what is specified but not
  built yet.
