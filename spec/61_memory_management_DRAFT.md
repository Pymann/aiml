# 61 — Memory Management [DRAFT — replaces the deprecated `[deprecated]_lifetime_ownership.md`]

> Status: DRAFT for review. Replaces the now-deprecated
> `[deprecated]_lifetime_ownership.md` (the older move/borrow proposal); this draft claims
> spec number 61 once approved. Where this draft and the implementation disagree, **this
> spec, once approved, wins** (Arbeitsprinzip 2).

## 0. Terminology & abbreviations

Every abbreviation used in this document, spelled out once here:

- **rc** — *reference count*: an integer stored with a heap resource, counting how many
  live handles point at it.
- **refcount / refcounted** — managed by a reference count: the resource is freed exactly
  when its rc reaches 0.
- **UAF** — *use-after-free*: reading or writing memory that has already been freed.
- **UB** — *undefined behaviour*: the language gives no guarantee; may crash, corrupt, or
  appear to work. AIML never *silently* relies on UB — it diagnoses the visible cases.
- **handle** — a small value (a target-pointer-sized slot: 8 bytes on 64-bit, 4 on wasm32) that refers to a heap resource;
  copying a handle does **not** copy the resource.
- **ptr\<T\>** — *pointer to a T*: a **unique owning** handle to a heap `T`. There is at most
  **one** `ptr` owner; assigning/passing/pushing it is a **move** (the source becomes
  moved-out, not a second alias), so it is **never** duplicated. Release follows it (frees the
  box exactly once). To share ownership use `shared<T>`; to reference without owning use
  `ref<T>`. Written literally as `ptr<T>`.
- **ref\<T\>** — *reference to a T*: a **non-owning** handle — a **borrow** when passed as a
  parameter, a **weak** reference when stored in a field. Never increfs, never freed through;
  release ignores it. The edge used to break reference cycles (§5.2). Written `ref<T>`.
- **shared\<T\>** — a **co-owned, refcounted** owning handle to a heap `T` (`[rc|fields]`).
  Copying increfs; freed at rc 0. The opt-in alternative to unique `ptr<T>` (§6.2). Written
  `shared<T>`.
- **adopt (`{ share: p }`)** — the one-way, explicit conversion of a unique `ptr<T>` into a
  `shared<T>`: reallocate with an rc header, move the fields, free the old block, leave `p`
  moved-out (§6.2). There is **no** reverse (`shared`→`ptr` is forbidden).
- **co-owner** — one of several `shared<T>` handles that jointly keep a referent alive.
- **move-send** — transferring a value to another thread by **moving** it through a `chan`/spawn
  (Model P, §5.3); the sender is left moved-out. Requires the value to be **exclusively owned**.
- **exclusive (ownership)** — a value whose entire transitively-reachable owned subgraph is
  reached by **no** other handle (owning or weak), from any thread. The precondition for
  move-send; checked statically (affine analysis) or by `send_unique` at run time.
- **`send_unique`** — the runtime-checked move-send escape: asserts every node's **strong rc == 1**
  at run time and moves, else traps. Borrow-freedom (no incoming/outgoing `ref`, no cross-thread
  reach) is **not** runtime-checked — weak refs aren't counted — so it stays a **static** guarantee
  for both send paths; immortal literals are exempt from the rc check (§5.3).
- **guarded `shared<T>`** — a cross-thread `shared<T>` bundled with a mandatory lock; its payload
  is reachable only inside the lock guard and no rc-bearing value escapes it (§5.3).
- **incref / decref** — increment / decrement a refcounted resource's rc (on share / on drop).
- **alias** — two or more handles referring to the **same** resource; a mutation through one
  is visible through the others.
- **borrow** — a non-owning use of a resource that must not outlive, free, or store it.
- **weak (reference)** — a stored non-owning reference that does **not** keep its referent
  alive; may become dangling if the referent is freed. Spelled `ref<T>`.
- **shallow copy** — duplicate the container/spine but **share** (incref) its referents.
- **decl** — a variable declaration statement (`- decl: [name, type, init?]`).
- **scope** — the innermost block (`body`, `while`/`if` body, function body) a local lives
  in; it ends at the block's closing.
- **scope exit** — the point where a scope ends and its stack locals are released.
- **owner** — the single place responsible for releasing a resource (a stack frame, a `new`
  box, or the rc-machinery for a refcounted resource).
- **move** — transfer ownership from source to target without copying the resource; the
  source becomes dead (must not be used again).
- **deep copy** — duplicate a resource and everything it owns, so the copy is fully
  independent (mutating one does not affect the other).
- **COW** — *copy-on-write*: share a resource until one holder mutates it, then copy lazily.

## 1. Goal & philosophy

AIML memory management is **explicit and under full user control, modelled on C++, with a
small number of deliberate simplifications**. The programmer decides what lives on the
stack and what lives on the heap, and releases heap objects explicitly.

The design rests on three ideas:

1. **Stack by default.** A plain `decl` creates a value in the current stack frame. It is
   released automatically at scope exit. No heap, no rc.
2. **Explicit heap via `new`, automatic release by default.** `{ new: T }` allocates a `T` on
   the heap and yields a unique `ptr<T>`. The programmer *chooses* the heap explicitly; release
   is **automatic at scope exit** (`scope`-default, §9.1) — this mirrors C++ `unique_ptr` /
   `make_unique`, not raw `new`/`delete`. `free` is available for **early** release, and
   `persistent` opts out when ownership escapes the scope.
3. **The simplification — refcounted managed resources.** Strings (§4) and containers
   (`list`/`map`/`buffer`, §5) are **reference-counted handles**: copying one **shares** the
   resource (cheap, incref) and it is freed exactly when the last handle goes away — never a
   deep copy on assignment (unlike Python), never a manual `free` (unlike C++ `new[]`).
   Independence is requested explicitly (`copy`/`clone`, §5.1). Strings add immutability, so
   their sharing is invisible; containers are mutable, so sharing means **aliasing** —
   deliberate reference semantics, with `ref<T>` weak back-edges to break cycles (§5.2).

Automatic release at scope exit is the **default** for a `new` box (`scope`, §9.1) — you write
no `free`. Qualifiers (§9) then adjust that default where needed: **`persistent`** opts **out**
of the scope drop (ownership escapes); **`producer`/`consumed`** affirm ownership transfer at
call boundaries; an explicit **`free`** releases early. So the drop is automatic (opt-out), not
opt-in; the explicit model — `new`, `ptr`/`shared`/`ref`, early `free`, move — is always
available underneath.

## 2. Storage classes

There are **two orthogonal questions** about any datum, and confusing them is the source of
most memory bugs. Keep them separate:

- **(A) What memory *discipline* does its TYPE follow?** — copy vs share vs move, and how it
  is torn down. This is a fixed property of the type (`string` always shares; `thread` never
  copies). §2.1 gives the six classes (plus a non-owning category) and a rule that places every built-in — present or
  future.
- **(B) Where is a given *instance* allocated?** — stack, refcount-managed heap, or an
  explicit `new` box. This is chosen per declaration. §2.2 gives the three allocation forms.

`abbr:` *rc* = reference count (§0); *POD* = "plain old data", a value that is just bytes with
no heap or resource behind it; *RAII* = "resource acquisition is initialisation", the rule
that a resource is released deterministically when its owning scope ends.

### 2.1 The six type classes (memory discipline of a built-in type)

Every built-in type — and every future one — belongs to exactly one class (plus a **non-owning
/ borrowed-view** category for handles the user never owns). Two axes define the classes: **how a
value is duplicated** (value-copy / share via rc / move-only) and **how it is torn down**
(nothing / free a heap buffer / close an external resource / runtime-owned).

| # | Class | Duplicate | Teardown | Mutable? |
|---|---|---|---|---|
| **1** | **Value (POD)** | value-copy (bit/field copy) | none | yes (the copy) |
| **2** | **Immutable shared handle** | **share (incref)** | free buffer at rc → 0 | **no** |
| **3** | **Mutable buffer / container** | **share (incref)** — aliasing (§5) | free block at rc → 0 | yes |
| **4** | **Unique resource** | **move-only** (copy forbidden) | close/join **once** at owner drop | n/a |
| **5** | **Shared resource** | **share (incref)**, **atomic** rc | close when **last** ref drops | n/a |
| **6** | **Runtime-owned singleton** | share the handle (runtime owns) | the **runtime's** concern; user never frees | n/a |
| **—** | **non-owning / borrowed view** | pointer-copied (no ownership) | never freed through the handle | n/a |

Classes 2, 3 and 5 all use rc; the difference is teardown — Class 2 frees an immutable byte
buffer, Class 3 frees a mutable buffer after releasing its elements, Class 5 must run an
**external** close (an OS handle, a peer). Class 4 and Class 5 both hold external resources;
the difference is ownership — Class 4 has **one** owner (move to transfer), Class 5 has
**several** co-owners (rc). This is why a `chan` (two endpoints) is Class 5, not Class 4: you
cannot move it to the receiver and still hold it at the sender. (Class 2 vs 3 differ only in
mutability, which is why sharing is invisible for Class 2 and visible — aliasing — for Class 3.)

**Placement rule for any new built-in** (answer top-down):

0. Is it a **runtime-owned singleton** — the runtime returns the *same* handle for the same
   identity and manages its lifetime (e.g. `db.open_*`, Spec 47 §1)? → **Class 6** (user handle
   is non-owning; the user never frees it).
1. Does it own an **external resource** that must be released exactly once (OS handle, GPU
   object, thread, lock, connection)?
   - Held by **one** owner at a time → **Class 4** (move-only, RAII).
   - Inherently **co-owned** by several holders → **Class 5** (rc + external close).
2. Else, is it **immutable**? → heap-backed **Class 2** (rc handle); small/plain **Class 1**.
3. Else (a mutable heap buffer) → **Class 3** (container discipline, §5). A **fixed-size,
   inline** aggregate (`array<T:N>`) has **Class-1 storage** (inline value block), but its
   copy/move/drop are **recursive on `T`** (§7 composition rule) — not unconditionally POD.

**Representation vs. class (verified from `context.h::getLLVMType`).** The *representation* of
every built-in is already fixed in the compiler and splits cleanly:

- **By-value** (an LLVM struct or scalar copied field-by-field): scalars; `vec2/3/4`,
  `ivec2/3`, `quat`, `mat4`; the event structs `mirror_event` `{ptr,ptr,i32}` and `rpc_event`
  `{ptr,i32,i32}`; `ui_style` (the 72-byte base config struct); `fn<…>` (a code pointer).
  (**Resolved:** `buffer` is **not** by-value — it is a **Class-3 refcounted handle** like
  `list`/`map` (§5); the earlier "header is a value, only its data buffer is Class 3" reading is
  superseded. The whole `{i32×4, i8* data}` header lives behind an rc handle, so assignment /
  by-value model copy **shares and aliases** the buffer.)
- **Handle** (an opaque `i8*`, or a pointer to a heap struct — **assignment copies the
  pointer, i.e. shares the referent**): everything else. This is why *"we always get handles /
  pointers"* is the correct recollection.

Representation does **not** by itself fix the *class* — a handle can be Class 2, 3, 4, 5, or 6;
they differ in **teardown**, which the current code leaves manual (explicit `.close()`/
`.free()`, no rc, no auto-scope-release for most). Assigning the class is exactly this spec's
job. The inventory below is by class:

| Class | Built-in types (representation in parens where notable) |
|---|---|
| **1 — Value (POD / value struct)** | `i8 i8_fast i8_least i16 i32 i64 u8 u16 u32 u64 f32 f64 bool`, enums; `vec2 vec3 vec4 ivec2 ivec3 quat mat4`; `mirror_event` `rpc_event` (value structs of borrowed fields); `ui_style` (base config struct); `JsonStyle` (config struct, `isBuiltin=false`); `fn<…>` (code pointer). **These are trivially copyable, no teardown.** |
| **1′ — Inline value aggregate (element-recursive)** | **`array<T:N>`** — a fixed-size inline `[N × T]` **value block** (Class-1 *storage*: inline, no own heap block, no own rc), but its **copy / move / drop are recursive on `T`** (the composition rule, §7), **not** unconditionally POD: `array<i32:N>` is trivial; `array<string:N>` incref's/decref's each element; **`array<tensor:N>`** (real, and `tensor` is a Class-4 `ptr`-handle) is **move-only** — shallow-copy forbidden, drop frees each `tensor`, deep-`clone` via per-element `aiml_tensor_clone`; `array<ptr<T>:N>` likewise move-only (may be unimplemented today). Same footing as a value model with those field types. |
| **2 — Immutable handle (rc-eligible)** | `string` (`char*`), `path` (interned immutable `char*` — **confirmed**, not Class 1) |
| **3 — Mutable container (rc, Option B)** | `list<T>` `map<K:V>` `buffer` (rc handle → heap `{size,cap,…,data}` block) **`cascade<X>`** (reuses the `list<i32>` layout — a cascade *is* a `list<i32>` row, so same rc/aliasing discipline) |
| **4 — Unique resource (move-only, close once)** | **OS/IO:** `file_handle` `process` `thread_handle` `socket_handle` `tls_socket_handle` `ws_handle` `url` `url_credential` `http_server_handle` `http_route_handle` `http_request_handle` `http_response_handle` `http_client_handle` `http_session_handle` `fs_options`. **GPU/render:** `gl_buffer` `gl_vao` `gl_shader` `gl_texture` `gl_framebuffer` `gl_renderbuffer` `gl_mesh` `window_handle`. **Assets:** `gltf_scene/node/mesh/primitive/material/animation/skin/texture/image` `audio_sound` `audio_group` `audio_handle` (legacy V1 asset-registry sentinel) `tensor` (explicit `tensor.free()`; single-owner via `owns_data`, slices are non-owning views) `connector` `ui_font` `ui_slider_style ui_button_style ui_label_style ui_progress_style ui_panel_style` (heap, **mutable**, currently †leaked). **Iteration:** `iterator<T>`. **Session:** `mirror_session` (explicit `session.close()`). `Code` (JIT code object). **Raw buffer:** **`dyn_array<T>`** (a single-`malloc`, runtime-sized, **unmanaged** heap scalar buffer — a unique owner, **not** rc'd/aliased; its whole point is *one* allocation and no per-op rc; freed once at owner drop, with `.free` as an optional early release; §5.6, spec/53). |
| **5 — Shared resource (rc + external close; **atomic** rc)** | `chan<T>` (**registered today** as `i8*`, not merely future); `lock` (mutex — structurally co-owned by the threads it guards; currently †leaked, no `pthread_mutex_destroy`). Cross-thread by nature → their rc is **atomic** (§5.3), internal state runtime-synchronized. |
| **6 — Runtime-owned singleton (non-owning user handle)** | **DB:** `Store<T>` `Kv<T>` `TimeSeries<T>` `VecStore` — `db.open_*` returns the **same handle** for the same name+path (Spec 47 §1, Category 1); the **runtime owns and mutex-protects** them. The user's handle is **non-owning**: never `free`/`close`d by user code (that would break other holders → premature close / double free). Lifetime is the runtime's. |
| **special / non-owning (borrowed view)** | `any` (`void*`, type-erased borrow); `input_event` (pointer into a **static global** event buffer, overwritten each poll); `mirror_peer` (owned by its `mirror_session`; user code refers to it by `peer_id`); `prefab_instance` (**engine-owned, single classification: non-owning user handle** — like a
Class-6 runtime-owned resource. The **engine** owns the node's lifetime (`engine.instantiate`
does `g_scene->instances.push_back`, returns a non-owning `i8*`); the user's handle **never**
owns it — scope-drop is a **no-op**, it is never `free`d by scope, and it is **not** a Class-4
unique owner. `engine.destroy(inst)` is an **engine scene operation** (remove the node from the
scene), the one way to end its life early — **not** user ownership; after it every handle
dangles, so guard with `engine.is_alive(inst)`. `get_hovered`/`get_dragged`/`hit_test` are
borrows into the same scene); a `tensor` **slice/reshape** (`owns_data=0`, aliases the parent buffer) — none of these are freed through the handle |

