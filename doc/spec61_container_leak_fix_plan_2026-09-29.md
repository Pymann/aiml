# Spec 61 — implementation plan for container leaks and memory errors

As of: 2026-09-29. **Implementation plan; regression tests added in the review, compiler/runtime fixes still open.** AIML HEAD read: `be736b3`, with existing uncommitted changes. Authoritative for this task is the current [spec 61](../spec/61_memory_management.md), including its DRAFT status; the older v2 draft with general model reference semantics is not the basis.

## 1. Goal and documented starting point

Close six findings from the open leak search: four leak classes as well as the `list<string>.insert` UAF and the scope teardown crash of a map with nested container values. After that the affected operations must fulfil the ownership rules of the spec, deliver correct contents and end without lost allocations or invalid memory accesses.

The investigation used the existing compiler and `libaiml`, native AOT repros and Valgrind with `AIML_MEM_V3=1`, `AIML_LIFETIME_DROPS=1`. No fresh compiler build, no WASM proof. Therefore run P0 before implementation. The byte counts given are historical measurements on x86_64, not ABI requirements.

Artifacts of the investigation: `/tmp/aiml-leak-audit-20260929/REPORT.md`, `results.json`, `provenance.json`; per repro `main.aiml` and the complete `valgrind.log`. `/tmp` is not permanent: P0 takes the sources and evidence of the findings into the project.

| ID | Finding / existing repros | Result at 200 runs | Spec contract |
|---|---|---:|---|
| B1 | `map_nested_control`, `map_nested_self_put` | Invalid read + SIGSEGV at scope teardown | §5, §5.7: values are handles; consistent strides/descriptors |
| B2 | `list_string_insert` | 200 invalid reads at list teardown | §4, §5.4: insert owns a string reference of its own |
| B3 | `map_shared_control` | 3,200 B direct + 3,800 B indirect lost | §5, §6.2: decref shared values regardless of the key type |
| B4 | `map_nested_remove`, `map_model_remove` | 8,000 B and 3,800 B lost in total | §5.4, §5.7, §7: removing frees the value according to its class |
| B5 | `map_model_replace` | 3,400 B direct lost | §5.5, §7: secure the source, drop the old model, install the new value |
| B6 | `list_pop_bound`, `list_pop_arg` | 3,490 B direct lost each | §5.4, §8: pop transfers ownership to the result |

Counter-probes: `map_model_control`, `map_string_control`, `map_shared_string_key_control`, `list_nested_control`, `list_pop_discard` were 0 definite / 0 indirect and without Valgrind errors. The map without `remove` is explicitly not a green counter-probe for B4 because of B1. `list_nested_remove_value` was rejected by the compiler and does not count as a leak.

## 2. Binding implementation rules

1. **The type determines ownership (§2.1, §4–§7).** Strings, container handles and `shared<T>` are shared; `ptr<T>` is moved; `ref<T>` stays non-owning; value models keep inline value semantics with recursive field operations. No global switch of models to reference semantics.
2. **A result owns exactly the references that its later release gives up.** Named copyable sources keep their drop. An owned temporary that is actually taken over loses its cleanup obligation exactly once. `producer` denotes additional freshness and is not a precondition for ownership of a `pop` result (§9.2).
3. **Secure-before-release (§5.5).** Evaluate the RHS once, secure the new reference or empty the move source at runtime, then free the old target, then store. Distinguish slot identity from equal handle values in different slots (§5.7). No blanket `oldHandle == newHandle → skip release` when a new reference has already been taken over.
4. **Shared slot classification (§5.7).** Physical LLVM storage type, stride, retain/release and serializer descriptor agree. A container value is a pointer slot, not the inline header and not the address of a handle slot. Pointer size from the LLVM DataLayout, never a fixed `8` for wasm32.
5. **Recursive release (§6.1, §7).** Inline model: free the fields, not the inline slot itself. Shared object: decref. Unique box: release the fields and the shell. Weak ref: no release. Nested container: decref with the correct inner descriptor.
6. **No covering up.** No switching off of drops, no additional `persistent` annotations, no blanket deep copies/`clone`, no Valgrind suppression for these findings. String copies in the touched memV3 store share per §4; `str_dup` is not a general ownership fix.

