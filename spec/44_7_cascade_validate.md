# Cascade Validation API (`cascade.validate` / `cascade.explain`)

A `- cascade:` declaration defines a bytecode wire-format: each node has an `id`
(the first `i32` in the row) followed by typed payload fields (enums, nested
cascades, or numeric aliases).  See `44_6_prefabs.md` for the UI-rendering side.

## Node declaration forms

Nodes may be written in three interchangeable spellings.  All produce the same
`[id, field…]` wire-format; pick whichever reads best.

```aiml
- cascade:
    name: Effect
    opcode: EffectCode                 # optional — see below
    nodes:
      # 1. list form (canonical when `opcode:` is set): the head is the enum member,
      #    the id is resolved from it — no hand-maintained integer.
      - [deal_damage, OwnerType, TargetType, Value]
      # 2. compact form: node name is the map key; a numeric leading element is the
      #    explicit id, a non-numeric leading element is field 0 (id from the key).
      - draw_card:   [DeckType, Value]      # id from `opcode:` enum member `draw_card`
      - counter_trap: [48]                  # explicit id, no payload
      # 3. explicit form: `name: { id: N, fields: [[fname, Type], …] }` (full control).
```

### `opcode:` — enum-typed node ids

`opcode: <Enum>` types element 0 (the node id) by that enum.  Instead of a
hand-maintained integer that mirrors the enum "by convention", the node head is
the enum member and the **compiler resolves the id from the enum and verifies
it**:

- list / compact-name form (no explicit id) → the id is looked up from the member.
- explicit id present → the compiler checks `id == Enum.member`, erroring on drift.
- a node whose head is not a member of the enum, or an `opcode:` naming an
  undeclared enum, is a hard compile error.

A leading `Enum.` prefix on a head (`EffectCode.deal_damage`) is accepted and
stripped.  Without `opcode:`, every node must carry an explicit numeric id (the
legacy form; still fully supported).

At runtime the compiler registers every cascade's node/field structure (and every
enum's valid member values) into global tables.  The `cascade` namespace exposes
these tables for **validating an incoming `list<i32>` against a cascade** — i.e.
checking that a bytecode row is a well-formed instance of that cascade before it is
trusted (e.g. a trigger/effect block loaded from persisted card data).

## `cascade.validate(cascade_name, row) → i32`

- **`cascade_name`** — `string`, the declared cascade name (e.g. `"Trigger"`).  A
  string literal or a runtime string.
- **`row`** — `list<i32>`, one bytecode row `[id, field0, field1, …]`.
- **Returns** `i32`:
  - `-1` — the row is a **valid** instance of the cascade (exactly one node,
    consuming the whole list).
  - `>= 0` — the **0-based index** in `row` of the first element that fails
    validation (unknown node id, out-of-range enum value, truncated payload, or
    trailing garbage).
  - `-2` — the cascade name is unknown (not declared).

Validation rules, applied recursively from index 0:
1. `row[0]` must match a node `id` of the cascade; otherwise invalid at `0`.
2. For each declared field of that node, consume the next element(s):
   - field type is an **enum** → the value must be one of that enum's declared
     member values, else invalid at that index.
   - field type is another **cascade** → recurse into it in place.
   - field type is a numeric **alias**/`i32` → any value is accepted.
3. After the node (and any nested cascades) are consumed, the index must equal
   `row.len` — trailing elements are invalid at the first trailing index.

## `cascade.explain(cascade_name, row) → string`

Returns a human-readable reason for the first validation failure, or `""` when the
row is valid.  Example: `pos 2: node 'on_card_played' field 'CardType' — value 99
is not a valid CardType`.  Intended for logging / diagnostics next to a
`cascade.validate` guard.

## `cascade.field_indices(cascade_name, row, field_type) → list<i32>`

Locate, **without hard-coding per-node layouts**, where a given field type sits in
a bytecode row — i.e. answer "at which position(s) — if any — does this enum/cascade
appear in the node that `row[0]` selects?".

