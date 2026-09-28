# 57 — Function Cascades (schema-bound visitor)  [DRAFT / rough]

Status: **Design settled; implementation in progress.**
- Cascade-side cleanup (`opcode:` + list-of-lists nodes + id/member verification) —
  **implemented** (spec 44_7, 9rooms migrated).
- **M1a** (`- cascade_class:` pool: AST + parser + registration + wiring validation) —
  **implemented**; test `tests/test_cascade_class_parse.aiml`.
- **M1b** (`- class: renders:`/`rules:` + `Mask` parsing: AST + parser + light
  validation) — **implemented**; test `tests/test_cascade_class_rules.aiml`.
- **M2 slice 1** (static pass: per-opcode top-level path enumeration → αAβ context
  matcher (tail-match, selection, wildcard) → specificity + `weight` tiebreaker →
  coverage + carrier-chain type check) — **implemented** (`checkCascadeClassSemantics`).
- **M2 slice 2** (flatten nested cascades: recursive path enumeration branching per
  sub-opcode, sub-fields flatten too, cycle guard; type-level mask matching — a cascade
  name matches any of its sub-opcodes, an enum name any of its members; per-path
  diagnostics deduped) — **implemented**; test `tests/test_cascade_class_nested.aiml`
  (sub-field stage reachable only via one branch); negatives verified.
- **M3a** (executable codegen): `render(carrier, block) -> taker` is **synthesized** as a
  real class (per-opcode `switch` on `block[0]` → statically-resolved handler chain),
  injected + registered before the visitTopLevel loop (so `{new: Pool}` / `pool.render`
  resolve via forward-declared methods).  **Executable + tested**
  (`tests/test_cascade_class_render.aiml`).  Scope: non-nested cascades, **by-ref carrier**
  (list/map) — handlers mutate it in place, `render` calls them fire-and-forget.  This
  routes around a pre-existing compiler bug: `set var = <.method(container)` (reassigning a
  container returned from a sibling method) corrupts/crashes.  Value / type-transforming
  carriers need that bug fixed (or a different emit).
- **M3b** (`self.field`): the pool gains implicit `blk`/`idx` members + a `field() -> i32`
  method; `render` stores the block and sets `idx` before each handler, so handlers read
  the actual current element via `<.field()`.  **Executable + tested** (same test now
  reads real block values → `t7`/`t9`, not constants).
- **M3c** (runtime nested-cascade dispatch): synthesis unified to **flatten every path**
  (nested cascades branch per sub-opcode) and emit `render` as one guarded `if` per path —
  the guard checks `block[pos] == opcode-value` at each cascade-opcode position (`&&`
  right-nested since it is binary), so a nested branch's extra stages run only when its
  sub-opcode is present at runtime.  **Executable + tested**
  (`tests/test_cascade_class_render_nested.aiml`: `in_zone` runs an extra `ZoneType` stage,
  `player` does not; both read real block data).  All M1–M3 tests green, no regressions.
- **Compiler bug fixed** (`set container_param = <.method(...)` wrote a struct into an
  8-byte by-ref pointer slot → SIGSEGV): `visitSetStmt` now dereferences by-ref
  container targets before the struct write (`byref_set_deref`).  `render` therefore uses
  the design-faithful `carry = <.handler(carry)` (not fire-and-forget).  Regression test
  `tests/test_sibling_return_container.aiml`.
- **M4** (real-world proof): a Function Cascade renders ability text for a real-shaped
  `Effect` cascade (nested `Value` cascade with an i32 payload, two opcodes sharing field
  renderers, `enums.name` reflection, `self.field`) — output **exactly equals** a
  hand-written reference over a block corpus.  Test `tests/test_cascade_class_ability_text.aiml`.
  This is the machinery that replaces the 1174-line opcode-switch `ability_text`.
