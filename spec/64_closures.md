# 64 — Blocks and Closures [PROPOSAL]

> Status: PROPOSAL. Derived from `doc/analysis/closures_feasibility_2026-10-15.md`, scoped to
> that analysis' recommended first scope, with three design decisions taken afterwards
> (2026-10-15): **named signatures** instead of an inline `<param:return>` notation, **two
> kinds** (a non-escaping `block`, an escaping `closure`), and **`fn<…>` left untouched**.
> Where this spec and the implementation disagree, **this spec wins** (Arbeitsprinzip 2).
> Memory wording, qualifiers and diagnostics are those of `spec/61_memory_management.md`.

## 1. Three callable types, and why

A callable either carries captured state or it does not, and if it does, it either outlives the
scope it was created in or it does not. Those two questions have different right answers, so they
get different types instead of one type with hidden costs:

| | `fn<…>` (spec/12_1) | **`block<Sig>`** | **`closure<Sig>`** |
|---|---|---|---|
| Carries captured state | no | **yes** — the creating frame itself | **yes** — a heap environment |
| Captures are written | — | **implicitly** (everything in scope) | **explicitly** (`capture:`) |
| Mutating a capture | — | writes the **enclosing variable** | writes the closure's **own field** |
| May escape its scope | yes (it is just code) | **no** (statically rejected) | **yes** — that is its purpose |
| Allocation | none | **none** (borrows the live frame) | one heap environment |
| Duplicate | value-copy | borrow-copy (non-owning) | **move-only** |
| Teardown | none | **none** | release environment + captures **once** |
| Memory class (§2.1) | 1 — Value (POD) | **non-owning / borrowed view** | **4 — Unique resource** |

`block<Sig>` is the Groovy case: a body handed to a function that calls it and then forgets it.
Because it provably does not escape, its captures are direct references into a frame that is still
alive — implicit capture is *safe*, nothing is copied, nothing is owned, nothing is freed.

`closure<Sig>` is the case where the callable outlives its birth scope — a factory result, a list
of handlers, stored state. There the frame is gone, so every capture must be brought along, and
whoever brought it along owns it.

`fn<…>` keeps its current meaning, notation, layout and ABI **unchanged**. There is no implicit
conversion and no cast between `fn<…>` and `block`/`closure` in any direction (§8).

## 2. `callable:` — the signature declaration

A callable signature is declared **once**, at the top level, like `model:` / `interface:` /
`enum:`, and referred to by name afterwards:

```yaml
- callable: { name: Each,   args: [[v, i32]] }                      # (i32) -> void
- callable: { name: Mapper, args: [[x, i32]], return: string }      # (i32) -> string
- callable:
    name: Cmp
    args: [[a, i32], [b, i32]]
    return: bool

- decl: [f,  closure<Mapper>]
- decl: [fs, list<closure<Mapper>>]
- args: [[xs, list<i32>], [body, block<Each>]]
```

| Key | Required | Meaning |
|---|---|---|
| `name` | **yes** | the signature's name; visible by the ordinary import-closure rules (spec/21 §2.1) |
| `args` | no (default `[]`) | parameters as in `def:` — `[[name, type], …]`, **named** |
| `return` | no (default `void`) | return type as in `def:` |

Default argument values (`[name, type, default]`, spec/07) are **not** accepted in a `callable:`
— default-argument forwarding through an indirect call is out of scope (§8).

**Type identity is structural, not nominal.** Two signatures with the same parameter types in the
same order and the same return type denote the **same** type; the name and the parameter names are
documentation. So `closure<Mapper>` and `closure<Mapper2>` with identical signatures are
interchangeable, and renaming a signature never changes what matches what.

**Parameter names are load-bearing in one way**: they are the default parameter names of a
`block:` / `closure:` written against that signature (§3.1), and they appear in diagnostics
("parameter `level` expects i32" instead of "argument 2 does not match").

**No commas inside a type.** This is why the notation is named rather than inline: `closure<Cmp>`
contains no comma, so it never needs quoting in a YAML flow sequence. (The inline `fn<…>` form
does — `[sinkfn, 'fn<string,i32:i32>']` — see §11.)

`callable:` names are accepted in `block<…>` and `closure<…>` **only**. `fn<Mapper>` is not
accepted; `fn<…>` keeps the inline notation of spec/12_1 (§11). Unifying them later is possible
and deliberately out of scope here.

## 3. `block<Sig>` — the non-escaping body

