# 58 — Class Groups (static component wiring)  [DRAFT]

Status: **Design settled. M1 + M2 + M3 implemented + tested.**
- **M2d** — subgroup **ports** (injected dependencies, §6.1): a subgroup declares
  the external instances it consumes via `requires: [[port, BaseClass]]`; the
  embedding parent **binds** each port to one of its own instances through the
  optional 3rd map element of a `subgroups:` entry
  (`subgroups: [[fx, EffectRuntime, { cards: cards }]]`).  A child instance may
  then name the port in `sees` — a **child→parent** reference, the inverse of the
  parent→child `subgroup.instance` edge, wired address-only in a deferred
  `__wire_ports` pass so a ported child's `on_wired` never observes an unbound
  port.  This lets a cohesive subsystem be its own subgroup that depends on a
  shared parent service (9rooms: an effect cluster needing `cards`) without a
  flat god-group.  Test: `tests/test_classgroup_subgroup_port.aiml`.
- **M3** — instance-level `implements: [Iface]`: a group instance may implement an
  interface satisfied by its base methods **and** its group `functions`, so a
  coordination function can implement an interface method and be **called across
  an interface-typed sees edge**.  The synthesized subclass carries the vtable, so
  such an edge upcasts the subclass directly (vs through the base when the base
  implements the interface).  This is what lets a coordinator drive a service
  across an edge with **no `svc` parameter and no import cycle**.  Test:
  `tests/test_classgroup_match_poc.aiml` — a faithful map of the 9rooms match
  subsystem (a `match_svc` whose `resolve_top` uses base state **and** a sees
  collaborator, implementing `Resolver`; a `combat` coordinator whose
  `resolve_all` loop calls `match_svc.resolve_top` across the edge).
- **M1** (parse `- classgroup:` → desugar each instance to a subclass of its
  base with `sees`→base-typed `ptr<>` members + `functions`→methods → container
  class with a synthesized `__ctor` doing phase-0 alloc + phase-1 sees back-patch,
  invoked by `visitNewExpr` on `{new: <group>}`; core static checks) —
  **implemented**.  Tests: `tests/test_classgroup_wiring.aiml`,
  `tests/test_classgroup_cycle.aiml` (mutual `sees` cycle wires via address-only
  back-patch), `tests/test_classgroup_neg_unknown_sees.aiml`.
- **M2a** — per-instance `constructor:` (a base method run in **phase 0**,
  sees-blind) + group-level `params:` (become container members, set at
  `{new: G, param: v}`, read by constructors).  Test: `tests/test_classgroup_ctor.aiml`.
- **M2b** — `subgroups:` (embedded child groups, owned + constructed by the
  parent in phase 0) with `export:` encapsulation; a parent instance may
  `sees` a child's **exported** instance via a dotted `subgroup.instance`
  target.  Tests: `tests/test_classgroup_subgroup.aiml` (nesting + ownership +
  external access), `tests/test_classgroup_subgroup_sees.aiml` (cross-group
  sees drives the child), `tests/test_classgroup_neg_unexported.aiml`.
- **M2c** — interface-typed `sees` edges via `inst:Iface` (§5.2): the edge member
  is `ptr<Iface>`, cross-edge calls dispatch through the interface vtable, and a
  mock is swapped purely by group config (a second group binding a different
  implementer).  Wiring upcasts through the concrete base (which carries the
  vtable).  Test: `tests/test_classgroup_iface.aiml`.

**Fixed 2026-10-09 — passing an instance to an interface-typed PARAMETER.**  An
interface-typed parameter (`ptr<Iface>` and `ref<Iface>` are the same
`AimlFatPtr_<Iface>` = {obj, vtable}) received a bare object pointer: the callee
then read the vtable from the wrong place and the first interface call
dereferenced null.  With a user-declared `- interface:` this **crashed**
(`jitRun`, addr=nil) for a plain class too — only builtin interfaces
(`mirror.RpcHandler`) worked, because they take a different path.  The upcast is
now emitted at the argument boundary of free functions AND methods, using a new
side-effect-free `declaredTypeOfExpr` so it also resolves **dotted** arguments
such as a group instance `grp.handler` (whose type lives in the container class's
member, not in a symbol).  This is what lets a coordinator hand a group instance
to a registry typed on the interface.  Test:
`tests/test_classgroup_iface_param.aiml` (free function, stored in a
`map<string:ref<Iface>>` and invoked through the vtable, and state changes behind
the sees edge showing through).

Not yet: interface-typed edges combined with cross-group (`subgroup.inst:Iface`);
using a group type **itself** as a function-signature parameter/return (same
synthesis-timing limit as cascade_class pools) — the interface type works.  A **group interface** (§12) is the way to pass a group as a value.

A `classgroup` is a purely declarative description of one architecture cluster:
which classes participate, which named instances exist, which sibling instances
each may reference (`sees`), and which coordination methods each instance adds.

