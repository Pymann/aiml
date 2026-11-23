# Review: be8c023 and 69b0f34

Date: 2026-09-10. Both commits are called `ownership dataflow`. Range reviewed: `HEAD~2..HEAD`, final state `69b0f34`. All 15 changed paths classified with respect to implementation, probes and documented acceptance. Git used read-only, as requested for the commit comparison. No compiler/runtime code changed, no new tests built or run.

The two untracked probes in the working tree are not part of these commits. Therefore local success reports are not automatically reproducible for a clean checkout.

## R1 — P1: a return via a view variable can free the actual owner

Location: `src/core/ast_compiler_stmt.cpp:1400–1414,1465`.

The exemption from return cleanup only takes the OwnershipId of the returned variable or its textual base name. Example: a local owned model `owner` with a list; a local `view` receives `owner.items` shallowly; `return view`. The analyzer marks `view` as Borrowed/Unknown, but keeps no relation to `owner`. Return protects only `view`; `owner` is freed. The returned list shell thus contains a freed buffer. The same problem exists with a borrowed model pointer under another local name.

The `retVar` check proves no alias safety. Needed: track the resource origin up to the real owner, an independent copy before cleanup, or until then suppress cleanup for an unresolved heap return. The case only becomes runtime-critical through the now activated return release.

## R2 — P1: member deep copy reads the pointer slot as a model for ptr<Model>

Location: `src/core/ast_compiler_stmt.cpp:1284–1296`; helper `src/core/ast_compiler.cpp:4994–5018`.

For a simple ptr variable `emitLValueAddress` returns its alloca. Only `is_byref_arg` is dereferenced; an ordinary `ptr<Model>` parameter or local pointer does not fall under it. The new fast path passes this address directly to `aiml_ser_deep_copy` with the descriptor of the target model.

For an assignment `set: [<.model, source_ptr]` the eight-byte pointer slot is therefore serialised instead of the pointee. Depending on the model fields this produces invalid pointers or reads outside the slot. The previous lower pointer path, by contrast, used the evaluated pointer value.

Correction: evaluate `lvTy` and the LLVM representation; for `ptr<T>` pass the actual pointee, for value T the struct lvalue. Check the null/type contract before access.

## R3 — P1: the new member fast path bypasses the check for incompatible models

Location: `src/core/ast_compiler_stmt.cpp:1284–1314`.

The deep copy returns at :1296; the existing diagnostic for a source of a foreign model type only starts at :1299. For a borrowed model parameter with a different layout the fast path kicks in, although `lvTy` is not checked against `finalTypeName`. The target descriptor is used to read the wrong source layout. Beyond a previously diagnosed program error that can lead to a memory violation.

Correction: put all compatibility checks before any copy/move lowering. Do not replace layout compatibility by the mere presence of an address.

## R4 — P1: the same owner can be moved several times into different members

Location: `src/core/ownership_analyzer.cpp:133–141`; `consumeLocalDrop` in `src/core/ast_compiler_stmt.cpp`.

A local classified as Owned stays Owned/Live unchanged after a planned member move. The source is neither marked Moved in the analyzer nor linked to the target resources. Example: a fresh `x`, then `set:[a.field,x]` and `set:[b.field,x]`. Both statements become a move, both store shallowly. `consumeLocalDrop` only removes the direct release of x. The recursive drops of a and b can still free the same inner heap twice; return cleanup makes this visible even with the usual final return.

Correction: record real consumption per path; detect reuse. Warning-first needs safe handling of the whole ownership chain involved, not just deleting the source drop. Without such transfer states, branch/loop merges cannot prove move safety.

## R5 — P1: calls can change the freed slot without updating the plan

Location: `src/core/ownership_analyzer.cpp:168–194` (conditions, calls and remaining statements).

According to the analyzer a freshly declared list local stays owned even when a function rebinds it to a foreign buffer via a by-ref parameter. Neither call arguments nor conditions provide mutation/retain effects. The new return cleanup can now free this foreign buffer. Deep copy for model member set alone does not fix this case.

Correction: evaluate known call effects, treat unknown mutations/retention conservatively. The conservative protection of the unmodelled function boundaries claimed in the report is therefore not given in general.

## R6 — P1: missing analysis still falls back to unchecked normal drops

Location: `src/core/ast_compiler_stmt.cpp:1642–1646`; `ownership_analyzer.cpp:58–72` and statement dispatch.

The gate only runs when a plan, a block exit and an OwnershipId are present. If one is missing, `emitScopeDrop` is executed anyway. Try/catch is not visited by the analyzer; the corresponding locals get no IDs/exit facts and bypass the gate. The missing coverage was already noted in watch review W1 and is not closed in these commits.

Correction: suppress conservatively when the proof is missing, or model the respective block/parameter explicitly. This is a remaining protection gap of the newly introduced analysis, not a claim that every release affected by it was only added in these commits.

## R7 — P2: the checked-in harness references test files that are not checked in

Location: `tests/run_memory_sanitizers.sh:143–146`.

The following referenced files are missing from the tree of `69b0f34` and only appear locally as untracked:

- `tests/_mem_sanitize/ownership_shared_member_void.aiml`
- `tests/_mem_sanitize/return_cleanup.aiml`

A clean checkout cannot fully pass the new harness. Correction: make the probes actually used part of the change; do not rely on the local working tree as a test precondition.

## R8 — P2: the local model copy fast path bypasses the const check

Location: `src/core/ast_compiler_stmt.cpp:899–922` versus the const check at `:974`.

For compatible heap models the deep copy including the early return happens before `sym->is_const`. A local assignment into a const model that used to be forbidden can thereby be accepted. The self-assignment shortcut also lies before it.

Correction: writability check before all fast paths. No runtime/ownership optimisation path may skip existing language checks.

## Probe and documentation findings

- `assign_local_copy.aiml` comments that the inner `yrec` is dropped. The analyzer, however, sets it to Unknown at the set and the gate suppresses exactly this drop. The probe checks content and later source access, but does not by itself prove the claimed cleanup independence. Demonstrate release/mutation of the copy specifically.
- `map_string_drop.aiml` only checks construction without a crash; despite the comment there is no content assertion. Map drop stays explicitly deferred and is therefore not validated by it.
- The data model/join/mayDrop module makes sense as a building block. But without alias resources, moved transfers and call effects it provides no complete ownership proof.
- The documented local improvements (direct view rebind, certain borrowed model member stores, simple return with its own list) are narrower than the statement that the whole core acceptance criterion is met.
- Deliberately deferred replace leaks and map/box drops are known remaining work; they are not counted twice here as a new unexpected regression.

## Result

Both commits improve individual known cases. A general release of the active return cleanup is not covered by the current analysis. Close R1–R5 before that release; correct R6 as a conservative gate rule. R7 additionally makes the documented harness success not reproducible from the commit alone.

All failure paths above are derived from the source; no concrete new crash repros were run in this review. Documented earlier suite/valgrind successes were not presented as our own verification.
