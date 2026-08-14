# Function Decorators

Decorators annotate a function so the compiler wraps its return values (and
in future: entry points) with a fixed transformation, without changing the
call sites.  They are declared via the `decorator:` field alongside `body:`.

```yaml
- def:
    name: <fn_name>
    args: [...]
    return: <type>
    decorator: [<decorator_name>, ...]     # ordered, right-to-left
    body: [...]
```

## Semantics

For each name in `decorator:`, the compiler transforms the function per that
decorator's rule.  When multiple decorators are listed, they compose in
right-to-left order (Python convention): `decorator: [A, B]` means the raw
return value is first fed through `B`, then through `A`.

A decorator MAY be a no-op for functions whose return type does not match
its expected input type — e.g. `vocab` only wraps `string`-returning
functions, silently pass-through otherwise.  This lets AIML add decorators
liberally without spamming compile errors.

## Built-in Decorators

| Name    | Wraps               | Effect                                           |
|---------|---------------------|--------------------------------------------------|
| `vocab` | `return: string`    | Wraps every `- return: <expr>` with `aiml_vocab_lookup(<expr>)` so the value is translated into the currently-selected language before it reaches the caller.  See `/spec/44_8_vocab.md`. |

Future built-ins may include `cache`, `log`, `deprecated`, `trace`.

## Example — vocab decorator

```yaml
- def:
    name: draw_verb
    return: string
    decorator: [vocab]
    body:
      - return: "Draw"

- def:
    name: card_word
    return: string
    decorator: [vocab]
    body:
      - return: "card"

- def:
    name: draw_card_phrase
    return: string
    decorator: [vocab]
    body:
      - decl: [verb, string, { call: [draw_verb] }]   # already translated
      - decl: [noun, string, { call: [card_word] }]   # already translated
      - return: "${verb} ${noun}"                     # concatenated fragments
```

With `de` selected, `{ call: [draw_card_phrase] }` returns `"Ziehen Karte"`
(if the vocab dictionary maps `"Draw" → "Ziehen"`, `"card" → "Karte"`).  No
explicit translate calls in the body.

## Composition Example

```yaml
- def:
    name: cost_line
    return: string
    decorator: [cache, vocab]        # right-to-left: vocab first, cache stores translated result
    body:
      - return: "Pay 2 mana"
```

## Custom Decorators

Not yet supported — this section will be filled once user-defined
decorators are on the roadmap.  For now the compiler recognises only the
built-in registry (see table above); an unknown decorator name is a
compile error.

## Interaction with Hot-Reload

Decorators are applied at compile time.  Changing a function's `decorator:`
list requires a rebuild (not a hot-reload).  The decorated version is
persisted by `sourceYaml` so version deltas are visible in diffs.
