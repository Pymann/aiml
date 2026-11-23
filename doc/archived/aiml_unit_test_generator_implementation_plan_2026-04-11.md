# AIML Unit Test Generator: Concrete Implementation Plan

This document turns the approach from `aiml_unit_test_implement.md` into an actionable plan.

The goal is **not** a magical fully automatic tool that guesses semantically correct tests. The goal is a
**safe generator for isolated test harnesses** that:

1. analyses an AIML file,
2. stubs external dependencies in a controlled way,
3. generates callable test skeletons,
4. compiles deterministically,
5. and cleanly prepares later manual assertions or golden checks.

## 1. Target picture

CLI:

```bash
./out/aiml -t src/foo/bar.aiml -o tests/test_bar_generated.aiml
```

Expected result:

- all `import` dependencies are cut or explicitly replaced in the generated test artefact
- missing classes/functions are added as **compilable stubs**
- `test_*` skeletons are created for publicly callable `def` blocks and class methods
- a `main` runner calls all generated tests deterministically
- the generator produces **valid AIML output**

Non-goals in the MVP:

- automatic semantically correct assertions
- perfect mocking of side effects
- complete detection of all native runtime contracts
- automatic construction of sensible test data for any arbitrary signature

## 2. Technical decision

Recommendation: **native in the compiler**, not as a separate Python tool.

Rationale:

- The compiler already knows the real symbol system.
- Builtins, namespaces, classes, models and methods already exist in the resolution layer.
- Errors such as "missing symbol vs. builtin" do not have to be rebuilt in a second logic.
- The same AST can be used for analysis, stub injection and export.

Pragmatic rollout:

1. internal compiler pass
2. CLI flag `--generate-tests`
3. export as `.aiml`
4. only afterwards, optionally, a helper script for batch generation

## 3. Architecture

Five new building blocks are introduced.

### 3.1 `TestGenConfig`

A small configuration for the pass:

- input file
- output file
- `strip_imports: bool`
- `generate_method_tests: bool`
- `stub_missing_symbols: bool`
- `emit_todo_asserts: bool`
- `target_namespace_prefix: string`

### 3.2 `SymbolInventory`

Collection of all symbols defined in the source file itself:

- classes
- models
- interfaces
- free `def`
- class methods
- namespaces/FQN names

### 3.3 `MissingSymbolReport`

Result of the analysis pass:

- missing types
- missing free functions
- missing members referenced as methods, as far as reliably detectable
- non-stubbable references as warnings

Important:
Not every unknown method chain may be stubbed blindly. `obj.foo.bar()` cannot be reconstructed
safely without type knowledge. In the MVP such cases must be marked as a **warning**, not
automatically "solved".

### 3.4 `GeneratedTestPlan`

Description of the tests to emit:

- `test_<function>`
- `test_<class>_<method>`
- optionally `smoke_<type>` for construction/init

### 3.5 `TestAstEmitter`

Builds the final file from the original AST plus stubs plus test skeletons.

## 4. Implementation phases

## Phase 1: CLI and export path

Goal:
The tool accepts `--generate-tests`, loads AIML, and writes an unchanged AST back out.

Work packages:

1. add the CLI flag to the existing `main`/argument logic
2. reuse the file loading and parse path
3. pin down the JSON/YAML emitter path for the AST export
4. output to the file given by `-o ...`

Definition of done:

- `./out/aiml --generate-tests file.aiml -o out.aiml` creates a file
- the created file is parseable
- a roundtrip without content changes works for simple test files

Risk:

- the export format must not destroy semantically relevant YAML structures

## Phase 2: symbol inventory

Goal:
The compiler can inventory a file without imports.

Work packages:

1. collect top-level nodes:
   - `class`
   - `model`
   - `interface`
   - `def`
   - `namespace`
2. inventory the methods per class
3. define FQN resolution for the local file
4. query builtin symbols explicitly from the existing compiler context, do not maintain them twice

Definition of done:

- for a given file, the pass can output all locally defined symbols as a report
- builtins are not marked as "missing"

## Phase 3: reference analysis

Goal:
The compiler reliably finds the **really relevant** external references.

Work packages:

1. recursive AST walk over:
   - `decl`
   - `set`
   - `return`
   - `if` / `while` / `try`
   - `call`
   - `new`
   - member/method chains
2. separate the reference classes:
   - type reference
   - function reference
   - constructor reference
   - method chain reference
3. conservative mode:
   - only mark references as stubbable that can be classified safely
   - only warn for ambiguous chains

Definition of done:

- `MissingSymbolReport` reliably distinguishes between:
  - present locally
  - builtin
  - definitely missing
  - unclear/warning

Important delimitation:

The original plan is too optimistic here. "Finding missing symbols" is not just a
simple recursive YAML scan as soon as member chains and methods without complete type information
come into play. In the MVP this pass must therefore be deliberately conservative.

## Phase 4: stub generation

