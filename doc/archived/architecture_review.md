# AIML C++ Architecture Review

## Short conclusion

The AIML C++ implementation has a viable basic framework:

- clearly recognisable layers of parser, AST, compiler and runtime
- good subsystem separation in `src/sys/*`
- an already modularised AST builder layer

The biggest problems today are not a "wrong basic idea", but **architectural layering on top of each other**:

- too much mutable state logic in a central `CompilerContext`
- several dispatch and compiler paths existing side by side
- monoliths that are too large in the key files
- inconsistent semantics between JIT/AOT and library/non-library
- guard rails against invalid IR that are too weak

The code looks like a system that has grown successfully, but carries several evolutionary stages at the same time.

## What is good today

### 1. The rough layering is right

The core split makes sense:

- frontend driver in `compiler.cpp`
- AST parsing in `src/core/ast/*`
- AST-to-LLVM codegen in `src/core/ast_compiler*.cpp`
- builtin/method wiring in `src/core/builtin_registry.cpp`
- runtime functionality in `src/sys/*`

That is conceptually much better than a completely mixed parser/runtime blob.

### 2. The AST builder layer is already relatively modular

Notably positive:

- `src/core/ast_builder.h`
- `src/core/ast/*_builder.cpp`

Here the language is split into responsible builders:

- `function_builder`
- `expression_builder`
- `class_builder`
- `model_builder`
- `extern_builder`
- `control_flow_builder`

That is one of the stronger parts of the architecture and a good model for the rest of the system.

### 3. The runtime is sensibly split by domain

Despite partly large files, `src/sys/*` is separated more cleanly in content than the compiler core:

- DB in `db_runtime.cpp`
- mirror in `mirror_runtime.cpp`
- network in `net_runtime.cpp`, `ws_runtime.cpp`, `tls_runtime.cpp`
- rendering/UI/engine in `engine_runtime.cpp`, `ui_runtime.cpp`, `prefab_runtime.cpp`
- serialisation in `ser_runtime.cpp`

That is a good basis for later stabilisation.

## Main problems

### 1. `CompilerContext` is a god object

File:
- `src/core/context.h`

The `CompilerContext` simultaneously carries:

- LLVM context and builder
- symbol tables
- functions, classes, enums, namespaces
- import and header-only state
- hot reload state
- bundle/schema state
- deserialisation state
- DB open options
- exception state
- runtime/builtin dispatch metadata

That leads to three problems:

- features couple implicitly via ambient state
- errors are hard to isolate
- JIT/AOT/libraryMode/headerOnlyMode influence each other indirectly

One symptom of this is e.g. `dbOpenPersist`, `dbOpenColumnLayout` and `dbOpenBasePathVal` as global context flags for a single call path.

### 2. `builtin_registry.cpp` is too large and too central

File:
- `src/core/builtin_registry.cpp`

Size:
- approx. `7250` lines

This file is currently:

- builtin catalogue
- namespace dispatch
- class/method registration
- type conversion logic
- a collection of special cases for runtime APIs

That is one of the biggest structural risk factors in the project.

Consequences:

- prone to breakage on change
- hard to test
- hard to navigate
- high probability of inconsistent special cases

### 3. There are several dispatch systems stacked on top of each other

At least these paths exist:

- `registerBuiltins(ctx)` in `src/core/builtin_registry.cpp`
- `SystemRegistry` in `src/sys/registry.h`
- `CallDispatcher` in `src/core/call_dispatcher.cpp`
- plus legacy remnants such as `src/core/expression.cpp`

That is architecturally unhealthy.

Particularly striking:

- `ast_compiler.cpp` uses the new builtin registry path
- `call_dispatcher.cpp` still uses `SystemRegistry`
- `expression.cpp` is an old YAML expression path

That means:

- several "truths" for call semantics
- harder feature extension
- higher danger that a fix lands in only one path

### 4. `compiler.cpp` mixes too many roles

File:
- `compiler.cpp`

Size:
- approx. `1826` lines

This file simultaneously does:

- loading YAML files
- initialising the CompilerContext
- import resolution
- header-only propagation
- deferred import compilation
- JIT setup
- AOT object generation
- entry point generation
- linking
- hot reload chunk compilation
- runtime symbol registration for the JIT

