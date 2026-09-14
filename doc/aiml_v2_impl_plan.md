# AIML v2 Reference-Semantics — Implementation Plan

Companion to `aiml_v2_memory_model.md` (the DECISION). This is the **how**: the current
representation, the target, and an ordered, gateable phase list. Grounded in the code as of
2026-09-14 (aiml @ `973509b`+model-runtime).

## 0. The core difficulty (read first)

The change is a **representation change**, and representation is **coupled**: a model local cannot be
a heap pointer while its `list<Model>` elements stay inline structs, or while a callee still expects a
by-value struct. So the naive "phase per operation, green after each" does not hold — the pieces flip
together.

**Strategy: build the new representation behind a compile switch, keep the old path green, gate the
whole new path, then flip.** Concretely a `ctx.modelRefcount` flag (env `AIML_MODEL_REFCOUNT`, default
OFF like `AIML_LIFETIME_DROPS` began) selects representation at codegen time. The deployed configs
(`exodus-*.json`) turn it ON only once the full 9rooms suite is green with it — v1's cardinal sin was
gating a different config than shipped (`feedback_broad_codegen_full_suite_gate`).

## 1. Current representation (v1, what we change)

- **Model type** → an LLVM `StructType` of its fields in declared order (`ctx.classes[name].type`).
  Scalars inline; `string`→`i8*`; `list/map/buffer`→their {len,cap,data}-ish inline structs;
  nested `Model` field → the nested struct **inlined**.
- **Model local** `decl [e, M, …]` → `alloca %M` (stack struct). No-init = zeroed struct.
- **`list<M>`** → elements are **inline `%M` structs**, stride = `sizeof(%M)` (`elem_size` in serNodes
  LIST node). `map<K:M>` similarly stores inline `%M` values.
- **`at`/`get`/`first`/`last`/`obj.field`** (model) → a **shallow struct copy** into a temp (the
  "view"): scalars copied, inner `list/map/string` heap **pointers shared** with the source. This is
  the double-free root.
- **By-value model arg** → struct copy (borrow); ABI per target (byval / pointer).
- **Ser/deser** → `serNodes` (flat `AimlTypeNode[]`) drive encode/decode/`aiml_ser_free_decoded`/
  `aiml_ser_deep_copy`. STRUCT node = inline nested model; LIST/MAP nodes carry elem size + elem class.
- **Scope exit / drops-on** → `aiml_ser_free_decoded(&local, serNodes, n)` deep-frees the inner heap
  (the current crash site when two views share it).

## 2. Target representation (v2)

- **Model with heap fields** → allocated by `aiml_model_alloc(sizeof %M)` (8-byte MODL header,
  refcount 1). A model **local/field/element holds a `%M*`** (pointer to the shared shell), not an
  inline struct.
  - *Scalar-only models* (no heap fields) can STAY inline value structs — refcounting buys them
    nothing and inline is cheaper. `ctx.classes[name].hasHeapFields` already exists; gate on it.
- **Field access** `e.field` → GEP **through the pointer** (auto-deref). Scalar field = load; model
  field = the stored `%M*` (shared).
- **`at`/`get`/member (model)** → load the stored `%M*` + `aiml_model_incref` → a **reference**.
- **Container `list<M>`/`map<K:M>`** → stores `%M*` (stride 8). `push`/`put`/`idx`/member-store =
  `incref` the stored ref, `decref` the slot's old ref. `remove`/free = `decref`.
- **`return` / by-value arg** → pass the `%M*`; the holder `incref`s for its lifetime (caller-return:
  refcount unchanged, ownership moves to caller; callee-arg: incref on entry, decref on exit — or the
  simpler "borrow, no count" if the callee cannot outlive the call, TBD in Phase 3).
- **Scope exit / rebind / container-remove** → `aiml_model_decref(obj, serNodes, n)` (frees at 0).
- **`at_copy`/`copy(x)`** → `aiml_model_alloc` + `aiml_ser_deep_copy(new, obj, serNodes, n)` → fresh
  refcount-1 independent object. (`aiml_ser_deep_copy` already exists.)
- **Ser/deser/KV** → decode allocates each model via `aiml_model_alloc` and stores `%M*`; `kv.get` =
  shared ref into the decoded cache (incref), `kv.get_copy` = deep copy (already implemented).

## 3. Phases (ordered; each builds under the flag, gate before flip)

