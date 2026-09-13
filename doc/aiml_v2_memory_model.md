# AIML v2 Memory Model — Reference Semantics for Models

**Status:** DECIDED (2026-09-13). Supersedes the spec/61 lifetime-drops retrofit for models.
**Decision owner:** Nikolai. **Scope:** how value-models with heap fields are copied, shared, and freed.

## 1. Why (what the v1 retrofit proved)

spec/61 lifetime-drops was bolted onto an undecided model semantics. The root conflict:

- `at`/`get`/member-access return a **shallow view** (the struct is copied, but its inline
  `list`/`map`/`string` heap pointers are **shared** with the source).
- 9rooms code **mutates those views in place** — `ent = at(zone, i); ent.apply_buff(b)` is expected
  to change the entry *in the zone*.
- Automatic freeing (drops-on) then frees the **same** inner heap from two owners → the pervasive
  double-free / UAF class (ZoneEntry/PlayerState/MkpEffPool/…). Retrofitting per-escape deep-copies
  does not converge (many distinct roots) and, where it does converge (full value-semantics), it
  **breaks the game logic** that relies on shared in-place mutation (proven live on
  `enchantment_picker`: `buffs=0`).

Conclusion: the semantics must be **decided**, not patched. We decide **reference semantics**.

## 2. The decision

**A value-model with heap fields is a shared, reference-counted heap object.** Binding, passing,
returning, and extracting a model **shares** it (refcount++), never deep-copies. Mutations are
in-place and visible to every holder. The object is freed **exactly when its refcount reaches 0** —
never based on "who dropped first". This eliminates the double-free class *by construction*.

Whoever explicitly wants an independent copy uses **`at_copy`** (and friends) — opt-in, never the
default.

### 2.1 Operation semantics

| Operation | v1 (today) | v2 (reference semantics) |
|---|---|---|
| `decl [e, M, {at:[list,i]}]` | shallow view (shares heap, not owned) | **reference** to `list[i]` (refcount++) |
| `decl [e, M, x]` (x a model local) | shallow copy | **reference** to `x` (refcount++) |
| `at`/`get`/`first`/`last` (model elem) | shallow view | **reference** |
| `obj.field` (model field) | shallow view | **reference** |
| `list.push(e)` / `map.put(k,e)` / `idx:[l,i,e]` | move + suppress / shallow | **store reference** (refcount++); slot's old ref decref'd |
| member-store `<.field = e` | move / deepcopy | **store reference** (refcount++); old decref'd |
| `return e` | move-out | **return reference** (refcount stays; caller now holds it) |
| by-value model arg | shallow copy (borrow) | **reference** (callee holds a ref for the call) |
| scope exit / rebind / container-remove | drop frees heap | **decref**; free only at 0 |
| **`at_copy` / `get_copy` / `copy(x)`** | (get_copy exists on Kv only) | **explicit deep-copy** → a fresh, independent object (refcount 1) |

Scalars, enums, and `list`/`map`/`buffer` of scalars keep value semantics (copied inline) — this
decision is about **models** (structs with heap fields) and `ptr<Model>`.

## 3. Runtime representation

- Every model heap allocation carries a small header, like strings do today
  (`{u32 magic 'MODL', u32 refcount}`), returned pointer skips the header.
- `aiml_model_incref(obj)` / `aiml_model_decref(obj)`: magic-guarded, null-safe, poison-on-free.
  `decref` at refcount 1 runs `aiml_ser_free_decoded` (deep-free the inner heap: strings decref,
  nested models decref, lists/maps free) then frees the shell.
- Strings already have this exact scheme (`aiml_str_alloc` + `aiml_str_incref/decref`,
  `AIML_STR_IMMORTAL` sentinel for the default `""`). Model refcounting mirrors it — reuse the
  pattern, don't reinvent.
- **Immortal empties**: default-init unset model string fields to the immortal `""`
  (`aiml_str_empty()`), never a `.rodata` literal (v1 bug §12: `decref` of a raw literal faults on
  its `-8` header). Same principle applies to any shared default.

## 4. Cycles

Reference counting cannot free cycles. AIML already has the machinery to deal with mutual `ptr<>`
back-references in serialization (DFS-cut guard — see `aiml_recursive_ptr_ser_guard`). v2:

- Mutual/back-reference `ptr<>` fields are **weak** by default (do not incref, are not decref'd),
  or annotated `{weak: true}`. The forward/ownership edge increfs; the back-edge borrows.
- This matches how the game already models back-refs (subcomponents for screen-splitting).

## 5. What this eliminates / enables

- **No double-frees, no UAF from sharing** — freeing is refcount-gated, order-independent.
- **In-place mutation stays free and correct** — 9rooms's `ent = at(...); mutate; (writeback
  optional)` works unchanged; explicit `store_entry_at` writebacks become harmless no-ops (they
  re-store the same object) and can be removed over time.
- **No per-read deep-copy perf cost** (the value-semantics tax we rejected).

## 6. Migration (9rooms)

- The **default** case (share + in-place) already matches the code → **most of the game needs no
  change**.
- Sites that genuinely need an independent snapshot switch `at` → **`at_copy`** (e.g. taking a
  before-image for diffing, or storing a template you'll mutate independently).
- Explicit double-`at`+writeback idioms (`store_entry_at`, `remove_entry_at`) keep working; simplify
  later.
- KV: `kv.get` = shared reference into the decoded cache (already the intent); `kv.get_copy` =
  independent (already implemented). Align model `at`/`at_copy` to the same split.

## 7. Testing from day one (non-negotiable)

Every memory-touching feature ships with:
- a `_mem_sanitize/` probe (bounded-RSS + correctness) run under **valgrind** in
  `run_memory_sanitizers.sh` (the harness exists),
- `AIML_MEM_DIFF` leak-gating on the 9rooms suite,
- the 9rooms suite run with the **deployed** flags (drops/refcount ON) — not a different config than
  production. v1's cardinal sin was gating a different configuration than shipped.

## 8. To implement (checklist)

- [ ] Model heap header + `aiml_model_incref` / `aiml_model_decref` (mirror string_runtime).
- [ ] `at` / `get` / `first` / `last` / member-access on models → return reference + incref.
- [ ] Container stores (`push`/`put`/`idx`/member-store) → store reference + incref; decref old slot.
- [ ] `return` / by-value arg → reference (incref for the holder's lifetime).
- [ ] scope-exit / rebind / container-remove → decref (not free).
- [ ] **`at_copy`** (list/map element deep-copy) + `copy(x)` builtin — the opt-in independent copy.
- [ ] Weak back-reference rule for cyclic `ptr<>` (default weak or `{weak:true}`).
- [ ] Retire the spec/61 per-escape deep-copy hacks (return-view, set-view, idx self-alias) — the
      refcount model makes them unnecessary. Keep the container **string** ownership fix (dup on
      insert) — strings stay value-owned by containers, independent of the model decision.
- [ ] Error messages: carry line/column through `sourceNode` (separate track, do first — see §9).

## 9. Orthogonal quick win: error locations

Independent of the memory model: diagnostics currently print `:?` (no line/column) because source
positions are not threaded through `sourceNode` into `ctx.fmtLoc`. Fixing this is cheap and pays off
in every future debug session — do it first, it needs no v2.

## 10. Relationship to the current (v1) work

Already landed and worth keeping regardless: the container **string**-ownership fix (map/list dup +
dense free) that fixed the mana/event-log regression, and the immortal-`""` default. The v1
value-model deep-copy escape hacks (return-view / set-view / idx self-alias / cascade box-drop
exclusion) are **interim** and get retired by this model.
