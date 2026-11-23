# Spec 61: comparison with the implementation

As of: 2026-09-12. Volatile source review; line numbers apply to the working state as read.

## Scope and limits of the statements

Spec 61 was read in full. The following ten points correspond to the spec review discussed earlier. Relevant compiler, ownership and runtime paths were cross-checked, plus the references to collections and heap serialisation.

No builds, repro tests, sanitizer runs or new heap measurements were done. Statements about generated behaviour are static derivations from the code. The reported rates of about 60 KB/min server and 8 MB/min client are therefore not quantitatively attributed to individual causes. No implementations were changed and no git commands were run for this report.

**Result:** the implementation is not simply undetermined: many decisions are programmed concretely. However, they are spread over the analyzer, drop registration and individual lowering paths and do not agree everywhere. An unambiguous spec alone would not fix the implementation gaps below.

## 1. Comparison of the ten spec points

All code paths are given relative to the AIML project root. The linked files are reachable from the report; line numbers are given separately so that the links also work in a markdown viewer.

| No. | Spec problem | Actual code | Assessment / necessary clarification |
|---|---|---|---|
| 1 | §3 declares local values to be owners; §3.2 exempts ordinary function returns. | `ownership_analyzer.cpp:36–114`, `freshOwned`: fixed initialiser patterns for no-init lists/models, interpolation, model get_copy, producer calls and certain boxes. Other initialisers stay non-owned. `ast_compiler_stmt.cpp:355–523` maintains the drop classification separately. | **A concrete heuristic, but a double truth.** Analyzer and drop registration must use the same classification function. Divergence already observed: F1. |
| 2 | §4 says push/return always consume; §7 adds copy and branch exceptions. | `ast_compiler_call.cpp:1261–1337`: model push decides based on the registered drop in the topmost cleanup frame. Member stores use the ownership plan. Calls/returns consume via `consumeLocalDrop`, `ast_compiler_stmt.cpp:1771`. | **Inconsistent mechanisms.** Not all transfers are represented as a uniform operation in the data flow. The helper removes drop entries from all cleanup frames. |
| 3 | Borrow escape is an error according to §3/§8, partly an automatic deep copy according to §7; getters may return views. | `ast_compiler_stmt.cpp:1003–1020`: a warning for certain pointer/reference parameters stored into this-members. Other member paths copy models/containers. `visitRetStmt` contains a conservative cleanup waiver for unclear return origin. | **Partial implementation, no general escape ban.** Storing a borrow, copying a value and returning a borrow need separate contracts. |
| 4 | Replacement: evaluate the RHS, free the target, then take over — insufficient for aliasing/copy errors. | New: central member free in `ast_compiler_stmt.cpp:1373–1385`; `emitContainerFieldFree`, `ast_compiler.cpp:3155`, checks address equality. `emitContainerFieldDeepCopy:3243` then nulls the target. Local model assignment in `ast_compiler_stmt.cpp:1044–1070` still describes a missing release of the old target heap. | **Improvement for member replacement, but no general alias-safe replacement.** F2 concerns the new path; local assignment stays separate. A fallible copy/commit contract is missing. |
| 5 | persistent names no actual longer-lived owner or release route. | `freshOwned:37` and drop registration exclude persistent. No concrete owner is bound by it. Later copies depend on the operation/type path. | **Unambiguous as an auto-drop exemption**, not implemented as a complete life cycle. A persistent original can be left behind without an owner reachable later. |
| 6 | consumed is accepted based on the pointer representation, although that guarantees no ownership/destructor. | Parameter check in `ast_compiler.cpp`, region from 4737: pointer/byref check and string parameter drop. The caller in `ast_compiler_call.cpp:420` removes local drops; this path contains no general origin check of the argument. | **Structurally unambiguous, semantically incomplete.** Borrow arguments, non-copyable handles and type-appropriate destructors need explicit rules. |
| 7 | No reference counting according to §1, but RC strings and decref in §3/§3.2. | `string_runtime.cpp:15–94`: RC header, alloc/dup, incref/decref. | **Here the code is unambiguous and the spec contradictory.** Complete coverage of all producers/transfers remains an implementation topic. Raw malloc/strdup pointers do not have the same contract as RC strings. |
| 8 | Target lifetime and dominance are mixed up with syntactic categories. | Model push uses the topmost cleanup frame. `ownership_analyzer.cpp:184–214` decides member transfer based on the source origin; a corresponding target lifetime/same-block check is missing there. | **Spec vagueness is mirrored in the code.** The dominance rule does not apply uniformly to member stores; the actual lifetime of the target container is not compared in general. |
| 9 | producer is mainly described for call-initialised declarations. | Drop detection in `ast_compiler_stmt.cpp` and `freshOwned` match the last part of the function name against `ctx.ownsReturnFns`. General owned expression temporaries are not covered by that; interpolation temporaries have a separate system. | **Implementation gap.** Ignored/nested producer calls and transfers without a local declaration need an expression contract. A short name instead of a resolved function identity carries a risk of name collisions. |
| 10 | A leak lint would already go silent at any syntactic transfer. | No such lint found in the compiler code reviewed. `ownership_analyzer.cpp:256` explicitly leaves general call effects outside the flow analysis; moves via push/return happen during lowering. | **Not implemented yet.** Existing flow facts are not sufficient to simply query all actual transfers and exits. |