The construct **desugars entirely onto existing AIML primitives** (`class …
extends`, members, methods). It introduces no ambient scope, no implicit
calling-convention parameter, and no hidden runtime call edges. The value is a
compact, compiler-validated subsystem topology that also removes the boilerplate
of passing service pointers around, of global singletons, and of source-level
import cycles between mutually-referencing subsystems.

## 1. Syntax

```yaml
- classgroup:
    name: MatchRuntime
    params: [[db_dir, path]]               # external config the group needs (optional)
    instances:
      - class: CardService
        instance: cards
        constructor: [init, db_dir]        # a base-class method; runs in phase 0

      - class: MatchService
        instance: match_svc
        sees: [cards, effects, triggers]   # sibling instances this one may reference
        constructor: [init_bare, db_dir]
        methods:                         # coordination methods added to THIS instance
          - def: { name: drive_ai_combat,        args: [[match_id, string]], body: [ ... ] }
          - def: { name: resolve_declared_spell, args: [ ... ],              body: [ ... ] }

      - class: CombatPriority
        instance: combat
        sees: [match_svc]
        methods:
          - def: { name: resolve_if_all_passed, args: [[req_email, string], [match_id, string]], body: [ ... ] }

    export: [match_svc, combat]            # instances visible to the outside / a parent group
```

Group-level keys: `name` (required), `params:` (§4), `subgroups:` (§6),
`export:` (§6), `implements:` (group interfaces, §12), `instances:`.

Each `instances[]` entry names:

- `class`     — the base class type it specializes.
- `instance`  — the concrete, group-unique instance name.
- `sees`      — sibling instances it may reference (optional; default `[]`).  An
  entry is `inst` (concrete, base-typed), `subgroup.inst` (a subgroup's exported
  instance, §6), or `inst:Iface` (interface-typed edge, §5.2).
- `constructor` — a base-class method that initializes intrinsic state (optional,
  phase 0, sees-blind, §4).
- `methods` — coordination methods added to this instance (optional).  Lifted onto
  the synthesized subclass, where the `sees` members live, so they have the full
  cross-edge API (§5.1).  (`functions` is accepted as a legacy alias.)

### 1.1 Splitting methods across files — `groupmethods:`

An instance's coordination `methods:` may live **outside** the group body — in its
own file — via a top-level `groupmethods:` block that names the target `group:` and,
per `instance:`, a `methods:` list:

```yaml
# phase_coordinator.aiml  (its own file)
- groupmethods:
    group: ServerRuntime          # target classgroup (defined elsewhere)
    instances:
      - instance: phaseco          # target instance
        methods:
          - def: { name: confirm_phase, ... }   # body uses <.matches (the sees member)
```

Before validation/synthesis the compiler folds every `groupmethods:` block into its
target `ClassGroupInstance` (`mergeGroupMethods`), so an externally-defined method is
**indistinguishable** from one written inline: it is lifted onto the synthesized
subclass with the same full cross-edge API (§5.1).  This keeps the group body lean
(just `class`/`instance`/`sees`) while each coordinator carries its own methods in its
own file.  An unknown `group:` or `instance:` is a hard error.

Visibility (`21_namespaces.md` §2.1): the methods are folded and emitted in the unit
that **defines** the group, so that unit must import every `groupmethods:` file of
its group (directly or transitively); a block outside its closure is not folded.  The
`groupmethods:` file itself does **not** import the defining file — that would be an
import cycle, and its own unit never resolves the `group:` name.  It imports only
what its method bodies use (models, helpers, the instance base classes).

## 2. Desugaring — an instance *is* a synthesized subclass

Each entry lowers to a real subclass. `match_svc` above becomes exactly:

```yaml
- class:
    name: MatchRuntime_match_svc
    extends: MatchService
    members:                                 # ALL from sees — compiler-generated,
      - [cards,    ptr<MatchRuntime_cards>]   # typed as the sibling's SUBCLASS
      - [effects,  ptr<MatchRuntime_effects>] # (inheritor) → full API across the edge
      - [triggers, ptr<MatchRuntime_triggers>]
    methods:
      - def: { name: drive_ai_combat,        ... }   # a group function: body references
      - def: { name: resolve_declared_spell, ... }   # self.* (base) + cards/effects/triggers
```

