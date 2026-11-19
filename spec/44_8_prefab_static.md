# Static Prefab-Usage Checks

`- prefab:` declarations (see `44_6_prefabs.md`) are registered into the runtime
prefab registry at program start (`aiml_prefab_register`).  `engine.instantiate`
looks a prefab up **by name** and, if the name is unknown, silently returns a
null / empty instance — a whole subtree of UI simply never appears, with no error
at compile time and no crash at runtime.  This is the exact silent-failure class
the cascade-validation work (`44_7_cascade_validate.md`) set out to eliminate for
bytecode; this section does the analogous job for prefab *names*, but at
**compile time**, statically.

The compiler collects every declared prefab name during the type pre-pass
(`registerTypes`) into a global registry (`ctx.prefabDecls`).  Because imports are
resolved eagerly into the same compilation context, a module's registry contains
its own prefabs **and** every prefab declared in a (transitively) imported module.
A separate AST-walk pass then verifies each instantiation site against that
registry.

## Tier 1 — instantiate-known

For every call of the form

```yaml
{ call: [engine.instantiate, "<Name>"] }
```

where `"<Name>"` is a **string literal**, `<Name>` must be a declared prefab that
is visible in the current module (declared locally or in an imported module).  If
it is not, the compiler emits a **hard error** and compilation fails:

```
<loc>: engine.instantiate("Foo") refers to an unknown prefab — no `- prefab:`
with that name is declared or imported in this module. Declare it, fix the
name, or add the missing import.
```

Rationale: a wrong or un-imported name is never intentional — it produces an
invisible widget at runtime.  Catching it statically turns a silent no-op into a
build failure at the exact call site.

### Scope and limits

- Only **string-literal** names are checked.  A runtime string
  (`engine.instantiate(some_var)`) cannot be resolved statically and is left
  untouched — no error, no false positive.
- The check is **per-module** and import-aware: every module that instantiates a
  prefab is expected to import the module declaring it (already the established
  convention — e.g. every 9rooms client screen imports `client/prefabs.aiml`).
  A module that instantiates a prefab it never imports is itself the bug this
  check surfaces (missing import → runtime silent failure).
- The pass runs for both entry and library (`libraryMode`) modules, mirroring
  `checkStringComparisons`, so imported modules are checked too.

## Tier 2 — child-known

A prefab addresses its children **by name**.  The child-addressing methods

```
# schreibend — ein falscher Name WIRFT
set_text set_texture set_child_visible set_child_font_size set_dropdown_options
set_dropdown_index set_dropdown_selected set_compact set_bg_color set_border_color
set_color set_color_runs clear_color_runs set_checked set_focus set_bar_value
set_bar_max set_bar_fill set_slider_value set_slider_min set_slider_max focus
select_all scroll_to_end bind_list
# lesend — ein falscher Name liefert stumm einen Vorgabewert
child_clicked text_value child_focused is_checked dropdown_value dropdown_index
dropdown_enum get_slider_value selection container_row_count
```

This list is **every** `prefab_instance` method whose first argument the runtime
resolves through `find_child` (abgeglichen gegen `src/sys/prefab_runtime.cpp`,
2026-10-15).  Until then it named 6 of them, which left 24 methods carrying the
identical failure class with no check at all — found while giving a job row a
status dot, where `set_bg_color("stat_dot")` on a mistyped name would have thrown
at run time with nothing to catch it in WASM.  One list serves Tier 2 and Tier 2b:
two copies of the same rule drift apart (that is how the memV3 double-gate bug
happened).

take the child name as their first argument and resolve it through the runtime
`find_child` (`child_index` is populated from exactly the declared `children:`,
no synthetic entries).  A miss **throws** `std::runtime_error` with the prefab
name and the list of valid children (`AIML_PREFAB_LENIENT=1` downgrades it to a
warning + NULL).  Tier 2 moves that failure from run time to compile time.

**Why compile time is not a nicety here.**  The throw is survivable natively
(an aiml `try`/`catch` can hold it, otherwise `terminate` prints `what()` plus a
backtrace to the call site).  In a **WASM** build it is not: exceptions are
compiled out, so the throw reaches `___cxa_throw` and aborts the whole
application —

```
[prefab-EXCEPTION] prefab widget child 'logo_img' not found in prefab 'MainMenuScene'
Aborted(Assertion failed: Exception thrown, but exception catching is not enabled.)
```

