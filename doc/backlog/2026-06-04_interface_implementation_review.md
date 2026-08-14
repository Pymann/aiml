# AIML Interface Implementation Review

Date: 2026-06-04
Status: backlog

## Summary

The current AIML interface implementation is conceptually sound in its `ptr<Interface>` fat-pointer approach, but it has a few correctness and safety gaps that should be addressed before interfaces are treated as fully robust compiler/runtime features.

The most important issues are:
- interface conformance is validated by method name, not by full signature
- missing interface methods do not reliably hard-fail compilation
- some interface upcast paths depend too heavily on expression shape
- interface redefinition / hot-reload behavior appears stale within one compiler context

## Findings

### 1. Signature mismatches are not enforced during interface implementation binding

Severity: high

The spec states that interface methods must match exactly, and that incomplete or invalid implementations should fail compilation.

Relevant references:
- `spec/19_inheritance.md`
- `src/core/ast_compiler.cpp`
- `src/core/ast_compiler_call.cpp`

Observed behavior:
- VTable construction looks up methods by mangled name like `Class_method`
- it does not validate that return type and parameter types match the interface declaration
- interface dispatch later builds a call signature from the interface method and casts the vtable slot to that type

Risk:
- a class can appear to implement an interface while actually exposing an incompatible method signature
- indirect calls can then execute with the wrong LLVM function prototype
- this is undefined behavior and can lead to memory corruption, invalid arguments, or garbage return values

Recommended fix:
- during interface vtable construction, compare the resolved class method signature against the interface method signature
- if they differ, emit a compilation error and abort vtable emission for that implementation
- add regression tests for mismatched return type, arity, and parameter types

### 2. Missing interface methods degrade into null vtable slots instead of hard compile failure

Severity: high

The inheritance spec promises a compilation error when a class does not fully implement an interface.

Observed behavior:
- when an interface method is missing, the compiler logs an error and writes a null slot into the vtable
- the vtable is still emitted
- interface dispatch later performs an indirect call without a null-slot guard

Risk:
- programs that should be rejected at compile time may survive into runtime
- the first virtual call on that slot can crash immediately
- this weakens interface guarantees and makes errors much harder to diagnose

Recommended fix:
- treat any missing required interface method as a hard compilation failure
- avoid emitting a vtable when any required slot is unresolved
- optionally keep a defensive runtime assertion for corrupted or manually-constructed invalid fat pointers

### 3. Upcast support to `ptr<Interface>` is brittle and expression-shape dependent

Severity: medium

Observed behavior:
- special interface-upcast logic in declaration handling only recognizes certain initializer forms, especially `VariableExpr`
- similar narrow handling exists for `map` / `kv` insertion paths

Risk:
- valid polymorphic code may compile or fail depending on whether the concrete value comes from a variable, a call result, or a more complex expression
- this creates non-obvious language behavior and makes interface support feel partial

Recommended fix:
- centralize upcast generation into a reusable helper that operates on typed values, not on AST shape alone
- make interface upcasts work uniformly for variables, call results, temporaries, container insertion, and returns
- add tests covering direct call expressions and temporary values assigned to `ptr<Interface>`

### 4. Interface redefinition behavior looks stale within one compiler context

Severity: medium

Observed behavior:
- interface source text is updated in the context
- interface metadata registration appears guarded so that the first definition wins for method shape data
- later redefinitions may therefore not refresh the effective interface signature set

Risk:
- hot reload or repeated compile flows can operate on stale interface metadata
- compiler behavior can drift away from the visible source of truth

Recommended fix:
- decide whether interface redefinition is forbidden or supported
- if forbidden, fail fast on duplicate definitions
- if supported, rebuild the interface metadata deterministically on redefinition
- add a focused test around repeated interface declarations in one compile/session context

## Architectural Notes

The current representation of `ptr<Interface>` as a fat pointer is a reasonable design choice. The main weaknesses are not the representation itself, but missing compiler invariants around conformance checking and vtable emission.

In short:
- the model is workable
- the compiler invariants are currently too soft
- the runtime is forced to tolerate invalid states that should never be emitted

## Suggested Backlog Tasks

1. Enforce exact interface method signature matching during vtable construction.
2. Turn unresolved interface methods into hard compilation failures.
3. Refactor interface upcast generation into a shared typed-value helper.
4. Clarify and implement duplicate interface declaration semantics.
5. Add regression tests for signature mismatch, missing methods, and non-variable upcasts.

## Suggested Test Coverage

Add targeted compiler tests for:
- wrong return type on interface implementation
- wrong parameter type on interface implementation
- missing required interface method
- assigning call results directly to `ptr<Interface>`
- inserting concrete implementations into containers expecting `ptr<Interface>`
- duplicate interface declaration behavior in one compiler session
