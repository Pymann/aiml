# AIML lifetime lab — spec/61 memory-management target

Small, **self-verifying** probes that pin down the memory behavior mandated by
`spec/61_memory_management_DRAFT.md`. Each probe defines its own types, performs one
lifetime operation, checks the survivors, and returns exit code 0 (PASS) / 1 (FAIL).

## Implementation status (branch `mem-v3-compiler`, gate `AIML_MEM_V3`)

These probes are the spec/61 **acceptance target**. Implementation progress under
`MODE=spec61` (which sets `AIML_LIFETIME_DROPS=1 AIML_MEM_V3=1`):

**GREEN — 12/16:** 02 string share (§4), 04 ptr/new/scope-free (§6/§9.1), 05 ptr move +
list<ptr> access (§6.2/§5.4), 06 shared<T> co-ownership (§6.2), 07 ref borrow + {ref} (§8),
08 ref weak cycle (§5.2), 09 free release protocol (§6.1), 10 producer/consumed (§9.2),
11 borrow-return + `borrows:` (§8), 12 scope/persistent (§9), 13 array/dyn_array/cascade/map/
buffer/get_copy (§2.1/§5), 14 tensor/resource move-only (§7).

**RED — 4 remaining, each a large architectural feature (not yet built):**
- **01, 03 — container refcounting/aliasing (Option B).** `list`/`map` must become refcounted
  *handles* (heap `{rc,size,cap,data}`) so assignment shares and mutation aliases. Today they
  are inline value structs `{size,cap,ptr}` — an ABI change touching every container op. Largest.
- **15 — memoized deep-clone graph (§5.1).** `aiml_ser_deep_copy` must gain a memo map so a
  shared sub-object reached by two owning edges is cloned **once** (DAG-preserving) and cycles
  terminate. A serializer rewrite.
- **16 — thread move/guard (§5.3).** move-send (`chan.send`/`send_unique`), `spawn`, `shared`
  and `lock` all exist, but spawn arg-marshaling of `chan`/`shared`/`lock` hits an IR bitcast
  bug, and the statically-enforced `lock:` guard scope is unbuilt.

Deployed/OFF path is **byte-identical**: every change is gated on `AIML_MEM_V3` (default OFF) or
additive. Run the old model for comparison with `MODE=off`.

Tier legend below (T0/T1/T2) is retained for reference.

Run: `bash tests/lifetime/run_lifetime.sh`
Under the tracer: `TRACE=2 bash tests/lifetime/run_lifetime.sh`
Old model for comparison (where a probe is model-agnostic): `MODE=off …`

## The model these probes encode (spec/61)

- **Stack by default** (§3). A plain `decl: [x, T]` (no `new`) is a stack value; scalars and
  value models/structs live inline and are released at scope exit. Pass-by-value copies.
- **Strings & containers are refcounted handles** (§4/§5, Option B). `set`/pass/store **shares
  (incref)**, never deep-copies; freed at **rc 0**. Containers are **mutable → aliasing**: two
  names for one container *are* one container. Independence is explicit: **`copy`** (shallow,
  shared referents) / **`clone`** (deep, §5.1 graph contract).
- **Three heap reference kinds** (§6.2):
  - **`ptr<T>`** — unique owner; assign/pass/push is a **move** (source moved-out); released by
    the **`scope` default** at scope exit (§9.1) — you rarely write `free` (early release only).
  - **`shared<T>`** — opt-in, refcounted co-ownership; incref on share, freed at rc 0.
  - **`ref<T>`** — non-owning borrow (as a param) / **weak** (as a stored field); no incref,
    never freed through; the edge that **breaks reference cycles** (§5.2).
- **Aggregates compose** (§7): a model/struct/`array<T:N>` is copyable iff every member is;
  move-only if any member is (`ptr`, by-value resource); drop releases each member per class.
- **Qualifiers** (§9): `scope` (default drop), `persistent` (opt out — ownership escapes),
  `producer` (return owns/outer-independent), `consumed` (param sink / rc-retain).

## Probe index (spec § → probe → tier)

Tiers: **T0** runs on today's syntax & already ~correct · **T1** today's syntax, **new
behavior** (red until compiler) · **T2** needs **new syntax** not yet parsed (`shared<T>`,
`{ share: }`, `{ copy: }`, `{ clone: }`).

| Probe | Spec § | Tier | New symbols covered |
|---|---|---|---|
| 01_stack_value_passbyvalue          | §3, §7      | T1 | value model on stack, pass-by-value (scalar isolated, container field aliases) |
| 02_string_share                     | §4          | T1 | string assign = share/incref, literal never freed, new-buffer-on-modify |
| 03_container_aliasing_copy_clone    | §5, §5.1    | T2 | list aliasing on assign, `{copy}` shallow, `{clone}` deep |
| 04_ptr_new_scope_free               | §6, §9.1    | T1 | `{new}`, `ptr<T>`, scope-default free, early `free`, per-iter free |
| 05_ptr_move_list_access             | §6.2, §5.4  | T2 | `list<ptr<T>>`, ptr move, `at`=borrow, `pop`=move-out, `remove`, `set`(i) |
| 06_shared_coown_adopt               | §6.2        | T2 | `shared<T>`, `{share: p}` adopt, incref, freed at rc 0 |
| 07_ref_borrow                       | §8, §6.2    | T1 | `ref<T>` param borrow+mutate, `{ref: x}`, auto-borrow of a value var |
| 08_ref_weak_cycle                   | §5.2        | T2 | `ref<T>` weak back-edge breaks a cycle, owning `list<ptr<Card>>` |
| 09_free_release_protocol            | §6.1        | T1 | class-based release: string/list/map/`ptr`/`shared`/nested-model fields |
| 10_producer_consumed                | §9.2, §8    | T2 | `lifetime: producer` (return), `lifetime: consumed` (sink), use-after-move |
| 11_borrow_return                    | §8          | T2 | `return: {type: ref<T>, borrows: a}`, two-ref-param disambiguation |
| 12_scope_persistent                 | §9, §9.1    | T2 | `lifetime: scope` (default), `lifetime: persistent` (escape) |
| 13_array_dynarray_cascade_map_buffer| §2.1, §5    | T2 | `array<T:N>`, `dyn_array<T>`, `cascade<X>`, `map<K:V>`, `buffer`, `get_copy` |
| 14_tensor_resource_moveonly         | §7, §2.1    | T2 | `tensor` (Class-4), `tensor.free`/`.clone`, model-with-resource ⇒ move-only |
| 15_clone_graph                      | §5.1        | T2 | deep `{clone}` of a DAG: memoized shared sub-object, independence |
| 16_threads_move_shared_guard        | §5.3        | T2 | `chan`, `chan.send` (move), `send_unique`, `spawn`, guarded `shared`+`lock` |

Together these exercise **every new symbol** of spec/61: `ptr`/`shared`/`ref`, `new`/`free`,
`{share}`/`{copy}`/`{clone}`/`{ref}`, the `lifetime:` qualifiers + `borrows:`, the §5.4 access
verbs, `array`/`dyn_array`/`cascade`/`map`/`buffer`/`tensor`, and the §5.3 thread constructs.
T2 probes need syntax the compiler does not parse yet — they document the target and go green
as the rewrite lands.
