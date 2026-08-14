# 58 — Class Groups (static component wiring)  [DRAFT]

Status: **Design settled. M1 + M2 + M3 implemented + tested.**
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

Not yet: interface-typed edges combined with cross-group (`subgroup.inst:Iface`);
using a group type as a function-signature parameter/return (same synthesis-
timing limit as cascade_class pools).

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
        functions:                         # coordination methods added to THIS instance
          - def: { name: drive_ai_combat,        args: [[match_id, string]], body: [ ... ] }
          - def: { name: resolve_declared_spell, args: [ ... ],              body: [ ... ] }

      - class: CombatPriority
        instance: combat
        sees: [match_svc]
        functions:
          - def: { name: resolve_if_all_passed, args: [[req_email, string], [match_id, string]], body: [ ... ] }

    export: [match_svc, combat]            # instances visible to the outside / a parent group
```

Group-level keys: `name` (required), `params:` (§4), `subgroups:` (§6),
`export:` (§6), `instances:`.

Each `instances[]` entry names:

- `class`     — the base class type it specializes.
- `instance`  — the concrete, group-unique instance name.
- `sees`      — sibling instances it may reference (optional; default `[]`).  An
  entry is `inst` (concrete, base-typed), `subgroup.inst` (a subgroup's exported
  instance, §6), or `inst:Iface` (interface-typed edge, §5.2).
- `constructor` — a base-class method that initializes intrinsic state (optional,
  phase 0, sees-blind, §4).
- `functions` — coordination methods added to this instance (optional).

## 2. Desugaring — an instance *is* a synthesized subclass

Each entry lowers to a real subclass. `match_svc` above becomes exactly:

```yaml
- class:
    name: match_svc
    extends: MatchService
    members:
      - [cards,    ptr<CardService>]        # from sees
      - [effects,  ptr<EffectProcessor>]
      - [triggers, ptr<TriggerManager>]
    methods:
      - def: { name: drive_ai_combat,        ... }   # body references self.* (base) + cards/effects/triggers
      - def: { name: resolve_declared_spell, ... }
```

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

## 5. sees typing & cross-edge visibility

A sees edge binds the seeing instance to a sibling.  The edge member's **static
type** decides what is callable across it.  Two forms:

### 5.1 Concrete (default) — `sees: [log]`

The edge member is typed as the **base class** of the target instance
(`ptr<CombatLog>`).  Rationale: the base class is already registered when the
subclass is synthesized, so this resolves even for a `sees` cycle (address-only
wiring), and it keeps layering honest — only the sibling's **base API** is
callable across the edge (a sibling's group `functions` are not).  This is the
default and covers same-group and cross-group (`subgroup.instance`) edges.

**sees onto a base member.**  If a sees target's name matches a member the base
class **already declares** (e.g. `Consumer` has `dep: ptr<Dep>` and `sees: [dep]`),
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
  functions:
    - def: { name: resolve_top, return: i32, body: [ ... uses <.cards + base ... ] }

- class: CombatCore
  instance: combat
  sees: [match_svc:Resolver]      # calls <.match_svc.resolve_top across the edge
  functions:
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

## 7. Static checks (compiler-enforced)

- every `sees` target names an instance in the same group (or, for a dotted
  `subgroup.instance`, an **exported** instance of an embedded subgroup) —
  unknown/unexported target is a hard error;
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

- **Resolved** (§5): edges are base-typed by default; interface typing is opt-in
  per edge via `inst:Iface`.  (Instance-typing — calling a sibling's group
  `functions` across an edge — is intentionally *not* offered; promote such a
  method onto an interface instead.)
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
        functions:
          - def: { name: drive_ai_combat,        args: [[match_id, string]], body: [ ... cards / effects / triggers ... ] }
          - def: { name: resolve_declared_spell, args: [ ... ],              body: [ ... ] }

      - class: CombatPriority
        instance: combat
        sees: [match_svc]
        functions:
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
