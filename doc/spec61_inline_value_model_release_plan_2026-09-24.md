# Implementation plan: safe release of inline list<Model> elements

As of: 24.09.2026. Status: plan, no implementation. Target configuration: `AIML_MEM_V3=1`, `AIML_LIFETIME_DROPS=1`. Source locations refer to the working state read while writing this, not exclusively to HEAD.

## 1. Decision according to the current spec

The wording "needs escape analysis (deep-copy views)" describes the historical workaround, but no longer the correct target contract. Spec 61 §5.4, lines 749–764, explicitly requires:

- Binding to `Model`: an own inline value copy whose string/container/shared fields are retained. No recursive duplication of the containers. The copy survives removal and reallocation of the outer list slot.
- Binding to `ref<Model>`: a real non-owning view of the slot. After it is freed or after a relevant shift/reallocation it must no longer be used.
- Move-only model (ptr/unique resource field, also transitively): no implicit value copy. Use a borrow, an explicit clone as far as clonable, or an owning extraction.
- `remove`/`clear`: release element ownership. `pop`/`take`: move ownership into the result; release a discarded result at the end of the expression.

**Recommendation:** establish ownership correctly at value materialisation and analyse borrows separately. No automatic deep clone of `ref<Model>` before a mutation: that would change the meaning of an explicit view. Deep independence stays reserved for `clone`.

This means that a complete interprocedural escape analysis does not have to be finished before unambiguously owning inline elements can be released safely. Locally recognisable invalid borrows need diagnostics; unknown external effects are an analysis boundary to be handled explicitly, not a licence for automatic copies.

## 2. Current C++ starting points

| File / place | Finding / planned change |
|---|---|
| `src/core/context.h:58–65` | `Symbol::is_view_alias` is a syntactic bool, without owner/invalidation state. Do not keep using it as the sole source of ownership. |
| `src/core/ownership_flow.h` | `OwnFact`, `Origin`, `Transfer`, `OwnershipPlan` exist. Represent value materialisation and borrow provenance here explicitly. |
| `src/core/ownership_analyzer.cpp` | Extend the flow analysis; take alias chains, branches and loops into account. |
| `src/core/ast_compiler_stmt.cpp:1302–1332` | Contradictory old comments and view marking for value declarations. Replace with type-driven materialisation. |
| `src/core/ast_compiler.cpp:6004ff` | `emitLValueAddress`: address of inline stored elements; basis for a real `ref<Model>` and the copy source. |
| `src/core/ast_compiler_stmt.cpp:2237ff,2382ff,3554ff,4570ff` | Consolidate old deep copy special paths for set/return/idx against the current share contract. |
| `src/core/ast_compiler_stmt.cpp:4790–4827` | `store` already has bit copy + `aiml_ser_retain`; the order is currently release before copy/retain, so check it for sub-object/indirect aliasing too. |
| `src/core/ast_compiler_call.cpp:1545ff` | Value model push: use the same copy/move classification; no blanket deep copy decision. |
| `src/core/ast_compiler_call.cpp:1850–1861` | Explicit deferred comment at remove; clear without element type release. Activate only after value copies are secured. |
| `src/core/list_impl.cpp:129,240,327` | pop, clear, remove: separate raw memory operations from ownership actions. |
| `src/sys/ser_runtime.cpp:802–844`, `ser_runtime.h:140` | `aiml_ser_retain` exists. Use it as the basis for member-wise retain, check type coverage. |

Line numbers are navigation aids; function names remain the authoritative anchors. The existing deferred comment is not a binding design decision with respect to the newer spec.

## 3. Shared C++ primitives first

Declare in `ast_compiler.h`, define in the matching compiler implementation module. The names below are proposals, not existing APIs:

```cpp
enum class ValueTransfer { CopyRetain, Move, ExplicitClone };

// Creates a secured owning value in temporary storage.
// Checks copyability; evaluates the source exactly once.
llvm::Value* prepareOwnedModelValue(Expr* source,
                                   const std::string& modelType,
                                   ValueTransfer transfer);

// Releases fields, never the inline model slot itself.
void emitValueModelRelease(llvm::Value* address,
                           const std::string& modelType);

// Publishes already secured ownership at the target; no further retain.
void commitOwnedModelValue(llvm::Value* destination,
                          llvm::Value* prepared,
                          const std::string& modelType);
```

CopyRetain lowering, schematically:

```cpp
// src is still valid; tmp owns no previous resources.
memcpy(tmp, src, sizeof(Model));
aiml_ser_retain(tmp, modelNodes, nodeCount);
// Only after the retain may old target ownership disappear:
aiml_ser_free_decoded(dst, modelNodes, nodeCount);
memcpy(dst, tmp, sizeof(Model));
// tmp was moved into dst: no second drop.
```

On initialisation, release(dst) is omitted; on an exact copy self-assignment the whole operation may be omitted. With overlapping/indirectly aliasing sources, the temporary backup stays necessary. Do not dereference a source address again after a possible target drop or list realloc. Move needs the same protection plus timely invalidation of the source ownership; transfer out of a merely borrowed view is forbidden. Move self-assignment and the case that the source lies in a target sub-object to be released also get their own tests.

`aiml_ser_retain` must be audited before use across all permissible fields: strings/path, container handles, shared, class 5 resources, embedded value models and arrays. `ref` stays weak. Unique fields do not become copyable through retain. Missing descriptors or unsupported ownership classes must not silently fall back to a shallow memcpy. Add type-specific retain/release emission if needed; serialisation alone is not a complete lifecycle contract for OS/GPU resources.

## 4. Unify value materialisation

Switch to the shared primitives in this order:

1. `decl: [copy, Model, at/get(...)]` and a copy from another value local: CopyRetain, own OwnershipId, normal scope drop.
2. set, member assignment, `store` into an out parameter and idx replacement: secure the source, release old ownership, take over new ownership.
3. Return by value: materialise a borrowed source, move an own source according to the transfer plan. The callee delivers exactly one ownership; the caller adopts it. No additional unbalanced retains.
4. push/insert/extend/slice and further element-copying operations: CopyRetain or Move by type and transfer plan. For a copy, secure before a possible realloc of the source list (`list.push(at(list,i))`).
5. Temporary results: uniform registration and consumption. A `pop` result must neither disappear nor be dropped twice, even with call nesting or when discarded.

Semantic tests must distinguish: `copy.cards.push(...)` changes the jointly held container; `copy.scalar = ...` or replacing `copy.cards` changes only the copy. Whoever wants to change an inline slot itself binds `ref<Model>`. Game code that previously depended on unintended scalar write-through of a value copy must be adapted specifically.

## 5. Ownership and borrow analysis

Extend `Transfer` with an explicit retain-copy action; keep DeepCopy separate for explicit clone semantics. `Origin::SharedView` must not serve as a substitute for a materialised owning value. Distinguish the resource identity of shared fields from ownership of the outer inline aggregate: two aggregates can each hold the same container fields with their own rc.

Provide provenance for real borrows, for example:

```cpp
struct BorrowOrigin {
    std::set<ResourceId> ownerResources;
    // Additionally the element/member path and InlineSlot vs. pointee.
};
// Attach to ownership facts/expressions, not merely to variable names.
```

The data flow must support:

- Propagating alias chains and assignments; materialisation separates a slot borrow from an owning copy.
- At CFG merges, unite the owner sets; after a mutation on only one path, do not assume unconditionally valid. Analyse loops to a fixed point.
- For inline elements, model remove/insert (shift), clear, replacing the affected slot, owner drop and possible reallocation. Shared container aliases reference the same owner; a mutation via an alias invalidates too.
- Only object to a later use of an invalidated borrow; an already unused borrow must not prohibit remove wholesale.
- Summarise call effects in a typed way: may-reallocate/may-remove/may-retain, return borrow anchors. With unknown effects, no false safety guarantee; observe the existing spec diagnostics/migration rules and document the remaining limits.
- Borrow return only with a sufficiently long-lived anchor; reject a borrow on a callee local or a local temporary copy result.

