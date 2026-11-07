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

Die V1-Fassung hielt Nebenindizes **nur im Speicher**: bei jedem `db.open_kv`
baut `aiml_db_index_backfill` sie aus dem Haupt-Kv neu auf und dekodiert dafuer
jede Zeile, um EIN Feld zu lesen.  Das war richtig gewaehlt — es braucht keine
Marke im Nutzer-Schluesselraum und kann nie veralten —, aber bei einem grossen
Kv ist es der dominierende Teil des Oeffnens.  Gemessen an cai (5,83 Mio Fakten,
6 indizierte Felder, 06.11.2026):

| Beim Oeffnen | Zeit | Speicher |
| --- | --- | --- |
| Primaerindex, mit Dump (spec/31 §6.2.1) | 0,12 s | 720 MB Datei-Abbildung |
| Nebenindizes auffuellen | ~250 s | 1867 MB **anonym** |

Der Primaerindex-Dump hat also sein Versprechen gehalten und dabei sichtbar
gemacht, wo die Last wirklich liegt.

**`index_dump: true` deckt beide Indexarten ab** — den Primaerindex der WAL und
jeden `db_indexed`-Nebenindex desselben Stores.  Es gibt dafuer kein eigenes
Schluesselwort: ein Nebenindex ist ein Index dieses Stores, und wer den Index
mitspeichern laesst, will ihn ganz.  Ohne `index_dump` bleibt es beim
V1-Verhalten, unveraendert.

#### Datei `<name>__<feld>.kvsidx`

Eine Datei je (Store, Feld), nicht eine gemeinsame: die Felder werden einzeln
geoeffnet, und ein beschaedigter oder veralteter Nebenindex soll nur sein
eigenes Feld neu aufbauen lassen, nicht die anderen mitreissen.

```
Kopf (64 Byte)
  char     magic[8]    "AIMLKVSX"
  uint32   version     1
  uint32   flags       Bit 0: unique,  Bit 1: mode == "lower"
  uint64   wal_dev     \
  uint64   wal_ino      |  Stempel der Haupt-WAL, genau wie
  uint64   wal_used     |  spec/31 §6.2.1
  uint64   wal_fp      /
  uint32   val_count   verschiedene Feldwerte
  uint32   key_count   Paare (Feldwert, kv-Schluessel)
  uint64   reserved    0

val_count x Feldwert-Eintrag (16 Byte, nach dem NORMALISIERTEN Wert sortiert)
  uint32   val_off, val_len     Stelle im Stringblock
  uint32   key_first, key_cnt   Fenster im Schluesselfeld

key_count x Schluessel-Eintrag (8 Byte, in Einfuegereihenfolge)
  uint32   key_off, key_len

Stringblock: Feldwerte und kv-Schluessel, jeder mit abschliessender 0
```

Sortiert wird nach dem **normalisierten** Feldwert, also nach `idx_normalize`
(bei `db_indexed: "lower"` die kleingeschriebene Form), denn genau damit sucht
die Abfrage.  Schreiben und Suchen muessen dieselbe Ordnung verwenden —
`kv_idx_cmp` —, sonst findet die binaere Suche nichts, und zwar still.

Die Strings tragen eine **abschliessende 0**, obwohl ihre Laenge daneben steht.
Das kostet ein Byte je String und erspart einen Fehler, der beim Primaerindex
schon einmal zugeschlagen hat: `finding_by` gibt einen `const char*` direkt
zurueck, und ein Aufrufer liest ihn als C-String bis zur 0.  Ohne Terminator
muesste jeder Rueckgabepfad kopieren oder laeuft ueber das Ende.

Fuer `unique: true` ist `key_cnt` immer 1.  Das Format bleibt dasselbe — ein
Layout weniger ist ein Fehlerpfad weniger.

#### Oeffnen

`aiml_db_index_open_for_kv` versucht die Datei zu laden, sobald der Store
`index_dump` traegt.  Angenommen wird sie nur, wenn **alles** stimmt:

1. `magic` und `version`,
2. `flags` gleich der Deklaration — wird aus `db_indexed: true` ein
   `unique: true` oder ein `"lower"`, ist der alte Dump unbrauchbar,
3. `wal_dev`/`wal_ino` gleich der heutigen Haupt-WAL (ein Checkpoint tauscht
   die Inode),
4. `wal_used` gleich dem heutigen Wasserstand,
5. `wal_fp` gleich dem Fingerabdruck ueber genau diesen Bereich,
6. alle Offsets und Fenster innerhalb der Datei.

Scheitert ein Punkt, wird die Datei verworfen und das Feld voll aufgefuellt —
wie in V1.  Der Dump ist **nie** maßgebend gegenueber der WAL: jeder Zweifel
kostet Zeit, nie Richtigkeit.

