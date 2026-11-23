# AionDB indexed fields (`db_indexed` modifier)

**Status**: backlog (2026-06-06)
**Motivation**: lookups like "all items with `name='Hammer'`" are currently
a linear scan in every caller (DataCreatures tool check, tag pool match,
recipe search etc.). DB engines solve this with a secondary index (B-tree/hash) —
AionDB does not have the pattern. We build it in as a compiler+runtime feature.

## Vision

```aiml
- model:
    name: Item
    fields:
      - [item_id,   string]
      - [name,      string, "", { db_indexed: true }]      # 1:N exact match
      - [item_type, i32,    0,  { db_indexed: true }]      # i32 too
      - [tags,      list<string>, [], { db_indexed: true }] # list = index every tag
```

Builtin methods on `Kv<string:Item>` (uniform, with the field name as the 1st arg):

```aiml
- decl: [ids,   list<string>, { call: [items.findings_by, "name", "Hammer"] }]
- decl: [tools, list<string>, { call: [items.findings_by, "item_type", 3] }]
- decl: [rare,  list<string>, { call: [items.findings_by, "tag", "rare"] }]    # list field

# unique → finding_by (singular) returns string instead of list:
- decl: [user_id, string,    { call: [users.finding_by,  "email", "alice@example.com"] }]
```

**Compile checks** (the field name must be a string literal in V1):
- the field exists in the value model
- the field has `db_indexed: true`
- `finding_by` (singular) requires `unique: true`
- `findings_by` (plural) also allows unique fields, then returns a list with at most 1 element

Optional modifiers:
- `{ db_indexed: "lower" }` — case-insensitive (the index stores a lower-case key)
- `{ db_indexed: true, unique: true }` — 1:1, `finding_by` legal

## Implementation

### Compiler

1. **Field modifier parser** (class_builder.cpp / model parser): recognise the `db_indexed` key + store it in ClassInfo/ModelInfo (`bool is_db_indexed`, `string db_indexed_mode`).
2. **db.open_kv site**: per indexed field emit an additional `db.open_kv("<table>__<field>", ...)`. Sidecar Kvs are `Kv<string:list<string>>` (1:N) or `Kv<string:string>` (unique).
3. **<kv>.set hook**: before the actual data set, load the old version (if it exists), then apply the diff to the index Kv per indexed field:
   - If `old.f != new.f` (or a list diff): `index[old.f]` without the id, `index[new.f]` with the id.
4. **<kv>.remove hook**: load the current version, take the IDs out of all index Kvs.
5. **Builtin methods** `findings_by(field_lit, value)` and `finding_by(field_lit, value)`: dispatch in the Kv method handler in `ast_compiler_call.cpp`. The compiler validates the field literal (exists, is `db_indexed`, for `finding_by` additionally `unique:true`), resolves the sidecar handle and emits a single index Kv lookup. With a dynamic 1st arg (V1) → hard error.

### Runtime

The current `db.open_kv` implementation does not need to change — the sidecar Kvs are completely normal Kvs. Only the compiler emits the hooks around them.

### Backfill / consistency

- **V1: indexes are pure in-memory state** (no WAL per index).  At the
  `db.open_kv` call the `AimlIndex` is always empty; backfill logic
  iterates over the main Kv and fills the maps anew.  No
  in-band marker, hence no risk of collision with user data.
- **Crash between the main set and the index set**: irrelevant in V1, because the
  index is freshly backfilled every time anyway.

### Persisting the index (V2 / out of scope for V1)

If backfill becomes expensive (large main Kvs, frequent restarts), we persist
the index itself.  **Reserved-key encoding is ruled out**, because
user values may contain arbitrary strings, including strings that look like sentinels.
**Chosen mechanism**: a separate meta sidecar
`<main>__<field>__meta` with `last_main_lsn`, `version` etc.  That way
the data sidecar `<main>__<field>` stays purely for user index entries.

### Edge cases

- **Empty value indexing**: `{ db_indexed: true, skip_empty: true }` — default skip_empty=false, otherwise everything accumulates under `""`.
- **List<T> fields — SET semantics**: duplicates in the source field are
  collapsed by the index (the membership question has no multiplicity).  The diff
  on update works on sets: `to_remove = old_set − new_set`,
  `to_add = new_set − old_set`.  The source field stays unchanged by the user
  (it can still contain duplicates), only the sidecar
  contains each value exactly once per record.  `index_add` is
  idempotent (a no-op if the record is already registered).
- **Range queries**: currently out of scope. Would need a B-tree sidecar instead of a hash Kv.
- **Composite indexes** (`(name, item_type)`): out of scope for V1. Workaround: maintain an additional field `composite_key` with `db_indexed: true` yourself.

## Effort estimate

| Part | Order of magnitude |
|---|---|
| Schema parser extension | 1-2h |
| Creating the sidecar Kvs in `db.open_kv` | 3-4h |
| Set/remove hooks (old vs new diff) | 4-6h |
| Auto-emitting the `find_by_<field>` methods | 3-4h |
| Backfill logic | 2-3h |
| Tests (single field, list field, unique, with migration) | 3-4h |

→ **2-3 sessions** for a clean first version with tests.

## Beneficiaries in the DataCreatures codebase

- `harvest.aiml` tool check (the current trigger)
- `discovery.aiml` `ensure_recipe_item` / `ensure_material_item` (idempotent check)
- `tag_pools.aiml` (static lists, less necessary)
- Skill lookups in `discovery.aiml` (`ensure_active_skill` dedup)
- A future `find_by_type` in the crafting engine