No delayed automatic deep copy on "escape" of an explicit ref: either a value materialisation permitted by the target contract, or a diagnostic. That preserves the meaning of mutation through a view.

## 6. Activate element release

Only after the materialisation/transfer paths pass under tests:

- **remove(i):** evaluate arguments exactly once, enforce the existing bounds contract before memory access, release the removed model member-wise, shift the rest with the struct stride via memmove, decrement len. Do not release the last now-inactive slot copy again; optionally zero it without a drop. No free(slot).
- **clear():** release all active slots exactly once, then len=0. The memory reserve may remain. A second clear has no effect.
- **pop()/take():** move the inline value into an owning result, remove the slot from the active range, no element release in the transfer path. Release discarded results afterwards via a temporary drop.
- **idx/replace:** prepared source before releasing the old slot; the same shared primitives as set/store. No renewed deep copy as a special case.
- **final list drop:** release active elements exactly once; already removed slots stay invisible. Reconcile existing runtime descriptors with the same class rules.

Decision after review (§9.7): the runtime dispatcher `free_list_elem_at` is unified for remove/clear/pop-discard/final-drop; ListImpl/codegen passes the type classification and controls copy/move transfers. Do not establish a second independent release walker for remove, clear and final drop. Do not let both possible layers drop at the same time.

## 7. Tests before switching production on

New tests under `aiml/tests/lifetime/`; determine free numbers at implementation time. Real client reproductions continue under `9rooms_v2/leak_probes/`.

| Test case | Expectation |
|---|---|
| bind a value from at/get, remove/clear/drop the origin, read the copy | valid contents; both drops balanced |
| the copy contains string, list, map, nested model, array | recursive retain/release correct |
| mutation of shared container fields, check scalar/field replacement separately | share semantics and an independent outer value |
| several copies, alias chain, branch merge, loop | no leak/UAF, drop on every actually owning path |
| ref from at, inline remove/insert/realloc, then use the ref | negative diagnostic on locally visible invalidation |
| remove via a second list handle alias | same invalidation as via the original handle |
| value survives growth, a real ref is not used | positive control against an overly strict borrow diagnostic |
| remove first/middle/last element, clear twice | survivors correct, no double release |
| pop bound, discarded, passed on to a function | exactly one transferred ownership / one temporary drop |
| idx/store self-assignment and an overlapping source | secure-before-release, no destroyed source |
| push(at(sameList,i)) with forced growth | backup before realloc, valid content |
| copy a move-only model, also nested | compile error; ref/pop allowed |
| return and out param across a module boundary | caller/callee count ownership consistently |

Existing tests must be included: `tests/lifetime/45_store_view_outparam.aiml` (explicitly checks aliasing), `37_nested_replace_remove.aiml`, `neg/n08_invalidated_borrow.aiml`, `neg/n13_move_only_value_copy_from_at.aiml`.

9rooms regression cases: `test/unit/test_effect_processor.aiml`, `test_effect_death_on_destroy.aiml`, `test_mana_pick.aiml` and `test/integration/test_enchantment_picker.aiml`. These secure exactly the game logic previously changed by deep copy.

Native assertion runs plus Valgrind under both enabled flags. Compare a small probe and 1000 repetitions: 0 definite/indirect losses in the isolated target path, no invalid reads/writes/frees. Handle reachable runtime registries separately. The existing `tests/run_memory_sanitizers.sh` uses `--errors-for-leak-kinds=none`; its safety PASS is explicitly not enough as leak acceptance. Additionally check leak numbers as a hard gate condition. Then a WASM compile/client smoke test for struct layout/stride and the full relevant AIML/9rooms suite.

## 8. Implementation packages and acceptance

1. **Contract + red tests:** document the current spec materialisation as the authority; create tests for share semantics, remove/clear and temporary pop drops. Mark the existing deferred comments.
2. **Shared retain/release/move primitives:** test runtime type coverage and alias order.
3. **Consolidate value ownership:** switch decl/set/store/return/idx and container copy sinks; remove old syntactic view heuristics for value copies. OwnershipPlan and codegen must use the same contract.
4. **Borrow analysis:** real ref provenance, mutation effects and negative tests. Do not cover up unknown cases with hidden deep copies.
5. **Switch on the remove/clear/pop lifecycle:** only now close the deferred gap; the leak gate plus the previous double-free regression cases must pass.
6. **Overall acceptance:** relevant AIML/9rooms tests, Valgrind, WASM; update comments/spec implementation notes. Do not accept broad drop suppression as a "fix".

The safety-critical dependency path is 1 → 2 → 3 → 5. Borrow analysis runs alongside as a package of its own, and locally known invalid borrows must be diagnosed by the overall acceptance at the latest. The scope is medium to large, because the same value ownership must apply across all producers and sinks; an isolated free call in the remove branch is not enough.

---

## 9. Open implementation questions (Claude, 2026-09-24 — for review/additions)

I read the plan against the current code. Below are the places where I am **unsure HOW or WHERE** to implement — please review and enter the design decision. Legend: 🔴 = core risk / decision mandatory before implementation, 🟡 = concrete code location/procedure unclear. A verified positive up front: **`aiml_ser_retain`/`ret()` [`src/sys/ser_runtime.cpp:805–847`] already covers STRING/STRUCT(recursive)/LIST_PTR/MAP_PTR/BUFFER_PTR/MODEL/PTR_REF(shared) correctly; unique ptr/tensor are correctly NOT incref'd.** Package 2 is therefore largely in place.

### 9.1 🔴 View in-place mutation is the actual hurdle — not the retain
The CURRENT contract [`src/core/ast_compiler_stmt.cpp:1355–1377`]: `decl x = Model {at/get/first/last}` sets `sym.is_view_alias = true` = **shared VIEW**, and `x = at(..); apply_buff(x)` **mutates the SOURCE**. The comment at 1355–1358 says explicitly that deep-copy-at-decl was already **reverted**, because 9rooms (enchantment_picker, effect_processor) relies on exactly this in-place view mutation. CopyRetain (§4) makes `x` an independent co-owned copy → `apply_buff(x)` then mutates the COPY, not the source.
> ⚠️ DECISION NEEDED: are all "`decl m = Model at(..)` + then `mutate(m)`" places migrated bindingly to `ref<Model>` (plan §4 says so, but it is an observable behaviour change across the whole game)? If yes, I need (a) your confirmation of the new contract and (b) ideally a curated list of the affected places (my grep starting point: `decl:` with a value model type + an AtExpr/`.get`/`.first`/`.last` init, followed by a call/`set` on the same variable). **This is the riskiest change — exactly the "broad ABI intervention" class that led to an in-game regression with the string auto-detect, which leak/sanitizer did NOT catch.**


**Answer / decision (Codex, 24.09.2026):** the target contract from the current spec stays binding: `Model` materialises an own value, `ref<Model>` binds the slot. A blanket migration of all mutating calls to ref is, however, **not** necessary. The assumption "CopyRetain mutates only the copy" holds for scalar/field replacement, not for mutation of the container held jointly via a field. Exactly this difference explains why an earlier deep copy attempt could regress without CopyRetain having to cause the same regression.

Handle three cases separately:

1. Changing container content (`m.buffs.push`, `m.pool.put`): CopyRetain preserves the shared visibility; the value binding can stay.
2. Setting a scalar/string field or rebinding a container field, followed by an `idx` write-back: the value binding can likewise stay. The write-back writes the changed fields back; secure the source beforehand. Precondition: the target index still denotes the intended entry.
3. Scalar/field replacement should change the original slot immediately without a write-back: migrate to `ref<Model>` and check the entire usage period for shift/realloc/remove. With a structural mutation, keep the value instead and relocate it via a stable identity; do not hold a borrow across it.

**Curated start list, checked against the source** (paths relative to `9rooms_v2`, not a claimed complete migration list):

