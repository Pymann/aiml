# ClassGroup Subgroup Ports

Date: 2026-09-04
Status: DONE (implemented 2026-09-04) — `tests/test_classgroup_subgroup_port.aiml` green;
spec/58 §6.1 + status M2d updated.  See memory `aiml_classgroups`.

## Goal

Extend `classgroup` composition so an embedded subgroup can declare a typed
dependency on an instance owned by its parent.  The parent binds that dependency
when embedding the subgroup; child instances may then name the port in `sees`.

Target syntax:

```yaml
- classgroup:
    name: EffectRuntime
    requires: [[cards, CardSvc]]
    instances:
      - class: EffectProc
        instance: effects
        sees: [cards, triggers]

- classgroup:
    name: ServerRT
    instances:
      - { class: CardSvc, instance: cards }
    subgroups: [[fx, EffectRuntime, { cards: cards }]]
```

`EffectRuntime_effects.cards` and every other child `sees: [cards]` edge must
refer to the exact `ServerRT.cards` object, not a copy.  Existing same-group
edges and parent-to-child exported subgroup edges must retain their behavior.

## Current Failure

`tests/test_classgroup_subgroup_port.aiml` currently fails before synthesis:

- `EffectRuntime.effects` and `EffectRuntime.triggers` reject `cards` as an
  unknown same-group `sees` target in `Compiler::checkClassGroups`.
- `ClassGroupBuilder` accepts only two scalar fields in each `subgroups` entry.
  It therefore interprets the binding map's `cards` value as the subgroup type;
  the parent is diagnosed as `subgroup 'fx' names unknown group 'cards'`.

The immediately following unknown-method and unknown-class diagnostics are
downstream effects of those two parsing/validation failures.

## Data Model And Parser

1. In `src/core/ast_nodes.h`, add a port representation such as
   `ClassGroupPort { name, className }` and replace the anonymous
   `subgroups` pair with a `ClassGroupSubgroup { name, groupType, bindings }`.
   `bindings` is a list or map of `portName -> parentInstanceName`.

2. In `src/core/ast/classgroup_builder.h`, add parser state for the group-level
   `requires:` sequence and for the third `subgroups` item, which is a binding
   map.  Keep the old two-element subgroup form valid.

3. In `src/core/ast/classgroup_builder.cpp`:
   - parse `requires: [[port, BaseClass], ...]` like the existing `params` pairs;
   - parse `subgroups: [[member, GroupType], ...]` unchanged;
   - parse `subgroups: [[member, GroupType, { port: instance }], ...]` into the
     new binding representation instead of treating `instance` as a group type.

The map-depth handling must not create a `ClassGroupInstance` while parsing a
subgroup binding map.

## Static Validation

Extend `Compiler::checkClassGroups` in `src/core/ast_compiler.cpp`.

1. Validate every `requires` declaration before instance edges:
   - port name is non-empty and unique within its group;
   - required base class exists;
   - a port name does not collide with an instance name.

2. Permit an undotted `sees` target when it is either a sibling instance or a
declared port.  Preserve the current self-reference and duplicate-edge checks.

3. Validate each embedded subgroup binding:
   - the subgroup type exists;
   - every port required by the child is bound exactly once;
   - no unknown or duplicate child-port binding is supplied;
   - the source names an instance owned by the parent, not an arbitrary member
     or grandchild;
   - the source instance base type is compatible with the port's required base
     type.

4. Keep parent access to `subgroup.exported_instance` governed by the existing
export validation.  A port is an internal injection point, not an export.

## Synthesis And Construction

Extend `Compiler::synthesizeClassGroups` in `src/core/ast_compiler.cpp`.

1. Around `resolveSees` (currently near lines 2368-2392), resolve a port name
to its declared base type.  A port edge synthesizes a collaborator member of
type `ptr<RequiredBase>`; it intentionally does not expose a child-specific
subclass API.

2. While building the child container (currently near lines 2450-2473), add one
`ptr<RequiredBase>` member for every required port.  These fields hold the
parent-owned instance temporarily and make injection explicit in the lowered
container layout.

3. Split child wiring when a group has ports:
   - `__ctor` allocates child instances and performs only wiring independent of
     external ports;
   - synthesize a helper such as `__wire_ports` that copies each container port
     into the collaborator members of child instances that `sees` it;
   - defer a ported child's `on_wired` hooks until `__wire_ports`, ensuring hooks
     never observe an unbound port.

4. In the parent generated `__ctor` (currently near lines 2480-2559):
   - construct child containers as today;
   - allocate the parent instances, so every binding source has an address;
   - assign each configured binding to the child container port;
   - call the child `__wire_ports` helper;
   - continue with the parent phase-1 `sees` wiring and phase-2 hooks.

This preserves address-only construction, supports child-to-parent references,
and guarantees that parent-to-child calls see a fully injected child before the
parent's post-wire hooks run.

## Tests And Documentation

1. Make `tests/test_classgroup_subgroup_port.aiml` green.  It must prove:
   - a child reads the parent service through a port;
   - a child-to-sibling-to-port chain works;
   - parent-to-child access still works through the subgroup export;
   - changing the parent service after construction is visible through the port.

2. Add negative compiler tests for missing binding, unknown port, duplicate
binding, non-parent source, and incompatible source type.

3. The new test currently also violates the global minimum identifier-length
rule (`fx`, `v`, `rt`, `a` through `d`).  Independently of port support, either
rename those identifiers or add `# static-check: off` to keep the feature test
focused on classgroup behavior.

4. Update `spec/58_classgroups.md` section 6 with the syntax, visibility rule,
binding validation, and the deferred post-wire lifecycle for ported subgroups.

## Verification

Run the focused test:

```bash
./out/aiml/aiml -r tests/test_classgroup_subgroup_port.aiml
```

Then run the existing regression coverage:

```bash
./out/aiml/aiml -r tests/test_classgroup_subgroup.aiml
./out/aiml/aiml -r tests/test_classgroup_subgroup_sees.aiml
./out/aiml/aiml -r tests/test_classgroup_neg_unexported.aiml
```