You never write this — the compiler synthesizes it from `sees:` + `methods:`,
and `__ctor` instantiates + wires every member.  (The member type is the
sibling's subclass, not its base — §5.1.)

Therefore:

- `sees` targets become ordinary `ptr<T>` **members** (composition, not inheritance).
- `functions` become ordinary **methods**.
- Inside a coordination method, a bare `cards` is a normal member access — there
  is **no ambient scope and no "spooky action at a distance."** The group is
  declarative configuration; the lowered class is fully explicit and can be
  printed by tooling.
- Single `extends` only. `sees` is composition, so there is no multiple
  inheritance and no diamond.

An instance is a *named architectural role/singleton*, not a bulk object: N wired
instances produce N subclasses. Bulk objects keep instantiating the base class
directly.

## 3. Construction model — two phases, order-free

The group is itself a synthesized container class. Instantiating it
(`{new: MatchRuntime, db_dir}`) runs:

- **Phase 0 — base construction.** Allocate every instance and run its nominated
  `constructor` (a base-class method) to initialize intrinsic state. A base
  constructor is **sees-blind by construction**: the base class has no sees
  members, so it *cannot* reference a sibling. Phase 0 is therefore per-instance
  independent — no ordering, no cross-instance dependency.
- **Phase 1 — sees wiring.** Back-patch each instance's sees members with sibling
  addresses. Every address exists after phase 0, and a `ptr<T>` needs only an
  address — never an initialized sibling.

Consequences:

- **No topological ordering is ever required** — not in phase 0 (independent) and
  not in phase 1 (flat address-only back-patch).
- **Cycles are a non-issue.** `A sees B` and `B sees A` wire fine, because wiring
  is address-only and no constructor can touch a sibling.
- Coordination that needs both self *and* siblings lives in **group-functions**
  and runs at **runtime**, on demand, after the group is fully built — never
  during construction.

**Post-wire hook (`on_wired`).**  A group-function named `on_wired` is called by
`__ctor` in a **phase 2**, after every sees is back-patched — so it *may* use
sees.  This is the home for init that intertwines intrinsic setup with a
collaborator (e.g. `reaction_window.init(triggers, costs, effects, <.cards)`),
which the sees-blind phase-0 constructor cannot do.  It is the automated form of
the explicit `start()`.  Phase 2 runs for all instances only after all phase-1
wiring completes (a subgroup's own `on_wired` runs inside its nested `__ctor`, so
children are fully post-wired before the parent's phases 1–2).

## 4. Constructors

`constructor: [<base_method>, <arg>, ...]` names a base-class method run in phase
0. It is sees-blind. Arguments come only from the group's `params` and literals —
**a sees target is never a constructor argument** (sees are wired in phase 1). If
`constructor` is omitted, the instance uses the base's default/zero-init.

An argument that names a group `param` becomes `<.param`; anything else is a
**literal**.  Numbers, `true`/`false` and qualified names (enum members such as
`Kind.file`, and floats) pass through verbatim; every other scalar is emitted as
a **string literal**.  That last step was missing until 2026-10-09: the YAML
parse drops a string's quotes, so `constructor: [init, "cas"]` emitted a bare
`cas` into the synthesized `__ctor` and the compiler read it as a type name
(`Unknown Type: 'cas'`) — numeric literals worked by accident.  Test:
`tests/test_classgroup_ctor_literal.aiml`.

## 5. sees typing & cross-edge visibility

A sees edge binds the seeing instance to a sibling.  The edge member's **static
type** decides what is callable across it.  Two forms:

### 5.1 Concrete (default) — `sees: [log]`

The edge member is typed as the target instance's synthesized **subclass**
(the *inheritor*, `ptr<CombatRuntime_log>`) — the exact instance the group
constructs.  So the edge exposes the sibling's **full API**: its base methods
**and** its group `functions`, reached by ordinary member resolution +
inheritance.  This means a coordination `methods:` method can drive a
sibling's coordination method across the edge **without** an interface-typed
edge (§5.2) and **without** the sibling declaring `implements:`.

> **A sees instance is a first-class collaborator — including its ATTRIBUTES.**
> The edge does not only expose methods: `<.dep.val` is **readable and writable**
> like a member of your own.  That is the difference between "collaboration" and
> "pass-through": without this property, every use of a neighbour would need
> either a getter/setter pair or an extra parameter in **every** signature along
> the way there.
>
> ```yaml
> - def: { name: peek_attr, return: i32,            body: [{ return: <.dep.val }] }
> - def: { name: poke_attr, args: [[nval, i32]],    body: [{ set: [<.dep.val, nval] }] }
> ```
>
> **Evidence:** `tests/test_classgroup_sees_base_member.aiml` reads `<.dep.val`
> across the edge, writes it, and then verifies the change **on the neighbouring
> instance itself** (`rtg.dep.get_v` → 7).  Reading alone would not be enough: a
> copy can be read without the write ever arriving.
>
> Practical consequence (user note 2026-10-14): whoever puts state into instances
> does **not** have to thread it through signatures.  A screen that sees `session`
> and `live` reaches `<.session.token` and `<.live.view` directly — that replaces
> a `ref<T>` borrow in every method of the call chain.

The subclass type is often not yet synthesized when the seeing subclass is built
(any `sees` cycle, e.g. `A sees B` and `B sees A`), so this relies on two
properties: (a) a `ptr<T>` is an **opaque** pointer whose layout is independent
of `T` — the pointee is resolved lazily at member-access, never at member
declaration; and (b) a synthesized sees-member is a wired **collaborator**, so
serialization **skips** it (§9) — which is what stops a bidirectional cycle from
recursing forever while the ser schema is built.  Wiring stays address-only, so
cycles need no diagnostic.  Covers same-group and cross-group
(`subgroup.instance`) edges.

**sees onto a base member (base-typed).**  If a sees target's name matches a
member the base class **already declares** (`Consumer` has `dep: ref<Dep>` and
`sees: [dep]`), the classgroup wires that existing member instead of synthesizing
a subclass-typed one.  That member keeps its declared (base) type, so this is the
path for ordinary **base methods** that use `<.dep` (a base method is compiled
against the base class and cannot see a subclass-synthesized member).  Use this
when the collaboration lives in base methods; use a synthesized subclass-typed
member (above) when it lives in group `functions`.

**The wired base member is a BORROW — declare it `ref<T>`, never `ptr<T>`.**  The
group owns its instances and releases them when the group is freed; a `ptr<T>`
member is a *unique owner* (spec/61 §5.2), so the same instance would be released
twice — a double free in `aiml_ser_free_decoded` at `free: <group>`.  Ownership is
carried by the **type**, and wiring does not change it (spec/61 §9): a collaborator
borrows.  The compiler rejects an owning `ptr<T>` member as a `sees` target and names
the `ref<T>` form in the diagnostic.  (Synthesized edges — the `sees` targets with no
matching base member — are generated as collaborators and are exempt: they are never
released and never serialized, §9.)

**sees onto a base member.**  If a sees target's name matches a member the base
class **already declares** (e.g. `Consumer` has `dep: ref<Dep>` and `sees: [dep]`),
the classgroup **wires that existing member** in phase 1 instead of synthesizing a
new one.  This lets ordinary **base methods** use the collaborator via `<.dep` —
the shape the real 9rooms `MatchService` needs (base methods use `<.cards_ref`
etc.) — while the classgroup supplies the wiring declaratively instead of a
constructor-injected pointer.

### 5.2 Interface-typed — `sees: [log:Logger]`

Suffix a sees target with `:Interface` to type the edge as an **interface fat
pointer** (`ptr<Logger>`).  The target instance's base class must `implement`
that interface.  Cross-edge calls then **dispatch through the interface vtable**,
so the edge depends on the abstraction, not the concrete class (dependency
inversion).

The two payoffs:

- **Polymorphism / DIP** — the coordination function calls interface methods; it
  no longer names a concrete collaborator.
- **Mock substitution by group config** — because each group synthesizes its own
  subclass and moves the same function source in, a second group can bind a
  *different* implementer (a mock) to the same instance name.  The interface
  guarantees both implementers satisfy the contract, so the identical
  coordination logic runs against either.  Example: a `ProdApp` group wires a
  `RealLogger`, a `TestApp` group a `MockLogger` — both `implement Logger`, both
  drive `svc`'s `run` unchanged.

**Lowering.** The edge member is `ptr<Interface>`.  Wiring upcasts the wired
instance to the interface *through its concrete base* — the base carries the
interface vtable, so the synthesized subclass does not re-declare `implements`:

```
- decl: [b_svc_logi,  ptr<RealLogger>, <.logi]     # view the instance as its base
- decl: [up_svc_logi, ptr<Logger>,     b_svc_logi] # decl-upcast base → interface fat ptr
- set:  [w_svc.logi,  up_svc_logi]                 # store into the ptr<Logger> slot
```

**Cross-edge coordination (instance `implements`).**  A sibling's group
`functions` are not on its base API, so to call one across an edge, the sibling
**instance declares** the interface it satisfies:

```yaml
- class: MatchCore
  instance: match_svc
  sees: [cards]
  implements: [Resolver]          # satisfied by the group-function below
  methods:
    - def: { name: resolve_top, return: i32, body: [ ... uses <.cards + base ... ] }

- class: CombatCore
  instance: combat
  sees: [match_svc:Resolver]      # calls <.match_svc.resolve_top across the edge
  methods:
    - def: { name: resolve_all, ... body: [ ... <.match_svc.resolve_top ... ] }
```

The interface method (`resolve_top`) is a group-function living on `match_svc`'s
synthesized subclass, so the subclass carries the `Resolver` vtable and the edge
upcasts the subclass directly.  This is the shape that removes a threaded `svc`
pointer and dissolves a `coordinator ↔ service` import cycle (§10).

Concrete and interface edges may be mixed freely on one instance.  (Interface
typing combined with a cross-group target — `subgroup.inst:Iface` — is not yet
supported.)

## 6. Subgroups — composition & encapsulation

`subgroups:` nest groups. A subgroup:

- is constructed and owned by its parent (nested lifetime);
- exposes only its `export:` instances to the parent and to sibling instances;
  everything else is private to the subgroup;
- may be referenced in a parent instance's `sees` via an `export`ed name.

This yields real architectural boundaries and avoids the flat "god-group": large
systems compose from small groups, each with an explicit surface.

Plain subgroup visibility is **one-way**: the parent references a child's exported
instance (`sees: [engine.counter]`), but a child instance cannot reach back into
the parent.  Ports (§6.1) add the inverse edge.

### 6.1 Ports — injected dependencies (child → parent)

A subgroup often depends on a service the **parent** owns (the 9rooms effect
cluster needs `cards`).  A subgroup declares such a dependency as a **port** and
the embedding parent **binds** it — a typed, validated injection point, not an
export.

```yaml
- classgroup:
    name: EffectRuntime
    requires: [[cards, CardSvc]]          # PORT: needs an external CardSvc instance
    instances:
      - class: EffectProc
        instance: effects
        sees: [cards, triggers]           # `cards` = the port (UP to parent); `triggers` = sibling
      - class: TriggerMgr
        instance: triggers
        sees: [cards]
    export: [effects, triggers]

- classgroup:
    name: ServerRT
    instances:
      - { class: CardSvc, instance: cards }
      - class: MatchSvc
        instance: matches
        sees: [fx.effects, fx.triggers]   # parent → child exported (already §6)
    subgroups: [[fx, EffectRuntime, { cards: cards }]]   # BIND port cards → parent instance cards
```

- **`requires: [[port, BaseClass]], …`** — group-level port declarations.  Each
  port is a name plus the base class an eligible binding must satisfy.
- **Binding** — a `subgroups:` entry may carry an optional 3rd element, a map
  `{ port: parentInstance, … }`.  Every port required by the child group must be
  bound exactly once, to an **instance of the embedding parent group** (not a
  grandchild, not an arbitrary member) whose class is the port's base class or a
  subclass of it.
- **`sees: [port]`** — inside the subgroup a port is a first-class `sees` target.
  Its edge member is typed as the port's declared **base** class (`ptr<CardSvc>`),
  a base-typed collaborator: it exposes the service's base API, deliberately not a
  child-specific subclass API.  Concrete same-group and cross-group edges keep
  their existing subclass typing (§5.1).

**Construction lifecycle.**  Ports wire address-only, like `sees`, but one phase
later than a plain subgroup because the bound parent instances must exist first.
The parent's `__ctor`:

1. constructs the child subgroups (phase 0, their own `__ctor` runs — but a ported
   child **defers** its `on_wired`, see below);
2. allocates and constructs the parent's own instances (so every binding source
   now has an address);
3. assigns each configured binding into the child container's port member and
   calls the child's synthesized **`__wire_ports`** helper, which back-patches
   every ported child instance's port member and *then* runs that child's
   deferred `on_wired` hooks — so a hook never observes an unbound port;
4. performs the parent's own phase-1 `sees` wiring and phase-2 `on_wired`.

A group **without** ports is unchanged: its `on_wired` still runs inside its own
`__ctor` (§3).  The deferral applies only to a group that declares `requires:`.

## 7. Static checks (compiler-enforced)

- every `sees` target names an instance in the same group, a declared **port**
  of the same group (§6.1), or, for a dotted `subgroup.instance`, an **exported**
  instance of an embedded subgroup — unknown/unexported target is a hard error;
- each `requires:` port name is non-empty, unique across the instance/subgroup/
  port namespace, and names a known base class;
- each subgroup port **binding** names a real port of the child group, is supplied
  at most once, binds to an **instance of the embedding parent group** whose class
  satisfies the port's base type, and every child port is bound (no unmet
  dependency);
