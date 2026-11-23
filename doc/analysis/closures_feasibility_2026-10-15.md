# Feasibility of closures in AIML

As of: 15.10.2026. Source-based architecture review; no implementation, no closure prototype and no new compiler test run. The effort is a rough technical estimate, not a measured implementation speed. Assumption: a person familiar with AIML, focused work, no major additional compiler bugs.

> **Addendum 15.10.2026:** Written up as a spec and acceptance tests: `spec/64_closures.md`
> (PROPOSAL) and `tests/closures/` (5 positive + 16 neg probes; written, not yet
> compiled). Three decisions deviate from the text below:
> **(1)** signatures are declared **by name** (`- callable: { name: Each, args: … }` →
> `block<Each>` / `closure<Each>`) instead of inline `<param:return>`; **(2)** there are **two**
> kinds — `block<Sig>` (non-escaping, implicit captures by reference, non-owning,
> zero allocation) and `closure<Sig>` (escaping, explicit captures, owning move-only);
> **(3)** `fn<…>` stays completely unchanged. Point (2) makes the `engine.run` case the
> easiest instead of the hardest: a `block` reaches the app state in the calling frame,
> without a global, without a heap environment, without an ownership transfer. Implementation is pending.

## Result

Closures are feasible. A restricted prototype is of medium difficulty; a safe, general language feature is demanding. The biggest work lies in ownership, lifetime and callback integration, not in the YAML parser or the indirect LLVM call.

AIML already has bare function pointers and interface calls with object context. That provides a considerable part of the technical basis. What is missing is linking lexically captured values to a callable, correctly managed object.

## What exists today

| Area | Finding | Relevant location |
|---|---|---|
| Function values | `fn<...>` is a single opaque code pointer, not a pair of code and environment | `src/core/context.h:1277`, `spec/12_1_function_pointers.md` |
| Function reference | A function name is converted into a pointer | `src/core/ast_compiler_expr.cpp:423` |
| Indirect call | The signature is reconstructed from the fn type; the call goes via `callOrInvoke`, including the existing argument cleanup logic | `src/core/ast_compiler_call.cpp:1035` |
| Object context | Interface dispatch uses `{obj, vtable}` and a hidden this parameter | `src/core/ast_compiler_call.cpp:1158` |
| Syntax/AST | The AST and expression/block builders reviewed have no dedicated closure expression or capture node | `src/core/ast_nodes.h`, `src/core/ast/expression_builder.h`, `src/core/ast/block_builder.cpp` |
| Memory rules | According to the spec, fn is a trivially copyable value without teardown; ptr/shared/ref and cleanup paths exist for other types | `spec/61_memory_management.md:156`, `src/core/borrow_check.cpp` |
| Engine callbacks | Fixed native signatures `void(float)` and `void()`; the builtin passes three pointers | `src/sys/engine_runtime.cpp:491`, `src/core/builtin_registry.cpp:8311` |
| Browser path | The active engine.run path uses a manual Asyncify loop | `src/sys/engine_runtime.cpp:503` |

Caution: the engine spec mentions set_main_loop_arg as a proposal; the active implementation already uses the manual Asyncify loop. A change must follow the code and preserve this callback/suspension semantics.

## Why fn should not simply get an environment

An existing fn value is only a code pointer and, per the memory model, copyable without teardown. A closure, by contrast, needs code, an environment and rules for freeing the environment. A silent switch of fn to two pointers would change layout, native callback ABIs, containers, parameters, returns and ownership.

Recommendation: keep fn compatible; introduce a separate callable closure type. `closure<...>` is only a working name here, not existing syntax. Represent it either as a managed object with call/destructor functions or as a fat pointer with an explicit owner of the environment. At the boundary to C an adapter and a context parameter are always needed. A cast to a bare function pointer does not solve that, in particular not for WASM.

## Recommended first scope

- An explicit capture list; no implicit capturing of all visible variables.
- Values are copied or explicitly moved according to the existing type rules. A value capture is not automatically a deep copy of all contained handles.
- First an owning, movable closure with a unique owner. Calls only borrow the closure. Shared ownership only after clear semantics.
- ptr capture only by move; do not create a second owning instance of the same ptr.
- No escaping ref captures in the first scope: return and storage must not create a reference to a stack frame that has already been left.
- No implicit transfer across threads, mirror, KV or sys.bundle. Reject these uses with a diagnostic for now.
- Explicitly reject unsupported combinations instead of letting them through as opaque pointers.