— a black page at the first frame, with no aiml-level stack and nothing to
recover from.  One stale child name therefore takes down the entire client, so
for a WASM target the static check is not a convenience, it is the **only**
line of defence.  Measured in cosmos on 2026-10-15: a child removed from a
prefab while a `<.scene.set_texture("logo_img")` stayed behind in the owning
class — exactly the member-held shape Tier 2 did not cover (see Tier 2b).

Within each function body the compiler tracks which prefab every
`prefab_instance` variable was instantiated from, but **only** for direct
bindings whose initializer is literally `engine.instantiate("<Name>")`:

```yaml
- decl: [inst, prefab_instance, { call: [engine.instantiate, "CardToken"] }]
- call: [inst.set_text, "card_name", card.name]    # "card_name" ∈ CardToken.children → ok
- call: [inst.set_text, "card_naem", card.name]    # typo → hard error
```

For every `inst.<child-method>("<child>", …)` on such a tracked variable, if
`"<child>"` is a string literal that is not among that prefab's declared child
names, the compiler emits a hard error at the call site:

```
<loc>: 'inst'.set_text("card_naem") addresses an unknown child of prefab
'CardToken' — no child with that name is declared in its `children:` list.
Fix the child name or add the child to the prefab declaration.
```

### Scope and limits

- **Conservative binding tracking** — only variables bound *directly* to
  `engine.instantiate("<literal>")` are tracked.  An instance obtained from a
  helper (`spawn_card_token(...) → prefab_instance`), a field, a function
  parameter, or a runtime-computed prefab name is **not** tracked, so it is never
  falsely flagged.  Reassigning a tracked variable to anything else drops it from
  tracking.
- **Branch scoping** — a binding created inside an `if` / `while` / `try` branch
  does not leak past that branch (branches are analysed on a copy of the binding
  map); a binding made before the branch stays visible inside it.
- Only **string-literal** child names are checked; a runtime string is left
  untouched.
- Only simple `var.method(…)` receivers are considered (no chained receivers).

## Tier 2b — member-held instances, reading methods

Tier 2 as described above covers a scene in a **local variable**.  That is not how
screens are built: a screen instantiates its scene **once** in `init` and keeps it
in a class member, then addresses children from every other method.

```yaml
- set: [<.scene, { call: [engine.instantiate, "MainMenuScene"] }]   # init
- call: [<.scene.set_text, "jobs_box", txt]                         # render
```

So the whole production shape sat outside the check.  Tier 2b closes it along both
axes at once — the receiver and the method set:

1. **Member receivers.**  Per class, the compiler collects `set: [<.<field>,
   engine.instantiate("<Prefab>")]` bindings over **all** methods of that class
   (a field is bound in `init` and used in `render` — a per-method map would see
   nothing).  Every `<.<field>.<child-method>("<child>", …)` in that class is then
   checked against `<Prefab>`'s declared children, with the same hard error as
   Tier 2.
2. **Reading methods.**  `child_clicked` and `text_value` are checked, not just the
   writing `set_*` family.  These two are the click-critical ones: a typo in
   `child_clicked("probe_btn")` is a **silently dead button** and one in
   `text_value("host_input")` a **silently empty input field** — no exception, no
   message, because reading a missing child legitimately yields "not clicked" /
   "empty".  A dead button is the failure mode a user reports as "the button does
   nothing", which is also the hardest to find by reading code.

### Scope and limits

- **One prefab per field, or nothing.**  A field is tracked only if *every*
  literal-instantiate binding to it names the **same** prefab.  Two different
  prefabs in two branches (a field reused for two scenes) drops it from tracking.
- A field assigned from anything not statically resolvable (a helper, a parameter,
  another field) is simply not tracked — it never produces an error.  The check
  stays conservative: it reports only what it can prove.
- **Fields only, not chains.**  `<.state.menu.scene.set_text(…)` is not tracked:
  the receiver is another object's field, whose binding lives in another class.
- Only **string-literal** child names, as in Tier 2.

### Why a hard error and not a warning

A warning here would be ignorable, and this failure class is specifically *not*
ignorable: natively it throws at the call site, in WASM it aborts the program.
There is also no legitimate reason to address a child that is not declared — the
child list is fixed data in the same build.  The only false-positive risk is a
name the compiler cannot resolve, and those are excluded by construction above.

## Tier 3 — dead-prefab