- [x] **P0. Runtime primitives** — `aiml_model_alloc/incref/decref/refcount` (`src/sys/model_runtime.*`).
      DONE, inert. `aiml_ser_deep_copy`/`aiml_ser_free_decoded` already exist for `at_copy`/decref.
- [x] **P1. Flag + type representation (core).** `ctx.modelRefcount` (env `AIML_MODEL_REFCOUNT`,
      read in both compile() AND registerTypes() — forward-decl runs first). A heap-field model local's
      alloca holds a `%M*` (pointer element type → existing `baseElemTy->isPointer()` auto-deref gives
      field read/write); `decl` no-init/`{new:M}` → `aiml_model_alloc`; move (new/producer) vs incref
      (variable/at share) at init. `visitNewExpr` uses `aiml_model_alloc`. Scalar-only models + OFF
      unchanged. **Field/rebind for a model *field of another model* not yet done — see P3/P6.**
- [x] **P2. Reference reads (list).** `at`/`get` on a `list<Model>` return the loaded `%M*` (a
      borrow; `visitDeclStmt` increfs when binding). member-access on a model local works via P1
      pointer auto-deref. **`first`/`last` and struct-field model-elem reads still TODO.**
- [x] **P3. Containers store `%M*` (core).** `list<Model>`: init (8-byte stride), push (incref +
      store pointer), get/at (load pointer), idx (incref-new-before-decref-old, self-alias safe),
      scope-free elemKind 3 (decref each). `map<K:Model>`: put (incref + store), get (load pointer),
      free valKind 3. All verified reference-semantic + valgrind-clean. **`remove` decref, `contains`/
      `index_of`/`sort` (identity vs deep), and map local scope-drop (map-drop disabled) still TODO;
      element stride in nested-model FIELDS is P6.**
- [x] **P4. ABI: return + by-ref arg.** DONE. A refcount-model arg passes by-ref as `%M*` (borrow, no
      count; param symbol has a pointer element type → field read AND write auto-deref). A
      refcount-model return is a `%M*`; `return <model_local>` MOVES (kind-7 decref suppressed for the
      returned object via retModelAddr). Forward-decl + definition + registerTypes env-read all aligned.
      Verified end-to-end (make→move-return + by-ref in-place mutation) + valgrind-clean.
- [x] **P5. Scope-exit → decref (core).** kind-7 ScopeDrop = `aiml_model_decref(obj, serNodes, n)`;
      emitted unconditionally on block-exit AND return paths (a decref is always safe, never
      mayDrop-gated), except the moved-out returned object. **Rebind (`set:[local, other_model]`)
      decref-old/incref-new not yet done.**
- [x] **P6. Ser/deser (core) — nested + element models.** `AIML_TD_MODEL` (kind 11) descriptor:
      encode follows the `%M*` (marker + struct bytes, wire UNCHANGED); decode allocates the shell via
      `aiml_model_alloc`; free decrefs (frees inner heap at the subtree index + shell at 0); deep_copy
      = encode+decode inherits it. serBuildSubtree emits it for a direct model field, `list<Model>`
      (esz=8) and `map<K:Model>` (vsz=8); struct construction makes a direct model field a `%M*`;
      initClassContainerMembers inits accordingly. **snShift fixed to shift `AIML_TD_MODEL.param1`**
      (was the nested-Buff-i32-came-back-0 bug). Verified: `Zone{buffs:list<Buff>}` field push/at +
      KV round-trip (encode via set / decode via get_copy) value-correct; scope-free valgrind-clean.
      **REMAINING (P6b): the typed-KV `get`/`get_copy`/`get_copy_at` methods must return/decode into a
      MODL pointer for a refcount-model value** — today they decode into a struct buffer, so the
      decl's kind-7 decref of the result reads a non-MODL pointer (a harmless-magic-mismatch but a
      valgrind Invalid read + the decoded value is not refcount-managed). This is the last KV piece.
- [~] **P4b. Class-method ABI.** DONE for signatures: the method forward-decl (registerTypeDecl) +
      definition (visitFunction) both build a refcount-model arg/return as `%M*`. The 9rooms game now
      COMPILES under `AIML_MODEL_REFCOUNT=1` (all IR verifies).

## Integration status (2026-09-14) — CORE DONE, runtime integration NEXT