A generator can create an internal environment class and a callable function per closure. The function gets the environment as a hidden parameter; capture accesses are rewritten to its fields. The existing interface dispatch is a template for this, but not a finished closure implementation.

## Necessary work packages

1. Define the semantics: capture modes, mutation, copy/move, nested closures, lifetime, null call, exceptions and supported storage locations.
2. Extend AST/parser: a closure expression with typed arguments, return type, capture list and body. Preserve source positions.
3. Extend name resolution: separate parameters, local names and captures; handle shadowing and nested scopes correctly. With explicit captures, undeclared free variables must be diagnosed.
4. Closure conversion: create the internal environment and the callable function; evaluate capture initialisation exactly once; generate types and symbol names deterministically.
5. Codegen/ABI: pass the environment on indirect calls; reuse the existing parameter/return ABI and exception handling. Native and WASM signatures must match exactly.
6. Ownership/cleanup: implement move, scope end, overwrite, return, container storage and exception unwinding for the environment and captures. Free partially initialised environments on error paths.
7. Runtime APIs: first make engine.run closure-capable, including ownership during the whole native/Asyncify loop and release at the end. Inventory further callback APIs individually; they do not become closure-capable automatically.
8. Integration: update AST walkers, static analysis, type inference, import/cache keys and debug/source information. Indirect calls are already a limit of some static effect checks; that must not be presented as a complete safety check.

## Difficult edge cases

- A closure outlives the function in which it was created: captures must not just point to its stack slots.
- Several closures are meant to see the same mutable variable: that needs a jointly owned box instead of independent copies. Do not include automatically in the first scope.
- Captures contain strings, containers, models or owning pointers: release and move must be correct even on throw.
- A closure holds the object that stores it: shared ownership can create cycles. Reference counting alone does not solve them.
- Switching threads requires additional synchronisation/send rules; do not bypass the existing shared rules.
- C callbacks without a user-data parameter cannot carry arbitrary closure state without additional registration/adapters. Generated native trampolines would not be a viable general WASM route.
- Persistent or remotely executed closures need code identity and a serialisable environment, not raw process pointers. That would be a project of its own with sys.bundle.
- Hot reload must take the lifetime of code and environment into account together; no invalid code pointer after a module switch.

## Rough effort ranges

| Goal | Difficulty | Order of magnitude |
|---|---|---|
| Only an explicit context for engine.run, no new language feature | small to medium | about 2–4 working days including native/WASM checks |
| Restricted closure prototype: explicit primitive captures, local/manageable calls | medium | about 1–2 weeks |
| Robust first version with owning captures, clean cleanup, return/storage and engine hookup | high | about 4–8 weeks |
| Additionally general reference captures, shared mutable state, thread switching, hot reload and remote serialisation | very high | a separate multi-week to multi-month project; cannot be estimated more narrowly in a serious way without precise semantics |

The ranges are not additive and depend strongly on the chosen scope and on the state of the ownership/WASM paths. An early vertical prototype should verify the estimate.

## Tests as acceptance criteria

- Two instances of the same closure with different captures stay independent.
- Return from a factory: the capture stays valid after the factory ends.
- Strings, containers and models are freed exactly once at scope end, overwrite and throw.
- ptr move into a capture: subsequent use of the original owner is rejected.
- An escaping stack reference is rejected.
- Signature errors and a null call yield defined diagnostics instead of a crash.
- Nested scopes and shadowing access the right value.
- Parameters and returns for containers/models follow the existing ABI exactly.
- Native and WASM execution of the same example give the same results.
- The engine callback can use its app state without an app global; browser suspension and termination do not free it prematurely.
- Existing fn and lifetime tests stay successful.

## Recommendation for cosmos

If the goal is only the last g_cosmos_app, the explicit engine context is the smallest intervention. The corresponding spec records an earlier decision "do not implement now"; this review therefore implements nothing.

If closures are wanted as a language feature, a separate, limited design is worthwhile: keep fn, explicit captures, an owning environment, no escaping references. First build a vertical prototype through parser → capture → call → release → WASM; after that the engine integration and more general capture types. That way the hardest part is checked early, before the syntax is extended to all runtime APIs.
