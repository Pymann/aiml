# Crash: `Kv<K:T>` get → mutate → set on same key (use-after-free in encoder)

**Discovered:** 2026-05-21 — `datacreatures` project, `handle_create_creature` path.
**Severity:** High. Crashes the host process (SIGSEGV) under a common usage pattern.
**Affected:** All typed `Kv<K:T>` where `T` is a struct containing string / list / map / buffer fields.
**Status:** Fixed 2026-05-21/22 — three structurally distinct APIs:
`kv.get` (zero-copy read), `kv.get_copy` (deep-copy at decl site), and
`ptr<T>` decl (zero-copy sync view).  `kv.set` is uniform — runtime detects
whether `val` aliases the cache slot and skips eviction when it does, so
mutate-and-set-same-key works for both the deep-copy and view patterns.
Regression tests: `tests/test_kv_get_mutate_set_same_key.aiml` (5/5),
`tests/test_kv_get_ptr_view.aiml` (3/3).  Full suite: 168 passed, 1 failed
(preexisting `test_db_kv_checkpoint`), 19 skipped (display).

---

## TL;DR

```aiml
- decl: [p, Player, { call: [players.get, email] }]   # 1. shallow copy from decoded_cache
- call: [p.creature_ids.push, c.creature_id]          # 2. mutates the shared heap buffer
- call: [players.set, email, p]                       # 3. SIGSEGV in aiml_ser_encode
```

`kv.get` returns a pointer into the `decoded_cache`; `decl: [p, T, kv.get(...)]`
only does a shallow `memcpy` of the struct bytes. All inner heap pointers
(strings, list data buffers) stay **shared** between `p` and the cache.

Mutations like `push` reallocate the shared buffer. On the following
`kv.set(same_key, p)` the runtime calls, as its **very first step**,
`kv_decoded_evict(kv, same_key)` — before the encoder runs. Eviction frees
all heap allocations of the cache entry, including the buffer that
is now also referenced in `p`.

The `aiml_ser_encode(p, …)` that directly follows dereferences dangling
pointers → SIGSEGV in `libc.strlen` via the `enc(STRING)` code path.

---

## Reproducer (minimal)

`tests/test_kv_get_mutate_set_same_key.aiml`:

```aiml
- model:
    name: Player
    fields:
      - [player_id,    string]
      - [email,        string]
      - [creature_ids, list<string>]

- decl: [kv, Kv<string:Player>, { call: [db.open_kv, "p.kvwal", {persist: true, path: "/tmp/aiml_repro/"}] }]

# Seed
- decl: [p1, Player]
- set: [p1.player_id, "id-1"]
- set: [p1.email,     "n@g.de"]
- call: [kv.set, "k", p1]

# get → mutate → set on SAME key → crash
- decl: [p, Player, { call: [kv.get, "k"] }]
- call: [p.creature_ids.push, "creature-x"]
- call: [kv.set, "k", p]      # ← SIGSEGV
- print: ["never reached"]
```

Variation that does **not** crash (different write key):

```aiml
- decl: [p, Player, { call: [kv.get, "k"] }]
- call: [p.creature_ids.push, "creature-x"]
- call: [kv.set, "k2", p]     # ← OK, eviction only hits "k2", not "k"
```

Variation that does **not** crash (no mutation):

```aiml
- decl: [p, Player, { call: [kv.get, "k"] }]
- call: [kv.set, "k", p]      # ← OK, cache + p share unmodified buffer; eviction is harmless
```

So the precondition is exactly the triple: **get + mutate + set-same-key**.

---

## Crash signature

```
=== AIML CRASH (signal 11 at addr=0x... tid=...) ===
backtrace:
 libc.so.6(+0x18b7dd)              # strlen
 libaiml.so(enc + line 159)        # STRING encode → strlen(str)
 libaiml.so(enc + line 183)        # LIST iteration → enc(element)
 libaiml.so(enc + line 168)        # STRUCT iteration → enc(field)
 libaiml.so(aiml_ser_encode + 0x93)
 libaiml.so(aiml_db_kv_set + 0x128)
 <user code>                       # handler that called kv.set
```

`addr2line` resolution against `src/sys/ser_runtime.cpp`:
- Line 159: `int32_t len = str ? (int32_t)strlen(str) : 0;`
- Line 183: `enc(buf, n.param1, ns, nn, base + i * esz);`
- Line 168: `enc(buf, fi + i, ns, nn, data + f.offset);`

So the encoder is iterating a STRUCT's fields, hits a LIST field, iterates
its elements, treats element 0 as a STRING, and segfaults inside `strlen`
because the `char*` it loaded is no longer a valid address.

