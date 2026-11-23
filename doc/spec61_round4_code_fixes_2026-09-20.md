# Spec 61 – concrete C++ proposals for review round 4

As of: 2026-09-20. **Implementation proposal, not yet an applied or compiled patch.** The excerpts show the changes to be built in; new helper types and functions are marked accordingly.

## Demonstrated errors and acceptance

| Error | Reproduction | Expectation after the fix |
|---|---|---|
| An internal weak reference points into the original after a clone | [probe 33](../tests/lifetime/33_round4_clone_internal_weak.aiml) | `duplicate.view == duplicate.owned`, the original stays independent |
| A tensor field is only copied as a pointer | [probe 34](../tests/lifetime/34_round4_clone_tensor_member.aiml) | Mutating the clone's tensor leaves the original value at 1 |
| A move inside an initialiser is not tracked | [negative probe n12](../tests/lifetime/neg/n12_round4_move_in_initializer.aiml) | compile error `use-after-move` |

Last complete run: 45 successful checks, these three failures. Binary tested: 20.09.2026, 18:07. Before implementation, re-align the build and parallel changes. Only change the affected paths; do not weaken the existing regression tests.

## 1. Shared basis: dedicated descriptors for clone semantics

Affected: [ast_compiler.cpp](../src/core/ast_compiler.cpp), `serBuildSubtree`, `serFieldSubtree`, clone lowering; [ser_runtime.h](../src/sys/ser_runtime.h) and [ser_runtime.cpp](../src/sys/ser_runtime.cpp).

The current descriptor builder treats weak references and tensor handles as `AIML_TD_RAWBYTES`. The clone walker can therefore neither correct references nor clone tensor data. The same builders are also used for serialisation and release. Therefore do not take the new semantics over unchecked into the existing wire descriptors.

### Proposal for the builder interface

```cpp
// NEW: internal to the compiler; not an AIML language feature.
enum class DescriptorPurpose { Existing, Clone };

static std::vector<AimlTypeNode> serBuildSubtree(
    const std::string& typeName, int32_t offset,
    CompilerContext& ctx, const llvm::DataLayout& dl,
    DescriptorPurpose purpose = DescriptorPurpose::Existing);
```

Also pass `purpose` through `serFieldSubtree` and all recursive calls. Existing callers stay at `Existing` for now; **all** memV3 clone entry points, including list/map clone and normal model clone, choose `Clone`.

Add new leaf kinds to `AimlTDKind` without changing existing numbers:

```cpp
// Proposal: currently the enumeration ends at AIML_TD_PTR_REF = 15.
// Align the numbers with the current header before building it in.
AIML_TD_WEAK_REF = 16, // pointer slot; neither owning nor to be followed recursively
AIML_TD_TENSOR   = 17, // owned tensor handle; clone via aiml_tensor_clone
```

In the builder **before** the pointer/builtin/RAWBYTES fallback:

```cpp
if (ctx.memV3 && purpose == DescriptorPurpose::Clone) {
    if (typeName.rfind("ref<", 0) == 0 && typeName.back() == '>') {
        return {{AIML_TD_WEAK_REF, 0, 0, offset, 0, 0}};
    }
    if (typeName == "tensor") {
        return {{AIML_TD_TENSOR, 0, 0, offset, 0, 0}};
    }
}
```

**Necessary integration:**

- Distinguish descriptor caches and LLVM global names by purpose. The same model name must not accidentally deliver the serialisation descriptor for a clone.
- Recursive `AIML_TD_PTR_REF` resolutions must reach the **clone variant** of the referenced type. Provide a separate clone registry path or a registry query extended by the purpose for that. Merely switching the root descriptor is not enough.
- The new kinds are leaves; functions for shifting child indices must not treat their parameters as subtree indices.
- Clone descriptors must not accidentally end up in `enc`/`dec`. Diagnose unknown kinds there explicitly; no silent omission of fields. Existing/OFF descriptors and the wire format are kept.
- Normal scope drop descriptors are a separate contract. Do not use the new clone descriptors as a replacement unchecked. If they serve to release partially created clones, weak ref must be a no-op and tensor a matching resource drop.

## 2. Correct internal weak references after the graph walk

### Cause

Under memV3 `serBuildSubtree` copies all `ref<T>` as raw pointers. That is only correct for **external** weak targets. For internal targets §5.1 requires redirection to the corresponding clone. An immediate memo lookup is not enough: the owned target can appear in the field layout only after the weak reference.

