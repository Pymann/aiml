# AIML Import and Test Architecture

## Starting point

The current fix for import resolution in the AIML compiler works, but is semantically fuzzy:

- an `import` can be resolved implicitly via `cwd`
- if that fails, it walks upwards relative to the file currently being compiled
- with nested imports, the search behaviour changes dynamically via `currentSourceFile`
- the same `import` syntax is used for production code, transitive dependencies and test-like substitute wiring

That solves acute problems, but makes the system hard to predict:

- the same code can find different files depending on the start directory
- the search strategy is not visible to users in the code
- an import can unintentionally pull in a file of the same name from a parent directory
- integration and test cases need different semantics from a normal production import
- for AI-assisted analysis, refactorings and test generation the behaviour is also hard to model, because the resolution cannot be derived locally from the AIML code itself

## Target picture

The import system should:

- be explicit instead of heuristic
- distinguish between a production dependency and a test substitute
- remain statically traceable
- offer its own controlled mechanisms for unit and integration tests
- solve mocking and stubbing not through file system coincidence, but through language and compiler features

## Assessment of the current fix

Positive:

- existing code keeps running
- 9rooms-like transitive imports work more robustly
- nested imports are no longer resolved only relative to the process `cwd`

Negative:

- the resolution rules are not visible in the AIML code
- the semantics is implicitly stateful (`currentSourceFile`)
- "walk up and probe" can produce surprising hits
- error analysis becomes harder, because it is not clear why exactly a certain file was found
- that is a poor foundation for a clean test stack
- static tools and AI assistants too can only reconstruct import relationships unreliably, because for that they need to know the runtime context, the working directory and implicit compiler state changes

## Additional problem: poor understandability for AI and tools

The current mechanism is not only opaque for humans, but also particularly unfavourable for AI-based development tools.

Problems:

- from a single AIML file it cannot be read unambiguously which file an `import` effectively means
- the resolution depends on hidden context: the current source file, the compiler's call location, the existing directory structure and recursive state changes
- for refactoring tools it cannot be decided stably which file is affected by a rename or move
- for AI-generated tests or mocks it is not clear which module actually has to be replaced
- documentation, architecture analysis and dead code detection also become less reliable

Consequence:

- the semantics is not well machine-readable
- it is not cleanly indexable
- it is considerably less suitable for automatic code navigation and AI assistance than an explicit, declarative import system

Especially if AIML is to be used more with AI-assisted workflows, determinism is not only a build topic, but also an understandability and tooling topic.

## Proposal 1: make import kinds explicit

The bare `import` should be reduced to a clear, narrow mechanism. There should be different forms for different cases.

Example:

```yaml
- import:
    path: "shared/mirror_helpers.aiml"
    kind: "relative"
```

Possible `kind` values:

- `relative`: relative to the importing file, without upward search
- `project`: relative to an explicit project root or configured import root
- `package`: later for reusable AIML packages
- `test_double`: only allowed in test builds

Rule:

- no silent fallback to `cwd`
- no automatic walking up the directory tree as the default
- if resolution fails, a hard and well-explained compiler error

## Proposal 2: configure import roots explicitly

When several modules such as `9rooms/client`, `9rooms/shared`, `9rooms/frontend` work together, that should not happen via search heuristics, but via declared roots.

Example in a compiler or project config:

```yaml
import_roots:
  - "."
  - "./shared"
  - "./frontend"
  - "./client"
```

Then:

- `kind: relative` uses only the importing file
- `kind: project` searches only in `import_roots`, in a defined order
- on request, the compiler outputs the exact match path in diagnostic mode

That is transparent and reproducible.

## Proposal 3: test build as a concept of its own

The compiler should know a real test mode instead of treating tests only as normal programs.

Example:

```yaml
- test:
    name: "mirror ws imports"
    mode: "integration"
```

Or via a build flag:

```text
aiml --test tests/test_mirror_ws_9rooms_imports.aiml
```

In test mode, dedicated language features are then allowed:

- `test_import`
- `mock`
- `stub`
- assertions and test fixtures
- controlled override rules

The production compiler thereby stays simpler and stricter.

## Proposal 4: a dedicated test import statement

If a test deliberately wants to pull in a different implementation, that should be clearly recognisable in the code.

Example:

```yaml
- test_import:
    target: "shared/payment.aiml"
    with: "tests/doubles/payment_stub.aiml"
```

Semantics:

- only allowed in test mode
- replaces exactly one target module
- the compiler checks type/signature compatibility
- the build output documents the active replacement

That is considerably better than "a file of the same name somewhere in the search path".

## Proposal 5: mock and stub generation in the compiler

Instead of writing doubles completely by hand, the compiler can generate test doubles from a module or interface.

### 5.1 Stub generation

Goal:

- fixed returns
- no side effects
- simple integration tests

Example:

```yaml
- generate_stub:
    from: "shared/payment.aiml"
    out: "tests/generated/payment_stub.aiml"
```

The compiler could:

- mirror all exported functions
- generate default returns
- take over models/enums
- generate neutral test variants for `extern` or global symbols

### 5.2 Mock generation

Goal:

- record calls
- check expectations
- configure returns specifically

Example:

```yaml
- generate_mock:
    from: "shared/payment.aiml"
    out: "tests/generated/payment_mock.aiml"
```

Additional compiler helpers:

- automatically generated call log structures
- `mock.expect_called`
- `mock.expect_not_called`
- `mock.set_return`