That is effectively an orchestrator, linker glue, JIT launcher and compiler frontend in one file.

That leads directly to the already visible JIT/AOT differences.

### 5. The code carries the old and the new IR abstraction model at the same time

In `Symbol` and more broadly in the compiler one sees:

- raw LLVM pointers: `alloca_inst`, `type`, `element_type`
- new wrappers: `ir_alloca_inst`, `ir_type`, `ir_element_type`

File:
- `src/core/context.h`

That is a classic migration state, but expensive in the long run:

- duplicate invariants
- duplicate sources of error
- code that is harder to read

The same can be seen in the compiler flow:

- some paths work directly with LLVM
- others via `IRGenerator`
- yet others mix both in the same block

### 6. Top-level semantics are not cleanly unified

A concrete architectural problem that is already producing real bugs:

- in `libraryMode`, top-level `DeclStmt` are materialised as `GlobalVariable`
- in the normal path, top-level statements are emitted into `__aiml_init`

File:
- `src/core/ast_compiler.cpp`

As a result, the storage semantics of top-level variables diverge.

That is not a small detail bug, but an architectural break:

- the same language construct has different storage semantics depending on the mode
- that has already contributed to the pointer return repro with invalid IR

### 7. Guard rails against invalid IR are too weak

A recurring pattern in the bugs so far:

- AIML does not produce a clear diagnostic result
- instead LLVM fails late or the runtime crashes

Examples:

- the pointer return repro ends in LLVM `LiveVariables`
- mirror bugs tipped over via debug prints or late runtime symptoms

`compileToObject(...)` verifies the module, but the whole JIT/chunk/run path is not architecturally secured equally strongly.

That is dangerous, because especially with an experimental language the compiler should fail aggressively early.

### 8. Many features are wired via string types instead of explicit semantics

Many decisions in the code appear via `typeName` string comparisons:

- `list<...>`
- `map<...>`
- `ptr<...>`
- `buffer<...>`
- `"path"`
- `"ui_style"`

That is normal for a prototype, but scales badly.

Consequences:

- many scattered special cases
- high maintenance effort
- harder refactorings
- more inconsistent corner cases

### 9. Diagnostics and source locations are not yet a real compiler subsystem

Most compiler errors today mainly show:

- the C++ source file
- the C++ line
- the error text

but not the actually affected AIML location.

That is architecturally unfavourable, because the foundation for it already partly exists:

- AST nodes carry `sourceNode`
- `YAML::Mark().line` is already used for `log.*`
- top-level declarations are already linked with YAML nodes during the parse

So the break is not missing raw information, but missing centralisation:

- no official `SourceLoc` abstraction
- no central diagnostic API
- many raw `LOG_ERROR(...)` paths without an AST/context reference

Consequences:

- high debugging costs
- poor error messages for language users
- slower work on exactly the architecture and type errors the system frequently produces today

## Concrete architectural tensions

### A. Good frontend modularisation vs. weak midend/backend cut

The parser is cut better than the compiler.

Comparison:

- parser: many small builders
- compiler: large files such as
  - `ast_compiler.cpp` (`1312` lines)
  - `ast_compiler_call.cpp` (`1735` lines)
  - `builtin_registry.cpp` (`7250` lines)

That indicates that the frontend side has already moved towards a good architecture, but the backend part has not yet followed.

### B. Runtime cleanly separated, but the ABI/dispatch layer is not

The domain runtimes are separated, but the binding to them is central and tightly coupled.

Today a lot depends on

- builtin registration
- context flags
- type names
- method names
- special compiler rules

fitting together exactly.

That makes feature work expensive and regressions likely.

### C. JIT and AOT are not two backends, but two different systems

The architecture suggests "same language, two output modes".

In fact, today they are rather:

- a file/AOT path with the full import and object model
- a reduced string/JIT path

That is one of the biggest systemic drivers of complexity.

## Concrete improvement proposals

### sprint_task_1: split `CompilerContext` into real sub-contexts

Recommended split:

- `SemanticContext`
  - symbol tables
  - functions/classes/enums/namespaces
  - current namespace/scope info

- `CodegenContext`
  - LLVMContext
  - module
  - builder
  - insert point/exception state
  - IRGenerator