A `- prefab:` that is declared but **never instantiated** anywhere in the build is
dead UI — cruft that bloats the prefab registry and misleads readers.  Tier 3
reports it as a **warning** (not an error: dead code does not break a build, and
a shared prefab file legitimately over-declares for some targets — see below).

Unlike Tiers 1–2, this is a **whole-program** property, and the compiler builds
each module in an isolated sub-compiler (its own `CompilerContext`).  So the
analysis piggybacks on the existing sub-compiler merge-back (the same path that
propagates `importedSourceFiles` and `linkedObjects` upward):

- Each module records, in its context, the prefab names it declares
  (`ctx.prefabDecls`) and the names it instantiates via a literal
  `engine.instantiate("<Name>")` (`ctx.prefabUses`).
- `resolveImport` folds every sub-compiler's `allPrefabDecls` / `allPrefabUses`
  into the parent's, so at the top level the two sets are unions over the whole
  (transitive) build graph.
- At the top-level (non-library) entry, every name in
  `allPrefabDecls \ allPrefabUses` is warned once.

### Scope and limits

- **Per build target.**  "Dead" means *not instantiated in this build's import
  graph*.  A project with several entry points (e.g. a `client` and a `menu`
  target) that share one `prefabs.aiml` will see prefabs used only by a *sibling*
  target reported here — correct for this build, but not project-wide dead.  The
  message says "in this build" for exactly that reason.  A prefab reported by
  **every** target is genuinely dead.
- **Complete-use-set gate.**  The warning is emitted only when the use-set is
  known complete: a real object/exe build (`noCache`, every import compiled
  fresh) or a lone single file (no imports).  A warm-cache multi-file JIT run can
  skip already-compiled imports, so Tier 3 stays silent there to avoid false
  "dead" reports.
- Only **string-literal** instantiations count as uses; a prefab reached only via
  a runtime-computed name would be falsely reported.  (In 9rooms every
  `engine.instantiate` uses a literal, so the use-set is exact.)
- **Declarative instantiation counts as use.**  Besides imperative
  `engine.instantiate`, a prefab is instantiated by the runtime when it is named
  in a `spawn: <Prefab>` (companion to `bind_list:`, one instance per list
  element) or `payload_spawn: <Prefab>` (companion to `cascade_type:`, one
  instance per cascade field) attribute of another prefab's child.  Those
  targets are folded into the use-set too, so list-row and cascade-chip prefabs
  that are *only ever* spawned this way are not falsely reported as dead.  Note
  these targets are **not** hard-validated the way `engine.instantiate` names are
  (Tier 1): a `- prefab:` is registered whenever its file is imported, but its
  spawn target may live in a file only *some* build targets pull in (e.g.
  `prefabs.aiml` declares `CardEditorScene`, which spawns `AA*Chip` from
  `cascade_chips.aiml`; the board-only client build imports the former but not
  the latter).  Flagging that would break legitimate partial builds.

## Tier 4 — input reachability

Tiers 1 to 3 ask whether a **name** exists. Tier 4 asks whether a declared
widget can ever be **reached by input** — the class of bug that produces a
button nobody can press, a decoration that swallows the pointer, and a plate
you click straight through.

### The model these rules rest on

Input travels **two independent paths**, and they arbitrate differently:

| Path | Order | Honours |
| --- | --- | --- |
| `hit_test` / `update_hover` (`prefab_runtime.cpp`, reverse instance order) | top-to-bottom by z, **first hit wins** | `visible`, `input_transparent` |
| the **widget pass** (same file, per child) | over **all** instances, no z arbitration, `if (over)` with **no `break`** | `visible`, `input_transparent`, `input_blocking` (as a modal gate) |

Three consequences, each one a bug class:

* A widget covered by a higher instance usually **stays clickable** — what it
  loses is the *hover*. The engine's own comment records the case: a fullscreen
  overlay blocked hover correctly while the buttons underneath kept firing.
  `input_blocking` is what closes that path.
* `input_transparent` is skipped by **both** paths, so an interactive child
  inside such a prefab is not merely hard to reach, it is **dead**.
* Because the widget pass does not stop at the first hit, **two overlapping
  widgets both fire** on one click.

The instance's hover rectangle is the prefab's **declared** `width`/`height`
(× scale), while children are **not** clipped to it unless `clip_children` is
set. A child outside that box is therefore drawn and clickable but never part
of the instance's hover.

### The rules