- for an interface-typed edge `inst:Iface`, `Iface` must be a declared interface
  (its base implementing it is enforced by the upcast at wiring);
- `instance` (and `subgroups` member) names are unique within a group;
- `class`, `constructor`, and each `functions` name resolve against the base; a
  `subgroups` entry names a known group; `export` names real instances;
- a `functions[]` def MUST NOT override a base method — name collision is a hard
  error (additive only, per design decision);
- self-reference in `sees` is an error; duplicate `sees` targets are an error;
- cycles in `sees` are allowed and require no diagnostic (wiring is address-only).

## 8. Non-goals

- No implicit "instantiate everything globally" — a group is instantiated
  explicitly, like any object.
- No hidden runtime call edges from `sees`: it grants *permission to reference* a
  sibling, nothing more (no construction order, no ownership transfer, no
  mandatory call).
- No cross-file graph resolution beyond subgroup composition in the first cut.

## 9. Open questions

- **Resolved** (§5.1): concrete edges are typed as the sibling's **subclass**
  (inheritor), so a coordination method calls the sibling's base API *and* its
  group `methods:` across the edge — the full API, no interface required.
  Interface typing remains available as an opt-in per edge via `inst:Iface` (§5.2)
  when a narrowed contract or a swappable implementation is wanted.
