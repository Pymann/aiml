# WP-B: AIML heap ownership contract

As of: 2026-09-07. Status: design/analysis (no compiler code). Implementation of
[work package B](memory_lifetime_fix_plan_2026-09-07.md#L68) of the memory lifetime plan.

Basis: the empirical baselines in
[findings_2026-09-07.md](../../../9rooms_v2/doc/analysis/memory_checks/findings_2026-09-07.md)
(leak probes A–G, render probes A/A0/B/BL/C/CF) and the current source state of
`ser_runtime`, `list_impl`, `string_runtime` and the deep copy sites in codegen.

Purpose: BEFORE WP-C emits automatic drops, fix ONE shared, decidable ownership
semantics for compiler AND runtime. The contract is the
precondition for safe cleanup without double free/use-after-free.

## 1. Ownership taxonomy

Every value (local, temporary, field, container element, return) carries exactly one
of these classes at compile time; where it is not statically decidable,
a runtime flag or a compiler diagnostic is needed (no silent default free).

| Class | Meaning | Drop at scope end? |
|---|---|---|
| **Static** | Points to immutable program data (string literal in `.rodata`, constant descriptors). | **Never free.** |
| **Borrowed** | Borrowed view of foreign ownership (`kv.get` cache view, `list.get`/`at` element alias, field reference, by-ref parameter). Owner and invalidation lie elsewhere. | **Never free** (the owner cleans up). |
| **Owned** | This slot owns the heap alone (fresh model instance, deep copy target, interpolation/concat result, decode result into its own buffer). | **Yes**, exactly once, in reverse init order. |
| **Shared** | Several owners, refcount-managed (managed `aiml_str_*`; future `chan<T>` handoffs). | **Release** (refcount−−), not `free`. |

Data flow states orthogonal to the class (plan §8.3): `initialized`,
`maybe-initialized` (conditional init → runtime flag), `moved` (ownership transferred →
no longer drop). A plain C++ `bool` at compile time is NOT enough for conditional
init/move.

## 2. Contract per origin

Columns: **Actual** = current runtime/codegen behaviour (evidenced); **Target** =
target contract; **Risk** = what a naive cleanup would break here.

| Origin / operation | Class | Actual (source state) | Target (WP-C) | Risk |
|---|---|---|---|---|
| String literal | Static | no header, `.rodata` address | never drop; never read a header in front of it (string_runtime.cpp:81 reads the magic IN FRONT OF an arbitrary address) | free/decref of a literal → crash/UB |
| Interpolation `"${…}"` | Owned | fresh heap string, **is NOT dropped** (probe F: ~144 B/iter) | drop at the end of the full expression, except on transfer (store/return/push) | double drop when transferred as a return at the same time |
| Concat accumulator (ast_compiler_expr.cpp:43–68) | Owned | new accumulator per segment, the replaced one is not released | release the replaced accumulator, pass on the final result as owned | do not free borrowed segments/literals |
| String-returning runtime (`strdup`/`aiml_str_dup`) | Owned or Shared | mixed: `aiml_prefab_text_value` = `strdup` (prefab_runtime.cpp:6021), others refcounted | mark per producer as owned OR shared; align consumers with it | `free` on refcounted / decref on `strdup` |
| Fresh model instance (`decl: [m, T]`) | Owned | heap fields **not dropped** (probe B: ~80 B/iter) | type-specific field drop at scope end | mixed fields (literal/borrowed) in the model → no blanket free |
| Model deep copy (`decl: [m, T, kv.get_copy]`, ast_compiler_stmt.cpp:586–607) | Owned | `aiml_ser_deep_copy` into a zero-init dst; **not dropped** (probe A: ~800 B/iter) | mark dst as owned + register cleanup; do NOT free the KV cache slot along with it | cache slot aliasing → UAF in the cache |
| `kv.get` (cache view) | Borrowed | pointer into `decoded_cache` (LRU 1024, evict deep-frees) | never drop; invalidation = cache eviction, document it | drop of a cache view → double free on eviction |
| `kv.get_copy` (return) | Owned | decoded_cache slot, but the compiler deep-copies into an independent local | local = Owned; the cache slot stays cache-managed | see above, cache aliasing |
| `kv.get_copy_at(key, dst)` | Owned (in a foreign buffer) | free-then-decode into the caller's buffer; **NEW**: `-1` on an invalid blob (dst nulled) | dst = Owned; drop the last content at scope end; only read on result `1` | forbid a dst alias onto the cache (codegen check) |
| `at`/`get` (element, DECIDED) | Borrowed | today: shallow copy into a value local, heap **shared** with the container (the broken middle) | **`at`/`get` → `ptr<T>` borrowed view** into the container slot (no copy, never dropped), analogous to `kv.get`. **New `at_copy` → `T` owned deep copy** (droppable), analogous to `kv.get_copy`. | the view is invalidated by `push`/`remove`/`clear`/grow (realloc) — do not hold it across a mutation |
| `push`/`insert` (list_impl.cpp:56, DECIDED) | Transfer or copy | no element lifetime descriptor; today `push` of a ptr into a value list = compile error ("Zwingende Böe") | **`push` → MOVE**: take over the struct slot shallowly (transfer the heap pointers, no deep copy) **and neutralise the source as `moved`**. **New `push_from` → deep copy** into the container (the source stays valid). | `push` WITHOUT neutralising the source → two owners → exactly the "Zwingende Böe" crash |
| `clear` (list_impl.cpp:200) | — | sets **only size=0** (elements leak; probes D/G: ~48 B/iter = buffer) | drop owned elements first; the capacity may stay | do not drop borrowed elements |
| `remove` (list_impl.cpp:287) | — | slot shift without an element drop | drop the removed owned element; shift slots as a transfer; neutralise the last slot | do not additionally release shifted elements |
| Container drop (scope end) | Owned | the buffer stays (probe D: ~48 B/iter) | drop the elements first, then the buffer | do not destroy borrowed parameter containers |
| Assignment/overwrite | Replace | RHS evaluation, the old target stays | evaluate the RHS safely first, drop the old target, take over the new one (plan §8.4 sketch) | self-assignment/source points into old fields → do not free the target first |
| by-ref parameter (`ptr<T>`, `is_byref_arg`) | Borrowed | the callee does not own | the callee never drops; the caller keeps ownership | drop in the callee → UAF at the caller |
| Return / permanent storage | Transfer | return deep copy present (ast_compiler_stmt.cpp:1219–1236) | secure the result before cleanup, transfer it to the caller, exempt it from the callee's cleanup | the callee drops transferred ownership again |
| `set_text` (prefab_runtime.cpp:3662) | Borrowed input | `std::string` assignment copies synchronously; the input stays the caller's business (BL probe = 0 → set_text does NOT leak) | call contract "borrowed input, copied synchronously"; the caller drops an owned input temporary afterwards | `free(text)` in set_text (the input may be a literal) |
| Pointer/interface/class instance | Borrowed (as a rule) | representation ≠ ownership | do not drop as an owning model merely because of the ptr representation; detect escaping | class instance/interface dropped as a model → UAF |
| Explicit `free` | manual drop | present | mark the slot as `moved` → no second auto-drop | double free auto+manual |

## 3. Decisions (gates for WP-C)

1. **No blanket `aiml_ser_free_decoded` on arbitrary models.** A model can
   mix literal, borrowed and owned fields and can contain later replaced/shallow copies.
   An owned mark on the whole model alone is not enough → a uniform
   **field contract** or per-field metadata. `aiml_ser_free_decoded`/`fre`
   (ser_runtime.cpp:471) only for compatibly owned decode fields; after a list free
   also null size/capacity.
2. **Cache aliasing stays taboo.** Neither `kv.get` views nor the cache slot handed back by
   `get_copy` may be dropped by scope cleanup. Only the independent local copy produced by the
   compiler is Owned.
3. **Verb split for containers (DECIDED 2026-09-08), mirrors `kv.get`/`get_copy`:**
   - `at`/`get` → **borrowed `ptr<T>` view** into the container slot (no copy, never
     dropped). `at_copy` → **owned `T` deep copy**. Today's broken shallow
     value copy (shared heap) thereby disappears completely.
   - `push` → **MOVE, UNCONDITIONALLY** (one meaning, NO type-dependent fork);
     `push_from` → **deep copy** (the source stays valid).
   - **`push` = move out of owned:** `push(x)` transfers `x`'s heap pointers into the slot
     and **nulls `x`** (heap fields = 0). The container is then the only owner
     (the same technique as `get_copy_at` `-1`: target nulled, safely readable).
   - **Safety reach of the nulling (precise, corrected):** the nulling protects
     AGAINST a double free via the **source slot `x`** — a pointer to `x` itself then
     reads an empty object (defined). It does **NOT** automatically make already
     escaped aliases onto `x`'s **heap fields** safe (e.g. a `ptr` to `x.name` taken before the move):
     that heap now belongs to the container, the alias keeps pointing validly at it —
     if the container is destroyed later, the alias becomes a
     **UAF**. I.e. aliases onto moved heap fields follow the lifetime of the
     **new owner** (the same invalidation rule as `at` views). So "no UAF" applies
     to the source slot, not necessarily to pre-existing field aliases.
   - No double owner via the source slot → no revival of the earlier
     list insertion crash ([[aiml_list_push_ptr_value_check]]; the cause back then was
     pointer vs. model size on insertion, NOT a demonstrated double free — the
     move contract is the clean semantics here, not the "only possible" rescue).
   - **Precondition (a fixed rule, no behavioural fork):** `push` requires an
     **owned** source. A move out of a borrowed `at`/`get` view = **compile error** →
     use `push_from`. Additionally a lexical **use-after-move diagnostic** on `x`
     (a logic aid, not mandatory for memory safety thanks to the nulling).
   - **View invalidation** is a property of `at` (borrowed), NOT of `push`:
     `push`/`remove`/`clear`/grow reallocate the buffer → open `at` views become invalid
     (like `kv.get`/C++ iterators) — documented, diagnosed where lexically visible.
   - A borrowed reference in a container (the pointee outlives the container) stays possible
     only via an explicit `list<ptr<T>>`.
4. **Replace before free on overwrite** (plan §8.4): evaluate the RHS into a temp first,
   then drop the target, then move — safe for self-assignment and source-points-into-target.
   `aiml_ser_deep_copy` returns `void` → a **checked replace variant** with an
   error contract is needed before occupied targets are overwritten.
5. **Do not use string refcounting more widely** until managed origin is
   secured in codegen. `aiml_str_incref/decref` (string_runtime.cpp:74/81) read the
   magic header IN FRONT OF the address passed; the size check proves no valid
   memory in front of it. For mixed runtime boundaries provide a safe registry/handle contract,
   and secure the registry for concurrency.

## 4. Semantics: what changes vs. what stays

**Deliberately changed semantics (released by the user 2026-09-08, §3.3):** `at`/`get`
become borrowed views instead of shallow value copies; `push` becomes a move (the source is consumed);
`at_copy` and `push_from` are new. That changes observable behaviour and is therefore
NO LONGER a "semantics only when necessary" case, but a deliberate decision. **Migration
required:** existing 9rooms_v2 code that (a) uses `at`/`get` as an independent copy
or (b) keeps using `x` after `push(x)` must be switched to `at_copy`/`push_from`.
WP-C brings diagnostics for this (use-after-move; move out of borrowed = error), so that
affected places become visible at build time instead of breaking silently. An audit of these
patterns (plan proposal) precedes activation.

**What stays unchanged:**
- Assignment/parameter passing (by-ref lists/maps, by-value model/class args) — cleanup
  is an additive release, NOT a reinterpretation as deep copy assignments.
- KV decoded_cache behaviour (LRU 1024, evict deep free) unchanged.
- `get_copy_at` buffer reuse (spec/31 §6.7.2) unchanged; only the
  error return value `-1` is new.
- Only if a semantics/ABI change is demonstrably unavoidable is the
  concrete case clarified with the user BEFORE implementation (plan §7.4).

## 5. Acceptance of the contract (plan §3)

Covered are locals, temporaries, parameters, fields, containers, returns and
runtime boundaries. Unresolved cases are NOT freed automatically in an unsafe way,
but get a targeted compiler diagnostic. WP-C activation follows the
order: container element rules → replace/return transfer → model field drops.
Every step is checked against the isolated probes AND ASan/LSan (mandatory).