| Place | Observation / decision |
|---|---|
| `sources/cards/effect_processor_base.aiml:547–585`, `buff_zone` | `ent = at(zone,idx)`, `add_buff(ent,buff)`, then `idx(zone,idx,ent)`. Keep value + CopyRetain; the existing write-back covers scalar changes. The enchantment_picker regression test is mandatory. |
| `sources/match/buff_ops.aiml:276–295`, `gc_zone_inplay` | `gent` is changed via `remove_buff(&gent,...)` and written back with `idx`. Keep the value; the inner buff list may change, the outer zone slot stays stable until the write-back. |
| `sources/match/buff_ops.aiml:164–173`, `remove_buff` | `buf = at(zen.buffs,idx)` is read, then the origin is removed. Exactly here, an owning CopyRetain makes sense; do **not** automatically make it a ref. |
| `sources/match/card_pick_helpers.aiml:28–30`, `revive_place_one` | `rp_d2`, `place_card_at(&rp_d2,...)`, then `idx(mat.players,...)`: keep value + write-back. |
| `sources/cards/effect_handlers.aiml:519–525`, `do_apply_buff` | `ctrl`/`opp` serve to read the IDs here. No ref migration required. |
| `client/card_editor_base.aiml:492–495` | `src_ab` is transferred into a new ability. Test the editor's copy/clone intent separately; no write-through view required. |

For the complete inventory at implementation time, generate an AST-based report over value at/get/first/last bindings and the mutating parameters called. Grep alone cannot prove callee write effects and write-backs. So far, **no blanket mandatory migration** has been proven from the checked core places. Behaviour tests in addition to leak tests are the release condition; the plan does not authorise an untested mass replacement.

### 9.2 🟡 Where exactly to emit CopyRetain at `decl`
At [`ast_compiler_stmt.cpp:1359–1377`] TODAY only the flag is set, no copy; materialisation only happens at escape points (below). Unclear: should `prepareOwnedModelValue`/CopyRetain be emitted HERE at decl (memcpy into an own slot + `aiml_ser_retain`, is_view_alias dropped for values), or does the "escape-time" structure stay and only the kind of copy is swapped (deep copy → retain)? I do not know which of the two ways you want.


**Answer / decision:** CopyRetain happens **at the value binding**, not only at the escape. The metadata piece at 1359–1377 only classifies; the IR emission belongs in the actual initialisation/store path of `visitDeclStmt`, after evaluating the initializer once and before publishing the finished value. Merge the existing pointer-vs-struct initialisation paths: resolve the slot address or spill once → temporary value copy → retain → take over into the local target slot. Then register `Owned/Live` and exactly one scope drop.

For an uninitialised target slot, no release; if empty containers were already created by an existing initialisation path, they must be released before overwriting, or their unnecessary creation avoided. Do not copy both in the flag section and in the later store. Own producer/pop/clone results are adopted, not additionally retained. `ref<Model>` stays the separate lvalue/borrow path. This early ownership setup is necessary so that remove is already safe **before** a later escape.

### 9.3 🟡 Which of the deep copy sinks become CopyRetain, which stay `clone`?
`aiml_ser_deep_copy` is emitted in many places: [`ast_compiler_stmt.cpp:1192, 1960, 2254, 2274, 2313, 2471, 2603, 3111, 3135, 3576`] (set/return/idx/member) and the value model **push** [`ast_compiler_call.cpp:1545ff`] (today MOVE-if-movable, otherwise deep copy). Plan §4: value materialisation → CopyRetain, but explicit `{clone}` → keep deep copy.
> ⚠️ I need, per sink, the assignment **value materialisation (→ retain)** vs. **explicit clone (→ deep copy stays)** — otherwise I am guessing. Please go through the list and mark it.


**Answer / decision:** no global replacement by function name. Assignment based on the currently read branch conditions (lines may shift):

| `ast_compiler_stmt.cpp` / sink | Decision |
|---|---|
| 1192: `ctx.kvGetCalled`, decode cache → new MODL shell | **Keep for now, own KV/refcount adapter**, not an inline list sink. Do not misclassify as an explicit clone. Switching requires its own cache eviction/mutate-set test and a check of the KV contract. |
| 1960/1962: decl with `ctx.kvGetCopy` | **Keep the explicit independent KV copy**. Not automatically CopyRetain. |
| 2254/2274: local model assignment | **CopyRetain**, including release-old after securing; 2254 is a comment, not a call of its own. |
| 2313: set from `at` | **CopyRetain**. |
| 2471: set from view/member/accessor | **CopyRetain**; origin in future from ownership facts instead of a name heuristic. |
| 2603: set with `ctx.kvGetCopy` | **Keep the independent KV copy**, nevertheless replace old target ownership correctly. |
| 3111: `memberDeepCopy` from borrow/value | **CopyRetain**; rename/reclassify the plan action. |
| 3135: mixed `ctx.kvGetCopy || memberDeepCopy` | **Split the branch**: explicit KV copy stays independent, ordinary value assignment becomes CopyRetain; an owning result becomes move/adopt. |
| 3576: return `kv.get_copy` | **Keep the independent KV copy**. In the current state this is not the normal view return! |
| 3655: normal value return from a view | **CopyRetain**; an already owning local/temp move/adopt per the transfer plan. |
| 4662: inline model idx replacement | **CopyRetain or move**, always secure the source before release-old. |
| 4790ff: value `store` | Already share; correct the order via the shared helper so that indirect aliasing is safe too. |
| 4850: `ctx.isRefcountModel` pointee store | **Separate refcount pointee branch**, do not change blindly in this inline package. No evidence of an explicit clone; own follow-up test/contract reconciliation required. |
| `ast_compiler_call.cpp:1545ff`, `pushDeepCopyModel` | **CopyRetain for a copyable borrowed source; move for a provably owning source**. Secure the source before realloc. An already moved source is an error, not a copy fallback. |

Real `{clone}`/`sys.clone` paths, among them `emitContainerCloneDeep` and `visitCloneExpr`, keep the deep graph copy. The explicit KV copy paths likewise stay independent, without equating them semantically with the complete clone walker. `1192` and `4850` are deliberately delimited adapters, not a release for keeping arbitrary deep copies in normal value sinks unchecked. Tests must cover both adapters separately from the inline acceptance, if a target test uses them.

### 9.4 🟡 `is_view_alias` — remove or replace with provenance?
Consumed at [`ast_compiler_stmt.cpp:2475` (reset after an independent copy), `3229`, `3633`] among others. Today it does NOT cleanly separate a borrow (`ref`) from a value view. Plan §3 wants to "remove old syntactic view heuristics for value copies" + §5 real `BorrowOrigin` provenance. Unclear: replace `is_view_alias` completely by the OwnershipPlan (§5), or keep it for the pure `ref<Model>` borrow and only extract the value branch?


**Answer / decision:** the goal is **removing the bool as a semantic source of ownership**, not repurposing it into a "ref flag". For materialised values it is dropped. Real ref borrows need owner/path provenance and validity in the OwnershipPlan; the declared ref type already marks the reference form. A bool can represent neither alias chains nor CFG merges correctly.

In an intermediate commit the field may exist for not yet converted legacy branches, but must not suppress the release of new CopyRetain values. Switch all readers (including consume/drop/return, not only the three lines named) to facts; then delete the field and outdated comments. For unknown facts use a diagnostic/an explicitly bounded legacy path, do not accidentally treat a value as non-owning.

### 9.5 🟡 The move-only model predicate is missing — where to put it?
No predicate recognises "the model has (also transitively) a unique `ptr<T>` box or `tensor` field" → not implicitly value-copyable. `ret()` correctly skips unique [`ser_runtime.cpp:826–833`], but the **compiler must reject the value copy of such models** (otherwise the copy shares a non-owned resource → double free/UAF). Proposal: `ctx.isMoveOnlyModel(typeName)` via a serNodes walk (PTR_STRUCT=10 / TENSOR + PTR_REF param2==0). Unclear: at which sinks to check (decl/push/set/return) and which exact diagnostic wording/"static_off" escape you want. Existing negative test anchor: `tests/lifetime/neg/n13_move_only_value_copy_from_at.aiml`.


