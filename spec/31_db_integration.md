# Chapter 31 — AionDB: The Built-In Database

> **Core idea:** A database in AIML is not an external tool connected via drivers.
> It is a reserved namespace (`db`) deeply integrated into the language —
> just like `sys`, `io`, or `time`. The compiler knows the types, the memory manager
> is the same as for regular variables, and schema versions are the same concept
> as class versions from Chapter 30 (Hot-Reload).

---

## 1. Why a Built-In Database?

External databases (SQLite, PostgreSQL, Redis, …) have a fundamental drawback:
they speak a different language than the program. You serialize data into text (SQL),
send it through an abstraction layer, and deserialize the result back.
Every step costs time and memory.

AionDB eliminates this boundary entirely:

- **Zero-Copy Reads**: A store read is a `ref<Entity>` borrow (spec/61 §5.4) — a real machine
  pointer into the database's memory region. Nothing is copied, nothing is deserialized; the
  store keeps ownership.
- **Type safety without mapping**: Because the compiler knows the AIML class `Entity`,
  you don't need to write an ORM. The class *is* the schema.
- **Schema evolution for free**: When a class is updated from `version: 1` to `version: 2`
  (appending a new field), AionDB detects this automatically and
  migrates existing pages — no downtime, no migration script.
- **Versioning built in**: Each row can optionally carry a timestamp,
  which enables "time-travel" queries: "What did the database look like 500 frames ago?"

---

## 2. The `db` Namespace

`db` is a **reserved namespace** in AIML. You cannot define your own classes,
functions, or variables under this name. It behaves analogously to `sys`:

```aiml
# Wrong — compiler error:
- def:
    name: db.connect   # "db" is reserved

# Correct:
- decl: [store, Store<Entity>]
- set:  [store, { call: [db.open, "entities"] }]
```

All `db` operations are **known at compile time**: the compiler emits direct
calls into the AionDB runtime, without dynamic dispatch. This is faster than
any driver-based approach.

---

## 3. Core Concepts (explained in detail)

### 3.1 What is a `Store<T>`?

A `Store<T>` is a **typed table** that stores rows of type `T`.
`T` must be an AIML `class` or `model`. The store behaves like a
very fast, persistent version of `list<T>` — with the difference that data
is not scattered on the heap, but organized in **pages** (contiguous memory blocks).

```aiml
- class:
    name: Entity
    members:
      - [id,       i32]
      - [pos_x,    f32]
      - [pos_y,    f32]
      - [hp,       i32]
      - [ai_state, i32]

# Open (or create if not present) a store for Entity objects:
- decl: [entities, Store<Entity>]
- set:  [entities, { call: [db.open, "entities"] }]

# Insert:
- decl: [e, Entity]
- set: [e.id, 1]
- set: [e.pos_x, 100.0]
- set: [e.hp, 100]
- call: [entities.insert, e]

# Read by ID (zero-copy — a ref<Entity> borrow into store memory):
- decl: [p, ref<Entity>]
- set:  [p, { call: [entities.get, 1] }]
- print: ["Entity hp: ${p.hp}"]
```

A `Store<T>` can be operated in two **layouts** (Chapter 3.3 explains why):

| Layout   | Description | Good for |
|----------|-------------|---------|
| `row`    | All fields of a row together in memory | Reading/writing individual objects |
| `column` | All values of a field together in memory | Bulk operations, SIMD, AI |

```aiml
- decl: [entities, Store<Entity>]
- set:  [entities, { call: [db.open, "entities", {layout: "column"}] }]
```

---

### 3.2 What is ECS — Entity Component System?

ECS is an **architectural pattern** used in games and increasingly in AI systems.
It solves a fundamental problem of object-oriented programming:
**inheritance explosion**.

#### The Problem Without ECS (classic OOP)

Imagine a game with players, enemies, projectiles, and particles:

```
Player    → Position, Velocity, Health, Sprite, InputHandler
Enemy     → Position, Velocity, Health, Sprite, AIController
Bullet    → Position, Velocity, Sprite, Damage
Particle  → Position, Velocity, Sprite, Lifetime
```

All four have `Position`, `Velocity`, and `Sprite`. In OOP you would build a base class
`MovableSprite`. But `Enemy` and `Player` have `Health`, `Bullet` has `Damage`…
An immediately complicated inheritance tree emerges, which is hard to extend.

Also: when the rendering code wants to draw all objects with `Sprite`, it jumps
through memory — `Player` objects here, `Enemy` objects there, `Bullet` objects
elsewhere. This is **cache-hostile** and therefore slow.

#### The ECS Solution

ECS separates **identity**, **data**, and **logic** completely:

- **Entity**: A unique ID. An entity *is* nothing — it *has* components.
- **Component**: Pure data struct, no methods. `Position`, `Velocity`, `Health` are components.
- **System**: A function that processes all entities with certain components.

#### `model` vs `class` for ECS Components

In AIML, ECS uses **`model` types** instead of `class`. The difference:

| | `class` | `model` |
|--|---------|---------|
| Methods | yes | **no** (pure data) |
| Inheritance | yes | no |
| ECS / `db.run_system` | no | **yes** |
| Column layout default | no | yes |

```aiml
# ECS components as model (pure data, no methods):
- model:
    name: Position
    members: [[x, f32], [y, f32]]

- model:
    name: Velocity
    members: [[dx, f32], [dy, f32]]

- model:
    name: Health
    members: [[hp, i32], [max_hp, i32]]

- model:
    name: Sprite
    members: [[texture_id, i32], [scale, f32]]
```

#### `primary_key` — natural entity identity

By default, the entity ID is the **slot index** in the store (an `i32`). This is the
fastest path (O(1) pointer arithmetic). If entities have a natural key
(UUID, username, IP address…), you can specify `primary_key` in the model:

```aiml
# No primary_key → entity ID = slot index (i32), fastest path
- model:
    name: Particle
    members: [[x, f32], [y, f32], [vx, f32], [vy, f32]]

# With primary_key → entity ID = uuid (string), internally HashMap<string, slot>
- model:
    name: Player
    primary_key: uuid
    members: [[uuid, string], [score, i32], [hp, i32]]
```

The type of `eid` in system functions is automatically derived from `primary_key`:
- No `primary_key` → `eid: i32` (slot index)
- `primary_key: uuid` (string) → `eid: string`

#### `db.run_system` — the ECS loop

The compiler reads the system function's signature and derives from it which
stores must be joined. The loop is compiled directly to LLVM IR —
no runtime dispatch, no string parsing:

```aiml
- decl: [positions,  Store<Position>]
- decl: [velocities, Store<Velocity>]
- decl: [health,     Store<Health>]
- set:  [positions,  { call: [db.open, "positions"] }]
- set:  [velocities, { call: [db.open, "velocities"] }]
- set:  [health,     { call: [db.open, "health"] }]

# Movement system: no eid needed — pure data transformation
- fn:
    name: move_system
    args: [[pos, ref<Position>], [vel, ref<Velocity>]]
    body:
      - set: [pos.x, { call: [pos.x + vel.dx] }]
      - set: [pos.y, { call: [pos.y + vel.dy] }]

# Damage system: eid needed — deletes entity from multiple stores
- fn:
    name: damage_system
    args: [[eid, i32], [h, ref<Health>]]
    body:
      - if:
          condition: { call: [h.hp <= 0] }
          body:
            - call: [health.delete, eid]
            - call: [positions.delete, eid]
            - call: [velocities.delete, eid]

# Compiler sees ref<Position> + ref<Velocity> → iterates over positions ∩ velocities
- call: [db.run_system, move_system, positions, velocities]

# Compiler sees only ref<Health> → iterates over health alone
- call: [db.run_system, damage_system, health]
```

`db.run_system` iterates over all entity IDs present in **all** passed stores
(intersection). For each match, the system function is called with the corresponding
component pointers — zero-copy, no allocation.

An `Entity` is now only an ID. The combination of its components describes what it is:

| Entity ID   | Position | Velocity | Health | Sprite | Damage |
|-------------|----------|----------|--------|--------|--------|
| 1 (Player)  | ✓        | ✓        | ✓      | ✓      |        |
| 2 (Enemy)   | ✓        | ✓        | ✓      | ✓      |        |
| 3 (Bullet)  | ✓        | ✓        |        | ✓      | ✓      |
| 4 (Particle)| ✓        | ✓        |        | ✓      |        |

#### Why is ECS fast?

The decisive trick is the **column store**: all `Position` components lie
sequentially in memory. The movement system reads them sequentially — no
cache misses, no pointer chasing. Additionally, the compiler can use **SIMD instructions**:
with AVX2, 8 `f32` values can be added simultaneously.

```
Memory without ECS (row layout, OOP):
[Player: pos_x, pos_y, hp, texture_id, dx, dy, ...]  ← 64 bytes
[Enemy:  pos_x, pos_y, hp, texture_id, dx, dy, ...]  ← 64 bytes, far away
→ Cache miss for every object

Memory with ECS (column layout):
[pos_x: 10.0, 25.0, 50.0, 99.0, ...]  ← all 4 entities, 16 bytes sequentially
[pos_y: 5.0,  12.0, 3.0,  80.0, ...]  ← 16 bytes sequentially
→ SIMD can process 4 pos_x values at once (128-bit register)
```

`model` types activate column layout automatically. For `class`-based stores
it must be specified explicitly via `{layout: "column"}`.

---

### 3.3 What is a `Kv<K,V>`?

A **key-value store** is the simplest database form: each value has a unique
key under which it is stored and retrieved. You know it from `map<K,V>` in AIML —
with the difference that `Kv<K,V>` is **persistent** (survives program restart) and
is optimized for very large data sets.

```aiml
# Store player configurations:
- decl: [config_db, Kv<string, PlayerConfig>]
- set:  [config_db, { call: [db.open_kv, "player_configs"] }]

- decl: [cfg, PlayerConfig]
- set: [cfg.sensitivity, 1.5]
- call: [config_db.set, "player_1", cfg]

- decl: [loaded, PlayerConfig]
- set:  [loaded, { call: [config_db.get, "player_1"] }]
```