- **`cascade_name`** — `string`, the declared cascade (e.g. `"Effect"`).
- **`row`** — `list<i32>`, one bytecode row `[id, field0, field1, …]`.
- **`field_type`** — `string`, a field type name as written in the cascade's node
  declarations — an enum (`"TargetType"`), a nested cascade (`"OwnerRef"`, `"Value"`),
  or a numeric alias (`"VALUE"`).
- **Returns** `list<i32>`: the 0-based `row` indices of **every** field of that type
  in the selected node, in declaration/row order.  A field type may recur in one
  node (e.g. `revive: [47, OwnerRef, OwnerRef, GameZone, CardType]` → `[1, 2]`), so a
  list — not a single index — is returned.  An **empty list** means the node has no
  such field (or the cascade name is unknown / the row can't be walked).

The node is walked exactly like `validate`: the id is consumed, then each declared
field consumes its slot(s) — a nested cascade recurses in place (and is itself
searched), an enum/alias consumes one slot.  This keeps the returned indices aligned
with the same offsets `validate` checks.

### Example — is this effect a player-targeted one, and where is the target?

```aiml
- import: "sources/models/cascades.aiml"      # declares the Effect cascade

# block = [10, OwnerRef.opponent, TargetType.creature, Value.fixed, 5]  (deal_damage)
- decl: [tgt_pos, list<i32>, { call: [cascade.field_indices, "Effect", block, "TargetType"] }]
- if:
    condition: { gt: [tgt_pos.len, 0] }        # deal_damage/heal_damage → yes; modify_stat → no
    then:
      - decl: [tp_idx,   i32, { at: [tgt_pos, 0] }]
      - decl: [target_t, i32, { at: [block, tp_idx] }]   # the TargetType value, layout-driven
      # … open a target request for OwnerRef × target_t …
```

This replaces per-effect-code branches (`if code == deal_damage or heal_damage …`)
with a query against the declared wire format, so new targeted effects need only a
cascade node declaration — no callsite change.

## Enumeration API — `cascade.count` / `cascade.row_at`

Enumerate **every possible row** of a cascade: the cross-product over each opcode of
its enum-member field values, with nested cascade fields branching per sub-opcode.
This is the corpus generator behind coverage tooling — e.g. run every `Effect` /
`Trigger` / `Cost` code through the ability-text generator (find blocks that render
empty/wrong) or through the effect/trigger processor (smoke test).

```
cascade.count (name: string, start_op?: i32, end_op?: i32) → i64
cascade.row_at(name: string, k: i64, start_op?: i32, end_op?: i32) → list<i32>
```

- **Field expansion.** An **enum** field expands over all its members; a **nested
  cascade** field recurses (each sub-opcode is its own branch); a **value/alias**
  field (`i32`, `VALUE`, `COUNT`, …) contributes **exactly one** representative (its
  `default`, else `1`) — numbers do not branch text/execution and would be unbounded.
- **`count`** returns the total number of enumerable rows. It **saturates** at the
  `i64` maximum rather than wrapping (deeply-nested cascades — e.g. `Effect.modify_stat`
  with nested `Value` inside `Duration` — reach into the billions). Unknown cascade → `0`.
- **`row_at(k)`** returns the `k`-th row (a valid `[opcode, field…]` wire row, mixed-radix
  decoded — O(depth), nothing materialised). `k` out of `[0, count)` → empty list.
- **`start_op`/`end_op`** (optional) bound which **top-level** opcode ids participate
  (inclusive); omit for all. They apply only at the top level — nested cascades always
  enumerate all their nodes. This lets you walk one opcode family at a time
  (`count(name, id, id)` / `row_at(name, k, id, id)`) instead of the full space.

There is **no iterator object**: the ranking is stateless random access, so the loop is
the iterator (and holds only the current row):

```aiml
- import: "sources/models/cascades.aiml"
- decl: [total, i64, { call: [cascade.count, "Effect"] }]
- decl: [k, i64, 0]
- while:
    condition: { lt: [k, total] }
    body:
      - decl: [row, list<i32>, { call: [cascade.row_at, "Effect", k] }]
      - decl: [txt, string, { call: [format_effect, row] }]   # or run the processor
      - if: { condition: { eq: [txt.len, 0] }, then: [{ log.warn: ["empty render: ${row}"] }] }
      - set: [k, { +: [k, 1] }]
```

Verified by `tests/test_cascade_enumerate.aiml` (count, row decoding, range filter,
out-of-range + unknown-cascade edge cases).

## Schema introspection — `cascade.type_range` / `cascade.describe`

Where enumeration walks the concrete rows, introspection reports the **schema** — the
set of possible values a field type may take, and the datatype combination of each
opcode. Both return **serialisable** containers (feed straight into `sys.serialize`),
so they double as review/documentation generators (print the possible combos into a
`.txt`, diff schema changes across builds).

```
cascade.type_range(type: string)                       → list<i32>
cascade.describe  (cascade: string, opcodes?: list<i32>) → list<string>
```

- **`type_range(type)`** — the possible values of a single field type: an **enum**
  yields its member values (e.g. `TargetType` → `[1,2,3,…]`); a **nested cascade**
  yields its sub-node ids; a **value/alias/i32** type yields the **empty** list (it does
  not range over a fixed set). Unknown type → empty.
- **`describe(cascade, opcodes?)`** — one `list<string>` describing the datatype
  combinations of the selected opcodes (omit `opcodes`, or pass an empty list, for
  **all**). **Element 0** is the opcode enum name (e.g. `"EffectCode"`); each following
  element is one opcode line

  ```
  [<id>]<node>  [<range>]<Type>  [<range>]<Type> …
  ```

  where `<range>` is the comma-joined `type_range` of that field (empty `[]` for
  value/alias fields). `opcodes` filters to the listed top-level node ids.

```aiml
- import: "sources/models/cascades.aiml"
# possible values of a field type
- decl: [vals, list<i32>, { call: [cascade.type_range, "TargetType"] }]   # → [1,2,3,…]
# full schema of one opcode family, printed for review
- decl: [ops, list<i32>]
- call: [ops.push, 20]                                                    # gain_passive
- decl: [lines, list<string>, { call: [cascade.describe, "Effect", ops] }]
- decl: [i, i32, 0]
- while:
    condition: { lt: [i, lines.len] }
    body:
      - log: [{ at: [lines, i] }]      # "[20]gain_passive [2,7,9]TargetType [41,23,…]Passive …"
      - set: [i, { +: [i, 1] }]
```

Verified by `tests/test_cascade_describe.aiml` (type_range for enum + nested-cascade,
describe Element-0 opcode-enum, per-field ranges, opcode filter).

## Example

```aiml
- import: "sources/models/cascades.aiml"   # declares the Trigger cascade + enums

- decl: [row, list<i32>]
- call: [row.push, 9]   # ON_CARD_PLAYED
- call: [row.push, 2]   # OwnerRef.opponent
- call: [row.push, 1]   # CardType.creature
- decl: [bad, i32, { call: [cascade.validate, "Trigger", row] }]
- if:
    condition: { gte: [bad, 0] }
    then:
      - call: [log.warn, "invalid Trigger row: ${{ call: [cascade.explain, \"Trigger\", row] }}"]
```

## Notes

- The cascade + enum registries are populated at program start for every declared
  `- cascade:` / `- enum:`, so validation works **headless** (JIT + compiled) as
  long as the declaring file is imported.
- Validation is structural (arity + enum-membership).  Semantic constraints beyond
  the wire format (e.g. "value must be > 0") are out of scope.
- Future work: a compile-time static check that flags statically-constructed rows
  which cannot form a valid cascade (see backlog).