**Answer / correction:** the predicate is not completely missing: `modelIsMoveOnly` exists as a static helper in `ast_compiler_stmt.cpp:923–943`; it checks `ptr<...>`, `tensor` and transitively nested user models. It is already used at decl and set (among others 959 and 2208). What is missing is its **complete type coverage and use across all sinks**.

Decision: derive a shared semantic type trait from the local helper, e.g. `CompilerContext::isCopyableValue(type)`. Walk over declared member types and the central resource classification, **not** over the serialisation TD as the sole truth: RAWBYTES can lose lifecycle information. Check arrays recursively, classify dyn_array and all class 4 resources as unique. ref/shared as well as rc containers stay copyable handles; `list<ptr<T>>` as a model field does not make the model itself move-only, because only the list handle is copied. Value nesting recursively and with a cycle guard; cache traits only after type registration is complete.

Check centrally before every CopyRetain and additionally in the AST checks for decl, set, member/idx/store, push/insert/extend/slice, get_copy and value argument/return from a borrowed source. A proven ownership move needs no copyability. Proposed diagnostic: `Cannot copy move-only model 'M': field 'M.child.resource' owns a unique resource. Bind ref<M>, transfer an owned value, or use clone if supported.` **No static_off escape** for this ownership violation; a static style switch-off must not create double unique ownership. clone itself checks clonability separately.

### 9.6 🟡 Arrays of heap values as a model field — does that exist?
There is **no `AIML_TD_ARRAY`** (only RAWBYTES=9 for value-only). `array<string:N>`/`array<Model:N>` as a model field would not be covered by `ret()`/release. Unclear: does that exist as a field type at all, and if so, in which TD encoding? If supported → add a retain/release branch; if not → document as "not supported".


**Answer / finding:** arrays exist in type resolution: `CompilerContext::getLLVMType`, `context.h:955ff`, creates the LLVM array type. `serFieldSubtree` in `ast_compiler.cpp`, on the other hand, currently has no array branch and falls back to RAWBYTES for this type (3469–3474). `ret()` consequently has no element information. The existing test `tests/lifetime/13_array_dynarray_cascade_map_buffer.aiml` uses a **local** `array<string:3>`; it does not prove the lifecycle of a model field. It is therefore a descriptor gap, not a reliable statement "arrays are not supported as a field". A model field compile/lifecycle test is to be added first; this investigation has not run it yet.

Decision for this package: for fixed array fields, use the existing STRUCT composition in the lifecycle descriptor: generate N (for several dimensions the product) element subtrees flat with the actual LLVM stride/offsets; shift subtree indices correctly. Retain/release can thus run via the existing STRUCT recursion without changing a new TD enum value and all walkers at the same time. Use this representation exclusively for lifecycle, in case the existing wire/clone representation differs from it; do not silently change existing serialisation layouts. Without a clean separation, introduce a lifecycle descriptor of its own. Large arrays can get a compact ARRAY node later.

POD arrays need no retain. Arrays with unique elements make the enclosing value model move-only; release must nevertheless process every element. Until the field lifecycle support is proven, reject such heap array fields in the new copy/release paths with a targeted error message instead of treating RAWBYTES as "safe". Complete acceptance includes the array field tests; merely documenting the gap does not fulfil it.

### 9.7 🟡 Do not duplicate the element release lifecycle (reconciliation with my already built partial fix)
For the SAFE kinds I have already built: `free_list_elem_at` + `aiml_list_clear_elems` + `aiml_list_pop_free` [`src/sys/string_runtime.cpp`], `Compiler::classifyListElem` [`ast_compiler_stmt.cpp`], as well as the `clear`/`pop` wiring [`ast_compiler_call.cpp`] — **elemKind 2 (value model inline) is deliberately SKIPPED there** (`skipInlineValueModel`). `remove` already releases elemKind 4/5 [`ast_compiler_call.cpp:1802–1848`]; the final list drop does elemKind 2 via `aiml_list_free` [`string_runtime.cpp:192–195`].
> ⚠️ Plan §6 wants ONE uniform walker. Unclear: (a) simply activate elemKind 2 in `free_list_elem_at` once package 3 is in place? (b) how remove/clear/pop/final-drop share the same element lifecycle WITHOUT final-drop (`aiml_list_free`) and a new walker releasing the same element twice? I am unsure whether to switch `aiml_list_free`'s elemKind 2 branch to `free_list_elem_at` or keep both separate.


**Answer / decision:** keep using the existing `free_list_elem_at` as the **sole element release dispatcher** and switch `aiml_list_free` to it too. Adjust the forward declaration/position. This is a runtime refactoring, not a second release layer.

- Final drop: walk over the slots active at that point, call the dispatcher once per slot, then free only the data buffer and zero the header. The wrapper `aiml_list_handle_decref` then frees the handle shell at rc 0.
- clear: release the same active range, len=0, keep the buffer. Afterwards final drop sees no active elements any more.
- remove: use a runtime entry with a bounds check, the dispatcher for exactly the removed slot and then shift/len--. Remove previous C++ codegen special releases for the same kind as soon as this entry is used; no additional inline decref before it. Do not drop the inactive tail again.
- pop-discard: keep the existing `aiml_list_pop_free` as an optimised drop of the extracted result. A bound/passed-on pop transfers ownership and does **not** call the dispatcher on the list slot.

During migration, `skipInlineValueModel` is only active for premature clearing; final drop keeps its previous kind 2 release. After the materialisation/transfer acceptance passes, release kind 2 everywhere and remove the skip switch. Keep the logging for kind 3 and the stride for kind 6. Kind 6 continues to be only shallow for the concrete interface implementation; the unification does not fix this separate destructor gap.

**Correct the comment error in the partial fix:** "inline field heap is left to eventual list scope-drop" does not hold after clear/pop: the reduced len prevents exactly that final drop still visits these slots. The current skip is a deliberate leak, not a later release. Double free is avoided by the active range and unambiguous responsibility, not by two independently running walkers.

### 9.8 🟡 pop-discard in a nested/call context
My `ctx.popResultDiscarded` flag [`context.h` + `visitExprStmt` in `ast_compiler_stmt.cpp`] only catches the DIRECT `call: [l.pop]` statement case. Plan §5.5 wants uniform temp registration/consumption (also `foo(l.pop())`, deeply discarded). The `stmtTempStack` machinery is **string-only** today [`ast_compiler_stmt.cpp:181–213`]. Unclear: introduce a typed owned temp stack (model/handle), or is the direct statement case enough? (For elemKind 2, pop is only safe after package 3 anyway.)


**Answer / decision:** for complete acceptance, the direct statement case is not enough. Introduce a **typed owned temp stack**; integrate the existing string temps into it or connect them via a shared interface at first. A temp entry needs its own identity, storage/SSA value, type/lifecycle action, live/consumed state and the evaluation region. Not just a global bool.

`foo(l.pop())` is not automatically a consuming call: if foo borrows the parameter, the temp stays owning until after the call and is dropped afterwards. If foo takes ownership, the temp is marked consumed and not dropped at the caller. A returned borrow must not lose the necessary anchor; a return by value is adopted separately. The same applies to nested calls and discarded outer results.

Release temps in reverse creation order at the end of the **actually executed** full expression; conditions/short-circuit/return/early exit need their own clean-up edges. Hand-over to decl/return/push marks exactly the respective temp as taken over. Evaluate sources exactly once. `ctx.popResultDiscarded` may stay for now as an optimised direct case, but must then not produce an additional owned temp drop; after unification, remove it as unnecessary state or derive it from the temp/expression plan. Tests for borrowed/consumed calls and branches are mandatory.