- **M5** (scalable recursive dispatch) — **REPLACES the M3c static-flatten emit.**  M3c
  enumerated **every flat schema path** (nested cascades = cartesian product) and emitted
  **one guarded `if` per path**.  This is O(∏ nested-branch-factors): fine for FLAT
  cascades (Cost, Trigger) but **explodes** for deeply-nested ones — the 9rooms `Effect`
  (OwnerRef **in** Value **in** Duration) hits ~tens-of-thousands of paths → the compiler
  runs for minutes and blows up RAM (crashed the IDE twice).  **M5 = recursive per-cascade
  dispatch:** emit **one shared `dispatch_<Cascade>(carry)` method per (cascade, context)**
  that switches on `block[<.idx>]` — **one branch per NODE** — and at a nested-cascade
  field **calls** `dispatch_<Sub>` (recursion, NOT inlining).  Cost = **O(Σ nodes)**, not
  O(∏): `Effect` ≈ ~60 branches instead of tens of thousands.  Runtime dispatch (value →
  branch/table) mirrors the intended polymorphic design (ch. 12/30 fptable/vtable); the
  per-node `if`-chain is the simplest form (a fptable/vtable jump is an optimisation).
  **Handler-internal `if`s stay** (verb plurality, alias-vs-enum, finalize) — those are
  O(nodes), legitimate.  Context (`before`/`after`) resolution uses **type-token prefixes**
  (a field contributes its type name, an opcode its node name), so `before:[modify_stat,
  OwnerRef]` matches regardless of the owner node's structural length — this also **removes
  the `all_in_zone` field-shift class of bug by construction**.  Immediate-parent context
  is preserved (the recursion carries it → distinct memoised dispatch variant); only deep
  cross-nesting-boundary context is out of scope (see limitations).  **Both** the emit
  (`synthesizeCascadeClasses`) **and** the semantic check (`checkCascadeClassSemantics`,
  which also flattened) move to this recursion.  Flat cascades degenerate to one level →
  identical output (Cost/Trigger + `test_cascade_class_*` unchanged).

Known limitations / next steps:
- **Phrasing order**: the walk appends per block element → text is in *block* order.  The
  legacy generator freely reorders (value before target).  Exact legacy phrasing needs a
  **slot/struct carrier** (handlers fill named slots, a finalizer assembles) — now
  unblocked by the container-set bug fix; a value(model)-carrier variant is the path.
- **Pool type in signatures**: the synthesized pool class is usable via `{new:}` +
  method calls in a body, but NOT as a declared parameter/return type in another
  function's signature (it's synthesized after `registerTypes`).  Moving synthesis into
  `registerTypes` would lift this.
- Cross-nesting-boundary `before`/`after` discriminating on a sibling field's runtime
  sub-opcode value (rare) — current matching is type-level per path.
- Type-transforming carriers (per-stage carrier type change) need a multi-typed carrier,
  not one `carry` variable.
Example sketch: `tests/_proposals/function_cascade_ability_text.aiml`.

## 1. Motivation

Bytecode blocks (e.g. the 9rooms `Effect` cascade `[opcode, field…]`) are today
processed via **hand-written opcode switches**:

```yaml
- if: [{ eq: [code, 1] },  … ]   # draw_card
- if: [{ eq: [code, 10] }, … ]   # deal_damage
- if: [{ eq: [code, 12] }, … ]   # modify_stat
…
```

The same cascade is duplicated in several places: **text rendering**
(`ability_text.aiml`), **execution** (`EffectProcessor.execute_effect`), potentially
validation/serialization. Each is error-prone (a missing opcode = silent no-op) and
accesses fields positionally (`block[2]`), untyped.

The **cascade schema** (chapter "cascades", `- cascade:`) already knows the field
layout per opcode. Idea: **bind a class to a cascade** so that its methods process
the fields as a **typed function pipeline** — the compiler generates the dispatch
and checks the types.

## 2. Concept

A **`CascadeClass`** binds a cascade and bundles the rule-holding member classes that
render it (see §5b). A bytecode block handed to its `render` triggers a **pipeline** that
walks element by element. (Superseded refinement, see §5b: the final dispatch is
context-driven `α A β` typed by `Mask`, not the two-level opcode/field split below. Kept
here for the original intuition.)

- **Opcode handler**: element 0 (the opcode) starts the pipeline — picks the
  "sentence skeleton" resp. the execution entry.
- **Field handler**: handles an element by its **field type** (OwnerRef, TargetType,
  …), independent of the opcode. One handler per field type is shared by **all**
  opcodes whose layout contains that type.

"Assign each method a range of enumerators" (user wording) = the dispatch
assignment: a handler covers an enum value or range (resp. a whole field type).

## 3. The carrier-type invariant (core)

A **carrier** (accumulator) flows through the pipeline; the class declares its entry
type `carrier: <T>` and its exit type `taker: <T>`. The rule the user requires:

> **The return type of the element-i function == the carrier parameter of the element-(i+1) function.**
>
> (Current binding form: all stages share one carrier type, passed by reference — §3a.)

The carrier is **type-transforming (variant (b))**: every element function may change
the carried type; the compiler checks each seam. (A uniform accumulator is the special
case where all stages share one type — e.g. text: `carrier == taker == Draft`.)

The carrier only ever transforms **carrier → carrier**; there is **no** finalization
step inside the cascade. Any final rendering of the exit carrier (e.g. joining a `Draft`
to a `string`) is the **caller's** job — keeping the cascade concept clean.

The compiler verifies **per opcode variant** along its schema (all static — see §8):
- `carrier == carryIn(element 0)` (entry) — checked equal across all entry handlers
- for each element position i: `return(handler@i) == carryIn(handler@i+1)`
- terminal element (`after: []`): `return == taker` — checked equal across all terminals

