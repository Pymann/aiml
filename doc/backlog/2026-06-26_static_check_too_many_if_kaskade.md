# Backlog: static check "if/else cascade limit"

**Status:** proposed (2026-06-26)
**Related:** spec/49 §14 (≤3 decls per file), §15 (main composes external)

## Idea

Compiler hard error when a single function contains more than **5
sequential `if`/`else if` blocks** that evaluate the same
discriminator field (method-name dispatch, enum string switch,
type tag switch).

Example that becomes an error in future:

```yaml
- def:
    name: dispatch_old_school
    args: [[ev, rpc_event]]
    body:
      - if: { condition: { eq: [ev.method, "login"] },     then: [...] }
      - if: { condition: { eq: [ev.method, "register"] },  then: [...] }
      - if: { condition: { eq: [ev.method, "validate"] },  then: [...] }
      - if: { condition: { eq: [ev.method, "list_x"] },    then: [...] }
      - if: { condition: { eq: [ev.method, "do_y"] },      then: [...] }
      - if: { condition: { eq: [ev.method, "complete"] },  then: [...] }  # ← 6 → error
```

## Diagnostic text (proposal)

```
ERROR: test.aiml:42 (in dispatch_old_school): 6 sequential if-checks
       against `ev.method`. Limit is 5; refactor to:
       • map<string:T> lookup (e.g. RpcDispatcher pattern), or
       • polymorphic interface with `implements:` + per-class behavior.
       See spec/16_classes.md §7 and sources/game/rpc_handlers/
       rpc_dispatcher.aiml for the canonical replacement.
```

## Why

If/else cascades over a discriminator field are a symptom of
missing polymorphism or a missing lookup table. They scale
badly (O(n) instead of O(1)), split add logic over 6+ places, are
harder to test (cross-cutting state in one giant function), and
are a magnet for copy/paste bugs. The static check steers
developers early towards the map/interface variant.

## Scope

- **Counter per function**, not per file (several small
  helper switches should stay allowed).
- **Discriminator detection**: an `if` sequence only counts if all
  conditions have the form `{ eq: [<same_lhs>, <literal>] }`.
  Differently built `if` sequences (e.g. range checks) are exempt.
- **Threshold = 5** (implement first, the value is negotiable).
- **No `static_off` escape** — analogous to the other caps (see
  feedback_quality_over_shortcuts).
- **`else if` counts too** once implemented; current AIML has
  no `elif` syntax, so only the `if` sequence counts.

## Test cases (for tests/)

1. `test_static_too_many_if_kaskade.aiml`: 6 `if {eq: [ev.method, "X"]}`
   in one function → exit 1 with the hint.
2. `test_static_if_kaskade_under_limit.aiml`: 5 `if` conditions
   → compile OK.
3. `test_static_if_kaskade_different_lhs.aiml`: 7 `if` conditions,
   but different discriminators (`a.x`, `b.y`, …) → compile OK.

## Implementation notes

- In `ast_compiler_stmt.cpp::visitFunction` iterate over the top-level stmts;
  count every `if` with a matching LHS.
- The counter resets at every non-`if` stmt (so that a split by block
  is not kept artificially small).
- Diagnostic text with a pointer to a concrete refactoring template
  (rpc_dispatcher.aiml) so that users easily find the map variant.