Laedt der Dump, meldet der Index das, und `aiml_db_index_backfill` kehrt sofort
zurueck.  Die erzeugte IR bleibt damit unveraendert — `open_for_kv` dann
`backfill`, wie bisher.

Anders als beim Primaerindex gibt es **keinen Ueberlauf-Pfad**: der Stempel ist
nur dann gueltig, wenn die WAL Byte fuer Byte dieselbe ist, aus der der Index
gebaut wurde.  Ein Nebenindex laesst sich nicht einfach nachspielen, weil ein
spaeteres `kv.set` auf einen schon vorhandenen Schluessel auch dessen ALTEN
Feldwert aus dem Index nehmen muesste, und der alte Wert steht nur an der alten
Stelle der WAL.  Das waere ein zweiter, feinerer Pfad fuer einen Gewinn, den
niemand gemessen hat.

#### Schreiben beim Lesen — Kopie je Feldwert

Nach dem Laden liegt der Index in der Datei-Abbildung und wird von dort
beantwortet.  Ein `kv.set` oder `kv.delete` darf ihn aber weiter aendern.  Dafuer
gilt: beim ERSTEN Eingriff in einen Feldwert wird dessen Fenster aus dem Dump in
den Speicher-Index (`map_1n` bzw. `map_unique`) kopiert und der Feldwert als
"im Speicher gefuehrt" vermerkt; danach gilt fuer ihn nur noch der
Speicher-Index.  Ein Feldwert, den niemand angefasst hat, kostet weiterhin kein
anonymes Byte.

Eine Abfrage schaut also: Feldwert im Speicher gefuehrt → Speicher-Index;
sonst → binaere Suche im Dump.  Beide Wege sind nie gleichzeitig gueltig, und
damit gibt es keinen Fall, in dem ein Schluessel doppelt oder gar nicht kommt.

#### Geschrieben wird beim Checkpoint

`db.checkpoint` ist der eine Moment, in dem der Store absichtlich in einen
haltbaren, geschlossenen Zustand gebracht wird; dort entsteht auch der
Primaerindex-Dump (spec/31 §6.2.1, Schritt 8).  Direkt danach schreibt jeder
Nebenindex dieses Stores seine Datei, mit dem Stempel der eben umbenannten WAL.

Folgen, die man kennen muss:

- Ein Store, der beschrieben aber **nie** gecheckpointet wird, hat keinen
  Nebenindex-Dump.  Das naechste Oeffnen fuellt auf — V1-Verhalten, kein
  Rueckschritt.
- Der Checkpoint wird **teurer**: er muss die Indizes durchlaufen und die
  Feldwerte sortieren.  Das ist der bewusste Tausch — einmal schreiben, viele
  Male guenstig oeffnen.  Wer einen Store nur fuellt und einmal liest, gewinnt
  nichts.
- `kv.clear` leert die Nebenindizes UND loescht ihre Dateien.  Das Leeren gilt
  auch ohne `index_dump`: die Indizes haengen am Store-Zeiger, nicht an der
  Datei, und ein geleerter Store darf keine Treffer mehr liefern.  Das Loeschen
  der Datei, weil ein liegengebliebener Dump den geleerten Store beim naechsten
  Oeffnen wieder bevoelkern wuerde -- der Stempel faellt zwar ohnehin darauf,
  aber sich auf eine Pruefung zu verlassen, wo Loeschen eindeutig ist, waere
  nachlaessig.

#### Kompatibilitaet — keine Migration

| Lage | Verhalten |
| --- | --- |
| Alte WAL, kein `.kvsidx` | Volles Auffuellen, wie V1 |
| `.kvsidx` da, WAL inzwischen geschrieben | Stempel faellt → volles Auffuellen |
| `.kvsidx` beschaedigt oder fremde Version | Verworfen → volles Auffuellen |
| `index_dump` wieder abgeschaltet | Datei bleibt liegen, wird nicht gelesen |

Es gibt nichts zu migrieren und kein Format zu heben: die Datei ist reine
abgeleitete Zwischenablage.  Wer sie loescht, verliert Zeit, nie Daten.

#### Gemessen (07.11.2026)

Ein `Kv<string:Fakt>` mit 300 000 Zeilen und zwei indizierten Feldern --
`subj` mit 15 000 verschiedenen Werten (kleine Buckets) und `rel` mit 40
(Buckets von 7500).  Gemessen wird oeffnen, `len` und zwei `findings_by`;
`tests/measure_sidx.sh` wiederholt jede Messung:

| Oeffnen | Zeit | RSS |
| --- | --- | --- |
| mit beiden Dumps | **0,12 s** | **98 MB** |
| ohne Nebenindex-Dump (Primaerindex-Dump da) | 1,45 s | 171 MB |
| ohne jeden Dump | 1,82 s | 189 MB |