Thus the chain is **statically type-continuous** — exactly the requested guarantee, and
`carrier`/`taker` being explicit lets the compiler check every signature seam.

### 3a. Carrier passing — by reference (binding)

The carrier is threaded **by reference**, never copied in and out of each handler. Every
element handler has the signature

```yaml
def: { name: f_mana, args: [[carry, ref<CostDraft>]], body: [ ... ] }   # no return
```

- The first parameter is `ref<carrier>` for a value-model carrier (`61_memory_management_DRAFT.md`
  §3: `ref<T>` is the tool for in-place mutation). A container carrier (`list`/`map`/`buffer`) is
  already a shared handle with reference semantics (spec/61 §5) and is taken as the plain
  container type (`[carry, list<string>]`). Either way the handler **mutates the carrier in
  place** and **returns nothing**.
- A handler that declares a return type, or takes the carrier by value, is a compile error.
  This is the cascade form of `49_static_analysis.md` §21 (no value round-trip through a call):
  copying the whole carrier into and out of every handler costs a copy per element and makes
  the ownership of the result ambiguous.
- The synthesized dispatch calls each handler as a plain call (`call: [<.handler, carry]`).
- The synthesized `render(carrier, block) -> taker` first takes an **independent copy** of the
  entry carrier into a local, runs the dispatch on that local, and returns it as `producer`.
  The caller's carrier is never mutated, and the taker is a fresh owned value.

Consequently `carrier` and `taker` must be the **same type**. A type-transforming chain (§3,
variant (b)) needs a multi-typed carrier and is rejected until that exists.

## 4. Runtime flow (proposed)

`renderer.render(carrier, block) -> taker`:
1. `code = block[0]` → pick `begin_<code>` → `acc = begin(carrier)`.
2. For `k = 1 .. schema_len(code)`: `ft = Schema(code)[k]`; the current field value is
   `block[k]` (typed as `ft`, read via `self.field`); `acc = handler@k(acc)`.
3. Return the exit carrier (`taker`) from the terminal handler; the **caller** does any
   final join/render.

The complete block is held as a ref in the class (`self.code`), together with the
current index (`self.index`). Advancing the index is **compiler-guaranteed** — the
programmer never manages it. Handlers read their field via `self.field` (typed per
the schema at `self.index`).

## 5. Why a compiler feature (vs. a library today)

Possible today (prototype): `map<i32 : fnptr>` dispatch + `cascade.field_indices` for
field positions + `enums.name` for enum→word. The feature adds on top: **declarative
assignment** (instead of manual registration), **automatic typed unpacking** of
`block[k]`, and the **static chain / completeness checks** from §3. Recommendation:
prototype first, then the feature as sugar.

## 5b. Problem: the same field type occurs multiple times in one opcode

Field-type dispatch (`on_field: OwnerRef`) breaks as soon as a type appears
**multiple times** in one opcode's layout:
- `[move_card, OwnerRef, TargetType, GameZone, GameZone]`  → GameZone 2×
- `[revive, OwnerRef, OwnerRef, GameZone, CardType]`       → OwnerRef 2×

(New node notation — see §6 Q9: `opcode: <Enum>` + list-of-lists nodes whose leading
element is the opcode member.)

The two occurrences mean different things (revive: OwnerRef#1 = who revives, #2 =
whose graveyard), and in the (b) pipeline they may require **different carry types**.
A single type-bound handler cannot serve both.

### CHOSEN APPROACH: context-driven dispatch (α A β), typed by `Mask`

Besides the current element `A`, each method states which **preceding history** α and
which **following history** β may surround it. The dispatch key is a context pattern —
a context-sensitive production `α A β`.

#### The `Mask` type

The context unit is a **`Mask`** — one per path stage. It "masks" which elements are
admissible at that stage:

```
model Mask {
  options:     list<i32>   # allowed member values of the stage-n enum; empty = any value of this stage
  is_wildcard: bool        # true = any element at all (options ignored)
}
```

A rule is three mask-parts plus a method — `on` is itself a `Mask`, so a handler can
grab several values at once (`on: {options:[deal_damage, heal_damage]}`) with the same
selection mechanic as the context, no special case:

```
- before: list<Mask>     # preceding history — the TAIL of the path prefix
  on:     Mask           # the handled element
  after:  list<Mask>     # following history — the HEAD of the path suffix
  def:    <method>
```

Surface YAML sugar → `Mask`:

| written | Mask |
|---|---|
| `deal_damage` / `[deal_damage, heal_damage]` | `options=[…], is_wildcard=false` (a **selection**) |
| a type name (`OwnerRef`) or omitted | `options=[], is_wildcard=false` (any value **of this stage**) |
| `"*"` (the QUOTED string — a bare `*` is a YAML alias marker) | `is_wildcard=true` |

#### Why `Mask` typing is exact — flattening