The work closes the six findings and the store/release paths directly used together. It opens no new GC/cycle architecture and changes neither the rules for container keys nor the semantics of `get_copy`/`clone`. Legacy OFF/v2 paths are kept separate and must not accidentally get the memV3 layout.

## 3. Work packages and order

Recommended sequence: **P0 → P1 → P2 → P3 → P4 → P5 → P6**. P2 and the narrowly isolated P3 depend technically only on P0; the order prioritises the safety errors. Shared helpers only come about in the form needed for these operations, no premature complete rework of the compiler IR.

### P0 — baseline and permanent regressions

**Files:** new probes in `tests/lifetime/`, `tests/run_lifetime_leak_gates.sh`, if needed `tests/run_memory_sanitizers.sh`; finding sources/logs under a new `doc/analysis/container_leak_audit_2026-09-29/` directory.

- Build compiler and native runtime from the same current source state, then AOT compile/link the repros again. Record HEAD, working tree diff, flags and binary hashes together; keep the historical logs.
- Take over all six findings and the five clean counter-probes. Use meaningful names; only assign numbers at implementation time after looking for free slots. Keep the existing probes 86/87 and their changes.
- Extend every repro with content/lifetime assertions: map shorter after removal, values not removed intact, new model visible after replacement, pop returns the expected text, insert preserves order and content even after the end of the source scope.
- Keep new open tests at first in a subdirectory not covered by the normal lifetime glob and report them explicitly as RED in the leak gate. Move them to GREEN and into the regular selection with the respective fix. A green overall status must not report the still open findings as fixed.
- Record the current GREEN cases as well as the native lifetime/safety state. Document foreign, pre-existing failures separately; do not move any existing GREEN case to RED to get this work through.

**P0 done:** every reported error is reproduced with a freshly built toolchain pair or its deviation is explained concretely. Permanent repro sources and before logs exist. Do not derive a statement about a historical crash from today's code alone.

### P1 — B1: nested map values with a uniform layout

**Files/functions:** `context.h` type resolution, `ast_compiler_call.cpp` map dispatch, `ast_compiler_stmt.cpp::emitMapHandleDecl`, `map_impl.{h,cpp}`, `ast_compiler.cpp::elemHandleSerNodes` and its descriptor builder, `string_runtime.cpp::aiml_map_free`, if demonstrably needed `ser_runtime.cpp`.

**Diagnostic step:** B1 is confirmed as a crash, the first wrong store has not yet been conclusively located. For the minimal case compare the generated IR and the runtime values: actual handle, map value slot, stored bytes, stride, root offset and root kind of the descriptor. Both header-instead-of-handle storage and a wrong slot/descriptor are to be checked. Do not treat only the later fault site with null checks.

**Implementation:**

1. Use, or specifically add, one central classification for container element/value slots. Proposed internal result: `storageType`, `stride`, ownership class and release/retain metadata. That is an interface draft, not a claimed existing helper.
2. `map<K:list<U>>`, `map<K:map<X:Y>>` and `map<K:buffer>` allocate and address pointer slots. `put/get/remove`, growth, decl/scope teardown and serializer use the same classification. `MapImpl::put` must not derive the slot size uncontrolled from the incidental LLVM type of the RHS.
3. Keep the receiver header pointer, the handle value and the address of a handle slot explicitly apart. An inline struct in the memV3 container value store is a codegen contract error with a diagnostic, not a reason for a silent bitcast/fallback.
4. The descriptor for a container value is rooted on its handle slot (`*_PTR`, matching offset); the runtime gets the address of exactly this slot. For inline models the model descriptor stays rooted on the inline bytes.
5. The same contract applies to native 64-bit and wasm32. Replace all affected size/stride constants together with the layout, without rebuilding the whole ABI without reason.

**P1 acceptance:** a simple nested map without a crash and without a leak; self-put at first without invalid memory accesses. Its complete reference balance is accepted in P4. Content after `get`, several keys, at least two growth steps; an alias stays valid after the drop of the outer map. `list`, `map`, `buffer` as a value, numeric and string keys. `copy/clone` and a serialised round trip as a regression for the touched layout paths. If the self-put then shows a reference leak, P4 must close it; report B1 safety and the final leak status separately.