A `block:` expression creates a body that borrows the frame it was written in:

```yaml
- callable: { name: Each, args: [[v, i32]] }

- def:
    name: each
    args: [[xs, list<i32>], [body, block<Each>]]
    body:
      - decl: [idx, i32, 0]
      - while:
          condition: { lt: [idx, xs.len] }
          body:
            - call: [body, { at: [xs, idx] }]     # run the passed body, as often as wanted
            - set: [idx, { +: [idx, 1] }]

- def:
    name: main
    return: i32
    body:
      - decl: [nums, list<i32>]
      - call: [nums.push, 3]
      - call: [nums.push, 4]
      - decl: [sum, i32, 0]
      - call:                                      # the body IS the last argument
          - each
          - nums
          - block:
              body:
                - set: [sum, { +: [sum, v] }]      # `v` from Each; `sum` is the real local
      - return: { -: [sum, 7] }                    # 0 — sum was mutated in place
```

Keys of a `block:` map: `args` (optional), `body` (**required**). There is no `capture:` — capture
is implicit — and no `name:`/`version:`.

### 3.1 Implicit capture, and where the parameter names come from

- Every name visible at the `block:` expression is visible inside its body: the enclosing
  function's parameters and locals, enclosing captures, globals, functions and types. Nothing is
  enumerated, nothing is copied: the body reads and writes **the enclosing variables themselves**.
  A name declared *after* the `block:` is not in scope and is not captured.
- The body's own `args` and locals **shadow** enclosing names by the ordinary scope rules
  (spec/07). No special shadowing rule is needed.
- **`args:` may be omitted**, and then the parameter names come from the target signature — `v`
  in the example above is `Each`'s own parameter name. This requires the target type to be known
  from the context (an argument position, or a `decl` with a declared `block<Sig>` type); where it
  is not, `args:` is **required** and its types must match the signature.

### 3.2 A block may not escape

A `block` value may only be:

1. passed as an argument to a `block<Sig>` parameter,
2. bound to a local **in the same scope** as the `block:` expression that produced it,
3. called.

A `block<Sig>` **parameter** may be called and passed on to another `block<Sig>` parameter.
Everything else is a `block-escapes` error (§9): returning it, storing it in a longer-lived local,
a model/class field, a container or a global, capturing it in a `closure:` (it is a borrowed view,
§4.3), or sending it to a thread, `chan`, KV, mirror or `sys.bundle`.

Because of that rule the borrowed frame is always alive while the block is callable, so:

- there is **no** allocation, **no** refcount and **no** teardown — a `block` is a non-owning
  borrowed view (spec/61 §2.1, last row); its drop is a no-op,
- no capture can dangle, and no ownership question arises at all.

### 3.3 Control flow out of a block

- **`return:` returns from the block**, not from the enclosing function — it yields the block's
  return value to whoever called the block.
- **`throw:` propagates to the caller of the block** and unwinds normally from there (spec/22,
  spec/61 §9.3). Nothing of the block itself needs releasing.
- **`break` / `continue` targeting a loop outside the block** is a `block-nonlocal-control` error.
  Non-local exit from a passed body is out of scope (§8); throw/catch is the supported way.

## 4. `closure<Sig>` — the escaping closure

A `closure:` expression creates a callable that owns a heap environment and may outlive the scope
it was created in. Its captures are **always enumerated**.

```yaml
- callable: { name: Adder, args: [[x, i32]], return: i32 }

- def:
    name: make_adder
    args: [[k, i32]]
    return: { type: closure<Adder>, lifetime: producer }
    body:
      - return:
          closure:
            capture: [k]
            body:
              - return: { +: [x, k] }        # `x` from Adder (args: omitted, §3.1)

- decl: [add3, closure<Adder>, { call: [make_adder, 3] }]
- decl: [r, i32, { call: [add3, 10] }]       # 13
```

Keys: `capture` (optional, default `[]`), `args` (optional — names from the signature, §3.1),
`body` (**required**). `name:` and `version:` are not accepted (§8). The compiler derives a
deterministic internal symbol name and environment type per `closure:` expression, so the
import/cache key (spec/37) is stable across runs of the same source.

The body sees exactly: its `args`, its locals, the **captured names**, and globals/functions/types
(spec/21 §2.1). **The enclosing function's locals are NOT visible unless captured** — using one is
a `closure-free-variable` error. This is the one place where enumeration is required, and the
reason is that the frame is gone by the time the closure runs. `args` and `capture` share one
namespace; a duplicate is an error. A capture may shadow a global; inside the body the capture
wins.