An Effect block is flat (`list<i32>`; nested cascades inlined). After flattening, every
path position is governed by exactly one enum — the (sub-)opcode enum of its stage:

```
Effect.deal_damage → [ deal_damage , ownerref-member , targettype-member , value-member , … ]
stage enum:           EffectCode     OwnerType          TargetType          ValueType
```

`Enum_n` = the governing enum at flattened stage `n`; `Mask.options` is a subset of its
members, stored as i32 values and **type-checked by the compiler against the stage-n
enum** from the schema (heterogeneous enum types across stages, homogeneous container).
Raw value-alias stages (`VALUE`/`COUNT`, i32, no enum) carry no members → only
`is_wildcard` applies in phase 1; numeric constraints (`min`/`max`/`range`) are a future
`Mask` field — the reason `Mask` is a struct, not a bare list.

#### Type-scoped mask matching (mask option → path token)

A `Mask.options` entry is a **type name** or a bare **member/node name**. Matching an
option `o` against a flattened path token `t` is **type-scoped** — it resolves against
the schema type at that stage, NOT by a global member-name search:

1. **Exact** — `o == t`. Covers a bare node name (`on: [fixed]`) and a **flat enum
   field**, whose token *is* the enum's type name (`on: TargetType` ↔ token `TargetType`).
2. **Cascade type** — `o` names a cascade and `t` is one of *that* cascade's node names
   (`on: OwnerRef` matches its `player`/`opponent`/… nodes).
3. **Opcode-enum of a cascade** — `o` names an enum and `t` is a node of a cascade **whose
   `opcode:` is `o`** (`on: OwnerType` still matches `OwnerRef`'s nodes, since `OwnerRef`'s
   opcode enum is `OwnerType`).

Crucially, an enum option matches a cascade **node** token **only** via rule 3 — *not*
merely because the enum happens to contain a member of the same name. This makes
**value- and name-identical subset enums safe**: e.g. 9rooms `PlayerRelation`
(`{any_player, player, opponent, …}`, values mirroring `OwnerType` for DB compatibility)
used as a flat field (`discard_card: [PlayerRelation, CardType, Value]`) does **not**
collide with `OwnerRef`'s `player`/`opponent` nodes. Without type-scoping the shared
member name made every `OwnerRef` node ambiguously claimed by `on: PlayerRelation` (→
"ambiguous handlers" error), and the generic opcode rule (`before:[] on:"*"`) leaked onto
the flat field so the **opcode was misread as the field value**. Regression:
`tests/test_cascade_type_scoped.aiml`.

#### Ordering (ordered sequence, Lesart B)

`before` is an **ordered** sequence matched against the **TAIL of the actual prefix**;
"further left is don't-care" is implicit (a shorter `before` = a weaker constraint), so
**no variable-length token** is needed — `is_wildcard` stays strictly "any single
element at THIS stage". `after` is symmetric against the **HEAD of the suffix**.
`after: []` = terminal (nothing follows) — a pure **match constraint**, not a
finalizer: the carrier flows out unchanged in kind (`carrier → carrier`). Any final
render of the exit carrier (`Draft → string`) is the **caller's** job, not the
cascade's.

```
# revive (flat): [revive, OwnerRef, OwnerRef, GameZone, CardType]
- { on: OwnerRef, before: [revive],          def: rev_who }    # stage 1 = reviver
- { on: OwnerRef, before: [revive, OwnerRef], def: rev_grave }  # stage 2 = graveyard owner
- { on: OwnerRef, before: ["*"],              def: add_owner }  # generic fallback
```

#### Validity invariant (grounds the grammar in the schema)

A rule's `before`/`after` must be **valid partial cascade paths** — exactly what
`cascade.validate` on the row truncated at the current step would accept. Formally, with
the per-opcode flat path `P_o = [o, t₁, …, tₙ]`: a rule `(before, on, after)` is
**well-formed** iff there is an opcode `o` and position `i` such that `type(P_o[i])` is
matched by `on`, `before` is a **suffix of** `[o, t₁, …, t_{i-1}]`, and `after` is a
**prefix of** `[t_{i+1}, …, tₙ]` (all modulo mask semantics). Consequences, now all
compiler-decidable:

- **Dead/impossible rules** are rejected: `before:[revive, GameZone]` — revive's stage 1
  is OwnerRef → no path matches → error (catches typos and thinkos).
- **Terminal validity**: `after:[]` only where some opcode actually ends → else error.
- **Exact completeness / overlap**: the valid contexts are finitely derivable from the
  schema, so the compiler enumerates every real `(prefix-tail, on, suffix-head)` and
  reports precisely which are uncovered — feeding the soft-coverage policy below.

#### Selection = list-of-lists, pruned by the invariant

