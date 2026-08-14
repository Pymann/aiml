# Chapter 45 — AionDB: Secondary Indexes via `db_indexed`

> **Core idea:** Mark a model field with `{ db_indexed: true }` to
> auto-maintain a secondary index sidecar Kv.  Lookups via
> `findings_by("field", value)` (1:N → `list<string>`) and
> `finding_by("field", value)` (unique → `string`) are O(1) Hash-Lookups
> instead of O(n) linear scans, validated at compile-time.

---

## Why

Linear scans over `Kv<string:Model>` for queries like *"all items with
`name='Hammer'`"* are the dominant cost in DataCreatures' tool-check,
tag-pool match, and recipe-search code paths.  Classic DB engines solve
this with secondary B-Tree/Hash indexes; AionDB now provides the same
primitive via a single field modifier — no separate index DDL, no
out-of-band sidecar wiring.

## Surface

### Declaration

```aiml
- model:
    name: Item
    fields:
      - [item_id, string]
      - [name,    string,       "", { db_indexed: true }]                  # 1:N exact match
      - [type,    i32,          0,  { db_indexed: true }]                  # i32 also supported
      - [tags,    list<string>, "", { db_indexed: true }]                  # list-field: each elem indexed
      - [email,   string,       "", { db_indexed: true, unique: true }]   # 1:1
      - [tag_lc,  string,       "", { db_indexed: "lower" }]               # case-insensitive
```

`db_indexed` accepts `true` (default mode) or the string `"lower"`.
The optional `unique: true` flag makes the index 1:1.

Supported field types in V1: `string`, `i32`, `i64`, `list<string>`.
Other types compile-error at the `findings_by` site, not at the
declaration (allows forward-compat without breaking model parsing).

### Lookup

```aiml
# 1:N — returns list<string> of kv-keys whose field equals the value
- decl: [hammers, list<string>, { call: [items.findings_by, "name", "Hammer"] }]

# i32 field
- decl: [tools,   list<string>, { call: [items.findings_by, "type", 3] }]

# list-field — search any tag membership
- decl: [rare,    list<string>, { call: [items.findings_by, "tags", "rare"] }]

# unique — returns the single kv-key or "" if none
- decl: [user_id, string,       { call: [users.finding_by,  "email", "alice@example.com"] }]
```

### Compile-Time Checks

The 1st arg must be a string literal in V1 (dynamic field names are
out-of-scope; they would defeat the at-compile validation).  Errors:

| Condition | Error |
|---|---|
| 1st arg not a string literal | `findings_by: first arg must be a string literal …` |
| Field doesn't exist on value-type | `findings_by: field 'X' does not exist on model 'Y'` |
| Field not `db_indexed` | `findings_by: field 'X' on model 'Y' is not db_indexed.  Add \`{ db_indexed: true }\` …` |
| `finding_by` on non-`unique` field | `finding_by: field 'X' on model 'Y' is not unique.  Use findings_by, or add \`unique: true\` …` |
| Field type unsupported (e.g. `f32`) | `findings_by: field 'X' has type 'f32' which is not supported by db_indexed in V1` |

`findings_by` on a `unique` field is legal — it returns a list with at
most 1 element.  Symmetric semantics, type-in-the-name.

## Semantics

### List-field indexing — SET semantics

`list<T>` indexed fields are treated as mathematical sets.  Duplicates
in the source list (e.g. user calls `push("rare")` twice) are collapsed
when computing index entries — the question *"does this item have tag
`rare`?"* doesn't care about multiplicity.  Source field stays
unchanged; only the sidecar is dedup'd.

On update of a list field: `to_remove = old_set − new_set`,
`to_add = new_set − old_set`.  Idempotent index writes ensure no
duplicate `kv_key` per `field_value` in the 1:N sidecar.

### Lowercase mode

`{ db_indexed: "lower" }` — search keys are lowercased before lookup.
The record itself preserves its original casing (`get_copy` returns
the original).  Useful for case-insensitive name searches without
normalizing the user-facing value.