**F44_8-1 — a click-through prefab has no interactive children (error).**
`input_transparent: true` together with a child of type `button`, `checkbox`,
`radio`, `slider`, `dropdown`, `text_input`, `textbox` or `cascade` is a
contradiction: the widget can be neither hovered nor clicked. *Escape:* if any
code calls `set_input_transparent` on an instance of this prefab, the prefab
value is only a default and the check stays silent.

**F44_8-2 — a child lies inside its prefab's box (warning).** `pos_x`,
`pos_y`, `pos_x + width`, `pos_y + height` within `0 … width/height` of the
prefab. Outside it, the child is drawn and the widget pass still hits it, but
`hit_test` never reports the instance there — hover-driven logic silently does
not apply. Deliberate overhang (a banner, a glow) belongs in a prefab that is
`input_transparent: true`, which also silences this rule.

**F44_8-3 — a child has a positive size (error).** `width <= 0` or
`height <= 0` is invisible and unhittable.

Both F44_8-2 and F44_8-3 apply **only to children whose box is used**: `rect`,
`sprite`, `button`, `checkbox`, `radio`, `slider`, `dropdown`, `text_input`,
`textbox`, `container`, `bar`, `cascade`. A `label` / `text` child is placed by
**text metrics** (`pos_x`/`pos_y` + `font_size`) and takes no input, so
`width: 0` is its normal state — the corpus declares labels that way
(`test_prefab.aiml`, child `zone_label`). Measuring a label against a box it
never uses would be a false finding, and this rule set is only worth having if
every finding is real.

**F44_8-4 — two interactive children do not overlap (warning).** Both receive
the same click, in declaration order, and both act. Two escapes, because real
code stacks alternatives in two different ways: one of the pair declared
`visible: false`, **or** either name passed to `set_child_visible` somewhere —
runtime-managed stacking is the common form, and 9rooms builds all of its
stacked panels that way.