Per-stage `options` sets nominally form a **cross-product** (`before:[[revive,transform],
[OwnerRef,CardType]]` → 4 combos), but combinations that are not valid schema prefixes
(e.g. `[revive, CardType]`) can never match a real path and drop out. So **per-stage
selection + the invariant = whole-sequence-disjunction semantics for free** — no separate
construct. A selection option that can never be schema-valid at its stage is a
warning/error (the invariant applied per option).

#### Determinism (specificity) and soft coverage

At a concrete position the rule with the **longest fixed** matching context wins; per
stage `singleton > k-element selection > wildcard`, and more constrained stages > fewer.
A genuine tie → `weight:` tiebreaker or error.

Coverage is **soft**: **type-validity is the hard filter** (only rules whose carry
in/out types still compose are candidates), specificity/weight only **rank** the
type-valid ones. A gap degrades to the best type-valid rule (wildcard / highest
`weight`) instead of erroring — but if **no** type-valid rule exists for a stage, that is
a hard error (no correct output possible).

### Class organization: `CascadeClass` bundles the method pool

A **`CascadeClass`** is the unit the compiler processes: it **bundles the rule-holding
classes** that together form one cascade's method pool, and declares the pool's carrier
endpoints. The compiler gathers **all rules from the member classes**, runs
path-enumeration + specificity + carrier-chain over the **union**, and emits the pool's
`render(carrier, block) -> taker`. This decouples code organization from the logical
pipeline. Two natural layers of member class:

- **Reusable per-field-type renderer** — the generic rules for one field type
  (`renders: OwnerRef`, context `["*"]`), written once and reused by every pool with such
  a field. A field that is itself a cascade (`Value`, `Duration`, `OwnerRef`) → its
  renderer IS that sub-cascade's own bundle, entered by **recursion** (see 6.5). A flat
  enum field (`TargetType`) → a renderer that `renders:` the **enum**.
- **Per-cascade skeleton** — the opcode "sentence begins" and the few **context-specific
  overrides** (revive#2, move_card from/to). Stays small because the bulk (generic field
  rendering) is factored out; specificity makes an override beat the generic type rule.

```yaml
# The pool — bundles the classes and pins the carrier endpoints:
- cascade_class:
    name: EffectText
    binds_cascade: Effect
    carrier: Draft
    taker:   Draft                # caller joins Draft -> string; no finalization inside
    members: [EffectSkeleton, OwnerRefText, ValueText, DurationText, TargetTypeText]

# A member = a reusable rule-bundle, bound to its own type:
- class:
    name: OwnerRefText
    renders: OwnerRef             # cascade OR enum
    rules:
      - { on: player,   def: { name: r_player,   args: [[c, Draft]], return: Draft, body: [ … ] } }
      - { on: opponent, def: { name: r_opponent, args: [[c, Draft]], return: Draft, body: [ … ] } }
```

