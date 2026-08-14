# Map-Literal-Initializer für `map<K:V>` — DONE 2026-07-02

Implementiert:
- `DictLiteralExpr.entriesExpr` speichert Reihenfolge-erhaltend die typed
  Value-Expressions (parallel zur legacy `entries: map<string,string>` für
  Call-Args).
- `expression_builder.cpp` befüllt beide beim Parse.
- `ast_compiler_stmt.cpp::visitDeclStmt` erkennt `map<K:V>` + `DictLiteralExpr`
  und emittiert `MapImpl::put` pro Entry.  Key-Typen: string, i32, i64.
- Test: `tests/test_map_literal_init.aiml` — grün.
- Spec: `spec/10_collections.md` Beispiel eingefügt.

---

## Beobachtung
Listen erlauben Compile-Time-Init mit `[1, 2, 3]`. Maps haben
das aktuell nicht — man muss nach dem `decl` einzeln `.put`-Calls emittieren:

```yaml
- decl: [effect_params, map<string:list<string>>]
- call: [effect_params.put, "draw_card",    ["DeckType", "ValueType"]]
- call: [effect_params.put, "discard_card", ["OwnerType", "CardType", "ValueType"]]
# … × 20
```

Für Config-Maps (Enum-Kaskaden im Card-Editor, Trigger→ParamCascade,
Effect→ParamCascade, Localization-Bundles etc.) ist das sehr laut.

## Wunsch
Map-Literal-Init im `decl` unterstützen, analog zu Listen:

```yaml
- decl: [effect_params, map<string:list<string>>,
    {
      "draw_card":    ["DeckType", "ValueType"],
      "discard_card": ["OwnerType", "CardType", "ValueType"],
      "move_card":    ["GameZone",  "GameZone"],
    }
  ]
```

YAML-Map als Init-Wert; Compiler emittiert die `put`-Calls unter der Haube.

## Nutzen konkret
- Card-Editor: `apply_param_cascade` kann von 200-Zeilen if-Kaskade auf
  `map.get(effect)` schrumpfen.
- Localization: `map<string:string>` mit allen Keys inline.
- Effect-Bytecode-Decoder braucht Reverse-Maps (id → name) für Rendering.

## Nice-to-have
- Bei `map<K:any>` implicit tagging pro Value (spec sagt heute schon:
  `any` speichert Runtime-Typ, wäre in Literal genauso möglich).
- Verschachtelte Literale (`map<string:map<...>>`) einheitlich.