*† = the type has **no teardown wired in the current runtime** and leaks for the process
lifetime (verified for `lock`, `ui_font`, `ui_*_style`; the `mirror_session` struct itself is
also suspect). The class states the discipline this spec **mandates**; the missing frees are a
migration/impl task (§12), not a change to the class.*

**Completeness.** This inventory now covers **every** type produced by
`context.h::getLLVMType` (enumerated exhaustively — all `typeName ==` and `rfind("…<")`
cases). User-defined types — `model`, `struct`, `class`, `classgroup`, `interface`, `enum` —
are **not** built-ins and follow the model rules (§3 value, §6/§6.2 heap `ptr`/`shared`, §7
copy), not this table. `void` is not a value. If a new built-in is added, place it with the
rule above and add a row here.

**Cross-cutting fact (implemented under memV3).** The rc discipline this spec assigns to
**Class 2/3/5** is the **default memory model** (`AIML_MEM_V3`, now the compiler default). Under it:
- **`string` — share-by-incref.** `set: [a, b]` / `decl: [a, string, b]` **incref** the shared
  buffer (the `AimlStrHeader{magic, refcount}` in `string_runtime.cpp`), not `aiml_str_dup` a
  fresh copy; a string literal / the immortal `""` (rc `0xFFFFFFFF`) has no header and its drop is
  a no-op (§4.6). This is §4's share-by-incref, implemented.
- **`list`/`map`/`buffer` (Class 3) — refcounted handles** (§5 Option B): assignment increfs the
  handle (aliasing), scope-exit decrefs, freed at rc 0. Container **fields** and nested container
  **elements/values** are handles too (§5.7).
- **user `model` with heap fields** — member-wise share per §7 (§2.1 rows 1′/handle).

(The legacy pre-memV3 path — deep-copy strings, inline value containers — is no longer maintained;
`AIML_MEM_V3=0` still selects it for A/B comparison only.)

### 2.2 The three allocation forms (where an instance lives)

Orthogonal to the type class: the same type can be placed in different memory.

| Form | Created by | Lives on | Released by |
|---|---|---|---|
| **Stack value** | `decl: [x, T]` (no `new`) — scalar or value `model`/`struct` | the stack frame | automatically at scope exit |
| **Refcount-managed** | a Class 2/3/5 value (`string`, `list`/`map`/`buffer`, `chan`, …) | the heap, rc-managed | automatically when rc → 0 |
| **Explicit heap box** | `decl: [p, ptr<T>, { new: T }]` | the heap | `free: [p]`, or a qualifier (§9) |

These compose: a stack value or a `new` box may *contain* refcounted resources (e.g. a model
with a `string` field). Releasing the container releases its fields (§7). A Class-4 resource is
**always** a single-owner handle regardless of form — a stack `thread_handle` is still move-only
and still joins at scope exit; it is never bit-copied even though it sits on the stack.

## 3. Scalars and value models on the stack

A `decl` without `new` is a **stack value**.

- **Scalars** — `i32`, `i64`, `f32`, `f64`, `bool`, enums — are plain values.
- **Value models / structs** — a `model` (or `class` used by value) declared without `new`
  is an inline struct in the stack frame.

```yaml
- decl: [count, i32, 3]          # a scalar on the stack
- decl: [conf, ServerConfig]     # a value model, inline on the stack (NOT heap)
- set: [conf.port, 8080]
# `count` and `conf` are released at scope exit; no `free` is written or needed.
```

**Pass-by-value.** A value-typed function parameter receives a **member-wise copy** of the
argument on the callee's stack frame. Mutating a **scalar** field of the parameter does not
affect the caller (each has its own copy). **But this is not blanket isolation:** a value
model's **string/container/`shared` fields are shared (incref), not deep-copied (§7)**, so
mutating a **container field** through the parameter **is visible to the caller** (they alias
one container, §5). A pass-by-value copy isolates the *scalar/inline* part and **shares** the
rc-managed part. For an independent copy of the whole thing use `clone`; for full aliasing pass
a handle (`ptr<T>`, §6).

```yaml
- def:
    name: bump
    args: [[cfg, ServerConfig]]   # by value: `cfg` is the caller's value, copied
    body:
      - set: [cfg.port, 9090]     # mutates the local copy only
- def:
    name: main
    body:
      - decl: [conf, ServerConfig]
      - set: [conf.port, 8080]
      - call: [bump, conf]
      # conf.port is still 8080 here — bump got a copy.
```

To let a callee **mutate** the caller's object **without taking ownership**, pass **`ref<T>`**
(a borrow, §6.2/§8) — *not* `ptr<T>` (which would **move** ownership to the callee) and not a
value (which copies). `ref<T>` is the right tool for in-place mutation.

## 4. Strings — refcounted immutable handles

A `string` value is a **target-pointer-sized handle** (8 bytes on 64-bit, 4 on wasm32) to a reference-counted, immutable byte buffer on
the heap.

Rules:

1. Creating a fresh string (interpolation, `sys.*` result, `str.*` builder) allocates a
   buffer with **rc = 1**.
2. **Assigning / copying a string shares it (incref), never deep-copies.**
   `set: [a, b]` makes `a` refer to `b`'s buffer and increments its rc. Both are valid.
3. A handle is dropped — at scope exit, on rebind (`set: [a, other]` drops the old `a`),
   or on container removal. **If the handle points at an rc-managed buffer**, its rc is
   decremented. **If it points at a static literal or the immortal `""` (no rc header), the
   drop is a no-op** — nothing is decremented or freed (see rule 6).
4. When an rc-managed buffer's rc reaches **0**, it is freed. That is the *only* way a string
   buffer is freed; a string is never passed to `free`. **Static literals are never freed at
   all** (they are not heap-allocated).
5. Strings are **immutable**: any "modification" (concatenation, replace) produces a **new**
   buffer (rc = 1); it never mutates a shared buffer, so sharing is always safe.
6. **String literals** (`"..."`) live in **static storage, carry no rc header, and are never
   released** — incref/decref on them are no-ops (verified: `aiml_str_incref/decref` no-op on a
   non-`STRN` pointer). Copying a literal into a managed slot is safe; the immortal empty string
   `""` (sentinel rc) is the safe default for unset string fields and is likewise never freed.
   So a slot may hold *either* an rc buffer or a literal, and the drop in rule 3 does the right
   thing for both — no contradiction.

```yaml
- decl: [name, string, "Ada"]    # literal
- decl: [alias, string, name]    # SHARE: alias and name point at the same buffer (incref)
- set: [greeting, "Hi ${name}"]  # NEW buffer (rc=1); does not touch name's buffer
# at scope exit: greeting drops (rc→0, its heap buffer freed). name/alias hold the literal
#   "Ada" (static, no rc) → their drops are no-ops; "Ada" is never freed (§4.6).
```

*Rationale:* strings are copied constantly (keys, labels, ids); deep-copying each assignment
(the C++ `std::string` cost) or each read (Python) is wasteful, and immutability makes sharing
observationally identical to copying. Strings are **one** of the rc-by-default classes — the
others are **containers** (Class 3, §5), the **shared resources** (Class 5), and opt-in
`shared<T>`; strings are just the one where immutability makes the sharing *invisible* (§2.1).

## 5. Containers — `list<T>`, `map<K:V>`, `buffer`

**Decision (was §11.1): containers are refcounted shared handles (reference types).** A
`list`/`map`/`buffer` value is a **target-pointer-sized handle** (8 bytes on 64-bit, 4 on wasm32) to a heap block `{rc, count, capacity,
data→buffer}`. This is the Class-3 discipline of §2.1.

> **Implementation status (gate `AIML_MEM_V3`, branch `mem-v3-compiler`).** Option B is
> implemented for **`list<T>`, `map<K:V>`, `buffer` and `cascade<X>`** — as value **locals**
> *and* as model **fields** (a field is a pointer-to-handle, so a by-value model copy
> shares/aliases the container). Runtime: `aiml_{list,map,buffer}_handle_{new,incref,release,
> copy}`; the block is `[rc][pad][struct]` with the stored pointer at the struct so all
> `ListImpl`/`MapImpl`/`BufferImpl` ops are unchanged. Serializer nodes `AIML_TD_LIST_PTR`/
> `AIML_TD_MAP_PTR`/`AIML_TD_BUFFER_PTR` carry the field indirection (emitted only under the
> gate). Acceptance probes: `tests/lifetime/03,17,18,19` (+ 01 for field aliasing). The
> OFF/deployed path keeps the inline value struct (byte-identical).
>
> **`buffer` resolved:** the whole `buffer` header is an rc handle (share/alias like `list`);
> the §2.1 "only the data buffer is Class-3" reading is superseded (see the §2.1 note).
> All three §5-rule-1 verbs are covered under memV3 — **assign** (locals), **store** (fields)
> and **pass** (function args, probe 20: the handle flows through the existing container by-ref
> param path, so a callee's mutation aliases the caller's container). **`dyn_array<T>` is Class 4,
> not Class 3** (§5.6): a unique single-`malloc` buffer, freed once at owner drop with `.free` as
> an optional early release (probe 21). Acceptance probes 03,17,18,19,20 (Class-3) + 21 (Class-4
> dyn_array). The OFF/deployed path keeps the inline value struct (byte-identical).
>
> **Nested containers (§5.7):** a container used as an **element or value** of another container
> (a container **key** is out of scope, §5.7) is the same handle (a pointer-sized slot), not an
> inlined `{size,cap,data}` struct — required by
> the §6.1 Class-3 "release each element by its own class (decref)" rule. `AIML_TD_LIST_PTR`/
> `_MAP_PTR`/`_BUFFER_PTR` descriptors are emitted for nested element slots exactly as for fields.

Rules:

1. **Assigning / passing / storing a container shares it (incref), never copies.**
   `decl: [b, list<i32>, a]` makes `b` the **same** list as `a` and increments its rc. Both
   handles see every mutation.
2. A handle is dropped at scope exit, on rebind, or on container removal → the block's rc is
   **decremented**; at **rc 0** the buffer's elements are released (per their class) and the
   block is freed. There is no `free` on a bare container (that is a compile error, §10).
3. **Mutation is visible through every handle (aliasing).** This is the deliberate reference
   semantics — the same object reached by two names *is* one object.

```yaml
- decl: [a, list<i32>]
- call: [a.push, 1]
- decl: [b, list<i32>, a]        # SHARE (incref) — b and a are the SAME list
- call: [a.push, 2]
- print: ["b.len = ${b.len}"]    # → 2  (aliasing: a.push is visible through b)
# scope exit: b drops (rc 2→1), a drops (rc 1→0 → buffer freed once)
```

### 5.1 Getting an independent copy — `copy` (shallow) vs `clone` (deep)

Aliasing is the default; **independence is requested explicitly**. Two verbs, two depths:

- **`{ copy: a }` — shallow.** Allocates a **new** container block, then copies each element
  slot **per its class**: scalars value-copied, `string` elements **incref'd**, `shared<T>`
  elements **incref'd** (co-owned), `ref<T>` elements **weak-copied** (no incref). The *spine*
  is independent (pushing to the copy does not affect the original); the *referents* are shared.
  *(New verb — to be added; see §12.)*
  - **Forbidden for move-only elements.** If the element type is **move-only** — `ptr<T>`
    (unique owner), a Class-4 resource held by value, or a move-only model (one with a `ptr`/
    resource field, §7) — shallow `copy` is a **compile error** (§10). Pointer-copying a unique
    owner would give two containers that each free the same object → double free. Duplicating
    such a container requires **`clone`** (deep, which allocates a *fresh* owned box per element)
    or an explicit element-by-element move.
- **`{ clone: a }` / `sys.clone` — deep.** Produces a fully **independent** copy of the owned
  subgraph. A Class-4 resource element is clonable **only if it has an element clone** (e.g.
  `tensor` via `aiml_tensor_clone` → `list<tensor>`/`array<tensor:N>` **is** clonable); a raw OS
  handle (fd, thread, socket, GL object) is **not** copyable **or** clonable. **What that forbids
  depends on the container kind:**
  - a **Class-3 rc container** (`list<file_handle>`, `map<…:socket_handle>`, …) is **still a
    shareable rc handle** — assignment/pass **shares the container** (incref, aliasing, §5),
    **without** duplicating its elements; only **independent `copy` and `clone` are forbidden**
    (they would need to duplicate the un-duplicable handles). It is not "move-only" — it is
    *share-only, not copy/clone-able*.
  - an **inline `array<resource:N>`** (a Class-1 value block, §2.1 row 1′) has no rc handle to
    share, so it genuinely **is move-only** (copying the inline resources is impossible).

  The exact contract for object graphs is below (it is **not** a naive recursive copy).

```yaml
- decl: [c, list<i32>, { copy: a }]        # ok: scalars value-copied — independent spine
- decl: [d, list<ptr<Node>>, { copy: n }]  # COMPILE ERROR: ptr elements are move-only
- decl: [d, list<ptr<Node>>, { clone: n }] # ok: deep graph-clone (see contract below)
```

**Deep-clone contract (object graphs).** `clone` walks the **owned** subgraph and must be
**memoized and cycle-safe** — a naive recursive copy is wrong (it multiplies shared nodes and
loops forever on cycles). Precisely:

1. **Owned edges are followed:** `ptr<T>`, `shared<T>`, container elements, `string`, and
   nested value-model fields are cloned recursively. `ref<T>` (weak) and Class-6/resource-by-
   `ref` edges are **not** followed (they are not owned).
2. **Memo map (original → clone): each reached object is cloned exactly once.** If two owned
   edges reach the same object (only possible via `shared<T>` co-ownership, or a value model
   reached twice), the clone contains **one** copy that both cloned edges point at — the
   DAG shape is preserved, sub-objects are **not** duplicated per edge.
3. **Cycles terminate and are reproduced.** When traversal reaches an object already in the memo
   map (including mid-cycle), it links to that object's clone instead of recursing — no infinite
   recursion, no truncation. (Only owned edges are followed; `ref` edges are rewired per rule 4.)
   **`clone` always yields an OWNING result — a value or an owning handle (`ptr`/`shared`),
   never a bare `ref`.** (A bare `ref` result would leave the fresh allocation ownerless — leaked
   or the borrow invalid. A weak back-edge *inside* the cloned graph is a graph edge, not the
   return type; it does **not** turn the result into a `ref`.) With that fixed:
   - A weak-`ref` back-edge or a **`shared` cycle** clones cleanly — the returned owning handle
     to the clone root is a **value/`ptr`** (the root is entered from outside) or a **`shared`**
     (a legal additional co-owner; rc counts it, rule 5).
   - **⚠ Restriction — a unique-`ptr` ownership cycle cannot be cloned into a returned `ptr`.**
     The owning root must have **no internal owning `ptr` edge** pointing at it (entered only
     from outside the subgraph — as a tree/DAG root is). In a pure-`ptr` cycle **every** node is
     already owned by an internal `ptr` edge, so a returned owning `ptr` to any node would make
     it a **second** unique owner — ill-formed. Such a clone is **rejected** (these graphs are
     themselves leaks, §5.2). To clone a cyclic *owned* structure, model the cycle with `shared`
     (multiple owners legal) or break it with a `ref` back-edge first. **Valid `clone` roots**
     are therefore: a node with no internal owning-`ptr` edge (→ value/`ptr` result), or any
     node of a `shared` graph (→ `shared` result). The result is **never** a bare `ref`.