Members are **reusable across pools** (the same `OwnerRefText` sits in the Effect,
Trigger and Value pools). A member's handler carrier types must compose within every pool
that includes it — checked in M2. Text ↔ execute = **one `CascadeClass` per purpose**
(different `carrier`/`taker`), sharing the schema. Classes stay cohesive and small
(AIML's ≤1499-line / small-function conventions fall out automatically) while the αAβ
power is retained — generic lives with the field type, the specific with the cascade.

**Superseded** (kept only for contrast): earlier options **(L1)** named-field dispatch,
**(L2)** (opcode, position) dispatch, **(L3)** field-type + `self.index` branching. All
are subsumed by αAβ + `Mask`: repetitions are distinct contexts (not names/indices),
sharing is wildcards/selections, and the carry-type check guides "share where types line
up, else split".

## 5c. Field kinds: enum vs. value/alias vs. cascade

Cascade fields are NOT only enums. The dispatch key (`on:`) and `self.field` are the
**schema type** of the element — of which there are three kinds:

| Kind | Example (Effect) | `self.field` type | Reflection | Phase-2 generation |
|------|------------------|-------------------|-----------|--------------------|
| **Enum** | `OwnerRef`, `TargetType`, `CardType` | the enum | `enums.name` → word | choose from **finite** member set |
| **Value/Alias** | `Value`,`Count`,`Sides` (i32 alias) | `i32` (or f32) | none — **raw number** | **generate a number** within alias bounds (`min`/`max`/`default`) |
| **Cascade** | `OwnerRef` with sub-nodes (`[all_in_zone, GameZone]`) | nested | — | recursive sub-dispatch (see 6.5) |

Consequences:
- **No `enums.name` on value fields**; value/enum are NOT combinable in one handler
  (different rendering/type). The compiler knows the kind from the schema and rejects
  misuse.
- **Alias metadata** (`min`/`max`/`default`/`label`, already in `- alias:`) is the
  constraint source: Phase 1 uses it for validation, Phase 2 as the generation range.
- **The Phase-2 policy has two "heads"**: classification (enum → pick from set) vs.
  regression/sampling (value → number in range). The compiler supplies, per field, the
  kind + the valid domain; the AI chooses/generates within it.

## 6. Open design questions

1. **Dispatch key**: always `block[0]`? Or configurable per cascade
   (`dispatch: <index>`)?
2. **Field vs. (opcode,position) dispatch**: is field-**type** dispatch enough (one
   `phrase_OwnerRef` for all), or are opcode-specific overrides needed (e.g. "OwnerRef
   reads differently in `revive`")? Proposal: field type as default, optional
   `on: [opcode, index]` override. (Largely resolved by the α A β approach in §5b.)
3. **Finalization** — RESOLVED: **out of scope for the cascade.** The pipeline only
   transforms `carrier → carrier`; rendering the exit carrier to a final form (e.g.
   `Draft → string`) is the **caller's** job. Keeping finalization out avoids polluting
   the concept with one use-case. So `carrier == taker` for text; they differ only under
   genuine carrier transformation (variant b).
4. **Variable/optional fields**: some opcodes have fewer fields (`counter_trap:
   [48]`). Pipeline simply ends after `schema_len`. OK.
5. **Nested cascades** — RESOLVED: a nested cascade is a **transparent continuation of
   the same carrier chain**, not a separate carrier/taker. The outer walk reaches the
   sub-cascade stage, reads its sub-opcode, dispatches into the **sub-cascade's own
   rules**, walks its fields, and returns to the position after them — the
   compiler-tracked index simply descends and re-ascends. The carry-chain check **flows
   through the boundary**: `carryOut(stage before) == carryIn(sub's first stage)` and
   `carryOut(sub's last stage) == carryIn(outer next stage)`; no adapter as long as the
   carry types line up (text: `Draft` throughout → seamless). A sub-binding's own
   `carrier`/`taker` apply only when it is invoked **top-level**; when nested, the
   boundary carry types must match (else an explicit adapter, or a compile error).
6. **Multiple blocks** — RESOLVED. A single Effect/Trigger/Cost code is **one**
   `list<i32>` (one opcode + its fields, nested cascades inlined). The multiplicity is
   at the **ability level** (an ability holds several effect codes → `list<list<i32>>`).
   So the function-cascade class processes **one** block; joining several is a thin outer
   loop (optionally a `render_all(list<list<i32>>)` convenience with separators), not part
   of the single-block pipeline.
7. **Text ↔ execute symmetry** — RESOLVED: same cascade, different `carrier`/`taker`
   (`Draft`/`Draft` vs `ptr<MatchSession>`/`void`). **One `CascadeClass` per purpose**;
   they share the **schema** (the real coupling), no forced common base. A shared
   interface only if a caller must swap them polymorphically (retrofit later).
8. **Reflection — enum labels** — RESOLVED (mostly existing infra): pretty display
   ("a creature" vs the identifier "creature") comes from the compiler's existing
   `enumLabels` (enum→id→label, context.h). Handlers reach it via reflection (an
   `enums.label(...)` builtin analogous to `enums.name`); per-member `label:` declaration
   syntax reuses the enum-label mechanism.
9. **The opcode element's type** — DONE (implemented, see spec 44_7 "Node declaration
   forms"). The cascade now names its opcode enum explicitly (`opcode: EffectCode`); the
   leading node element is an enum-member reference (`[deal_damage, …]` or
   `[EffectCode.deal_damage, …]`), the compiler resolves the id from it and **verifies**
   node id == enum value and name == member; `nodes` collapse to a plain **list of
   lists**. Legacy map form + raw ints stay accepted. This gives type-safe element-0
   dispatch (`on: {options:[deal_damage]}`, stage enum `EffectCode`). 9rooms
   `cascades.aiml` is migrated. Remaining tie-in: per-node `label` on the enum member
   (Q8).

## 6b. Target picture: AI programming

Two motivations for why this feature is a good fit for AI:

**(i) AI/LLM-generated code.** The productions `α A β → transform` are **local** and
**compiler-verified** (type chain, completeness, index walk). A model can emit
individual rules without tracking global state — bad composition, missing cases, or
type breaks are caught by the compiler. The declarative rule form is also easy to emit
and to review. This makes the cascade a **safe target for agents/codegen**.

**(ii) ML/tensor pipelines with shape checking.** A forward pass is a typed pipeline
in which **tensor shapes** transform stage by stage. Exactly the carry-chain check
(`return(i) == param(i+1)`) then becomes **static shape verification across layers**:
carry = tensor with shape, each production = a layer, context (`α/β`) expresses
layer-ordering rules ("Conv may only follow a 4D tensor"). Together with AIML tensors
(ch. 50) + ai_support (ch. 51), a potentially very strong tool for shape-safe models.

(To settle: which direction primarily drives — then we align carry types and the
`α/β` expressiveness to it.)

**Important — array vs. tensor typing (verified, ch. 50 + 52):**
- **`array<T:d0:d1:…>`** (ch. 52) carries element type AND shape **statically in the
  type** (`array<f32:4:4>`). → A **nominal** carry-chain check (our core) thus checks
  **shapes automatically**: `return array<f32:4:4>` ≠ `param array<f32:4:8>` is already
  a type error today. Shape safety for array pipelines is essentially **free**.
- **`tensor`** (ch. 50) is **f32-only** and **dynamically shaped**: the shape is
  runtime metadata (`t.shape(dim)`, `t.ndim()`), NOT in the type. The compiler does not
  know the shape statically. → Shape-safe tensor pipelines would need a new
  **statically-shaped tensor type** (`tensor<f32:64:128>`) + shape inference. That is
  the big extra effort.

**Extension ladder** for the carry:
`scalar (i32/f32)` → `array<T:…>` (shape already in the type, checkable today) →
`tensor<dtype:shape>` (new, needs shape inference).

## 6c. Weighting / fallback on non-exact match

When no exactly matching context (α A β) exists for a path: which rule resp. next
element to choose?

**Invariant (always holds):** type safety is a **hard filter**, weighting only a
**soft tiebreaker**. Only rules whose carry in/out types still compose are candidates;
the weighting merely ranks the type-valid ones.

Two levels:
- **(1) Static (compile time):** resolve overlap/partial match deterministically —
  **specificity** (longest fixed context > wildcard), optional `weight: N` as override.
  Completeness via a `["*"]` catch-all. Stays fully static + type-safe.
- **(2) Generative (runtime / AI):** the cascade as a **typed, weighted grammar** —
  among several type-valid continuations a weighting (or a policy/AI) chooses/samples
  the next element. The compiler provides the hard constraints (which continuations are
  type-correct), the weight chooses among them → **guaranteed type-correct** generation
  of sequences. Bigger lever, bigger effort.

Open: is (1) enough for the first goal, or is (2) — generate, not just process —
designed in from the start?

## 6d. Staging (phase 1 → phase 2)

The goal is **(2)** (generative, weighted grammar); the start is **(1)**. (1) is a
**subset** of (2), not a throwaway step — shared core: `α A β` rules, typed carry chain
(strictly nominal, `i32 ≠ i8`), type-as-hard-filter, `self.code`/`self.index`
mechanics.

- **Phase 1 — processing (static):** the compiler chooses among the type-valid
  candidates (specificity/`weight`) and generates the walk. Deterministic,
  compile-time safe. First use case: ability text (`ability_text.aiml`), later effect
  execution.
- **Phase 2 — generation (goal):** same rules; the compiler exposes at each point the
  set of type-valid next elements, and a policy/AI chooses/samples among them. Compiler
  = hard constraints, AI = free but guaranteed type-correct choice.

Carry extension ladder (orthogonal, see §6b): `scalar` → `array<T:…>` (shape checked
for free) → `tensor<dtype:shape>` (new).

## 7. Relationship to existing features

- `- cascade:` (schema + `cascade.validate`/`field_indices`) — provides the layout.
- `enums.name`/`enums.value` (ch. 14 reflection) — enum→string.
- Function pointers / fptable (ch. 30) — dispatch machinery (only needed for phase 2).
- Decorators (ch. 55) / meta (ch. 56) — possibly the mechanism through which the
  `before`/`on`/`after` rule annotations are realized.

## 8. Implementation plan — phase 1 (compiler, concrete)

Scope: **static, deterministic. No weights, no tensors.** Carrier = nominal types. First
goal: the `Effect` `CascadeClass` builds a `Draft` (`carrier == taker == Draft`); the
caller joins it to a `string`, replacing `ability_text.aiml`. Key simplification: since
rule resolution is **static** and each opcode's schema is **fixed**, per-opcode codegen
is a **straight-line handler call chain** — NO runtime dispatch / no fptable needed (that
only comes with phase 2 / generation).

Existing infrastructure we reuse:
- Schema already available: `ctx.cascades[name]` → `CascadeInfo.nodes[]` with
  `{name,id(opcode),fields[]}`, `fields[].typeName` (context.h:279ff, byId/byName).
- Classes/methods: `ctx.classes[name].methods` (`FunctionInfo`), `visitFunction`.
- Type comparison (for the chain check), enums/aliases, `enums.name`.

### M1 — AST + parser  (`ast_nodes.h`, `ast_builder.cpp`)
- New **`CascadeClass`** decl — the **method pool**. It bundles the rule-holding classes
  that together render one cascade: `{ name, binds_cascade OR binds_enum (string),
  carrier, taker (type names — the pool's entry/exit carrier), members: [class-names] }`.
  `<CascadeClass>.render(carrier, block) -> taker` is the generated entry point. Members
  are reusable across pools (e.g. `OwnerRefText` in the Effect, Trigger and Value pools).
- Extend `ClassDecl` with optional `renders: <cascade|enum>` + `rules` (vector) — a
  reusable **rule-bundle**. A rule's handler carrier types (its `def` arg/return) must
  compose within any pool that includes the class (checked in M2).
- `Mask` node: `{ options: vector<string> (member-names, resolved to i32 per stage
  enum), is_wildcard: bool }`. Surface sugar: a bare member/type token → `options`; a
  type name or omission → empty `options`; `"*"` → `is_wildcard`.
- New `Rule` node: `{ before: vector<Mask>, on: Mask, after: vector<Mask>, weight?: int,
  def: <FunctionDecl> }`. `before` matches the prefix TAIL (ordered), `after` the suffix
  HEAD; `after: []` = terminal (match constraint only, no finalization). `def` = existing
  method parsing.
- `ast_builder.cpp`: recognize `- cascade_class:` and the new `- class:` keys; build the
  `Mask` lists and rule bodies (via the existing `def` path).
- **Cascade side — DONE** (`opcode:` + list-of-lists nodes + enum-member leading element
  + id/name verification): implemented in `ast/cascade_builder.*` + `visitTopLevel(
  CascadeDecl)`; `CascadeInfo.opcode` in context.h; see spec 44_7.

### M2 — semantic model + static pass  (`context.h`, `ast_compiler.cpp`)
New `CascadeClassInfo { boundName (cascade or enum), carrierType, takerType,
vector<RuleInfo> }` (rules gathered from all `members`); `RuleInfo` = `{ before:
vector<Mask>, on: Mask, after: vector<Mask>, FunctionInfo*, carryIn, carryOut }` —
`Mask` = `{ options (resolved i32 member values), is_wildcard }`. New pass (after the
member classes' methods are compiled), per pool:
1. **Enumerate paths**: per opcode node a concrete path `[opcodeName, fieldType_1, …,
   fieldType_k]` from `node.fields[].typeName`.
2. **Resolve a rule per position**: for position i, filter rules whose `on` covers the
   element key AND whose `before`/`after` (with `*`) match the actual prefix/suffix.
   Overlap → **specificity** (longest fixed context wins); tie → error (resp. `weight`
   as tiebreaker, optional in phase 1).
3. **Completeness**: every position of every path → exactly one rule, else compile
   error "no handler for <opcode> element i (<type>)".
4. **Carrier-chain check (core)**: `carrier == carryIn(pos 0)` (equal across all entry
   handlers); ∀i `carryOut(i) == carryIn(i+1)` (strictly nominal, i32≠i8); terminal
   (`after:[]`) `carryOut == taker` (equal across all terminals). Reuse the existing type
   comparison.
5. **Field-kind check**: `self.field` at position i has the schema type
   (enum/alias/cascade); `enums.name` only on enum fields (else error).

### M3 — codegen  (`ast_compiler.cpp` / new emitter)
- Inject two implicit members into the pool: `code : ptr<list<i32>>` (block ref),
  `index : i32`.
- Generated method `<CascadeClass>.render(carrier, block) -> taker`:
  `self.code = &block; code0 = block[0];` → `switch(code0)` with, per opcode, the
  statically resolved chain: `self.index=0; c = begin@op(carrier); self.index=1;
  c = h@1(c); … ; self.index=k; return c;` (chain per opcode known at compile time → no
  runtime table). The exit carrier is returned as-is; the **caller** does any final
  render.
- `self.field` inside a handler → typed load `code[self.index]`, cast to the schema
  type at `self.index` (special `resolveProperty` for cascade-bound methods).
  `self.index` as a runtime field keeps handlers position-agnostic/shareable (the
  compiler sets it before each call).

### M4 — porting + tests
- Port `ability_text.aiml` (Effect) to the new syntax; diff the output against the old
  `generate_ability_text` over a block corpus.
- Negative tests: broken chain (return i32 → next expects i8) = compile error;
  completeness gap = compile error; `enums.name` on Value = error.

### Effort / order
**Start with the cascade datatype cleanup** (M1 cascade-side: `opcode:` + list-of-lists
nodes + enum-string leading element + verification). It is self-contained, fully
back-compatible (the legacy map form keeps working), and unblocks type-safe element-0
dispatch — a good, low-risk first step that also improves the existing cascade system on
its own. Then the M1 class-side (`CascadeClass` + `renders`/`rules`, `carrier`/`taker`).
Then
**M2 the bulk** (path enumeration + context matcher + specificity + chain type check) →
M3 (moderate) → M4. Deliberately NOT in phase 1: weight tiebreaker beyond specificity,
runtime dispatch/generation (fptable), tensor shapes.
