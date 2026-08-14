# AionDB Indexed Fields (`db_indexed` Modifier)

**Status**: Backlog (2026-06-06)
**Motivation**: Lookups wie "alle Items mit `name='Hammer'`" sind aktuell
ein linearer Scan in jedem Caller (DataCreatures Tool-Check, Tag-Pool-Match,
Recipe-Search etc.). DB-Engines lösen das per Sekundärindex (B-Tree/Hash) —
AionDB hat den Pattern nicht. Wir bauen ihn als Compiler+Runtime-Feature ein.

## Vision

```aiml
- model:
    name: Item
    fields:
      - [item_id,   string]
      - [name,      string, "", { db_indexed: true }]      # 1:N exact match
      - [item_type, i32,    0,  { db_indexed: true }]      # auch i32
      - [tags,      list<string>, [], { db_indexed: true }] # list = jedes Tag indexen
```

Builtin-Methoden auf `Kv<string:Item>` (uniform, mit Field-Name als 1. Arg):

```aiml
- decl: [ids,   list<string>, { call: [items.findings_by, "name", "Hammer"] }]
- decl: [tools, list<string>, { call: [items.findings_by, "item_type", 3] }]
- decl: [rare,  list<string>, { call: [items.findings_by, "tag", "rare"] }]    # list-field

# unique → finding_by (Singular) returnt string statt list:
- decl: [user_id, string,    { call: [users.finding_by,  "email", "alice@example.com"] }]
```

**Compile-Checks** (Field-Name muss String-Literal in V1):
- Field existiert im Value-Model
- Field hat `db_indexed: true`
- `finding_by` (Singular) verlangt `unique: true`
- `findings_by` (Plural) erlaubt auch unique-Felder, returnt dann list mit max 1 Element

Optional Modifier:
- `{ db_indexed: "lower" }` — case-insensitive (Index speichert lowercase key)
- `{ db_indexed: true, unique: true }` — 1:1, `finding_by` legal

## Implementierung

### Compiler

1. **Field-Modifier-Parser** (class_builder.cpp / model parser): `db_indexed`-Key erkennen + im ClassInfo/ModelInfo speichern (`bool is_db_indexed`, `string db_indexed_mode`).
2. **db.open_kv-Site**: pro indexed field eine zusätzliche `db.open_kv("<table>__<field>", ...)` emittieren. Sidecar-Kvs sind `Kv<string:list<string>>` (1:N) oder `Kv<string:string>` (unique).
3. **<kv>.set-Hook**: vor dem eigentlichen Daten-Set die alte Version laden (falls existent), dann pro indexed field den Diff aufs Index-Kv anwenden:
   - Wenn `alt.f != neu.f` (oder list-Diff): `index[alt.f]` ohne id, `index[neu.f]` mit id.
4. **<kv>.remove-Hook**: aktuelle Version laden, IDs aus allen Index-Kvs rausnehmen.
5. **Builtin-Methoden** `findings_by(field_lit, value)` und `finding_by(field_lit, value)`: dispatch im Kv-Methoden-Handler in `ast_compiler_call.cpp`. Compiler validiert das Field-Literal (existiert, ist `db_indexed`, bei `finding_by` zusätzlich `unique:true`), löst den Sidecar-Handle auf und emittiert einen einzelnen Index-Kv-Lookup. Bei dynamischem 1. Arg (V1) → Hard-Error.

### Runtime

Aktuelle `db.open_kv`-Implementierung muss nicht geändert werden — die Sidecar-Kvs sind ganz normale Kvs. Nur der Compiler emittiert die Hooks drumherum.

### Backfill / Konsistenz

- **V1: Indexe sind reine In-Memory-State** (kein WAL pro Index).  Beim
  `db.open_kv` Aufruf ist der `AimlIndex` immer leer; Backfill-Logik
  iteriert über das Main-Kv und füllt die Maps neu.  Kein
  In-Band-Marker, daher kein Kollisionsrisiko mit User-Daten.
- **Crash zwischen Main-set und Index-set**: irrelevant in V1 weil der
  Index sowieso jedes Mal frisch backfilled wird.

### Persistierung des Index (V2 / out-of-scope V1)

Wenn Backfill teuer wird (große Main-Kvs, häufige Restarts), persistieren
wir den Index selbst.  **Reserved-Key-Encoding scheidet aus** weil
User-Werte beliebige Strings inkl. Sentinel-aussehender Strings enthalten
dürfen.  **Gewählter Mechanismus**: separater Meta-Sidecar
`<main>__<field>__meta` mit `last_main_lsn`, `version` etc.  So bleibt
der Daten-Sidecar `<main>__<field>` rein für User-Index-Einträge.

### Edge Cases

- **Empty value indexing**: `{ db_indexed: true, skip_empty: true }` — default skip_empty=false, sonst sammelt sich alles unter `""`.
- **List<T>-Felder — SET-Semantik**: Duplikate im Quellfeld werden vom
  Index kollabiert (Membership-Frage hat keine Multiplicität).  Diff
  beim Update arbeitet auf Mengen: `to_remove = old_set − new_set`,
  `to_add = new_set − old_set`.  Das Quellfeld bleibt vom User
  unverändert (kann weiterhin Duplikate enthalten), nur der Sidecar
  enthält jeden Wert exakt einmal pro Record.  `index_add` ist dabei
  idempotent (no-op wenn der Record schon registriert ist).
- **Range-Queries**: aktuell out-of-scope. Würde B-Tree-Sidecar brauchen statt Hash-Kv.
- **Composite Indexes** (`(name, item_type)`): out-of-scope für V1. Workaround: zusätzliches Feld `composite_key` mit `db_indexed: true` selbst pflegen.

## Aufwand-Schätzung

| Teil | Größenordnung |
|---|---|
| Schema-Parser-Erweiterung | 1-2h |
| `db.open_kv` Sidecar-Kvs anlegen | 3-4h |
| Set/Remove-Hooks (alt vs neu Diff) | 4-6h |
| `find_by_<field>`-Methoden auto-emittieren | 3-4h |
| Backfill-Logik | 2-3h |
| Tests (single-field, list-field, unique, mit Migration) | 3-4h |

→ **2-3 Sessions** für saubere Erstversion mit Tests.

## Beneficiaries im DataCreatures-Codebase

- `harvest.aiml` tool-check (aktueller Auslöser)
- `discovery.aiml` `ensure_recipe_item` / `ensure_material_item` (idempotent check)
- `tag_pools.aiml` (statische Listen, weniger nötig)
- Skill-Lookups in `discovery.aiml` (`ensure_active_skill` dedup)
- Künftige `find_by_type` in Crafting-Engine
