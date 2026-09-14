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
- [ ] **P1. Flag + type representation.** Add `ctx.modelRefcount` (env-gated). When ON, a heap-field
      model's LLVM local/field type becomes `%M*`; `decl` no-init emits `aiml_model_alloc`; field GEP
      auto-derefs. Scalar-only models unchanged. Old path fully preserved when OFF.
- [ ] **P2. Reference reads.** `at`/`get`/`first`/`last`/member-access (heap-field model) → load `%M*`
      + incref. Retire the v1 shallow-view copy on this path (flag-gated).
- [ ] **P3. Container element type = `%M*`.** `list<M>`/`map<K:M>` store pointers (stride 8, serNodes
      LIST/MAP elem = a MODLPTR kind). `push`/`put`/`idx`/member-store: incref new + decref old slot.
      `remove`: decref. Update `elem_size`/stride everywhere it is assumed `sizeof(struct)`.
- [ ] **P4. ABI: return + by-value arg.** Return hands out the `%M*` (ownership to caller). By-value
      model arg: incref on callee entry, decref on exit (or borrow-no-count if provably call-scoped).
- [ ] **P5. Scope-exit / rebind / remove → decref.** Replace the drops-on `aiml_ser_free_decoded`
      scope-drop for heap-field models with `aiml_model_decref`. Rebind decrefs old, increfs new.
- [ ] **P6. Ser/deser/KV.** `aiml_ser_decode` allocates nested/element models via `aiml_model_alloc`
      and stores `%M*`; `aiml_ser_free_decoded` decrefs them; encode follows the pointer. `kv.get` =
      shared ref (incref), `kv.get_copy` stays deep. Wire format on disk/wire is UNCHANGED (still the
      flat encoding) — only the in-memory decoded shape changes.
- [ ] **P7. `at_copy` / `copy(x)` builtins.** Opt-in deep copy (alloc + `aiml_ser_deep_copy`). Also
      useful under v1, so can land independently and early if wanted.
- [ ] **P8. Weak back-refs.** Mutual/back-ref `ptr<>` fields default weak (no incref/decref) or
      `{weak:true}`; forward edge owns. Reuse the `aiml_recursive_ptr_ser_guard` DFS-cut machinery.
- [ ] **P9. Retire v1 escape hacks.** Remove return-view / set-view / idx self-alias deep-copies and
      the cascade box-drop exclusion (refcount makes them unnecessary). KEEP the container **string**
      ownership fix (dup on insert) + the narrow literal-into-field dup + immortal `""` — strings stay
      value-owned by containers, independent of the model decision.

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