### P2 — B2: `list.insert` acquires element ownership

**Files/functions:** `ast_compiler_call.cpp` insert/push dispatch, `list_impl.{h,cpp}::insert`, existing string/model/container retain helpers and ownership transfer registration.

1. Route push and insert through the same type-driven preparation step for an element to be inserted. The primitive for shifting/storing gets an already secured value and its correct storage type.
2. Named string: incref; owned string temporary: take over the reference and consume the temporary cleanup. Treat literals/the immortal empty string via the existing valid string contract. Do not introduce any new direct header probe on arbitrary pointers.
3. Secure the same principle for the element classes already supported: container/shared handles retain/adopt, copyable models member-wise retain, real unique moves with an emptied source. No raw byte copy of a non-trivial model as a substitute for ownership.
4. Secure the source **before** realloc or shifting. With `insert(..., at(sameList,...))` the source slot can become invalid; use a secured handle or an independent temporary value slot. Provisionally acquired ownership must also be cleaned up on an existing error/rejection path.

**Acceptance:** the original UAF repro green; the local source lives longer/shorter than the list; the source readable afterwards; a fresh interpolation, a literal and a pop result; inserting at the beginning/in the middle/at the end, with and without growth, a source from the same list. For types touched together, at least one container/shared/value model probe each as well as the existing unique-move negative tests.

### P3 — B3: map scope release regardless of the key type

**File/function:** `src/sys/string_runtime.cpp::aiml_map_free`.

- Correct the outer loop gate: all supported value kinds with a release obligation must reach the pass. Concretely, today `valKind == 3` is missing when `keyKind != 1`. Better to use a clear `valueNeedsRelease` predicate/switch than key ownership as a proxy.
- Keep the inner shared release operation: exactly one `aiml_model_decref` per live slot; no additional release for a co-owner that still exists.
- Null values and empty maps stay valid; scalar values trigger no object release. Dense iteration `[0,size)` is kept.

**Acceptance:** integer, enum and string keys with `shared<Item>`; 200 and 400 repetitions each 0/0 B; several keys, map growth, an empty map and an external shared owner still alive. The source stays readable after the map drop, the last owner frees the fields and the shell.

### P4 — B4/B5: type-appropriate remove and safe replace

**Files/functions:** `ast_compiler_call.cpp` map dispatch, `map_impl.{h,cpp}::put/remove`, the shared slot classification from P1, existing recursive model release/retain helpers; with changed move behaviour, the ownership analyzer and cleanup management in the same package.

**Remove:**

1. Pass value ownership/release metadata; `bool valIsString` is not enough. For every entry found, free the key and the value according to their class. The current key string release stays, exactly once.
2. Free the value **before** the shift and the size reduction. Entries shifted by `memmove` are moved, not copied: neither an incref nor an additional drop of the old byte position. Set the now unused tail slot to an empty state for owning types, without a second release.
3. A missing key is a no-op. First/middle/last entry are treated symmetrically. If the search key lives in the data being deleted, it must not be used after the release.

**Put / replace:**

1. Before the update, secure the source per the spec: share named copyable values; take over owned temporaries; empty unique-move sources at runtime. Replace the blanket `consumeLocalDrop` for all non-strings by the actual transfer contract. **Switch append and update over to this contract in the same package**: if the caller takes SHARE instead of MOVE in future, both store branches must receive exactly the secured reference; merely releasing the old one in the update is not enough. Do not clear a cleanup list globally on a route that is only executed conditionally.
2. In the value slot found, free the old value with exactly the same classification as remove/scope teardown and only then store the secured new value. Drop inline models recursively; decref shared values; decref container handles. Do not take over the existing key anew. A new key temporary secured before the key lookup must be freed regularly in the update/error path; only append takes over the new key ownership.
3. Also align the current string update with secure-before-release: with an aliasing RHS it must not first free the old string and then read its content. Share by retain/adopt per §4, do not repair by deep copy.
4. Pointer equality alone does not skip an ownership balance that is already due. An already secured new reference plus the old slot reference: free the old one, install the new one, even for the same referent. Only an **identical source/target slot** allows the real early no-op. Different slots with the same handle and a move must correctly dismantle the source slot plus the surplus reference.
5. Do not hold source and target slots across RHS calls that can grow the same map. Materialise/secure a value from an existing map slot before a potential realloc; re-resolve the target position after mutating expressions according to the existing evaluation order.