Internally `Kv<K,V>` uses a **hash table** with open addressing —
O(1) for `get` and `set`.

---

### 3.4 What is `TimeSeries<T>` and Time-Travel?

A `TimeSeries<T>` is an **append-only log**: data is never overwritten,
but always appended with a timestamp. This enables **time-travel queries**:
"What was the state at time X?"

This is particularly valuable for:
- **Games**: Replay system, debugging ("Why did entity 5 die at frame 1337?")
- **AI training**: Past states can be used as training data
- **Audit logs**: Every change is traceable

```aiml
- class:
    name: GameState
    members:
      - [score, i32]
      - [player_hp, i32]
      - [boss_hp, i32]

- decl: [history, TimeSeries<GameState>]
- set:  [history, { call: [db.open_timeseries, "game_history"] }]

# Save every frame:
- decl: [state, GameState]
- set: [state.score, 1500]
- set: [state.player_hp, 75]
- call: [history.append, state, { call: [time.now] }]

# Time-travel: query state at tick 1000:
- decl: [past, GameState]
- set:  [past, { call: [history.at, 1000] }]
- print: ["Score at tick 1000: ${past.score}"]

# Query range (for ML training):
- decl: [replay, list<GameState>]
- set:  [replay, { call: [history.range, 0, 5000] }]
```

---

### 3.5 What is `VecStore` — Vector Search for AI?

A **VecStore** is a specialized database for **vectors** (arrays of `f32` numbers).
It answers the question: "Which stored vectors are most similar to this query vector?"

#### What is it used for?

Modern AI models (language models, image models, recommendation systems) convert objects
into high-dimensional numeric vectors, called **embeddings**. Similar objects have
similar vectors. An embedding vector for "cat" lies close to "dog" in vector space,
far away from "spaceship".

Concrete use cases:

- **RAG (Retrieval-Augmented Generation)**: Document embeddings are stored.
  When a user asks a question, the k most similar documents are found and
  provided as context to a language model.
- **Game AI**: Enemy behavior is stored as a vector. Similar situations from
  past games are found quickly.
- **Recommendation system**: Store item embeddings, suggest similar items.

```aiml
# Store 128-dimensional embeddings:
- decl: [knowledge, VecStore]
- set:  [knowledge, { call: [db.open_vec, "knowledge_base", {dims: 128}] }]

# Insert an embedding (e.g. from an ML model):
- decl: [embed, list<f32>]   # 128 floats
# ... fill embedding ...
- call: [knowledge.insert, "doc_42", embed]

# k-nearest-neighbor search (k=5 most similar):
- decl: [query_vec, list<f32>]
# ... fill query embedding ...
- decl: [results, list<VecResult>]
- set:  [results, { call: [knowledge.search, query_vec, 5] }]

# VecResult contains: id (string), score (f32, similarity 0.0–1.0)
- decl: [r, VecResult]
- set:  [r, { call: [results.get, 0] }]
- print: ["Most similar document: ${r.id}, Score: ${r.score}"]
```

Internally `VecStore` uses **HNSW** (Hierarchical Navigable Small World) —
a graph-based algorithm for approximate nearest-neighbor search.
It is faster than brute-force (O(log N) vs O(N)) and scales to millions of vectors.

---

## 4. Queries

AionDB avoids runtime parsing entirely. There are two query modes:

- **`db.query`** — for single stores with optional filter and sorting
- **`db.run_system`** — for ECS queries across multiple stores (see §3.2)

### 4.1 `db.query` — Single-Store Queries

> **Nur `Store<T>`.** `db.query` arbeitet auf einem Store (`db.open`), nicht
> auf einem `Kv<K:V>` (`db.open_kv`) — die beiden haben verschiedenes
> Innenleben. Ein `Kv` als erstes Argument ist seit 2026-11-12 ein
> **Übersetzungsfehler**. Vorher übersetzte der Aufruf sauber und las zur
> Laufzeit Unsinn: einmal ein SIGSEGV (`aiml_db_store_is_active`, Adresse
> `0x3f800000` — das Bitmuster von `1.0f`, als Zeiger benutzt), einmal stumm
> eine leere Liste. Ein `Kv` wird mit `.iter` durchlaufen oder mit `.get`
> gelesen. Netz: `tests/test_db_query_on_kv_error.aiml`.

The `{where: ...}` argument takes a **function**, not a string.
The function is compiled to a function pointer and called per element —
no parser, direct IR:

```aiml
# Predicate function — compiled to LLVM IR by the compiler:
- def:
    name: is_alive
    args: [[e, ref<Entity>]]
    return: i32
    body:
      - return: { ">": [e.hp, 0] }

- def:
    name: is_active_enemy
    args: [[e, ref<Entity>]]
    return: i32
    body:
      - decl: [alive, i32]
      - set: [alive, { ">": [e.hp, 0] }]
      - decl: [enemy, i32]
      - set: [enemy, { eq: [e.ai_state, 2] }]
      - return: { and: [alive, enemy] }

# All living entities, sorted by pos_x, at most 100:
- decl: [alive, list<ref<Entity>>]
- set:  [alive, { call: [db.query, entities,
    {where: is_alive},
    {order_by: "pos_x"},
    {limit: 100}
  ]}]
```