### 4.1 The capture list

Each entry has one of four forms:

```yaml
capture:
  - count                              # (a) value capture of the in-scope variable `count`
  - [cards, { mode: clone }]           # (b) in-scope variable, explicit mode
  - [n, i32, 0]                        # (c) new environment field, init evaluated ONCE
  - [node, ptr<Node>, src, { mode: move }]   # (d) new field + init + explicit mode
```

- **(a)/(b)** capture the in-scope variable of that name; the environment field has that
  variable's declared type and keeps the same name inside the body.
- **(c)/(d)** are shaped exactly like `decl: [name, type, init]`: they declare an environment
  field and initialise it from the expression. This is how a closure gets state with no
  counterpart in the enclosing scope (a counter, §6.2), and how an arbitrary expression is
  captured under a chosen name.

Every capture initialiser — the implicit one of (a)/(b) and the explicit one of (c)/(d) — is
evaluated **exactly once**, at closure creation, in list order.

### 4.2 Capture modes

The mode decides **how** the value is brought in. It is existing spec/61 vocabulary; nothing new:

| `mode` | Effect | Notes |
|---|---|---|
| `value` (**default**) | the spec/61 §8 **value-parameter** rule: scalars and value-model fields copied field-wise, `string` / container / `shared<T>` fields **incref'd (shared)** | not a deep copy; a captured `list` **aliases** the enclosing one (§5 reference semantics) |
| `copy` | spec/61 §5.1 **shallow** copy — independent outer spine, referents shared | rejected for a move-only element type, as §10 |
| `clone` | spec/61 §5.1 **deep** copy — fully independent | the way to get a private container |
| `move` | spec/61 move — the source is **moved-out**; later use is **use-after-move** (§61 §10) | **required** for `ptr<T>` and every move-only value |

`mode: move` on a `ptr<T>` is the only legal way to capture one: the single ownership transfers
into the environment, so no second owner is created. Any other mode on a `ptr<T>` is a
`closure-move-required` error.

### 4.3 What a closure may not capture

Rejected with `closure-borrowed-capture` (§9) — the capture would outlive what it points at:

- a `block<Sig>` value (a borrowed view of a frame — §3.2),
- `ref<T>` values (the weak / borrow handle, spec/61 §5.2),
- every **non-owning / borrowed view** of spec/61 §2.1: `any`, `input_event`, `yaml_node`,
  `mirror_peer`, `prefab_instance`, a `tensor` slice/reshape (`owns_data = 0`),
- the direct result of an element borrow (`at` / `get`) — capture `[name, T, { clone: … }]` /
  `{ copy: … }`, or a `shared<T>`, instead.

With that rule **no closure can hold a pointer into a frame that has been left**. Relaxing it
needs an anchor rule like spec/61 §8's `borrows:`, which this scope does not define.

In scope: `string`, `list`/`map`/`buffer`/`cascade`, value models, `shared<T>`, `ptr<T>` (by
`move`), `array<T:N>`, Class-4 resources (by `move`), and other `closure<Sig>` values (by `move`).

## 5. Calling

Both kinds are called with `call` — identical to a direct call and to a `fn<…>` call (spec/12_1):

```yaml
- call: [body, 7]                         # statement
- set: [x, { call: [add3, 1] }]           # expression
```

**Calling borrows.** A call never consumes, moves or re-owns either kind; both may be called any
number of times. Argument passing and returning follow the **existing** ABI of spec/61 §8 exactly
— a container argument is incref'd for the call, a `ptr` argument moves in, a value model is
copied member-wise, a returned string is owning. Neither kind introduces a second calling
convention.

An uninitialised `closure<Sig>` is **null**; calling it raises `NullPointerException` (spec/22),
as for `fn<…>`. `{ neq: [c, null] }` tests it. A `block` cannot be null — it has no uninitialised
form (it may only come from a `block:` expression or a `block` parameter, §3.2).

## 6. Ownership and lifetime

A `block` owns nothing: drop is a no-op, there is nothing to free, and §3.2 guarantees the
borrowed frame outlives it. The rest of this section is about `closure<Sig>`, which is a
**Class-4 unique resource** (spec/61 §2.1): move-only, teardown exactly once at owner drop.