## 2. Concrete findings

### F1 — P1: new UI string drops are suppressed by the ownership plan

**Code:** [drop registration](../../src/core/ast_compiler_stmt.cpp), lines 448–474; [analyzer](../../src/core/ownership_analyzer.cpp), lines 55–58; [drop gate](../../src/core/ownership_flow.cpp), `mayDrop`; [runtime](../../src/sys/prefab_runtime.cpp), `aiml_prefab_text_value` / `aiml_prefab_dropdown_value`.

The runtime now returns `aiml_str_dup` instead of raw `strdup`. `visitDeclStmt` recognises the short names `text_value` and `dropdown_value` as owned string producers and registers a drop with an ownership ID. For strings, `freshOwned` still recognises only interpolation. The local value therefore gets Borrowed instead of Owned in the plan. `mayDrop`, however, requires Owned + Live + Resource and refuses the drop.

**Effect:** in a normally analysed function block the new drop registration cannot fix the getter leak. A missing plan can produce different behaviour, because the normal block exit then emits registered drops without this gate check.

**Proposal:** a shared classification with resolved builtin/function identity instead of two lists of short names. Do not remove the gate check across the board. Future validation: check the getter result in a normal function block and on return paths; no repro has been run so far.

### F2 — P1: the self-assignment guard only protects the free, not the subsequent copy

**Code:** [compiler](../../src/core/ast_compiler.cpp), `emitContainerFieldFree:3155`, guard against `dstPtr == srcPtr`, as well as `emitContainerFieldDeepCopy:3243–3270`; [member store](../../src/core/ast_compiler_stmt.cpp), region 1373–1432.

With identical addresses `emitContainerFieldFree` skips the release. The subsequent deep copy helper, however, nulls `dstPtr` regardless and only reads `srcPtr` afterwards. With identical addresses the source is therefore already nulled as well. A member self-assignment of a list can reach this path: the source is a member and is classified as DeepCopy; the plain local self-assign shortcut does not apply there.

**Effect according to the code path:** data loss and loss of the previous heap pointer when an identical source/target slot is reached. No runtime repro run.

**Proposal:** treat an identical source/target as a no-op across the whole replacement. For overlapping but not identical sources, make an independent copy before the target release. Pointer equality alone does not prove freedom from aliasing.

### F3 — P1: transfer/branch state and drop lists stay separate

**Code:** [analyzer](../../src/core/ownership_analyzer.cpp), member transfer around 184 and call fallback around 256; [codegen](../../src/core/ast_compiler_stmt.cpp), `consumeLocalDrop:1771`; [list push](../../src/core/ast_compiler_call.cpp), region 1261–1337.

A member store can classify an owned local as a move without checking the same-block condition of model push. `consumeLocalDrop` removes matching entries from all cleanup frames. The data flow analysis does not capture general calls as corresponding state transitions. A later `mayDrop` cannot restore a drop entry that was previously removed completely.

**Effect:** conditional transfers and different operation paths remain a source of under-drop. A general use-after-move/merge error diagnostic is not replaced by this. Not every conceivable branch example was run.

**Proposal:** the transfer plan and the exit plan must be the shared basis for decisions; locally erasing a globally used cleanup list must not replace a path-dependent decision.

### F4 — P2: producer detection via short names instead of function identity

**Code:** [analyzer](../../src/core/ownership_analyzer.cpp), `freshOwned`; [declarations](../../src/core/ast_compiler_stmt.cpp), model/box/list producer detection; [context](../../src/core/context.h), `ownsReturnFns`.

The detection splits the call name at the last dot and looks the rest up in the producer set. That way the ownership of a method of the same name on another receiver type can be attributed. For a getter result wrongly recognised as a producer, a drop on foreign ownership would be possible.

**Status:** a name collision risk from the implementation; no concrete 9rooms collision and no runtime error demonstrated in this review.

**Proposal:** take producer metadata from the resolved callee or the interface/function signature. Use the same identity in analyzer and codegen.

