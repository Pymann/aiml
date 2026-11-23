# Spec 61: implementation plans for handle SHARE, return ownership and deserialisation

As of: 2026-09-25. AIML HEAD read: `a281adb` (list alias fall-through and self-move protection). **Plan, no implementation.** Historical leak figures come from the linked probes; verify with a freshly built compiler before implementation.

- [Round 6 findings and probes](../../9rooms_v2/leak_probes/round6_general/README.md)
- [Round 5 deserialisation finding](../../9rooms_v2/leak_probes/round5_interpolation/README.md)
- [Spec 61](../spec/61_memory_management.md)
- [Existing inline value model plan](spec61_inline_value_model_release_plan_2026-09-24.md)

## Starting point and shared rules

`a281adb` now keeps the necessary fall-through drop for list aliases. Its DeepCopy protection for a local container that is written back into a container field prevents the observed self-move crash. It does not yet implement the SHARE required by §5. Only replace this protection once a real handle install and self-slot/alias tests are green together. Finding 2/JsonStyle and finding 5/list alias are not work packages to be implemented again; their tests stay regressions.

Binding rules:

- Containers are pointer-sized handles everywhere (§5), in model fields too. Ordinary assignment shares the same container; the source stays valid.
- Secure-before-release and self-slot no-op (§5.5). A real MOVE empties its source ownership at runtime before the target drop.
- An ordinary string/copyable model return gives the caller ownership. `producer` additionally guarantees freshness/outer independence and is not a precondition for a drop (§4/§7/§8/§9.2).
- Model copy is a recursive copy/retain by field class. Container fields stay shared; deep copy only with an explicit clone contract (§7).
- Distinguish a borrow (`ref<T>`) from an owned result. The LLVM pointer type, a function name suffix or origin from a call alone prove no ownership.

## Plan 1 — round 6 finding 4, then finding 1 and finding 3

### Goal and delivery order

Three separate implementation commits with their own green regressions. **P1-B/C are not ready for implementation for now:** first fulfil P1-R (working up the revert, spec clarification and client gate). P1-A also gets the behaviour gate named below:

1. **P1-A: real handle SHARE** for `set: [box.items, src]` — bounded and deliverable first.
2. **P1-B: two-sided string return contract** — caller and callee in one atomic change step.
3. **P1-C: two-sided value model return contract** — uses the result classification from P1-B and copy/retain/drop from the existing model plan.

P1-A does not need P1-B/C. P1-C requires correct recursive retain/drop helpers; add missing field classes to their shared helper first, do not introduce special copies per return site.

### P1-R — mandatory precondition: secure the old revert and client behaviour

**Added review:** the original plan did not sufficiently take into account the earlier discarded string return classifier and the required client acceptance. [Inline model plan §10.1 B](spec61_inline_value_model_release_plan_2026-09-24.md) documents the regression **deck → card editor despite green leak/sanitizer tests**. In the current `ast_compiler.cpp` `classifyOwnedStringReturn` is still deactivated (`[[maybe_unused]]`, commented-out call sites). The separate memory entry `[[owned-return-and-pop-clear-leaks]]` was not found directly for this review; the finding is confirmed via the existing repository document and the deactivated call sites.

The declaration-based, two-sided return contract proposed here is conceptually something different from a producer auto-detect derived from function bodies: ownership and freshness are separated. **That does not prove, however, that the earlier regression is ruled out.** A complete rebuild only fixes mixed ownership conventions, not faulty lifetimes or changed client behaviour. Do not reactivate auto-detect in passing.

Before P1-B/C:

1. Reconstruct the old attempt/revert and the affected navigation functions. Per failed path, show the previous and the planned retain/drop behaviour including parameters, members, event/callback use and imports. If the old patch cannot be reconstructed, leave this evidence gap explicitly open.
2. Build a repeatable real client scenario on the previously working state: start/login with isolated test data → deck list → deck editor → open a card → card editor → back; check the selection, the right card/deck ID, contents, changes/saving and back navigation. Navigate back and forth several times, wait for asynchronous responses completely. Additionally lobby → match → back, as far as the changed return paths are involved there.
3. Demonstrate that the test detects the error back then, preferably against the archived faulty build. If that is not available, the historical detection capability stays unproven; a deliberately failing navigation/identity check only validates the test mechanism.
4. Measure memory development in the **same client scenario**: fixed fixture, warm-up, the same completed navigation cycles, compare after short-lived screens have been dismantled. Use live AIML allocations/references or allocator live bytes; WASM linear memory capacity or RSS alone prove no leak, since the memory does not necessarily shrink. Document caches and deliberately long-lived screens as owners. Allow no cycle-proportional residual growth for the paths examined; fix the measurement method and any baseline interference share in advance.
5. Browser/WASM build of the real 9rooms client with `exodus-client.json` and a matching freshly built WASM runtime, then the scenario above. The existing `aiml/run_wasm_tests.sh` is an additional node/WASM check, **not a replacement** for browser navigation, rendering and asynchronous client integration. The concrete build/browser runner is to be verified before implementation; no already existing automated UI gate is claimed here.

**Spec before P1-B/C:** §8 says explicitly about copyable value/container returns that string/container/shared fields are shared into the caller and callee locals drop regularly. That covers the normal model return. For a standalone plain `string` return the previous rule is derived from §4 plus §8; an equally explicit rule line of its own is missing in the text read. Before implementation, record explicitly in spec 61: "A `return: string` gives the caller an owning string reference; an existing string is shared, an owned temporary can be taken over; without `producer` no freshness is guaranteed." Likewise align temporary drop/passing on and the separate producer contract. That is a planned spec clarification, not a spec change already made in this review. No automatic equating of owned and producer.

Until these preconditions are fulfilled, the existing targeted annotation strategy remains the operative approach. A new producer annotation is only allowed with proven freshness/independence; it must not relabel a mere shared field return. P1-B/C remain the planned systematic fix, not a released broad auto-detect rollout.

**P1-A behavioural risk:** switching from the previous move/DeepCopy to SHARE changes visible alias semantics. Inventory affected stores and subsequent mutation of the source in the game **and the client**. Whoever needs real independence must use `copy` or `clone` explicitly according to the domain contract; do not automatically clone all callers to preserve a compiler bug. In particular check the deck/card editor and the self-alias writeback cases with content/mutation assertions. A pure 0-byte gate is not enough for P1-A either.

### P1-0 — baseline and regressions before code changes

- Rebuild compiler/runtime; record commit, flags, binary hashes and source state together. Do not overwrite existing before logs.
- Re-measure round 6 `list_member_set_10/1000`, `list_member_set_return_1000`, `list_member_set_read_1000`, `return_plain_10/1000`, `return_producer_1000`, `box_plain_10/1000`, `box_producer_1000`. Historically: 40 B/set, 20 B/plain string return, 13 B/plain TextBox return; concrete allocator sizes are not permanently fixed semantics.
- Create permanent tests in `tests/lifetime/`; choose free numbers at implementation time. Proposed names: `container_member_share`, `plain_string_return_owned`, `plain_model_return_owned`. Analysis variants/logs stay in `9rooms_v2/leak_probes/`.
- Carry these cases as open RED cases in the existing `tests/run_lifetime_leak_gates.sh` and move them to GREEN with the respective fix. The normal lifetime suite must not accidentally take open red probes in as expected green tests: until then keep them in a subdirectory that is not run automatically, or exclude them explicitly.

### P1-A — handle SHARE in the member store

**Change sites:** `src/core/ast_compiler_stmt.cpp` (`visitSetStmt`, container decl/release helpers), expression/property resolution, `src/core/ownership_analyzer.cpp`, `ownership_flow.h` and the associated transfer evaluation.