### Concrete runtime proposal

Keep the existing owner memoisation, but extend it by a separate address mapping and pending reference corrections:

```cpp
// NEW in ser_runtime.cpp
struct WeakCloneFixup {
    uint8_t* destinationSlot;
    const void* originalTarget;
};

struct CloneContext {
    // Replacement for the previous g_clone_memo table:
    std::unordered_map<const void*, void*> ownedMemo;

    // Target addresses for weak references, also to values stored inline.
    std::unordered_map<const void*, void*> relocated;
    std::vector<WeakCloneFixup> weakFixups;
};

static thread_local CloneContext* g_clone_ctx = nullptr;

static void finishWeakCloneRefs(CloneContext& context) {
    for (const auto& fixup : context.weakFixups) {
        const auto found = context.relocated.find(fixup.originalTarget);
        const void* target = found != context.relocated.end()
            ? found->second : fixup.originalTarget;
        // No incref: the result stays a weak reference.
        std::memcpy(fixup.destinationSlot, &target, sizeof(target));
    }
}
```

New `clw` branch:

```cpp
case AIML_TD_WEAK_REF: {
    const void* target = nullptr;
    std::memcpy(&target, src, sizeof(target));
    std::memcpy(dst, &target, sizeof(target));
    if (target) g_clone_ctx->weakFixups.push_back({dst, target});
    break;
}
```

Register the mapping for every newly cloned owned heap object **before the recursion**:

```cpp
g_clone_ctx->ownedMemo.emplace(originalObject, clonedObject);
g_clone_ctx->relocated.emplace(originalObject, clonedObject);
// Only after that process its contents with clw(...).
```

This concerns `MODEL`, `PTR_STRUCT`, `PTR_REF` and container handles. Keep the existing rules for unique owners and shared refcounts; in particular, for a unique owner found again do not simply create a second owning pointer.

Additionally record the addresses of copied inline values/fields/elements in `relocated`. Only take actual valid referent addresses for that, do not interpret arbitrary pointer slots across the board as owned objects. A borrow of a value or a field must also be able to point to its new counterpart. `relocated` therefore stays separate from `ownedMemo`.

After completing the whole walk, run `finishWeakCloneRefs(context)` exactly once. Nested runtime calls must use the same context or restore the previous thread-local context via RAII; the previous simple setting to `nullptr` is not enough for reentrancy.

### Important compiler hookup: final target address

For probe 33 the target lies in the heap and stays stable. In general, however, weak references can point to the cloned **root as a value** or its inline fields. The clone must then not bind them to a temporary alloca whose content is copied elsewhere afterwards.

For declaration/assignment targets, preferably clone directly into the final target slot. For temporary return values either use a stable result location until the ownership handoff, or provide an explicit relocation with a renewed reference correction. Check the current final load/store of a model value in clone lowering for this. The runtime alone cannot repair this address change.

### Tests

- Probe 33 turns green; probe 28 for external references stays green.
- An additional case with the weak field **before** the owning field, so that the second phase is really needed.
- A weak reference to the clone root and to an inline field; no reference to a temporary compiler location.
- A null ref stays null; weak edges increase no refcount and are not followed on drop.

## 3. Copy tensor fields via the tensor clone

### Cause

The RAWBYTES fallback copies the tensor address. A `tensor.fill` on the clone therefore changes the original data. For a tensor the matching runtime function already exists: `void* aiml_tensor_clone(void*)` in [tensor_runtime.h](../src/sys/tensor_runtime.h).

### Concrete runtime proposal

Include the header in the clone runtime module and handle the new leaf node:

```cpp
#include "tensor_runtime.h"

// In clw(...):
case AIML_TD_TENSOR: {
    void* originalTensor = nullptr;
    std::memcpy(&originalTensor, src, sizeof(originalTensor));
    void* clonedTensor = originalTensor
        ? aiml_tensor_clone(originalTensor) : nullptr;
    // For a non-null original a failed clone must not be passed on
    // silently as a successful null value.
    // Connect to the defined runtime allocation error path here.
    std::memcpy(dst, &clonedTensor, sizeof(clonedTensor));
    if (originalTensor && clonedTensor) {
        g_clone_ctx->relocated.emplace(originalTensor, clonedTensor);
    }
    break;
}
```