## 3. Additional life cycle and status findings

- **Drop order:** the normal block exit and supported return paths walk cleanup frames/entries in reverse. That is already implemented concretely, not fundamentally open.
- **Return cleanup:** `visitRetStmt` (ast_compiler_stmt.cpp from 1590) restricts cleanup based on `safeToCleanup`. For unclear return origin releases are deliberately omitted. The full goal of deterministic release is thus not reached yet.
- **Exceptions:** `visitTry` / `visitThrow` (same file from 2055 / 2175) contain no complete ownership unwind. The analyzer also does not treat try/throw structures like its block/if/while/return paths.
- **Containers with boxes:** `aiml_list_free` (string_runtime.cpp from 105) knows raw, string and model elements. The registration for `list<ptr<T>>` establishes no general recursive owned box drop. Nested containers likewise need an explicit check of their descriptors and drop kinds.
- **Consumed parameters:** the analyzer initialises parameters as Borrowed; the string parameter drop registration is a separate path. No general resource ownership for all accepted handle types follows from that.
- **Interfaces:** the status description in spec 61 is outdated: lifetime signature information, interface dispatch transfers and a check against incompatible consumed overrides are present. That is not a claim of complete interface ownership correctness.
- **Map/other drop coverage:** existing runtime free functions do not automatically prove complete registration and calls for every local, temporary or member form. The implementation status must make this difference visible.

## 4. Corrections relative to earlier findings in the conversation

1. The earlier blanket finding "member assignment does not free the old content" is outdated for the state now read. A central member free exists; remaining problems concern in particular alias protection, divergent local assignment paths and complete drop coverage.
2. UI getters now return RC strings and have a registration special case. The open finding is the missing alignment with the analyzer (F1), no longer raw strdup alone.
3. The original server/client leak rates can be confirmed neither as fixed nor as unchanged from these changes or the source review. Server RSS and occupied WASM heap are, moreover, different measures.

## 5. Priority for further work

1. Fix F1 and F2 specifically; keep the existing safety gates.
2. Unify the ownership classification for analyzer and codegen, including resolved producer/builtin identity.
3. Bind copy/move/replace decisions, including branches, to a shared transfer plan.
4. Maintain the spec status separately from the target contract; do not describe missing support across the board as "always safe, at most a leak".
5. After that, confirm the relevant runtime paths with suitable memory error checking and heap measurement. This report neither implements nor starts such tests.

This is not a proposal to reintroduce the discarded WP-C as a whole. The findings concern the existing ownership implementation and its concrete interfaces.

## 6. Sources read

- [Spec 61](../../spec/61_lifetime_ownership.md)
- [Ownership analyzer](../../src/core/ownership_analyzer.cpp)
- [Ownership flow](../../src/core/ownership_flow.cpp)
- [Statement codegen](../../src/core/ast_compiler_stmt.cpp)
- [Call codegen](../../src/core/ast_compiler_call.cpp)
- [Compiler / field free and copy](../../src/core/ast_compiler.cpp)
- [String/list runtime](../../src/sys/string_runtime.cpp)
- [Prefab runtime](../../src/sys/prefab_runtime.cpp)

### File fingerprints at filing time

The hashes serve to detect later changes; the review is not an atomic repository snapshot.

Recorded: 2026-09-12T14:13:46+02:00

| File | SHA-256 |
|---|---|
| `spec/61_lifetime_ownership.md` | `768224cba95fe3273fd97432b96d84fed3c88f1f16c0bc305a10e36a79381986` |
| `src/core/ownership_analyzer.cpp` | `d6bf58551def3dea713b19035e4c219293afa55cd4af59cea1f743a6a93b10c9` |
| `src/core/ownership_flow.cpp` | `cd55ef68e0ccb726ff5e8521f2fc88c7862d074a09aaee5ed1baa8e1659e6404` |
| `src/core/ast_compiler_stmt.cpp` | `622f47037338d983b569d51bf1bc0628e0b5ed09049777041343dd77dec870e2` |
| `src/core/ast_compiler_call.cpp` | `bb25303d036692c452bda8f7dcb55251ab20a21191be7572088aa2eeedeaf808` |
| `src/core/ast_compiler.cpp` | `91828cfc759dfecef8b144ac2aa0cb26b61e1d9014e937054d1a18dd6aa3e882` |
| `src/sys/string_runtime.cpp` | `07d18e2b56ee04142225b36bf7315160cfdf83870dc0aaa3491bd120a03955b6` |
| `src/sys/prefab_runtime.cpp` | `de43d89a14172111dcf2ddc851a82e4c2d10bf53a92110b2c7b1f3a93dea75ef` |