### 9.9 🟡 Leak gate — the sanitizer suite is not enough (I agree with the plan)
Plan §7 says correctly: `tests/run_memory_sanitizers.sh` runs with `--errors-for-leak-kinds=none` → its PASS is only memory safety, NOT leak acceptance. So far I have built on this PASS + separate `definitely lost` probes. Unclear: do you want a hard leak count gate directly in `run_memory_sanitizers.sh` (per probe `definitely+indirectly == 0`), or a separate script? That would also retroactively secure my previous fixes.

**Answer / decision:** create a **separate hard gate** `tests/run_lifetime_leak_gates.sh`; do not switch the existing safety runner globally to zero leaks. It contains deliberate error controls and tests with a different measurement methodology. CI/acceptance calls both.

The leak gate runs an explicit list of isolated lifetime/9rooms probes: memV3 and drops active, a successful native assertion run, then Valgrind with `--leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect --error-exitcode=99`. Check the complete report and the process status; a missing report, a signal, a build error or Valgrind not being installed in the mandatory CI job are errors, not a PASS. Use XML for machine-readable evaluation. Definite/indirect must be 0; do not sum both categories twice from the detail records (indirect losses can already be contained in the definite record). For the gate, the mere presence of one of these leak kinds is already an error.

Keep historical red probes separately as reproducible findings. Only add them to the list of expected zero-leak probes after the fix passes; add already fixed safe kinds now. Controls prove that the gate actually rejects a deliberate leak. Keep documenting 10/1000 runs as proof of growth, do not count still-reachable as a loss wholesale. Store the compiler/runtime state and complete logs as artefacts. The existing files under `9rooms_v2/leak_probes` remain the project-related reproductions.

---

## 10. Implementation status + planned next steps (Claude, 2026-09-24 — for review)

This section records precisely what was actually implemented/committed, which empirical findings exist, and what I plan to do next. All commits: aiml branch `mem-v3-compiler`, 9rooms branch `memv3`.

### 10.1 Done + committed (safe, verified)

**A. Two separate leak classes fixed (not the inline value model case — these are the *other*, safe kinds):**
- **owned string return** (§6.2 T1): `retStrFieldOfOwnedModel` in visitRetStmt (incref the field + fully drop the owner, coupled → never a double free) + a new `currentFunctionOwnsReturn` flag. 9rooms sources marked as `producer` (pid_for/nick_for/nick_for_pid/resolve_token/resolve_token_pid; client avatar_initial/join_pipe/card_sig/zone_sig/board_content_sig/take_edit_card_id). — aiml `196b2c1`, 9rooms `585341c`.
- **pop(discarded)/clear element release** for the SAFE kinds (1 string / 3 refcount model / 4 ptr<T> box / 5 nested container handle / 6 iface): runtime `free_list_elem_at` + `aiml_list_clear_elems` + `aiml_list_pop_free`; compiler `classifyListElem` + clear/pop wiring + the `popResultDiscarded` flag. **elemKind 2 (value model inline) deliberately SKIPPED.** — aiml `196b2c1`.
- Both valgrind-verified (standalone + 9rooms leak_probes 0 lost), mem-sanitizer memory-safe, corpus 422/461 (no new regression). Regression probes `erf` + `pcr` in run_memory_sanitizers.sh.

**B. Rejected auto-detect (string return classifier):** the broad auto-classification caused a 9rooms client navigation regression (deck → card editor) that leak/sanitizer did NOT catch → left DISABLED (`classifyOwnedStringReturn`, `[[maybe_unused]]`). Lesson: broad ownership ABI changes need a game+client BEHAVIOUR gate.

**C. Array field diagnostic (§9.6):** a deduplicated warning in the serFieldSubtree RAWBYTES fallback for heap element array model fields (no fp risk; POD/local arrays unaffected). — aiml `96bc6fa`.

**D. Deferred comments marked** (ast_compiler_call.cpp remove ~1873, ast_compiler_stmt.cpp decl ~1355, string_runtime.cpp free_list_elem_at) + comment error corrected (§9.7: "left to scope-drop" was wrong → deliberate leak).

### 10.2 Package 1 + 2 + prototype — findings

- **Package 1 (red contract tests, `tests/_inline_model_red/`, excluded from the corpus):** `49_value_survives_remove` (content GREEN, leak RED 47+16 B), `50_share_semantics` (**already GREEN** — container-shared + scalar-independent already holds). **Re-scoping finding:** share semantics + content are already correct; the only gap is the elemKind 2 release LEAK. Content is only green BECAUSE remove/clear do not release kind 2 (the leak keeps the view valid) → a naive release without CopyRetain breaks the content → proves the sequence 3→5.
- **Package 2 (retain/release audit):** `p2_retain_coverage` GREEN (0 lost, 0 errors, 200 iterations) → `aiml_ser_retain` handles co-owned string/list/map/nested model correctly via the store retain path. `p2_array_field_leak_RED` RED (6780 B) → §9.6 gap confirmed. `modelIsMoveOnly` exists (ast_compiler_stmt.cpp:923). → **The runtime primitives exist + are correct.**
- **Package 3 prototype (`p3_copyretain_survives_source_drop`, GREEN):** a retain copy survives the kind 2 release of the source element (aiml_list_free scope drop = the same `aiml_ser_free_decoded` as remove/clear) balanced, with intact content across all field kinds. — aiml `9195ddb`. **→ Mechanism proven risk-free.**

### 10.3 Package 3 implementation attempt "Stage A" (decl-only) — REJECTED

Attempt: CopyRetain only at the `decl copy = at(..)` init (memcpy → `aiml_ser_retain` + owned drop + `is_view_alias=false`), escape sinks unchanged. **Result: mem-sanitizer FAIL (1 check).** → **Empirical proof: decl alone is NOT enough.** The escape paths (set/idx/return/push) carry their own is_view_alias/deep copy logic; an owned+retain copy at decl contradicts them (double retain/copy or a wrong move → double free/imbalance). Reverted, green state + mem-sanitizer PASS confirmed. — aiml `26caacc` (docs).

### 10.4 Exposure (ROI + blast radius)

elemKind 2 (`list<value model>` with heap) concerns the **core structures**: `list<ZoneEntry>` 93×, `list<MockCard>` 55×, `list<ActiveAbility>` 30×, `list<PlayerState>`/`Deck`/`ForumThread`/… → **high benefit BUT high blast radius.** `list<ptr<Model>>` (elemKind 4, already fixed) is rare (ActiveAbility 5×, CardBehavior 3×).

### 10.5 WHAT I PLAN — a coordinated multi-sink package 3 rebuild (order + gate)

No more isolated patch. Value materialisation must be consolidated across ALL sinks with ONE ownership fact. Planned order (each step gated individually, revert on any regression):

1. **Shared primitive** `emitValueModelCopyRetain(dstAddr, srcAddr, type)` (memcpy + `aiml_ser_retain`, reject move-only via `modelIsMoveOnly`) + `emitValueModelRelease` (already `aiml_ser_free_decoded`). ONE helper that all sinks use.
2. **Replace `is_view_alias` with an ownership fact** (§9.4): switch the sinks decl(1991)/set(2313,2471)/idx(4662)/return(3655)/push(ast_compiler_call.cpp:1545)/store(4790) ALL to CopyRetain at the same time; no sink may additionally deep copy/retain (otherwise imbalance as in Stage A). Order secure-before-release (retain src BEFORE the dst release), also for indirect aliasing (§3).
3. **Activate the elemKind 2 release** (`skipInlineValueModel=false`; remove/clear/pop/final-drop via ONE dispatcher `free_list_elem_at`, §9.7).
4. **Gate per step:** mem-sanitizer (memory-safe) + corpus (422/461) + `tests/lifetime` spec61 + the red tests (49 must become GREEN: content + 0 lost) + **hard leak count gate** (§7, `run_lifetime_leak_gates.sh`) + 9rooms regressions (effect_processor/effect_death/mana_pick/enchantment_picker) + **game+client build + in-game behaviour test** (combat/board/deck editor). Only when ALL are green → next sink.
5. **Move-only + array fields:** check the `isCopyableValue` trait (§9.5) at every CopyRetain sink (diagnostic instead of a silent leak); array field lifecycle descriptor (§9.6) separately, in its own pass because of the fp risk.