1. Determine the target type and the planned slot resolution before the previous blanket `consumeLocalDrop`. Only hold a concrete slot pointer across RHS evaluation if its lifetime and address demonstrably stay stable during it. For memV3 class 3 targets plan an explicit `Transfer::Share`; the normal named source keeps its owner status and its drop. Replace the `DeepCopy` workaround from `a281adb` only for these now correctly implemented stores.
2. Evaluate the RHS once and normalise it as a **handle value**. For named containers load the handle from the slot; for fields respect the existing receiver/slot distinction. Additionally mark owned call/read temporaries. Per §5.4/§5.7 `at/get` of a string/container element returns an owning handle; its retain must happen exactly once, at the read or at its directly combined materialisation. Take over an already retained read result in the install, do not incref again. An ignored read must also drop the ownership it acquired. Never interpret a `{size,cap,data}` value or its address unchecked as a handle.
3. For `box.items` get the slot with `resolvePropertyWritePtr(..., wantContainerSlot=true)`. In this memV3 path a struct result is a codegen contract error: a diagnostic instead of falling through into `Struct-content copy`.
4. Emit the assignment via a shared type-driven handle install helper. Use element classes, recursive descriptors and strides from the existing type classification/DataLayout; no hard-wired element size 8.
5. After a successful install return from this path. Legacy memcpy and automatic DeepCopy branches must no longer take it over.

Proposed new interface (sketch, not an existing helper):

```cpp
struct ContainerResult {
    llvm::Value* handle;
    llvm::Value* sourceSlot;  // optional, identifies storage; not the referent
    ReferenceOwnership ownership; // BorrowedReference or OwnedTemporary
    ResolvedType type;
};

emitInstallContainerHandle(destinationSlot, result);
```

Install order:

```text
evaluate the RHS exactly once; check the resolved slot identity
identical source/target slot -> no-op
named/borrowed reference -> retain(newHandle)
owned temporary -> secure ownership and empty the source slot/live flag
release(oldHandle, complete type metadata)
store newHandle in destinationSlot
consume the taken-over temporary from cleanup
```

P1-A must not treat an unknown normal container call as an owned temporary merely because of its syntax. For supported call paths normalise the actual return ownership; a legacy borrow not yet migrated needs an explicit retain adapter. A change of its return contract must cover caller and callee together. The statement that P1-A can be delivered independently applies to the named SHARE core; additional call paths are only released with a verified boundary contract.

With two different slots holding the same handle, retain-before-release stays correct. Do not use pointer equality of the referents as proof of a missing ownership obligation. Do not register an additional scope drop at every assignment; the existing target drop reads the current slot. When extending generally to `idx`, do not cache the target slot across a container reallocation triggered by the RHS.

**P1-A acceptance:**

- Set/set+read probes: 0 definite/indirect, no invalid access/free errors.
- `src.push` after the assignment changes `box.items.len`, and vice versa; test beyond the original capacity in each case. That proves shared header/growth semantics.
- Same slot, the same handle in different slots, alias writeback `tmp=box.items; box.items=tmp`, a filled old target, repeated rebinding and a temporary RHS.
- `test_class_list_struct_scalar_writeback` and `56_list_share_alias_no_leak` stay green; they protect the commit already delivered.
- Check the same install rules specifically with `list<string>`, `list<list<i32>>`, `map<string:string>` and `buffer`. Additionally share `list<ptr<Node>>`: the container stays a shareable handle even if its elements are move-only (§5.1). The boxes may only be freed at the last container owner; no element copy and no DeepClone on SHARE. Releases must handle last owners and elements correctly.

### P1-B — ordinary string return with one caller reference

**Change sites:** `ast_compiler_stmt.cpp` (`visitReturnStmt`, string decl/set), `ast_compiler_call.cpp` (free calls and methods), function/interface metadata in `context.h`, ownership/temporary management. Multi-file imports and indirect/interface dispatch must also know the same contract.

**Decision:** an internal AIML function with a `string` result returns a caller-owned reference in principle; `producer` is an additional independence property. Classify builtins/FFI by their actual contract and normalise with a retain at the adapter where needed. An externally delivered unmanaged `char*` is not a provably valid AIML string handle.

Introduce a result classification based on the resolved function symbol, e.g. `OwnedStringReference`, `OwnedValueAggregate`, `BorrowedReference`, `Scalar`. This information must be available across declarations/imports. No longer use `stringProducerFns` or `ownsReturnMatches(shortName, ...)` as the sole proof of ownership for normal value returns. Methods of the same name must not inherit foreign contracts.

