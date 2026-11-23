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

### Cost of index maintenance — and what the bucket size may not do

`kv.set` is **O(1) amortized** (spec/31 §9), and that holds *including* the
index hook.  Normatively, per indexed field of the written row:

| Step | Complexity | Must not depend on |
|---|---|---|
| remove the old field value from the sidecar | O(1) amortized | number of kv-keys sharing that value |
| add the new field value to the sidecar | O(1) amortized | number of kv-keys sharing that value |
| `findings_by(f, v)` | O(hits) | number of kv-keys **not** sharing that value |

The second column of exclusions is the point.  A 1:N sidecar bucket holds every
kv-key that carries one field value, so a **low-cardinality** field — `sorte`,
`kind`, `status`, a language tag — collects a bucket that grows to the size of
the whole store.  If maintaining that bucket were linear in its length, each
`set` would cost O(rows) and a write batch would be **quadratic**; filling a
store would get slower the fuller it got, with no bound in sight and nothing in
the user's own code to blame.

That is not hypothetical: until 2026-11-04 `aiml_db_index_add` scanned the
bucket with `std::find` to deduplicate, and `aiml_db_index_remove` compacted it
with `std::remove`.  Measured with 4 000 `set` per block into one growing store
(`tests/kv_set_scaling/`):

| after … rows | no indexed field | indexed, all values distinct | indexed, few values |
|---|---|---|---|
| 4 000 | 27 ms | 41 ms | **487 ms** |
| 16 000 | 20 ms | 32 ms | **1 754 ms** |
| 32 000 | 22 ms | 31 ms | **5 074 ms** |

Flat, flat, linear — so the third column is quadratic in total.  At 32 000 rows
one `set` cost 1.3 ms; reading German Wikipedia into cai, whose sentence index
writes over a thousand keys per article, this alone turned a 69 s batch into one
that had not finished after 600 s.

The implementation therefore keeps per bucket, alongside the keys in insertion
order, **the position of each key within it** — add is a map insert plus a
`push_back`, remove marks a grave and compacts only once graves outnumber live
entries (amortized O(1)).  `findings_by` skips the graves and so still yields
**insertion order**, which is the order it has always returned and which callers
may rely on.

Choosing a high-cardinality field is good practice — a bucket of one is cheaper
than a bucket of a million whatever the complexity — but it is *not* a condition
for the cost promise above.  An index on a field with three distinct values is a
legitimate, supported thing to declare.

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

### Persistence — `index_dump: true` (spec/31 §6.2.1)

The V1 version kept secondary indexes **only in memory**: on every `db.open_kv`,
`aiml_db_index_backfill` rebuilds them from the main Kv and for that decodes
every row to read ONE field.  That was the right choice — it needs no
marker in the user key space and can never go stale — but for a large
Kv it is the dominant part of opening.  Measured on cai (5.83 million facts,
6 indexed fields, 2026-11-06):

| On open | Time | Memory |
| --- | --- | --- |
| Primary index, with dump (spec/31 §6.2.1) | 0.12 s | 720 MB file mapping |
| Filling the secondary indexes | ~250 s | 1867 MB **anonymous** |

So the primary index dump kept its promise and in doing so made visible
where the load really lies.

**`index_dump: true` covers both kinds of index** — the primary index of the WAL and
every `db_indexed` secondary index of the same store.  There is no separate
keyword for it: a secondary index is an index of this store, and whoever has the index
stored along wants all of it.  Without `index_dump` the V1 behaviour remains,
unchanged.

#### File `<name>__<field>.kvsidx`

One file per (store, field), not a shared one: the fields are opened
individually, and a damaged or stale secondary index should only make its
own field rebuild, not drag the others along.

```
Header (64 bytes)
  char     magic[8]    "AIMLKVSX"
  uint32   version     1
  uint32   flags       bit 0: unique,  bit 1: mode == "lower"
  uint64   wal_dev     \
  uint64   wal_ino      |  stamp of the main WAL, exactly as in
  uint64   wal_used     |  spec/31 §6.2.1
  uint64   wal_fp      /
  uint32   val_count   distinct field values
  uint32   key_count   pairs (field value, kv key)
  uint64   reserved    0

val_count x field-value entry (16 bytes, sorted by the NORMALISED value)
  uint32   val_off, val_len     position in the string block
  uint32   key_first, key_cnt   window into the key array

key_count x key entry (8 bytes, in insertion order)
  uint32   key_off, key_len

String block: field values and kv keys, each with a terminating 0
```