- **Drop.** `scope` is the default (spec/61 §9.1): at scope exit, on **every** path including
  `throw`-unwind, the environment is released and **each captured field is released by its own
  discipline** (the §7 composition rule) — strings and containers decref, a captured `ptr<T>` box
  is freed, a captured Class-4 resource is closed, scalars do nothing. Exactly once.
- **Copy is forbidden.** `{ copy: c }` and any assignment producing a second live handle is a
  `closure-copy-forbidden` error. `set: [c2, c]` is a **move** — `c` is moved-out afterwards.
  There is no closure `clone` (§8).
- **Parameter.** A by-value `closure<Sig>` parameter is a **move-in sink** — exactly as for
  `ptr<T>` and every move-only value (spec/61 §8); `lifetime: consumed` affirms it. To let a
  callee only *call* it, the parameter is **`ref<closure<Sig>>`** (a borrow). *For the
  "hand a body to a function" pattern, use `block<Sig>` — that is what it is for.*
- **Return.** A `closure<Sig>` return is a **move out**; `lifetime: producer` affirms it. The
  factory pattern is sound: the captures live in the heap environment, never in the factory's
  frame.
- **Containers.** `list<closure<Sig>>` / `map<K:closure<Sig>>` hold **move-only elements**:
  pushing moves in, the container releases them. So `{ copy: … }` of such a container is the
  existing §10 error "shallow copy of a move-only element", and `{ clone: … }` is an error too,
  because no closure clone exists.
- **Model / class field.** A model with a `closure<Sig>` field is **move-only** (spec/61 §7).
- **Early release.** `free: [c]` releases now and nulls the slot (spec/61 §9.1, spec/20); the
  scope drop then does nothing — no double free.
- **`persistent`** opts out of the scope drop where ownership escapes invisibly, with the same
  leak lint.
- **Partially built environments.** If a capture initialiser throws, the fields already
  initialised are released and the closure is never bound (spec/61 §9.3).

### 6.1 Two closure instances are independent

```yaml
- decl: [add3, closure<Adder>, { call: [make_adder, 3] }]
- decl: [add5, closure<Adder>, { call: [make_adder, 5] }]
# { call: [add3, 10] } == 13   { call: [add5, 10] } == 15   — separate environments
```

### 6.2 Closure mutation is per instance and private; block mutation is not

A `closure:` body may `set:` a captured name; the write goes to **that instance's** environment
field, **persists across calls**, and is **not** visible to the enclosing scope:

```yaml
- callable: { name: Next, return: i32 }
- decl:
    - next_id
    - closure<Next>
    - closure:
        capture: [[n, i32, 0]]           # a field with no counterpart outside
        body:
          - set: [n, { +: [n, 1] }]
          - return: n
# { call: [next_id] } → 1, 2, 3, …
```

A **`block:` body is the opposite**, and that is the point: `set:` on a captured name writes the
enclosing variable itself (§3, the `sum` example).

For a *closure* that must share mutable state, capture a `shared<Box>` by `value`: the incref
makes the enclosing scope and every closure that captured it co-owners of the same heap object, so
writes are mutually visible (spec/61 §6.2), with the §10 owning-cycle lint still watching. A
dedicated implicit capture box is not in this scope (§8).

### 6.3 Nesting

A `block:` or `closure:` body may contain either kind. A nested `block:` captures implicitly,
including the outer body's args and locals. A nested `closure:` must enumerate its captures as
always — and may **not** capture an enclosing `block` value (§4.3).

## 7. Native and engine callbacks

Neither kind is a C function pointer. A native callback API accepts them **only where that API's
own spec says so**; everywhere else `fn<…>` remains the only accepted callback type and passing a
block/closure is a `callable-not-transferable` error. Generated per-callable native trampolines
are explicitly not the mechanism — they do not carry to wasm32.

**`engine.run` is the designated first callable-capable API** (`spec/44_1_engine.md`), and
`block<Sig>` is the **recommended** form for it, because the frame that calls `engine.run` is
exactly the frame that stays alive for the whole loop:

```yaml
- callable: { name: Frame, args: [[dt, f32]] }

- def:
    name: main
    return: i32
    body:
      - decl: [app, AppState]              # ordinary local — no global needed
      - call:
          - engine.run
          - block:
              body:
                - call: [update, app, dt]  # `app` is the real local, `dt` from Frame
      - return: 0
```

- `engine.run` **borrows** the callable for the whole loop; ownership and drop timing are
  unchanged by the call. `ref<closure<Sig>>` is accepted as well, for a callable built elsewhere.
