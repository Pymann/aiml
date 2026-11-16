# [DEPRECATED] Lifetime & Ownership

> **DEPRECATED — superseded by `61_memory_management.md`.** This document is no longer a
> numbered spec. Its move/borrow model (no reference counting by default) has been replaced by
> the memory-management model in the new 61: five storage classes, refcounted strings **and**
> containers (Option B), `ptr<T>` = owning / `ref<T>` = weak, `scope`-default drops, and the
> `producer`/`consumed` lifetime qualifiers. Kept for history only; do not implement against it.

## 1. Goal

Heap-bearing values (`string`, `list<T>`, `map<K:V>`, and models/structs with such
fields) must be released deterministically when their owner dies, so repeated work
with a constant working set keeps a bounded live heap. AIML achieves this with
**explicit, compile-time single-owner semantics** — not garbage collection (runtime
reachability tracing) and not whole-program escape analysis. Ownership is readable
**locally** from a symbol's type, its initializer, and the operations applied to it;
a reader (human or tool) never has to trace the whole program to know a symbol's fate.

Cost model: every release is a `free` emitted at a **known** point at compile time
(like a C++ destructor / Rust drop). There is **no** runtime tracing, **no** reference
counting, and **no** hidden per-variable runtime flags. Transferring ownership is a
pointer move (zero-copy).

## 2. The `lifetime` qualifier

Ownership is expressed with a qualifier on the **variable declaration**, alongside the
existing decl qualifiers. The qualifier is a **modifier map**, and — like every other decl
modifier (`const`, `volatile`, …) — it lives in the **4th position**, *after* the initializer;
it is never a separate position that competes with the init-value:

```aiml
- decl: [x,   MatchSession, <init>, { lifetime: scope }]      # scope is the default — omit it
- decl: [buf, MatchSession, null,   { lifetime: persistent }] # outlives this scope; no init → null
- decl: [h,   FileHandle,   <init>, { lifetime: consumed }]   # MUST be moved out exactly once
```

**Position, and why there is no ambiguity.** The decl grammar is `[name, type, init, {mods}]`.
The initializer always occupies position 3 and the modifier map always position 4, so a map
in position 4 is unambiguously a qualifier and a map in position 3 is unambiguously an
init-value. When a qualified local needs **no** initializer (e.g. an uninitialized
`persistent` buffer), put the `null` literal in position 3 (a struct-init no-op) so the
qualifier keeps its position-4 slot — never collapse the qualifier into position 3.
(**Parameters** are the one case where the qualifier sits in position 3: a parameter
declaration is `[name, type, {mods}]` and has *no* init slot, so a map there can only be a
qualifier — see §3.1.)

| Value | Meaning | Dropped at scope exit? |
|---|---|---|
| **`scope`** (default) | Owned; released at scope exit — unless consumed first (§4). | yes, unless consumed |
| **`persistent`** | Owned, but bound to a longer-lived owner (a member, a stored box, static data). | no |
| **`consumed`** | Linear obligation: MUST be moved out exactly once (coding aid, §5). | no when consumed; if it reaches scope exit un-consumed → **dropped** (with a warning, §5) so it never leaks |

Only heap-bearing values are affected. Scalars (`i32`/`f32`/`i64`/`bool`/enum) have no
heap; the qualifier is meaningless on them and is ignored.

## 3. Ownership from type + initializer (no qualifier for the normal case)

"Value type = owned" applies to **`decl` locals**, not to function parameters (§3.1).
For almost all symbols the owner is already visible **locally**, with no qualifier:

- A **value type** `T` (string/list/map/model), declared as a **local** via `decl`, is
  **owned** (default `lifetime: scope`).
- A **`ptr<T>`** obtained from `at` / `get` / `&` / a parameter is a **borrow**: it never
  owns, is never dropped, and is valid only within the owner's lifetime. **Borrows do not
  escape** — storing a borrow in a member or container is a compile error, which forces an
  explicit copy or move.
- A **`ptr<T>`** obtained from `{ new: T }` is an **owned box** (default `scope`: the
  pointee is freed at scope exit).