4. **Weak `ref<T>` back-references are rewired, not followed:**
   - **internal** — if the referent is *within* the cloned subgraph (in the memo map), the
     clone's `ref` points at the **clone** of that referent (so a self-contained graph's
     back-edges stay internal to the copy);
   - **external** — if the referent is *outside* the cloned subgraph (never reached), the
     clone's `ref` keeps pointing at the **same external original** (still weak, no incref).
5. **Refcounts of the clone reflect its OWN owning edges — not blanket 1.** Each cloned object's
   rc is set to **the number of cloned owning references that point at it**, computed during the
   memoized walk. A `shared` sub-object co-owned by *k* owning edges within the subgraph is
   cloned **once** and its clone's rc is **k** (not 1); an object with a single owner gets rc 1.
   (Setting every clone to rc 1 would under-count co-owned nodes and free them too early.)
6. **Independence is of OWNED mutable state — external weak refs are preserved by design.** The
   clone shares **no owned mutable state** with the original: every owned string/container/box is
   fresh. It may still **observe** external objects through **external weak `ref`s** (rule 4) —
   those are non-owning observations, not shared owned state, so preserving them is **not** a
   contradiction with independence. ("Fully independent" means: nothing the clone *owns* is
   shared with the original; it does not mean the clone is severed from every external object it
   weakly references.) This is the deep (transitive) independence §9.2 refers to.

**Clone type mapping — the result is the OWNING counterpart of the source, never the source
type verbatim.** `clone` does **not** require "same argument and result type" (that older rule
collides with "clone never yields a bare `ref`"). Instead it maps the source to an owned result:

| Source of `{ clone: x }` | Result type |
|---|---|
| a value / value model / `array<T:N>` | an independent **value** of that type |
| a **`ptr<T>`** (or a `ptr` element) | a fresh **`ptr<T>`** (new owned box) |
| a **`shared<T>`** | a fresh **`shared<T>`** (rc per rule 5) |
| a **`ref<T>`** — e.g. the result of `at`/`get` (§5.4) | an **owned** result whose kind is the **target type** (below): a **value** (`T`), a **`ptr<T>`**, or a **`shared<T>`**. **Never** a `ref<T>`. |
| a container (`list<E>`/`map`/…) | an independent container (deep, elements per their class) |

So `clone(ref<Model>)` (cloning a borrowed element) yields an independent owned `Model` — as a
value, `ptr<Model>`, or `shared<Model>` depending on the target type (below) — resolving the
§5.4 question. Cloning is **forbidden** where an owned member is non-clonable (§7): e.g. a borrow
of a `file_handle`.

**How the target type is determined (context type, else default).** The clone result type is
fixed **statically** in this order:
1. **A declared target slot** — `decl: [c, shared<Node>, {clone: r}]` uses `shared<Node>`; a
   `set`/`push` into a typed slot uses that slot's type.
2. **The expected context type** when there is no declaration slot — a `clone` used **as a
   function argument** takes the **parameter's** declared type; **as a `return` value** it takes
   the function's **return type**; as a container element, the element type. This is ordinary
   context-type inference.
3. **Default when the context imposes no owning type** (e.g. an expression statement, or a
   context that accepts "any owning form"): the default is determined **solely by the static
   source type**, with no overlap:
   - source **value / value model / struct / `array<T:N>`** → a **value** of that type;
   - source **`ptr<T>`** → **`ptr<T>`**;
   - source **`shared<T>`** → **`shared<T>`**;
   - source **`ref<T>`** → a **value `T`** (the value form — *always*, regardless of whether `T`
     is heap-capable; to obtain `ptr<T>`/`shared<T>` from a borrow, name the target type via a
     declaration or context, cases 1–2);
   - source **container** → that container type.

   The default is **never** a bare `ref`. Because each rule keys on the *source's own static
   type* (`value` / `ptr<>` / `shared<>` / `ref<>` / container) and those are disjoint, the
   default is unambiguous — a `ref<Model>` defaults to a `Model` **value** (not `ptr<Model>`);
   there is no "ref-to-heap-object" special case.

Whatever the resolved target type, the graph-ownership runtime check below still applies (it can
reject a value/`ptr` target that turns out to need `shared`).

**The result type is STATIC (the declared target); the graph-ownership contract (§5.1 point 3)
is enforced by a DEFINED RUNTIME CHECK — not by a data-dependent result type.** Whether the
cloned subgraph has an owning back-edge to its root is a **runtime** property (the same
`shared<Node>` field may be null in one instance and a self-reference in another), so it
**cannot** decide the result type. Therefore:

- **Result type = the resolved target type** (declared slot → context type → default, per the
  rule just above), fixed at compile time by the type mapping. It does not depend on the runtime
  graph.
- **Runtime check in the memoized walk:** if the walk finds an **owning edge that points back at
  the root**, it validates against the declared target and traps with a **defined error**
  otherwise:
  - internal **`shared`** back-edge + target `shared<T>` → **ok** (root co-owned, rc = k, rule 5);
  - internal **`shared`** back-edge + target value/`ptr<T>` → **runtime error** "clone: graph is
    internally co-owned at the root; target must be `shared<T>`";
  - internal **unique-`ptr`** cycle through the root → **runtime error** (a second unique owner
    is ill-formed, §5.2 — such a graph is itself a leak);
  - **no** owning back-edge to the root (trees, lists, DAGs — the common case) → the declared
    value/`ptr`/`shared` target is produced normally.

*Why not a conservative static rule?* Deciding the result type from the **type structure**
alone (e.g. "if `T` can self-reference, force `shared`") is **rejected as too pessimistic**: a
plain linked list `Node { next: ptr<Node> }` is type-recursive yet runtime-acyclic and must
clone into a `ptr<Node>` — a static "self-referential type ⇒ shared" rule would wrongly force
`shared` on every list/tree. The declared-target + runtime-check design keeps result types
static and predictable while catching the genuinely ill-formed graphs at run time.

⚠ **Impl note:** the current `sys.clone` → `aiml_ser_deep_copy` (Spec 45) is **serialization-
based and depth-truncating** — it does **not** satisfy this contract (a finite linked list or
any cyclic/DAG graph is cut off or mis-copied). The deep-clone primitive must be reimplemented
as the memoized graph walk above; coordinated with **Spec 45** and the copy/clone verb spec
**Spec 29.3** (which must adopt this type mapping in place of the old same-type rule) — §12.

### 5.2 Cycles and weak back-references

Because containers (and any refcounted reference) keep their referent alive, a **reference
cycle** never reaches rc 0 and **leaks** — e.g. a `Zone` whose `list<Card>` owns cards, each
`Card` holding a reference back to its `Zone`. Pure rc cannot break this (a garbage collector
is a non-goal, §13).

**The owning direction is declared, never inferred** (the system cannot know which edge is the
"owner" — §0). The programmer designates it through the reference type (full detail in §6.2):

| Type | Owns? | On copy | Keeps referent alive? |
|---|---|---|---|
| **container / `string`** (Class 2/3) | yes (rc) | **incref** (share) | yes, until rc 0 |
| **`ptr<T>`** (unique) | yes (single) | **move** (source dies) | yes, until its one owner drops |
| **`shared<T>`** (opt-in, rc) | yes (co-own) | **incref** (share) | yes, until rc 0 |
| **`ref<T>`** | **no** — borrow / weak back-ref | pointer copied, **no incref** | **no** |

So a back-edge is spelled `ref<T>`, and *that* is how one knows it is a back-reference:

```yaml
- model: { name: Zone, fields: [[cards, list<Card>]] }   # owning: Zone keeps its cards alive
- model: { name: Card, fields: [[owner, ref<Zone>]] }    # WEAK back-ref: does NOT keep Zone alive
# zone drops → rc 0 → releases its cards; card.owner never held zone, so no leak.
```

Rule: **ownership edges are containers / `ptr` / `shared`; every back-reference is `ref<T>`
(weak, no incref).** A cycle can form through **any** owning edge — refcounted (containers,
`shared`) **or unique `ptr`**. Unique ownership does **not** prevent cycles: an owner can be
**moved into its own owned subgraph** (via a deref of itself, as above), leaving every object
with exactly one owner yet the graph looping back on itself. Such a `ptr` cycle becomes
**orphaned and unfreeable** — the external handle is moved-out, nothing reaches it, and no rc
exists to even detect it (arguably worse than an rc cycle, which at least tracks a count). A
**mixed** cycle (some `ptr`, some `shared`/container edges) leaks the same way. Whatever the
edge kinds, an all-owning cycle leaks; breaking it needs **one `ref<T>` (weak)** edge; if the
wrong edge is weak, the referent dies early (dangling). The static **owning-cycle lint** (§10)
flags type graphs whose **owning** edges — `ptr`, `shared`, and container elements alike — form
a cycle with no `ref<T>` to break it ("at least one edge must be `ref<T>`"); it cannot pick
*which* edge (semantic), only that one is required.

> This makes `ptr<T>` = **owning** and `ref<T>` = **non-owning/weak** load-bearing across this
> spec, and requires a coordinated update to **spec/18** (which today forbids storing a
> `ref<T>` in a field) so that a stored `ref<T>` is defined as a weak reference (§12).

### 5.3 Refcounting across threads

AIML has native OS threads (Spec 26/47), so the refcount on a shared resource can be touched
by two threads at once. An `rc++`/`rc--` that is not atomic **races** → a lost decrement leaks,
a lost increment frees a live object (UAF). The current runtime's rc is **non-atomic**
(verified: no atomics in the string/container/model runtime).

**The refcount is separate from the payload — this is the crux.** incref/decref happen
**implicitly** on assignment, storage, parameter passing, and scope-drop; they are **not**
under any lock the author writes. Because those implicit ops (especially later drops) escape
a *bare* user lock, a lock "around the data" is **insufficient for rc-safety** — rc-safety is a
property of the *type/mechanism*, not of ad-hoc user locking. The resolution (below) is
**Model P**: cross-thread you either **move** an exclusive value (no sharing, so no rc race), or
use a **guarded `shared<T>`** whose lock guard brackets *every* rc op (incref **and** drop),
which a bare lock cannot. Conversely, atomic rc protects only the **count**, never the payload —
so guarded `shared` still needs its lock for field access. Count-safety and payload-safety are
independent obligations.

The per-class contract:

- **Class 2/3 (`string`, `list`, `map`, `buffer`) — non-atomic rc, moved across
  threads (Model P, message passing).** They cross a thread boundary only by **move** — a
  `chan.send(v)` or spawn argument **consumes** `v` (move-out; nothing remains in the sender). The
  transfer is safe iff the **entire transitively-reachable owned subgraph is EXCLUSIVE** to `v`
  (no owning *or* weak handle from any thread reaches any node) — **rc == 1 on the outer object
  is not sufficient** (a contained `list`/`string` may be aliased in the sender). **This is not a
  clone** and costs **zero copies** for the common "build a message locally, then send it"
  pattern — the value already unique is moved as-is. `clone` is needed **only** when the sender
  wants to **keep** a copy of shared data (clone the part it sends, keep its own).
  - **Exclusivity is established statically, with a runtime escape (RESOLVED, §11.18):**
    - **Static uniqueness (default, zero-cost):** an **affine analysis** proves the sent subgraph
      is unaliased — every rc member was **moved in fresh** (a `new`, a literal, another move),
      never a *share* of an existing handle, and neither `v` nor any node was aliased (no
      surviving incref, stored field, or outstanding `ref`). The build-locally-then-send pattern
      passes; a `send` of a value the analysis cannot prove unique is a **compile error** with the
      fix suggested (make members fresh, or use the escape / `clone`).
    - **`send_unique(v)` (runtime escape):** for a value the author knows is exclusive but the
      static analysis cannot prove, `send_unique` performs a **runtime check that every node's
      strong rc == 1** and moves it, else **traps**. A small runtime cost, no clone.
      **Scope of the runtime check (decided):** the rc==1 test covers **strong ownership only**. A
      **weak `ref<T>` is not counted** (§5.2), so RC **cannot** detect an *incoming* borrow into the
      sent subgraph at runtime — `send_unique` therefore does **not** waive the **weak-ref-closed**
      requirement below: borrow-freedom (no outstanding `ref` into or out of the subgraph) stays a
      **static** guarantee, enforced by the affine/uniqueness pass at the `send` site, for both the
      static and the `send_unique` paths. `send_unique` only escapes the *strong-uniqueness* proof,
      never the weak-ref-closure check. (There is no runtime borrow registry; adding one — to make
      incoming borrows runtime-detectable — is explicitly out of scope for this model.)
      **Exemption — immortal, immutable memory.** A **string literal** and the immortal `""` carry
      **no rc header** (§4.6, rc `0xFFFFFFFF`), and other static/immutable blobs likewise; they are
      **immutable and never freed**, so they cannot be aliased-for-mutation or double-freed. Such
      values are **exempt from both the static exclusivity proof and the `send_unique` rc==1 check**:
      the static analysis already treats "a literal" as moved-in-fresh (unique), and `send_unique`
      **skips the rc test for header-less immortal nodes** (there is no counter to read, and none is
      needed). A message object carrying a string literal must therefore pass **both** send paths.
  - **Weak-ref-closed is required (binding).** The sent subgraph must contain **no `ref<T>`
    pointing at an object left behind in the sender** — an outbound weak edge would let the
    receiver reach sender-thread state (concurrent access + lifetime hazard). Such an edge is a
    **compile error** at `send`; model durable cross-thread references by **id + a Category-1
    registry** (§5.2) instead. This is checked as part of the exclusivity/uniqueness pass.