Goal:
Definitely missing references become minimal, compilable stubs.

Work packages:

1. type stubs:
   - a missing `class` with a small dummy member
   - a missing `model` optionally likewise as a dummy struct
2. function stubs:
   - name
   - a generic empty arg list only if the signature is really unknown
   - better: derive the parameter count from the call site
3. return types:
   - `void` only for real statement calls
   - for expression context calls a placeholder type must be chosen
4. stub naming convention:
   - keep the original name
   - optionally a comment/metadata `AUTO-GENERATED STUB`

Definition of done:

- missing simple classes/functions are added in a compilable form
- expression contexts do not produce obvious type breaks

Important point:

The claim from the original document "types can mostly be omitted" is too loose for the
compiler reality. For expression contexts the generator needs a usable
return type, otherwise it only produces other compiler errors.

## Phase 5: import isolation

Goal:
Generated test files are deliberately isolated.

Work packages:

1. remove all top-level imports by default
2. an optional `--keep-imports` mode for later integration tests
3. write the original file path as a comment/metadata into the test file
4. generate a warning block if cut imports could not be stubbed completely

Definition of done:

- in the default mode the generated file contains no productive imports
- instead, the compiler only reports real generator gaps

## Phase 6: test skeleton generation

Goal:
All locally testable targets get a consistent `test_*` scaffold.

Work packages:

1. free functions:
   - `test_<name>`
2. class methods:
   - `test_<class>_<method>`
3. test body MVP:
   - start print
   - TODO print or comment
   - optionally a minimal smoke call
4. skip problematic targets:
   - private methods
   - generated/native bindings without safe callability

Definition of done:

- the generated file contains a testable skeleton for every suitable candidate
- name collisions are avoided

Recommendation:

In the MVP, first only **smoke skeletons**, no auto assertions yet. A half-correct assert generator
is more dangerous than an honest test stub.

## Phase 7: `main` runner and result aggregation

Goal:
The generated file is directly executable.

Work packages:

1. generate `main` if not present
2. call all `test_*` in a stable order
3. a simple exit code:
   - `0` if all test functions run through
   - `1` if the generator itself wants to mark a fatal TODO block
4. optionally later:
   - `passed/failed/skipped` counters

Definition of done:

- generated tests can be compiled and started directly

## Phase 8: validation and compiler regression tests

Goal:
The generator itself gets its own tests.

Mandatory tests:

1. a simple free function without dependencies
2. a file with imports
3. a file with a missing class
4. a file with a missing free function
5. a file with an expression call that needs non-`void`
6. a file with class methods
7. a file with `try/catch`
8. a file with a member chain that only ends as a warning
9. a golden output test for stable emission

Definition of done:

- the generator path itself is regression-testable
- the output is stable enough for golden comparisons

## 5. Concrete implementation places in the compiler

Probable intervention points:

- the CLI/argument parser in the compiler `main`
- reuse the AST parse path
- read the symbol resolution/builtin registry, do not copy it
- a new pass, e.g. `TestGeneratorPass`
- AST export via the existing JSON/YAML writer

Sensible new files:

- `src/core/test_generator_pass.h`
- `src/core/test_generator_pass.cpp`
- `src/core/test_generator_types.h`
- `tests/test_generate_unit_tests_*.aiml`

## 6. Order for the real implementation

Recommended order:

1. CLI + AST export
2. symbol inventory
3. MissingSymbolReport
4. simple type/function stubs
5. test skeleton generator
6. `main` runner
7. golden tests
8. only afterwards more demanding expression/method chain cases

This is important so that the generator becomes usable early without wanting to become "smart" too early.

## 7. Safety gain for development

The generator improves development safety on three levels:

1. a fast isolated compile test of individual AIML building blocks
2. earlier visibility of API/type interface problems
3. standardised test harnesses instead of ad-hoc test files

The actual safety gain, however, does **not** come from automatically invented assertions,
but from:

- reproducible isolation
- consistent stubs
- a stable test runtime entry point
- and later targeted manual assertions

## 8. Deliberate non-goals in the MVP

These points should explicitly **not** be solved in the first cut:

- automatic semantic test case generation
- intelligent mock returns from source code interpretation
- complete inference of all method signatures
- coverage of private/internally encapsulated methods
- snapshot/golden assertions on program output content

## 9. Definition of done for the whole feature

The feature is only done when:

1. `--generate-tests` runs stably for simple AIML files
2. generated files compile without imports
3. missing simple types/functions are stubbed automatically
4. `test_*` skeletons plus `main` are emitted correctly
5. the generator itself has its own regression tests
6. warning cases are reported explicitly as warning cases instead of being silently "solved" wrongly

## 10. Next concrete step

What makes sense right now:

1. build `Phase 1` and `Phase 2`
2. a first debug report `--generate-tests --dump-symbols`
3. then validate on a small file such as `tul/math/matrix.aiml` or a pure core test file

That keeps the first cut small, but reliable.
