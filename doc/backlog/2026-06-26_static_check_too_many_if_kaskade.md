# Backlog: Static-Check "if/else-Kaskade-Limit"

**Status:** proposed (2026-06-26)
**Verwandt:** spec/49 §14 (≤3 decls per file), §15 (main composes external)

## Idee

Compiler hard-error wenn in einer einzelnen Funktion mehr als **5
sequentielle `if`/`else if`-Blöcke** auftauchen, die dasselbe
Discriminator-Feld auswerten (Method-Name-Dispatch, enum-string-Switch,
Type-Tag-Switch).

Beispiel das in Zukunft zum Error wird:

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

## Diagnostic-Text (Vorschlag)

```
ERROR: test.aiml:42 (in dispatch_old_school): 6 sequential if-checks
       against `ev.method`. Limit is 5; refactor to:
       • map<string:T> lookup (e.g. RpcDispatcher pattern), or
       • polymorphic interface with `implements:` + per-class behavior.
       See spec/16_classes.md §7 and sources/game/rpc_handlers/
       rpc_dispatcher.aiml for the canonical replacement.
```

## Warum

If/else-Kaskaden über ein Discriminator-Feld sind ein Symptom für
fehlendes Polymorphismus oder eine fehlende Lookup-Table. Sie skalieren
schlecht (O(n) statt O(1)), splitten Add-Logik auf 6+ Stellen, sind
schwerer testbar (cross-cutting state in einer Riesen-Function), und
sind ein Magnet für Copy/Paste-Bugs. Der Static-Check kanalisiert
Entwickler früh auf die Map/Interface-Variante.

## Scope

- **Counter pro Funktion**, nicht pro File (mehrere kleine
  Helper-Switches sollen erlaubt bleiben).
- **Discriminator-Erkennung**: `if`-Sequenz zählt nur dann, wenn alle
  Conditions die Form `{ eq: [<same_lhs>, <literal>] }` haben.
  Anders-aufgebaute `if`-Folgen (z.B. range-checks) sind ausgenommen.
- **Threshold = 5** (vorher implementieren, Wert ist verhandelbar).
- **Kein `static_off`-Escape** — analog zu den anderen Caps (siehe
  feedback_quality_over_shortcuts).
- **`else if` zählt mit** sobald implementiert; aktuelles AIML hat
  keine `elif`-Syntax, daher zählt nur die `if`-Sequenz.

## Test-Fälle (für tests/)

1. `test_static_too_many_if_kaskade.aiml`: 6 `if {eq: [ev.method, "X"]}`
   in einer Funktion → exit 1 mit dem Hinweis.
2. `test_static_if_kaskade_under_limit.aiml`: 5 `if`-Conditions
   → compile OK.
3. `test_static_if_kaskade_different_lhs.aiml`: 7 `if`-Conditions,
   aber verschiedene Discriminators (`a.x`, `b.y`, …) → compile OK.

## Implementation Notes

- Im `ast_compiler_stmt.cpp::visitFunction` über die top-level Stmts
  iterieren; jeden `if` mit ausgleichendem LHS in einen Counter.
- Counter resettet bei jedem nicht-`if`-Stmt (so dass split-by-block
  nicht künstlich klein gehalten werden).
- Diagnostic-Text mit Pointer auf konkrete refactor-Vorlage
  (rpc_dispatcher.aiml) damit User die Map-Variante leicht findet.
