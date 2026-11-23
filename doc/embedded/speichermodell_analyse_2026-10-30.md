# Embedded suitability of the AIML memory model

As of: 2026-10-30. Source analysis of the current local working state, including existing uncommitted changes. No changes to compiler or runtime; no ARM/Cortex-M0 runtime, memory or real-time measurements. The statements describe implemented paths and their assessment, not demonstrated board support.

## Result

The original embedded goal is still reachable for the elementary language core: primitive number/bool types, pure value models and fixed arrays are suitable in principle. Automatic releases do not rule out embedded. What is decisive are hidden allocations, boundable memory and runtime costs, and the runtime required.

The current ENV switches, however, do not yield a secured heap-free profile. For small controllers and strict real-time requirements, an end-to-end bare-metal port and a compiler check that excludes dynamic allocations transitively are missing.

LLVM supports the relevant ARM targets. Code generation alone, however, provides neither startup code nor target libraries, linker configuration or a suitable AIML runtime. Cortex-M0 belongs to the Armv6-M family; ARMv7 denotes other architecture variants and should not be treated as an equivalent target.

## 1. The two environment variables

The correct second name is `AIML_MEM_V3`. No evaluation of `AIML_MEMV3` was found.

In the compiler examined, both switches are effectively **on** by default:

- `AIML_MEM_V3`: `CompilerContext::memV3 = true`; evaluated in the constructor.
- `AIML_LIFETIME_DROPS`: the context field is initially `false`, but `Compiler::compile()` sets it to `true` before code generation and then takes the ENV variable into account.

Values evaluated as **off**: empty string, `0`, `off`, `OFF`, `false`, `FALSE`. Other set values activate the respective switch. Unset therefore means **on** for both in normal compilation.

The variables influence the generated code at compile time. A later change at the start of an already generated program does not switch its memory model. Compilation units that belong together must use consistent settings.

**Documentation contradiction:** `doc/env_variables_overview.md` still says "default OFF" for lifetime drops. Several comments also contain older statements. For this analysis the executable initialisations are authoritative.

Sources: [context.h](../../src/core/context.h), [Compiler::compile](../../src/core/ast_compiler.cpp), [ENV overview](../env_variables_overview.md).

### AIML_MEM_V3=1

Activates the newer representation and ownership paths:

- `list`, `map`, `buffer` and `cascade` are represented by reference-counted handles in the V3 management paths. Assignments can share the same mutable resource.
- Strings are shared by reference counting in the corresponding assignment paths.
- `ptr<T>` stands for unique ownership, `shared<T>` for shared reference-counted ownership and `ref<T>` for non-owning access.
- Additional static checks diagnose recognisable ownership violations, for example use after an ownership transfer or release. This is not a complete memory safety guarantee.
- The YAML statement `free: [p]` is produced as an executable AST node. For a matching `ptr<T>` box, owned fields are taken into account on release.

Reference counting is not a tracing garbage collector. Owning cycles are not collected automatically by it.

### AIML_MEM_V3=0

Selects older representation and management paths. The V3 container handles and V3 ownership checks are dropped. Further settings, in particular `AIML_MODEL_REFCOUNT`, can influence the behaviour additionally.

**This does not remove dynamic allocations:** older container paths also use `malloc`/`realloc` for contents and growth.

Particularly relevant: with V3 switched off, the YAML statement `free: [p]` is discarded in the parser and does nothing. An FFI call of an explicitly declared C function `free` is to be distinguished from that.

Source: [statement_builder.cpp](../../src/core/ast/statement_builder.cpp), [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp), [list_impl.cpp](../../src/core/list_impl.cpp).

### AIML_LIFETIME_DROPS=1

Activates essential automatic release and the associated ownership paths:

- Cleanup of local owned resources at scope end, normally in reverse declaration order.
- Decrementing reference counts; releasing the resource at the last owner.
- Cleanup on return paths, taking the returned ownership into account.
- Tracking and releasing temporary results.
- Associated ownership transfers at call boundaries, e.g. for `consumed` and under V3 for `ptr<T>`.
- Various copy, overwrite and parameter management paths.