**Callee:**

- A demonstrably owning interpolation/builtin/call temporary: return the existing reference and consume its drop. Take ownership from the result record, not across the board from `StringInterpolationExpr`: an interpolation without conversion/concatenation can internally just pass on an existing string and then needs a retain like a borrow.
- A named local string, parameter or model field: secure a reference for the result before scope cleanup. A correct simple reference implementation: incref for the return, then the regular local drop. With a shared persistent owner its reference stays.
- Literal/immortal empty string: the same contract with a no-op retain/release; no new allocation necessary.
- Secure the result before all local drops; keep freeing interpolation intermediate results and other local fields. The current producer condition at `retStrFieldOfOwnedModel` must no longer exclude normal returns from the necessary retain/owner cleanup.

**Caller:**

- Register the result as an owned string temporary, regardless of `producer`.
- `decl`/`set` takes over exactly this reference; no additional incref as currently for a supposed borrow.
- A return not bound or only used transiently is dropped after the end of its use. Condition/return expressions and nested calls need cleanup even without ordinary statement fall-through.
- Change both sides in one commit. Switching on only the caller registration can over-release returns borrowed so far; switching on only the callee retain can create new leaks.

**P1-B test matrix:** fresh return with/without producer, literal, parameter, local string, field of a local model, field of a longer-lived owner, chained wrapper; use in decl, rebind, interpolation, log/print, ignored call, condition and renewed return. Include methods/interfaces and two files. An ordinary field return stays allowed without producer; do not establish the workaround as a new language obligation.

**Acceptance:** plain and producer variants both 0 bytes; contents correct; the returned string outlives local callee owners while their sibling fields are freed. Do not claim a new producer guarantee for shared field returns.

### P1-C — ordinary value model return with a caller drop

**Change sites:** `ast_compiler_stmt.cpp` (`modelOwnedInit`, model decl/set/return), `ownership_analyzer.cpp::freshOwned`, the result metadata from P1-B and shared recursive CopyRetain/drop helpers. Take the return ABI into account (struct by value/indirect result area).

**Decision:** copyable model returns are value copies of their own whose RC fields are shared (§7/§8). First implement an unambiguous CopyRetain reference path; move elision only afterwards, as long as the complete ownership behaviour stays identical.

1. For a **copyable** model the callee builds an independent result value by member-wise copy/retain **before** its locals drop. Copy scalars; retain string/container/shared fields; handle nested aggregates and arrays recursively. Copy `ref` fields only as weak pointers. Handle class 5 resources (`chan`, `lock`) and `shared<T>` with their atomic retain/release; do not free class 6 singletons and real borrowed handles recursively (§2.1/§6.1).
2. The caller takes over the complete owning result value. `modelOwnedInit` and the flow analysis must include normal model value calls; no restriction to `owns_return`/producer. Real `ref<Model>` returns stay non-owning.
3. Decl with an initial value: do not create an unnecessary default heap that is overwritten afterwards. If already initialised, drop the existing ownership correctly before replacement. A later `set` assignment: secure the new result completely, drop the old value, take over the new one.
4. Manage model temporaries typed. An ignored model return or one only used as an argument needs a recursive drop; the plain string temp list is not enough for that. On a renewed return pass the result ownership on correctly. In the CopyRetain reference path an owning RHS temporary is recursively dropped exactly once after the new result references have been built; on actual adoption its cleanup is consumed instead. Also test this distinction for `return make_model()` and `return clone(...)`, otherwise the intermediate value can leak despite a correctly dropped final result.
5. Do not duplicate move-only models through CopyRetain: a separate MOVE with runtime empty of the owning fields or a checked drop flag. Keep borrow/move-only negative tests.

**P1-C test matrix:** TextBox minimal probe; model with a string and a list field; return of a parameter; return of a local value; several return paths; direct field/argument forwarding; ignored result; rebinding; nested models/arrays; let an alias survive; move-only model and a real borrow return as counter-controls. For the copyable model, scalar fields are independent, container fields must see the same mutation. Deep copy would be wrong despite leak freedom.

