# Prefab Enum-Valued Selection Widgets (Dropdown + Radio)

Date: 2026-07-12
Status: backlog

## Summary

The AIML prefab / UI engine should offer selection widgets — **dropdowns AND
radio-button groups** — that return an **enum value directly**, instead of a
string that callers must then parse back into an enum. Today
`scene.dropdown_value("type_dd")` yields a `string`, forcing a string→enum
boundary parser (e.g. `card_type_from_string` in 9rooms_v2). That parser is a
hand-rolled string switch and now trips the `str_cmp` static check (spec/49
§16) — it needs `static_off: ["str_cmp"]` because there is currently no other
way to turn the UI's string selection into a typed enum.

A typed selection widget removes the boundary entirely: the widget is bound to
an enum type, its option list is derived from the enum members, and the value
accessor returns the enum (i32) directly. No string comparison, no boundary
parser, no `static_off`.

Applies to every single-choice selection widget:

- **dropdown** (`dropdown_value`)
- **radio-button group** (radio value accessor)

Both share the same underlying need: one selected option out of a fixed set —
which is exactly what an enum models. The binding/accessor design below should
be one mechanism reused by both widget kinds.

## Motivation

- Eliminates the one remaining legitimate string→enum parser at the UI layer.
- Makes UI selections type-checked end to end (invalid option is impossible).
- Aligns the UI with the enum-first direction pushed by the `str_cmp` rule.

## Rough Design

- Prefab side (`src/core/ast/prefab_builder.cpp` + UI runtime): a dropdown or
  radio-group can declare an enum type; option labels come from the enum members
  (or an explicit label map), and the selected value is stored as the enum's i32.
- Runtime accessor: a typed variant of the value accessor that returns the enum
  value rather than the display string (display string still available for
  rendering via the existing enum→string helper). Same accessor shape for
  dropdown and radio.
- Open question: how options are labelled (raw member name vs. a supplied label
  table) and whether the binding is declared in the prefab YAML or wired in code.

## Follow-ups Once Implemented

- 9rooms_v2: drop `static_off: ["str_cmp"]` from
  `sources/models/card_types.aiml::card_type_from_string` and switch the
  `card_editor_screen` dropdown call sites to consume `CardType` directly.
- Revisit any other UI dropdown → string → parse chains.

## References

- spec/49 §16 (string-literal comparison static check)
- 9rooms_v2 `client/card_editor_screen.aiml` (`dropdown_value("type_dd")` → `card_type_from_string`)
- 9rooms_v2 `sources/models/card_types.aiml` (`card_type_from_string`, now `static_off: ["str_cmp"]`)