- **Resolved** (§11): container + subclass types are visible in importing units
  (import-time header synthesis); coordination methods are callable cross-unit via
  the container (`g_runtime.<instance>.<method>`), retiring the per-instance `g_*`
  global bridge.
- Interface typing combined with a cross-group target (`subgroup.inst:Iface`) is
  not yet supported.
- May a `functions[]` def be marked private to the instance vs part of its public
  surface?
- Serialization: synthesized subclasses carry sees `ptr<>` members; confirm
  heap-ser treats them like any other `ptr<>` member (skip / re-inject on load),
  keeping the data-vs-collaborator boundary clean (a stored `Kv<T>` value never
  serializes its ambient collaborators).

## 10. Worked example — 9rooms match subsystem

**Before (today).** `MatchService.init(db, lobbies, dsets, decks, cards)`; free
functions thread a `svc` pointer everywhere (`resolve_if_all_passed(svc, email,
id)`); a `g_matches` global; a `combat_priority ↔ match_service` *import* cycle
that forces a thin `resolve_declared_spell` wrapper (private `cards_ref`/`effects`
are unreachable across the edge); `match_service.aiml` pinned at the 1499-line
limit because coordination logic must live in the base file.

**After.**

```yaml
- classgroup:
    name: MatchRuntime
    params: [[db_dir, path]]
    instances:
      - { class: CardService,     instance: cards,   constructor: [init, db_dir] }
      - { class: DeckService,     instance: decks,   constructor: [init, db_dir] }
      - { class: DeckSetService,  instance: dsets,   constructor: [init, db_dir] }
      - { class: LobbyService,    instance: lobbies, sees: [dsets], constructor: [init, db_dir] }
      - { class: EffectProcessor, instance: effects, constructor: [init] }
      - { class: TriggerManager,  instance: triggers, constructor: [init] }

      - class: MatchService
        instance: match_svc
        sees: [cards, effects, triggers, lobbies, decks, dsets]
        constructor: [init_bare, db_dir]
        methods:
          - def: { name: drive_ai_combat,        args: [[match_id, string]], body: [ ... cards / effects / triggers ... ] }
          - def: { name: resolve_declared_spell, args: [ ... ],              body: [ ... ] }

      - class: CombatPriority
        instance: combat
        sees: [match_svc]
        methods:
          - def:
              name: resolve_if_all_passed
              args: [[req_email, string], [match_id, string]]
              body: [ ... match_svc.match_get(match_id) ... ]   # no svc param

    export: [match_svc, combat]
```