It is a **warning and not an error** for a measured reason. On the 9rooms client
the rule found 22 overlaps; the `set_child_visible` escape removed 16, and of
the remaining 6, three (`AckDot`'s three checkboxes) are still runtime-managed
stacks whose management the compiler cannot see: the instance comes from a
container row, and the `var = engine.instantiate("Literal")` binding Tier 2
relies on does not track it. An error with that rate would be unusable; a
warning that names its escape is worth having, because the other three findings
(`ForumCatRow`, `ForumThreadRow`: a delete button on top of a full-row click
area) are exactly the bug this chapter is for.

### F44_8-5 and F44_8-6 are hints behind a switch

Both rules rest on the same Tier-2 binding, and real code defeats it: instances
travel through helper functions and container rows, so "nobody reads this
widget" often means "the compiler cannot see who reads it". Measured on the
9rooms client: F44_8-6 named 24 prefabs, and spot checks showed their widgets
**are** read (`pchip.child_clicked("btn_main")` on an instance the binding never
tracked). As a hint the rule is useful; as an always-on warning it lies.

Both therefore require `AIML_LINT_PREFAB_INPUT=1`, the same shape as the
env-gated rules in `spec/49` §11. F44_8-1 to F44_8-4 are always on, because their
evidence is the declaration itself.

**F44_8-5 — decoration declares itself (warning, gated).** A prefab with **no**
interactive child, not `input_transparent`, not `draggable`, and no instance of
which is ever asked about pointer input (`is_hovered`, `was_clicked`,
`child_contains`, `child_rect_x`/`_w`, `set_draggable`) can only ever *take*
hover from what lies under it. The remedy depends on the intent, and the
diagnostic names both: `input_transparent: true` for decoration,
`set_input_blocking(1)` for a plate that is meant to block.

**F44_8-6 — a declared widget is read somewhere (warning, gated).** A prefab whose
interactive children are never read — no `child_clicked`, `is_checked`,
`text_value`, `dropdown_value`/`_index`/`_enum`, `get_slider_value`,
`selection`, `child_focused` on any of its instances — is dead UI: it renders,
it reacts visually, and nothing acts on it. *Escape:* a call with a
**non-literal** child name (a loop over names) disables the rule for that
prefab, because Tier 2's limits (only string literals are tracked) apply here
too.

### What the rules found when they were first run (2026-11-19)

A check is worth having only if its findings are real, so the rule set was
measured against the corpus and against the largest real interface in reach
before it was kept:

| | corpus (`tests/`, 1238 files) | 9rooms client (2290 diagnostics in total) |
| --- | --- | --- |
| always on (F44_8-1 … -4) | **0** | **11** — 5 × F44_8-2, 6 × F44_8-4 |
| gated hints (F44_8-5, -6) | 4 | 35 — 11 × F44_8-5, 24 × F44_8-6 |

Three false-positive classes were found and removed on the way, and each one
shaped a rule:

1. **Labels have no box.** F44_8-3 first reported every `label` declared
   without `width`/`height` — three in the corpus alone. A label is placed by
   text metrics and takes no input, so the box rules now apply only to the types
   whose box is actually used. `cascade` is excluded for the same reason: the
   runtime builds its sub-UI itself, and nothing establishes that the declared
   box is its area.
2. **Stacks are managed at runtime.** F44_8-4 reported 22 overlaps on the
   client; 16 of them were alternative panels toggled with `set_child_visible`,
   not with `visible: false`. Hence the second escape.
3. **Instances travel.** F44_8-6 reported widgets as unread that are read
   through instances the Tier-2 binding never sees (container rows, helper
   functions). Hence the switch.

Of the six overlaps that remain on the client, three (`AckDot`) are still
runtime-managed stacks the compiler cannot see, and three are the real thing:
`ForumCatRow` and `ForumThreadRow` put a delete button on top of a full-row
click area, so one click acts twice.

### Where a finding is reported

F44_8-1 to F44_8-4 are findings about **one child**, and they report at that
child's line and column — a prefab has a dozen children, and a diagnostic
pointing at the `- prefab:` line leaves the reader searching (spec/49 F49-100).
F44_8-5 and F44_8-6 are findings about the **prefab**: the first says the prefab
as a whole consumes no input, the second names several widgets at once. Both
report at the declaration.

### Scope and limits

The rules above use **only** declaration data and the wiring Tier 2b already
collects. Deliberately **not** checked:

* **Occlusion between instances.** Instance position and z come from `set_pos`
  and `set_z` at runtime and are usually computed (card layouts, scroll
  offsets). Worse, z takes effect for hit-testing **one frame late**, because
  the sort happens while drawing. A compiler claiming "always covered" would be
  guessing, and a wrong finding costs exactly the time it was meant to save
  (F49-103).
* **Runtime visibility.** `set_visible` can turn any of this on and off per
  frame.

"Always covered" needs runs, not a declaration. The honest home for it is a
frame-time audit, and the engine already has the data: the hit recorder writes
**every** child under the press point to `prefab_last_hit`, separated by `|`,
without stopping at the first. An `engine.input_audit()` over the live scene —
covered widgets, hovered instances with no interactive child, several
interactive rectangles under one point — is an open item for `spec/44_9`
(UI testing), not for this chapter.

## `--prefab-report` — wiring audit mode

`aiml <entry> --prefab-report [--asset-roots=…]` runs the whole import graph
through the same fact-collection passes but **emits no objects/binary** — it
prints a grouped prefab-wiring report to stdout instead.  It is meant for
answering "did I wire everything the way I intended?".

The report lists, for every prefab in the build:

- **declaration site** (`file:line`) and child count;
- **how it is reached** — each `engine.instantiate` site (`file:line`) and each
  declarative `spawn:` / `payload_spawn:` site (`OwnerPrefab.child (channel)`),
  or a `DEAD` marker if neither;
- a `[draggable]` tag when the prefab is whole-prefab draggable;
- **buttons** — every `type: button` child, each flagged `<NO child_clicked>`
  when its name is never polled via `inst.child_clicked("<name>")` anywhere in
  the build;
- **children** addressed at runtime via `set_*` (best-effort — only instances
  bound to a local variable are tracked; a scene stored in a `<.field` is not,
  so an empty list is not proof of "unwired").

A trailing **interaction summary** counts total buttons, buttons with no
`child_clicked` handler anywhere (definite dead buttons — a button name never
polled is unambiguous; the "wired" side is a heuristic because child names are
not prefab-unique), and the number of draggable prefabs.

Implementation: `ctx.reportPrefabs` makes `checkPrefabUsage` append tab-separated
fact records (`D`/`I`/`S`/`C`/`B`/`K`) to `ctx.prefabReportLines`; these merge up
the sub-compiler hierarchy into `AIMLCompiler::allPrefabReport` (exactly like the
Tier-3 sets) and `printPrefabReport` groups and renders them at the top level.
`--prefab-report` forces `noCache` so every import is re-walked (a cache hit
would drop a module's facts), and skips object emission in `resolveImport`.
Drop *targets* are handled imperatively (board logic), so the report captures
drag **sources** (draggable prefabs), not drop zones.

## Naming rule — prefab and child names are >= 5 characters

A prefab and each of its children are addressed **by name across files** — via
`engine.instantiate("<Prefab>")`, `spawn:` / `payload_spawn: <Prefab>`, and the
child-addressing methods (`set_text` / `set_texture` / `child_clicked` /
`set_child_visible` / `set_dropdown_*` …).  These names are the UI's cross-file
wiring vocabulary, so a terse name like `bg` / `btn` / `pa0` reads as noise at
every call-site and collides easily.

Therefore prefab names and prefab child names must be **at least 5 characters** —
stricter than the 3-character floor `validateIdentifier` applies to ordinary
identifiers (variables, arguments, functions, classes).  A shorter name is a
hard compile error:

```
<loc>: prefab child name 'bg' is shorter than 5 characters — prefab and child
names are referenced by name across files; pick a descriptive name (>=5 chars)
```

Implementation: `CompilerContext::validatePrefabName` (min 5), called from
`checkPrefabUsage` for each `- prefab:` name and every child `name:`.  Runs once
per module (not in the fixed-point `registerTypes` loop, to avoid double-logging).

### `--prefab-report`: behavioral wiring (owner fields + driver methods)

Beyond the static structure, the report shows how each prefab is *driven*.  Prefab
instances live in class fields (`<.spell_stack`) and are operated by
`<.field.<op>(...)` calls across methods.  `checkPrefabUsage` (report mode) records
two extra fact kinds:

- `F  <class> <field> <prefab>` — a `set: [<.field, engine.instantiate("<prefab>")]`
  binding (which field holds which prefab).
- `O  <class> <field> <op> <bucket> <method> <arg0?>` — a `<.field.op(...)` call,
  classified into a bucket: `content` (set_text/texture/dropdown_options/bind_list),
  `visibility` (set_visible/set_alpha/set_child_visible), `motion`
  (set_pos/set_z/set_size/set_scale), `gauge` (set_bar_*), `interaction`
  (child_clicked/was_clicked/set_draggable).

Under each root prefab the report prints an `owner:` block listing, per bucket, the
driver methods and their ops — so e.g. `DamagePopup`'s rise shows as
`motion: apply_dmg_anim (set_pos)` + `visibility: apply_dmg_anim (set_alpha)`, and
`PhasePanel`'s ack wiring as `content: init (bind_list(ack_container))`.

High-confidence completeness flags: `(!) created but never driven` (field bound but
no ops) and `(!) bar child but set_bar_* never called`.  We deliberately do **not**
flag "no content", because content is frequently set by a helper that takes the
instance as a parameter (`populate(inst, …)`), which the field-op scan cannot
attribute — so an empty `content:` line is not proof of missing wiring.

### `--prefab-report`: collections and actions

Two further sections surface functional wiring the structural tree omits:

- **COLLECTIONS** — `list<prefab_instance>` class members (`M` records).  A direct
  `<.field.push(x)` whose `x` resolves to a prefab (literal `engine.instantiate`,
  a spawn-helper, or a tracked local) binds the element type (`L` record); every
  `<.field` reference records a touching method (`T`).  A spawn-helper is a
  function returning `prefab_instance` whose body instantiates exactly one prefab.
  Fields filled indirectly through a parameter (e.g. `mana_pool_ali` via a helper
  that pushes to its list arg) are still listed, with `element: ?`.
- **ACTIONS** — every `net_*` call is recorded with its enclosing method (`N`),
  and printed as `<class>.<method> -> net_a, net_b, …`.  This shows the server
  RPCs each method fires (e.g. `MainMenuApp.run -> net_play_card, …`,
  `DeckEditorScreen.handle_drop -> net_update_deck`), i.e. the functional wiring
  behind drag/drop and clicks that the prefab tree alone cannot express.

`F` binding now also resolves helper-bound fields (`<.soul_bob = spawn_soulmage(...)`).

### `--prefab-report=<scene>`: focus one scene + scene index

`--prefab-report` (no value) prints the whole build.  `--prefab-report=<substr>`
focuses one scene: a case-insensitive substring matched against the instantiate
**source** path — e.g. `--prefab-report=board` restricts the trees to
`board_screen.aiml` + `board_widgets.aiml`.  Because every field-owned prefab is
instantiated in its owner class's source, a source substring captures the whole
scene without a separate class match.  COLLECTIONS are filtered by class; ACTIONS
are always shown in full (server RPCs frequently live in a dispatch class, not the
scene class, so `net_play_card` is never hidden from a board view).

Every report opens with a **scene index** listing each instantiate source and its
root count (matches marked `*`), so the available `--prefab-report=<substr>` values
are discoverable.  Roots are grouped by *all* their instantiate sites, so a prefab
used in several screens (e.g. `CardFull` in the board and the card editor) appears
under each.

### `--prefab-report`: KEY BINDINGS

Keyboard input is read via `input.key_pressed("<key>")`.  Each such call with a
literal key is recorded (`Y` record) with its enclosing method, and printed in a
**KEY BINDINGS** section: `"<key>" -> <class>.<method>` (e.g. `"space" ->
BoardScreen.apply_reaction_window`, `"f"/"s"/"d"/"a" -> apply_movement_keys`).
Filtered by class like collections, so a scene focus shows only that scene's keys.

## Behoben (2026-10-15): Kindnamen-Check deckte die Produktionsform nicht ab

Gefunden am 2026-10-11, geschlossen am 2026-10-15 — die Lücke bestand aus zwei Hälften,
die zusammen genau die gebaute Wirklichkeit ausnahmen: geprüft wurden nur **schreibende**
Kindmethoden (`set_*`) auf einer Instanz aus einer **lokalen** Bindung.  Beides traf auf
keinen einzigen cosmos-Bildschirm zu: dort hält ein Klassen-Member die Szene, und die
klickkritischen Methoden sind die lesenden (`child_clicked`, `text_value`).

Bis dahin lautete die Auflage „der Abgleich bleibt Handarbeit".  Diese Handarbeit hat am
2026-10-15 versagt: beim Umbau des cosmos-Hauptmenüs (doc/spec/107) fielen die Kinder
`logo_img` und `build_lbl` aus der `MainMenuScene`, zwei Aufrufe blieben stehen, der Build
war grün — und der WASM-Client brach im ersten Rahmen ab (`___cxa_throw`, schwarze Seite).
Ein Check, der die vorherrschende Schreibweise nicht sieht, schützt nicht; die Regel steht
jetzt als **Tier 2b** oben.

## Behoben (2026-10-13): unknown child keys were silently ignored

Found while adding the per-child `font:` attribute (cosmos `doc/spec/98` F98-15): the prefab child
parser (`src/core/ast/prefab_builder.cpp`) is a chain of `else if (childKey == …)` with **no final
`else`**.  A key it does not know is dropped without a word, so a prefab that asks for something
the build cannot deliver compiles clean and then behaves as if the line were not there.

That is how the `font:` attribute looked like it worked for a whole build-and-screenshot cycle
before anyone noticed that nothing had changed.  Asset *values* are already strict — a texture
stem that does not exist is a compile error, deliberately, "typo catched at build-time, not as
black-square at runtime" — and the same reasoning applies one level up to the *keys*.

Needed: an error on an unknown child key, with the valid keys listed (the error path that
`prefab_runtime` already produces for an unknown child **name** is the model).  Same rule as aiml
`spec/62` F62-65 for pipeline steps, for the same reason: here the defaults are the dangerous
direction.

**Behoben.** Die Kette hat ein abschliessendes `else`, das ein unbekanntes Attribut als
Compile-Fehler meldet (Kind, Typ, Schluessel; bei `password` mit dem Hinweis auf `secret`).
Die Regel steht in spec/44_6 §4, der Negativ-Test ist `tests/test_prefab_unknown_attr.aiml`.
Anlass war ein unmaskiertes SSH- und sudo-Passwort im cosmos-Client: dort stand
`password: true`, der Maskierungs-Schluessel heisst `secret` — die Zeile wurde verworfen und
die Passwoerter standen im Klartext auf dem Schirm.

Beim Scharfschalten kamen drei weitere stille Luecken ans Licht: `min`, `scrollable` und
`tail` standen in der Attribut-Tabelle, wurden aber nur vom JSON-Lader der Laufzeit gelesen;
und `visible` las **niemand**, obwohl 9rooms es an 44 Kindern deklariert. Alle vier sind
inzwischen durchgezogen (Parser, Deskriptor, Laufzeit).