Every v2 mechanism is implemented and **valgrind-clean via isolated probes**
(`tests/_mem_sanitize/v2/`): model locals, by-ref args + move-return, list/map reference
containers (push/get/at/idx/put), nested model fields, `list<Model>`/`map<Model>` fields, and the
full **nested-model KV round-trip** (encode/decode/free). The game **compiles** under v2.

But a real 9rooms test (`test_creature_damage`) **SIGSEGVs at runtime under v2** — the game exercises
model-usage patterns the probes don't yet cover. Remaining integration work (the next focused phase,
best driven by the `MALLOC_CHECK_=3` + gdb + `ns=__aiml_sernodes_X` recipe to pinpoint each crash):
- **Rebind / member-store of a MODEL field/local** (`set:[parent.model_field, x]`, `set:[local, m]`):
  store the `%M*` with incref-new + decref-old (like idx). NOT yet implemented → likely the current
  crash (a model field store goes through the generic struct/scalar path).
- **`ptr<Model>` interplay** with refcount models (boxes, `<`-self-pointer, get_base fat-pointers).
- **classgroup** instances + `sees`-collaborators holding models.
- **interface vtable dispatch** with model args/returns (sret-stub skip for model returns).
- **`list.remove`/`first`/`last`/`contains`**, **map local scope-drop**.
Then: run the full 9rooms suite with drops-on + refcount-on and watch category-A crashes go to 0.

### Integration findings (2026-09-14, from `test_creature_damage` under v2)
- **DONE:** rebind + member-store of models (P5b) — `set:[local,m]` / `set:[parent.model_field,m]`
  incref-new/decref-old; class-method ABI signatures; game compiles under v2.
- **CURRENT CRASH:** `do_deal_damage` (effect_handlers.aiml:670) `set:[mst.ctrl_player_id,
  ctrl.player_id]` → `aiml_str_incref` on ASCII bytes (a string read as a pointer). `ctrl =
  at(mat.players, ctx.source_owner_idx)`; either the index (read from `ctx: ptr<EffectCtx>`) or the
  fetched `%PlayerState*` is garbage → a layout/offset mismatch somewhere in the
  MatchSession/EffectCtx/PlayerState graph as built-vs-read under v2. Needs the
  `MALLOC_CHECK_=3`+gdb+`ns=__aiml_sernodes_X` recipe to trace which model is mis-laid-out.
- **PINPOINTED (2026-09-14, aiml -r source-instrumentation):** the crash is NOT the ptr<> strip —
  `mat:ptr<MatchSession>` / `ctx:ptr<EffectCtx>` args work (do_deal_damage read `ctrl.player_id="me"`,
  `opp.player_id="opp"`, `ctx.source_owner_idx=0`, `players.len=2` all correctly). It crashes at
  `fxp.find_creature_zone(mat, ctx.source_iid)` → `locate_in_combat` iterating **`combat_zones`, a
  `list<list<ZoneEntry>>`** (nested containers of a refcount model) → `aiml_str_incref` on ASCII
  bytes = a `ZoneEntry` string field read at a wrong offset / a garbage element. **Next: fix
  nested-container-of-models (`list<list<Model>>`).** The outer `list<list<ZoneEntry>>` stores inline
  SerList structs (esz=serListHeaderSize); each inner `list<ZoneEntry>` stores `%ZoneEntry*` (esz=8,
  MODEL elem). Suspect: the inner-list VIEW obtained via `at(combat_zones,i)` + push/idx-writeback
  (store_entry_at/load_entry_at helpers) doesn't round-trip the `%ZoneEntry*` elements correctly, or
  a ZoneEntry was pushed as an inline struct somewhere. Add a `list<list<Model>>` probe first, then
  fix. (The ptr<> strip is still worth cleaning up but is NOT this crash.)
- **FIXED (96bdb3f):** `set:[model_local, at(..)/view/deserialized]` hit two v1 value-model set
  paths (set-view deep-copy + "user struct from i8* → memcpy") that wrote a full struct into the
  model local's 8-byte `%M*` slot → the local held the first field (a string ptr) as its "value".
  Both now `isRefcountModel`-gated; the v2 rebind stores the `%M*`. Isolated repro
  (`set out=at(lez,0)` → `out.iid=cr7`) fixed. The basic `list<list<Model>>` probe already passed.
