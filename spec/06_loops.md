# Loops

## while

### Canonical Form

```yaml
- set: [counter, 0]
- set: [limit, 5]

- while:
    condition: { lt: [counter, limit] }
    body:
      - print: [counter]
      - set: [counter, { +: [counter, 1] }]

- print: [counter]  # 5
```

### Short Form

```yaml
- while: [{ lt: [counter, limit] }]
  body:
    - print: [counter]
    - set: [counter, { +: [counter, 1] }]
```

The first (and only) list element is the condition. Both forms are equivalent.

---

## Walking a collection

There is no element-wise loop. The idiom is `while` over an index, reading the
element with `at`:

```yaml
- decl: [i, i32, 0]
- while:
    condition: { lt: [i, items.len] }
    body:
      - decl: [item, Item, { at: [items, i] }]
      - call: [log.info, item.name]
      - set:  [i, { +: [i, 1] }]
```

For lazy traversal of a collection or a database handle, take an `iterator<T>`
and drive it with `while` (`spec/54_iterators.md`).

## TODO — `for:` is not a statement

`- for: [item, list]` is **not implemented**, and not merely unimplemented: the
compiler rejects the key by name with

> unknown statement 'for' — not an aiml statement.  aiml has `while:` (spec/06).

(`src/core/ast/statement_builder.cpp`, hint table; no dispatcher in
`root_state.cpp`, `block_builder.cpp` or `control_flow_builder.cpp` knows the
key — the loop forms are `while`, plus `if`/`ret`/`try`/`text`.)

Open, deliberately undecided here: whether an element-wise loop is worth having
as sugar over the index form, and if so what it does about the element's
ownership — a copy, as `at` gives, or a borrow (`spec/61`). Until that is decided
**and** implemented, `while` and `iterator<T>` are the only ways to walk a
collection, and this chapter stays the single source for loop syntax.

**Corpus note (2026-11-16).** Three documents described or used `for:` although
it never existed: `spec/aiml_reference_guide.md` §5.2, `spec/31_db_integration.md`
§8 and `spec/32_4_network_websockets.md` §3. All three were rewritten to the
index form on 2026-11-16. `spec/62_pipelines.md` kept its two uses (§11, §13)
until pipelines were implemented; on 2026-11-27 both were rewritten to `while:`
over an index, and `tests/pipeline_spec_examples/run.sh` compiles them straight
from the chapter. No production code and no test ever used `for:`.
