# Bug: cross-file `class: X models: [Y]` fails to register

**Discovered:** 2026-05-23 — datacreatures `ActiveSkillBehavior` / `BaseSkillBehavior` registration.
**Severity:** Medium. Blocks the "model-in-one-file, behavior-in-same-file" pattern
when the model also uses `inherits:` from a model in another imported file.
**Status:** Open. Workaround available (collect behavior classes in a single file
that imports all models first).
**Repro:** `tests/test_class_models_cross_file.aiml` (+ `tests/_cross/*.aiml`).

---

## Symptom

```
[ERROR] tests/_cross/base_model.aiml: Unknown model: Derived
```

emitted from [src/core/ast_compiler.cpp:535](../../src/core/ast_compiler.cpp)
(the `class.models` resolution loop in `registerTypeDecl`).

## Minimal failing case

`base_model.aiml`:
```yaml
- model: { name: Base, fields: [[a, i32, 0]] }
```

`derived_with_class.aiml`:
```yaml
- model:
    name: Derived
    inherits: Base
    fields: [[b, i32, 0]]

- class:
    name: DerivedBehavior
    models: [Derived]
    methods:
      - def: { name: sum, return: i32, args: [], body: [{ return: { +: [<.a, <.b] } }] }
```

`main.aiml`:
```yaml
- import: "base_model.aiml"
- import: "derived_with_class.aiml"
- decl: [d, DerivedBehavior]
...
```

Expected: prints `sum = 7`.  Actual: error.

The SAME structure inside ONE file (`tests/test_class_models_inherits_chain.aiml`)
passes — so the bug is about cross-file import-ordering, not the
`inherits`/`models` semantics themselves.

## Root cause (hypothesis)

`Compiler::registerTypes` is single-pass over `prog->declarations`
(ast_compiler.cpp around line 267).  `ModelDecl` has deferral logic
(returns early when parent / field-types are not yet ready), but
single-pass means the deferred model is never retried.

When imports get inlined into `prog->declarations`, the order can end up:

1. `model Base` (from base_model.aiml)
2. `model Derived inherits Base` (from derived_with_class.aiml) — registers OK
3. `class DerivedBehavior models: [Derived]` — should be OK, Derived just registered

The reported error pins the failure on `base_model.aiml`, which suggests
the declarations from the two imported files are NOT being interleaved in
source-textual order — the class is seen before its model.

Either:
- `import` resolution prepends rather than inlines, putting child-declarations
  in a different order than the user expects;
- Or `ClassDecl` is being processed in a phase where some `ModelDecl`s have
  not yet been registered (e.g. they were deferred).

## Workaround

Put all `class: XBehavior models: [Y]` decls in a single file that imports
all required models first.  Order within that file is preserved, so
`Y` is registered before `XBehavior` references it.

```yaml
# sources/shared/tile_behaviors.aiml
- import: "sources/shared/item.aiml"               # registers Item
- import: "sources/shared/skill/active_skill.aiml" # registers ActiveSkill
- import: "sources/shared/skill/base_skill.aiml"   # registers BaseSkill
- import: "sources/shared/tileable.aiml"           # interface

- class: { name: ItemBehavior,       models: [Item],       implements: [Tileable], methods: [...] }
- class: { name: ActiveSkillBehavior, models: [ActiveSkill], implements: [Tileable], methods: [...] }
- class: { name: BaseSkillBehavior,  models: [BaseSkill],  implements: [Tileable], methods: [...] }
```

Then in server / client: `- import: "sources/shared/tile_behaviors.aiml"` —
the model files don't need to define their own behaviors.

## Real fix (later)

Convert `Compiler::registerTypes` from a single-pass loop into a
fixed-point loop:

```cpp
void Compiler::registerTypes(Program* prog) {
    // ... builtin exception classes ...
    size_t lastRegistered = SIZE_MAX;
    while (true) {
        size_t before = ctx.classes.size() + ctx.enums.size();
        for (const auto& decl : prog->declarations)
            registerTypeDecl(decl.get());
        size_t after = ctx.classes.size() + ctx.enums.size();
        if (after == before || after == lastRegistered) break;
        lastRegistered = after;
    }
}
```

Plus: in the `ClassDecl.models` branch (line 535), don't `LOG_ERROR` on
unknown model — return early like `ModelDecl` does on missing parent.
Emit the error only on the FINAL pass once registration is stable.

## See also

- `tests/test_class_models_inherits_chain.aiml` — same-file pattern works.
- `tests/test_class_models_cross_file.aiml` — this repro.
- `src/core/ast_compiler.cpp` lines 267 (registerTypes), 485+ (ClassDecl
  branch), 626+ (ModelDecl branch with deferral).