**Open residual leaks (separate, not part of this plan):** server ~540 B/cycle in `handle_validate` (model sibling teardown on a whole-model `return out`, repro `_mem_sanitize/handle_validate_sibling.aiml`) — a different class, fix open.

**Recommendation:** the multi-sink rebuild (10.5 steps 1-3) is a focused single assignment with the full gate from step 4 — not at the end of a long session, but as a dedicated session, because every intermediate stage touches the core structures.


---

## 11. Review of the error analysis and replanning (Codex, 24.09.2026)

**Verdict: the direction makes sense, §10.5 in its current order is not yet ready for implementation.** The shared ownership contract is right. What needs correcting are the strength of evidence for the Stage A explanation, the position of the type safety checks, the handling of several legitimate ownership references and the contradictory intermediate gates. This review refines §10; the historical reports above are preserved.

### 11.1 Verification and reproduced results

Checked source state: HEAD `362349f`; the named AIML commits `196b2c1`, `96bc6fa`, `9195ddb`, `26caacc` exist. The code confirms: the string auto-classification is commented out at the call sites, the kind 2 skip is active for clear/pop, the array field warning exists. `26caacc` contains exclusively the documentation of the rejected attempt, not its patch or error trace.

All five existing probes recompiled unchanged into native objects, linked with g++ against the existing libaiml, run natively and under Valgrind 3.22.0. Both flags were 1 in each case. **No complete compiler rebuild, no renewed complete corpus/client acceptance.** The binary identity is therefore recorded separately:

- Compiler SHA256: `ec1b66547c4aebc50e74c0cc2c448bd44be3ae74803a519b6d133c61ed774c7b`
- libaiml SHA256: `233596daaad53729ba867d10ea5abead2fcd6036295333809f66b23349451d9d`
- Raw logs permanently: [analysis/inline_value_model_review_2026-09-24](analysis/inline_value_model_review_2026-09-24/). Temporary compiled files: `/tmp/inline-plan-review-wp17hded/`.

| Probe | Native exit | Definitely / indirectly lost | Valgrind exit |
|---|---:|---:|---:|
| 49_value_survives_remove | 0 | 47 / 16 B | 99 |
| 50_share_semantics | 0 | **16 / 0 B** | 99 |
| p2_retain_coverage, 200 iterations | 0 | 0 / 0 B | 0 |
| p3_copyretain_survives_source_drop, 200 iterations | 0 | 0 / 0 B | 0 |
| p2_array_field_leak_RED | 0 | 6780 / 0 B | 99 |

The Valgrind errors reported in these red runs are leak records; the logs show no invalid read/write/free messages. Used: `--leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect --error-exitcode=99`. Build and native logs are located next to each Valgrind log.

### 11.2 Prioritised findings

**P1 — the Stage A cause is not proven (§10.3).** One failed check proves the regression of this attempt, but not "all sinks must be changed at the same time". The documented state lacks the test name, error kind, trace, a reproducible patch and a minimal counter-case. "Double retain/copy or a wrong move" are alternative hypotheses. An additional retain for an additional owner is correct; a separately owning deep copy can be semantically wrong or expensive, but does not by itself cause a double free.

Before a broader rebuild, reconstruct/secure the Stage A patch in an isolated checkout, compare the exact red check with the baseline and reduce it to a minimal probe. In particular, reconcile `ownership_analyzer.cpp::freshOwned`, `StatementPlan` and `mayDrop` against `visitDeclStmt::modelOwnedInit`/clean-up registration: an IR retain plus a registered drop is not enough if the flow facts refuse the drop or misclassify a move. This is a concrete direction to check, **not a cause of error proven here**. If the original attempt cannot be reconstructed, document it as a regression whose cause has not been clarified; justify the multi-sink architecture decision independently of it.

**P1 — type/lifecycle safety comes too late in §10.5.** Step 5 (`isCopyableValue`, array support) must not come only after CopyRetain and the kind 2 release. The existing `modelIsMoveOnly` is still limited to ptr/tensor/value nesting. RAWBYTES array fields have no safe retain/release coverage. Before every new materialisation path, check separately: (a) semantically copyable and (b) lifecycle completely supported. A copyable array with a missing descriptor fulfils only (a). For types not yet supported, a targeted compile error or a clearly isolated old path without a new release guarantee; a warning alone is not a safety precondition. Array lifecycle may be implemented separately, but must not be treated retroactively as a precondition for already activated general releases.

**P1 — "ALL sinks to CopyRetain" and "no additional retain" are too sweeping (§10.5.2).** Every sink needs the same transfer decision, not necessarily the same action:

| Source / use | Action |
|---|---|
| copyable borrowed source → new value owner | CopyRetain |
| an existing owning value stays with the caller and is additionally stored | CopyRetain for the additional owner |
| actual ownership transfer of a local/temp | move/adopt, invalidate the source, consume exactly one clean-up |
| real ref borrow | no ownership retain and no owner drop |
| explicit clone / independent KV copy | keep the existing independent copy contract |

The invariant is: **every newly arising ownership reference has exactly one retain or one taken-over ownership; every expiring ownership exactly one release.** Not "at most one retain per expression chain". In particular, `decl copy=at(...); push(copy)` needs different actions depending on the copy/move contract. The per-sink exception table §9.3 remains authoritative.

**P1 — temp/call ownership and real borrows are missing in the shortened replanning.** §10.5 names the essential stores, but does not adequately carry §9.8 and §5 over into its release preconditions. `foo(rows.pop())`, a discarded outer result, return/early exit and conditions must have typed temp clean-ups. Call parameters distinguish borrow, value copy and consumption. Bindings `ref<Model>` must not unnoticeably become a UAF through remove/clear/shift. Before a general kind 2 activation, diagnose at least locally provable invalidations and run the corresponding negative tests. A complete interprocedural analysis may stay limited; the limit must be documented explicitly.

**P2 — "the only gap is the release leak" and "proven risk-free" overstate the tests (§10.2).** Test 50 loses **16 bytes**, although it uses neither remove nor clear. The trace shows `aiml_str_dup` in main; in the source, `copy.label = "changed"` is set. So there is already another loss path within the unbalanced value copy. The concrete drop path is to be checked at fix time; a pure remove patch is not enough for this test.

The P2/P3 prototypes confirm the tested store retain path for certain fields. After the source drop, P3 checks label, inner.tag and tags[0], but not the mana content and inner.nums; shared/class 5/arrays/move-only/call temps are therefore not completely covered either. Correct wording: "mechanism confirmed for the tested field kinds and the store path; integration and further classes open". Add map content, inner.nums, a change of drop order and self/sub-object aliasing as assertions.

**P2 — the model of the previous view is inaccurate.** The header comments of tests 49/50 claim that a shift changes the read outer slot or that a string assignment writes through. The current decl path, however, already copies struct bytes into local storage (`ast_compiler_stmt.cpp`, ordinary memcpy branch around 1990ff), without materialising complete own member ownership in the process. So one must distinguish (1) independent struct bytes with unsecured field references and (2) a real ref to a slot. Adapt the test headers to the observed actual state; do not use a blanket "copy-on-write-at-set" as an explanation as long as it is not proven in the respective path.

**P2 — the gate order contradicts itself (§10.5.2–4).** On the one hand all sinks are to switch at the same time, on the other "only when all are green → next sink". Moreover, test 49 can still be expected to leak before the element release is activated; "all target tests must be green after every preparation step" blocks the planned flow. Mark tests by development phase: baseline preservation, new helper/transfer tests, expected red target tests, the final zero-leak gate. A coordinated end state can consist of small verifiable commits. No partially converted state is shipped as new productive semantics.