### Unique mode

`{ db_indexed: true, unique: true }` — the index maps each field value
to at most one kv-key.  `set` with a duplicate value silently
overwrites the previous mapping (last-writer-wins).  `finding_by`
returns `""` when no match (not null).

## Architecture

```
┌───────────────────────────────┐
│  User AIML                    │  ← `findings_by("name", "Hammer")`
└───────────────┬───────────────┘
                │ compile-time:
                │   • validate literal field name
                │   • check `db_indexed`, `unique`
                │   • emit lookup IR
                ▼
┌───────────────────────────────┐
│  Kv<K:V> method dispatcher    │  ast_compiler_call.cpp
└───────────────┬───────────────┘
                │ runtime:
                │   • aiml_db_index_lookup(kv, "name") → AimlIndex*
                │   • aiml_db_index_findings(idx, "Hammer", &list) → list<string>
                ▼
┌───────────────────────────────┐
│  AimlIndex (in-memory)        │  db_runtime.cpp
│   unique  → map<str,str>      │
│   1:N     → map<str,vec<str>> │
└───────────────────────────────┘
```

### Sidecar lifecycle

- **Open** (`db.open_kv` site, compile-time emit):
  - Open the main Kv as usual
  - For each `db_indexed` field on the value-type model:
    - `aiml_db_index_open_for_kv(kv, field_name, unique, mode)` — registers the index
    - `aiml_db_index_backfill(kv, idx, field_offset, field_tag)` — populates from existing main-Kv entries
- **`kv.set(key, val)`**:
  - Load old via `aiml_db_kv_get`; if not null, `index_remove(old_field_val, key)` for each indexed field
  - Main set
  - `index_add(new_field_val, key)` for each indexed field
- **`kv.delete(key)`**:
  - Load val; `index_remove(val.field, key)` for each indexed field
  - Main delete
- **`findings_by("f", v)`** / **`finding_by("f", v)`**:
  - `aiml_db_index_lookup(kv, "f")` → cached handle
  - `aiml_db_index_findings(idx, v, &out)` / `aiml_db_index_finding(idx, v)`

### Persistence

**V1: in-memory only.**  Indexes are re-populated from the main Kv at
every `db.open_kv` via `aiml_db_index_backfill`.  This guarantees
consistency without any in-band markers in user keyspace.

V2 (out-of-scope, see backlog): if backfill becomes expensive on
large Kvs we add a separate meta-Sidecar `<main>__<field>__meta` for
LSN tracking — *never* an in-band marker in the data sidecar because
user-indexed values may legally contain arbitrary strings.

## C-API (`src/sys/db_runtime.h`)

```c
void* aiml_db_index_open_for_kv(void* parent_kv, const char* field_name,
                                 int32_t unique, const char* mode);
void* aiml_db_index_lookup(void* parent_kv, const char* field_name);
void  aiml_db_index_add(void* idx, const char* field_val, const char* kv_key);
void  aiml_db_index_remove(void* idx, const char* field_val, const char* kv_key);
void  aiml_db_index_findings(void* idx, const char* field_val, void* out_list);
const char* aiml_db_index_finding(void* idx, const char* field_val);
void  aiml_db_index_backfill(void* kv, void* idx, int32_t field_offset, int32_t field_tag);
```

`field_tag` for backfill: `0`=string, `1`=i32, `2`=i64, `3`=list<string>.

## Out-of-Scope (V1)

- **Composite indexes** `(name, type)` — workaround: hand-maintain a
  `composite_key` field with `db_indexed: true`
- **Range queries** — would need a B-Tree sidecar instead of Hash
- **Dynamic field names** at `findings_by` call — must be literal
- **Index persistence** — see V2 note above
- **`skip_empty: true`** — parsed but not yet wired; default behavior
  indexes everything including `""`/`0`

## Tests

`tests/test_db_indexed_*.aiml` — nine tests covering basic, list-field,
unique, lower mode, update-diff, reopen-survival (write+read pair),
backfill on first-time enable (write+read pair).  All passing.