**Acceptance:** `box_plain_*` and `box_producer_1000` each 0 bytes. A caller drop of its own must not free a model graph that the callee borrowed without securing it. Therefore deliver callee and caller changes together here as well.

## Plan 2 — round 5 finding B: deserialisation → value model (deferred)

### Status, goal and delimitation

A separately executable plan; **still deferred** until it is selected for implementation. Documenting it does not lift the deferral. The goal is the safe takeover of `sys.deserialize(json, "Model")` into an inline stored model value, including repeated assignment and release of the internal result block.

Historical control: `original_100` and `deser_only_100` both lose 28,900 bytes; original case 289 bytes (48 direct, 241 indirect). Interpolation is not the cause of this finding. A new baseline is needed: do not treat the old measurement as proof for every later build.

**Change sites:** `serialize_impl.cpp::emitDeserializeStruct` (malloc result block), `SerializeImpl::deserialize`, `ast_compiler_call.cpp` (`sys.deserialize` dispatch), `ast_compiler_stmt.cpp` (decl/set with a heap pointer RHS), typed temporary/ownership metadata.

The current outer result block is an internal malloc carrier for model fields, not automatically a public `ptr<Model>` or RC model. Its release must follow from this concrete allocation origin. Never free borrowed KV/view pointers in the neighbouring assignment path as such result blocks.

### P2-0 — a contract of its own and red tests

- Run the existing round 5 probes freshly; keep the original test, deserialisation without interpolation and the pure manual construction control.
- Prepare a new regression `deserialize_value_model_replace` in the lifetime suite: direct decl initialisation, separate decl+set, a thousand rebindings of the same variable, nested strings/lists, an empty model, use after takeover, normal scope/return exit.
- Test a JSON RHS from a string field of the previous target model. The old target content may only be freed after complete evaluation/deserialisation. The original JSON input must stay alive as long as the decoder reads it; its origin is not a move signal.
- Record the existing behaviour on invalid JSON separately; do not combine this ownership fix with new parse error semantics. Successfully created partial objects must also be freed on existing error paths.

### P2-A — track the result origin typed

The proposed expression result type `OwnedDecodedValue` contains the model type/descriptor, a pointer to the internal malloc carrier, the cleanup state and the allocation kind. Register exactly at the `sys.deserialize` dispatch for the affected value model variant; classify primitive deserialisation, container root, `ptr<T>` and RC model paths separately.

No global `lastCallWasDeserialize` flag: nested expressions and several results must stay independent. Do not mark all pointer RHSs or all `producer` results as malloc carriers across the board. If P1-C already provides typed model temporaries, reuse this infrastructure; otherwise implement a bounded typed result record. P2 does not depend on P1-B's string ABI change.

### P2-B — reference implementation for copyable models: CopyRetain, then free the result

Before choosing this path, determine recursive copyability per §7. It is allowed **exclusively for copyable models** and is to be implemented and tested first for the existing round 5 cases. A direct `ptr`/class 4 field or a move-only inline aggregate rules out CopyRetain. A `list<ptr<T>>` field, by contrast, is itself a shareable container and does not make the surrounding model move-only (§5.1/§7).

For move-only models supported by the deserializer implement P2-C directly as the necessary transfer path; there it is not a mere optimisation. Do not replace it by an implicit clone and do not forbid previously allowed model types across the board just for the leak fix. Which resource types are deserialisable at all is determined by the existing serialisation contract, not by spec 61 alone.

Flow for the copyable case:

```text
1. Evaluate the complete owned deserialisation result.
2. CopyRetain its model fields into an independent intermediate value (§7).
3. If the target is already initialised: drop its previous owned fields.
4. Take the intermediate value over into the target; the target owns its references.
5. Drop the deserialisation result recursively and free its malloc carrier.
6. Consume the temporary cleanup; the target has exactly one scope drop.
```

The CopyRetain references secure the new fields against step 5. This creates no deep container copies. On a fresh decl initialisation omit the old target drop if the target is demonstrably still uninitialised. With a previous `decl state`, by contrast, really free its default containers. Without an effective later target drop the new graph stays leaky despite a freed carrier and a correct rebind.