Result:

- the `svc` parameter disappears — coordination methods read injected members;
- `g_matches` is replaced by `rt.match_svc` on the constructed group;
- the import cycle dissolves — the instance graph is acyclic, and even a cyclic
  one would wire fine (address-only);
- coordination logic moves out of `match_service.aiml` into the group file,
  relieving the line limit;
- the construction argument order that `MatchService.init` fixes by hand is now
  derived and validated from `sees`.

## 11. Cross-unit visibility & the runtime-root (`g_runtime`) pattern

### 11.1 The model

A classgroup desugars to synthesized **types** — one `Group_<instance>` subclass per
instance plus a `Group` **container** class (§2).  These types are ordinary classes
and are **visible in every unit that (transitively) imports the file defining the
classgroup**, exactly like a hand-written `class`.  A handler in another file may
therefore write:

```yaml
- decl: [g_runtime, ptr<ServerRuntime>]        # the container type, imported
...
- call: [g_runtime.match_svc.play_card, pid, mid, card, zone]   # base API
- call: [g_runtime.phaseco.confirm_phase, pid, mid]             # a group method
```

`g_runtime.<instance>` is the instance's **subclass** pointer, so the call has the
instance's **full API** — its base methods *and* its group `methods:` (§5.1) — across
unit boundaries.

### 11.2 Linkage — one body, many forward-decls