`lifetime: persistent` suppresses ordinary local scope drops. Module-global values get no normal function scope drop. Non-owning references are not released as owners. Not every resource type or exit path is to be regarded as completely checked by this list.

### AIML_LIFETIME_DROPS=0

Suppresses essential automatic releases without removing the associated allocations. Local heap resources and temporary results can therefore be left behind. Stack memory is still dismantled according to the normal function/stack rules.

The switch affects more than end-of-block releases: certain copy, overwrite and ownership transfer paths are coupled to it as well. Switching it off can therefore change behaviour and is not a general switch to clean manual memory management.

With V3 still active, its representation, ownership diagnostics and explicit `free:` stay active in principle. Individual operation-related releases also remain.

| MEM_V3 | LIFETIME_DROPS | Classification |
|---|---|---|
| 1 | 1 | Current default: V3 with automatic releases |
| 1 | 0 | V3 with essential gaps in automatic cleanup |
| 0 | 1 | Older memory paths with automatic drops; YAML `free:` has no effect |
| 0 | 0 | Older memory paths without automatic drops; no heap-free profile |

Sources: [ast_compiler_stmt.cpp](../../src/core/ast_compiler_stmt.cpp), [ast_compiler_call.cpp](../../src/core/ast_compiler_call.cpp), [ast_compiler.cpp](../../src/core/ast_compiler.cpp).

## 2. Elementary data types

| Type group | Representation and behaviour | Embedded assessment |
|---|---|---|
| Integer and `bool` | Direct LLVM values, no heap allocation or reference counting of their own | Suitable in principle |
| `f32`, `f64` | Direct LLVM floating-point values, no heap allocation of their own | Suitable, but computing costs and required helper functions depend on the target |
| Pure value model | Fields inline as a struct; a local declaration creates memory and initialises it with zero | Suitable if all fields recursively manage without dynamic resources |
| `array<T:N>` | Fixed size, contiguous elements; local stack memory | Suitable with a matching element type and bounded stack requirements |
| `ptr<T>` | Unique owner; declaring a pointer variable is not itself the allocation of the target object; `{new: T}` uses the heap | Controllable, but not an arbitrary non-owning address type |
| `dyn_array<T>` | Runtime size, heap allocation | Not to be equated with a fixed array |

### Primitives

Primitive number and bool values do not activate dynamic memory management merely by being used. Even with V3 and lifetime drops switched on, no reference counting arises from them.

In this assessment `string` explicitly does not belong to the group of simple number/bool values: dynamically created strings use their own runtime and heap paths.

### Models

A simple value model is **not turned into a reference-counted heap object across the board** by V3. If it contains only primitive values, fixed arrays of suitable elements and further pure value models, its representation stays struct-like.

The condition applies recursively: a contained model with dynamic fields brings their behaviour along. Container fields can trigger allocations already at initialisation. Initialisers and called functions can allocate dynamically regardless of the visible result type.

An owning `ptr<T>` field itself only needs space for a pointer, but can carry responsibility for a separately allocated resource. It is therefore to be distinguished from a completely resource-free value model.

### Fixed arrays

`array<T:N>` has a capacity known at compile time and needs no heap allocation for its own memory. The write access examined computes an element address and stores the value directly.

Limits:

- No automatic range check is generated in the access path examined. Invalid indices can corrupt memory.
- A fixed array does not automatically make the resources of its elements static. Arrays of purely numeric values or pure value models are the clear starting point.
- Large local arrays can overload the stack.
- Arrays with owning pointers need a separate check of their element ownership; the fixed capacity does not guarantee complete automatic element management.

Sources: [type resolution](../../src/core/context.h), [declaration and array write access](../../src/core/ast_compiler_stmt.cpp), [array specification](../../spec/52_arrays.md).

### ptr and non-owning addresses