### P2-C — transfer for move-only models; optional optimisation for copyable models

For supported move-only decode results this path is mandatory; for copyable models only optimise after a green reference path. Precondition: a fresh, exclusive compiler temporary; no observable source owner and no source within the old target graph.

```text
1. Evaluate the result; take the fields into a secured transfer value.
2. Empty the source ownership at runtime (type-appropriate sentinels or a runtime drop flag).
3. Drop the old initialised target value.
4. Move the secured transfer value into the target.
5. Only free the empty malloc carrier; consume the temporary cleanup.
```

No recursive release of the already transferred fields in step 5. No `free` of a string handle. `memset(0)` is not a universal empty for resources; class 4 fields need their respective invalid states. This optimisation changes no normal model copy semantics and consumes no named user variable.

A later deserialisation directly into a result area would be a further separate optimisation step. Do not decode directly into the still live target: that would bypass the secure-before-release/error path guarantees.

### P2-D — cleanup and counter-controls

- A non-consumed decode temporary: recursive field drop plus release of the outer malloc block.
- In the CopyRetain path: drop the decode temporary completely and remove its cleanup afterwards; the target holds the separately retained references. In the transfer path: no recursive release of the transferred fields by the source, only free the emptied outer carrier. Both variants need an unambiguous consumed cleanup state.
- Unchanged KV/borrow sources: no `free` of the source block; on normal model materialisation CopyRetain instead of an automatic deep copy.
- Repeated set on a filled target: free the old list including its elements, keep legitimate foreign RC aliases.
- Test normal returns from the surrounding function and ignored decode results. If expression forms not supported so far occur, diagnose them explicitly instead of freeing them untyped.

**Plan 2 acceptance:** the original test and the deserialize-only probes each 0 definite/indirect; no invalid reads/writes/frees. The rebind loop does not grow with the number of calls, contents/nested data stay correct. An alias onto old RC fields survives a rebind until its last reference drops. Heap pointer/borrow/RC model controls keep their own release contract. Only after that move the new regression test to GREEN.

## Shared integration and definition of done

Per package, first run focused positive/negative tests and the matching probes, then the relevant existing gates. For the final integrated compiler changes, from `aiml/`:

```sh
AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1 bash tests/lifetime/run_lifetime.sh
bash tests/run_lifetime_leak_gates.sh
bash tests/run_memory_sanitizers.sh
bash run_aot_tests.sh
bash run_wasm_tests.sh
bash run_all_tests.sh --no-build
```

**Additionally mandatory:** a real 9rooms WASM client build and the combined browser behaviour + memory gate from P1-R. The native and node/WASM scripts above do not cover this gate. A client check that cannot be run means missing acceptance for P1-B/C, not implicit success.

Before `--no-build` the build must actually contain the changes. Only start the general corpus once at a time; no parallel runs with shared test paths. Confirm existing known failures on the same baseline state and report them separately, instead of using them to excuse new failures across the board. Memory sanitizer success alone is not a leak acceptance: the hard leak gate requires 0 definite + 0 indirect and no Valgrind access errors.

- On return contract changes rebuild all dependent AIML objects/modules. No mixing of old borrow conventions and new owned return conventions; a physically identical pointer/struct ABI does not mean a compatible ownership contract. Check build cache/interface versioning and invalidate where necessary.
- Do not automatically restart running game/HTTP/media servers. Compiler and test acceptance first; integration of the services as a step of its own.
- Per package, file new logs with compiler/runtime hashes, a snapshot of the tests and a results table. No unproven transfer of historical leak figures to the new build.
- Success does not only mean 0 bytes: observable SHARE semantics, valid borrow lifetimes, no double frees and correct source validity are part of the acceptance.

**Recommendation:** deliver P1-A with behavioural acceptance first. P1-B and P1-C only after P1-R is fulfilled and the spec has been explicitly clarified, then each closed on both sides. Plan 2 stays an independent follow-up package; share its typed temporary management with P1-C where possible, without blocking the three round 6 fixes until the deserialisation repair.


