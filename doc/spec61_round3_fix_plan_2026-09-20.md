# Spec 61: plan to fix the three findings from review round 3

As of: 2026-09-20. Status: planned, not yet implemented.

## Goal and scope

Fix the three demonstrated errors from probes 30–32. Authoritative are spec 61 §5.1 (deep clone and preserving the graph structure) and §6.2 (borrow instead of ownership transfer for `ref<T>`).

The last complete test run gave 42 successful checks and three failures. The older regression tests already passed. Since changes to the compiler are happening in parallel, this baseline must be re-checked before implementation.

| Finding | Actual behaviour | Expected behaviour | Regression test |
|---|---|---|---|
| F1: list clone is shallow | Mutation of the clone changes the original from 7 to 99 | All owned mutable elements are cloned independently | [30_round3_list_clone_deep.aiml](../tests/lifetime/30_round3_list_clone_deep.aiml) |
| F2: container DAG is lost | Two originally identical lists are cloned separately; `right.len` stays 2 | Both edges point to exactly one cloned container; `right.len` becomes 3 | [31_round3_clone_container_dag.aiml](../tests/lifetime/31_round3_clone_container_dag.aiml) |
| F3: borrow assignment is treated as a move | `set: [view, second]` subsequently leads to `use-after-move` for `second` | `view: ref<T>` borrows; `second: ptr<T>` stays a valid owner | [32_round3_ref_rebind_borrow.aiml](../tests/lifetime/32_round3_ref_rebind_borrow.aiml) |

The implementation stays behind the existing spec 61 switch `AIML_MEM_V3`. The semantics of `copy` stay shallow. Older errors and other ownership construction sites only belong in this scope if they directly block the three corrections.

## 1. Secure the baseline

- Review the current diff and keep parallel changes; re-read the functions named below.
- Build a compiler from the current C++ state and document the commit or working state used as well as the build time.
- Run `ONLY=round3 bash tests/lifetime/run_lifetime.sh`. Do not re-implement cases that are already fixed.
- Use existing control cases for container aliasing, shallow copy, graph clone and borrowing as a comparison.

## 2. F2: memoise containers in the graph clone

**Affected places:** [ser_runtime.cpp](../src/sys/ser_runtime.cpp), `clw`, in particular `AIML_TD_LIST_PTR`, `AIML_TD_MAP_PTR`, `AIML_TD_BUFFER_PTR`, as well as `aiml_clone_deep`.

So far the three container cases create a new handle for every incoming edge. They must use the same original object → clone mapping as the rest of the graph walker.

1. Before every container allocation, check the source-address-based memo table.
2. On a hit, take over the already cloned handle and increase its **container refcount** for the additional owning edge. Do not use `aiml_model_incref` here: model and container headers are different runtime contracts.
3. For a new object, create the matching list, map or buffer handle and enter it into the memo table **before** recursively copying its contents.
4. Process all sub-objects within the same clone call with the same memo table. Null handles keep their existing semantics.
5. Make sure every owning edge is counted exactly once and temporary helper handles leave no additional references behind.

**Acceptance:** probe 31 passes. A mutation via `duplicate.left` is visible via `duplicate.right`, while the original stays unchanged. Additionally check corresponding DAG cases for map and buffer, since they use the same changed switch branch. No multiple releases may occur at scope end.

## 3. F1: hook the list clone up to the deep-clone walker

**Affected places:** [ast_compiler_stmt.cpp](../src/core/ast_compiler_stmt.cpp), `emitListHandleDecl`; [string_runtime.cpp](../src/sys/string_runtime.cpp), `aiml_list_handle_copy`; the generation of clone type descriptors in [ast_compiler.cpp](../src/core/ast_compiler.cpp).

So far `emitListHandleDecl` executes `copy` and `clone` together via `aiml_list_handle_copy`. This runtime function deliberately copies only the container and increases the refcounts of its shared elements.

1. Split the handling of `isCopy` and `isClone`. `copy` keeps the shallow runtime path and its move-only check.
2. Execute `clone` via the memoised deep-clone walker from section 2. Reuse existing helper functions for clone expressions and type descriptors, or make them specifically shareable.
3. Respect the actual representation: under memV3 a local list contains a **handle pointer**, not a list header stored inline. Source address, target address and root type descriptor must match; a `LIST_PTR` descriptor needs the address of the handle slot.
4. Evaluate the source expression exactly once. Do not silently replace a valid clone expression with a null handle when the source is not a simple variable name.
5. Use one shared memo table for the whole list. A separate clone call per element would destroy aliases between elements again.
6. Take the result handle into the target slot as exactly one new owning reference and connect it to the existing scope drop. Do not run an additional shallow element incref loop after the deep clone.

**Acceptance:** probe 30 passes. Additionally check:

- Two list entries with the same `shared<T>` result in exactly one new shared target object within the clone.
- `list<ptr<T>>` gets new owned objects; no pointer address is shared with the original.
- A shallow `copy` of a list with `shared<T>` still shares the elements, but owns its own container.
- The existing scalar copy/clone probes and probe 15 stay green.

## 4. F3: align the move diagnostic with the target type

**Affected place:** [borrow_check.cpp](../src/core/borrow_check.cpp), `BorrowChecker::stmt`, branch for `SetStmt`.

The existing case distinction still treats an unknown target slot, or one not recorded as `ptr`, as an owning receiver. So for a `ref<T>` target the pointer source is wrongly set to `Moved`.

1. Determine the static type of the target slot reliably. For local variables use the type information of all declarations and parameters; a missing entry in `ptrLocals` does not prove an owning target type.
2. For `ref<T>` targets check the source for valid use, but do **not** mark it as moved. Update the borrow origin or existing invalidation information, insofar as the checker records it.
3. Use the same decision for resolvable `ref<T>` fields. A field target alone must not automatically trigger a move.
4. For real owning target slots keep the existing move rule. Forbidden cross-kind conversions must still be diagnosed; the correction must not allow them through a blanket exemption.
5. In lowering, cross-check that a borrow assignment neither empties the source slot nor removes its drop or transfers ownership to the `ref`. Probe 32 must also run correctly after a successful diagnostic phase.

**Acceptance:** probe 32 passes. Additionally check borrow field assignment. The negative tests for real moves, illegal conversions and reuse of a pointer passed to an owning parameter stay effective.

## 5. Order and final check

Recommended order: F2 → F1 → F3. F1 needs the correct container memoisation; F3 is independent of it. Create a small, separately checkable diff per change.

After the corrections, from the AIML project directory:

```bash
ONLY=round3 bash tests/lifetime/run_lifetime.sh
bash tests/lifetime/run_lifetime.sh
```

- All three original probes and the targeted additions pass.
- The complete spec 61 lifetime suite passes, including negative and warning checks. Do not weaken expectations to make faulty behaviour green.
- Check the changed clone paths with a freshly built ASan compiler for invalid memory accesses and double frees. Use acyclic probes for leak checks; handle deliberate ownership cycles separately.
- Also run relevant model-independent control tests with `MODE=off`, to check the restriction to memV3. Do not require the whole spec 61 suite to be green in the old mode.
- On new failures only extend the affected checks; do not repeat successful unchanged checks without reason.
- Write a final report with the changed C++ path, build state, test commands and results.

## Done when

The new tests 30–32 and all previous lifetime checks pass, list clones copy owned elements independently and keep internal container aliases, and borrow assignments trigger no ownership transfers. The existing shallow `copy` contract and real move diagnostics are preserved.