Der Nebenindex-Dump nimmt also 1,33 s und 73 MB ab -- hier das Zwoelffache an
Zeit und 43 % des Speichers.  Platz kostet er: 4,7 MB (`rel`) und 5,1 MB
(`subj`) neben einer 33,5-MB-WAL, grob 16 Byte je Paar plus die Strings.

#### Gemessen an cai (07.11.2026)

Diese Zahlen gelten fuer dieses Modell.  Darum nachgemessen am echten
Wissensgraphen von `cai` -- auf einer Kopie von `out/n0511_alles`, 3,7 GB WAL
ueber 10 Stores, davon 13 indizierte Felder (4 allein auf `v2_fakten`:
`rel`, `subj`, `obj`, `rel_subj`):

| | |
|---|---|
| Dinge | 3.549.716 |
| Namen | 6.480.381 |
| Fakten | 5.797.217 |
| Propositionen | 2.295.484 |
| Relationen | 42.886 |

Gemessen wird `cai2 graph stats` -- oeffnen, Zaehler, Relationsnamen.  Jede
Zeile ist ein ganzer Prozessstart:

| Zustand | Zeit | RSS |
| --- | --- | --- |
| kein Dump (so liegen die Gedaechtnisse heute) | 251 s | 19,3 GB |
| nur Primaerindex-Dump (`.kvidx`, §6.2.1) | 230 s | 18,1 GB |
| **beide Dumps** | **3,7 s** (warm 0,70 s) | **4,2 GB** |

Das Ergebnis kehrt die Erwartung um: der **Primaerindex**-Dump nimmt hier fast
nichts ab (251 → 230 s, 8 %).  Der ganze Gewinn liegt am **Nebenindex**-Dump,
230 s → 3,7 s und 18,1 → 4,2 GB.  Der Grund steht in §6.2.1: den Primaerindex
baut ein WAL-Durchlauf, `aiml_db_index_backfill` dagegen **dekodiert jede
Zeile** einmal, nur um ein Feld zu lesen.  Das ist die Zeit, und es ist der
Speicher.

Platz kostet er: 3,6 GB `.kvsidx` neben 1,2 GB `.kvidx` und 3,7 GB WAL (je
Store: `fakten` 1,4 GB, `namen` 960 MB, `props` 954 MB, `dinge` 302 MB).  Der
Nebenindex-Dump ist hier also **groesser als der Primaerindex-Dump** -- er traegt
je Paar den Feldwert UND den kv-Schluessel, waehrend `.kvidx` nur Schluessel
plus `(offset, length)` haelt.

Einmalige Kosten: ein Checkpoint-Durchlauf schreibt alle 22 Dateien und brauchte
7:34 (21,7 GB RSS), weil er die Indizes erst baut.  Ein vorhandenes Gedaechtnis
bekommt die Dateien also **beim Checkpoint, nicht beim Oeffnen**.

Die Gegenprobe, ohne die keine der Zahlen etwas heisst: die Ausgabe von
`graph stats` ist mit und ohne Nebenindex-Dump **byte-gleich** (`cmp`), und die
schnelle Messung wurde nach der langsamen wiederholt, damit die Reihenfolge im
Seitencache sie nicht erklaert.

Und die Pruefung, ohne die eine gute Zahl nichts bedeutet: `tests/run_sidx_gates.sh`
Gate 3 **vergiftet** eine gueltige Datei (ein Schluessel im Stringblock
umgeschrieben, Stempel unberuehrt) und verlangt, dass der vergiftete Wert
herauskommt.  Ein gruener Unit-Test allein beweist nichts -- faellt der
Stempel, fuellt der Store still auf, und alles stimmt trotzdem.

#### Was der Dump NICHT loest

- Das erste Fuellen eines Stores — da gibt es noch nichts zu laden.
- Zusammengesetzte Indizes, Bereichsabfragen: unveraendert ausser Umfang.
- Den Speicher der **Schreibseite**: wer viele verschiedene Feldwerte anfasst,
  kopiert sie einzeln in den Speicher-Index und landet am Ende dort, wo V1
  angefangen hat.  Der Dump ist fuer lesende Oeffnungen gebaut.

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

`aiml_db_index_backfill` kehrt ohne Arbeit zurueck, wenn der Index seinen Dump
geladen hat (siehe Persistence) — der Dump IST das Ergebnis des Auffuellens,
gestempelt auf dieselben WAL-Bytes.

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

`tests/test_kv_sidx_*.aiml` — der Nebenindex-Dump: Schreiben mit Checkpoint,
Lesen aus dem Dump, Aenderung nach dem Laden (Kopie je Feldwert), `unique` und
`"lower"`, beschaedigte Datei, Stempelbruch durch einen Schreibzugriff ohne
Checkpoint, `kv.clear`.  Dazu `tests/run_sidx_gates.sh` fuer das, was eine
einzelne .aiml-Datei nicht pruefen kann (Dateien zwischen zwei Laeufen
vertauschen, Groesse und Zeit messen).