### 5.3 Practical feasibility in the current C++ compiler

Soberly considered, stub generation in the current code is considerably easier than real mock generation.

Relatively easy:

- function signatures are already available in structured form after the parse and are processed in `registerTypeDecl(...)` and `visitFunction(...)`, see [compiler.cpp](../../compiler.cpp#L217) and [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L933)
- types, models and enums are registered beforehand anyway, so the public shape of an imported module can in principle already be derived for a stub module, see [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L466)
- for pure function modules without global mutable state, a generated stub would essentially only be a second AIML AST or directly generated IR with default returns

Medium difficulty:

- imported modules run in `libraryMode` and get their own `__aiml_lib_init_*` functions, which are executed before the top-level code of the main module, see [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L24) and [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L172)
- a stub for a module with top-level initialisation would have to replicate this init semantics compatibly, otherwise the behaviour changes compared to the real module
- for global decl stmts the compiler creates real LLVM globals and enters them into `ctx.symbol_table` immediately, see [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L53) and [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L93)

Difficult:

- symbol binding is currently strongly file- and state-dependent: `resolveImportCallback`, `currentSourceFile`, `libInitFuncNames`, `symbol_table` and sibling injection interlock, see [compiler.cpp](../../compiler.cpp#L145) and [compiler.cpp](../../compiler.cpp#L357)
- a mock that is only meant to replace individual functions does not have a clean module boundary today, but effectively has to hit the same symbol names, globals and initialisation orders as the original
- `extern` globals are deliberately merged via plain `__lib_<name>`, non-`extern` globals via file-name-specific symbols; a generated mock would have to hit exactly these rules, otherwise silent link or runtime deviations arise, see [ast_compiler.cpp](../../src/core/ast_compiler.cpp#L68)
- because a great deal is resolved via `ctx.symbol_table`, partially replacing a module is harder than completely replacing an import

Consequence:

- stub generation for simple, rather functional modules is feasible
- mock generation for modules with globals, top-level code, `extern` state and several sibling imports is rather laborious in the current compiler
- real mock support becomes considerably easier if AIML first gets more explicit module contracts and stricter import semantics

## Proposal 6: linguistic separation between API and implementation

Mocking becomes easier if AIML cleanly separates contract and implementation.

Recommendation:

- explicit module API or interface files
- implementations bind against a contract, not directly against concrete files

Example direction:

```yaml
- import:
    path: "shared/payment.api.aiml"
    kind: "project"
```

And then in the production or test build:

- the real implementation binds `payment.api`
- the test binds `payment.mock`

That is architecturally cleaner than global file replacement.

## Proposal 7: overrides at module level instead of file system level

The compiler or build driver should be able to accept module overrides explicitly.

Example:

```text
aiml --override shared/payment.aiml=tests/doubles/payment_stub.aiml
```

Or in a test file:

```yaml
- test_overrides:
    "shared/payment.aiml": "tests/doubles/payment_stub.aiml"
    "client/api.aiml": "tests/doubles/api_mock.aiml"
```

Advantages:

- fully reproducible
- CI-friendly
- easy to log
- no implicit search path side effects

## Proposal 8: compiler diagnostics for import resolution

Independently of the final syntax, the compiler should get a transparent diagnostic mode.

Example:

```text
aiml --trace-imports client/main.aiml
```

Output:

- which file imports which
- by which rule it was resolved
- which candidates were rejected
- which test overrides or doubles were active

That would be useful immediately, even before a bigger language reform.

## Proposal 9: a test pyramid for AIML

The language probably needs three test levels.

### 9.1 Compiler unit tests

Goal:

- parser
- import resolution
- symbol binding
- type checking
- mock/stub compatibility

Form:

- small AIML files
- expected compiler diagnostics
- IR or symbol table checks

### 9.2 Module tests

Goal:

- a single AIML module against stubs or mocks
- no real infrastructure

Form:

- `test_import` or `--override`
- assertions on returns, model state, call logs

### 9.3 Integration tests

Goal:

- real modules together
- real mirror/ws/db flows

Form:

- as few overrides as possible
- real cross-module interaction
- scenario tests rather than fine-grained mocks

9rooms `mirror sync` clearly belongs on this level.

## Concrete recommendation

I would not keep relying on the current "walk up and probe" mechanism. Instead:

1. Short term:
   introduce `--trace-imports` and hard logging output for resolution decisions.

2. Medium term:
   introduce `import.kind` plus explicit `import_roots`.

3. For tests:
   build `test_import` or `--override` as the official mechanism.

4. Afterwards:
   introduce mock/stub generation based on module APIs or interfaces.

## Minimal migration path

A realistic way without a big bang:

### Phase 1

- keep the current mechanism
- but emit a warning when upward search was used
- implement `--trace-imports`

### Phase 2

- new syntax `import: { path, kind }`
- the old string form continues under deprecation

### Phase 3

- `test_import` and `--override`
- first stub generation for simple function modules

### Phase 4

- API/interface-based mock generation
- remove the old implicit heuristic

## Conclusion

The current fix is understandable as an emergency measure, but too implicit as long-term semantics. For AIML, an explicit module system with its own test overlay would be considerably better than a file-system-based search heuristic.

In my view, the most important design decision is:

- production imports must be deterministic and strict
- test replacements must be explicit and linguistically visible
- the import semantics should be so local and declarative that AI tools, indexers and refactoring tools can understand it without hidden runtime context

Then both 9rooms integration cases and later unit tests become considerably cleaner.
