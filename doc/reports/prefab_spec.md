# Prefab Spec Gaps

This note captures the remaining specification gaps in
`test/aiml/spec/44_6_prefabs.md`. The chapter already defines a substantial
runtime surface; this document focuses only on places where the behaviour is
still ambiguous, internally inconsistent, or likely to cause implementation
drift.

## Scope

- Base spec reviewed: `test/aiml/spec/44_6_prefabs.md`
- Focus: prefab runtime semantics, not implementation planning
- Goal: identify missing or unclear rules that should be made normative

## Findings

### 1. `.child(child_name)` return semantics are unclear

Reference: `44_6_prefabs.md` section "Child Property Methods"

The method table defines:

- `.child(child_name) -> prefab_instance`

This is underspecified for non-prefab children such as `label`, `rect`,
`button`, `slider`, and `text_input`.

Open questions:

- Does `.child(...)` apply only to nested prefab children, or to every child?
- If it applies to every child, is the return value a true instance, a proxy,
  or a synthetic wrapper?
- Which instance methods are valid on the returned value?
- Does mutating the child-instance affect parent-local coordinates or world
  coordinates?

Suggested spec fix:

- Restrict `.child(...)` to nested prefab-instance children only, or
- explicitly define a child-handle/proxy type instead of reusing
  `prefab_instance`

### 2. Click and change event lifecycle is not precise enough

References:

- `child_clicked(child)` in the child method table
- `on_click` / `on_change` in the style field table

The spec uses phrases like "clicked this frame" and "fires when the value
changes", but does not fully define event lifetime.

Open questions:

- Exactly when is `child_clicked(...)` set?
- Exactly when is it reset?
- Is it stable across multiple reads in the same frame?
- Does querying it consume the event?
- Is `on_click` fired before or after `child_clicked` becomes visible to AIML?
- For `on_change`, does programmatic mutation also emit the callback?

Suggested spec fix:

- Define an explicit per-frame event model:
  - edge-triggered event flags are latched during input processing
  - readable for the remainder of the frame
  - reset at the start of the next input/update pass
- State whether setter APIs fire callbacks or not

### 3. Slider interaction rules need stronger runtime semantics

Reference: slider description and slider child methods

The spec describes value mapping and callback intent, but not several important
interaction details.

Open questions:

- Does clicking anywhere on the track immediately set the slider value?
- Must the cursor hit the handle specifically, or is the whole track draggable?
- Once grabbed, does the slider keep mouse capture until release?
- How does slider dragging interact with general instance dragging for
  `draggable` prefabs?
- Does `set_slider_value(...)` trigger `on_change`, or is callback emission
  user-input-only?
- What happens when `min >= max`?

Suggested spec fix:

- Add a short "Slider interaction" subsection defining:
  - press target
  - capture semantics
  - drag priority over prefab dragging
  - callback rules for user vs programmatic changes
  - clamping/degenerate range behaviour

### 4. Radio-group scope is surprisingly global

Reference: `radio` child type description

The spec states that radios sharing a `group` string are exclusive "across the
entire scene".

That may be intentional, but it is strong enough that it should be called out
as a deliberate runtime rule rather than a casual detail.

Open questions:

- Is grouping truly scene-global, or should it be scoped per prefab definition
  or per prefab instance?
- Do hidden radios participate in deselection?
- Do nested radios participate in the same global group namespace?
- Does `set_checked(...)` on one radio deselect peers in the same group?

Suggested spec fix:

- Add an explicit "Radio-group scope" rule
- State whether exclusivity applies:
  - scene-wide
  - only to visible radios or all radios
  - equally to user clicks and programmatic writes

### 5. Callback error handling and timing are incomplete

Reference: section `5b. Callbacks`

The callback section already defines the intended function signature and lookup
strategy, but not failure modes or execution timing.

Open questions:

- If a callback name is unknown, is that a compile error, runtime warning, or
  silent no-op?
- If a function has the wrong signature, where is the error reported?
- Are callbacks executed immediately during input dispatch, or deferred until
  later in the frame?
- Is reentrancy allowed if a callback mutates prefab/widget state?

Suggested spec fix:

- Define callback resolution failures as compile-time diagnostics where possible
- Define runtime invocation timing explicitly
- State whether callbacks may safely mutate the same prefab instance that fired
  them

### 6. Material coverage and fallback behaviour are inconsistent

References:

- duplicated `5a. Materials` sections
- unknown-material fallback behaviour

The material section is currently duplicated and partially redundant, which
increases the risk of spec drift.

Open questions:

- Which of the two `5a` sections is authoritative if wording diverges?
- Does unknown `material:` silently fall back forever, or should tooling emit a
  warning?
- Does `material` apply only to `rect` / `button` / `text_input`, or can
  `slider`, `bar`, `checkbox`, and `radio` also use materials in whole or in
  part?

Suggested spec fix:

- Merge the two material sections into one normative section
- State fallback diagnostics expectations
- State widget coverage for custom materials explicitly

### 7. Nested instances are not fully reconciled with input and hit-testing

References:

- nested instances section
- hit-testing / hover sections

The spec says nested children are skipped by the top-level draw loop and by
`hit_test`, which is clear enough mechanically, but it leaves some behavioural
holes.

Open questions:

- Are interactive nested prefab trees intentionally unsupported?
- Can nested children ever receive hover, click, or focus?
- If not, should the spec forbid interactive widgets inside nested instances?
- How does `.child(...)` resolve on nested content: parent-local, direct-only,
  or recursive?

Suggested spec fix:

- Add a short rule block clarifying whether nested instances are:
  - visual composition only, or
  - also intended to participate in input

### 8. Clip/scroll semantics need explicit interaction rules

Reference: scroll viewport + clipping section

The rendering rule is reasonably clear, especially the whole-child clipping
behaviour, but input semantics are not fully described.

Open questions:

- Are hit-tests performed in scrolled local space or original local space?
- Can a clipped-out child remain focused?
- Does scrolling affect only rendering, or also input coordinates?
- Do nested clip regions replace outer clips by rule, or is that only a
  temporary implementation note?

Suggested spec fix:

- Add a short "Input under clip/scroll" subsection
- State whether clip/scroll semantics are normative runtime behaviour or
  temporary implementation constraints

### 9. C API listing has visible drift

Reference: C API section

The low-level API summary appears to have some duplication and possible drift
from the higher-level language description.

Examples:

- `aiml_material_register(...)` appears twice
- not every high-level behavioural rule is obviously reflected in the C API
  summary

Suggested spec fix:

- Deduplicate the C API block
- Treat the C API summary as a derived appendix and keep the normative rules in
  the higher-level behavioural sections

## Recommended next edits to `44_6_prefabs.md`

High-value changes with low editorial cost:

1. Clarify `.child(...)` semantics or replace it with a dedicated child-handle
   concept.
2. Add one short subsection for event lifecycle (`child_clicked`,
   `on_click`, `on_change`).
3. Add one short subsection for slider drag semantics and drag-priority.
4. Add one short subsection for radio-group scope and programmatic updates.
5. Merge the duplicated material sections.
6. Clean the C API appendix to match the normative text.

## Non-goals

These are not treated as spec gaps here unless the team wants to elevate them:

- implementation architecture choices
- shader optimization details
- hot-reload internals
- editor-specific UX decisions
