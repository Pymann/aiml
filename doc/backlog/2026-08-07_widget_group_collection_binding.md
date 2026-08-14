# Widget-Group → Collection Binding (sliders → map / list)

Date: 2026-08-07
Status: backlog

## Summary

Chapter 44.6 §5d (Widget↔Model Binding, Phase 1) binds a **single** scalar
widget to a **single** scalar field (`i32` / `f32` / enum). Real forms often need
the inverse of a container: a **fixed group of widgets** that together represent
one **collection field** — e.g. the card editor's 9 mana-cost sliders that are
really one `map<ManaColor:i32>`, or a row of dropdowns that is one `list<i32>`.

Today those still need manual boundary conversion (read each slider, build the
map / list; on load, split the map back across sliders). This is exactly the
gap that stopped the 9rooms card editor from adopting §5d binding wholesale.

Proposal: a **widget group** construct — analogous to how cascades *containerize*
a schema — that aggregates a set of sibling widgets into one collection field,
so `sync_from_model` / `sync_to_model` fill/harvest the whole group.

## Motivating case

`map<ManaColor:i32>` mana cost ↔ 9 sliders `mc_fire … mc_water`, keyed by the
`ManaColor` enum (1..9). Load = split map across sliders; save = collect
non-zero sliders into the map. Same shape for `list<i32>` passive codes ↔ N
dropdowns.

## Sketch (not final)

A group child (or a per-widget `bind_key:`) declares the collection field plus
each member's key/index:

```yaml
- prefab:
    name: CardEditForm
    binds: CardDraft
    children:
      # each slider contributes one entry to the map field `mana_cost`,
      # under the given enum key:
      - { name: mc_fire,  type: slider, bind: mana_cost, bind_key: ManaColor.fire,  max: 9.0 }
      - { name: mc_light, type: slider, bind: mana_cost, bind_key: ManaColor.light, max: 9.0 }
      # … 7 more …
```

- `bind:` names a `map<K:V>` (or `list<V>`) field on `binds`.
- `bind_key:` gives the map key (enum/int) — for `list<V>` an integer index.
- Compiler resolves `(field_offset, key, value_tag)`; runtime `sync_from_model`
  reads `map.get(key)` (or `list[idx]`) into the widget, `sync_to_model` writes
  it back (`map.put` / `list.set`). Missing key on load → widget default (0).

Open questions:
- Value tag limited to `i32`/`f32` initially (matches slider); enum values later.
- Sparse maps: does `sync_to_model` write 0-value entries or omit them? (Card
  editor omits zeros → map only holds paid colors.) Likely a `bind_skip_zero`
  flag.
- Ordering/So many keys: keep it declarative per-widget (`bind_key`) rather than
  a separate group node, to avoid a new container type.

## Payoff

Closes the §5d gap for the card editor and any keyed-slider / indexed-widget
form, so a whole form (scalars **and** its mana/passive collections) becomes a
single `bind_model` + `sync_*` pair with no manual map/list plumbing.

## References

- `spec/44_6_prefabs.md` §5d (scalar binding, Phase 1) — this extends it.
- `spec/44_6_prefabs.md` §5c (`bind_list`, container↔list) — the dynamic-list
  counterpart; this is the *fixed-group* counterpart.
- 9rooms card editor (`client/card_editor_screen.aiml`) — the mana-cost /
  passive boundary conversion this would remove.