- **STILL CRASHING (next):** `test_creature_damage` under v2 still SIGSEGVs in `do_deal_damage` at
  `aiml_str_incref` on ASCII bytes — a `ModStat` string-member-store (`set:[mst.<str>, src.<str>]`,
  mst a fresh refcount model) or the `fill_owner_zone(block,&mst)` path reads/writes a `ModStat`
  string field at a wrong offset (ModStat may have a nested model/list field shifting offsets).
  Bisect further inside do_deal_damage (665-690) per-line.
- **LESSON:** an incremental `exodus build` can leave a STALE binary that crashes; a spurious "OFF
  regression" vanished after a CLEAN rebuild. Always clean-rebuild before concluding an OFF
  regression. OFF confirmed unbroken at HEAD (creature_damage PASS 3/3, pristine env).
- **DESIGN HAZARD (secondary):** `CompilerContext::isRefcountModel` STRIPS a leading `ptr<`/
  `ref<` wrapper, so `ptr<Model>` and value `Model` are treated identically (both → i8p reference,
  MODL alloc). That conflates an explicit box with a value reference and mishandles
  `list<ptr<Model>>` / `ptr<Model>` boxes (e.g. a `ptr<M>` decl's `ci=classes.find("ptr<M>")` misses
  → no scope-decref registered → leak; a box-drop kind-4 `free(obj)` on a MODL frees `obj` not
  `obj-8`). Decide `ptr<Model>` semantics under v2 (likely: do NOT strip — `ptr<Model>` stays an
  explicit borrow/box, only bare `Model` is the refcounted value) BEFORE chasing the crash, since it
  shapes the fix. 9rooms uses `ptr<Model>` pervasively (services, `<`-self-pointer, get_base).

- [ ] **P7. `at_copy` / `copy(x)` builtins.** Opt-in deep copy (alloc + `aiml_ser_deep_copy`). Also
      useful under v1, so can land independently and early if wanted.
- [ ] **P8. Weak back-refs.** Mutual/back-ref `ptr<>` fields default weak (no incref/decref) or
      `{weak:true}`; forward edge owns. Reuse the `aiml_recursive_ptr_ser_guard` DFS-cut machinery.
- [ ] **P9. Retire v1 escape hacks.** Remove return-view / set-view / idx self-alias deep-copies and
      the cascade box-drop exclusion (refcount makes them unnecessary). KEEP the container **string**
      ownership fix (dup on insert) + the narrow literal-into-field dup + immortal `""` — strings stay
      value-owned by containers, independent of the model decision.

## 3a. P4 ABI — detailed design (staked out 2026-09-14)

**Finding that de-risks the whole phase:** model args are the *only* non-container aggregate still
passed **by value**. `list<T>`/`map<K:V>` already pass **by reference** as an opaque `i8p` via the
`is_byref_arg` path (`visitFunction` ast_compiler.cpp:4632 sets `byref` for `list<`/`map<`; the
call-site derefs at ast_compiler_call.cpp:670; `argIsByref` is parallel to `argTypes`). **v2's model
ABI reuses this exact machinery** — a heap-field model arg becomes a byref `i8p` just like a list.

### Signatures (flag ON, heap-field models only; scalar-only models stay by-value structs)

| | v1 today | v2 (P4) |
|---|---|---|
| **arg** `[m, M]` | `resolveType(M)` = `%M` struct, by-value (private copy / borrow) | `i8p` = `%M*`, **by-ref borrow** (`is_byref_arg`); callee sees the caller's shared object |
| **return** `M` | `%M` struct by-value (+ WASM sret stub, ast_compiler.cpp:44 `getOrCreateSretStub`) | `i8p` = `%M*`; **sret stub disappears** for heap-field models (pointer return needs no struct lowering) |
| **interface/vtable** | `this`=i8p, model params by-value struct, struct-return via sret stub in the vtable slot | model params/returns become `i8p` → uniform with `this`/byref args; **no sret stub** for those slots |
| **classgroup cross-unit** (`g_runtime`) | same as free fn | same — signatures shift with the free-fn rule, both sides recompiled together |

### Refcount placement (the rules)

- **arg = borrow, NO incref/decref.** The caller holds a live reference across the whole call, so the
  callee borrows for free — identical to how list/map args are not refcounted. If the callee *stores*
  the arg into a container/field that outlives the call, **that store increfs** (P3), so lifetime stays
  balanced without touching the arg itself. (`argLifetimes`/spec/61 §3.1 already models
  scope-vs-consumed; a `consumed` model arg = the callee takes ownership → decref on callee exit.)
