# AIML

AIML is a statically typed, compiled, general-purpose programming language whose
surface syntax is **YAML**. A program is a valid YAML document: the compiler is
handed a tree, not a token stream. It compiles through LLVM 18 — JIT by default,
ahead-of-time to object files on request — and also targets `wasm32`.

> **Name.** This is not AIML, the *Artificial Intelligence Markup Language*
> (2001, ALICE/Pandorabots). The collision is known and unresolved.

```yaml
- call: [io.write, io.stdout, "Hello from io.write!\n"]
- decl: [name, string]
- set: [name, "User"]
- decl: [age, i32]
- set: [age, 25]
- print: ["Hello ${name}, you are ${age}."]
- call: [sys.exit, 42]
```

That is [tests/test.aiml](tests/test.aiml), verbatim. Run it:

```bash
out/aiml/aiml tests/test.aiml     # JIT-compiles and runs; exit code is the program's
```

(`print` is legacy and on its way out; new code uses `log` and `io.write`.)

## The shape of the language

Three rules carry most of the syntax:

* **A statement is a list item.** `- decl:`, `- set:`, `- if:`, `- while:`,
  `- call:`, `- return:`, `- def:`, `- class:`.
* **An expression is a mapping**, and the operator is the key:
  `{ +: [a, b] }`, `{ at: [list, i] }`, `{ call: [obj.method, arg] }`,
  `{ new: Widget }`. Logical operators take exactly two operands; longer chains
  nest explicitly.
* **Types are written as strings**: `i32`, `f64`, `string`, `ptr<Node>`,
  `list<string>`, `map<string:i32>`, `array<f32:4:4>`.

```yaml
- decl: [arr, "array<i32:3>"]
- idx: [arr, 0, 10]
- idx: [arr, 1, 20]
- idx: [arr, 2, { +: [{ at: [arr, 0] }, { at: [arr, 1] }] }]
```

Declarations, classes and functions are mappings with named fields, so a
definition reads as data:

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
```

(`<.member` is a member of the current object.) Both excerpts are from the test
suite: [tests/test_collections.aiml](tests/test_collections.aiml),
[tests/test_classgroup_ctor.aiml](tests/test_classgroup_ctor.aiml).

## Why YAML

* **A program is data.** Anything that can read, write, diff or generate YAML
  can do the same to AIML source — including programs, and including the
  language's own `yaml` namespace.
* **The structure is given, not inferred.** There is no expression-parsing
  ambiguity to resolve and no precedence to memorise: nesting is written down.
  `spec/04_1_precedence.md` exists for the few places where it still matters.
* **Diagnostics have a location.** Every node has a file, line and key path, so
  errors name the key they came from.

The cost is verbosity, and the language does not hide it.

## Where the project stands

Last full suite run (2026-11-07): **1052 entries, 866 passed, 8 failed, 178
skipped.**

* **7 of the 8 failures are intentional.** Spec and tests are written before the
  implementation, so the acceptance tests for unimplemented chapters are red on
  purpose: `spec/62` pipelines and `spec/64` closures are `[PROPOSAL]`, and
  `sys.bundle` (`spec/29_1`) has no handler yet.
* The 8th (`tests/venv/kontor_stub.aiml`, 600 s timeout) is a real open failure.
* Of the 178 skips, 103 are files run by their own `run.sh` driver (the driver
  is counted instead); 35 need an interactive display, 4 a network peer, 2 a
  wasm toolchain, 1 is manual.

Implemented and covered by passing tests: the type system above, models,
classes, interfaces, inheritance, namespaces, exceptions, overloading, function
pointers, enums, iterators, decorators, cascades (`spec/57`), class groups
(`spec/58`), tensors (`spec/50`), threads and channels, hot reload (`spec/30`),
persistence via AionDB — `Kv`, `Store`, `VecStore`, `TimeSeries`, indexed fields
and heap serialization (`spec/29`, `31`, `45`) —, HTTP/HTTPS, WebSockets, TLS,
SSH, LDAP and SAML 2.0 (`spec/32`, `33`, `67`), an OpenGL renderer with glTF
loading, UI widgets, prefabs and audio (`spec/43`, `44`), static analysis
(`spec/49`), tracing (`spec/48`), and a project-local toolchain (`aiml venv`,
`spec/68`).

Specified but **not** implemented: blocks and closures (`spec/64`), pipelines
(`spec/62`), services (`spec/66`), `sys.bundle` (`spec/29_1`), generated
unit-test stubs (`spec/59`), symbol clearance (`spec/65`). The memory model
(`spec/61`) is implemented and is the basis of the leak gates, but the chapter
itself is still marked DRAFT.

Verified on Ubuntu 24.04 (x86-64). macOS and Windows are untested; `wasm32` is a
target, not a host. There is no CI yet.

## Getting started

* [GETTING_STARTED.md](GETTING_STARTED.md) — the first program, the parts of the
  language in the order you meet them, how to run things.
* [BUILDING.md](BUILDING.md) — building the compiler from source, the pinned
  dependency set, the test suite, the container recipe.
* [spec/00_reference_guide.md](spec/00_reference_guide.md) — index of the 115
  specification chapters. The spec is normative: where it and the implementation
  disagree, the spec wins.
* [spec/aiml_reference_guide.md](spec/aiml_reference_guide.md) — the compact
  cross-cutting reference (types, operators, built-in methods, namespaces).

## Repository layout

| Path | What is in it |
| --- | --- |
| `src/`, `main.cpp`, `compiler.cpp` | the compiler, JIT and runtime (C++17, LLVM 18) |
| `spec/` | the normative specification, 115 chapters |
| `tests/` | 1238 test programs plus their drivers |
| `performance/` | standalone benchmarks (see the caveats in its README) |
| `doc/` | internal working material: plans, audits, analyses — not user documentation |
| `zimmod/`, `tools/` | optional module and helper tools |
| `exodus*.json` | build configurations, see [BUILDING.md](BUILDING.md) |

## License

AIML is MIT licensed — [LICENSE](LICENSE). Vendored third-party code and the
licenses of the libraries it links against are listed in
[THIRD_PARTY.md](THIRD_PARTY.md).