---

## Root cause walkthrough

### Step 1 — `kv.get` returns a cache pointer

[`aiml_db_kv_get`](../../src/sys/db_runtime.cpp#L1509) for a typed persistent KV:

```cpp
if (kv->ser_nodes) {
    auto dc = kv->decoded_cache.find(key);
    if (dc != kv->decoded_cache.end()) {
        kv_decoded_touch(kv, key);
        return dc->second.data();        // returns cache pointer
    }
    std::vector<uint8_t> decoded((size_t)kv->val_size, 0);
    aiml_ser_decode(val_ptr, decoded.data(), kv->ser_nodes, kv->ser_n);
    auto& cached = kv->decoded_cache[key];
    cached = std::move(decoded);
    kv_decoded_touch(kv, key);
    kv_decoded_lru_trim(kv);
    return cached.data();                // returns cache pointer
}
```

`cached` is a `std::vector<uint8_t>` owned by the KV. `cached.data()` is a
pointer into that vector. Lifetime: until eviction.

Inside the decoded bytes, every `string`/`list<T>`/`map<K:V>` field holds a
**heap pointer** that was malloc'd by the decoder ([`ser_runtime.cpp:255-262`](../../src/sys/ser_runtime.cpp#L255)
for strings, [`ser_runtime.cpp:294-303`](../../src/sys/ser_runtime.cpp#L294) for lists). These heap blocks
are owned by the cache entry; their lifetime is bound to the cache slot via
`aiml_ser_free_decoded`.

### Step 2 — `decl: [p, T, kv.get(...)]` is a shallow memcpy

[`ast_compiler_stmt.cpp:296-304`](../../src/core/ast_compiler_stmt.cpp#L296):

```cpp
if (declTy && declTy->isStructTy() && initRaw && initRaw->getType()->isPointerTy()) {
    uint64_t sz = ctx.module.getDataLayout().getTypeAllocSize(declTy);
    ...
    ctx.builder.CreateCall(mcf, { unwrappedAlloca, initRaw, ctx.getSizeConst(sz) });
}
```

So `p`'s alloca receives `sizeof(T)` raw bytes from the cache. Every inner
pointer field — `player_id`, `email`, `creature_ids.data` — is now shared
between `p` and the cache. There is no recursive deep-copy of strings or list
buffers.

### Step 3 — Mutation realloc's the shared buffer

`p.creature_ids.push(x)` compiles to [`ListImpl::push`](../../src/core/list_impl.cpp#L56),
which checks `size == capacity` and `realloc`s the data buffer if full.

For a freshly-decoded empty list, the decoder set `size=0, capacity=4,
data=malloc(4*esz)` ([`ser_runtime.cpp:296-298`](../../src/sys/ser_runtime.cpp#L296)). So:
- Push to a 0/4 list → no realloc, just `data[0] = x`, `size=1`. The shared
  buffer now has 1 valid element. Cache and `p` both observe this.
- Push to a 4/4 (already-full) list → `realloc(data, 8 * esz)`. If realloc
  relocates, the OLD buffer is freed by `realloc` itself, and the cache still
  holds the freed pointer. If realloc grows in place, `p.data == cache.data`
  remains, with new contents.

Either way, at the end of step 3, `p` and the cache hold pointers to a
buffer whose lifetime is now mixed: the cache thinks it owns it, and `p` is
using it.

### Step 4 — `kv.set(same_key, p)` evicts the cache first

[`aiml_db_kv_set`](../../src/sys/db_runtime.cpp#L1450):

```cpp
if (kv->ser_nodes && val) {
    kv_decoded_evict(kv, key);                          // ← FREES cache + shared buffers
    char* blob = aiml_ser_encode(val, kv->ser_nodes, kv->ser_n);
    ...
}
```

[`kv_decoded_evict`](../../src/sys/db_runtime.cpp#L1087) calls
[`aiml_ser_free_decoded`](../../src/sys/ser_runtime.cpp) on the cached bytes,
which walks the type tree and `free()`s every string / list-data / map-data
heap block.

After eviction, `p.creature_ids.data` is a dangling pointer.

### Step 5 — Encoder reads through the dangling pointer

`aiml_ser_encode(val=p, ...)` walks the type tree and hits the list field:

```cpp
case AIML_TD_LIST: {
    const SerList* list = (const SerList*)data;
    int32_t count = list->size;            // still 1 (or whatever push left it)
    ...
    for (int i = 0; i < count; i++)
        enc(buf, n.param1, ns, nn, base + i * esz);   // base = list->data (freed)
}
case AIML_TD_STRING: {
    const char* str = nullptr;
    memcpy(&str, data, sizeof(char*));     // reads freed memory
    int32_t len = strlen(str);             // ← SIGSEGV
}
```

The dereferenced `data + i * esz` falls into freed heap; the `char*` loaded
from it is garbage; `strlen` crashes.

---

## Why this only manifests in some real flows

- **Test isolation never hits it.** Unit tests typically `kv.set` to fresh
  keys, or they `kv.get` without mutating, or they don't round-trip at all.
  The combination is unusual enough that the existing AIML test suite doesn't
  cover it (verified — none of `tests/test_kv_*.aiml` exercises it).
- **Different keys are safe.** `kv.get("a")` → mutate → `kv.set("b", p)`
  does not crash because eviction targets `"b"` only; `"a"`'s cache slot
  remains intact, and `p` continues to share live pointers with it until
  `p` goes out of scope.
- **No-op writes are safe.** `kv.get("a")` → `kv.set("a", p)` without
  mutation: both still hold the same unmodified heap pointers; eviction
  frees them once; encoder reads from the local copy but the underlying
  bytes haven't been modified. Wait — this would still crash if the
  eviction happens before encode. Empirically it doesn't (the test in
  `tests/test_kv_get_set_roundtrip.aiml` passes). I believe this works
  because the freed memory hasn't been reused yet, so the garbage matches
  the freed contents byte-for-byte. **This is a heisenbug — it might
  segfault under memory pressure or with a malloc that scribbles on free
  (e.g. `MALLOC_PERTURB_`).** Worth treating as latent crash.
- **The bug needs a list/map/buffer field.** Pure-primitive structs (no
  inner heap pointers) survive the eviction because there's nothing to
  free.

---

## Why `ptr<T>` does NOT fix it

A reasonable instinct is to switch to `decl: [p, ptr<Player>, ...]` to avoid
the shallow copy. This is **worse**, not better:

1. `p` would be the cache pointer itself, no memcpy.
2. `kv.set(email, p)` passes `val = cache.data()`.
3. Eviction in `kv_decoded_evict` frees the cache entry — including the top
   struct bytes themselves, not just the inner allocations.
4. Encoder then reads from a fully-freed region. Crash is even more
   reliable (failure begins at offset 0, not at the first list field).

`ptr<T>` is fine for **read-only** access (zero-copy queries). Use it when
you will not call `kv.set` on the same handle, and do not need to outlive
LRU eviction.

---

## Historical: original user-side workaround

Build a fresh local struct and copy fields with **string interpolation**
to force strdup — interpolation produces a brand-new heap allocation that
is independent of the cache.

```aiml
- decl: [p_old, Player, { call: [players.get, email] }]
- decl: [p,     Player]                              # fresh, zero-inited

# Strdup'd via interpolation — survives later eviction.
- set: [p.player_id, "${p_old.player_id}"]
- set: [p.email,     "${p_old.email}"]
- set: [p.created_at, p_old.created_at]               # primitives are by-value

# Fresh list — its data buffer was never seen by the cache.
- decl: [old_ids, list<string>, p_old.creature_ids]
- decl: [i, i32, 0]
- decl: [n, i32, old_ids.len]
- while:
    condition: { lt: [i, n] }
    body:
      - decl: [cid, string, { at: [old_ids, i] }]
      - call: [p.creature_ids.push, "${cid}"]         # strdup
      - set: [i, { "+": [i, 1] }]

# Append new entry (already a strdup'd hash from elsewhere — interpolate to be safe).
- call: [p.creature_ids.push, "${new_creature_id}"]

- call: [players.set, email, p]                       # safe: p has no shared pointers
```

This was applied in `datacreatures/sources/server/server_main.aiml ::
handle_create_creature` on 2026-05-21.  After the runtime/compiler fix landed
later the same day, the workaround was removed in favor of the clean
`{copy: true}` API.

---

## Applied fix (2026-05-21)

After exploring the runtime-side fix (encode-before-evict) and finding it
insufficient — because `push()` can `realloc()` the list buffer, leaving the
cache's `data` pointer stale and triggering a **double-free during eviction**
even after a successful encode — we settled on an **opt-in compile-time
deep-copy** at the `decl:` site:

### User API

Three structurally distinct patterns — the user's intent is visible at the call site
(separate method names + type system), no option bags / no behavioural overloading:

```aiml
# (1) kv.get — zero-copy READ.  Returns a pointer into the decoded cache;
#     decl: [p, T, …] does a shallow memcpy.  Safe for read-only use.
#     UB if the user then mutates p and writes back under the same key.
- decl: [p, Player, { call: [players.get, email] }]
- call: [print, p.player_id]                       # safe

# (2) kv.get_copy — deep-copy at the decl site (encode+decode roundtrip).
#     Destination has independent heap allocations.  Safe for any mutation,
#     including mutate-and-set-same-key.  Best when you also might NOT persist
#     (cache stays untouched).
- decl: [p, Player, { call: [players.get_copy, email] }]
- call: [p.creature_ids.push, c.creature_id]       # mutates p only
- call: [players.set, email, p]                    # safe

# (3) ptr<T> view — zero-copy alias to the cache slot.  Mutations land directly
#     in the cache; cache stays internally consistent because push/realloc
#     update the cache's list field in place.  kv.set detects val==cache and
#     skips eviction (cache IS the canonical state).
- decl: [p, ptr<Player>, { call: [players.get, email] }]
- call: [p.creature_ids.push, c.creature_id]       # mutates cache directly
- call: [players.set, email, p]                    # safe; encodes cache → WAL
```

`kv.set` is uniform — it inspects the value's LLVM type at compile time
(pointer vs struct, line 1218 of `ast_compiler_call.cpp`) and at runtime
detects when val aliases the cache slot (`val_is_cache` in `aiml_db_kv_set`),
choosing the appropriate path automatically.  No `set_view` variant needed.

### Implementation

Four layers:

1. **`src/sys/ser_runtime.{h,cpp}`** — new public function
   `aiml_ser_deep_copy(dst, src, nodes, n)` that does an encode-then-decode
   roundtrip.  Precondition: `dst` is zero-initialised.  `decode` malloc's
   fresh heap for every string/list/map/buffer field, so `dst` becomes
   fully independent of `src`.

2. **`src/core/ast_compiler_call.cpp` — Kv method dispatch** —
   * `methodName == "get_copy"` sets `ctx.kvGetCopy = true` and dispatches
     to the same runtime function (`aiml_db_kv_get`); the deep-copy itself
     happens at the consumer site.
   * `methodName == "set"` accepts both struct-value and `ptr<T>` args:
     if `valVal->getType()->isPointerTy()`, the pointer is passed through
     directly (no temp-alloca spill); otherwise the value is spilled to a
     temp alloca and the alloca pointer is passed.  This enables the
     `ptr<T>` sync-view pattern.

3. **`src/core/ast_compiler_stmt.cpp::visitDeclStmt`** — when initialising a
   struct from a pointer-returning initialiser, checks `ctx.kvGetCopy`.
   If set AND the class has heap fields (`ci.hasHeapFields && ci.serNodesVar`),
   emits `aiml_ser_deep_copy(dst_alloca, init_ptr, serNodes, n)` instead of
   the shallow `memcpy`.  Resets `ctx.kvGetCopy` after consumption so it
   only applies to the immediate decl.

4. **`src/sys/db_runtime.cpp::aiml_db_kv_set`** — two interrelated changes:
   * **Encode-first**: encode is performed before any cache eviction so a
     `ptr<T>` value pointing into the cache remains readable through encode.
   * **val_is_cache detect**: if `val` equals the cache slot for `key`
     (i.e. the `ptr<T>` sync-view pattern), the cache entry is dropped from
     bookkeeping but `aiml_ser_free_decoded` is NOT called on it.  This
     avoids double-freeing the user's pushed pointers (which may include
     string literals or other non-decoded pointers) while still allowing the
     next `kv.get` to re-decode fresh from WAL.

### Why not zero-copy by default + auto-detect?

We considered three alternatives:

| Design | Pros | Cons |
|---|---|---|
| **Separate methods (`get_copy`) + `ptr<T>` view (chosen)** | Zero perf cost for read-only `kv.get`; user intent visible at the call site; AIML stays structurally clean (no behavioural option bags) | User must remember which variant to use; static analysis would help |
| **`{copy: true}` option bag on `kv.get`** | One method, configured at the call site | Behavioural overloading on flag arg violates AIML's "explicit method per semantics" structure |
| **In-sync cache+instance via value-type** | No opt-in needed | Requires ABI extension (back-pointer in list/string structs) or per-mutation runtime hooks; very invasive |

The chosen design fits AIML's strong preference for **structural distinction**:
different intent → different method name (or different type).  `kv.get` and
`kv.get_copy` are two clearly different operations, and the `ptr<T>` view
pattern uses the type system rather than a flag.

The user must still pick the right pattern — forgetting `get_copy` and
mutating a value-type `T` then writing back under the same key is still UB.
Static analysis flagging that pattern is a worthwhile future enhancement.

## Earlier rejected runtime-only fix attempt

The minimal first attempt was to **reorder eviction in `aiml_db_kv_set`** so eviction
happens *after* the new blob is materialized:

```cpp
// File: src/sys/db_runtime.cpp::aiml_db_kv_set (line ~1450)

if (kv->ser_nodes && val) {
    // OLD: kv_decoded_evict(kv, key); aiml_ser_encode(val, ...);
    // NEW: encode first (still safe — val points into cache, cache still alive),
    //      then evict.
    char* blob = aiml_ser_encode(val, kv->ser_nodes, kv->ser_n);
    if (!blob) return;
    kv_decoded_evict(kv, key);   // ← now safe; val is no longer needed
    ...
}
```

This is correct because:
- `aiml_ser_encode` reads from `val` and writes to a fresh `std::vector`.
  It does not retain a pointer into the source after returning.
- After encode, the cache slot is no longer needed for this set operation;
  any future `kv.get` will repopulate it from the freshly-written WAL data.
- The new blob is written to disk after eviction completes, so on-disk
  state remains consistent.

**Risk:** If `aiml_ser_encode` itself triggered a `kv.get`/`kv.set` on the
same KV (it doesn't, but check), the cache state during encode would
be the old (now-stale) entry. Doesn't matter — encode only reads `val`,
not the cache.

A deeper fix would be to make `kv.get` return a **deep copy** for mutable
typed handles. That's a bigger refactor — every typed `kv.get` would
pay the deep-copy cost, defeating the zero-copy design goal of the
2026-04-30 mmap refactor. The reorder-eviction fix preserves zero-copy
for the read path while making mutation-and-set safe.

A complementary improvement: introduce an explicit `kv.snapshot(key) → T`
method that documents the deep-copy semantic, leaving `kv.get` for the
zero-copy read-only case. This puts the choice in user hands.

---

## Detection / regression test

Add to `tests/`:

```aiml
# tests/test_kv_get_mutate_set_same_key.aiml — regression for the
# get-mutate-set-same-key crash documented in doc/issue/.

- model:
    name: Bag
    fields:
      - [name,  string]
      - [items, list<string>]

- decl: [kv, Kv<string:Bag>, { call: [db.open_kv, "bag_repro", {persist: true, path: "/tmp/aiml_kv_repro/"}] }]

- decl: [b1, Bag]
- set: [b1.name, "first"]
- call: [b1.items.push, "alpha"]
- call: [kv.set, "k", b1]

- decl: [b, Bag, { call: [kv.get, "k"] }]
- call: [b.items.push, "beta"]
- call: [kv.set, "k", b]              # must NOT crash

- decl: [b2, Bag, { call: [kv.get, "k"] }]
- if:
    condition: { neq: [b2.items.len, 2] }
    then:
      - print: ["FAIL: expected items.len=2, got ${b2.items.len}"]
      - call: [sys.exit, 1]
- print: ["test_kv_get_mutate_set_same_key: 1 passed, 0 failed"]
```

Should be added to `run_all_tests.sh`. Until the runtime fix lands, this
test will document the failure mode and (after the fix) verify it.

---

## Files involved

- **Runtime:** [`src/sys/db_runtime.cpp::aiml_db_kv_set`](../../src/sys/db_runtime.cpp#L1450)
- **Runtime:** [`src/sys/db_runtime.cpp::aiml_db_kv_get`](../../src/sys/db_runtime.cpp#L1509)
- **Runtime:** [`src/sys/db_runtime.cpp::kv_decoded_evict`](../../src/sys/db_runtime.cpp#L1087)
- **Runtime:** [`src/sys/ser_runtime.cpp::enc / aiml_ser_encode / aiml_ser_free_decoded`](../../src/sys/ser_runtime.cpp)
- **Compiler:** [`src/core/ast_compiler_stmt.cpp` (decl with struct initialValue)](../../src/core/ast_compiler_stmt.cpp#L296)
- **Spec:** [`spec/31_db_integration.md`](../../spec/31_db_integration.md) §6.4-§6.7
- **Migration doc:** [`spec/migration/aiondb_kv_mmap_refactor.md`](../../spec/migration/aiondb_kv_mmap_refactor.md)
- **Discovered in:** `datacreatures/sources/server/server_main.aiml ::
  handle_create_creature` (workaround applied 2026-05-21)
