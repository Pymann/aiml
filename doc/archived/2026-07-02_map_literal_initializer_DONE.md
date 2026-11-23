# Map literal initialiser for `map<K:V>` — DONE 2026-07-02

Implemented:
- `DictLiteralExpr.entriesExpr` stores the typed value expressions in order
  (parallel to the legacy `entries: map<string,string>` for
  call args).
- `expression_builder.cpp` fills both while parsing.
- `ast_compiler_stmt.cpp::visitDeclStmt` recognises `map<K:V>` + `DictLiteralExpr`
  and emits `MapImpl::put` per entry.  Key types: string, i32, i64.
- Test: `tests/test_map_literal_init.aiml` — green.
- Spec: example added to `spec/10_collections.md`.

---

## Observation
Lists allow compile-time init with `[1, 2, 3]`. Maps currently do not
have that — after the `decl` you have to emit `.put` calls one by one:

```yaml
- decl: [effect_params, map<string:list<string>>]
- call: [effect_params.put, "draw_card",    ["DeckType", "ValueType"]]
- call: [effect_params.put, "discard_card", ["OwnerType", "CardType", "ValueType"]]
# … × 20
```

For config maps (enum cascades in the card editor, trigger→ParamCascade,
effect→ParamCascade, localisation bundles etc.) that is very noisy.

## Wish
Support map literal init in `decl`, analogous to lists:

```yaml
- decl: [effect_params, map<string:list<string>>,
    {
      "draw_card":    ["DeckType", "ValueType"],
      "discard_card": ["OwnerType", "CardType", "ValueType"],
      "move_card":    ["GameZone",  "GameZone"],
    }
  ]
```

A YAML map as the init value; the compiler emits the `put` calls under the hood.

## Concrete benefit
- Card editor: `apply_param_cascade` can shrink from a 200-line if cascade to
  `map.get(effect)`.
- Localisation: `map<string:string>` with all keys inline.
- The effect bytecode decoder needs reverse maps (id → name) for rendering.

## Nice-to-have
- For `map<K:any>` implicit tagging per value (the spec already says today:
  `any` stores the runtime type, which would be just as possible in a literal).
- Nested literals (`map<string:map<...>>`) handled uniformly.