- **`shared<T>` across threads — GUARDED (lock mandatory, statically enforced).** Cross-thread
  co-ownership is the opt-in escape from Model P, and it is made statically safe by **bundling
  the `shared<T>` with a lock** (≈ Rust `Arc<Mutex<T>>`). The top-level rc is atomic (keeps the
  object alive while co-owned), and the payload is protected by the required lock as follows:
  - **The payload is reachable ONLY inside a `lock` guard scope.** The compiler forbids any field
    access to a thread-shared guarded `shared<T>` outside the guard — you obtain the `T` only by
    acquiring the lock (a guard scope), so every read/mutate is serialized.
  - **Nothing rc-bearing may escape the guard.** A `string`/container/`shared` value read inside
    the guard is a **borrow that cannot outlive the guard scope** (a scoped `ref`, statically
    enforced). This is the key that makes it sound where a plain "lock the data" fails: because no
    rc handle leaves the guard, **both** the incref (inside) **and** its matching decref (also
    inside, at guard exit) are under the lock — the "implicit drop escapes the lock" hazard
    cannot arise. To *keep* a field's value beyond the guard, **`clone`** it inside the guard (the
    clone is independent and leaves as an owned value, not a shared rc handle).
    **The clone must be self-contained.** `clone` copies *owned* structure but **preserves weak
    `ref<T>` edges** (§5.1) — so cloning only *part* of the protected graph can carry a `ref` that
    still points at a **protected original**, re-opening unguarded access. Counter-example: the
    protected object owns `a` and `b`, and `a` holds a `ref` to `b`; cloning only `a` yields a clone
    whose `ref` still reaches the protected `b` outside the lock. A clone may leave the guard **only
    if none of its weak references reach any object that stays protected**. Otherwise it is a
    **compile error** — clone the **fully weak-closed subgraph** (so every referent is copied too),
    or null/re-anchor the escaping `ref` to a non-protected owner (id + Category-1 registry, §5.2).
  - **Entry contract — no pre-existing unguarded alias of the protected subgraph.** The guard only
    serialises access reached *through the guard*; it cannot retroactively serialise a handle that
    was already shared **before** the value became guarded. So at the transition to a guarded
    `shared<T>` (constructing/first-sharing it cross-thread), the compiler requires that **no
    unguarded external alias of the guarded subgraph survives**: every owning reference into the
    protected object (and its rc-bearing members — its containers/strings/`shared` fields) must be
    **either moved into the guard or gone**. Counter-example that must be rejected: a local
    `list<T>` is stored into a model field, then that model is made a cross-thread guarded
    `shared<T>` — the original local still reaches the same container handle **outside** the lock,
    so mutation races. Rule: **all permitted access to the protected subgraph lives in one lock
    domain**; a leftover unguarded alias (local, other field, outstanding `ref`) is a **compile
    error** at the guard-transition, diagnosed by the same affine/uniqueness pass as `send` (§5.3
    weak-ref-closure). Bring data in by **move** (or `clone` a private copy) so nothing outside the
    guard aliases the protected nodes.
  - **The entry check also governs every later write INTO the protected subgraph.** The same rule
    is not one-shot at construction — it applies to **every `store`/`insert`/`set(i)`/field-swap**
    that puts a value into a protected node from *inside* the guard. An ordinary share-assignment of
    an **external** handle into a guarded field would leave the external variable holding an
    **unguarded alias** after the guard exits (counter-example: a correctly guarded model is given
    an outside `list<T>` by a plain share inside the guard; the outside local still reaches that
    handle unlocked). So a value written into the protected subgraph must be **moved in** (source
    emptied) or **cloned** (independent copy); a value **already in the same lock domain** (read
    from this or another node of the same guard) may be shared. Writing an external, still-aliased
    handle in is a **compile error**, checked identically to the entry contract above.
  - So a guarded `shared<T>` is fully static-safe: rc lifetime via the atomic count, payload via
    the lock guard, no rc op straddling the lock, **and no pre-existing alias bypassing it**.
    Non-guarded thread-shared mutation of a non-atomic field remains a compile error (you must go
    through the guard).
- **Class 5 (`chan<T>`, `lock`) — ATOMIC rc.** These are **shared across threads by their very
  nature** (a channel joins a sender thread and a receiver thread; a mutex guards several
  threads), so they **cannot** be single-thread-moved like Class 2/3 — their reference count
  **must be atomic**. Their *internal* state is runtime-synchronized (the channel locks its own
  queue; the lock *is* a mutex). Values sent **through** a `chan` are **moved** (§ above), not
  aliased. This closes the gap that §2.1 made `chan`/`lock` rc-resources without a sync rule.
- **Class 6 (`db`), `log`, `path`-intern** are runtime-managed and internally mutex-protected
  (Spec 47 §1, Category 1) — safe to touch from any thread; their handle rc, where any, is the
  runtime's concern.

So the safe cross-thread paths are exactly (**Model P — message passing primary**): **move an
exclusive value** (Class 2/3, statically checked, `send_unique` escape), a **guarded
`shared<T>`** (atomic rc + mandatory lock guard, nothing rc escapes it), a **Class-5 primitive**
(`chan`/`lock`, atomic), or a **Category-1 runtime primitive** (`db`/`log`/`path`) — plus
**id + Category-1 registry** for durable cross-thread references (§5.2). A non-atomic rc value
concurrently shared *outside* a guard is always a compile error. This is a coordinated addition
to **Spec 47** (§12).

### 5.4 Element access, extraction, replacement, invalidation

A container of **move-only** elements (`list<ptr<T>>`) cannot return a second owner from a read,
so access and ownership-transfer are **distinct verbs**. The rule per verb, by element class:

| Verb | `list<ptr<T>>` (unique) | `list<shared<T>>` | `list<string>` | `list<T>` value / `list<Model>` |
|---|---|---|---|---|
| **`at` / `get`(i)** — read | returns a **`ref<T>` borrow** (the list keeps ownership; never moves out) | `ref<T>` borrow | `string` (shared handle, incref) | **scalar → by value**; **value-model → `ref` borrow** into the slot |
| **`get_copy`(i)** — independent read | forbidden (would duplicate a unique owner — use `clone`) | `shared<T>` (incref, co-own) | `string` (incref) | member-wise **copy** — **forbidden if the model is move-only** (ptr/resource field); use `clone` |
| **`pop` / `take`(i)** — extract | **moves** the owned `ptr<T>` **out** and returns it (caller now owns; slot removed) | returns the `shared<T>` (the list's ref transfers out) | returns the `string` | returns the value |
| **`remove` / `delete`(i)** — drop | **releases** the element (frees the `ptr` box) and removes the slot; returns nothing | decref | decref | drop (release members) |
| **`set`(i, v)** — replace (§5.5 order: **secure new first, then release old**) | move `v` in (`v` moved-out), then **free** the old box | incref/move new, then decref old | incref new, then decref old | copy/move new, then drop old |
| **`push`/`insert`(v)** | **move** `v` in (`v` moved-out) | incref | incref | copy/move in |

So: **`at`/`get` = borrow** (ownership stays in the list), **`pop`/`take` = move out** (the only
way to get an owning `ptr<T>` back), **`remove` = release**. There is no verb that both keeps
the element in the list *and* hands a second owner out — that would break unique ownership.

**"`at`/`get` = borrow" is shorthand for the pointer/model cases only — read the table per class:**

| Element class | What `at`/`get` yields | Ownership |
|---|---|---|
| **scalar** (`i32`, `f32`, …) | the number **by value** (a copy) | no borrow, no handle — a value |
| **`string`** | the **owning string handle** (incref) | co-owns the referent; balanced by its own drop |
| **container** (`list`/`map`/`buffer` element, §5.7) | the **owning container handle** (incref) | co-owns; aliases the inner container |
| **`ptr<T>` / `shared<T>` / value-`Model`** | a **`ref<T>` borrow** into the slot | the container keeps ownership; never moves out |

So the "borrow" reading is exact **only** for the `ptr`/`shared`/value-model cases; a **scalar** is a
value, and a **string** or **container** element is an **owning handle** (incref), not a borrow.
(`pop`/`take` still moves the element out of the slot; `remove` still decrefs.)

**Materialisation of a value-model read (the binding decides).** `at`/`get` on a `list<Model>`
element yields a **borrow-view** of the slot; what the binding does with it is fixed by the
**declared target type**, not by the accessor:

- **`decl: [x, Model, { at: … }]`** — bind to a **value `Model`** → **materialise an owning
  value-copy**: member-wise per §7 (each string/container/`shared` field **incref'd**, scalars
  copied). `x` is independent of later list growth/reallocation and drops its own references at
  scope exit. **Forbidden if `Model` is move-only** (a `ptr`/resource field, §7) — a copy would
  duplicate a unique owner; use `ref<Model>` (borrow) or `clone`.
- **`decl: [x, ref<Model>, { at: … }]`** — bind to **`ref<Model>`** → keep the **borrow** (no
  incref, not freed through); invalidated by list growth or an external drop (below). This is the
  only form allowed for a move-only model.

The `store: [ref<Model>, view]` out-param fill (§7) is the same materialise-by-share rule applied
through a borrow target: bit-copy + retain (incref fields).

**Reads by element kind (the two special cases).**
- **Scalar element** (`list<i32>`, `array<f32:N>`, …): `at`/`get` returns the scalar **by
  value** (a copy of the number), not a `ref` — a scalar is a value (§2.1 Class 1). `set`(i, v)
  overwrites it by value.
- **`get_copy` is forbidden for a move-only element** — not only `ptr<T>`, but also a
  **move-only value model** (`list<Model>` where the model has a `ptr`/resource field, §7): it
  cannot be copied. Use `clone` for an independent element, or `at` for a borrow.

**Borrow returns are allowed for container elements** — reconciling with §8's "never return a
`ref<T>`". That ban is about returning a borrow to a **callee local** (which dies). A `ref<T>`
from `at`/`get` borrows the **element's referent**, which lives as long as the **container**
does; returning it is fine **iff** the container outlives the caller (it is the caller's own, a
parameter, or a longer-lived owner) and is not mutated in a way that invalidates it (below). A
`ref` into a container the callee itself owns and drops must **not** be returned.

**Reference (borrow) invalidation.** A `ref<T>` from `at`/`get` is invalidated by **either**
side — the container **or** an external owner of the referent:

- **Inline-element containers** (`list<T>` value, `list<Model>` by value, `array<T:N>`,
  `buffer`): elements live **inline in the buffer**, so **any reallocation on growth** (`push`
  past capacity, `insert`) **and any shift** (`remove`/`insert`) invalidates **every** outstanding
  element borrow — exactly like a C++ `vector` invalidating references on reallocation.
- **Pointer-element containers** — the pointer *slots* move on realloc but the **pointees are
  separate allocations that do not move**, so a borrow of a pointee **survives growth/realloc**.
  Whether `remove`/`set`(i)/`clear` invalidates a borrow depends on the **element kind** — the
  op removes the *slot* either way, but only a **release to rc 0** frees the pointee:
  - **`list<ptr<T>>`** (unique): `remove`/`set`/`clear` **frees** the box (sole owner) → borrow
    **dangles**. **`pop`/`take` does NOT** — it **moves** the pointee out; it stays **alive**
    (now caller-owned), so the borrow **remains valid**. (release vs move.)
  - **`list<shared<T>>`**: `remove`/`set`/`clear` **decrefs** → the pointee is freed **only if
    that was the last owner** (rc → 0); if another owner remains, the pointee lives and the
    borrow **stays valid**. So "remove ⇒ dangling" holds **only at the last reference**.
  - **`list<ref<T>>`** (weak): `remove`/`set`/`clear` drops a **non-owning** slot — the referent
    is **not** touched at all; a borrow of the referent **stays valid** (its lifetime was never
    the container's). It dangles only when the referent's real owner frees it (below).
- **External invalidation (either kind).** For a **weak-element** container (`list<ref<T>>`) or
  any borrow whose referent is **owned elsewhere**, that external owner can `free`/drop the
  referent **independently of the container** — the borrow then dangles even though no container
  op ran. A borrow is valid only while *both* the container path and the referent's owner keep
  it alive.

Using an invalidated borrow is a use-after-free / dangling error (§10), diagnosed where locally
visible. This corrects **Spec 10** (collections) **and Spec 52** (`array<T:N>`), which still
describe element access as a pointer-copy (§12).

**Bulk & derived operations — `extend`, slice (governs Spec 10).** These carry the **same per-class
ownership contract** as the single-element verbs above; Spec 10 lists them but leaves ownership to
this section:

- **`dst.extend(src)`** appends each element of `src` **by its element class** — exactly `push`
  applied element-wise: **scalar** copied by value; **`string`** incref'd (shared); **container**
  handle incref'd (shared, §5.7); **`shared<T>`** incref'd. **A move-only element type — `ptr<T>`,
  a by-value resource (`tensor`/`file_handle`/…), or a move-only value-model (§7) — cannot be
  `extend`ed** (it would give two owners of one unique thing): a **compile error**. `clone`-first
  does **not** fix it — the clone has the **same** `list<ptr<T>>` type, so `dst.extend(clone)` is
  still a duplicating extend and still rejected. The legal alternatives are to **move** the
  elements out of the (owned) source into `dst` **one by one** (`pop`+`push`, source emptied), or
  to **`clone` each selected element individually** and `push` the fresh copy. `src` is
  **unchanged** (read-only borrow); **self-extend** (`x.extend(x)`) is defined and realloc-safe
  (snapshot the length first). The element **type must match** (`list<U>` into `list<T>`, `U≠T`,
  is a compile error, Spec 10).
- **A slice `x[a:b]`** produces a **new, independent container** (a fresh handle, its own spine),
  **not** a view: its elements are filled **by element class** exactly as `extend` above (scalars
  copied; strings/containers/`shared` incref'd). **A move-only element type cannot be sliced** for
  the same reason (duplicating unique owners) — a **compile error**, and `clone`-first does not
  help (same type); build the sub-range explicitly by **moving** or **per-element `clone`+push**.
  Mutating the slice's spine (push/remove) does not affect the source; mutating a **shared element
  through the slice** is visible in the source (they co-own that element). A slice is thus a
  shallow **`copy`** of the selected range (§5.1), never a borrow of the source's buffer.

### 5.5 Assignment order — secure-before-release (self-assignment & aliased sources)

Any rebinding assignment to an rc slot — `set: [dst, src]` for a `string`/container/`shared`, a
`ptr` move, or a `set`(i) that replaces a container element (§5.4) — must **secure the source
before releasing the old destination**, in this exact order:

0. **Self-move guard.** If the source **slot** is the destination **slot** (e.g. `set: [a, a]`,
   or a `ptr` self-move), the assignment is a **no-op** and is **exempt** from the whole
   sequence below — nothing is captured, released, or stored. (Otherwise nulling the slot in
   step 1 would lose the only reference.)
1. **Secure the source, and for a MOVE put the source into its EMPTIED state — at runtime.** For
   an rc copy, incref the source. For a move, **capture** the source value and immediately record
   its slot as **emptied** so the later release (step 2, §6.1) skips it. "Emptied" is defined
   **by the moved thing's kind**, because not every movable has a single nullable pointer:
   - a **`ptr`/`shared`/handle slot** → write **`null`**;
   - a **Class-4 resource by value** → write its type's **invalid sentinel** (fd `-1`, a null
     handle, `tensor` null) so its close/teardown is a no-op;
   - a **move-only value model / struct** (no single pointer — its owning members are inline) →
     **recursively empty each owning member**, or set a per-object **moved-out drop-flag** the
     release protocol checks; scalar members need no change;
   - an **inline `array<T:N>`** whose element is move-only → empty **each element** the same way
     (per-element null/sentinel/recurse).
   This is a **runtime** emptied-state, not merely a compile-time "moved-out" mark — see the
   `p = p.child` case below. **Partial moves** (moving one member out of an aggregate while the
   rest stay) are covered: only the moved member's slot is emptied; the aggregate's later release
   skips exactly that member and still releases the others.
2. **Release the old destination** — decref / `free` / drop the value `dst` currently holds. Its
   recursive release (§6.1) treats the now-null source slot as empty and does **not** touch the
   moved-out value.
3. **Store** the secured source into `dst`.

The naive "release old, then take new" order is **wrong** and corrupts memory whenever the
source aliases or is reachable through the destination:

- **Self-assignment** (`set: [a, a]`) → handled by step 0 (no-op). A source drawn from the same
  container (`set: [lst[i], { at: [lst, i] }]`) that is *not* the identical slot is secured
  first (incref) so it is not freed by the old-destination release.
- **Source reachable through the target — `set: [p, p.child]`** (move a `ptr<Child>` field into
  `p`): step 1 captures `p.child` **and nulls the `p.child` slot**; step 2 then releases old `p`,
  whose recursive release visits `p.child`, finds **null**, and **skips** it — so the extracted
  child is **not** double-freed. A mere compile-time "variable moved-out" mark could not achieve
  this (the child sits in a *field slot* the release protocol traverses); the **runtime null of
  the slot** is what makes it correct. After: `p` points at the former child, the old parent is
  freed, the child survives.

This "incref-new-before-decref-old" ordering is a **normative correctness rule** for the
compiler's assignment lowering (not an optimization); a `ptr` move captures-then-invalidates in
the same order. It applies to every rc/owning slot: `string` (§4), containers (§5), `shared`
(§6.2), model/aggregate fields (§7), and container elements (§5.4).

### 5.6 `dyn_array<T>` — the unmanaged single-`malloc` buffer (Class 4)

`dyn_array<T>` is the deliberate **low-level** counterpart to the Class-3 containers: a
**single-`malloc`, runtime-sized, contiguous scalar buffer** (`i32`/`i64`/`f32`/`f64`/`u8`/`i8`/
`bool` only — see spec/53). Its whole reason to exist alongside `list<T>` is **one allocation and
zero per-operation refcounting**: no `{size,cap}` header, no rc, no growth/realloc, no aliasing —
just a typed heap block with `at`/`idx` (raw GEP, no bounds check). Use it for hot numeric /
FFI-shaped buffers whose exact size is known at allocation; use `list<T>` when you want growth,
a rich API, or shared/aliased ownership.

Because it is **not** rc'd, it is a **Class-4 unique resource** (§2.1), with the same lifetime
discipline as `ptr<T>` (§6, §9.1):

1. **Unique owner, move-on-assign.** Assigning / passing / returning a `dyn_array<T>` **moves**
   it (the source is moved-out); it is never silently shared. A shallow pointer-copy is forbidden
   (two owners would each free the one block → double free). To duplicate, **`clone`** (fresh
   `malloc` + byte-copy of the elements).
2. **Freed once at owner drop (scope default, §9.1).** You rarely write `free`: the buffer is
   released automatically when its owning slot leaves scope (unless it was moved out, or is
   qualified `persistent`, §9). This replaces spec/53's "manual `.free` or it leaks".
3. **`.free` is an optional *early* release.** `arr.free` frees the block now and **nulls the
   slot**, so the scope-default drop is a null no-op (no double free) — exactly the `ptr<T>`
   early-`free` rule (§6.1).

```yaml
- decl: [buf, "dyn_array<i32>", { new_array: [i32, n] }]  # one malloc, unique owner
- idx: [buf, 0, 42]
- decl: [x, i32, { at: [buf, 0] }]
# no `free` written → released automatically at scope exit (§9.1). `buf.free` = optional early.
```

### 5.7 Nested containers — a container as an element / value

A container used **as an element or value of another container** is the **same refcounted
handle** it is as a local, a field, or a return value — never an inline value block. So the
element/value slot of a `list<list<T>>`, `list<map<K:V>>`, `map<K:list<V>>`, `map<K:map<…>>`
(etc.) holds a **handle** (a target-pointer-sized slot: 8 bytes on 64-bit,
4 bytes on wasm32 — the element **stride is the target pointer size**, never a fixed 8 and never
the inner `{size,cap,data}` struct inlined). This is a direct consequence of §6.1's Class-3 rule:

> **Not covered here:** a **`buffer` with container content.** `buffer` (spec/11) is an **untyped
> byte buffer**: `buffer.write(v, list<…>)` *serialises* the value into bytes (a deep copy), it does
> **not** store a typed owning handle slot — so `buffer<list<…>>` is **not** a nested-handle
> container and this section does not apply to it. A `list<buffer>` / `map<K:buffer>` is fine: its
> element **is** a `buffer` handle (Class-3), released by decref like any other container element.
> A **container as a map _key_** is likewise out of scope: it needs a defined content-equality and
> hash (handle-identity keys break under `clone`; content keys break under mutation), which this
> memory model does not specify — map keys stay the already-defined key types (`string`, integer,
> enum). Only element and **value** positions get the nested-handle contract below.
the outer container "at rc 0 releases each element/key/value **by its own class**" — an inner
`list`/`map`/`buffer` element is released by **decref**, which only makes sense if the slot holds a
handle. There is no separate "inline nested container" representation; the OFF path is out of scope.

**Ownership per operation** (identical to the container's local/field/return contract — the element
slot is just another place an owning reference lives):

| Operation | Ownership behaviour |
|---|---|
| `push(inner)` / `insert` | store the handle. A **copy** source (aliasing) → **incref** (the container co-owns); a genuine **move** source → take over the source's owning reference (no incref, source moved-out). |
| `at(outer, i)` **or** `outer.get(i)` read as a `list<T>` **value** | load the handle and **secure an owning reference** for the result value (incref) — the result is an owning handle, aliasing the same inner container (mutation is visible in the outer, §5.4). The `at` and `.get` accessors are **identical** for a nested-container element: both load the handle at pointer stride and incref when bound; neither may fall back to the inline-struct getter (which would read the element at struct stride and return garbage). |
| `idx(outer, i, inner)` (replace) | **secure-before-release** (§5.5): incref/adopt the new handle, **decref the old** slot handle, then store the new handle. **No-op only when the source and destination are the *same slot*** (writing a slot back into itself). When they are **different slots that happen to hold the same handle value**: a **copy** source (it keeps its own reference) may be **optimised away** (the decref-old and incref-new cancel); a **move** source must still **clear the source slot** and **decref the surplus reference** so the count stays balanced. The provenance of the source (e.g. that it came from an earlier `at` on this same index) is **irrelevant** — only slot identity, and copy-vs-move, decide. |
| `pop(outer)` / `take` | **transfer** the slot's owning reference into the result: remove it from the container and hand back its existing owning handle. **No decref** of the returned element — ownership moves to the caller. |
| `remove(outer, i)` / replace-old / **outer drop** | **decref** the inner handle; its recursive drop (releasing the inner container's own elements + block) runs only at **its** rc 0. |

A **borrowed** (non-owning) view of an inner element — `ref<list<T>>` from an explicitly borrowing
access — is defined separately (it never increfs and is never freed through); it is not the
owning-value `at` above.

**The write-back pattern** `sez = at(outer, i); …mutate sez…; idx(outer, i, sez)` stays correct but
becomes **semantically redundant**: `at` already returns an owning handle aliasing the inner
container, so the mutations are already visible in `outer`; the trailing `idx` re-stores the same
handle (its old==new fast-path makes it a no-op). Code may keep it or drop it.

**`copy` vs `clone` (§5.1) compose transitively here.** A shallow `{ copy: outer }` duplicates the
outer spine but **shares** (increfs) each inner container handle — both outers see inner mutations.
A deep `{ clone: outer }` produces **independent** inner containers (each cloned once, internal
aliases preserved via the memoized graph walk, §5.1).

**One classification, everywhere.** Element stride, load/store, incref/decref and the serializer
type descriptor for a nested container element are all driven by the **same** type classification
(a container element → a `*_PTR` handle descriptor, e.g. `AIML_TD_LIST_PTR`). `push`, `at`, `idx`,
`pop`, `remove`, iteration, growth/realloc, `copy`/`clone` and serialization must all agree that the
slot is a pointer; mixing a new pointer stride with an old inlined-struct copy corrupts memory.

## 6. Explicit heap — `new`, `ptr<T>`, `free`

A heap object is created with `{ new: T }`. By default it is **`scope`-managed** (§9.1): the
runtime releases it automatically at scope exit — like C++ `unique_ptr` / `make_unique`, not
raw `new`/`delete`. You normally write **no** `free`.

```yaml
- decl: [conf, ptr<ServerConfig>, { new: ServerConfig }]  # heap-allocate; conf is a ptr<>
- set: [conf.host, "0.0.0.0"]   # dot-access auto-dereferences the ptr<> (writes *conf)
- decl: [h, string, conf.host]  # read *conf
# no `free` needed — conf is released automatically at scope exit (scope-default, §9.1)
```

`free: [conf]` may still be written for **early** release; it releases now and nulls the slot,
and the scope-default drop then sees an already-released slot and does nothing (no double free).

Rules:

1. `{ new: T }` allocates a `T` on the heap and returns a **`ptr<T>`** (a unique owning
   pointer, `scope`-managed by default). Its fields are default-initialised exactly like a
   stack `decl` (empty containers, `""` strings, nested defaults).
2. `free: [p]` runs the object's release: each refcounted field is decref'd — `string` (§4)
   **and** `list`/`map`/`buffer` (§5, a container is a refcounted handle) — each `ptr<T>`
   field is followed and freed (owning), each `ref<T>` field is ignored (weak), then the
   object itself is freed. See §6.1 for the exact protocol. `free` runs this at an **explicit
   early** point; scope exit runs the **same** release automatically otherwise.
3. **Automatic release is the default (`scope`, §9.1).** A `new` box is released at scope exit
   on every path, unless (a) it is **moved out** on a path (returned / stored / passed to a
   `ptr<T>` parameter — the drop is suppressed, the new owner is responsible), or (b) it is
   marked **`persistent`** (§9, ownership escapes untracked — then the programmer is responsible
   and the leak lint (§10) warns if it does not actually escape). A box that is neither moved
   nor `persistent` cannot leak.
4. `ptr<T>` is a **unique** owning pointer: assigning / passing-by-value / pushing it is a
   **move** — the bits are copied to the destination and the source becomes **moved-out**
   (later use is a use-after-move error, §10), so there is always exactly **one** owner.
   Dot-access (`p.field`) auto-dereferences. To share ownership use `shared<T>` (§6.2); to
   reference without owning use `ref<T>`.
5. **Errors:** double-`free`, `free` of a non-`new` value, and use-after-`free` are errors;
   the compiler diagnoses the cases visible within a block/function (§10). Cross-function
   aliasing is a documented contract, not a full checker (as in C++).

### 6.1 What `free: [p]` does — the release protocol

`free: [p]` (where `p : ptr<T>`) performs exactly these steps, in order:

1. **Null check.** If `p` is null, `free` is a **no-op** (as with C++ `delete nullptr`).
2. **Recursive member release** — run the *destructor* of `*p`, visiting each field of `T`
   in field order and releasing what that field **owns**. The action is fixed by the field's
   **type class** (§2.1); concrete types are only examples:
   - **Class 2 — immutable rc handle** (`string`, `path`) → **decref**; the buffer frees at
     rc 0.
   - **Class 3 — rc container** (`list<E>`, `map<K:V>`, `buffer`, `cascade<X>`)
     → **decref** the container handle; at rc 0 it releases each element/key/value **by its own
     class** (recursively) and frees its block; at rc > 0 nothing frees (another handle shares
     it).
   - **Class 5 — shared resource** (`chan<T>`, `lock`) → **atomic decref**; at the last drop the
     runtime runs its **external close** (destroy the channel/queue, `pthread_mutex_destroy`).
   - **`shared<U>`** field (opt-in co-owned model) → **atomic decref**; released only at rc 0.
   - **`ptr<U>`** field → **followed (owning, unique)** → run its recursive release (§6.1),
     freeing the inner box exactly once.
   - **Class 4 — unique resource by value** (`file_handle`, `thread_handle`, `socket_handle`,
     `tensor`, `gl_*`, …) → run its **close/teardown** exactly once (close fd, join/detach
     thread, `tensor.free`, delete GL object). (A model with such a field is move-only, §7.)
   - **nested value `model`/`struct`** field → recurse into **its** member release (inline; no
     separate block to free).
   - **`array<T:N>`** field (inline value block) → release **each element** by its class
     (recursively); the array storage itself is inline (no separate block).
   - **Class 1 — value/POD** (scalars, enums, `vec`/`mat`, …) → nothing to release.
   - **Non-owning — `ref<U>` (weak), Class-6 singleton (`db`), and any borrowed-view handle**
     (`input_event`, `mirror_peer`, a `tensor` slice, …) → **not followed / not released** (the
     `ref` is weak — the cycle-breaking edge, §5.2; the runtime owns the singleton/view).
3. **Free the object block** — release the single heap block that `{ new: T }` allocated for
   `*p`.
4. **Null the owner; weak refs dangle.** Because `ptr<T>` is **unique** (no owning aliases —
   §6.2), `p` is the object's only owner: after release the compiler **sets `p`'s slot to
   null**, so a later use is a null-deref caught at the use site and a later `free` is a
   no-op (this is the guaranteed-null the scope-default relies on, §9.1). Any outstanding
   **`ref<T>` (weak)** to the object is **not** tracked and is now **dangling** — using it is a
   borrow-contract violation (a `ref` must not outlive its owner, §5.2); the compiler diagnoses
   the locally-visible cases (§10). (There is no owning alias to dangle, only weak refs.)

**Symmetry with scope exit.** Releasing a **stack** value at scope exit runs the *same*
member release (step 2) on the inline value — but **without** step 3 (there is no separate
object block) and without a pointer. So the general rule is:

> **release(value)** = recursively release the value's owned members (step 2).
> **`free`(ptr\<T\> box)** = release(\*box) **then** free the box's own heap block.

Both reduce to one recursive walk; `free` differs only by also freeing the outer block.

```yaml
- model: { name: Row,   fields: [[label, string], [ids, list<i32>]] }
- model: { name: Table, fields: [[name, string],  [rows, list<Row>]] }

- decl: [tbl, ptr<Table>, { new: Table }]
# ... fill tbl.name and push several Row values into tbl.rows ...
- free: [tbl]
#   release(*tbl):
#     • decref tbl.name                         (string field → rc)
#     • decref tbl.rows                          (list<Model> handle → rc)
#         at rc 0: for each Row r: release(r) (decref r.label; decref r.ids), free rows block
#   free the Table heap block
#   → tbl's slot is set to null (unique owner, §6.1 step 4); a later use is a null-deref,
#     a later free a no-op. (Only an untracked weak `ref` to *tbl would dangle.)
```

Edge cases:
- `free` of a value that is **not** a `new`/`ptr<>` box (a stack value, a `string`, a bare
  `list`) → **compile error** (§10). Strings and containers are released by rc (§4/§5); stack
  values by scope exit; none of them by `free`.
- `free` in a loop of a box `new`'d in the **same iteration** → correct (allocate + free per
  iteration; no accumulation).
- `free` of a box whose **`ptr<>`** field owns another `new` box → the inner box **is** freed
  (owning field, followed in step 2). A **`ref<>`** field is *not* followed — its referent is
  owned elsewhere.

### 6.2 Owning representations — `ptr<T>` (unique), `shared<T>` (co-owned), `ref<T>` (borrow)

A `new` object is reached through one of three reference kinds. Ownership is a property of the
**type**, fixed at compile time; nothing distinguishes them at run time except the presence of
a refcount.

- **`ptr<T>` — unique owner.** Object layout `[ fields ]` (no refcount). Exactly **one** owner;
  assign/pass/push is a **move** (source dies). Freed by its owner (scope-default §9). The
  default and cheapest; use it for entities that live in one place and **move** between places
  (a card travels deck → hand → field). This is C++ `unique_ptr`.
- **`shared<T>` — co-owned (opt-in).** Object layout `[ rc | fields ]`. **Many** owners;
  assign/pass/push **increfs**; freed automatically at **rc 0**. Use it only where an object is
  genuinely co-owned with no single primary owner. Costs the refcount + incref/decref work,
  has non-deterministic teardown, and can form leaking cycles (break them with `ref<T>`, §5.2).
  This is C++ `shared_ptr`.
- **`ref<T>` — non-owning borrow / weak.** Just an address to the fields; never increfs, never
  frees. Obtainable from **both** `ptr` and `shared` (the compiler points it at the fields —
  for a `shared` it skips the refcount header). The universal "use, don't own" boundary; most
  parameters should be `ref<T>`. Must not outlive its owner.

**Conversion matrix — the only legal assignments across kinds** (identical at every boundary:
parameter, `list<…>`/`map<k:…>` element, model field):

| target ↓ / source → | `ptr<T>` | `shared<T>` | `ref<T>` |
|---|---|---|---|
| **`ptr<T>`** (unique) | move | **forbidden** | forbidden (a borrow does not own) |
| **`shared<T>`** (co-own) | explicit `{ share: p }` only | incref | forbidden |
| **`ref<T>`** (borrow) | ✓ (address of fields) | ✓ (skip rc header) | ✓ |

- **`shared<T>` → `ptr<T>` is always forbidden** — you cannot extract unique ownership from a
  shared object (other owners may exist), and the layouts differ (a `ptr` would `free` at the
  wrong address). No `rc==1` escape hatch.
- **`ptr<T>` → `shared<T>` via `{ share: p }`** (explicit adopt), which does exactly: (1)
  allocate a new `[ rc=1 | fields ]` block B; (2) **move** `p`'s fields into B (bit-copy the
  string/container **handles**, no incref/decref; copy scalars); (3) free `p`'s old block as
  **raw** memory (no member release — the members moved); (4) `p` is now **moved-out** (and any
  prior `ref` to it is invalid, per the ordinary borrow rule); (5) yield a `shared<T>` → B,
  rc = 1. A one-time reallocation, deterministic, exactly once.
- The **only implicit cross-kind conversion is to `ref<T>`** (borrow). Everything else is a
  compile error, except the explicit `{ share: p }`.

**Borrowing a plain value variable (`&`-of an lvalue).** The matrix above starts from `ptr`/
`shared`/`ref`, but a **value** location — a local `Card`, `list<i32>`, `array<f32:8>`, a struct
field, a container element — is none of those, yet it too can be borrowed as `ref<T>`. The rule:
- a `ref<T>` may be taken from **any live owning *lvalue*** — a value local/field/global, a
  container element (§5.4), or a `ptr`/`shared` deref. It is an **implicit address-of** at the
  binding site: **passing a value variable where a `ref<T>` parameter is expected auto-borrows
  it** (a named lvalue has stable storage), and a `decl: [r, ref<T>, x]` binds a borrow of `x`.
  An explicit borrow spelling `{ ref: x }` is available where the source must be named.
- **Borrowing a temporary *owner* is forbidden — but forwarding an *existing* borrow is not.**
  The distinction is whether the inline expression **produces an owning value** or **is already a
  `ref<T>`**:
  - **Creating a `ref` of a temporary owning value** — a call that returns an owned value/`ptr`/
    `shared` used inline, a freshly-constructed aggregate literal, an interpolation result — has
    **no stable storage** for its owner (the owner is dropped at the end of the enclosing
    expression), so taking a `ref` of it is a **compile error**. Bind it to a named `decl` first,
    then borrow that.
  - **Passing along an expression that is *already* a `ref<T>`** — the result of `at`/`get`
    (§5.4), or the return of a borrow-returning function (`return: {type: ref<T>, borrows: …}`,
    §8) — is a **borrow of pre-existing storage**, not of a temporary owner, and **is allowed**.
    It carries its own anchor and lifetime (from `at`/`get`'s container, or the callee's declared
    `borrows:` anchor), and remains subject to §5.4 invalidation. (An `at`/`get` result *is* an
    inline expression, but it borrows the container's element, not a temporary — so the "inline"
    shape alone does not forbid it; the ownership of what it refers to does.)
  This is what makes the "temporaries never anchor a borrow-return" rule of §8 enforceable at the
  source: you cannot take a `ref` of a temporary owner in the first place, while a `ref` that
  already refers to durable storage may be forwarded.
- the borrow is subject to the ordinary invalidation rules (§5.4) — it must not outlive the
  lvalue it borrows, and a mutation that reallocates/moves that lvalue invalidates it.

```yaml
- decl: [z, Zone]                         # a value local (owning lvalue)
- call: [scan, z]                         # scan(x: ref<Zone>) — z is auto-borrowed
- decl: [rz, ref<Zone>, { ref: z }]       # explicit borrow of z
# - call: [scan, { new_zone_temp }]       # ERROR: cannot borrow a temporary — name it first
- decl: [c, ptr<Card>, { new: Card }]     # unique
- decl: [s, shared<Card>, { share: c }]   # adopt: c moved-out; s owns [rc=1|Card]
- call: [pool.cards.push, s]              # incref → rc=2 (pool co-owns)
- call: [inspect, s]                      # inspect(x: ref<Card>) — borrow from shared, ok
# s drops (rc 1), pool drops (rc 0 → freed once). c is dead since the {share}.
```

## 7. Aggregates with heap-owning members — the composition rule

This rule governs **every value aggregate identically**: a value **model**, a **struct**, and an
inline **`array<T:N>`** (a homogeneous aggregate of N `T`s). Copy / move / drop of the aggregate
are the **composition of its members'** rules — nothing about the aggregate is "POD" unless
every member is. A model copy (pass-by-value §3, assignment) is **member-wise**, each field (or
array element) copied **per its class**:

- each `string` field → **incref** (shared, §4);
- each container field (`list`/`map`/`buffer`) → **incref** (shared, §5 — reference semantics;
  the two models now share one container, mutation is visible through both);
- each scalar/enum field → value-copied;
- each **`shared<T>`** field → **incref** (both copies co-own the referent, §6.2);
- each **`ref<T>`** field → pointer copied as a **weak** reference (no incref; does not keep
  the referent alive) — the cycle-breaking back-edge;
- a **`ptr<T>`** field makes the model **move-only.** A unique owner cannot be duplicated into
  two models, so a model that has any `ptr<T>` field **cannot be copied** — it can only be
  **moved** (source dies) or deep-`clone`d (which allocates a fresh box for the field). This
  mirrors a C++ struct with a `unique_ptr` member. Use a `shared<T>` field if the model must
  stay copyable while referencing a heap object;
- a **Class-4 resource field** (an owned `file_handle`, `thread_handle`, `socket_handle`,
  `tensor`, `gl_*`, … held **by value**, not through `ref`) is move-only in the same way: the
  aggregate becomes **move-only**, and releasing it **closes** the resource (§6.1). A member
  that merely *borrows* a resource holds it as `ref<T>` (weak) and stays copyable;
- a **nested aggregate** member (`array<U:N>`, a value model/struct) recurses into **this** same
  rule.

**Move-only propagation (the composition).** An aggregate is **copyable** iff **every** member
is copyable; it is **move-only** if **any** member is move-only (`ptr<T>`, a by-value Class-4
resource, or a move-only nested aggregate). Drop releases every member per its class,
recursively. This is one rule for models, structs, and `array<T:N>` alike — e.g. an
`array<tensor:N>` is move-only, drops each `tensor`, and a struct embedding it inherits that.

**Clonability is composed over ALL owned members — recursively, not just the move-only ones.**
An aggregate is deep-`clone`able **iff every owned member it would clone is recursively
clonable** — including members that are perfectly **copyable but not clonable**. The two axes are
independent:
- a `string` member is clonable (fresh buffer); a `tensor` via `aiml_tensor_clone`. A
  **`ptr<T>`** or **`shared<T>`** member is clonable **only if its referent is (recursively)
  clonable** — cloning it deep-copies the pointee, so a `ptr<Model>` whose `Model` holds a
  `file_handle` is **not** clonable. Not unconditional.
- a **`list<file_handle>`** member is **copyable** (the list handle is a shareable Class-3 rc
  container, §5.1) **but NOT clonable** — cloning it would have to duplicate the un-clonable
  `file_handle` elements. A model containing it is therefore **copyable (shares the list) but
  not clonable**;
- a by-value **non-clonable resource** member (raw fd/thread/socket/GL handle) makes the
  aggregate neither copyable **nor** clonable — only movable.

So: **copyable** iff every member is copyable (§ above); **clonable** iff every owned member is
recursively clonable — a stricter, separately-composed property. Copyable ≠ clonable.

No member is deep-copied on an ordinary copy: this is Option B end-to-end. For an **independent**
aggregate (fresh strings/containers/`ptr` boxes/clonable resources) use `clone` (deep, §5.1).

```yaml
- model: { name: Card, fields: [[title, string], [costs, list<i32>]] }
- decl: [a, Card]
- set: [a.title, "Bolt"]
- call: [a.costs.push, 3]
- decl: [b, Card, a]       # copy: b.title shares a.title (incref); b.costs SHARES a.costs (incref)
- call: [b.costs.push, 9]  # ALSO visible through a.costs (aliasing) → a.costs is now [3, 9]
- decl: [c, Card, { clone: a }]  # DEEP: c has its own title buffer and its own costs list
# scope exit: b, a drop → the shared costs list frees once at rc 0. title holds the literal
#   "Bolt" (static, no rc) → drops are no-ops; "Bolt" is never freed (§4.6).
```

## 8. Function parameters and returns

- **Value parameter** (`x: T` for scalar/**copyable** model): pass-by-value (§3) — a copy on the
  callee stack; string and container fields **incref'd** (shared, §4/§5), scalars value-copied.
- **Move-only model parameter** (a model/struct/`array` with a `ptr` or by-value resource field,
  §7): a value copy is **impossible**, so passing it by value is a **move** — the caller's value
  is **moved-out** (use-after-move afterward, §10) and the callee owns it. To *borrow* it
  without transfer, the parameter must be **`ref<T>`** (as with `ptr`, §8 below). A pass-by-value
  parameter of a move-only type is thus a sink; use `ref<T>` for a read/mutate borrow.
- **Container parameter** (`x: list<T>` / `map` / `buffer`): the handle is passed and
  **incref'd** — the callee shares the caller's container; mutations are visible (this is the
  reference semantics of §5, and resolves the old by-value/by-reference question §11.2).
- **`ptr<T>` parameter = MOVE (the one rule).** Passing a `ptr<T>` by value **transfers
  ownership to the callee** — the caller's box is **moved-out** (use-after-move afterward, §10),
  exactly as §6.2 says of every `ptr` handoff. The callee now owns it (releases it by its own
  `scope` default, or moves it on). **To let a callee use the object *without* taking ownership,
  pass `ref<T>`** (borrow — the caller keeps its `ptr`). There is **no** "borrow-by-default"
  for a `ptr` value parameter; the borrow spelling is `ref<T>`. `lifetime: consumed` (§9.2) only
  **affirms** this move — it changes nothing (a bare `ptr<T>` value parameter already moves).
- **`shared<T>` parameter (co-own)**: the handle is passed and **incref'd**; the callee is a
  co-owner for the duration (and beyond, if it stores it — `lifetime: consumed`, §9.2). Its
  drop decrefs.
- **`ref<T>` parameter (borrow)**: a non-owning borrow — no incref, no `free`. It must not
  outlive the call **except** when it is **explicitly returned** as a borrow bound to that same
  parameter (the ref-return rule below) — then the borrow's lifetime is re-projected onto the
  caller's argument. That re-projection grants an **upper bound, not a guarantee**: the returned
  `ref` is valid for **at most** as long as that argument's owner lives **AND** only while it is
  not independently invalidated by the ordinary rules (§5.4) — e.g. mutating `a.cards` (a
  reallocation/removal) invalidates a `ref` borrowed from it even though `a` still lives. Accepts
  a borrow of a `ptr` **or** a `shared` (§6.2). The correct choice for "look at, maybe hand a
  borrow back", and the way to write one function that works for both owning kinds.
- **Return by value / container** (copyable value): the returned value's string, container and
  `shared` fields are **incref'd (shared)** into the caller — a **member-wise share, not a deep
  copy** — and the callee's locals drop as usual (rc balances). `lifetime: producer` (§9.2)
  affirms the **outer** result is not aliased into the callee's state (shallow independence);
  **deep/transitive independence needs `clone`** (§9.2 depth note).
- **Return a move-only value** (a model with a `ptr`/resource field): a copy is impossible, so
  the return is a **move** — the callee's local is moved out (its scope drop suppressed), the
  caller receives sole ownership. This is a `producer` (§9.2) by construction.
- **Return `ptr<T>`**: returns a heap object the **caller now owns** and whose `scope` default
  frees it. The box is **moved out** (no realloc); `lifetime: producer` (§9.2) affirms this.
- **Return `shared<T>`**: returns a co-owned handle (the caller holds one ref); rc frees at 0.
- **Returning a `ref<T>`** — the returned borrow must be **anchored to a specific parameter via a
  viable OWNERSHIP path**, and the result is valid for **at most** the anchor argument's lifetime,
  **subject to §5.4 invalidation** (never longer, and possibly shorter). The **one guaranteed
  anchor** is a **`ref<T>` parameter**, and the borrowed referent must be reached from it **only
  through owning edges** — the param itself, its value fields, its `ptr`/`shared`/container
  members, container elements (§5.4). **Reachability alone is not enough:** a path that steps
  through a **weak `ref` field** (`a.external: ref<Card>`) does **not** prove the referent is
  owned via `a` — that field may point at an object freed independently, so a `ref` returned
  through it is **not** a valid anchor and is **rejected**. The anchor path must be provably
  owning end-to-end.
  - **Not guaranteed / disallowed anchors:** a **callee local** or a callee-`new`'d value (dies
    at return); a **`ptr<T>` parameter** (moved-in — the callee **owns** it and frees it at
    return, so a `ref` to it dangles); and — the subtle ones —
    - a **`shared<T>` parameter is NOT a safe anchor by itself**: the caller may have passed a
      **temporary** (an rvalue with no surviving named owner), which is dropped at the end of the
      call expression → the returned `ref` dangles. A `shared` argument can anchor a return only
      if it is a **named value the caller demonstrably keeps** — which the callee cannot see; so
      the language does not treat `shared` params as borrow-return anchors.
    - **`consumed` is per §9.2, not blanket "moved-in":** on a **`ptr`** param `consumed` is a
      move-in (callee owns → never an anchor); on an **rc** param `consumed` means the callee
      **retains** its own reference (the anchor for that retained reference is the *callee's*
      storage, not a caller-surviving owner) → also **not** a valid return anchor.
  - **Temporaries — same rule as §6.2.** A temporary *owning value* (an inline call that
    returns an owned value/`ptr`/`shared`) **never anchors** a returned `ref`. But an argument
    that is **already a borrow expression** — an `at`/`get` result, or another borrow function's
    return — **does** anchor: it forwards the **original owner's** lifetime, and that owner is
    what the result binds to (not the inline shape). Throughout, **the original owner's lifetime
    is what matters**, consistent with §6.2.
  In short: the anchor is whatever **durable owner** the borrowed referent belongs to — a `ref`
  parameter's argument, or the owner carried by a forwarded borrow. For handing back *ownership*,
  return `ptr`/`shared`, not `ref`.
  - **How the anchor is expressed — signature + inference.** The binding is part of the
    **signature**, written as `return: { type: ref<T>, borrows: <param_name> }`, naming the
    `ref<T>` parameter the result borrows from. The result is then valid for **at most** as long
    as the value passed for `<param_name>` lives **and** only while §5.4 has not invalidated it
    (never longer, possibly shorter). **Inference:** if the function has **exactly one** `ref<T>`
    parameter and the returned `ref` derives from it through an **owning path**, `borrows:` may be
    omitted. With **two or more** `ref<T>` parameters, `borrows:` is **required** — an
    un-annotated, ambiguous ref-return is a **compile error**. The compiler verifies the returned
    `ref` actually derives from the named parameter through an **owning path** (not a weak `ref`
    field, per the anchor rule above), else error.

```yaml
- def:
    name: pick
    args: [[a, ref<Zone>], [b, ref<Zone>]]
    return: { type: ref<Card>, borrows: a }   # valid for AT MOST as long as caller's `a` lives,
    body: [ { return: { at: [a.cards, 0] } } ] #   and only while a.cards isn't mutated (§5.4);
                                              #   must derive from `a` through owning edges, not `b`
```

## 9. Lifetime qualifiers

Written as a `lifetime: <qualifier>` key on a `decl`, a parameter, or a `return` type — e.g.
`return: { type: ptr<Node>, lifetime: producer }`, `[box, ptr<Node>, { lifetime: consumed }]`.

**Two things are separate — keep them apart:**
- **Ownership** (owning vs non-owning) is carried by the **type**: `ptr<T>` owns, `ref<T>`
  borrows/weak (§5.2). Qualifiers do **not** set ownership.
- **Lifetime qualifiers** control the two things the type does *not*: **when a `ptr<T>` box is
  dropped** (`scope`/`persistent`), and **affirm the transfer/independence contract at a call
  boundary** (`producer`/`consumed`).

Qualifiers govern only the **owned `ptr<T>` / manual `new` path** (and move-only Class-4
handles). Refcounted resources — strings, containers, Class-5 — are freed by rc automatically
and take `scope`/`persistent` only as a no-op; they may still take `producer`/`consumed` for
their *independence/retention* meaning below.

### 9.1 Drop timing — `scope` (default) and `persistent`

- **`scope` — the default for every `ptr<T>` box.** The box is released automatically at scope
  exit on **every** path (fall-through, `return`, `break`, `continue`, early error return) —
  C++ `unique_ptr` semantics. Writing `lifetime: scope` is optional and only for emphasis.
  - **Move-out suppresses the drop.** On a path where the box is handed off — returned,
    stored into a longer-lived owner, or passed to an owning `ptr<T>` parameter — the scope
    drop is **suppressed** (the object moves; no free-then-use). The source is then **moved**,
    and any later use is a **use-after-move** error (§10).
  - **Early `free` is allowed.** `free: [p]` releases now and **nulls** the slot; the scope
    drop then sees an already-released (null) slot and does nothing → no double free. Full
    manual control is preserved despite the automatic drop.
  - **Loops:** a `scope` box `new`'d inside a loop body is released **each iteration** (no
    accumulation).
- **`persistent`** — **opt out** of the scope drop. Use it when ownership escapes in a way the
  compiler cannot see as a move (anchored somewhere untracked). No auto-free here; the new
  owner is responsible. If a `persistent` box does **not** actually escape, the leak lint warns
  (§10).

### 9.2 Boundary contracts — `producer` (return) and `consumed` (parameter)

For **owning (`ptr`) boundaries** these **affirm** the contract the type already carries — a
`ptr` value parameter moves (§8), a `ptr` return moves out — so `producer`/`consumed` are
**purely affirming** there. They are kept because they (a) make intent explicit for a human or
an AI reader, (b) **guarantee a move instead of a copy** — no defensive re-allocation, and (c)
make the ownership lint **modular** (a one-line signature lookup instead of looking inside the
callee). For **rc types** (string/container), where a parameter is borrowed-for-the-call by
default, `consumed` is **not** purely affirming: it declares that the callee **retains** the
argument beyond the call (see below) — a real added contract.

- **`producer` (return)** — the returned value is **freshly produced and owned by the caller**,
  and its **outer object is not aliased into the callee's own state** (the caller may mutate the
  returned handle's *spine* without touching the callee's data). This is **shallow (outer)
  independence**, not transitive — see the depth note below.
  - On a **`ptr<T>`** return: the callee's box is **moved out** (its scope drop suppressed); the
    **same** heap block reaches the caller — **no realloc**. (Contrast a **by-value model**
    return, which per §8 copies **member-wise** — string/container/`shared` fields **incref'd
    (shared)**, scalars copied — *not* a deep copy; and a `ref<T>` return, which **borrows** — it
    hands back **no** ownership and is valid only when anchored to caller-surviving state, §8.)
  - On an **rc container / string** return: the returned handle is **not the callee's own live
    handle** — it is a fresh allocation / `copy` (shallow) / `clone` (deep). So the caller may
    mutate the **outer** container freely. (This is why `conn_registry` returns `{ type:
    list<string>, lifetime: producer }` — a snapshot spine, not a live view of the registry.)
  - **⚠ Depth of independence.** `producer` guarantees only the **outer** container/object is
    independent. For a nested type (`list<list<i32>>`, a model owning containers), a shallow
    `copy` leaves the **inner** containers **shared** (incref'd) with the callee — mutating an
    inner list *would* be visible on both sides. **Transitive (deep) independence requires
    `clone`** and is a **stronger, explicit** guarantee that `producer` alone does **not**
    imply. If a producer must be deeply independent, its body must `clone`; the annotation
    should say so (e.g. a doc note or a `producer(deep)` form — §11.3 leaves the surface open).
  - **Checked obligation (callee):** every return path must yield a value whose **outer** object
    is freshly owned and un-aliased (an unfreed `new`, another `producer` result moved through,
    or a `copy`/`clone`). The checker verifies outer-independence, not depth.
  - **Caller reads only the annotation:** `producer` ⇒ the result is owned and its outer object
    independent ⇒ the receiving `scope` local frees it (`ptr<T>`) or may keep it (rc). No
    `producer` ⇒ a plain `ptr<T>` return is still owning; a `ref<T>` return is a borrow the
    caller must not free.
- **`consumed` (parameter)** — the callee **takes/keeps** what is passed.
  - On a **`ptr<T>`** parameter: this is **already the default** (a `ptr` value parameter moves,
    §8) — `consumed` only **affirms** it. Ownership is moved in (a sink); the caller's box is
    moved (use-after-move afterward), and the callee must `free`/move/store it (its own scope
    drop applies otherwise, with a warning if it neither frees nor hands it on). To *borrow*
    instead (no transfer), the parameter must be `ref<T>`, not `ptr<T>` — there is no `consumed`
    that would turn a `ptr` param into a borrow.
  - On an **rc container / string** parameter (which by default is **borrowed**, incref'd only
    for the call): `consumed` says the callee **retains a reference beyond the call** (stores it,
    keeps the incref) — here `consumed` **does** change the caller's expectation (the argument is
    now co-owned), so it is meaningful, not purely affirming, for rc parameters.

```yaml
- def:
    name: build
    return: { type: ptr<Node>, lifetime: producer }   # affirm: caller owns the returned box
    body:
      - decl: [n, ptr<Node>, { new: Node }]           # scope-default
      - return: n                                       # MOVE out — no realloc, no copy
- def:
    name: keep
    args: [[node, ptr<Node>, { lifetime: consumed }]]  # sink: takes ownership
    body:
      - call: [store.nodes.push, node]                 # callee now owns it
- def:
    name: main
    body:
      - decl: [r, ptr<Node>, { call: build }]          # producer ⇒ r owns ⇒ scope frees r…
      - call: [keep, r]                                # …unless moved: consumed moves r out
      # r is moved-out here; using r after this line is a use-after-move error.
```

> Qualifiers only ever *shorten or affirm* what the explicit `new`/`free` + `ptr`/`ref` model
> already expresses; omitting `producer`/`consumed` leaves the type's ownership unchanged,
> only less explicit. The exact reserved set is `scope`, `persistent`, `producer`, `consumed`.

## 10. Diagnostics

> **Implementation status (memV3).** A conservative, report-only borrow/ownership pass
> (`src/core/borrow_check.cpp`, memV3-only, isolated from drop-planning) now implements most of
> §10. **Errors:** shallow `{copy}` of a move-only element; illegal `shared→ptr` assignment;
> **use-after-move**; **use-after-free**; **double-free**; `free` of a `shared<T>`; `free` of a
> non-heap value. **Warnings:** the **owning-cycle** lint and the **persistent-leak** lint. The
> pass is an intra-function flow (ptr Live/Moved/Freed + `at`/`get` ref-borrow Valid/Invalidated)
> with must-* merge across `if`/`else` (loops/`try` reset) — no false positives on the acceptance
> suite. `free:` now materializes as a real node under memV3 (active early-release + slot-null; OFF
> drops it byte-identically), which is what makes the free-based checks possible.
>
> **§10 is now COMPLETE.** Also implemented: **`ref→ptr`/`ref→shared` cross-kind** rejection and
> the **invalidated element borrow** check (§5.4 — a ref from `at`/`get` used after a structural
> mutation of its container). Probes: `tests/lifetime/neg/n01–n09`, `warn/w01–w02`.

- **use-after-free** — using a `ptr<T>` after `free` on it, within visible scope: error.
- **use-after-move** — using a `ptr<T>` (or a `shared` handed to `{share}`) after it was
  moved (assigned/pushed/passed-by-value/returned/consumed): error. This is what enforces
  unique ownership (§6.2).
- **double free** — two `free`s reaching the same box on a path: error.
- **`free` of a non-heap value** — `free` on a stack value, a `shared`/`ref`, or a non-`new`
  handle: error (a `shared` is released by rc, never `free`d; a `ref` never owns).
- **illegal cross-kind assignment** — any conversion the §6.2 matrix forbids (notably
  `shared<T>` → `ptr<T>`): error.
- **shallow `copy` of a move-only element** — `{ copy: … }` on a container whose element type
  is move-only (`ptr<T>`, a by-value Class-4 resource, or a move-only model, §5.1): error
  (would duplicate a unique owner → double free). Use `clone` (deep) instead.
- **invalidated element borrow** — using a `ref<T>` from `at`/`get` after an op that invalidated
  it, **per the differentiated §5.4 rules** (not a blanket "remove ⇒ dangling"): any growth/shift
  on an **inline-element** container; on a **pointer-element** container, `remove`/`set`/`clear`
  frees the pointee only for **`ptr`** (always) or **`shared`** (only at the last ref) — for
  **`ref`** elements the referent is untouched — and **`pop` keeps the pointee alive** (borrow
  stays valid); plus **external** release of a referent owned elsewhere. Error where locally
  visible; the exact predicate is §5.4.
- **leak lint** — because `scope` is the default (§9.1), a plain `new` box **cannot** leak (it
  auto-frees). The lint therefore targets the escape hatches: a **`persistent`** box that
  reaches scope exit **without** being moved out / stored anywhere (a declared escape that does
  not happen → leak): **warning** (honours `static_off`). A moved-out box is the new owner's
  responsibility (checked there).
- **owning-cycle lint** — a type graph whose **owning** edges of **any** kind — `ptr<T>`
  (unique), `shared<T>`, and container elements — form a cycle with **no `ref<T>`** edge to
  break it: **warning** ("this structure can leak; make one back-edge `ref<T>`"). Includes
  **mixed** `ptr`+rc cycles and unique-`ptr` self/mutual loops (unique ownership does **not**
  prevent cycles — §5.2). It flags *that* a weak edge is required, not *which* (semantic).
- **string / container correctness** is automatic (rc for strings and containers) and needs no
  diagnostics; reference cycles are the one rc hazard, covered by the lint above.

## 11. Open design decisions (to confirm before implementation)

**11.1 Container copy semantics.** **RESOLVED → Option B** (refcounted shared handles, §5).
Copy shares (incref), mutation aliases, freed at rc 0; independence via `copy` (shallow) /
`clone` (deep, §5.1). Chosen for consistency with the "everything is a handle" reality of the
built-ins (§2.1) and to avoid implicit deep copies. Accepted costs: aliasing semantics,
reference cycles (§5.2 weak `ref<T>` + §10 lint), and rc must be added to containers.

**11.2 Container parameter passing.** **RESOLVED → by shared handle (incref).** Matches
today's by-reference behavior and Option B; independence is explicit (`copy`/`clone`). No
surprise deep copies of large lists.

**11.3 Qualifier surface.** **RESOLVED (§9).** `scope` is the **default** drop for every
`ptr<T>` box (unique_ptr semantics; move-out suppresses it); `persistent` opts out. Ownership
lives in the **type** (`ptr` owns, `ref` borrows/weak) — `consumed` (param) and `producer`
(return) are kept as **explicit affirmations** of the transfer/independence contract (retained
for readability, the move-not-copy guarantee, modular lint, and back-compat with 9rooms's
existing `lifetime: producer`). Reserved set: `scope`, `persistent`, `producer`, `consumed`.
*Note: `scope`-as-default makes the drop opt-**out**; the MEMORY.md goal wording ("Qualifier
opt-in") should be reconciled to say the drop is automatic and `persistent`/manual `free` are
the opt-outs.*

**11.4 COW for strings/containers?** Not chosen (adds a hidden mutation check + non-obvious
copy point); listed only as a possible future optimisation.

**11.5 `ptr<T>` = owning, `ref<T>` = non-owning/weak.** **RESOLVED** (§5.2, §6.2). Replaces the
earlier draft claim that `ptr<>` fields are non-owning — that was **wrong** and contradicted
spec/18. `ptr<T>` is a **unique** owner (move, no incref); `free`/release **follows** it and
**ignores `ref<>`** (weak back-edge). Requires the coordinated spec/18 update in §12 (a stored
`ref<T>` becomes a *weak* reference).

**11.9 User-model ownership.** **RESOLVED → M3** (§6.2). Three reference kinds for a `new`
object: **`ptr<T>`** unique owner (move; the default, for entities that live in one place and
travel), **`shared<T>`** opt-in co-owned refcounted handle (for genuine co-ownership), and
**`ref<T>`** the universal non-owning borrow. Cross-kind conversion follows the §6.2 matrix:
`shared`→`ptr` **forbidden**, `ptr`→`shared` only via explicit `{ share: p }` (move-adopt +
realloc), and the only implicit bridge is `ref`. For durable/serializable references between
entities prefer an **id + owning registry** over a stored weak `ref` (an id cannot dangle and
serializes; §5.2). Rationale: single-owner+move is the correct model for card-game entities
(they move, are not co-owned) and stays deterministic — though **not automatically cycle-free**
(a unique owner moved into its own subgraph still cycles, §5.2; break it with `ref`/id); `shared` is available where
truly needed without making rc the default for models (avoids the v2 "everything refcounted").

**11.6 Class-4 vs Class-5 boundary (§2.1).** **Resolved by code:** `lock` is structurally
co-owned across threads → **Class 5** (today it has no `pthread_mutex_destroy` and leaks; the
spec mandates a destroy-on-last-drop). `mirror_session` is a **single-owner** handle with an
explicit `session.close()` → **Class 4**; `mirror_peer` is owned by its session (created/
`delete`d inside the session, addressed by `peer_id`) → **non-owning** from user code, not a
separately-owned handle.

**11.7 `path` placement (§2.1).** **Resolved by code:** `getLLVMType("path")` is an interned
immutable `char*` — identical representation to `string`. → **Class 2.** (Left here only to
confirm no OS resource is attached; none is.)

**11.8 Handle types whose teardown was undecided (§2.1).** **Resolved by code:**
- `input_event` — a pointer into a **static global** buffer, overwritten on the next poll →
  **non-owning borrow** (never freed by the user).
- `ui_font`, `ui_*_style` — heap resources (`new` + GL texture / atlas), **mutable** after
  creation (many setters), currently leaked → **Class 4** (owned resource; a real
  free/`free_font` must be exposed — see †). Not immutable, so not Class 2.
- `mirror_event`/`rpc_event` — value structs (Class 1) **containing borrowed pointers**
  (`peer`, `type`, `method`): the struct is copied by value; those inner pointers are
  **non-owning views** and must not be freed through the event.

**11.10 DB handles are Class-6 singletons, not Class-4.** **Resolved by code + Spec 47:**
`db.open_*` returns the **same handle** for the same name+path (Spec 47 §1, Category 1); the
runtime owns and mutex-protects them. Classifying them Class-4-unique (as an earlier draft did)
would let user `close`/scope-drop **prematurely close** a handle other holders still use →
double free. → **Class 6** (non-owning user handle; the runtime owns the lifetime; user never
frees). Coordinated with Spec 47.

**11.11 `array<T:N>` vs `dyn_array<T>`.** **Resolved by code:** `array<T:N>` lowers to an inline
`[N × T]` value block → **Class-1 storage**, but copy/move/drop are **recursive on `T`** (§7):
trivial for `array<i32:N>`, incref/decref for `array<string:N>`, **move-only** for
`array<tensor:N>`/`array<ptr<T>:N>` (§2.1 row 1′). `dyn_array<T>` is a heap `i8*` runtime-sized
buffer → **Class 4** (a *unique, unmanaged* single-`malloc` scalar buffer — **not** rc'd/aliased;
its advantage over `list` is exactly *one* allocation and zero per-op rc; §5.6). It is **not**
Class 3: the earlier "same discipline as `list`" reading is superseded (§5.6, spec/53).

**11.12 Aggregates with Class-4 (resource) members.** **RESOLVED (§6.1/§7 composition rule):**
a model/struct/`array` holding a resource **by value** is **move-only** and releasing it
**closes** the resource. It is deep-`clone`able **iff** the resource has an element clone
(`tensor` yes, via `aiml_tensor_clone`; a raw OS handle no → neither copy nor clone, only move).
Copyability/clonability of any aggregate is **composed** from its members (§7). A member that
only *borrows* a resource holds `ref<T>` (weak) and stays copyable.

**11.13 Refcount thread-safety.** **RESOLVED & CONFIRMED (§5.3):** atomic rc for `shared<T>`
**and Class 5 (`chan`/`lock`)** — the types that are cross-thread-shared (by opt-in or by
nature). Class 2/3 rc is **non-atomic** and single-thread-owned, crossing threads only by
**move** (Rust-`Send`); Class 6 / `log` / `path`-intern are runtime-mutex-protected
(Category 1). The cost trade is deliberate: single-threaded string/container work pays **no**
atomic overhead; only the shared kinds do. **Key principle:** the rc is separate from the
payload — a user lock on the data does **not** protect the implicit incref/decref, and an atomic
rc protects only the count, not the payload. Coordinated with Spec 47.

**11.14 `string` is share-on-assign under memV3 (implemented).** `set`/`decl`/`push` of a string
**incref** the shared buffer via its rc header (`string_runtime.cpp`), not `aiml_str_dup`; a
string literal / immortal `""` (rc `0xFFFFFFFF`, no header) is a no-op on drop (§4.6). This is
§4's share-by-incref, now the default (`AIML_MEM_V3`). (The legacy `dup`-on-assign path survives
only under `AIML_MEM_V3=0` for A/B comparison; see §2.1.)

**11.15 `prefab_instance` — single classification: engine-owned, non-owning handle.** Not a
Class-4 unique owner *and* a borrow at once (that double reading is resolved): the **engine**
owns the node (`engine.instantiate` → `g_scene->instances`), the user handle is **non-owning**
(scope-drop is a no-op, never `free`d by scope). `engine.destroy`/`is_alive` are **engine scene
ops**, not user ownership. → **special/non-owning** (Class-6-adjacent).

**11.16 `producer` depth (OPEN — surface only).** `producer` guarantees **outer** independence
(the returned object is not aliased into the callee's state), **not** transitive: a nested
`list<list<i32>>` returned via a shallow `copy` still shares its inner lists (§9.2 depth note).
Deep independence requires the body to `clone`. **Open:** whether to add an explicit
**`producer(deep)`** (or `lifetime: producer_deep`) surface that *checks* the body returns a
deep clone, versus leaving deep-vs-shallow as a documented convention. The **semantics** are
fixed (producer = outer-only; clone = deep); only the annotation surface is open, alongside
§11.3.

**11.17 Deep-clone of `shared` sub-objects — default chosen, variant open.** The §5.1 contract
makes `clone` **fully independent**: a `shared<T>` sub-object reached within the owned subgraph
is **cloned once** (memoized) so the clone shares no mutable state with the original. This is
the chosen default. **Open (variant):** a "clone-structure-but-keep-shared-resources" mode that
would instead **incref** external/`shared` sub-objects rather than duplicate them. Not needed
now; recorded so the default (full independence) is an explicit choice, not an accident.

**11.18 Cross-thread transfer contract — RESOLVED → Model P (message-passing) with statically
enforced move-send + guarded `shared`.** (§5.3.) The safe cross-thread paths are decided:
- **Move-send (primary):** `chan.send`/spawn **consumes** an **exclusively-owned** value
  (transitive sole ownership, **weak-ref-closed**). Exclusivity is **statically** established by
  an **affine/uniqueness analysis** (every rc member moved in fresh, nothing aliased) — zero
  copy for build-then-send; a non-provable send is a **compile error**. **`send_unique(v)`** is
  the runtime escape (rc == 1 check + move, else trap). **No forced clone** — `clone` only to
  keep a copy of shared data. Outbound weak edges are **forbidden** (compile error); use
  **id + Category-1 registry** for durable cross-thread references.
- **Guarded `shared<T>` (opt-in co-ownership):** atomic rc + a **mandatory lock**; payload
  reachable only inside the **lock guard**, and **no rc-bearing value escapes the guard** (scoped
  borrow) so every rc op — incref *and* its drop — is under the lock. Statically enforced.
- **Class-5 (`chan`/`lock`) atomic**, **Category-1** (`db`/`log`/`path`) runtime-locked —
  unchanged.

Rationale: the common case is safe **by construction** (move an exclusive value; the sender
keeps nothing), and genuine co-ownership is an **explicit, statically-guarded** escape — no
whole-program alias inference, no reliance on unverifiable user lock discipline. Coordinated with
**Spec 47**; the `chan`/spawn move-send API and `send_unique`/guarded-`shared` surface must be
specified there before the thread path is implemented.

## 12. Relationship to existing specs & migration

> **This spec's changes reach well beyond spec/18.** The coordinated updates are spec/18
> (`ptr`/`ref` semantics), **spec/47** (thread-safety of rc — §5.3), **spec/10** (collection
> element access/extraction/invalidation §5.4, aliasing under Option B), **spec/52** (fixed
> `array<T:N>` — raw-pointer `at`/`idx` and element ownership), **spec/53** (`dyn_array<T>` —
> manual `.free` lifetime → Class-3 rc), and **spec/45** (deep-`clone` graph contract — the
> truncating serializer copy is insufficient, §5.1), and **spec/29.3** (copy/clone verb type
> mapping). Each is listed below.

- **spec/18 (Refs and pointers) — COORDINATED UPDATE REQUIRED.** spec/18 today says `ptr<T>`
  and `ref<T>` are functionally identical, and that a `ref<T>` must **never** be stored in a
  field / outlive its scope. This spec makes them **semantically distinct**: `ptr<T>` = **unique
  owning — move on handoff, no incref** — followed by release; `ref<T>` = non-owning (borrow as
  parameter, **weak** as a stored field). spec/18 must be updated so that a **stored `ref<T>` is
  a defined weak reference** (not undefined behaviour) — this is what makes cycle-breaking (§5.2)
  expressible. Until both land together (Principle 2), the two specs are inconsistent; flag on
  divergence.
- **spec/15 (Models):** `decl: [x, Model]` = stack value; `decl: [p, ptr<Model>, {new}]` +
  `free` = heap. This draft makes that split load-bearing (it was descriptive before).
- **spec/45 (Heap serialization) — COORDINATED UPDATE REQUIRED.** The decoded-model release
  primitive is reused as the per-object destructor for `free` / scope exit (fine). But
  `aiml_ser_deep_copy` **cannot** back the deep `clone` verb: it is serialization-based and
  **depth-truncating**, so it fails the §5.1 deep-clone contract for object graphs (recursive/
  cyclic/DAG types — even a finite linked list — are truncated or mis-copied, and shared
  sub-objects are duplicated). `clone` must be reimplemented as the **memoized, cycle-safe
  graph walk** of §5.1 (original→clone map, weak-`ref` internal/external rewiring). Spec 45's
  copy path must either be upgraded to that contract or `clone` must use a new primitive.
- **spec/29.3 (copy/clone verbs) — COORDINATED UPDATE REQUIRED.** The clone **type mapping** of
  §5.1 (result type = target: declared slot → context type → default; source kind → owned
  result: value→value, `ptr`→`ptr`, `shared`→`shared`, **`ref`→owned value / `ptr` / `shared`
  per the target, never `ref`**, container→deep container) **replaces the old
  same-argument-and-result-type rule**, which collides with "clone never yields a bare `ref`".
  Spec 29.3 must adopt this mapping (incl. the target-type resolution and the graph-ownership
  runtime check) and the shallow `copy` (§5.1) / `clone` split.
- **spec/10 (Collections) — COORDINATED UPDATE REQUIRED.** Spec 10 still describes element
  access as a **pointer-copy**; under §5.4 that is wrong for `list<ptr<T>>` (a pointer-copy of a
  unique owner → double free). The verbs must be respecified per §5.4: **`at`/`get` = borrow
  (`ref<T>`)**, **`pop`/`take` = move-out** (the only way to extract an owning `ptr<T>`),
  **`remove` = release**, **`set`(i) = secure-new (incref/move) then release-old** (§5.5 order);
  plus the **borrow
  invalidation** rule per §5.4 — inline-element containers on growth/shift; pointer-element
  containers by element kind (`ptr` frees on remove, `shared` only at the last ref, `ref` never,
  `pop` keeps the pointee alive) plus external release; **not** the shortened "remove ⇒
  dangling". The shallow `copy` verb (§5.1) also lands here.
- **spec/52 (Arrays, `array<T:N>`) — COORDINATED UPDATE REQUIRED.** Spec 52 uses **raw-pointer**
  element semantics: `at` **returns a `ptr<T>` (a pointer copy)** and `idx`/set writes a raw
  pointer (`array<ptr<Node>:8>`, `at`→`ptr<T>`, `idx: [nodes,0,p]`). Under §5.4/§7 that is
  wrong: **`at` = borrow (`ref<T>`)**, **`set` = secure-new (incref/move) then release-old**
  (§5.5 order), and an
  `array<ptr<T>:N>` / `array<tensor:N>` is **move-only** with per-element drop (§7 composition).
  Spec 52 must adopt the §5.4 verbs and the element-recursive copy/move/drop (§2.1 row 1′).
- **spec/53 (Dynamic arrays, `dyn_array<T>`) — COORDINATED UPDATE REQUIRED.** Spec 53 makes
  lifetime a **manual, optional `arr.free`** (`free(ptr)`; "optional — reclaimed on exit
  otherwise", i.e. it leaks if omitted). Under §5.6 `dyn_array<T>` is **Class 4 (unique,
  unmanaged single-`malloc` buffer)** — **not** rc'd/aliased (its advantage is exactly *one*
  allocation): it is freed **once at owner drop** (scope-default, §9.1), with `.free` as an
  **optional early** release (nulls the slot so the scope-drop is a no-op). Assignment/pass is a
  **move** (unique owner; shallow copy would double-free); `clone` (fresh `malloc` + byte-copy)
  is the way to duplicate. Spec 53 must switch from *manual-`.free`-or-leak* to *scope-default
  free once* + `.free`-as-early-release, and state the move-on-assign rule.
- **spec/47 (Thread safety) — COORDINATED UPDATE REQUIRED.** §5.3 adopts **Model P**
  (message-passing primary, §11.18): non-atomic rc for Class 2/3, crossing threads only by
  **statically-checked move-send** of an exclusive value (`send_unique` runtime escape;
  weak-ref-closed; no forced clone); **atomic rc for `shared<T>` and Class 5**; cross-thread
  co-ownership only via a **guarded `shared<T>`** (mandatory lock, nothing rc escapes the guard);
  Category-1 (`db`/`log`/`path`) unchanged. spec/47 must add the **`chan`/spawn move-send API**,
  **`send_unique`**, and the **guarded-`shared`/lock-guard** surface; place each rc'd type in its
  Category table; and state the **rc-vs-payload principle** (a bare user lock does not cover the
  implicit incref/decref — only the guard does; atomic rc protects the count, not the payload).
  Confirms the singleton contract for Class-6 (§11.10).
- **String sharing — DONE (memV3).** Every assignment path shares by **incref**, not `dup`:
  `set: [a, b]` / `decl: [a, string, b]` and container `push` all incref the buffer via its rc
  header; scope-exit decref frees at rc 0; the immortal `""` (no header) is a no-op. The legacy
  `dup`-on-assign path is kept only under `AIML_MEM_V3=0` for A/B comparison (§2.1, §11.14).
- **Containers gain rc — DONE (memV3).** `list`/`map`/`buffer` are refcounted **handles** (Option
  B): the heap block carries an `rc`, incref-on-share / decref-on-drop across assignment, storage,
  params, returns, and removal; container **fields** and nested **elements/values** are handles
  too (§5.7). Freed at rc 0.
- **Migration — 9rooms:** because `scope` is the default (§9.1), most 9rooms `new` boxes that
  were "freed implicitly / via drops-on" become **correct automatically** (auto-freed at scope)
  — there is **no** requirement to write a `free` for every `new`. The audit instead finds the
  cases that deviate from the default: (a) a box whose ownership **escapes** its scope (stored
  in a longer-lived owner without being a syntactic move) must be marked **`persistent`**, else
  it is auto-freed too early (dangling); (b) any reference **cycle** through refcounted edges
  needs one `ref<T>` weak back-edge or an id+registry (§5.2), else it leaks; (c) a spot that
  wants **early** release keeps an explicit `free`. Smaller and more targeted than a
  free-everywhere pass, but it does require identifying every escaping box.

## 13. Non-goals

- No tracing garbage collector.
- No whole-program escape analysis.
- rc is the discipline for the type classes that declare it — Class 2 (immutable handles:
  `string`, `path`), Class 3 (containers: `list`/`map`/`buffer`, Option B, §5), Class 5
  (shared resources: `chan`, `lock`), and **opt-in `shared<T>`** for user models (§6.2). It is
  **not** the default for a user `new` object: `ptr<T>` is **unique** (no rc), released
  manually (`new`/`free`) or by the `scope` default (§9). `shared<T>` is chosen explicitly
  where co-ownership is genuinely needed — never the default (avoids the v2 "everything
  refcounted").
- No automatic cycle reclamation. Ownership cycles — through **any** owning edge: containers,
  `shared`, **or a unique `ptr` moved into its own owned subgraph** (§5.2) — are broken
  **manually** with a `ref<T>` weak back-edge or an id+registry; the owning-cycle lint (§10)
  only warns. Unique ownership does **not** by itself prevent cycles.
