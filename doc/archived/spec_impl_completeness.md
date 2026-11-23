# AIML spec vs implementation

## Short conclusion

The AIML implementation covers a usable core of the language, but is not complete against the current spec. The biggest point is not only missing implementation, but also that according to [SPEC_REVIEW.md](../../spec/SPEC_REVIEW.md) the spec itself still has 71 open inconsistencies, gaps or contradictions.

The gap therefore has to be looked at on two levels:

- the spec is not yet standardised in several places
- the implementation covers some specified convenience and strictness rules only partly or not at all

## What already looks like a reliable core

- Basic syntax for `def`, `call`, `decl`, `set`, control flow and basic types
- Models as the data layout and the basis for serialisation
- The basic mirror path with registered objects, deltas and RPC queues
- Persistence and runtime builtins in a productively usable but inconsistent state

## Biggest gaps against the spec

### 1. Function calls and call semantics

The spec in [07_functions.md](../../spec/07_functions.md) promises more than the current compiler reliably secures:

- default arguments and named trailing overrides are specified
- `map<string:any>` as input for function signatures with validation and auto-unboxing is specified
- clear `TypeMismatchException` semantics are specified

According to the analysis so far, the current call path is only partly strict here:

- wrong parameter types do not generally lead to a clean frontend error
- casts and type adjustments partly happen implicitly
- the difference between user functions and builtin special cases is not cleanly standardised

Affected places:

- [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp)
- [call_dispatcher.cpp](../../src/core/call_dispatcher.cpp)
- [ir_generator.cpp](../../src/core/ir_generator.cpp)

### 2. Models, structural typing and deep member access

The spec in [15_models.md](../../spec/15_models.md) is considerably richer than today's robust everyday scope:

- assignment from maps with runtime validation
- structural typing for model parameters
- `ptr<Model>` with auto-dot dereferencing
- deep access to nested fields

The known practical gap is above all the access path:

- deep member chains like `a.b.c` are not consistent for read/write/lvalue
- this produces many helper variables and whole-object reassigns

Affected places:

- [member_chain_plan.md](../../member_chain_plan.md)
- [ast_compiler_expr.cpp](../../src/core/ast_compiler_expr.cpp)
- [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp)
- [utils.cpp](../../src/core/utils.cpp)

### 3. Mirror spec vs runtime behaviour

The mirror spec in [44_5_mirror.md](../../spec/44_5_mirror.md) describes a relatively smooth transport-agnostic API.

The runtime is functional, but not fully at that level:

- `session.update()` semantics were not as robust in the WS path as the spec suggests
- queue/poll/wait ergonomics lead to too much boilerplate on the AIML side
- some real mirror cases first had to be secured through repros and reports

Affected places:

- [mirror_ws_crash_report.md](../../mirror_ws_crash_report.md)
- [builtin_registry.cpp](../../src/core/builtin_registry.cpp)
- [mirror_runtime.cpp](../../src/sys/mirror_runtime.cpp)

### 4. JIT and AOT behaviour are not equivalent

From the language's point of view, AIML source should have the same semantics as far as possible, whether via the file/AOT path or the string/chunk/JIT path.

The current compiler has known differences here:

- a different compiler context
- different import handling
- different object and link paths
- different initialisation and symbol registration

Affected places:

- [jit_vs_aot.md](../../jit_vs_aot.md)
- [compiler.cpp](../../compiler.cpp)
- [ast_compiler.cpp](../../src/core/ast_compiler.cpp)

### 5. The spec itself is not yet a hard contract

Without standardising the spec, the completeness question can only be answered to a limited extent. The existing review shows open points in:

- type names
- YAML syntax variants
- error semantics
- `any` behaviour
- collection access
- reserved namespaces

Affected place:

- [SPEC_REVIEW.md](../../spec/SPEC_REVIEW.md)

## Priority

If the goal is real closeness to the spec, this order makes the most sense:

1. Normalise and prioritise the spec
2. Harden function calls and type checking
3. Unify model/member access
4. Align mirror behaviour and API ergonomics with the spec
5. Establish JIT/AOT parity

## Result

Measured against the spec, AIML today is best described as "partly implemented, but not consistently conformant". The core works, but the language as a specified whole is not yet closed enough to speak of a high degree of completeness.