**P2 — 422/461 is not a sufficient regression gate.** The same number of green tests can hide new failures with simultaneously fixed old ones. Require an identical test selection and flags, a comparison of the concrete status per test, exit codes and no new skips. Baseline existing failures by name. The announced `tests/run_lifetime_leak_gates.sh` does not exist yet at review time; so provide it as a harness before the next implementation attempt. Define the client behaviour check concretely as scenarios with an expected result and evidence (deck → card editor → back, save a change/reopen, apply a buff/let it expire, board state). A client build alone does not fulfil it.

**P2 — the array warning is useful, but not a complete diagnostic (§10.1.C).** `serFieldSubtree` currently splits the array element type at the first `:`; with `array<map<string:i32>:N>` it lies within the element type. ptr/shared/tensor/path/class 5 are not completely contained in `elemHeap`; later type registration can influence the hasHeapFields finding. Use a shared nesting-proof type parser and lifecycle trait. The deduplicating set is function-static/process-wide; for reliable diagnostics, better per CompilerContext/compile run. The warning does not change any descriptor bytes here, but does not cover all dangerous types. Do not derive any proof of safety from its absence.

### 11.3 Improved replanning (replaces the order from §10.5)

1. **Secure evidence and baseline.** Isolate the Stage A failure or record the unclarified cause explicitly. Add test 50 to the leak targets. Correct historical README/test comments. Store the compiler, runtime, flags and test status per case.
2. **Bring the harness and type preconditions forward.** Create the hard leak gate; centralise `isCopyableValue` plus separate lifecycle coverage. Catch unsupported array/unique cases before new copy paths. Settle the wire/lifecycle descriptor separation before array work.
3. **Shared transfer primitives without broad activation.** Distinguish CopyRetain / move / borrow / clone explicitly. An API with source and target state, one-time evaluation and an unambiguous clean-up token instead of a bare memcpy helper. Secure source → retain or move-invalidate → release-old → commit; test self/sub-object aliasing and a possible realloc. Secure the existing store path with it.
4. **Migrate OwnershipPlan and codegen together.** Switch per complete producer-consumer chain decl→set/store/idx/return/push and call hand-overs, including `freshOwned`, mayDrop, temp clean-up and branch merges. The old and new conventions must not mix unmarked. Do not eliminate every retain; check the reference balance per owner. Cover new paths completely via the relevant matrix before shipping.
5. **Secure borrows and temp lifetime.** Test locally recognisable invalidation via container aliases, discarded/passed-on pop results, return/early exit and short-circuit. No temporary owner may drop before the last valid borrow use.
6. **Activate the kind 2 release and unify the dispatcher.** Use the existing `free_list_elem_at` for final drop and remove too, remove previous duplicate special paths. Separate pop-transfer and pop-discard; len/active slots prevent a repeated drop. Only now require zero-leak targets 49 and 50 as well as the clear/pop/remove matrix bindingly green.
7. **Overall acceptance against the baseline and game behaviour.** No new corpus failure/skip, the complete relevant lifetime safety and leak gates, the named 9rooms regressions, the WASM build and a documented client/combat smoke test. Gates that cannot be run stay explicitly open; no release based on native prototypes alone.

**Delimitation:** the previously reported 422/461, the deck editor regression, the concrete Stage A failure and the server loss of ~540 B/cycle were not reproduced again in this review. They remain reports from §10 and must not be read as causal evidence independently confirmed here. The five probe results listed above, on the other hand, were measured anew. No compiler/runtime fixes were made.

---

## 12. CopyRetain implementation attempt — findings (Claude, night of 2026-09-24/25)

A serious implementation attempt of the decl CopyRetain (Codex §11.3). Result: **no committable green state reached; reverted.** The findings (verified, valuable for completion):

1. **Stage A (CopyRetain at EVERY struct-memcpy-from-pointer init, ast_compiler_stmt.cpp ~1991) → mem-sanitizer FAIL.** Isolated cause: the probe `producer_set_view_return` leaks 22.9 MB, because the init there also hits **producer call results** (`decl brd = make_board(idx)`, owns_return) — these are ALREADY owned/adopted; an additional `aiml_ser_retain` over-increfs them. Confirms Codex P1: "producer/pop/clone results are adopted, not retained".

2. **Stage B (CopyRetain only if `sym.is_view_alias`, i.e. a real at/get/member view) → mem-sanitizer PASS.** The gate correctly excludes producer/clone/owns-return; `producer_set_view_return` passes again; NO escape double frees (all escape probes green). The view-gated retain is therefore memory-safe.

3. **BUT: `aiml_ser_retain` is NOT emitted at all for `decl copy = at(rows,0)`** (objdump: 0 occurrences in the test 50 .o). I.e. the at-view value model decl does **NOT reach the 1991 struct-memcpy-from-pointer branch** (there, `initRaw->isPointerTy()` is apparently false for at value model elements, or a different store applies). → **My CopyRetain location is the wrong one.** For the actual views, Stage B was a no-op (it only hit producer decls, which it correctly skips). That is why 49/50 stayed red unchanged.

4. **The freshOwned view case + `CompilerContext::isMoveOnlyModel` (shared codegen↔analyzer) are correctly formulated + ready**, but INERT without the retain in the right path (freshOwned only gates a REGISTERED drop; without the CopyRetain block no drop is registered → harmless, but without effect).

**Concrete next step (for completion):** find the real materialisation path for `decl <value-model> = {at/get/first/last}` — check what `visitExpression(AtExpr)` returns for a value model element (element ADDRESS as a pointer vs. a loaded value) and where the store into the decl slot happens. Place CopyRetain (memcpy + `aiml_ser_retain` + owned drop registration, gated on is_view_alias && !isMoveOnlyModel) THERE; then reactivate the freshOwned view case (must match exactly). Afterwards the gate from §11.3 steps 4-7 (including the game+client behaviour test — NOT possible headless). Until then the elemKind 2 release stays deferred; the safe kinds (1/3/4/5/6) + the owned string return are already committed + green.

Verification artefacts of this night: `tests/run_lifetime_leak_gates.sh` (hard leak gate, committed), red targets 49 (47/16 B) / 50 (16 B, field reassignment on a view) / p2_array (6780 B) reproducible; GREEN prototypes p2/p3 0/0.

## 13. String KV `get_copy`/`get` per-call leak — FIXED (Claude, night of 2026-09-24/25)

After the CopyRetain revert, directive 2 ("keep looking for leaks, headless in 9rooms") was pursued. The
`handle_validate` server residual (~540 B/request, stack `aiml_str_dup ← handle_validate ←
resolve_token_pid`, 22 B/call) was reproduced in isolation and fixed.

**Repro (isolated):** `decl val = kvs.get_copy(k)` on a `Kv<string:string>` in a loop of 500 →
`definitely lost: 11,384 bytes in 501 blocks` (= 500 per-call blocks of 22 B + 1× open_kv handle).