- The browser path is the **manual Asyncify loop** that `src/sys/engine_runtime.cpp` actually
  implements (not `set_main_loop_arg`). Asyncify preserves the whole suspended stack, so a
  `block`'s borrowed frame — and the app state in it — stays valid across suspension and
  resumption; neither suspension nor shutdown may release anything early.
- Native and wasm32 must run the same program with the same result.

This is also the answer to the `g_cosmos_app` global the analysis set out from: a `block` passed
to `engine.run` reaches the app state in the calling frame, with no global, no heap environment
and no ownership transfer.

Every further callback API is inventoried and converted **individually**; none becomes
callable-capable implicitly.

## 8. Not in this scope (rejected, not silently accepted)

- **implicit capture in a `closure:`** — a closure always enumerates (§4); implicit capture is the
  `block` kind's feature, and it is safe only because a block cannot escape,
- **an escaping block** — stored, returned, put in a container/field, or captured by a closure
  (§3.2),
- **non-local `break`/`continue`** out of a block body (§3.3),
- **`ref` captures / escaping stack references in a closure** (§4.3),
- **`fn<…>` ⇄ `block`/`closure`** assignment, conversion or cast, in either direction — including
  "a closure with an empty capture list is really an `fn`",
- **`callable:` names in `fn<…>`**, and any change to `fn<…>`'s notation (§11),
- **default argument values** in a `callable:` signature (§2),
- **closure `copy` / `clone`** — no closure is duplicated (§6),
- **a self-referential / recursive block or closure** — both are anonymous and cannot name
  themselves; recursion goes through a named `def`,
- **transfer across threads** — through `chan<T>`, a spawn argument, or any move-send (spec/61
  §5.3),
- **persistence and remote execution** — `db`/KV storage, `mirror` replication, `sys.bundle`
  serialisation (spec/29): that needs code identity plus a serialisable environment, not a raw
  process pointer, and is a separate project,
- **hot reload** (spec/30) — code pointer and environment must be reasoned about together; until
  that is specified, `version:` is not accepted and neither kind may be live across a module swap,
- **shared implicit capture boxes** — use `shared<T>` (§6.2).

**Static analysis note (spec/49).** A call through a block or closure is an **indirect** call and
therefore the same boundary for the effect and frame-context closures that `fn<…>` and
`ptr<Interface>` calls already are (spec/49 "Direct calls only"). Neither kind may be presented as
covered by those checks.

## 9. Diagnostics