**Acceptance:** the original model/container remove and model replace repros 0/0 B; repeated replace, update after growth, same/different keys, same/different handle values, `put(k,get(k))`, a source from another slot of the same map, a source from a field of the old model. Content and alias assertions as well as a live co-owner after remove/replace. At least string, nested container, shared, a copyable model with string+container+nested-model fields. Check supported unique/weak value routes positively/negatively against §6.2; unsupported types must not silently fall through as trivial pointers.

### P5 — B6: treat pop results as owned results

**Files/functions:** `ast_compiler_call.cpp` pop dispatch and call argument paths, `list_impl.cpp::pop`, `ast_compiler_stmt.cpp` decl/set/return/statement cleanup; existing owned temporary management in compiler/context. Coordinate with the uncommitted work on container return/producers (probes 86/87), do not introduce a second competing temporary list.

1. `pop` removes the slot and returns its **existing ownership**: no additional incref on extraction, no decref of the returned element. It is not a borrowed `.get` return and needs no `producer` name heuristic.
2. Register a string pop as an owned temporary when producing the result. For other supported non-trivial pop types use the matching typed result/cleanup contract; aggregates need a stable temporary slot for an address-based drop.
3. `decl`/`set` takes over the owned string exactly once, without a borrow retain; `set` frees the old target per §5.5. As an ordinary call argument the temporary stays valid until the end of its use and is freed afterwards; additional parameter/callee references must be balanced separately. A consuming parameter takes over ownership.
4. Passing on to return, push, insert or map put takes over or shares per the target contract. Exactly one cleanup is to be consumed; no premature release inside nested expressions. An expression containing two pops creates two separate ownership obligations.
5. Handle a directly discarded pop either by the same result drop or by the existing `aiml_list_pop_free` special path, never both. Conditions, return, `break`/`continue` and early block exits must not skip temporary cleanup.
6. Secure the existing behaviour for an empty list separately; this leak fix does not invent new empty-pop semantics. An invalid pop must not pass as a green leak probe.

**Acceptance:** bound, rebind, argument of a free function/a method, directly discarded, passing on by return, re-push/insert, interpolation and a nested call; 200/400 iterations with the correct text and 0/0 B. Check container/shared/value model/unique pop specifically, as far as supported by the compiler, so that the new classification does not create a string special contract for all types.

### P6 — integration and final acceptance

**Available runners (from the AIML project root):**

```bash
AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1 bash tests/run_lifetime_leak_gates.sh
AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1 bash tests/run_memory_sanitizers.sh
MODE=spec61 bash tests/lifetime/run_lifetime.sh
```

Also run the targeted new probes via `run_wasm_tests.sh` with an explicit file list and a freshly built matching wasm32 runtime. The runner exists; a native Valgrind measurement replaces neither WASM execution nor its separate leak measurement. Both the native AOT acceptance and the JIT lifetime execution are needed.

- All six findings must become fixed GREEN regressions. The targeted strict runner `python3 tests/lifetime/container_leaks/run.py` must return exit 0. The existing leak gate only reports RED cases; its exit 0 alone is therefore not enough as acceptance of this work. Per package, targeted probes first; after integration the existing GREEN leak cases and the safety suite completely. Keep the status of foreign cases as it is at implementation time. In the review 86/87 are already GREEN; keep securing their unchanged tests. The new case 88 is foreign ongoing work. No downgrading or overwriting of these changes.
- Per leak probe: native exit 0 plus content assertions, **0 definite / 0 indirect**, no invalid read/write/free and a complete Valgrind run. Build/link errors, signal abort, timeout or a missing tool/log are not a PASS. No deviating leak thresholds because of the historical byte counts.
- Still 0/0 B at 200 and 400 runs. Additionally check a longer repeated run with a bounded population after warm-up, to discover references that are permanently reachable but grow uncontrolled. RSS alone is not clear proof of a leak; use live allocations after the complete owner teardown.
- WASM: correct contents, alias semantics, scope ends and growth for the new cases. Check pointer strides/descriptors explicitly in wasm32. If the environment is missing, report it as a pending platform acceptance.
- Because of the shared compiler paths, finally check the affected 9rooms scenarios (mana map, event log list, repeated model/container updates) with the actual deployment flags; determine the concrete app/browser runner at implementation time. Do not derive a browser acceptance from a node test alone.
- Carry out changes to runtime signatures synchronously in native and WASM registration/exports. Do not test or ship newly built compilers against an old runtime.