Sorting is by the **normalised** field value, i.e. by `idx_normalize`
(for `db_indexed: "lower"` the lower-cased form), because that is exactly what
the query searches with.  Writing and searching must use the same order —
`kv_idx_cmp` — otherwise the binary search finds nothing, and does so silently.

The strings carry a **terminating 0**, although their length is stored next to them.
That costs one byte per string and spares a bug that already struck once in the primary index:
`finding_by` returns a `const char*` directly,
and a caller reads it as a C string up to the 0.  Without a terminator
every return path would have to copy or would run past the end.

For `unique: true`, `key_cnt` is always 1.  The format stays the same — one
layout fewer is one error path fewer.

#### Opening

`aiml_db_index_open_for_kv` tries to load the file as soon as the store
carries `index_dump`.  It is accepted only if **everything** matches:

1. `magic` and `version`,
2. `flags` equal to the declaration — if `db_indexed: true` becomes
   `unique: true` or `"lower"`, the old dump is unusable,
3. `wal_dev`/`wal_ino` equal to today's main WAL (a checkpoint swaps
   the inode),
4. `wal_used` equal to today's high-water mark,
5. `wal_fp` equal to the fingerprint over exactly this range,
6. all offsets and windows within the file.

If one point fails, the file is discarded and the field is filled completely —
as in V1.  The dump is **never** authoritative over the WAL: every doubt
costs time, never correctness.

If the dump loads, the index reports that, and `aiml_db_index_backfill` returns
immediately.  The generated IR therefore stays unchanged — `open_for_kv` then
`backfill`, as before.

Unlike the primary index there is **no overflow path**: the stamp is
only valid if the WAL is byte for byte the same one the index was
built from.  A secondary index cannot simply be replayed, because a
later `kv.set` on an existing key would also have to take its OLD
field value out of the index, and the old value is only at the old
place in the WAL.  That would be a second, finer path for a gain that
nobody has measured.

#### Writing while reading — copy per field value

After loading, the index lives in the file mapping and is answered from
there.  But a `kv.set` or `kv.delete` may still change it.  For that the rule is:
on the FIRST change to a field value, its window is copied from the dump into
the memory index (`map_1n` or `map_unique`) and the field value is marked as
"kept in memory"; from then on only the memory index applies to it.  A field value
that nobody has touched still costs no
anonymous byte.

A query therefore looks: field value kept in memory → memory index;
otherwise → binary search in the dump.  The two paths are never valid at the same time, and
so there is no case in which a key comes twice or not at all.

#### Writing happens at checkpoint time

`db.checkpoint` is the one moment in which the store is deliberately brought into a
durable, closed state; that is also where the
primary index dump is produced (spec/31 §6.2.1, step 8).  Directly afterwards every
secondary index of this store writes its file, with the stamp of the just renamed WAL.

Consequences one needs to know:

- A store that is written to but **never** checkpointed has no
  secondary index dump.  The next open fills it — V1 behaviour, no
  step back.
- The checkpoint becomes **more expensive**: it has to traverse the indexes and sort the
  field values.  That is the deliberate trade — write once, open cheaply many
  times.  Whoever only fills a store and reads it once gains
  nothing.
- `kv.clear` empties the secondary indexes AND deletes their files.  The emptying applies
  without `index_dump` too: the indexes hang on the store pointer, not on the
  file, and an emptied store must not return hits any more.  The file is deleted
  because a leftover dump would repopulate the emptied store on the next
  open -- the stamp would catch it anyway,
  but relying on a check where deleting is unambiguous would be
  careless.

#### Compatibility — no migration

| Situation | Behaviour |
| --- | --- |
| Old WAL, no `.kvsidx` | full fill, as in V1 |
| `.kvsidx` present, WAL written in the meantime | stamp fails → full fill |
| `.kvsidx` damaged or a foreign version | discarded → full fill |
| `index_dump` switched off again | file stays, is not read |

There is nothing to migrate and no format to upgrade: the file is a purely
derived scratch store.  Whoever deletes it loses time, never data.

#### Measured (2026-11-07)

A `Kv<string:Fakt>` with 300,000 rows and two indexed fields --
`subj` with 15,000 distinct values (small buckets) and `rel` with 40
(buckets of 7500).  What is measured is opening, `len` and two `findings_by`;
`tests/measure_sidx.sh` repeats every measurement:

| Open | Time | RSS |
| --- | --- | --- |
| with both dumps | **0.12 s** | **98 MB** |
| without secondary index dump (primary index dump present) | 1.45 s | 171 MB |
| without any dump | 1.82 s | 189 MB |

So the secondary index dump removes 1.33 s and 73 MB -- here twelve times the
time and 43 % of the memory.  It costs space: 4.7 MB (`rel`) and 5.1 MB
(`subj`) next to a 33.5 MB WAL, roughly 16 bytes per pair plus the strings.

#### Measured on cai (2026-11-07)

These numbers apply to this model.  Hence re-measured on the real
knowledge graph of `cai` -- on a copy of `out/n0511_alles`, 3.7 GB WAL
across 10 stores, with 13 indexed fields (4 on `v2_fakten` alone:
`rel`, `subj`, `obj`, `rel_subj`):

| | |
|---|---|
| Things | 3,549,716 |
| Names | 6,480,381 |
| Facts | 5,797,217 |
| Propositions | 2,295,484 |
| Relations | 42,886 |

What is measured is `cai2 graph stats` -- opening, counters, relation names.  Every
row is a whole process start:

| State | Time | RSS |
| --- | --- | --- |
| no dump (how the memories are stored today) | 251 s | 19.3 GB |
| only primary index dump (`.kvidx`, §6.2.1) | 230 s | 18.1 GB |
| **both dumps** | **3.7 s** (warm 0.70 s) | **4.2 GB** |

The result reverses the expectation: the **primary index** dump removes almost
nothing here (251 → 230 s, 8 %).  The whole gain lies with the **secondary index** dump,
230 s → 3.7 s and 18.1 → 4.2 GB.  The reason is in §6.2.1: the primary index is
built by one WAL pass, whereas `aiml_db_index_backfill` **decodes every
row** once, just to read one field.  That is the time, and it is the
memory.

It costs space: 3.6 GB of `.kvsidx` next to 1.2 GB of `.kvidx` and 3.7 GB of WAL (per
store: `fakten` 1.4 GB, `namen` 960 MB, `props` 954 MB, `dinge` 302 MB).  The
secondary index dump is therefore **larger than the primary index dump** here -- it carries
the field value AND the kv key per pair, while `.kvidx` only holds keys
plus `(offset, length)`.

One-off cost: a checkpoint pass writes all 22 files and took
7:34 (21.7 GB RSS), because it first builds the indexes.  An existing memory
therefore gets the files **at checkpoint time, not on open**.

The cross-check without which none of the numbers means anything: the output of
`graph stats` is **byte-identical** with and without the secondary index dump (`cmp`), and the
fast measurement was repeated after the slow one, so that the order in the
page cache does not explain it.

And the check without which a good number means nothing: `tests/run_sidx_gates.sh`
gate 3 **poisons** a valid file (a key in the string block
rewritten, stamp untouched) and demands that the poisoned value
comes out.  A green unit test alone proves nothing -- if the
stamp fails, the store silently refills, and everything is right anyway.

#### What the dump does NOT solve

- The first fill of a store — there is nothing to load yet.
- Composite indexes, range queries: unchanged except in scope.
- The memory of the **write side**: whoever touches many distinct field values
  copies them one by one into the memory index and ends up where V1
  started.  The dump is built for reading opens.

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

`aiml_db_index_backfill` returns without doing any work if the index has loaded its dump
(see Persistence) — the dump IS the result of the fill,
stamped onto the same WAL bytes.

## Out-of-Scope (V1)

- **Composite indexes** `(name, type)` — workaround: hand-maintain a
  `composite_key` field with `db_indexed: true`
- **Range queries** — would need a B-Tree sidecar instead of Hash
- **Dynamic field names** at `findings_by` call — must be literal
- **`skip_empty: true`** — parsed but not yet wired; default behavior
  indexes everything including `""`/`0`

## Tests

`tests/test_db_indexed_*.aiml` — nine tests covering basic, list-field,
unique, lower mode, update-diff, reopen-survival (write+read pair),
backfill on first-time enable (write+read pair).  All passing.

`tests/test_kv_sidx_*.aiml` — the secondary index dump: writing with checkpoint,
reading from the dump, change after loading (copy per field value), `unique` and
`"lower"`, damaged file, stamp break through a write without
checkpoint, `kv.clear`.  Plus `tests/run_sidx_gates.sh` for what a
single .aiml file cannot check (swapping files between two runs,
measuring size and time).
