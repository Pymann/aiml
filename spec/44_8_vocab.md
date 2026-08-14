# Vocab — i18n / Translation Runtime

Zero-friction internationalisation.  Two design goals:

  1. **No translation calls in normal code.**  A widget with `label: "Save Card"`
     renders as `"Karte speichern"` when German is selected — the label field
     itself stays English (the canonical form used everywhere for
     serialization, lookups, IDs).
  2. **Explicit loading via the asset engine.**  The AIML side reads both
     JSON assets and hands them to the runtime once at startup.

## Interception points

There are two orthogonal places where a source string becomes its
translation.  Both fall back silently to the source when no match exists.

| Interception             | Layer            | Covers                                       |
|--------------------------|------------------|----------------------------------------------|
| `push_text_batch`        | Prefab runtime   | Every label, button caption, dropdown row, ability-text render — automatic and transparent. |
| `decorator: [vocab]`     | Compile wrapping | Formatter functions that build fragments before concatenation.  See `/spec/55_decorators.md`. |

Double-translating an already-translated string is silently harmless: the
second lookup misses (German string not in English-keyed vocab) and the
pass-through returns it unchanged.

## Asset format

### `languages.json`

Index → language code.  The index is the position in the value-array
of every `vocab.json` entry.  Index `0` is the source language (usually
English) and never needs its own translations.

```json
{
  "0": "en",
  "1": "de",
  "2": "fr"
}
```

### `vocab.json`

Source string → array of translations.  Entry at position `i` is the
translation for the language whose `languages.json` index is `i`.

```json
{
  "Save Card":  ["Save Card", "Karte speichern", "Sauvegarder"],
  "Reset":      ["Reset",     "Zurücksetzen",    "Réinit."],
  "Draw":       ["Draw",      "Ziehen",          "Piocher"],
  "card":       ["card",      "Karte",           "carte"]
}
```

An empty string in the array is treated as "not yet translated" — the
lookup falls back to the source.  A shorter-than-expected array behaves
identically (missing entries fall back).

## AIML side

```yaml
# Startup — load JSON bytes via the asset registry, feed vocab runtime.
- decl: [langs_bytes, string, { call: [sys.asset_text, "vocab/languages.json"] }]
- decl: [vocab_bytes, string, { call: [sys.asset_text, "vocab/vocab.json"] }]
- decl: [rc, i32, { call: [vocab.load, langs_bytes, vocab_bytes] }]
- if: { condition: { neq: [rc, 0] }, then: [{ call: [log.warn, "vocab load failed: ${rc}"] }] }

# Query.
- decl: [n_langs, i32, { call: [vocab.language_count] }]
- decl: [cur,     i32, { call: [vocab.current_language] }]

# Selection (from the settings-overlay language dropdown).
- call: [vocab.set_language, 1]     # → "de"

# Language code, e.g. for the settings dropdown text.
- decl: [code, string, { call: [vocab.language_code, 1] }]   # → "de"
```

## Runtime API (C)

Declared in `sys/vocab_runtime.h`.

| Function                                              | Purpose                                           |
|-------------------------------------------------------|---------------------------------------------------|
| `aiml_vocab_load(langs_json, vocab_json)`             | Parse both JSON streams into module state.  Rebuilds tables on a second call.  Returns `0` on success, `-1` on languages-JSON error, `-2` on vocab-JSON error. |
| `aiml_vocab_set_language(idx)`                        | Switch active language; out-of-range clamps to 0. |
| `aiml_vocab_current_language()`                       | Current active language index.                    |
| `aiml_vocab_language_count()`                         | Total languages loaded.                           |
| `aiml_vocab_language_code(idx)`                       | Language code for the given index (`NULL` if OOR). |
| `aiml_vocab_lookup(source)`                           | The core lookup — used by both interception points. |

`aiml_vocab_lookup` guarantees: never returns `NULL` (falls back to
`source`), never allocates, and hands back a pointer that stays valid
until the next `aiml_vocab_load`.

## Fast-path invariants

- If `aiml_vocab_load` has never succeeded, every `lookup` is a single
  branch and returns `source`.
- If the current language is index 0 (source), every `lookup` short-
  circuits with the same fast path.
- Only when a non-source language is active and the source appears in
  the map does a hash-map lookup happen.

## Hot-reload

Calling `aiml_vocab_load` again wipes and repopulates the tables.  The
current language index is preserved (clamped if the new load has fewer
languages).  This lets a settings screen re-load `vocab.json` on the fly
without restarting the app.