The synthesized method bodies (`__ctor`, each subclass's group `methods:`) are
**emitted once**, in the unit that *defines* the classgroup (e.g. `server_state`).
Every importing unit re-runs classgroup synthesis in **header-only** form during
import processing: it registers the container + subclass **types** and
**forward-declares** their methods, then links to the defining unit's emitted
bodies — the same one-definition rule that governs a regular imported class and its
methods.  The synthesized layout is deterministic, so every unit builds a
structurally identical container/subclass and the pointers are ABI-compatible.

`{new: <Group>}` (which runs `__ctor`) is normally issued only in the defining unit
(e.g. `init_server_state`); importing units hold the resulting pointer via the
container global and never re-construct it.

### 11.3 The runtime-root pattern — retiring the `g_*` bridge

Historically each instance was aliased to its own base-typed global
(`g_matches: ptr<MatchService>`, `g_phaseco: ptr<PhaseCoordinator>`, …) because
only base-typed globals were reachable across units, and base-typed globals can
only call **base** methods.  That forced every coordination method to be a base
method with a hand-declared `members: [matches]` (redundant with `sees`).

With cross-unit container visibility (§11.1) the whole bridge collapses to **one**
global:

```yaml
# server_state — ONE global instead of a dozen g_* aliases
- decl: [g_runtime, ptr<ServerRuntime>]
...
- set: [g_runtime, { new: ServerRuntime, db_dir: db_dir }]

# any handler, any file — full API on every instance
- call: [g_runtime.matches.play_card, ...]
- call: [g_runtime.phaseco.confirm_phase, ...]
```

Each coordinator now needs neither its own `g_*` global, its aliasing line in
`init`, nor a `members:` declaration for what `sees` already grants — the sees
member is synthesized on the subclass (§5.1) and reached through
`g_runtime.<instance>`.

## 12. Group interfaces — `groupinterface:` (2026-10-28)

Status: **implemented 2026-10-28.**  `tests/classgroup_giface/run.sh`: 3 positive + 12 negative
tests green, valgrind (the usual procedure, `VALGRIND=1`) without memory errors and without leaks
from generated code.  Three fixes the feature needed, each with its own regression test:

- **An interface-typed member assigned from `{new:}`** stored the bare object pointer instead of
  the fat pointer; the first call through it jumped to address 0 (any interface, not only group
  interfaces).  Now upcast like the decl site — `tests/test_iface_member_set_new.aiml`.
- **An owning `ptr<Iface>` member was not released with its owner** (any interface): the class
  descriptor had no kind for it.  New descriptor kind `AIML_TD_IFACE` frees through the vtable's
  free slot; a classgroup container behind an interface runs its `__dtor` there.
- **`free: <group>` with a sees edge freed the sibling twice**: the instance class's own free
  descriptor listed its sees member (a borrow, §9) as owned.  Collaborators are now skipped there
  as in the other descriptor paths — `tests/test_classgroup_free_sees.aiml`.

A classgroup is a concrete type.  Whoever embeds it, or holds it, names the
implementation — and so depends on all of it.  A **group interface** is the
contract for a group's *outside*: which instances it exposes and which interface
each of them satisfies.  Code written against the contract works with every group
that implements it.

Three uses drive the design:

- **A stable package API.**  A package (e.g. `ufo`, imported as `@ufo/…`, spec/21
  §3.1) publishes a group interface; its users depend on that, not on the
  package's classes.  The package can rebuild its inside freely.
- **Swapping a whole subsystem.**  §5.2 swaps one collaborator by group config
  (real vs. mock logger).  A group interface does the same for a subsystem: a
  kontor-backed layer store, a local one, a mock — chosen in one place.
- **Groups as values.**  A group type cannot be a parameter or return type (§1,
  "Not yet").  A group *interface* can: it has a fixed layout (an interface fat
  pointer) independent of the synthesized container.

### 12.1 Syntax

```yaml
- interface: { name: Registry, methods: [[push, i32, [[ref_name, string]]]] }
- interface: { name: Codec,    methods: [[encode, string, [[data, string]]]] }

- groupinterface:
    name: LayerStore
    exports:                     # [instance name, interface it must satisfy]
      - [registry, Registry]
      - [codec, Codec]

- classgroup:
    name: KontorStore
    implements: [LayerStore]     # group level — instances keep their own `implements:`
    instances:
      - { class: HttpRegistry, instance: registry }      # HttpRegistry implements Registry
      - { class: TarCodec,     instance: codec }
      - { class: Cache,        instance: cache }         # not in the contract: still fine
    export: [registry, codec, cache]
```

- `groupinterface:` keys: `name` (required), `exports` (required, non-empty): a list
  of `[instance, Interface]` pairs.  `level:` (spec/65) is accepted as on any type.
- An export is typed by an **interface**, never a class: the contract names an
  abstraction (that is the point), and an interface edge is what §5.2 already
  wires and dispatches.
- `classgroup` gains a group-level `implements: [GroupIface, …]`.  It is distinct
  from the instance-level `implements:` (§5.2), which stays as it is.

### 12.2 Using a group interface

A group interface is a type like an interface: `ptr<G>` owns, `ref<G>` borrows
(spec/61 §2).  It may be a local, a class member, a parameter, a return type, a
list/map element.

```yaml
- def:
    name: publish
    args: [[store, ref<LayerStore>], [what, string]]
    return: i32
    body:
      - decl: [packed, string, { call: [store.codec.encode, what] }]   # through the contract
      - return: { call: [store.registry.push, packed] }

- def:
    name: main
    return: i32
    body:
      - decl: [store, ptr<LayerStore>, { new: KontorStore }]   # build, then view as the contract
      - decl: [rc, i32, { call: [publish, store, "layer"] }]
      - decl: [reg, ref<Registry>, store.registry]             # an export as a value: a borrow
      - return: rc                                             # dropping `store` frees the group
```

- **`{new: Impl}` and a variable of an implementing group convert to `ptr<G>` /
  `ref<G>`** wherever an interface conversion happens today (decl, argument,
  member store, return) — the container *is* an implementer of `G`.
- **`x.<export>.<method>(…)`** calls the export's interface method;
  **`x.<export>`** alone is a `ref<Iface>` — a borrow of the instance the group
  owns, never an owner.  Both work on locals, parameters and members
  (`<.store.registry.push`).
- Only the contract is reachable: `x.cache` is an error even though the concrete
  group has it.  Code that needs the full API holds the concrete type
  (`ptr<KontorStore>`), where everything is as before (§5.1).
- **Ownership**: `ptr<G>` owns the group exactly as `ptr<KontorStore>` would;
  dropping it runs the container's `__dtor` (all instances, subgroups).  `ref<G>`
  borrows.

### 12.3 Lowering

`groupinterface: G` desugars, at parse time, to an ordinary interface:

```yaml
- interface:
    name: LayerStore
    methods:
      - [__gi_registry, ref<Registry>, []]
      - [__gi_codec,    ref<Codec>,    []]
```

A group with `implements: [G]` adds `G` to its **container's** interfaces and
synthesizes one accessor per export:

```yaml
- def:                                    # on the container class KontorStore
    name: __gi_registry
    return: ref<Registry>
    body:
      - decl: [gi_view, ref<KontorStore_registry>, <.registry]
      - decl: [gi_up,   ref<Registry>, gi_view]          # upcast (base or instance vtable, §5.2)
      - return: gi_up
```

and the consumer's `x.registry…` compiles as `x.__gi_registry()…`.  Everything
else — registration, the vtable, the upcast at decl/argument/return, ownership of
`ptr<G>`, cross-unit headers (§11) — is the existing interface machinery.  The
upcast in the accessor goes through the instance's subclass when the *instance*
declares `implements: [Iface]` (the vtable lives there, §5.2), else through its
base class.

### 12.4 Static checks (exact messages)

On the group interface:

- **GI-1** an export names no interface:
  `groupinterface 'G': export 'r' names unknown interface 'X'.`
- **GI-2** an export is typed by a class:
  `groupinterface 'G': export 'r' must be typed by an interface, 'X' is a class.`
- **GI-3** an export is listed twice: `groupinterface 'G': export 'r' is listed twice.`
- **GI-4** no exports: `groupinterface 'G' exports nothing.`

On a classgroup that implements one:

- **GI-5** unknown name: `classgroup 'X': implements unknown group interface 'G'.`
- **GI-6** an ordinary interface named at group level:
  `classgroup 'X': 'G' is an interface, not a group interface — instance interfaces go on the instance (spec/58 §5.2).`
- **GI-7** the export is missing:
  `classgroup 'X' implements 'G' but has no instance 'r' (G exports r: Iface).`
- **GI-8** it exists but is not exported (an explicit `export:` list without it):
  `classgroup 'X' implements 'G' but does not export 'r'.`
- **GI-9** the instance does not satisfy the interface:
  `classgroup 'X' implements 'G': instance 'r' (class C) does not implement 'Iface'.`
- **GI-10** two implemented group interfaces disagree:
  `classgroup 'X' implements 'G' and 'H', which export 'r' as different interfaces ('A' vs 'B').`

On a use:

- **GI-11** not part of the contract:
  `'c' is not part of group interface 'G' (exports: codec, registry).`
- **GI-12** reaching past an export (an interface has methods, no fields):
  `'r' of group interface 'G' is an interface ('Iface'): call one of its methods, x.r.<method>.`

### 12.5 Cross-unit and packages

The lowered interface is registered like any other interface, so a group interface
declared in one file is usable in every file that imports it (§11): the package
declares `G` (and the interfaces it names) in one module, an implementation in
another; a user imports the first everywhere and the second only where it builds
the group (its composition root).

### 12.6 Not in this version

- **Ports and params in the contract.**  Whoever *builds* a group names the
  concrete type (`{new: KontorStore, …}`, a `subgroups:` entry) and binds its
  params/ports there; a holder of `ptr<G>` never does.  They join the contract
  together with the next item.
- **Generic subgroup slots** (`subgroups: [[store, LayerStore]]`, the
  implementation chosen by the embedding parent) and `sees: [store.registry]`
  through a group interface — that is `subgroup.inst:Iface` (§5.2, not yet).
- Group-interface inheritance (`extends:`), group-level methods in the contract.

### 12.7 Tests

| test | checks |
|---|---|
| `tests/test_classgroup_giface.aiml` | a real and a mock group behind one contract; free function on `ref<G>`; `ptr<G>` from `{new:}`; member `ptr<G>` (`<.store.registry.push`); `x.export` as a value; the concrete type still has its full API |
| `tests/test_classgroup_giface_instance_impl.aiml` | the export is satisfied by an instance-level `implements:` (group function implements the interface method) |
| `tests/test_classgroup_giface_cross_unit.aiml` (+ `tests/_cross/giface_api.aiml`, `giface_impl.aiml`) | contract, implementation and user in three files |
| `tests/test_classgroup_neg_giface_*.aiml` | GI-1 … GI-12, one file each; `tests/classgroup_giface/run.sh` checks every `# EXPECT-ERROR:` text |
| valgrind | `test_classgroup_giface*` without invalid accesses; no leak from generated code (`VALGRIND=1 bash tests/classgroup_giface/run.sh`, probes in `tests/run_memory_sanitizers.sh`) |