## 4. Done criterion and planned deliverables

- [ ] P0: permanent repros, a fresh baseline and unchanged historical evidence.
- [ ] P1: the first wrong B1 layout/descriptor path demonstrated and corrected; native/WASM stride rules consistent.
- [ ] P2: insert secures its own element ownership before growth/shift.
- [ ] P3: shared map teardown correct for all allowed key types.
- [ ] P4: remove and replace free all supported value kinds correctly, including self-alias.
- [ ] P5: pop results are taken over or dropped exactly once, on all tested exits.
- [ ] P6: new cases GREEN; previous GREEN cases without regression; platform/integration status documented.

Delivery: small coherent compiler/runtime changes per package, permanent repros with content checks, registered leak/safety gates and a final before/after report. Only tick off a package status once its tests have been run. No claim to solve all memory errors of the language or all still open spec 61 migration points with this.


## 5. Review of 2026-09-29 and existing proof tests

**Review corrections:**

1. **P1/P4 dependency:** the original P1 acceptance already required complete self-put leak freedom, although only P4 corrects the ownership balance on replacement. P1 removes the layout/safety cause; the self-put case only becomes finally GREEN after P4.
2. **Gate statement:** the existing runner reports RED, but returns no error status for it. The added targeted runner checks only the target contract and ends with non-zero on every leak, safety/build/assertion error. Historical failures are metadata, not allowed XFAILs. Complete logs are kept.
3. **Store contract complete:** changes to the blanket move suppression affect append AND update as well as key temporaries not taken over. Both branches must be switched over together to the same acquisition/transfer contract.
4. **Status of foreign work:** 86/87 are now GREEN, 88 was newly created independently. These tests and changes are kept; the older RED reference was corrected.
5. **Coverage is not the overall migration:** the new minimal tests prove the six original findings and secure controls; in addition B7 came about (model get borrow crash). The larger alias/WASM/integration test matrix in P1–P6 remains part of the later implementation. P0 as a whole stays open until the complete existing suite baseline.

Permanent sources: [container leak tests](../tests/lifetime/container_leaks/README.md).
Measured results and binary hashes: [review findings](analysis/container_leak_audit_2026-09-29/README.md).
The new sources contain content/size assertions. Leak freedom is checked separately by Valgrind; a normal exit 0 explicitly does not prove it. The default run tests N=200 and N=400 with constant string payloads, so that lost bytes can be compared without a change in string length.


### Review addendum B7: map model get as a borrow

The added field read assertion `decl [view, ref<Item>, {call: [index.get, "key"]}]; view.text` already crashes after a single put, without remove/replace. Valgrind reports an address made of the text bytes (`payload=...`) in `strcmp`, not a leak. Permanent isolated test: `map_model_get_borrow.aiml`. The original B5/control tests keep only their size assertion, so that the earlier get crash does not prevent the leak proof.

**P1b, before the complete P4 content acceptance:** per §5.4, access to a value model slot must return a borrow view; binding to `Model` materialises per §7, binding to `ref<Model>` keeps the slot address. Check the map get dispatch in `ast_compiler_call.cpp` and `map_impl.cpp::get` for whether a by-value struct load is used as a slot address. Demonstrate the exact correction with the generated IR, do not change the return type the same way across the board for scalar/string/container get results. Regressions: ref binding + field read, value binding + materialised owner, unchanged scalar and owning handle reads; do not hold a borrow across invalidating map mutations. B7 is moved to GREEN separately. It is an additional safety precondition for the model content assertions required in the plan, not proof of further lost bytes.
