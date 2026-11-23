# Spec 61: cascade parameters - findings and implementation plan

As of: 2026-10-01. Mode examined: AIML_MEM_V3=1, AIML_LIFETIME_DROPS=1.
Scope: analysis, new probes and plan; no compiler correction through this investigation.

## Result

The reported inconsistency is real. In the working state updated during the
investigation, cascade is already contained in CompilerContext::isByrefParamType.
That fixes free functions, but is enough neither for all call routes nor for
spec 61 conformant ownership. The compiler was rebuilt with exodus build -j 2;
the results named below come from this build.

1. **P1: parameter rebinding destroys caller ownership.**
   visitFunction registers normal container parameters as a borrow without retain/drop
   (src/core/ast_compiler.cpp:5480, :5543, :5624). The handle set path
   (src/core/ast_compiler_stmt.cpp:2848) releases the old handle anyway.
   rebind.aiml demonstrates UAF/double free and a leak of the new handle:
   24 bytes definite + 16 bytes indirect, 12 Valgrind errors, exit 99.
2. **P1: the parameter does not survive a rebinding of its original owner.**
   owner_rebind.aiml passes holder.row and ref<Holder>, replaces holder.row
   in the callee and uses row afterwards. The old handle was already freed by
   aiml_ser_free_decoded. Valgrind reports a UAF; exit 139.
   The probe initialises the field explicitly and ends the seed scope before the call,
   so that no additional local reference hides the error.
3. **P1: class methods still have a divergent forward declaration.**
   For methods, registerTypeDecl still only checks list/map
   (src/core/ast_compiler.cpp:4109); visitFunction, by contrast, uses the new
   shared helper (:5366). The existing LLVM forward declaration is reused.
   Interface conformance expects a pointer (:4772) and rejects the method.
   Direct methods therefore have to be checked too. buffer is missing at the same place.
4. **P1: fn calls use yet another divergent signature.**
   src/core/ast_compiler_call.cpp:717 builds parameter types directly with getLLVMType
   and passes raw expression values. indirect.aiml fails LLVM verification:
   GenericList as the argument against list_i32 in the signature; in addition, under the new ABI
   the target must receive a handle pointer.

## Reproduction

bash tests/lifetime/cascade_params/run.sh

The runner builds native AOT probes and links against out/libaiml. It also checks
ERROR messages in the build log, because in this investigation the compiler partly
returned exit 0 on LLVM verification errors. Valgrind is mandatory; errors, leaks,
timeouts and non-zero program exits make the runner fail.

| Probe | Checked | Result |
|---|---|---|
| direct.aiml | free function, passing on, growth to 40 elements, content | exit 0; 0 definite/indirect; 0 Valgrind errors |
| dispatch.aiml | direct method and interface method with the same signature | build error: arg row does not match the interface |
| indirect.aiml | fn<cascade<ProbeCode>:i32> | LLVM verification error |
| rebind.aiml | local rebinding of the parameter, the caller stays unchanged | exit 99; UAF/double free; 24/16 bytes lost |
| owner_rebind.aiml | the old parameter stays alive despite the owner being rebound | exit 139; UAF |

Logs of the final run: /tmp/aiml-cascade-params-33EmlC/.
The previous run without indirect is in /tmp/aiml-cascade-params-vFFbEM/.
The probes are deliberately not yet a GREEN gate.

## Target contract

Spec 61, table 2.1: cascade<X> is a class 3 container with a list<i32> layout.
Section 5 requires aliasing through shared handles; sections 8 and 11.2 require
an incref on parameter passing. What is meant is a copied handle to the same
container, no copy of size/capacity/data and no reference to the caller's slot.

- Mutation and reallocation of the container are visible to all aliases.
- set on the parameter only replaces its local handle.
- Every parameter owns a reference of its own for its lifetime.
- Rebinding first secures the new handle, releases the old one and stores the new one.
- Normal exit, early return and exception unwind release exactly this reference.
- Returning or storing may share/transfer the reference correctly.

**Spec contradiction:** section 9.2 still describes RC parameters as borrowed by
default and consumed as retain/co-ownership; section 8 already requires an incref.
In addition the compiler treats consumed across the board as a caller move.
Recommendation: keep section 8 as the default; align 9.2 with it for RC types
and make consumed concrete as a co-ownership contract. ptr stays a move.
Merely weakening section 8 to a borrow does not solve owner_rebind.

## Implementation plan

### 1. Unify the parameter ABI completely