- **return:** reuse the ownership analyzer's owned-vs-borrowed verdict (`ownsReturn`/`freshOwned`):
  - returning an **owned fresh** object (`return {new:M}` / a producer local) → **move** (transfer the
    existing ref; refcount unchanged; the local's scope-decref is suppressed — the return consumes it).
  - returning a **borrowed reference** (`return at(list,i)` / `return <.field`) → **incref before
    return** so the caller gets its own ref while the container keeps its own. This *replaces* the v1
    return-view deep-copy hack (P9).
- **caller of a model-returning fn:** binds the returned `%M*` into a local that now owns a ref;
  scope-exit decrefs it (P5).

### What P4 touches

- `visitFunction` (ast_compiler.cpp:4632): extend the `byref` predicate to heap-field models when
  `ctx.modelRefcount`; set `argIsByref`/`is_byref_arg` for them.
- Forward-decl in `registerTypeDecl` (ast_compiler.cpp:3531 `byref` block) — **must** use the identical
  predicate or signatures mismatch (the note already warns about this for list/map).
- Call-site arg passing (ast_compiler_call.cpp): pass the model local's `%M*` (already a pointer under
  P1); a by-value struct-arg caller path is retired for these under the flag.
- Return (`visitRetStmt`): return the `%M*`; incref-if-borrowed / move-if-owned per above.
- `getOrCreateSretStub` + vtable slot wiring: skip the sret stub for heap-field model returns.

### Dependency + first testable slice

P4 is **not runnable before P1** (the object must be a heap `%M*` for a pointer ABI to mean anything —
a by-ref stack struct cannot outlive the callee, defeating the point). The **minimal coherent,
valgrind-testable unit** is **P1-core + P4-arg + P4-return**, no containers/`at` needed:

```
- def: {name: bump, args: [[obj, Thing]], body: [ - set: [obj.tag, "hit"] ]}   # mutate through byref
- def: {name: make, return: Thing, body: [ - decl: [t, Thing, {new: Thing}], - return: t ]}  # move-out
- def: {name: main, return: i32, body:
    - decl: [t, Thing, {call: [make]}]     # P1 alloc via producer + move; t owns the ref
    - call: [bump, t]                       # P4 arg byref → in-place mutate
    - # assert t.tag == "hit"               # proves shared in-place mutation
    - return: 0 }                            # P5 scope-decref frees once
```
`Thing` must carry a heap field (e.g. `tag: string`) so it is a MODL object (scalar-only stays inline).
Probe `_mem_sanitize/model_ref_arg_return.aiml` under valgrind: correct value + bounded RSS + no
double-free. This slice is the recommended first implementation increment.

## 4. Gating (every phase, non-negotiable)

- `_mem_sanitize/` probe(s) under valgrind in `run_memory_sanitizers.sh` (bounded RSS + correctness).
- Full 9rooms suite with **the deployed flags** (`AIML_LIFETIME_DROPS=1` + `AIML_MODEL_REFCOUNT=1`),
  compared to the 134/149 baseline — no regressions, crashes trending to 0.
- WASM link + a headless WASM smoke (models cross the ABI + serializer differently on wasm32).
- Only flip the config default ON when the suite is green with it.

## 5. Risk register

- **Stride/`elem_size` assumptions**: many sites assume `list<Model>` stride = `sizeof(struct)`. P3
  must find them all (grep `elem_size`, `getTypeAllocSize` on model types, container ABI). A miss =
  silent corruption (the WASM `nested_container_stride` class).
- **ABI (P4)**: pointer args change calling convention vs the current byval/pointer-struct mix;
  interface/vtable dispatch + classgroup cross-unit calls must agree. Highest-risk phase.
- **Mixed representation during build-out**: the flag keeps OFF green, but ON is only coherent once
  P1–P6 all land — expect ON to be red mid-sequence. Do not flip until the set is complete.
- **Cycles (P8)**: a missed back-ref that increfs → a leak (not a crash); `AIML_MEM_DIFF` catches it.
- **KV on-disk compatibility**: keep the wire/disk encoding identical (P6) so existing saved data +
  schema fingerprints still load.

## 6. Sequencing note

P7 (`at_copy`) and the P1 flag scaffolding are the safe first coding steps (additive / OFF-gated).
P3 (containers) and P4 (ABI) are the heavy, high-risk middle. Recommend landing P1+P7 first (small,
reviewable), then a focused push on P2→P6 as one coherent representation change gated as a unit.