The result is a **weak list** `list<ref<T>>` (spec/61 §5.4/§5.5): every element is a borrow into
the store, the store keeps ownership. The list itself is an owned container handle — releasing it
(scope end, rebind) frees only the list, never a record. An element dangles when its record is
deleted from the store. Binding the result to `list<ptr<T>>` is a **compile error** (a `ptr<T>`
element is a unique owner and would free the store's record); for independent owned copies use
`store.get_copy` per element. The `where` function receives each record as `ref<T>`.

### 4.2 Available Operators

| Operator   | Description | Example |
|------------|-------------|---------|
| `where`    | Filter function `(ref<T>) → i32` | `{where: is_alive}` |
| `order_by` | Field name (compile-time validated) | `{order_by: "pos_x", dir: "asc"}` |
| `limit`    | Maximum results | `{limit: 100}` |
| `offset`   | Skip results | `{offset: 50}` |
| `at`       | Time-travel snapshot (with TimeSeries) | `{at: 1000}` |

**Note:** `order_by` and `select` use field names as strings — however these are
validated **at compile time** against the schema of `T`. An unknown field
is a compiler error, not a runtime error.

### 4.3 ECS Query: `db.run_system`

For queries across multiple stores (ECS archetype intersection), use
`db.run_system`. See §3.2 for the full explanation with examples.

```aiml
# Compiler sees ref<Position> + ref<Velocity> → iterates over intersection
- call: [db.run_system, move_system, positions, velocities]
```

---

## 5. Transactions

AionDB offers two transaction modes:

### 5.1 Standard Transaction (ACID)

```aiml
- call: [db.begin]

- call: [entities.insert, new_entity]
- call: [inventory.delete, old_item_id]

# Commit all changes atomically:
- call: [db.commit]

# Or roll everything back:
# - call: [db.rollback]
```

Until `db.commit` is called, changes are **only visible to the current session**.
Other threads/agents see the old data.

### 5.2 Frame Transaction (for Games)

In game mode, `db.begin_frame` groups all changes of a frame under a
single timestamp. This is more efficient than many individual commits:

```aiml
- call: [db.begin_frame, { call: [time.now] }]   # Starts frame N

# All game-loop operations of this frame:
- call: [entities.insert, spawned_enemy]
- call: [entities.update, 5, {hp: 80}]
- call: [history.append, current_state]

- call: [db.end_frame]   # All changes get the same timestamp
```

---

## 6. Persistence — WAL and Memory-Mapped Files

AionDB writes data in two layers:

### 6.1 WAL (Write-Ahead Log)

Before a change is applied to the actual page, AionDB writes it to a
**sequential log** (WAL). This is fast (sequential writes are very efficient
on SSDs) and crash-safe: if the program crashes, AionDB can read through the WAL
on next start and restore the state.

### 6.2 Pages (mmap)

Both `Store<T>` and `Kv<K,V>` are **mmap-backed**:

- **`Store<T>`** stores rows in **pages** (default 4 KB) on disk, mapped
  into process memory via mmap. A `ref<Entity>` read points directly into
  the mapped page.
- **`Kv<K,V>`** maps the WAL file itself (sparse-extended to an initial
  reserve of 16 MB; `mremap`'d to grow). All values live exclusively in
  the WAL, the in-process index holds only `(offset, length)` per key.
  Mit `index_dump: true` liegt dieser Index auf der Platte statt im Prozess —
  siehe §6.2.1.
  `kv.get` returns a pointer into the mmap'd region for blob/string/fixed
  modes — true zero-copy.

**Fehlender Schlüssel.**  `kv.get` auf einen Schlüssel, den es nicht gibt, liefert den
Nullwert des Werttyps: `0` für `i32`/`i64`, `0.0` für `f32`/`f64`, `""` für `string`, einen
Nullzeiger für ein Modell.  Bis 01.11.2026 lud der Zahl-Pfad ungeprüft vom Nullzeiger und
stürzte ab (SIGSEGV), während `Kv<string:string>` bereits `""` lieferte.  Wer zwischen
„fehlt" und „steht auf 0" unterscheiden muss, fragt vorher `kv.has(key)`.

The operating system manages which pages are RAM-resident and which are
on disk. AionDB does not implement paging itself.

> **Apparent vs. real file size:** `kv.kvwal` files appear as 16 MB
> (or larger after `mremap`-grow) in `ls -la`. The actual disk usage
> (`du -h`) reflects only the touched pages. Backup tools must be
> sparse-aware (see §6.8).

### 6.2.1 Index-Dump — `index_dump: true`

Der Index aus §6.2 liegt **nur im Prozess** und wird bei jedem `open` aus der WAL
neu aufgebaut.  Fuer kleine Stores ist das die schnellere Wahl.  Mit der Groesse
kippt die Rechnung, und zwar in beide Richtungen gleichzeitig.  Gemessen am
Wissensgraphen von `cai` (06.11.2026, 3,02 Mio Wikipedia-Artikel):

| | |
|---|---|
| Schluessel ueber alle Stores | 18.166.149 |
| Schluesselbytes | 713 MB |
| WAL sichtbar | 5,53 GB |
| Index **im RAM** | **19,5 GB** (434 Byte je Schluessel) |
| Aufbauzeit je Prozessstart | **~5 min** |

Die 434 Byte je Schluessel sind `unordered_map`-Aufwand — Bucket-Array,
Knotenzeiger, `std::string`-Kopf, Allokator-Rundung — fuer eine Nutzlast von
Schluessel plus `(offset, length)`.  Dazu kam: eine Abfrage auf diesem Store
brauchte 63 s auf einer Scherbe, davon **63 s Aufbau und praktisch 0 s Abfrage**
(`graph stats`, `ask` und ein Namensschlag waren bis auf 2 % gleich schnell).

**`index_dump: true`** legt den Index neben der WAL ab und benutzt ihn von dort,
**ohne ihn in eine Hashmap zu laden**:

```aiml
- decl: [kv, Kv<string:string>, { call: [db.open_kv, "namen", {persist: true, index_dump: true}] }]
# → namen.kvwal  +  namen.kvidx
```

Die Vorgabe ist `false`; bestehende Stores verhalten sich unveraendert.
`index_dump` ohne `persist: true` ist ein Fehler: ohne WAL gibt es keine Offsets,
auf die ein Dump zeigen koennte.

#### Datei `<name>.kvidx`

Header 64 Byte, dann ein nach Schluesselbytes **sortiertes** Eintragsfeld, dann
der Schluesselblock:

| Feld | Breite | Bedeutung |
|---|---|---|
| `magic` | 8 | `"AIMLKVIX"` |
| `version` | 4 | Formatversion |
| `wal_version` | 4 | `version` der zugehoerigen WAL |
| `val_size` | 8 | `val_size` der zugehoerigen WAL |
| `wal_used` | 8 | **Wasserstand**: Stand der WAL beim Schreiben des Dumps |
| `wal_dev` | 8 | `st_dev` der WAL beim Schreiben des Dumps |
| `wal_ino` | 8 | `st_ino` der WAL beim Schreiben des Dumps |
| `wal_fp` | 8 | **Fingerabdruck** des beschriebenen WAL-Bereichs (s. u.) |
| `count` | 8 | Zahl der Eintraege |
| je Eintrag: `val_off` | 8 | Offset des Werts **in der WAL** |
| je Eintrag: `key_off` | 4 | Offset im Schluesselblock |
| je Eintrag: `val_len` | 4 | Wertlaenge |
| je Eintrag: `key_len` | 4 | Schluessellaenge |
| je Eintrag: Polsterung | 4 | Ausrichtung auf 24 Byte |

Bewusst 24 Byte und **kein** `pack(1)`: ein gepackter Eintrag brauchte
unausgerichtete 64-Bit-Zugriffe mitten im mmap.  Die vier Byte Polsterung sind
billiger als diese Klasse von Fehlern.

Fuer die Messung oben: 18,17 Mio × 24 Byte + 713 MB Schluessel = **1,11 GB**
gegen 19,5 GB im RAM, und die Datei ist **datei-gebunden**, also vom Kernel
rueckgewinnbar — anders als die anonymen 19,5 GB heute.

#### Oeffnen

1. `.kvidx` mmappen.  Ist sie unbrauchbar, wird sie **ignoriert** und §6.2 gilt
   unveraendert (voller Replay).  Der Dump ist nie maßgebend gegenueber der WAL.
   Unbrauchbar heisst:
   - `magic` falsch, oder `version`/`wal_version`/`val_size` passen nicht zur WAL;
   - `wal_dev`/`wal_ino` weichen vom `fstat` der WAL ab — die **WAL-Datei wurde
     ersetzt**, also sind alle Offsets im Dump wertlos;
   - `wal_used` liegt **hinter** dem Ende der WAL-Daten;
   - `wal_fp` passt nicht zum Fingerabdruck des beschriebenen Bereichs (s. u.).

   Die Inode-Pruefung ist der tragende Teil, und sie ist kein Zufallsfund:
   `kv.checkpoint` schreibt die WAL als Temp-Datei und zieht sie per `rename`
   herueber (§6.3).  Die Inode wechselt also **genau dann**, wenn sich die Offsets
   verschieben.  Ein `fstat` genuegt; ein Pruefwert ueber die Daten waere so teuer
   wie der Replay, den wir vermeiden wollen.

   Der Restfall ist eine **wiederverwendete Inode** nach einem Checkpoint durch
   ein fremdes Binary.  Den deckt der Wasserstand ab: eine Verdichtung entfernt
   veraltete Saetze, das Datenende liegt danach **vor** dem Wasserstand, und der
   Dump fliegt raus.  Entfernt die Verdichtung nichts, bleiben die Saetze in
   gleicher Reihenfolge an gleicher Stelle — dann ist der Dump weiter richtig.

#### Fingerabdruck `wal_fp`

Inode und Wasserstand allein genuegen nicht.  `cp quelle ziel` **behaelt die
Inode des Ziels**: wer eine einzelne `.kvwal` aus einem Backup zuruecklegt,
haette eine fremde WAL unter einem gueltig aussehenden Dump, und bei einer
groesseren Datei greift auch der Wasserstand nicht.  Darum bindet der Dump sich
an den **Inhalt des Bereichs, den er beschreibt**:

- Bereich ist `[sizeof(AimlKvWalHeader), wal_used)` — also genau das, worauf die
  Eintraege zeigen.  **Anhaengen aendert ihn nicht**, denn das passiert hinter
  dem Wasserstand; der Ueberlauf-Pfad bleibt damit nutzbar.
- Bis 64 KiB Bereichslaenge wird er ganz gehasht.  Darueber 16 Stichproben von
  je 4 KiB an den Offsets `start + floor(i * (len - 4096) / 15)` fuer `i = 0..15`.
- Hash ist FNV-1a 64, begonnen mit `wal_used` als Startwert.

Das kostet hoechstens 64 KiB Lesen, ist reproduzierbar und faellt bei
Austausch, Rueckkopieren und Teilbeschaedigung auf.  Ein Beweis ist es nicht —
ein Hash bleibt ein Hash; die Zusage lautet: **ein falscher Dump fuehrt nie zu
falschen Daten, sondern wird verworfen**, und die drei Pruefungen zusammen
machen ein Durchkommen unwahrscheinlich genug, um den Replay zu sparen.
2. **Nur den WAL-Rest ab `wal_used`** nachspielen, in einen kleinen
   Ueberlauf-Index im RAM.  Er traegt, was seit dem Dump geschrieben wurde,
   einschliesslich Tombstones fuer `kv.delete`.
3. Kein Aufbau ueber den Dump-Teil: `kv.get` sucht **binaer im mmap**, O(log n).

**Suchreihenfolge:** erst der Ueberlauf-Index (neuer), dann der Dump.  Ein
Tombstone im Ueberlauf verdeckt einen Dump-Eintrag — sonst wuerde ein
geloeschter Schluessel nach dem Neustart wieder auftauchen.

#### Schreiben

Der Dump entsteht **ausschliesslich bei `kv.checkpoint`** (§6.3), und nur bei
`index_dump: true`.  Das passt dorthin: der Checkpoint schreibt die WAL neu,
**alle Offsets aendern sich**, ein Dump von vorher waere falsch.  Darum gilt
auch: `kv.checkpoint` und `kv.clear` schreiben den Dump neu oder loeschen ihn —
sie duerfen ihn nie stehen lassen.

Bewusst **nicht** beim Freigeben des Stores, aus zwei Gruenden.  Erstens waere
der Ueberlauf-Pfad dann nie in Betrieb und damit nicht pruefbar: jedes
Prozessende faltete ihn ein.  Zweitens ist ein stilles Gigabyte Schreiblast beim
Beenden eine Ueberraschung, die niemand bestellt hat.  Folge, ausdruecklich:
**ein Store, der nie checkpointet, bekommt nie einen Dump** und verhaelt sich
wie §6.2.  Wer den Gewinn will, ruft `kv.checkpoint`.

#### Nach dem Checkpoint gibt der Store den RAM-Index her

Der Checkpoint muss jeden Schluessel in die Hand nehmen: er schreibt die WAL neu,
also entsteht fuer jeden Eintrag ein neuer Offset, und aus genau diesen Offsets
besteht der neue Dump.  Was er NICHT darf, ist den vollstaendigen Schluesselsatz
danach liegen lassen.  Er sammelt die Paare `(Schluessel, Ort)` darum in einer
**Liste**, schreibt daraus den Dump, laedt ihn (§"Oeffnen") und steht hinterher
da wie ein frisch geoeffneter Store: Dump gemappt, Ueberlauf leer.

Zwei Gruende, beide gerechnet:

- Eine Liste kostet grob **60 Byte je Schluessel**, die `unordered_map` des
  Ueberlaufs **434** (die Rechnung steht am Anfang von §6.2.1).  Der Checkpoint
  ist damit nicht nur hinterher billiger, sondern auch waehrend seiner Laufzeit.
- Ohne das Abgeben ist **periodisches Checkpointen kein Mittel**, einen langen
  Schreiblauf im Speicher zu begrenzen — es hebt ihn im Gegenteil dauerhaft, weil
  der Ueberlauf danach alles traegt.  Genau dieses Mittel braucht aber jeder
  Neuaufbau eines grossen Gedaechtnisses aus Protokollen.

**Der Rueckfall ist Pflicht.** Scheitert das Schreiben des Dumps oder seine
Pruefung beim Laden, muss der Store den Ueberlauf aus der Liste auffuellen,
bevor er die Sperre abgibt.  Ein Store mit leerem Ueberlauf und ohne gueltigen
Dump haette seinen ganzen Inhalt verloren, ohne dass eine Zeile fehlt — der
teuerste Fehler, den dieser Abschnitt zulaesst.  Ohne `index_dump: true` gibt es
keine Liste: dort bleibt der Ueberlauf der Index, wie in §6.2.

#### Zusammenspiel

| | |
|---|---|
| `db.kv_evict` (§6.6) | wirkt jetzt auch auf den Index, weil er datei-gebunden ist.  Bisher hiess es dort ausdruecklich „keep index", und genau der Index war der groessere Posten. |
| `db.reload_kv` (§6.5) | liest den Dump neu und spielt den Rest ab Wasserstand nach, statt die ganze WAL. |
| `decoded_cache` (§6.7.3) | unberuehrt; die 1024er Grenze gilt weiter. |
| Zeigerverträge (§6.7) | unveraendert: Werte kommen weiter aus dem WAL-mmap. |

#### Kompatibilitaet — keine Migration

Das **WAL-Format bleibt unveraendert**.  `index_dump` legt eine zusaetzliche
Datei an und ruehrt Header und Satzaufbau der WAL nicht an.  Darum gibt es
nichts zu migrieren, in keiner Richtung:

| Fall | Verhalten |
|---|---|
| alte WAL, neues Binary mit `index_dump: true` | keine `.kvidx` vorhanden → voller Replay (§6.2); der Dump entsteht beim naechsten Checkpoint |
| neue WAL **mit** `.kvidx`, altes Binary | `.kvidx` ist dem alten Binary unbekannt und wird nicht angefasst → voller Replay, korrekt |
| altes Binary **schreibt** an die WAL an | Inode bleibt, Daten wachsen → das neue Binary spielt den Rest ab Wasserstand nach |
| altes Binary macht `checkpoint` | Inode wechselt → Dump wird verworfen → voller Replay |
| Gedaechtnis kopiert, verschoben, aus einem Backup geholt | Inode wechselt → Dump wird verworfen → voller Replay |

Eine verwaiste oder fremde `.kvidx` kann also nie zu falschen Daten fuehren,
sondern hoechstens zu einem langsamen Start.  Sie darf jederzeit geloescht
werden; sie wird beim naechsten Checkpoint neu geschrieben.

#### Wer ueber einen persistenten Kv laeuft

**Jede** Aufzaehlung aller Eintraege muss Ueberlauf UND Dump zusammenfassen und
die vom Ueberlauf verdeckten Dump-Schluessel auslassen.  `kv->index` allein ist
mit Dump **nicht** der Inhalt des Stores, sondern nur das, was seit dem letzten
Checkpoint geschrieben wurde -- bei einem frisch geoeffneten Store also nichts,
und nach einem `kv.checkpoint` ebenfalls nichts (§"Nach dem Checkpoint").

Das ist keine theoretische Warnung.  Das Auffuellen der Nebenindizes
(`db_indexed`, spec/45) lief ueber `kv->index` und baute damit einen **leeren**
Index: `findings_by` fand nichts, waehrend `kv.len` und `kv.get` korrekt blieben.
Am cai-Wissensgraphen gemessen: **0 statt 682593** exportierter Fakten, ohne
Fehlermeldung.  Gefunden wurde es nicht von den Unit-Tests, sondern vom
Vergleich eines vollen Exports mit und ohne Dump.

Regressionsprobe: `tests/test_kv_index_dump_5_indexed_write.aiml` +
`_6_read.aiml` (Phase 6 faellt, wenn das Auffuellen wieder den Ueberlauf liest).

#### Gemessen (06./07.11.2026)

Isoliert, an einem einfachen `Kv<string:string>` mit 2 Mio Schluesseln (ohne
`db_indexed`, damit nichts anderes die Zahlen verdeckt):

| Store oeffnen + drei Werte lesen | Zeit | RSS |
|---|---|---|
| Replay ueber den mmap-Zeiger | 1,60 s | 395 MB |
| **mit Index-Dump** | **0,12 s** | **128 MB** |

Also 13× schneller und 3× weniger Speicher; der Dump kostete dort 79 MB neben
128 MB WAL.  Grob **0,8 s und ~135 Byte je Million Schluessel**, die wegfallen.

Am Replay selbst, vorher und nachher (cai-Teilgraph s06, 5,83 Mio Schluessel):
`read()` je Feld 63,62 s → mmap-Zeiger 39,32 s.

**Treue** ist an echten Daten geprueft: nach Checkpoint und Dump sind
Schluesselmengen und Werte aller 5,83 Mio Eintraege von s06 bytegleich mit dem
Original (sechs Stores, 0 Abweichungen).

#### Wo der Gewinn verschwindet

Am cai-Wissensgraphen (18,17 Mio Schluessel) sank die Oeffnungszeit nur von
275,9 s auf 261,7 s.  Das passt genau zur isolierten Messung — 0,8 s je Mio
Schluessel sind bei 18,17 Mio eben 14 s -- und zeigt, dass dort etwas ANDERES
dominiert: das Auffuellen der `db_indexed`-Nebenindizes (spec/45) dekodiert bei
jedem Oeffnen jede Zeile und baut die Nebenindizes im RAM neu auf.  Gemessen am
geoeffneten s06: 1867 MB anonym (Nebenindizes) gegen 720 MB Datei-Abbildung
(der Dump).  Der Index-Dump nimmt dem Oeffnen den Primaerindex ab; wer die
Nebenindizes nicht ebenfalls haltbar macht, merkt davon wenig.

Daraus folgt der zweite Teil: `index_dump: true` macht AUCH die
`db_indexed`-Nebenindizes haltbar, je Feld eine Datei `<name>__<feld>.kvsidx`.
Format, Stempel und Grenzen stehen in spec/45 "Persistence" — ein eigenes
Schluesselwort gibt es dafuer nicht, weil ein Nebenindex ein Index dieses
Stores ist.

**Nachgemessen am 07.11.2026**, nachdem es den Nebenindex-Dump gibt — dieselbe
Art Gedaechtnis (`out/n0511_alles`, 3,7 GB WAL, 5,8 Mio Fakten), ein ganzer
`cai2 graph stats`-Start je Zeile:

| Zustand | Zeit | RSS |
|---|---|---|
| kein Dump | 251 s | 19,3 GB |
| nur `.kvidx` | 230 s | 18,1 GB |
| `.kvidx` + `.kvsidx` | **3,7 s** | **4,2 GB** |

Die Vorhersage dieses Abschnitts trifft also zu, und zwar deutlicher als
erwartet: der Primaerindex-Dump allein nimmt **8 %** ab, die Nebenindizes den
Rest.  Die Reihenfolge der Arbeit war damit falsch herum — wer nur §6.2.1
umsetzt, hat an einem solchen Store so gut wie nichts gewonnen.  Zahlen,
Dateigroessen und die Gegenproben stehen in spec/45 "Gemessen an cai".

Warnung aus eigener Erfahrung: die erste Messung ergab hier 0,06 s und 212 MB.
Das war der Stand MIT dem Nebenindex-Fehler von oben -- das Auffuellen tat
nichts, und der Fehler hat seine eigene Bestzahl erzeugt.  Wer eine
ueberraschend gute Zahl sieht, prueft zuerst, ob noch alles getan wird.

Zweitens, und unabhaengig vom Dump: der Dump laeuft **sortiert** statt in
Hash-Reihenfolge.  Wer sich -- bewusst oder nicht -- auf die Reihenfolge einer
Aufzaehlung verlassen hat, sieht andere Ergebnisse.  In cai hing daran die
Dimensionswahl fuer Relationen ohne Schemaeintrag ("der zuerst gesehene
gewinnt"): 729 von 9240 Relationen entschieden sich anders.  Nicht der Dump war
falsch, sondern die Annahme, eine `unordered_map` habe eine Reihenfolge.

#### Was der Dump NICHT loest

Der Aufbau des Ueberlauf-Index ist weiter O(WAL-Rest).  Wer viel schreibt und
selten checkpointet, zahlt beim Oeffnen wieder.  Der Dump verschiebt die Kosten
auf den Checkpoint, er schafft sie nicht ab.

### 6.3 Checkpoint

`db.checkpoint` compacts each persistent KV's WAL by removing
overwrite/delete tombstones, leaving only the live records. The
operation is **crash-safe via temp-file + atomic rename**:

1. Write all live records to `${name}.kvwal.compact` (new file)
2. `fsync` the temp
3. `munmap` the old file, `close` the old fd
4. `rename(temp, real)` — POSIX atomic on the same filesystem
5. mmap the new file

If the process crashes between steps 1 and 4, the old file is intact;
on next open, the orphan `.compact` is automatically cleaned up at the
start of the next checkpoint.

```aiml
- call: [db.checkpoint]        # Manual: compact all persistent KVs/Stores/TS
- call: [db.auto_checkpoint, 1000]  # Automatic every 1000 writes
```

> **Pointer invalidation:** `kv.get` pointers from before `db.checkpoint`
> are **invalid** afterwards (the underlying mmap is replaced). Re-fetch
> them via `kv.get` after the checkpoint.

### 6.4 Storage Root — `db.set_path` und `AIONDB_PATH`

Bis 2026-04-30 nutzte AionDB einen festen Default `./aiondb/` relativ zum
Working-Directory. Das fuehrt zu zwei Problemen:

1. **IDE-Watcher** scannen das Projekt-Verzeichnis und ersticken an den
   sparse-extended `.kvwal`-Dateien (apparent size MB-GB pro File).
2. **Mehrere Tools** im selben Repo schreiben in dasselbe `aiondb/`,
   was zu Verwechslungen fuehrt.

`db.set_path` setzt einen globalen Speicher-Root, der von allen
nachfolgenden `db.open*`-Aufrufen genutzt wird:

```aiml
- call: [db.set_path, "/home/user/.cache/myproject/aiondb"]
- decl: [kv, Kv<string:string>, { call: [db.open_kv, "config", {persist: true}] }]
# → /home/user/.cache/myproject/aiondb/config.kvwal

- decl: [traj, ..., { call: [db.open_kv, "trajectory", {persist: true, path: "runs/"}] }]
# → /home/user/.cache/myproject/aiondb/runs/trajectory.kvwal
```

#### Resolution-Reihenfolge

Wird **bei jedem** `open_*` Aufruf neu evaluiert:

1. **`AIONDB_PATH` env var** (dominiert immer)
2. via `db.set_path()` gesetzter Wert
3. Fallback `./aiondb/`

Anschliessend wird das `path:` Argument der `open`-Funktion angehangen:

| `path:` Argument | Verhalten |
|---|---|
| nicht gesetzt / leer | nur Root nutzen |
| relativer Pfad (`"sub/dir/"`) | Root + Sub-Pfad anhaengen |
| absoluter Pfad (`"/tmp/..."`) | as-is, Root wird ignoriert (backward-compat fuer Tests) |

Das Verzeichnis wird beim `open` **angelegt, einschliesslich aller Zwischenebenen**.  Bis
2026-10-15 war das ein einstufiges `mkdir`: ein `path: "sub/dir/"` schlug beim ersten
Oeffnen mit `ENOENT` fehl, der open-Pfad lief weiter, und der Store blieb still leer.  Das
Beispiel `runs/trajectory.kvwal` zwei Absaetze weiter oben war damit unbelegt.

#### Beispiele

```bash
# Tul-Programm setzt Default im AIML-Code:
# - call: [db.set_path, "/home/user/.cache/tul"]

# Programm-Default verwenden:
./tul_train ...
# → /home/user/.cache/tul/...

# Ueberschreiben via env var:
AIONDB_PATH=/data/aiml/run42 ./tul_train ...
# → /data/aiml/run42/...
```

#### Designziele

- **Programmatischer Default** im AIML-Code via `db.set_path` — Apps wie
  `tul_train` koennen einen sinnvollen Default-Pfad einprogrammieren
  (z.B. unter `~/.cache/`), ohne dass jedes CLI-Aufruf einen Pfad
  mitgeben muss.
- **Operative Override** via env var ohne Source-Aenderung —
  fuer Container, CI, Multi-Run-Setups.
- **Test-Kompatibilitaet** durch absolute-Path-Pass-Through —
  bestehende Tests die `/tmp/aiml_test_xyz/` nutzen funktionieren
  unveraendert.
- **IDE-Friendly Default-Layout** — wenn der Root ausserhalb des
  Workspace liegt, sieht kein File-Watcher die Sparse-Files.

> **Hinweis:** `db.set_path` aendert nur den Default fuer **zukuenftige**
> open-Aufrufe. Bereits geoeffnete Stores behalten ihren urspruenglich
> gewaehlten Pfad.

### 6.5 Cross-Process Sync — db.reload_kv

AionDB loads the WAL file exactly **once** on the first `open_kv` call (singleton per name).
If another process (e.g. the backend) writes new entries after startup, the
current process does **not see them automatically**.

`db.reload_kv(handle)` solves this problem: it closes and reopens the WAL file descriptor,
clears the in-memory cache, and replays the WAL from the beginning. Afterwards, all
entries written by other processes in the meantime are visible.

```aiml
- decl: [kv, Kv<string:DeckSet>, g.db_decksets]
- call: [db.reload_kv, kv]          # Re-read WAL from disk
- decl: [has, i32, { call: [kv.has, some_id] }]
```

**When to use:** Whenever a process opens a persistent KV and then needs to read data
written by another process — especially with frontend/backend separation sharing a
common `./aiondb/` directory.

**When not needed:** Within a single process that both writes and reads itself
(the writing `kv.set` call already updates the in-memory cache).

### 6.6 Memory Eviction — db.kv_evict

Long-running processes (e.g. per-file training) may accumulate large
mmap-resident page pages that are no longer needed in the current phase.

`db.kv_evict(handle)` releases those pages back to the OS via
`madvise(MADV_DONTNEED)`. The **index stays intact**, the file stays
open, and subsequent reads transparently re-fault the pages from disk.

```aiml
- call: [db.kv_evict, kv]      # release mmap pages, keep index + handle
#                              # mit index_dump: true ist der Index selbst
#                              # datei-gebunden und wird mit freigegeben (§6.2.1)
- decl: [v, string, { call: [kv.get, "k"] }]   # works — page faults in
```

**Semantics**

| Operation | Effect |
|---|---|
| `kv.get / has / len / iter / get_blob` | Index lookup unchanged; mmap re-faults on touch |
| `kv.set` | Appends to mmap (pages re-fault into RAM) |
| `kv.delete` | Index entry removed; tombstone written to WAL |
| `db.reload_kv` | Reopens fd, rebuilds index from WAL |

**Typed mode (`Kv<string:Foo>`):** the `decoded_cache` (LRU-bounded to
1024 entries) is fully cleared on `evict`. Subsequent reads decode from
the mmap'd serialized blob.

**Non-persistent KVs (`{persist: false}`):** there is no mmap; `evict`
clears the in-memory map (data is lost — same semantic as before).

**Tensor stores** (opened internally via `tensor.save`/`tensor.load`)
expose `tensor.kv_evict(store_name)` which resolves the singleton
handle automatically:

```aiml
- call: [tensor.kv_evict, "${cfg.db_tag}_pf${fi}_tul_trajectory"]
```

> **When to use:** After writing data that won't be re-read soon. The
> page-faulting cost on next read is paid only for actually-touched
> pages; cold data stays on disk.

### 6.7 Pointer Lifetime Contract for `Kv<K,V>`

`kv.get(key)` returns a pointer that depends on the value mode and
storage class:

| Mode | Source | Invalidated by |
|---|---|---|
| **Blob / Fixed (persistent)** | mmap'd WAL | `kv.checkpoint` / `kv.clear` / `kv.set` *only if it triggers `mremap`-grow* |
| **String (persistent)** | mmap'd WAL (with `\0` terminator) | same as above |
| **Typed (persistent)** | `decoded_cache[key]` | `kv.set(key,_) / kv.delete(key) / kv.evict / kv.clear / kv.checkpoint`, **plus LRU eviction** when cache > 1024 entries |
| **Any mode (non-persistent)** | `kv->map[key]` (typed: `decoded_cache[key]`) | `kv.set(key,_) / kv.delete(key) / kv.clear / kv.evict` |

**Self-write exception (typed, both storage classes).** `kv.set(key, p)` where `p` is the
`ref<T>` that `kv.get(key)` returned — i.e. the value **is** the key's own cache slot — does
**not** invalidate `p`: the slot is encoded and persisted, never released (a self-write is a
no-op for the slot, spec/61 §5.5 step 0). Only a `kv.set(key, v)` with any other value `v`
replaces and frees the slot. See §6.7.1.

**Important changes from earlier versions:**

- **Blob/Fixed/String pointers are no longer invalidated by `kv.set` on
  a different key.** Old WAL records stay in the file; the index just
  points to the latest. Pointers from earlier `kv.get` calls remain
  valid until the entire mmap is replaced (`checkpoint` / `clear`) or
  the file grows (`mremap-grow`, only when total writes exceed the
  initial 16 MB reserve).
- **Typed pointers can be invalidated by `kv.get` on other keys**
  (LRU eviction when more than 1024 distinct typed entries are read).
  For typical workloads this is harmless; for >1024 simultaneously held
  typed pointers, set `MAX_DECODED_LRU` higher or rebuild your access
  pattern.
- **`kv.evict` is now safe across pointer holders for blob/fixed/string**
  — it only releases pages, not the mapping. Pointers remain valid.

> **Recommendation:** Treat `kv.get` pointers as **borrowed, short-lived
> references**. Don't store them across `checkpoint`/`clear`/`mremap-
> grow` boundaries. For long-lived references in typed mode, copy the
> struct fields you need.

### 6.7.1 Typed Kv Access Patterns (`kv.get`, `kv.get_copy`, `ref<T>`)

Typed `Kv<K:T>` where `T` is a struct with heap fields (strings, lists,
maps, buffers) requires **explicit choice** between three access patterns,
enforced at compile time:

| Pattern | Declaration | Semantics | When to use |
|---|---|---|---|
| **Zero-copy view** | `decl: [p, ref<T>, kv.get(k)]` | `p` is a borrow of the decoded_cache slot (spec/61 §5.4). Mutations land directly in the cache. `kv.set(k, p)` encodes the cache and writes WAL; eviction is skipped because the cache IS the canonical state — the slot stays, so `p` remains valid after the set (self-write, §6.7). | Read-only fast access, or in-place updates that persist back to the same key. |
| **Deep-copy value** | `decl: [p, T, kv.get_copy(k)]` | `p` has its own malloc'd heap allocations (encode-then-decode roundtrip at decl site). Mutations to `p` do NOT touch the cache. `kv.set(k, p)` encodes `p`, then evicts the cache. | Local mutations that might or might not be persisted; or when you want isolation from cache state. |
| **— ERROR —** | `decl: [p, T, kv.get(k)]` | Would do shallow memcpy → `p` shares heap pointers with cache → use-after-free on mutate-and-set-same-key (see `doc/issue/kv_get_mutate_set_same_key_crash.md`). | **Compiler rejects** with diagnostic suggesting `ref<T> + kv.get` or `T + kv.get_copy`. |
| **— ERROR —** | `decl: [p, ref<T>, kv.get_copy(k)]` | Semantically contradictory: deep-copy wasted on a borrow. | **Compiler rejects** with diagnostic suggesting `T + kv.get_copy` or `ref<T> + kv.get`. |
| **— ERROR —** | `decl: [p, ptr<T>, kv.get(k)]` | A `ptr<T>` is a unique owner; the cache slot is owned by the store (spec/61 §5.4). | **Compiler rejects** with diagnostic suggesting `ref<T>` or `get_copy`. |

```aiml
- model:
    name: Player
    fields:
      - [player_id, string]
      - [creature_ids, list<string>]

# (1) Zero-copy view — cheapest path, mutations propagate to cache.
- decl: [p, ref<Player>, { call: [players.get, "n@g.de"] }]
- call: [p.creature_ids.push, "c-1"]
- call: [players.set, "n@g.de", p]      # safe — cache is canonical

# (2) Deep-copy — independent struct, costs one encode+decode pass.
- decl: [p, Player, { call: [players.get_copy, "n@g.de"] }]
- call: [p.creature_ids.push, "c-1"]
- call: [players.set, "n@g.de", p]      # safe — encode reads p, then evict cache
```

The compiler implements the diagnostic in `src/core/ast_compiler_stmt.cpp::
visitDeclStmt` by reading `ctx.kvGetCalled` / `ctx.kvGetCopy` flags set by
the Kv method dispatch in `ast_compiler_call.cpp`. Primitive types
(`string`, `i32`, …) and structs without heap fields (`hasHeapFields == false`)
are exempt — there's nothing to share/free.

For struct types `T` registered without heap fields (only primitive members),
both `ref<T>+get` and `T+get` are safe; the compiler does not emit a
diagnostic.

`kv.set` is uniform — it inspects the value's type at compile time:
- value-type `T` arg → spill to temp alloca, encode struct bytes
- `ref<T>` arg → pass pointer through directly (no temp alloca; encode reads
  through the pointer)

At runtime, `aiml_db_kv_set` additionally checks if `val` aliases the
current decoded_cache slot for `key`; if so, eviction is replaced with a
plain bookkeeping drop (no `free_decoded`) — the cache and user struct
share the same heap pointers (some of which may be user-pushed literals
or non-decoded data), and double-freeing them is undefined.

### 6.7.2 Reusable deep-copy into a caller buffer (`kv.get_copy_at`)

`kv.get_copy(k)` allocates a **fresh independent deep-copy on every call**.  In a
hot loop that copies the same record each iteration (e.g. a per-tick scheduler that
does `decl: [m, T, kv.get_copy(k)]` every frame), that is one full heap allocation
per iteration.  Until the codegen frees value-type locals at scope exit, those
copies accumulate; and even once it does, the allocate/free churn is pure overhead.

`kv.get_copy_at(key, dst)` gives the **same isolation as `get_copy` with a bounded,
non-accumulating footprint**: it deep-copies the stored value **into a caller-owned
buffer that is reused across calls**.  The reused thing is the **struct shell**; the
nested heap fields (lists/strings/maps) are still re-decoded (allocated) each call,
with the buffer's previous heap freed first — so the live footprint stays bounded
across calls (no accumulation), but each call is not literally zero-allocation.

| Pattern | Declaration / call | Semantics | When to use |
|---|---|---|---|
| **Reusable deep-copy** | `decl: [buf, T]` **once**, then `kv.get_copy_at(k, {'&': [buf]})` per use (or `kv.get_copy_at(k, pbuf)` with `pbuf: ptr<T>`) | Before refilling, the runtime **deep-frees `buf`'s current heap fields**, then decodes the stored value fresh into `buf`.  `buf` is fully independent of the decoded_cache (like `get_copy`).  Returns `i32` — `1` = found & copied, `0` = missing key (`buf` left unchanged), `-1` = value present but the blob failed to decode (schema mismatch / corrupt): `buf` is deep-freed and zero-inited, so it is **safe to reuse but holds no valid value** — the caller must not read/tick/write it back. | Hot paths that repeatedly copy a record into the same working buffer (scheduler tick, per-frame reads) — bounded footprint, no accumulation across calls (decode still allocates the heap fields each call, but the prior ones are freed first). |

Ownership / lifetime contract:

- The caller owns `buf` and declares it **once** (a class member, or a local hoisted
  out of the loop).  `get_copy_at` never allocates `buf` itself.
- Each call is self-cleaning: `buf`'s previous heap (nested lists/strings/maps) is
  freed **before** the new decode overwrites it, so repeated calls into the same
  `buf` do **not** grow memory.  On the first call `buf` must be zero-initialised
  (the default for `decl: [buf, T]`); freeing zeroed fields is a no-op.
- After the call, `buf` holds an **independent** deep copy: mutating it does not touch
  the cache; `kv.set(key, buf)` encodes `buf` then evicts, exactly like the `get_copy`
  value path.
- `buf`'s type must be the Kv's value type `T`.  `T` without heap fields degrades to a
  plain byte copy (still valid).  The `dst` argument is a `ptr<T>` (pass `{'&': [buf]}`
  for a value-type local, or a `ptr<T>` directly).
- **Decode-error contract (`-1`):** if the stored blob cannot be decoded into `T`
  (schema fingerprint mismatch after a model change, or a structurally corrupt blob),
  the runtime deep-frees any partially-decoded fields, zero-inits `buf`, and returns
  `-1`.  Callers **must** branch on the result and only read/tick/write-back `buf`
  when the result is `1`.  Because a schema-mismatched blob stays undecodable until it
  is rewritten, a hot loop should record such keys and skip them rather than decode
  them every iteration (each failed decode re-emits the `[ser-FATAL]` diagnostic).

```aiml
# Reusable buffer — ONE allocation for the lifetime of the owner.
- class:
    name: Scheduler
    members:
      - [work, MatchSession]     # reused per tick, never re-alloc'd
    methods:
      - def:
          name: tick_one
          args: [[mid, string]]
          body:
            - decl: [ok, i32, { call: [<.matches.get_copy_at, mid, {'&': [<.work]}] }]
            # Only 1 means <.work holds a valid copy; 0 = missing, -1 = undecodable.
            - if: { condition: { neq: [ok, 1] }, then: [{ return }] }
            # <.work is now an independent copy of matches[mid]; mutate + set back.
            - call: [<.matches.set, mid, <.work]
```

Runtime: `aiml_db_kv_get_copy_at(kv, key, dst)` — missing key → return `0` (dst
untouched); else `aiml_ser_free_decoded(dst, nodes, n)` then `aiml_ser_decode(wal_val,
dst, nodes, n)` → return `1`.  Never returns the cache slot; `dst` is always
caller-owned and independent.

### 6.7.3 Who fills the `decoded_cache` — and what the 1024 bound guarantees

The 1024-entry bound of §6.7 is only a bound if **reads are the only thing that populates
the cache**, and the pointer contract only holds if the trim keeps its hands off the entry
the current call is about to hand back. Both rules are normative:

1. **A read never returns a slot it has just released.** `kv.get` / `kv.get_copy` insert the
   freshly decoded value, mark it most-recently-used, and only then trim. That trim evicts
   from the cold end and **must leave the most-recently-used entry alone** — it is the one
   being returned. A trim that drains the whole list hands the caller a pointer into a
   freed slot; the next read of that value walks released string pointers.
2. **Only reads populate the cache.** Index maintenance must not. The `db_indexed` backfill
   that `db.open_kv` emits for every indexed field walks *all* existing rows; it may *use* a
   cached decode when one is there, but a row it decodes itself belongs in its own buffer
   and is released again (`aiml_ser_free_decoded`) once the field value is indexed. If the
   backfill published those rows into `decoded_cache` instead, opening a store with N rows
   would pin N decoded rows — the documented bound would not be a bound — and the cache
   would hold entries the LRU list never heard of.

Rule 2 is what makes rule 1 reachable when it is broken: once `decoded_cache.size()`
exceeds the LRU list's length, the trim can never get back under the bound by popping that
list, so it pops **all** of it — including the entry just inserted and returned.

Measured on 2026-11-04 (cai, German Wikipedia into the knowledge graph): the name store
holds two `db_indexed` fields and tens of thousands of rows, so every `db.open_kv` filled
the cache with every row and left the LRU list nearly empty. Reads kept hitting that cache
and looked healthy for 50 minutes. The first `kv.set` on a name evicted its key, the next
read of that same name missed, and the trim then freed the slot that read returned — the
process died in `aiml_ser_deep_copy` → `enc` → `strlen` on a released string, 50 minutes of
graph work lost with it. A store under 1024 rows never showed it. Regression:
`tests/kv_decoded_cache/run.sh` (1200 rows, two processes over one WAL).

What rule 2 costs when it is broken, measured in the same session: a cai process that
opens its nine stores and does nothing else (`graph stats`) peaked at **289 MiB** before the
fix and **258–260 MiB** after it — ~30 MiB pinned in read caches at open time, with no read
having happened yet. Against a store set with no rows the difference is **0 MiB**, which is
the signature to look for: the old behaviour grew with the row count, the fixed one does
not. (Run-to-run spread ±2 MiB. The per-row cost is ~250–300 bytes — 32-byte struct, four
string headers, the map node with its key — so one 18k-row store accounts for ~5 MiB and the
30 MiB are the several indexed stores together.)

### 6.8 Migration Notes — Format Versions

Each `.kvwal` file starts with a 32-byte header containing magic
`AIMLKVWL`, format version, mode flags, and `wal_used` cursor.

| Version | Format | Note |
|---|---|---|
| (pre) | no header, raw records | rejected on open with FATAL |
| 1 | header + records, strings without `\0` | rejected on open with FATAL |
| **2** | header + records, **strings include `\0` terminator** for zero-copy reads | current |

**Upgrading to a new build:** delete all stale `.kvwal` files
(`rm -rf aiondb/`) and re-create the data. The runtime aborts with a
clear FATAL message if it encounters a file from an older version.

**Sparse-file operational notes** (apparent size 16 MB+ per KV):

- `cp --sparse=always` to copy without inflating
- `rsync -S` (or `--sparse`)
- `tar --sparse` (or `-S`)
- `ls -la` shows apparent size; `du -h` shows real disk usage
- Disk quotas count real blocks, not apparent

> **9rooms migration step:** before the first run with the
> mmap-refactored libaiml, remove the `aiondb/` directory (or at least
> all `*.kvwal` files). The game re-populates them on next launch. No
> AIML source changes required — the C-API and AIML method signatures
> are stable.

### 6.9 Shared W₀ — pf_ensure_base_weights

Per-file training uses the merge formula `W_new = W_0 + μ_post`, which requires
all sub-runs to share the **same** starting weights W₀.

Without explicit initialization, each worker would independently random-init
(via `train_transformer_on_texts` with `resuming=0`), producing divergent W₀
values and invalidating the merge.

`pf_ensure_base_weights(lm_bpe, cfg)` runs on the **main thread** before worker
start:

1. Builds `base_store = build_store_name(cfg.pf_db_tag, "tul_transformer")`
2. Checks `tensor.load(base_store, "tok_embed")` — if non-null, returns (already
   initialized, e.g. from a prior training run)
3. If null: random-inits global weights + calls `init_layer_lists(base_store, 0, cfg)`
   + calls `save_transformer_weights(...)` to persist to WAL
4. All workers then find weights in the base store and start with `resuming=2`

```aiml
# In train_per_file, after pf_precollect, before worker start:
- call: [pf_ensure_base_weights, lm_bpe, cfg]
```

The merge's `mp.base_store` points to `build_store_name(cfg.pf_db_tag, "tul_transformer")`
which is guaranteed to have weights after this call.

### 6.10 Die Typisierung gehoert dem Store, nicht dem Oeffner

Ein `Kv<K:V>` wird von seiner **ersten** Oeffnung typisiert.  `db.open_kv` fuehrt
eine Registratur je `(Verzeichnis, Name)`; jede weitere Oeffnung desselben Stores
im selben Prozess bekommt **denselben** Handle zurueck -- mit der `val_size` und
dem Deskriptor der ersten Oeffnung (§6.4 bestimmt das Verzeichnis).

Daraus folgt eine normative Regel:

> **Jede Oeffnung eines Stores muss denselben Werttyp verlangen.**  Weicht eine
> spaetere Oeffnung in `val_size`, in der Typisierung (typisiert vs. roh) oder im
> Schema-Fingerabdruck des Deskriptors ab, ist das ein **Programmierfehler**.  Die
> Laufzeit meldet ihn beim Oeffnen laut und bricht ab; sie darf den abweichenden
> Oeffner nicht stillschweigend bedienen.

Der Grund, warum das nicht tiefer abgefangen werden kann: `kv.get` dekodiert mit
dem Deskriptor des **Handles** und gibt einen Zeiger auf den dekodierten Satz
zurueck; die Aufrufstelle liest ihn als ihren eigenen **deklarierten** Typ.  Der
Fingerabdruck, der jeden KV-Blob schuetzt (§7, `aiml_ser_decode`), kann das nicht
sehen -- Blob und Handle passen ja zusammen, nur der Leser passt nicht.  Ohne
Wache am Oeffnen liest der abweichende Leser die Bytes eines fremden Satzaufbaus:
`has` sagt 1, der Satz sieht plausibel aus, und der erste Zeichenketten-Zeiger,
der auf Nicht-Zeiger-Bytes faellt, stuerzt beim Weitergeben ab
(`aiml_ser_encode` <- `aiml_ser_deep_copy`).

Dazu traegt der WAL-Kopf (§6.8) `flags` (typisiert) und `val_size` der
**erzeugenden** Oeffnung.  Beim Oeffnen einer vorhandenen Datei werden beide mit
dem Verlangten verglichen; eine Abweichung wird **gemeldet, aber nicht** zum
Abbruch gemacht -- ein angehaengtes Feld (§7, Schema-Evolution) aendert `val_size`
zulaessig, und der Blob-Fingerabdruck sagt dann beim Lesen, was zu tun ist.

**Wo die Regel beisst.**  Beim Global stehen Name und Typ in *einer* Zeile:

```aiml
- decl: [disc_enemies_kv, Kv<string:Enemy>, { call: [db.open_kv, "disc_enemies", {persist: true}] }]
```

Zieht man den Zustand nach spec/58 in eine Klasse oder eine classgroup-Instanz,
stehen sie an **zwei** Stellen -- der Typ im Klassenkoerper, der Name im
Konstruktor:

```aiml
- class:
    name: WeltSpeicher
    members:
      - [feinde, Kv<string:Enemy>]        # Typ hier
    methods:
      - def:
          name: oeffne
          body:
            - set: [<.feinde, { call: [db.open_kv, "disc_enemies", {persist: true}] }]   # Name hier
```

Das ist zulaessig und richtig typisiert (eine Probe ueber Global, Klassenmitglied,
classgroup-Instanz, Gruppenmethode und Zwei-Schritt-Leihe zeigt in allen fuenf
Wegen den typisierten Oeffnungsaufruf).  Waehrend einer **schrittweisen**
Migration oeffnen aber Global und Mitglied denselben Store, und ein vertauschtes
Paar (Name des einen Stores, Typ des anderen) ist dann genau der Fall oben: kein
Compiler-Fehler, kein Laufzeit-Wort, ein fremd gelesener Satz.  Gefunden am
11.11.2026 in datacreatures (abgebrochener Repository-Pilot), nachgestellt in
`tests/kv_typ_divergenz/`.

---

## 7. Schema Evolution — Integration with Hot-Reload

The most powerful feature of AionDB is the seamless integration with the
**class versioning system** from Chapter 30.

When a class is updated from `version: 1` to `version: 2`
(new field added — always **append only**, never remove or reorder fields),
AionDB detects this automatically on the next store open:

```aiml
# Program v1 — Entity has 3 fields:
- class:
    name: Entity
    version: 1
    members:
      - [id,    i32]
      - [pos_x, f32]
      - [hp,    i32]

# Program v2 — Entity gets a new field (append only!):
- class:
    name: Entity
    version: 2
    members:
      - [id,       i32]
      - [pos_x,    f32]
      - [hp,       i32]
      - [mana,     i32]   # NEW — patched into existing rows with default value 0
```

AionDB compares the schema registry (Chapter 30.1) with the stored schema of the page.
If it detects a higher version with new fields, it applies **in-place migration**:

1. All existing rows already have space reserved for new fields via the `padded_size` system.
2. AionDB writes the default value of the new field into the padding area.
3. From that point on, `p.mana` reads the correct value — no migration scripts, no downtime.

---

## 8. Complete Usage Examples

### 8.1 Game Loop with ECS

```aiml
# --- ECS components as model (pure data, column layout automatic) ---
- model:
    name: Position
    members: [[x, f32], [y, f32]]

- model:
    name: Velocity
    members: [[dx, f32], [dy, f32]]

- model:
    name: Health
    members: [[hp, i32]]

# --- Open stores ---
- decl: [positions,  Store<Position>]
- decl: [velocities, Store<Velocity>]
- decl: [health,     Store<Health>]
- set:  [positions,  { call: [db.open, "positions"] }]
- set:  [velocities, { call: [db.open, "velocities"] }]
- set:  [health,     { call: [db.open, "health"] }]

- decl: [history, TimeSeries<Health>]
- set:  [history, { call: [db.open_timeseries, "health_history"] }]

# --- Define systems ---

# Movement system: no eid needed — pure data transformation
# Column layout → SIMD processes 8 pos_x values simultaneously
- def:
    name: move_system
    args: [[pos, ref<Position>], [vel, ref<Velocity>]]
    body:
      - set: [pos.x, { +: [pos.x, vel.dx] }]
      - set: [pos.y, { +: [pos.y, vel.dy] }]

# Damage system: eid needed — deletes entity from multiple stores
- def:
    name: damage_system
    args: [[eid, i32], [h, ref<Health>]]
    body:
      - if: [{ <=: [h.hp, 0] }, true]
        then:
          - call: [health.delete, eid]
          - call: [positions.delete, eid]
          - call: [velocities.delete, eid]

# --- Initialization: spawn 1000 enemies ---
- decl: [i, i32]
- set: [i, 0]
- while: [{ <: [i, 1000] }]
  body:
    - decl: [pos, Position]
    - decl: [vel, Velocity]
    - decl: [hp,  Health]
    - set: [pos.x, { call: [math.random, 0.0, 1000.0] }]
    - set: [vel.dx, 1.0]
    - set: [hp.hp, 100]
    - call: [positions.insert,  pos]
    - call: [velocities.insert, vel]
    - call: [health.insert,     hp]
    - set: [i, { +: [i, 1] }]

# --- Game loop ---
- def:
    name: game_loop
    return: void
    body:
      - call: [db.begin_frame, { call: [time.now] }]

      # Compiler generates intersection loop: positions ∩ velocities
      - call: [db.run_system, move_system, positions, velocities]

      # Compiler generates loop over health alone
      - call: [db.run_system, damage_system, health]

      - call: [db.end_frame]
```

### 8.2 AI Application: Feature Store for ML Training

```aiml
# Collect training data for a reinforcement learning agent:

- class:
    name: Observation
    members:
      - [frame,      i64]
      - [pos_x,      f32]
      - [pos_y,      f32]
      - [enemy_dist, f32]
      - [action,     i32]   # 0=up, 1=down, 2=left, 3=right
      - [reward,     f32]

- decl: [replay_buffer, Store<Observation>]
- set:  [replay_buffer, { call: [db.open, "rl_replay", {max_size: 1000000}] }]

- def:
    name: record_step
    args: [[obs, Observation]]
    return: void
    body:
      - call: [replay_buffer.insert, obs]

# Mini-batch for training (random 64 samples):
- def:
    name: sample_batch
    return: list<ref<Observation>>
    body:
      - return: { call: [replay_buffer.sample_random, 64] }
```

### 8.3 AI Application: RAG with Vector Search

```aiml
# Retrieval-Augmented Generation: store documents and find similar ones

- class:
    name: Document
    members:
      - [doc_id,   string]
      - [title,    string]
      - [content,  string]

- decl: [docs,       Store<Document>]
- decl: [doc_vecs,   VecStore]
- set:  [docs,     { call: [db.open,     "documents"] }]
- set:  [doc_vecs, { call: [db.open_vec, "doc_embeddings", {dims: 768}] }]

# Insert document + store embedding:
- def:
    name: index_document
    args: [[doc, Document], [embedding, list<f32>]]
    return: void
    body:
      - call: [docs.insert, doc]
      - call: [doc_vecs.insert, doc.doc_id, embedding]

# Find similar documents for a query (k=3):
- def:
    name: retrieve
    args: [[query_embedding, list<f32>]]
    return: list<Document>
    body:
      - decl: [matches, list<VecResult>]
      - set:  [matches, { call: [doc_vecs.search, query_embedding, 3] }]

      - decl: [result, list<Document>]
      - decl: [i, i32, 0]
      - while:
          condition: { lt: [i, matches.len] }
          body:
            - decl: [m, VecResult, { at: [matches, i] }]
            - decl: [d, ref<Document>]
            - set:  [d, { call: [docs.get, m.id] }]
            - call: [result.push, d]
            - set:  [i, { +: [i, 1] }]
      - return: result
```

---

## 9. Performance Characteristics

| Operation | Complexity | Note |
|-----------|------------|-------|
| `store.get(id)` | O(1) | Slot index (no primary_key) or HashMap lookup |
| `store.insert(v)` | O(1) amortized | Append to active page |
| `db.query(store, ...)` | O(N) | Can become O(log N) with index |
| `db.run_system(fn, s1, s2)` | O(min(|s1|,|s2|)) | Intersection via slot bitmap |
| `kv.get(key)` | O(1) | Hash table |
| `kv.set(key, val)` | O(1) amortized | Including the `db_indexed` hook — independent of how many rows share a field value (spec/45) |
| `timeseries.at(t)` | O(log N) | Binary search on timestamps |
| `timeseries.range(a,b)` | O(K) | K = number of results |
| `vecstore.search(v, k)` | O(log N) | HNSW index |
| Column read (SIMD) | O(N/8) | 8× faster than scalar loop |

---

## 10. Thread Safety

All `db` operations are **implicitly thread-safe** (category 1, see Chapter 47).
The runtime guarantees:

- **Singleton handles:** `db.open_kv("name", {persist: true})` always returns the same
  handle for the same name+path — no duplicate WAL, no memory leak.
- **Atomic single operations:** `kv.set`, `kv.get`, `kv.has`, `kv.delete`, `store.insert`,
  `store.get` are each atomic. Concurrent calls from different threads
  on the same handle are safe.
- **Checkpoint safety:** `db.checkpoint` is serialized — no race between
  checkpoint and set/delete.

**Not atomic** are sequences of multiple calls (e.g. `has` + `set`).
For atomic read-modify-write the AIML author must use an explicit `lock`.

---

## 11. AionDB2 — Next-Generation Storage Engine

The existing AionDB design in this chapter remains the **stable compatibility path**.
Its `Store<T>` mmap/page model, WAL behavior, singleton handles, and current KV
semantics must continue to work unchanged so that existing AIML projects do not break.

For larger persistent datasets and more traditional on-disk database structures,
we introduce a separate engine line: **`aiondb2`**.

The key architectural rule is:

- **No refactor-in-place of AionDB v1 semantics**
- **No silent behavior changes for `db.open`, `db.open_kv`, `db.open_timeseries`**
- **New storage engine lives beside the old one, with explicit opt-in**

### 11.1 Why `aiondb2` instead of refactoring `aiondb`

The current AionDB design is strongly optimized around:

- zero-copy pointers into mapped pages
- ECS-style direct component access
- simple WAL replay semantics
- language/runtime integration that assumes stable pointer behavior

An industrial on-disk engine with B-tree or LSM-tree internals introduces a
different world:

- custom block/page formats
- explicit pager or compaction layers
- separate read/write paths
- on-disk indexes and recovery metadata
- different pointer lifetime and caching rules

Trying to retrofit these properties into the original engine would risk breaking:

- zero-copy assumptions in `Store<T>`
- KV reload/evict semantics
- current performance expectations
- old databases and deployed programs

Therefore `aiondb2` is specified as a **new backend family**, not as a hidden rewrite.

### 11.2 Positioning

`aiondb2` is intended for cases where the original mmap/WAL-first design becomes
awkward or too memory-coupled:

- very large KV datasets
- range scans over sorted keys
- secondary indexes
- remote or partially local storage
- workloads where direct `ref<T>` borrows into mapped pages are not the dominant advantage

`aiondb` remains the natural choice for:

- ECS component stores
- local zero-copy object access
- page-mapped entity data
- tightly integrated single-process runtime access

### 11.3 Planned engine styles

`aiondb2` may expose more than one internal engine strategy, but all under a
new explicit namespace/API surface.

Candidate implementations:

- **B-tree / B+tree engine**: best fit for sorted key lookup, range scans,
  page-oriented persistence, and LMDB-style designs.
- **LSM-tree engine**: best fit for write-heavy append workloads, blob stores,
  and large KV datasets where compaction is acceptable.

The final choice may be:

- one default engine only, or
- multiple backends selectable by option

This chapter does **not** require that `aiondb2` preserves the same internal
pointer model as `aiondb`.

### 11.4 API separation

To avoid ambiguity, `aiondb2` must be opened explicitly. The old API keeps its
current meaning.

Illustrative direction:

```aiml
# Existing engine, unchanged semantics:
- decl: [entities, Store<Entity>]
- set:  [entities, { call: [db.open, "entities"] }]

# New engine family, explicit opt-in:
- decl: [cfg2, Db2Kv<string:PlayerConfig>]
- set:  [cfg2, { call: [db2.open_kv, "player_configs"] }]
```

Possible naming options:

- reserved namespace `db2`
- explicit backend option, e.g. `{engine: "aiondb2"}`
- distinct handle types such as `Db2Store<T>`, `Db2Kv<K:V>`

Preferred rule: **new namespace plus new handle types**, because this prevents
accidental mixing at compile time.

### 11.5 Pointer and loading semantics

Unlike `aiondb`, `aiondb2` must not promise universal raw zero-copy pointers for
all store types.

Allowed models for `aiondb2`:

- page-pinned pointer valid for the current transaction/snapshot
- decoded object view backed by an internal cache
- iterator/cursor API instead of raw `ref<T>` borrows
- streaming reads from block storage

This means `aiondb2` is free to read data:

- from mmapd blocks,
- from pager-managed file pages,
- or via direct file-handle / block reads

depending on engine design.

In other words: **direct file-handle reads are valid in `aiondb2`**, but they are
an implementation choice of the new engine, not a semantic change to `aiondb`.

### 11.6 Compatibility contract

The compatibility contract between both generations is:

- `aiondb` databases continue to open with `db.*`
- `aiondb2` databases open only with the new API
- no automatic in-place upgrade on first open
- migration is explicit and tool-driven

Suggested migration path:

```aiml
- call: [db2.import_from_db, old_store, new_store]
- call: [db2.export_to_db, new_store, old_store]   # optional tooling
```

If file formats differ, the runtime must reject mismatched opens clearly rather
than attempting heuristic fallback.

### 11.7 Initial scope recommendation

To keep the project tractable, `aiondb2` should start with a narrower target than
full parity with all v1 database forms.

Recommended first scope:

1. `Db2Kv<K:V>` for persistent sorted key/value access
2. range iteration and prefix/range scan support
3. explicit transactions / snapshots
4. blob-friendly persistence for AIML tensors and large values

Later scope:

- `Db2Store<T>` with indexed object storage
- secondary indexes
- remote page/block backends
- replication and distributed operation

### 11.8 Relationship to future work

Several open topics from this chapter may move more naturally into `aiondb2`
than into the original engine:

- B-tree indexes
- remote/distributed stores
- compression tiers
- encrypted pages/blocks
- advanced query planners

This is intentional. `aiondb` should stay lean and stable; `aiondb2` is the place
for more ambitious storage-engine work.

---

## 12. Open Design Questions (Future Work)

- **Indexes**: Automatic B-tree indexes for `where` fields — no compiler integration yet.
- **Joins**: `join` syntax is specified but not yet implemented.
- **Distributed stores**: `db.open_remote("host:port", "store_name")` — extension for Chapter 32 (Network).
- **Encryption**: Transparent AES encryption at the page level.
- **Compression**: LZ4 or Zstandard for rarely used pages (cold storage).
- **Reactive queries**: `db.subscribe(query, callback)` — called when the result set changes.

## Einblick in geöffnete Speicher

Für eine Betriebssicht (cosmos `doc/spec/99` §99.2) gibt `db` Kennzahlen der **geöffneten
persistenten** Speicher heraus — nie Inhalte.

```yaml
- decl: [count, i32, { call: [db.open_count] }]
- decl: [name,  string, { call: [db.open_name, idx] }]
- decl: [path,  string, { call: [db.open_path, idx] }]
- decl: [wal,   i64,    { call: [db.open_wal_used, idx] }]
- decl: [rows,  i64,    { call: [db.open_entries, idx] }]
- decl: [wrts,  i64,    { call: [db.open_writes, idx] }]
```

- Gezeigt werden nur **geöffnete** Speicher: ein Speicher, den niemand geöffnet hat, ist kein
  Betriebszustand, sondern eine Datei.
- `open_writes` ist ein **Zähler**, kein Zeitstempel.  Ein Zeitstempel kostete je Schreibvorgang
  einen Uhrzugriff auf dem heißen Pfad; die Sicht vergleicht ohnehin zwei Schnappschüsse, und die
  Differenz sagt „bewegt sich", während der Schnappschuss-Zeitpunkt sagt, wie alt die Aussage ist.
- Ein Index außerhalb des Bereichs liefert Leeres bzw. `0` — kein Absturz, keine Ausnahme
  (`tests/test_kv_introspection.aiml`).
- **Keine Werte.**  Eine Oberfläche, die KV-Inhalte zeigt, zeigt früher oder später ein Geheimnis,
  und die Begründung „nur für Diagnose" hält bis zum ersten Bildschirmfoto in einem Ticket.