- `CompileSession`
  - libraryMode
  - headerOnlyMode
  - currentSourceFile
  - import/object tracking
  - bundle/hot reload state

- `CallContext`
  - short-lived call options such as DB open flags

That makes many implicit side effects disappear.

### sprint_task_2: split the builtin registry into modules

`builtin_registry.cpp` should be broken down into registering sub-modules, e.g.:

- `builtin_registry_core.cpp`
- `builtin_registry_io.cpp`
- `builtin_registry_db.cpp`
- `builtin_registry_net.cpp`
- `builtin_registry_ui.cpp`
- `builtin_registry_mirror.cpp`
- `builtin_registry_math.cpp`

Goal:

- keep a shared registration surface
- but no 7000-line collection file any more

### sprint_task_3: settle on a single official call/builtin dispatch

A decision is needed:

- either `builtin_registry` is the official path
- or `CallDispatcher` stays central

But:

- `SystemRegistry`
- `CallDispatcher`
- `builtin_registry`

should not permanently keep living in parallel.

Recommendation:

- `builtin_registry` as the main path
- mark `SystemRegistry`/`CallDispatcher` as legacy and remove them step by step
- document `expression.cpp` very clearly as legacy or delete it

### sprint_task_4: unify the top-level storage semantics

Top-level `decl` should not depend on the mode.

Recommendation:

- always model top-level storage as real module globals
- `__aiml_init` only for initialisation, not for owning variables

That would:

- align JIT/AOT
- align library/non-library
- reduce pointer/global bugs

### sprint_task_5: force JIT/AOT onto a shared compile core

`compile()` and `compileFromString()` should use the same core:

- the same context setup
- the same import strategy
- the same verification
- the same runtime symbol preparation, as far as possible

Where differences are necessary, they should live explicitly in a small strategy layer, not scattered across `compiler.cpp`.

### sprint_task_6: enforce IR validation before every codegen path

Recommendation:

- always verify the module before JIT finalisation
- likewise for chunk compilation
- optionally, additionally verify functions after `visitFunction`

Goal:

- earlier AIML compiler errors
- no late LLVM pass crashes

### sprint_task_7: dismantle the legacy IR/LLVM dual model

Decision:

- either `IRGenerator` is the official abstraction
- or the compiler works directly with LLVM

But not both in parallel permanently.

In practice:

- reduce `Symbol` step by step to one representation
- build new code paths only via the target abstraction
- mark old fields and remove them afterwards

### sprint_task_8: centralise type system rules

Today many special rules are scattered across the code via string comparisons.

Recommendation:

- a central `TypeSemantics` layer
- with answers there such as:
  - is it a heap type?
  - is it a pointer type?
  - is it trivially copyable?
  - does it need init?
  - does it need a serialize descriptor?

Then `visitDeclStmt`, builtin dispatch and the serializer do not each have to rebuild their own type string logic.

### sprint_task_9: centralise source locations and diagnostics

Recommendation:

- introduce an official `SourceLoc` structure
- the builder should consistently propagate position data onto nodes
- a central compiler API such as `ctx.error(node, msg)` instead of raw `LOG_ERROR(...)`
- switch the most frequent semantic errors first:
  - unknown type
  - undefined property
  - undefined variable
  - wrong argument count/types

Benefit:

- considerably better AIML error messages
- lower debugging costs
- faster work on all other architectural construction sites

## Prioritisation

If only the most important levers are to be implemented, my order would be:

1. split `CompilerContext`
2. unify top-level storage
3. centralise source locations and diagnostics
4. settle on a single official builtin/call dispatch
5. break down `builtin_registry.cpp`
6. normalise JIT/AOT via a shared compile core
7. hard IR verification before every codegen path

## Overall assessment

The architecture is **not broken**, but it is well past the point at which further features can still be placed cheaply on the existing structure.

The code shows two very different levels of maturity:

- frontend/AST building: already relatively clean
- compiler and binding layer: functionally strong, but structurally overloaded

The most important insight is:

The system now needs fewer new individual features and more **disentangling of the central layers**.

If this disentangling succeeds, many of the current error classes will also become rarer:

- JIT/AOT divergences
- global/pointer semantics errors
- builtin special case regressions
- hard-to-explain sync/serializer/call bugs