Under V3 `ptr<T>` means unique ownership, including ownership transfer and possible automatic release. `{new: T}` allocates an object via `calloc`; for matching local owners its release is registered.

An address of stack memory, static memory or a hardware register must not be accidentally treated and freed as an owning heap box. For non-owning access, `ref<T>` or raw address accesses explicitly intended for it are semantically appropriate. Their concrete suitability for MMIO must be checked separately: pointer access alone in particular does not yet guarantee correct volatile/register access semantics.

Sources: [visitNewExpr](../../src/core/ast_compiler_expr.cpp), [scope drop registration](../../src/core/ast_compiler_stmt.cpp).

## 3. Why automatic release does not rule out embedded

Automatic release at scope end is compatible with embedded in principle, similar to C++ destructors. A known release point, however, does not guarantee a constant execution time: the last reference to a large object graph can trigger many further releases.

The essential quantities to check are:

- Maximum RAM and stack usage.
- Boundable runtime of operations and releases.
- Implicit allocations, growth and fragmentation.
- Defined behaviour on memory exhaustion or capacity overflow.
- Platform dependencies of the runtime used.

Static memory areas and fixed capacities are particularly suitable for strict limits. Dynamic memory management is not ruled out for every embedded system; FreeRTOS, for example, explicitly supports static and dynamic allocation.

External classification: [FreeRTOS: Static vs Dynamic Memory Allocation](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/09-Memory-management/03-Static-vs-Dynamic-memory-allocation), [Embedded Rust Book: Collections](https://docs.rust-embedded.org/book/collections/index.html).

## 4. Concrete limits of the current overall state

These findings concern the overall port, not the basic representation of pure value types:

1. **Hidden heap with V3 containers:** already `aiml_list_handle_new()` calls `calloc` for an empty list. Switching off the drops does not prevent this allocation.
2. **Hosted dependencies of the string runtime:** the non-WASM path of `header_readable()` uses `sysconf` and `mincore`. For bare metal a different implementation or a suitably excluded part of the runtime is required.
3. **Direct object output path:** `--target` and `setTargetTriple()` exist. `compileToObject()`, however, sets the target back to the host triple. Other output paths are to be distinguished from this; LLVM target support alone does not fix this AIML gap.
4. **No demonstrated closed heap-free profile:** the two ENV switches do not guarantee that functions, including their call chains and field initialisations, manage without dynamic allocations.
5. **No Cortex-M0 proof in this analysis:** neither a successful board boot nor ABI, MMIO, interrupt, stack or runtime behaviour were verified here. The far-reaching statements in `spec/41_os_support.md` have therefore not been adopted as proof of implementation.

Sources: [string_runtime.cpp](../../src/sys/string_runtime.cpp), [compiler.cpp](../../compiler.cpp), [OS/bare-metal specification](../../spec/41_os_support.md). Background: [Clang cross-compilation](https://clang.llvm.org/docs/CrossCompilation.html), [Armv6-M/Cortex-M0 target](https://doc.rust-lang.org/rustc/platform-support/thumbv6m-none-eabi.html).

## 5. Conclusion for the target picture

For the delimited core of primitive values, pure value models and fixed arrays, the embedded target picture is preserved. Automatic ownership management does not have to be removed in principle for this, nor does V3 have to be replaced by the older memory paths.

For a robust small embedded profile the following would be sensible next development steps:

1. A compiler profile that recognises dynamic allocations as errors, also transitively through calls, initialisers and fields.
2. Containers and strings with a fixed capacity and defined behaviour on overflow, if more than arrays is needed.
3. Where dynamic memory is allowed, explicitly selectable pools/arenas with documented limits.
4. An end-to-end cross-compile path and a minimal bare-metal runtime.
5. A reproducible proof on a concrete target with RAM/flash, stack and runtime measurements.

These are proposals, not existing guarantees or implemented modes. For a strictly heap-free program the operations called must also be checked; a mere list of its variable types is not sufficient as proof.
