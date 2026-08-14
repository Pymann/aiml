# Runtime `const char*` Returns Must Own the Buffer (or Doc as Borrowed)

Date: 2026-07-09
Status: backlog
Discovered by: 9rooms_v2 DeckEditor bug — user-typed deck name was empty
                after push into `list<Deck>`, even though the interpolation
                logged the correct value at push time.

## Summary

AIML runtime C-side functions return `const char*` and the compiler wraps
that pointer as an AIML `string`.  Two functions were caught returning a
pointer into a mutable widget-internal `std::string`:

- `aiml_prefab_text_value(inst, child_name)` → returned `ch->text.c_str()`
- `aiml_prefab_dropdown_value(inst, child_name)` → returned `ch->dd_value_cache.c_str()`

The AIML side stored the pointer in a local `string` variable, then in a
Model field via `set:`, then pushed the Model into a `list<Model>`.  All
three values shared the SAME buffer.  When the caller subsequently reset
the widget via `set_text("", …)`, the `std::string` reassignment freed the
old buffer — every consumer (including the pushed Model's `name`) now
pointed at freed heap → read back as `""`.

Fix committed: both functions now `strdup` before returning; consumer owns.

## Why this happens (not just here)

The AIML string type expects **ownership** (compiler-generated `strdup` on
consume — see `arg_dump_runtime.cpp:77` and `visitAtExpr` for list<string>
in ast_compiler_call.cpp).  Runtime functions returning raw internal
`c_str()` pointers break that contract silently: nothing crashes until the
underlying container mutates.

## Other functions at latent risk (audit needed)

Definitely mutable internals:
- `aiml_input_event_type` / `_key` / `_char_` (input_runtime.cpp:42–49) —
  next event overwrites the buffer.  Consumers that store the string past
  the next `input.next_event()` call get corruption.
- `aiml_ui_text_input` / `aiml_ui_text_area` (ui_runtime.cpp:1359, 1396) —
  immediate-mode UI buffer.

Probably stable (once-registered, but same contract violation on paper):
- `aiml_prefab_enum_labels`, `aiml_prefab_cascade_options`,
  `_cascade_field_type`, `_cascade_field_label`
- `aiml_vocab_lookup`, `aiml_vocab_language_code`
- `aiml_engine_platform`, GLTF names, mirror events

## Decision (2026-07-09, user)

Do NOT blanket-`strdup` all runtime `const char*` returns — for hot-path
metadata (enum labels, cascade options, vocab lookups) that's an
unnecessary allocation per call and per frame.

Apply `strdup` only to **UI-input-reads** where the underlying string can
be mutated by later runtime calls in the same AIML frame:
1. `aiml_prefab_text_value` — done
2. `aiml_prefab_dropdown_value` — done
3. TODO if audits find them problematic: input event getters, ui_text_input,
   ui_text_area.

## What's still not done

- No test in `aiml/tests/` reproduces the widget-mutation-invalidates-read
  case (my minimal `test_list_model_string_persist*.aiml` tests all PASS
  because they use string literals — the bug needs the runtime-mutable
  path).  Adding a `test_prefab_text_value_survives_reset.aiml` would nail
  it down.
- No compiler-level guard.  A future pass could annotate runtime string
  returns as "borrowed" vs "owned" in a builtin registry, so the compiler
  auto-`strdup`s the borrowed ones.  Related to the Phase-1 refcount
  backlog item (2026-06-06) — that would also fix this at the source.

## Repro

Before the fix:
```yaml
- decl: [name_v, string, { call: [<.popup.text_value, "name_input"] }]
- decl: [deck, Deck]
- set: [deck.name, name_v]              # holds pointer into widget.text
- call: [<.decks.push, deck]            # copy still holds same pointer
- call: [<.popup.set_text, "name_input", ""]  # frees widget.text buffer
- decl: [x, Deck, { call: [<.decks.get, 0] }]
# → x.name = ""   (was "user typed value" between the get and the reset)
```

After the fix: `text_value` returns a strdup'd copy; `deck.name` owns its
buffer independent of the widget.