## Renewed review against spec 61 — 2026-09-25

Checked against the complete relevant ownership rules; spec SHA-256: `0b5194fe5b0af1fa79fb64b9e82b35b0f547af60173406713d13f94daa43903a`. Code reference state `a281adb`. The spec was not changed. The work packages above already contain the following corrections.

| Review point | Spec basis | Result / change |
|---|---|---|
| Real container SHARE and order | §5, §5.5 | Conformant; slot lifetime during RHS evaluation made more precise. No move/DeepCopy of a named class 3 source. |
| SHARE with move-only elements | §5.1, §7 | Test added: `list<ptr<T>>` stays shareable. Container sharing does not duplicate elements. |
| Ownership of `at/get` results | §5.4, §5.7 | Made more precise: a container/string read gets an owning reference; take over an existing retain, do not double it. |
| Normal string/model returns | §4, §7, §8, §9.2 | An owned caller result without producer is correct. Producer stays an additional freshness obligation. For plain container calls the P1-A dependency was narrowed. |
| Passing on a model temporary | §6.1, §7, §8 | Cleanup of the intermediate value explicitly separated by CopyRetain and adoption. |
| CopyRetain of the decode result | §7, §8 | **Essential gap corrected:** P2-B only for copyable models; P2-C mandatory for supported move-only models. |
| Recursive member handling | §2.1, §6.1, §7 | Class 5 retain/drop atomically; do not own class 6 and borrow handles. |
| Deserialisation carrier and target drop | §6.1, §7; concrete allocation in the current code | The internal malloc block is an implementation detail. CopyRetain and transfer cleanup separated; no release of borrowed KV source blocks. |

**Additional acceptance tests from the review:**

- Nested container `at/get` as the RHS, as an ignored expression and as a passed-on return: no missing or double retain.
- String return of an interpolation that internally just passes on an existing string, including an empty literal/empty result.
- `return make_model()` with an owning intermediate value: no residual graph after callee/caller cleanup.
- As far as supported by the decoder: a model with a directly owned `ptr` field (MOVE necessary) versus a model with a `list<ptr<T>>` field (CopyRetain of the container allowed); both without double ownership of the elements.
- Negative borrow return counter-probe: `ref<Model>` to a callee local is still rejected (§6.2/§8). Ownership normalisation must not bypass this diagnostic.

**Limit of the review:** spec 61 defines the memory rules, but neither the concrete temporary data structure nor all FFI/JSON type and error contracts. Their technical design is marked as an implementation decision and must be verified against the actual backend/builtin contract. The corrected plan is compatible with the rules checked; this statement is not a confirmation of an already implemented or tested solution. Plan 2 stays deferred.


### Review correction: release status

The preceding statement "compatible with the rules checked" assesses the desired memory semantics, not the safety of the broad rollout. After taking the historical client revert into account: **hold back P1-B/C until P1-R is fulfilled.** The explicit plain string spec rule and a real client/WASM behaviour plus memory acceptance are open preconditions. P1-A stays the priority, but also needs behavioural evidence. Plan 2 stays deferred unchanged.


## Implementation addendum 2026-09-25: P1-A

P1-A is implemented in the C++ code: named container RHSs normalised as handles, `Transfer::Share`, the source keeps its scope drop, retain-new before recursive release-old and store. The additional map/buffer fall-through drop was aligned with the registered handle drops. Legacy struct builtins keep their previous path.

New regression tests: `tests/lifetime/61_member_handle_share.aiml` and `62_member_share_recursive_release.aiml`, both in the leak gate and in the WASM test list. Original list member leak: 40,000 → 0 B per 1,000 calls. The extended test additionally proved 184,000 B of map local loss in the first SHARE intermediate state; after correction also 0. Nine hard leak gates and seven WASM tests passed. Complete evidence and limits: [probe report](../../9rooms_v2/leak_probes/round6_share_fix/README.md).

P1-B/P1-C stay open; the return probes are still red. No general classifier reactivated and no new return contract introduced. The user uses `run_web.sh` with client heap logging; that does not yet replace a confirmed navigation comparison. Round 5 B/P2 stays deferred.