This is an owned resource clone, not a shared incref. Do not hand out the same new tensor handle to several unique owners. A `ref<tensor>` stays a `WEAK_REF` and is not duplicated on this path. Normal model/container recursion must reach the tensor node; do not build in special handling exclusively for top-level `clone(tensor)`.

The existing tensor runtime defines how data, views and metadata are cloned. Reuse this logic instead of rebuilding tensor layouts in the serializer. On failing clones, already created resources must be freed according to the defined error contract, if the runtime does not terminate the process.

**Tests:** probe 34 turns green; probe 14 stays green. Additionally check a null tensor field and a tensor in a nested model. Check the scope drop of the clones under ASan for double frees/UAF; the automatic resource release must not be hidden by manual releases in the test.

## 4. Evaluate the ownership effects of all call expressions

Affected: [borrow_check.cpp](../src/core/borrow_check.cpp), `scanUses`, `exprStmt`, `stmt(DeclStmt)`.

### Cause

`stmt(DeclStmt)` calls `scanUses`. Its `CallExpr` branch only checks the arguments for reads. The actual move logic lies only in the separate `exprStmt` path. Semantically identical calls therefore differ depending on whether their result is stored.

### Concrete extraction of the call effects

Move the already existing transfer rules into a shared helper. Example for the part of free function calls:

```cpp
// NEW: method of BorrowChecker.
void applyFunctionCallMoves(const CallExpr& call, int line) {
    const auto fit = ctx.functions.find(call.functionName);
    if (fit == ctx.functions.end()) return;

    const auto& types = fit->second.argTypeNames;
    const auto& lifetimes = fit->second.argLifetimes;
    for (size_t j = 0; j < call.args.size(); ++j) {
        const bool ptrParam = j < types.size()
            && types[j].rfind("ptr<", 0) == 0;
        const bool consumed = j < lifetimes.size()
            && lifetimes[j] == "consumed";
        if (!ptrParam && !consumed) continue;
        const std::string source = asPtrVar(call.args[j].get());
        if (!source.empty()) markMoved(source, line);
    }
}
```

The existing `CallExpr` branch of `scanUses` becomes:

```cpp
if (auto* call = dynamic_cast<const CallExpr*>(e)) {
    for (const auto& argument : call->args) {
        scanUses(argument.get(), line);
    }
    applyFunctionCallMoves(*call, line);
    return;
}
```

**The helper must not additionally be called in the old `exprStmt` path.** Otherwise the same call would move twice and wrongly report `use-after-move`. Therefore rebuild `exprStmt` so that every call runs through the shared expression evaluation exactly once. Keep the existing special handling of `free`; move container moves and borrow invalidation out into call effects that are applied unambiguously once as well.

The name `scanUses` is then misleading; after the functional correction rename it, for example to `analyzeExpr`. Existing tests on move states must not themselves evaluate the whole expression once more.

### Integration limits

- The function resolution shown corresponds to the existing code. For methods/namespaces use the same resolved signature as in call lowering; do not introduce a new short-name heuristic path.
- Initialisers, assignment RHSs, return expressions and nested arguments must run through the shared path.
- The evaluation order must correspond to lowering. For short-circuit operators, effects of an operand that may not be executed are merged with the existing flow states; do not blindly treat both sides as executed unconditionally.
- The fix shown tracks existing `ptr` locals. Do not claim that all aggregates, resources or complex lvalues are automatically covered by it.

**Tests:** n12 is correctly rejected; n11 stays correctly rejected. An additional positive control case: the same initialiser call without a later use of the owner compiles and runs. Further check: a `ref<T>` argument stays usable, a nested owning call is captured and a call is not processed twice as a move.

## 5. Order and verification

1. Check the current diff and build state; keep parallel changes.
2. Implement the independently solvable call checker fix and test it specifically.
3. Introduce the clone descriptor purpose and the new leaf kinds, including recursive registry resolution.
4. Integrate the tensor clone and the two-phase weak ref correction including the final target location.
5. Build compiler and runtime freshly; then:

```bash
ONLY=round4 bash tests/lifetime/run_lifetime.sh
bash tests/lifetime/run_lifetime.sh
```

After that check the changed memory paths with a freshly built ASan binary. Check the existing OFF/serialisation operation specifically, because type descriptors are shared infrastructure. All previous regression tests must continue to pass.

**Completion criterion:** probes 33 and 34 pass, n12 fails with the intended diagnostic, the 45 checks successful so far stay successful, and the new control cases cover field order, null values and one-time call effects.