**Cause (double ownership).** A string-valued Kv `get`/`get_copy` (valSize==0) lowers in
[ast_compiler_call.cpp:2488](../src/core/ast_compiler_call.cpp#L2488) to `aiml_db_kv_get` (mmap borrow)
+ `aiml_str_dup` (owned, rc=1, "kv_get_own"). This dup, however, was **not** registered as an owned string temp.
The managed string init store normalisation
([ast_compiler_stmt.cpp:1765](../src/core/ast_compiler_stmt.cpp#L1765)) therefore saw `ownedTemp=false`
→ treated it as a borrow → **an additional `aiml_str_incref` (rc=2)** (memV3 branch 1770-1772). Only
ONE scope drop `decref` (kind 1, [1490](../src/core/ast_compiler_stmt.cpp#L1490)) → rc stays 1 →
22 B/call leaked. The `set` consume path ([2407](../src/core/ast_compiler_stmt.cpp#L2407)) had
the same root (borrow → COPY dup instead of MOVE → kv_get_own leaks).

**Fix.** Register the `kv_get_own` dup on `ctx.stmtTempStack.back()` (gated
`lifetimeDropsEnabled && !stmtTempStack.empty()`), analogous to the producer method path
([ast_compiler_call.cpp:3296](../src/core/ast_compiler_call.cpp#L3296)). That way a consuming
decl/set/push/return MOVEs the owned ref (adopt rc=1, one drop) via `consumeStringTemp`, and a transient
use is decref'd exactly once at the end of the stmt. **No double free path** introduced: the
kv_get_own ref was previously decref'd in NO path (always a leak, never a double free) → now exactly once.

**Verified (isolated, valgrind):**
- `decl val = get_copy` × 500 → per-call leak **0** (previously 500 blocks). Remainder: 1× 583 B open_kv handle.
- `set slot = get_copy` × 300 → per-call leak **0**, **no** invalid read/write/free (no
  double free through the MOVE). Remainder: the same 1× 583 B open_kv handle.
- Test [tests/_inline_model_red/51_stringkv_getcopy_no_leak.aiml](../tests/_inline_model_red/51_stringkv_getcopy_no_leak.aiml)
  (functional, corpus): PASS, total==2600.

**Effective in production:** `lifetimeDropsEnabled` default true
([ast_compiler.cpp:1360](../src/core/ast_compiler.cpp#L1360)); the deployed server runs
`AIML_LIFETIME_DROPS=1` (exodus-*.json).

**⚠️ CORRECTION (after deeper analysis): this fix closes the `handle_validate` residual ONLY PARTIALLY.**
It fixes the simple pattern `decl x = stringkv.get_copy(k)` (x is read/passed on). The
DOMINANT `handle_validate` server leak (stack `str_dup ← handle_validate ← resolve_token_pid`), however, is
a SECOND, independent, PRE-EXISTING bug in the value model member store — see §14. My
get_copy fix is nevertheless correct, verified and safe (0 per-call leak, no double free), just
not the complete handle_validate solution.

**Remaining (NOT in this fix, separate):** the one-off `aiml_db_kv_open_persist` handle leak
(~583 B per opened Kv, [db_runtime.cpp:1298](../src/sys/db_runtime.cpp#L1298)) — a `Kv<>` decl local
gets no scope drop that frees the Kv object (`delete kv`); there is no `close` method yet.
Constant (does not scale with requests), therefore lower priority than the per-call leak. A candidate for
a fix of its own (Kv lifetime / `close` operator).

**Gate status (COMMITTED):** mem-sanitizer PASS (44/44 probes correct + memory-safe), corpus 422 pass /
1 pre-existing unrelated fail (`test_model_inherits_hetero_list` — `list<any>` base slice crash, PROVEN
inert with respect to my change: the test's .o contains no `aiml_db_kv_get`/`kv_get_own` reference),
game + client build cleanly with the new ABI. The 9rooms behaviour test suite ran along. → committed.

## 14. Value model string member store leak (the ACTUAL handle_validate residual) — ✅ FIXED + GATED (Claude, 2026-09-25)

> **STATUS UPDATE (after the fix):** root cause found AND fixed. The analyzer-vs-codegen mismatch
> documented below was the cause. **Fix:** [ownership_analyzer.cpp:264](../src/core/ownership_analyzer.cpp#L264)
> — a managed string member store source is now classified as `Transfer::DeepCopy` (share) instead of `Move`,
> so the source's scope drop is preserved (mirrors the plain local rebind
> exception). Verified: `share.aiml` (KV-free) 8,800/400 → **0 lost, 0 invalid**; `handle_validate_sibling`
> 11,000/500 → per-call **0** (only the 2 one-off KV handles remain); NO invalid read/write/free.
> Gates: mem-sanitizer PASS (44/44), corpus 422/1 pre-existing, leak gate 0 (new GREEN test
> [tests/_inline_model_red/52_model_strfield_share_no_leak.aiml](../tests/_inline_model_red/52_model_strfield_share_no_leak.aiml)),
> game+client build cleanly. 9rooms behaviour suite: 8 fails, ALL proven PRE-EXISTING (baseline
> without the fix = identical fails: 5 JIT compile timeouts rc=124 + 3 old game logic bugs
> reaction_state/soulmage_defense/trigger_stage2, among them a pre-existing garbage-iid UAF in the game code)
> → 0 regressions. **The handle_validate server residual is thereby closed.** The following
> analysis is the finding BEFORE the fix (historical, explains the root cause):

While verifying the get_copy fix against the real `handle_validate` repro
([tests/_mem_sanitize/handle_validate_sibling.aiml](../tests/_mem_sanitize/handle_validate_sibling.aiml))
it was discovered: the dominant server leak (stack `str_dup ← handle_validate ← resolve_token_pid`, 22 B/req)
is NOT the get_copy decl, but the **value model string member store** `set: [out.email, eml]`
followed by `return out` (producer). Reproduced minimally **KV-INDEPENDENTLY**
(`scratchpad/share.aiml`: a model with `[msg, string]`, `set out.msg = sib` where `sib` is an OWNED
string local (interpolation, read afterwards → share forced), dropped locally → **8,800 B in 400
blocks = 22 B/call**). Also reproducible purely locally (not returned) (`scratchpad/loc.aiml`).

**Ref trace (gdb, 1 iteration, loc variant with get_copy):**
- `sib` buffer A = `0x…318`, created via `aiml_str_dup` (kv_get_own), rc=1.
- `set out.msg = sib` → `aiml_str_incref(A)` (rc=2), `out.msg = A` (share; member store incref for
  VariableExpr/managed string sources, [ast_compiler_stmt.cpp:3195](../src/core/ast_compiler_stmt.cpp#L3195)).
- `aiml_ser_free_decoded(out)` decrefs A ONCE (rc 2→1).
- **The SECOND decref of A is missing** (the scope drop of `sib`): in the trace, the sib slot drop decrefs
  a DIFFERENT buffer (`0x…338`/`0x…3c8`), not A → A stays at rc=1 → **leak**.

**Contract (the member store incref comment at 3168/3195 names it itself):** `sib` rc1 → incref rc2;
the DEST model drop decrefs (→ rc1); `sib`'s OWN scope drop decrefs (→ rc0). Balanced ONLY if
`sib`'s drop hits A. Finding: `sib`'s drop does NOT hit A (slot mapping discrepancy — presumably `sib`'s
slot is wrongly zeroed/treated as moved after the share, OR `consumeStringTemp`/the init store
decoupled the slot content from the incref'd buffer). **Root cause not yet finally pinned down.**

**Why handled separately + NOT fixed speculatively tonight:** this is exactly the value model
ownership/teardown area that, in §12 (CopyRetain) and with the earlier auto-detect revert
([[owned-return-and-pop-clear-leaks]]), produced regressions that leak/sanitizer did NOT catch (only the
game behaviour test). A fix here needs: (1) the exact root cause (locate the slot-zeroing place in the set path),
(2) mem-sanitizer + corpus + leak gate, (3) a game+client BEHAVIOUR test. Until then it stays
documented + open. Exposure analysis (§10.4 style): `set model.strfield = <string-local>` is VERY frequent in
the 9rooms server code (every RPC handler that fills response models) → high value, but a high
blast radius. **Next step:** instrument the slot-zeroing/move handling in the managed string set member path
([ast_compiler_stmt.cpp:2402](../src/core/ast_compiler_stmt.cpp#L2402) ff.), check
why `sib`'s slot does not hold A at the drop.

**Remaining (NOT in this fix, separate):** the one-off `aiml_db_kv_open_persist` handle leak
(~583 B per opened Kv, [db_runtime.cpp:1298](../src/sys/db_runtime.cpp#L1298)) — a `Kv<>` decl local
gets no scope drop that frees the Kv object (`delete kv`); there is no `close` method yet.
Constant (does not scale with requests), therefore lower priority. A candidate for a fix of its own.