Use the existing helper for method forward declarations as well (:4109).
Free forward declaration, definition, interface conformance, vtable call and fn call
must use the same parameter type mapping. In the fn path also load argument values
as handles; merely changing the LLVM FunctionType is not enough.
Also align container return types in the fn path with the handle ABI.
No global switch of getLLVMType: the layout of the container operations stays necessary.
Use the existing alias/namespace resolution. Assess legacy memV3=0 separately.

Acceptance: direct, dispatch and indirect compile; mutations, contents and
passing on are correct. Add forward references and buffer methods.

### 2. Carry normal container parameters as local RC owners

In the function prologue under memV3, retain the incoming handle and register a matching
pendingParamDrop: cascade uses a list release with an i32 element layout.
Apply the same contract to list/map/buffer; the missing retain is a
shared container gap. Reuse existing drop/descriptor helpers.

Carry parameter status and ownership separately: is_param must not mean
"owns no reference" across the board. Ownership analysis and cleanup must represent local
parameter slots on rebind and on all exits. Existing caller
temp releases must not act as a second release of the same reference.

Acceptance: rebind and owner_rebind without memory errors and without definite/indirect leaks.
Check control variants for list/map/buffer, two identical arguments, self-assignment,
early return, fallthrough and throw/catch.

### 3. Balance return, escape, consumed and temporary arguments

visitRetStmt (:3764, :3996) currently treats parameters as a borrow and increfs
returns. After step 2 either move the parameter owner into the result and
exempt the drop, or retain the result and drop the parameter normally. Do not
combine an incref with a drop exemption: that loses a reference.

set/push/member store must secure the required reference for RC sources;
passing on must not consume a foreign caller reference. Align consumed behaviour with the
cleaned-up contract for free, direct method and interface calls
(ast_compiler_call.cpp:526, :874, :3363).
Owned producer temps, nested calls and interface/fn temps must be released exactly once after the
call/statement or on unwind.

Acceptance: return a parameter, ignore/bind/pass on the result; store a parameter
in a field/container and use it after the caller's scope; consumed with an alias,
a fresh temp, an unused parameter and an exception. Content and leak tests for each.

### 4. Document cascade type rules and the spec consistently

Bring sections 8/9.2 as well as comments on is_param/is_byref_arg up to the new contract.
isRefContainerType (context.h:424) so far does not cover ref<cascade<...>>.
List the ban explicitly in the spec in line with the class 3 classification
and secure it with negative probes for parameter, return, field and local.

Separate side finding: at on cascade is currently rejected (ast_compiler_expr.cpp);
the new probes therefore use the supported .get. Whether at/idx are to count as a cascade
API is to be clarified separately from the parameter ABI. Likewise, a cascade model field
not explicitly initialised was observed as null in the first probe;
initialisation is to be investigated separately, it is no proof about parameter ownership.

### 5. Acceptance and gate

Only take the probes into run_lifetime_leak_gates.sh after green semantic assertions and zero
Valgrind errors. Limit: 0 definite + 0 indirect bytes.
Let the existing 97_cascade_model_carrier_render, 132_interface_returns,
141_buffer_handle_set_and_param and 142_cascade_param_handle run along.
After that run the lifetime/sanitizer suites and the existing cascade/interface suites.
Verify JIT as well as WASM separately for the changed direct/indirect ABI.

The investigation itself ran native AOT + Valgrind. JIT, WASM and the
whole test suite were not run; this plan is not an overall release
of the implementation of spec 61.


## Implementation state (2026-10-01, night)

- **Step 1 done:** `CompilerContext::isByrefParamType` (list/map/buffer/cascade) at all
  signature sites (definition, free and method forward declaration, interface check, vtable call);
  the `fn<…>` call uses the same mapping and passes container locals as handles.
- **Step 2 done:** container parameters hold a reference of their own (prologue incref + drop via
  `makeHandleDrop`), `consumed` takes over the caller's reference without an incref. `set` onto/from parameters
  shares the handle. spec/61 §8 made more precise (no real contradiction to §9.2: "borrowed for the call").
- **Step 3 done:** returning a parameter moves its reference (no incref + drop exemption).
- **Step 4 done:** `ref<cascade<…>>` is a compile error (neg/n14). Side finding fixed:
  cascade model fields did not count as heap (`serHasHeap`) → the model drop never freed them (test 145).
- **Step 5:** all probes in `tests/run_lifetime_leak_gates.sh` (GREEN), plus 142/144/145.
- Unwind (spec/61 §9.3, new) implemented along with it: exception paths release locals/parameters, catch owns the object.
- Open (separately): `at` on cascade (API question), special JIT/WASM verification of the indirect ABI.