The qualifier is therefore an **escape hatch**, written only when deviating from the
default (`persistent`, `consumed`) — never once per declaration.

### 3.1 Parameters are borrowed by default

A function/method **parameter is a borrow by default**, whatever its type (value `T`,
`ptr<T>`, or by-reference `list<T>`/`map<K:V>`): the callee does **not** own it and
**never drops it** at function end; the **caller** retains ownership and releases it in
the caller's own scope. This is mandatory — otherwise every call would free its
arguments. In particular, a value-typed parameter is **not** subject to the
"value type = owned" local rule of §3, and a by-reference `list`/`map` parameter is
never freed by the callee.

To take ownership through a parameter, declare it **`consumed`** at the parameter
declaration (parameters are declarations, so the qualifier lives there — not a separate
signature syntax):

```aiml
- def:
    name: keep
    args: [[item, ptr<Thing>, { lifetime: consumed }]]  # this function takes ownership of item
    body:
      - push: [<.store, item]                           # item moved into the member (see §4)
```

### 3.1.1 `consumed` requires a reference/handle type

**A `consumed` parameter must be a reference/handle** — something that is *one handle to
one heap resource*, so that "move" is the transfer of that single owner. Concretely the
parameter must lower to a **pointer**:

- **`ptr<T>`** (explicit pointer / owned box),
- **`list<T>` / `map<K:V>`** (implicitly by-reference — an implicit pointer),
- **`string`** (a reference-counted heap handle),
- and any **built-in handle type** that lowers to a pointer (`path`, `thread`, file/socket
  handles, …). The rule is **structural** — the compiler accepts `consumed` on *any*
  parameter whose lowered type is a pointer — so new handle built-ins qualify automatically
  without touching this rule.

`consumed` is **rejected at compile time** on the two categories that are not single
handles:

- **A value `model`/`class` parameter** (passed as a *shallow struct copy* whose inner heap
  is shared with the caller): "moving" it is ambiguous and would double-free the shared
  inner heap. To move a model, pass **`ptr<Model>`** instead (or let the callee copy it with
  `push_from` / assignment, keeping the parameter a borrow).
- **A scalar** (`i32`/`f32`/`i64`/`bool`/enum): no heap, nothing to own.

Semantics of a `consumed` parameter: the caller's argument is **moved in** — it is
consumed at the call site (dead in the caller afterward; the caller does not free it),
and the callee now owns the handle and must consume or drop it (§5). The built-in
ownership-taking operations (`push`, and container inserts) behave as if their element
parameter is `consumed`; `return` moves the returned value to the caller. Everything
else — the common case, e.g. a function that only reads/mutates its argument or copies
from it (like a KV `set` that encodes a copy) — leaves the parameter a borrow, and the
caller keeps its value.

### 3.1.2 Implementation status

The two halves of a `consumed` move are wired in stages, so that any not-yet-complete
combination is a **leak (under-drop), never a double-free**:

- **caller-move** (drop the caller's ownership of a moved argument): implemented on the
  **free-function** call path and on **direct method calls** (`obj.m(x)` on a concretely
  typed receiver). **Interface (vtable) dispatch** does not apply it yet — it sees only the
  interface signature, which carries no lifetime — so `consumed` on a parameter of a method
  that **overrides an interface method is rejected at compile time** (calling only via the
  concrete type, or a free function taking the handle, works today).
- **callee-drop** (free an un-consumed `consumed` handle at function-body exit — the §5
  safety net): implemented for **`string`** (the only pointer-handle with a scope-drop
  primitive today). For `ptr<T>`/`list`/`map` the parameter is still accepted and
  caller-move applies, but there is no callee-drop yet — an un-consumed such parameter
  leaks (safe) until the box/list/map drop primitives exist (tracked with the leak work).
- **use-after-move** diagnostics (§4: using a moved value is a compile error) are not yet
  emitted; misuse is a latent leak/UAF only under programmer error, not from codegen.
- **owned-box scope-drops** (§3 `{ new: T }`, §3.2 `producer`) are implemented, gated by
  `AIML_LIFETIME_DROPS`: a value-model local (kind-0), an owned `ptr<Concrete>` box (deep-free
  pointee + free shell), and an owned `ptr<Interface>` fat pointer (per-class `__free_<Class>`
  in a trailing vtable slot, dispatched on the concrete type; null fat pointers skipped). Moves
  are removed by the same caller-move machinery (`return`/member-store/`push`/`put`), so an
  owned box that is handed on is transferred, not double-freed.

### 3.2 Return ownership: the `producer` return lifetime

Ownership qualifiers read in **two voices** that mirror the data flow: a **symbol is
`consumed`** (passive — §3.1: the parameter/value is taken), and a **function is a `producer`**
(active — it makes a fresh owned value and hands it to the caller). `producer` is therefore the
**output** counterpart of the `consumed` **input** qualifier.

§3 says a `ptr<T>` from `at`/`get`/`&`/a parameter is a **borrow**, and (implicitly) a value
model or `ptr<T>` obtained from a **general function call** is likewise treated as a borrow /
shared value — **not** owned by the caller, **not** dropped. This default is mandatory for
safety: a callee may `return <.member` (a getter handing back a **shallow view** that shares
the object's heap), so blindly dropping every call result would double-free that shared heap.

A function whose return **is** a freshly-allocated, independent owned value declares itself a
**producer** via the **object-form return** — the return type carries a `lifetime`, exactly as a
parameter declaration does (`{ lifetime: consumed }`):

```aiml
- def:
    name: get_base
    args: [[email, string], [card_id, string]]
    # object-form return: `producer` = this function makes a fresh owned value → the caller owns it.
    # (CardBase is an interface — the concrete box is a CreatureCard/ItemCard/…)
    return: { type: ptr<CardBase>, lifetime: producer }
    body:
      # Not-found / not-owner paths build NO box and return null; a null owned box is a
      # no-op at the caller's drop (the fat pointer is {obj=null, vtable=null}).
      - if: { condition: { eq: [{ call: [<.type_idx.has, card_id] }, 0] }, then: [{ return: null }] }
      - decl: [dat, CreatureData, { call: [<.creatures.get_copy, card_id] }]   # owned local (deep copy)
      - if: { condition: { neq: [dat.owner_player_id, email] }, then: [{ return: null }] }
      # A fresh owned box + populate it from the local copy.
      - decl: [obj, ptr<CreatureCard>, { new: CreatureCard }]
      - call: [copy_header, obj, dat]        # copies dat's name / abilities into obj (independent heap)
      - return: obj                          # obj is MOVED to the caller (upcast to ptr<CardBase>);
                                             # its own scope-drop is removed here, so no double-free.
      # `dat` is NOT returned → it stays owned and is deep-freed at this function's scope exit.
```

Every non-null path returns a box that this function just allocated (`{ new: … }`) and filled
with its **own** independent heap (via `copy_header` deep-copying from the owned `dat`) — that
is exactly what `producer` asserts, so it is safe for the caller to drop the result. `dat`
is a local owned copy that is *not* handed out, so it is dropped normally when `get_base`
returns (its heap does not escape into `obj`, which owns a separate copy).

With the annotation, a caller's **call-initialised local** owns the return and is **dropped at
scope exit** — unless it is moved first (return / member-store / push / put consume it, §4),
exactly like a `{ new: T }` box or a no-init value model:

```aiml
- decl: [resp, DeckListResp, { call: [g_decks.list, pid] }]   # g_decks.list is a producer
- call: [session.send_rpc, peer, "set_result", 0, resp]        # resp dropped at scope exit
```

The drop matches the return type:

- **value `model`/`class`** — deep-freed in place (`aiml_ser_free_decoded`).
- **`ptr<ConcreteClass>`** (owned box) — the pointee is deep-freed, then the heap shell freed.
- **`ptr<Interface>`** (fat pointer `{obj, vtable}`) — freed by **virtual dispatch on the
  concrete type**: each concrete class implementing an interface gets a synthesized
  `__free_<Class>` (deep-free + shell free) in a **trailing slot of that interface's vtable**;
  the drop loads that slot and calls it with `obj`. A **null** fat pointer (e.g. a
  `producer` function that `return null` on a not-found path) is skipped.
- **`list<T>`** — the caller's local is a kind-2 list-drop: the data buffer is freed and, for
  `list<string>`, each element is decref'd; for `list<Model>` with heap fields, each element is
  deep-freed. (A caller local initialized from a `producer` list call must **not** re-run the
  empty-buffer `initList` before the returned struct is memcpy'd in — that would orphan the
  freshly-allocated buffer; the compiler skips it for call/variable initializers.) `map<K:V>`
  producers are accepted syntactically but the caller-side map-drop is still **disabled** (see
  §8), so a `map` producer result currently leaks (safe) until the map-drop audit lands.

**When to declare `producer`:** a function that *builds* a fresh independent result — `decl
out; …; return out`, `return { new: T }`, or a deep copy (`get_copy`-style). **When NOT:** a
getter that `return <.member` (a shallow view) stays a plain `return: T` (default `scope`).
`producer` is **opt-in and fail-safe**: forgetting it on a fresh-builder is a **leak (safe)**;
putting it on a view-returning getter would double-free — so declare it only on genuine
owned-value producers. Move-detection (§4) removes the caller's drop when the value is handed
on, so a produced value that is itself re-returned or stored is transferred, never double-freed.

## 4. Consumption: `push`, `return`, and consuming functions

By agreement, **`push` and `return` always consume** (move). More generally, a
**consuming function** takes ownership of its argument.

- A **`scope`** local that is consumed becomes **moved / dead** afterward → it is **not**
  dropped at scope exit (consumption overrides the `scope` default). Using a moved value
  is a compile error (use-after-move).
- A **`persistent`** local is **exempt** from consumption: a consuming operation **copies**
  from it instead (the original survives), and it is not dropped at scope exit.
- A **`consumed`** local: see §5.

The copying counterparts stay explicit: `at_copy`, `push_from`, and assignment perform a
deep copy (the source keeps its own value and is dropped normally at scope exit).

## 5. `consumed` — linear coding aid

`consumed` enforces that an owned value is **handed off** rather than accidentally dropped
(e.g. a handle/resource that must be passed on):

- If a control-flow path reaches scope exit **without** having consumed the value → a
  **warning** ("consumed value is only conditionally / not consumed"), **and the value is
  still dropped** at scope exit (like a `scope` value) so it never leaks. `consumed` never
  skips cleanup — it only *warns* that you forgot the intended hand-off.
- Consuming it twice → a compile error (use-after-move).

It is purely a diagnostic; its runtime behavior when un-consumed equals a `scope` value
(dropped at scope exit); when consumed, it is moved out (not dropped).

## 6. No runtime drop flag → consumption must be statically determinate

AIML deliberately emits **no hidden per-variable runtime flag** ("drop flag"). The
consequence: a value's consumed state must be unambiguous at compile time on each
control-flow path.

- If a local is consumed on some branches but not others and the branches merge, the
  ownership state at the merge is ambiguous and the drop cannot be emitted unambiguously
  without a flag → **rejected** (compile error, both `scope` and `consumed`): consume it
  on *all* branches or *none* (or hoist the consumption before the branch, or use a copy).
  Uniform rejection of ambiguous ownership at a merge keeps the model flag-free.
- A value that is left **un-consumed on every path** is not ambiguous — it is simply
  **dropped** at scope exit (a `consumed` value additionally warns, §5). No leak.
- This is a slightly stricter **compile-time rule** in place of runtime overhead — zero
  runtime cost, locally readable. Real code consumes either unconditionally or
  symmetrically across branches.

## 7. Operation semantics (replacing the shallow-share middle ground)

Previously `at`/`get`/`set`/`push` shallow-copied and **shared** heap between two values,
which made ownership undecidable. The read/borrow verbs:

| Operation | Semantics |
|---|---|
| `at` / `get` | borrow (`ptr<T>` view); does not own (see §7.1 for invalidation) |
| `at_copy` | owned deep copy |
| `push_from` | deep copy into the container |

### 7.0 The single copy-vs-move determinant

Whether handing a source value to a target **moves** (transfers ownership, source dead,
zero-copy) or **copies** (deep copy, source stays valid) is decided by **exactly one**
rule — no per-operation special cases:

> **MOVE** iff **(a)** the target outlives the source (a member, a container slot, the
> caller via `return`) **and** **(b)** the source is an **owned, consumable** value
> (`lifetime: scope` or `consumed`). **COPY** in every other case.

Concretely:

| Source → / Target ↓ | `scope` / `consumed` owned (value or `{new}` box) | `persistent` | borrow (`ptr<T>` from `at`/`get`/param) |
|---|---|---|---|
| **outlives source** — member, container (`push`), `return` | **MOVE** (source dead) | **COPY** (deep; original survives) | **COPY** (deep; you don't own it) |
| **same/shorter scope** — local-to-local `set: [y, x]` | **COPY** (both valid) | **COPY** | **COPY** |

This resolves the earlier contradictions:
- A `scope` `{new}` box stored into a member → **move** (transfer, box dead) — the
  `init`-pattern keeps its members with zero clone.
- A `persistent` value/box stored anywhere → **copy** (it must survive; for a box this
  clones the pointee). "Owned boxes always move" was too strong: only `scope`/`consumed`
  owned boxes move; `persistent` copies.
- A borrow can never be moved (you don't own it) → always a deep copy.

Assignment `set: [dst, src]`: RHS evaluated first, the previous `dst` dropped, then `dst`
takes the new value per the table (self-assignment safe). To force a copy where the table
would move (keep the source AND give the target an independent value), use the explicit
copy verbs (`at_copy` / `push_from`) or assign from a copy.

### 7.0.1 The MOVE requires **dominance**; a conditional `push` falls back to COPY

A container `push` of an owned value-model is implemented as a zero-copy **shallow move**
(store the struct into the element slot, then drop the source's scope-drop). That is sound
only when the `push` **dominates the source's scope exit** — i.e. the `push` and the
source's `decl` live in the **same innermost block**, so the source is moved on *every* path
that reaches its scope exit. When the `push` is nested inside a **conditional** block below
the source's declaration scope (the archetype: a filter `if` inside a loop body, e.g.
`DeckService.list` pushing only the caller's own decks), the move happens on *some* paths but
not others. §6 forbids a runtime drop flag, so the compiler does **not** try to move-on-some,
drop-on-others; instead the `push` degrades to a **deep copy**:

> A `push` (or member-store) of an owned value-model **moves** iff it dominates the source's
> scope exit (same innermost block). Otherwise it **deep-copies**: the list/member gets an
> independent heap and the source **keeps its scope-drop**, freed normally on every path.

This keeps ownership unambiguous and flag-free without rejecting the ubiquitous
loop-filter-`push` pattern (which §6's merge rule, read literally, would reject): the
conditionally-pushed source is simply a `scope` local that is copied into the container and
dropped at its own scope exit like any other. The cost is one deep copy per conditional
`push` (the same cost as writing `push_from` explicitly); the unconditional case keeps the
zero-copy move. Failing to degrade here was the root cause of a real per-iteration leak
(`DeckService.list` leaked one `Deck` + its `card_ids` for every non-matching entry); the
regression probe is `tests/_mem_sanitize/conditional_push_drop.aiml`.

### 7.1 Borrow validity and invalidation

The escape ban (§3.1: a borrow may not be stored) is **not** enough on its own: a borrow
(`ptr<T>` from `at`/`get`/`kv.get`) can dangle **within its own scope** if its owner is
mutated. A borrow is **invalidated** by any of:

- `clear` / `remove` / a growth (reallocation) of the borrowed container,
- overwriting the owner (`set: [owner, …]`) or dropping it,
- `kv.set` on the key a `kv.get` view came from (cache eviction).

**Contract:** a borrow is valid only until the next mutation of its owner; do not use a
borrow across such a mutation. The compiler **diagnoses the locally visible cases** — a
borrow used after a mutating operation on the same owner within the same block is an error.
Cross-scope / cross-function cases remain a documented contract (as with C++ iterators and
today's `kv.get`), not a full borrow-checker. When in doubt, take an owned copy
(`at_copy` / `kv.get_copy`) instead of a borrow.

## 8. Diagnostics summary

- **use-after-move** (reading a moved `scope`/`consumed` local): error.
- **double consume**: error.
- **branch-divergent consumption** at a merge (consumed on some paths, not others):
  error — for both `scope` and `consumed` (§6, ambiguous ownership rejected).
- **`consumed` never consumed on any path**: warning (and the value is still dropped, §5).
- **storing/escaping a borrow** (`ptr<T>` from `at`/`get`/param into a member/container):
  error.
- **using an invalidated borrow** (a borrow used after a mutating op — `clear`/`remove`/
  grow/owner-overwrite/`kv.set` — on the same owner within the same block): error (§7.1);
  cross-scope invalidation is a documented contract, not statically enforced.

### 8.1 Planned: leak lint for undeclared owned returns (TODO, not yet implemented)

A future **green warning** (non-breaking, like the spec/49 static checks; honours `static_off`)
should surface the *silent* leak class that the `producer` return lifetime (§3.2) exists to
fix: a call-returned owned value that the compiler conservatively treats as a borrow and
therefore never drops, because the callee did not declare its return `producer`.

Flag `decl: [x, T, { call: f(...) }]` when **all** hold:

1. `T` owns heap — a model/class with heap fields, or `ptr<T>` / `string` / `list<>` / `map<>`.
2. `f`'s return is **not** `producer` **and** not a known borrow source (`at` / `get` / `&` / a
   parameter). `get_copy` is exempt (already owned → dropped).
3. `x` is **never consumed** on any control-flow path (no `return` / member-store / `push` /
   `put`) before scope exit.

→ *"`x` holds a call-returned heap value that is neither dropped nor moved; if `f` transfers
ownership, mark its return `producer`, otherwise it returns a borrow (suppress)."*

This is exactly the per-RPC `decl resp = svc.list(...); send_rpc(resp)` pattern, so the lint
doubles as the **worklist** for making every response-building method a `producer`. It is a
*warning*, not an error: borrow-vs-owned for an undeclared return is not provable (the reason
`producer` is opt-in), so a genuinely-discarded borrow is a tolerated false positive, resolved
by declaring the return `producer` (the real fix) or `static_off`. Feasible with the existing
analyzer state (`DeclStmt` + `CallExpr` visit, `ctx.classes` heap-field info, `ctx.ownsReturnFns`,
and the move/consume flow facts) — no new infrastructure. Optional later escalation: when the
callee's return is *provably* a fresh value (`return { new: T }` / a locally-built value),
strengthen the wording to "should be a `producer`".

## 9. Non-goals

- No garbage collection and no reference counting as a default. A future `shared<T>`
  (multi-owner, refcounted) would be a separate, rare opt-in and is **not** part of this
  qualifier.
- No escape analysis (whole-program inference of frees).
- Existing value semantics are unchanged (lists/maps passed by reference, model/class
  args by value); the only additions are deterministic release and the visible transfer
  rules above.

## 10. Relationship to existing specs

- [45 - Heap Serialization](45_heap_serialization.md): `aiml_ser_free_decoded` is the
  drop primitive for decoded model heaps; scope drops reuse it.
- [10 - Collections](10_collections.md): container element drops on `clear`/`remove`, and
  the `at`/`at_copy`/`push`/`push_from` verbs, follow §7.
- [31 - DB Integration](31_db_integration.md): `kv.get` = borrow, `kv.get_copy` /
  `kv.get_copy_at` = owned copy — already consistent with §3/§7.