| Diagnostic | Condition |
|---|---|
| `block-escapes` | a `block` value is returned, stored beyond its scope, put in a container / field / global, captured by a `closure:`, or sent across a boundary (§3.2) |
| `block-nonlocal-control` | `break`/`continue` inside a block body targeting a loop outside it (§3.3) |
| `callable-signature-mismatch` | parameter count/types or return type do not match the target signature — on assignment, argument passing, or call (§2) |
| `callable-args-required` | `args:` omitted where the target signature cannot be inferred from context (§3.1). ⚠ **Open for review:** every position in which AIML accepts a value today is typed (a `decl` carries a type, an argument has a parameter type, a `return:` has the function's return type), so this rule has no reachable violation and no probe — either drop it, or name the untyped position it is meant to catch |
| `callable-unknown-signature` | `block<X>` / `closure<X>` names no visible `callable:` declaration |
| `callable-not-transferable` | a block or closure reaches a thread, channel, KV/db, mirror, `sys.bundle`, or a native callback not declared callable-capable (§7, §8) |
| `closure-free-variable` | a `closure:` body uses a name that is not an arg, a body local, a capture, or a visible global/function — including an attempt at self-reference (§4) |
| `closure-capture-unknown` | a capture of form (a)/(b) names a variable that is not in scope |
| `closure-duplicate-capture` | a name appears twice in `capture`, or in both `capture` and `args` |
| `closure-borrowed-capture` | a `block`, a `ref<T>`, a non-owning view, or a direct element borrow is captured (§4.3) |
| `closure-move-required` | a `ptr<T>` or other move-only value is captured without `mode: move` |
| `closure-copy-forbidden` | `{ copy: c }` of a closure, or any assignment producing a second live handle (§6) |
| `closure-fn-mismatch` | assignment or cast between `fn<…>` and `block`/`closure` |

The existing spec/61 §10 diagnostics apply unchanged to closures and their captures:
**use-after-move** (a moved closure, or a capture source moved by `mode: move`),
**use-after-free** (after `free: [c]`), **double free**, **shallow `copy` of a move-only element**
(a container of closures), the **leak lint** (`persistent` that does not escape) and the
**owning-cycle lint** (a closure captured into the object that stores it is an owning cycle, and
is linted like any other).

Run time: calling a null closure raises `NullPointerException` (§5).

## 10. Acceptance criteria

`tests/closures/` plus the lifetime probes listed with it. In spec terms:

1. A `block` passed as the last argument is called repeatedly by the callee; its implicit captures
   **mutate the caller's locals** (§3).
2. `args:` omitted takes the parameter names from the signature; omitted where the type cannot be
   inferred is rejected (§3.1).
3. A `block` that escapes — returned, stored, pushed, captured by a closure — is rejected (§3.2).
4. `break`/`continue` across a block boundary is rejected; `throw` out of a block reaches the
   block's caller and unwinds correctly (§3.3).
5. Two closure instances with different captures stay independent (§6.1).
6. A closure returned from a factory keeps its captures valid after the factory returned (§6.1).
7. Captured strings, containers and models are released **exactly once** at scope exit, at
   overwrite, and on `throw`-unwind (§6).
8. A `ptr<T>` captured by `mode: move` leaves the original moved-out; later use is rejected (§4.2).
9. Capturing a `ref<T>`, a borrowed view or a `block` is rejected (§4.3).
10. A `closure:` body using an un-captured enclosing local is rejected (§4).
11. A signature mismatch and an `fn` ⇄ `closure` assignment are rejected; a null closure call
    raises instead of crashing (§5, §9).
12. `value` / `copy` / `clone` captures of a container give exactly the aliasing, shallow and deep
    behaviour of spec/61 §5.1 (§4.2).
13. Closure capture mutation persists per instance and is invisible outside; a captured
    `shared<T>` *is* mutually visible; block mutation writes the enclosing variable (§6.2).
14. Two `callable:` declarations with identical signatures are interchangeable (§2, structural).
15. Parameters and returns of block/closure calls follow the existing spec/61 §8 ABI (§5).
16. An engine callback reaches its app state with no app global; browser suspension and shutdown
    do not release it early (§7).
17. Native and wasm32 execution of the same example agree (§7).
18. Thread, KV, mirror and `sys.bundle` uses are rejected with a diagnostic (§8).
19. The existing `fn<…>` tests and the spec/61 lifetime suite stay green — `fn<…>` is unchanged
    (§1, §11).

## 11. Relationship to existing specs

| Spec | Relationship |
|---|---|
| `12_1_function_pointers` | **unchanged.** `fn<…>` keeps its inline `<param:return>` notation, its POD code-pointer representation and its ABI. `block`/`closure` are sibling types with a *different* (named) notation — a deliberate inconsistency, chosen over migrating ~60 `fn<…>` sites in 36 files. ⚠ Unrelated finding to fix there separately: the multi-parameter inline form **must be quoted** in a YAML flow sequence (`[sinkfn, 'fn<string,i32:i32>']`), because the comma otherwise splits the sequence; the spec never says so and its own examples (and `tests/test_fn_ptr.aiml:36`) are unquoted |
| `07_functions` | `args` / `return` / `body` of a `callable:` / `block:` / `closure:` are those of `def:`; `name` / `version` are not accepted on a `block:`/`closure:`, and defaults not in a `callable:` |
| `61_memory_management` | `closure<Sig>` is a **Class-4 unique resource**; `block<Sig>` is a **non-owning / borrowed view** (last row of the §2.1 table). §7 (composition), §8 (params/returns), §9 (qualifiers) and §10 (diagnostics) apply as written |
| `18_refs_and_pointers` / `20_free_operator` | `ref<closure<Sig>>` borrows; `free: [c]` releases early |
| `22_exceptions` | a `throw` out of either body propagates to that callable's caller; unwind releases a closure environment (§61 §9.3) |
| `44_1_engine` | `engine.run` is the first callable-capable API, with `block<Frame>` recommended (§7); the Asyncify loop in `src/sys/engine_runtime.cpp` is the behaviour to preserve |
| `49_static_analysis` | a block/closure call is an indirect call — the same analysis boundary as `fn<…>` and interface dispatch |
| `21_namespaces` | a `callable:` name is visible by the ordinary import-closure rules, like a `model:` name |
| `29_serialize` / `29_1_sys_bundle` / `30_hot_reload` / `26_concurrency` | explicitly **out of scope**, with a diagnostic rather than silent acceptance (§8) |
