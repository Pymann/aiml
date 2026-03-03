# Kapitel 31 — AionDB: Die eingebaute Datenbank

> **Leitgedanke:** Eine Datenbank ist in AIML kein externes Tool, das man über Treiber anbindet.
> Sie ist ein reservierter Namespace (`db`), der tief in die Sprache integriert ist —
> genau wie `sys`, `io` oder `time`. Der Compiler kennt die Typen, der Speicher-Manager
> ist derselbe wie für normale Variablen, und Schema-Versionen sind dasselbe Konzept
> wie class-Versionen aus Kapitel 30 (Hot-Reload).

---

## 1. Warum eine eingebaute Datenbank?

Externe Datenbanken (SQLite, PostgreSQL, Redis, …) haben einen fundamentalen Nachteil:
Sie sprechen eine andere Sprache als das Programm. Man serialisiert Daten in Text (SQL),
schickt sie über eine Abstraktionsschicht, und deserialisiert das Ergebnis zurück.
Jeder Schritt kostet Zeit und Speicher.

AionDB eliminiert diese Grenze vollständig:

- **Zero-Copy-Reads**: Ein Datenbankzeiger `ptr<Entity>` ist ein echter Maschinenzeiger
  auf den Speicherbereich der Datenbank. Es wird nichts kopiert, nichts deserialisiert.
- **Typsicherheit ohne Mapping**: Weil der Compiler die AIML-Klasse `Entity` kennt,
  muss man kein ORM schreiben. Die Klasse *ist* das Schema.
- **Schema-Evolution gratis**: Wenn eine Klasse von `version: 1` auf `version: 2`
  aktualisiert wird (neues Feld anhängen), erkennt AionDB das automatisch und
  migriert bestehende Seiten — ohne Downtime, ohne Migrationsskript.
- **Versioning eingebaut**: Jede Zeile kann optional einen Zeitstempel tragen,
  der "Time-Travel"-Abfragen ermöglicht: "Wie sah die Datenbank vor 500 Frames aus?"

---

## 2. Der `db`-Namespace

`db` ist ein **reservierter Namespace** in AIML. Man kann keine eigenen Klassen,
Funktionen oder Variablen unter diesem Namen definieren. Er verhält sich analog zu `sys`:

```aiml
# Falsch — Compilerfehler:
- def:
    name: db.connect   # "db" ist reserviert

# Richtig:
- decl: [store, Store<Entity>]
- set:  [store, { call: [db.open, "entities"] }]
```

Alle `db`-Operationen sind **Compile-Time-bekannt**: Der Compiler emittiert direkte
Aufrufe in die AionDB-Runtime, ohne dynamisches Dispatch. Das ist schneller als
jeder Treiber-basierte Ansatz.

---

## 3. Grundkonzepte (ausführlich erklärt)

### 3.1 Was ist ein `Store<T>`?

Ein `Store<T>` ist eine **typisierte Tabelle**, die Zeilen vom Typ `T` speichert.
`T` muss eine AIML-`class` oder ein `model` sein. Der Store verhält sich wie eine
sehr schnelle, persistente Version von `list<T>` — mit dem Unterschied, dass Daten
nicht im Heap verstreut liegen, sondern in **Pages** (zusammenhängende Speicherblöcke)
organisiert sind.

```aiml
- class:
    name: Entity
    members:
      - [id,       i32]
      - [pos_x,    f32]
      - [pos_y,    f32]
      - [hp,       i32]
      - [ai_state, i32]

# Einen Store für Entity-Objekte öffnen (oder anlegen, falls nicht vorhanden):
- decl: [entities, Store<Entity>]
- set:  [entities, { call: [db.open, "entities"] }]

# Einfügen:
- decl: [e, Entity]
- set: [e.id, 1]
- set: [e.pos_x, 100.0]
- set: [e.hp, 100]
- call: [entities.insert, e]

# Lesen per ID (Zero-Copy — gibt einen Zeiger auf den Store-Speicher zurück):
- decl: [p, ptr<Entity>]
- set:  [p, { call: [entities.get, 1] }]
- print: ["Entity hp: ${p.hp}"]
```

Ein `Store<T>` kann in zwei **Layouts** betrieben werden (Kapitel 3.3 erklärt warum):

| Layout | Beschreibung | Gut für |
|--------|-------------|---------|
| `row`  | Alle Felder einer Zeile zusammen im Speicher | Einzelne Objekte lesen/schreiben |
| `column` | Alle Werte eines Feldes zusammen im Speicher | Massenoperationen, SIMD, AI |

```aiml
- decl: [entities, Store<Entity>]
- set:  [entities, { call: [db.open, "entities", {layout: "column"}] }]
```

---

### 3.2 Was ist ECS — Entity Component System?

ECS ist ein **Architekturmuster**, das in Spielen und zunehmend in KI-Systemen
verwendet wird. Es löst ein fundamentales Problem der objektorientierten Programmierung:
**Vererbungs-Explosion**.

#### Das Problem ohne ECS (klassisches OOP)

Stellen wir uns ein Spiel vor mit Spieler, Gegner, Projektile und Partikel:

```
Player    → Position, Velocity, Health, Sprite, InputHandler
Enemy     → Position, Velocity, Health, Sprite, AIController
Bullet    → Position, Velocity, Sprite, Damage
Particle  → Position, Velocity, Sprite, Lifetime
```

Alle vier haben `Position`, `Velocity` und `Sprite`. In OOP würde man eine Basis-Klasse
`MovableSprite` bauen. Aber `Enemy` und `Player` haben `Health`, `Bullet` hat `Damage`…
Sofort entsteht ein komplizierter Vererbungsbaum, der schwer zu erweitern ist.

Außerdem: Wenn der Rendering-Code alle Objekte mit `Sprite` zeichnen will, springt
er durch den Speicher — `Player`-Objekte hier, `Enemy`-Objekte dort, `Bullet`-Objekte
woanders. Das ist **cache-feindlich** und damit langsam.

#### Die ECS-Lösung

ECS trennt **Identität**, **Daten** und **Logik** vollständig:

- **Entity**: Eine eindeutige ID. Eine Entity *ist* nichts — sie *hat* Komponenten.
- **Component**: Reine Datenstruct, keine Methoden. `Position`, `Velocity`, `Health` sind Components.
- **System**: Eine Funktion, die alle Entities mit bestimmten Components abarbeitet.

#### `model` vs `class` für ECS-Komponenten

In AIML verwendet ECS **`model`-Typen** statt `class`. Der Unterschied:

| | `class` | `model` |
|--|---------|---------|
| Methoden | ja | **nein** (pure data) |
| Vererbung | ja | nein |
| ECS / `db.run_system` | nein | **ja** |
| Column-Layout default | nein | ja |

```aiml
# ECS-Komponenten als model (pure data, keine Methoden):
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

#### `primary_key` — natürliche Entity-Identität

Standardmäßig ist die Entity-ID der **Slot-Index** im Store (ein `i32`). Das ist der
schnellste Pfad (O(1) Pointer-Arithmetik). Wenn Entities einen natürlichen Schlüssel
haben (UUID, Username, IP-Adresse…), kann man `primary_key` im Model angeben:

```aiml
# Kein primary_key → Entity-ID = Slot-Index (i32), schnellster Pfad
- model:
    name: Particle
    members: [[x, f32], [y, f32], [vx, f32], [vy, f32]]

# Mit primary_key → Entity-ID = uuid (string), intern HashMap<string, slot>
- model:
    name: Player
    primary_key: uuid
    members: [[uuid, string], [score, i32], [hp, i32]]
```

Der Typ von `eid` in System-Funktionen wird automatisch aus `primary_key` abgeleitet:
- Kein `primary_key` → `eid: i32` (Slot-Index)
- `primary_key: uuid` (string) → `eid: string`

#### `db.run_system` — der ECS-Loop

Der Compiler liest die Signatur der System-Funktion und leitet daraus ab, welche
Stores gejoint werden müssen. Die Schleife wird direkt zu LLVM IR kompiliert —
kein Runtime-Dispatch, kein String-Parsing:

```aiml
- decl: [positions,  Store<Position>]
- decl: [velocities, Store<Velocity>]
- decl: [health,     Store<Health>]
- set:  [positions,  { call: [db.open, "positions"] }]
- set:  [velocities, { call: [db.open, "velocities"] }]
- set:  [health,     { call: [db.open, "health"] }]

# Movement-System: kein eid nötig — reine Datentransformation
- fn:
    name: move_system
    args: [[pos, ptr<Position>], [vel, ptr<Velocity>]]
    body:
      - set: [pos.x, { call: [pos.x + vel.dx] }]
      - set: [pos.y, { call: [pos.y + vel.dy] }]

# Damage-System: eid nötig — löscht Entity aus mehreren Stores
- fn:
    name: damage_system
    args: [[eid, i32], [h, ptr<Health>]]
    body:
      - if:
          condition: { call: [h.hp <= 0] }
          body:
            - call: [health.delete, eid]
            - call: [positions.delete, eid]
            - call: [velocities.delete, eid]

# Compiler sieht ptr<Position> + ptr<Velocity> → iteriert über positions ∩ velocities
- call: [db.run_system, move_system, positions, velocities]

# Compiler sieht nur ptr<Health> → iteriert über health allein
- call: [db.run_system, damage_system, health]
```

`db.run_system` iteriert über alle Entity-IDs, die in **allen** übergebenen Stores
vorhanden sind (Intersection). Für jeden Treffer wird die System-Funktion mit den
entsprechenden Component-Zeigern aufgerufen — zero-copy, keine Allokation.

Eine `Entity` ist jetzt nur noch eine ID. Die Kombination ihrer Components beschreibt,
was sie ist:

| Entity-ID | Position | Velocity | Health | Sprite | Damage |
|-----------|----------|----------|--------|--------|--------|
| 1 (Player)| ✓        | ✓        | ✓      | ✓      |        |
| 2 (Enemy) | ✓        | ✓        | ✓      | ✓      |        |
| 3 (Bullet)| ✓        | ✓        |        | ✓      | ✓      |
| 4 (Particle)| ✓      | ✓        |        | ✓      |        |

#### Warum ist ECS schnell?

Der entscheidende Trick ist der **Column-Store**: Alle `Position`-Komponenten liegen
hintereinander im Speicher. Das Movement-System liest sie sequenziell — keine
Cache-Misses, keine Pointer-Verfolgung. Außerdem kann der Compiler **SIMD-Instruktionen**
verwenden: Mit AVX2 kann man 8 `f32`-Werte gleichzeitig addieren.

```
Speicher ohne ECS (Row-Layout, OOP):
[Player: pos_x, pos_y, hp, texture_id, dx, dy, ...]  ← 64 Bytes
[Enemy:  pos_x, pos_y, hp, texture_id, dx, dy, ...]  ← 64 Bytes, weit entfernt
→ Cache-Miss bei jedem Objekt

Speicher mit ECS (Column-Layout):
[pos_x: 10.0, 25.0, 50.0, 99.0, ...]  ← alle 4 Entities, 16 Bytes hintereinander
[pos_y: 5.0,  12.0, 3.0,  80.0, ...]  ← 16 Bytes hintereinander
→ SIMD kann 4 pos_x-Werte auf einmal verarbeiten (128-bit Register)
```

`model`-Typen aktivieren Column-Layout automatisch. Bei `class`-basierten Stores
muss es explizit via `{layout: "column"}` angegeben werden.

---

### 3.3 Was ist ein `Kv<K,V>`?

Ein **Key-Value-Store** ist die einfachste Datenbankform: Jeder Wert hat einen eindeutigen
Schlüssel, unter dem er gespeichert und abgerufen wird. Man kennt es von `map<K,V>` in AIML —
mit dem Unterschied, dass `Kv<K,V>` **persistent** ist (überlebt Programmneustart) und
für sehr große Datenmengen optimiert ist.

```aiml
# Spieler-Konfigurationen speichern:
- decl: [config_db, Kv<string, PlayerConfig>]
- set:  [config_db, { call: [db.open_kv, "player_configs"] }]

- decl: [cfg, PlayerConfig]
- set: [cfg.sensitivity, 1.5]
- call: [config_db.set, "player_1", cfg]

- decl: [loaded, PlayerConfig]
- set:  [loaded, { call: [config_db.get, "player_1"] }]
```

Intern verwendet `Kv<K,V>` eine **Hash-Tabelle** mit offener Adressierung —
O(1) für `get` und `set`.

---

### 3.4 Was ist `TimeSeries<T>` und Time-Travel?

Ein `TimeSeries<T>` ist ein **append-only Log**: Daten werden nie überschrieben,
sondern immer mit einem Zeitstempel angehängt. Das ermöglicht **Time-Travel-Abfragen**:
"Wie sah der Zustand zu Zeitpunkt X aus?"

Das ist besonders wertvoll für:
- **Spiele**: Replay-System, Debug ("Warum ist Entity 5 bei Frame 1337 gestorben?")
- **AI-Training**: Man kann vergangene Zustände als Trainingsdaten verwenden
- **Audit Logs**: Jede Änderung ist nachvollziehbar

```aiml
- class:
    name: GameState
    members:
      - [score, i32]
      - [player_hp, i32]
      - [boss_hp, i32]

- decl: [history, TimeSeries<GameState>]
- set:  [history, { call: [db.open_timeseries, "game_history"] }]

# Jeden Frame speichern:
- decl: [state, GameState]
- set: [state.score, 1500]
- set: [state.player_hp, 75]
- call: [history.append, state, { call: [time.now] }]

# Time-Travel: Zustand bei Tick 1000 abfragen:
- decl: [past, GameState]
- set:  [past, { call: [history.at, 1000] }]
- print: ["Score at tick 1000: ${past.score}"]

# Bereich abfragen (für ML-Training):
- decl: [replay, list<GameState>]
- set:  [replay, { call: [history.range, 0, 5000] }]
```

---

### 3.5 Was ist `VecStore` — Vektorsuche für KI?

Ein **VecStore** ist eine spezialisierte Datenbank für **Vektoren** (Arrays von `f32`-Zahlen).
Er beantwortet die Frage: "Welche gespeicherten Vektoren sind diesem Abfragevektor am ähnlichsten?"

#### Wozu braucht man das?

Moderne KI-Modelle (Sprachmodelle, Bildmodelle, Empfehlungssysteme) konvertieren Objekte
in hochdimensionale Zahlenvektoren, sogenannte **Embeddings**. Ähnliche Objekte haben
ähnliche Vektoren. Ein Embedding-Vektor für "Katze" liegt im Vektorraum nah bei "Hund",
weit weg von "Raumschiff".

Konkrete Anwendungsfälle:

- **RAG (Retrieval-Augmented Generation)**: Man speichert Dokumenten-Embeddings.
  Wenn der Nutzer eine Frage stellt, sucht man die k ähnlichsten Dokumente und
  gibt sie als Kontext an ein Sprachmodell.
- **Spiel-KI**: Gegner-Verhalten wird als Vektor gespeichert. Man findet
  schnell ähnliche Situationen aus vergangenen Partien.
- **Empfehlungssystem**: Item-Embeddings speichern, ähnliche Items vorschlagen.

```aiml
# 128-dimensionale Embeddings speichern:
- decl: [knowledge, VecStore]
- set:  [knowledge, { call: [db.open_vec, "knowledge_base", {dims: 128}] }]

# Ein Embedding (z.B. von einem ML-Modell) einfügen:
- decl: [embed, list<f32>]   # 128 Floats
# ... Embedding befüllen ...
- call: [knowledge.insert, "doc_42", embed]

# k-Nearest-Neighbor-Suche (k=5 ähnlichste):
- decl: [query_vec, list<f32>]
# ... Query-Embedding befüllen ...
- decl: [results, list<VecResult>]
- set:  [results, { call: [knowledge.search, query_vec, 5] }]

# VecResult enthält: id (string), score (f32, Ähnlichkeit 0.0–1.0)
- decl: [r, VecResult]
- set:  [r, { call: [results.get, 0] }]
- print: ["Ähnlichstes Dokument: ${r.id}, Score: ${r.score}"]
```

Intern verwendet `VecStore` **HNSW** (Hierarchical Navigable Small World) —
ein Graph-basierter Algorithmus für approximate nearest-neighbor-Suche.
Er ist schneller als brute-force (O(log N) statt O(N)) und skaliert auf Millionen Vektoren.

---

## 4. Abfragen

AionDB vermeidet Runtime-Parsing vollständig. Es gibt zwei Abfragemodi:

- **`db.query`** — für einzelne Stores mit optionalem Filter und Sortierung
- **`db.run_system`** — für ECS-Queries über mehrere Stores (siehe §3.2)

### 4.1 `db.query` — Einzel-Store-Abfragen

Das `{where: ...}`-Argument nimmt eine **Funktion** entgegen, keinen String.
Die Funktion wird zu einem Funktionszeiger compiliert und per Element aufgerufen —
kein Parser, direktes IR:

```aiml
# Predicate-Funktion — vom Compiler zu LLVM IR compiliert:
- def:
    name: is_alive
    args: [[e, ptr<Entity>]]
    return: i32
    body:
      - return: { >: [e.hp, 0] }

- def:
    name: is_active_enemy
    args: [[e, ptr<Entity>]]
    return: i32
    body:
      - decl: [alive, i32]
      - set: [alive, { >: [e.hp, 0] }]
      - decl: [enemy, i32]
      - set: [enemy, { ==: [e.ai_state, 2] }]
      - return: { and: [alive, enemy] }

# Alle lebenden Entities, sortiert nach pos_x, maximal 100:
- decl: [alive, list<ptr<Entity>>]
- set:  [alive, { call: [db.query, entities,
    {where: is_alive},
    {order_by: "pos_x"},
    {limit: 100}
  ]}]
```

### 4.2 Verfügbare Operatoren

| Operator | Beschreibung | Beispiel |
|----------|-------------|---------|
| `where`  | Filterfunktion `(ptr<T>) → i32` | `{where: is_alive}` |
| `order_by` | Feldname (Compile-Zeit validiert) | `{order_by: "pos_x", dir: "asc"}` |
| `limit` | Maximale Ergebnisse | `{limit: 100}` |
| `offset` | Ergebnisse überspringen | `{offset: 50}` |
| `at`     | Time-Travel-Snapshot (mit TimeSeries) | `{at: 1000}` |

**Hinweis:** `order_by` und `select` verwenden Feldnamen als Strings — diese werden
jedoch **zur Compile-Zeit** gegen das Schema von `T` validiert. Ein unbekanntes Feld
ist ein Compilerfehler, kein Runtime-Fehler.

### 4.3 ECS-Query: `db.run_system`

Für Queries über mehrere Stores (ECS-Archetype-Intersection) verwendet man
`db.run_system`. Siehe §3.2 für die vollständige Erklärung mit Beispielen.

```aiml
# Compiler sieht ptr<Position> + ptr<Velocity> → iteriert über Intersection
- call: [db.run_system, move_system, positions, velocities]
```

---

## 5. Transaktionen

AionDB bietet zwei Transaktionsmodi:

### 5.1 Standard-Transaktion (ACID)

```aiml
- call: [db.begin]

- call: [entities.insert, new_entity]
- call: [inventory.delete, old_item_id]

# Alle Änderungen atomar committen:
- call: [db.commit]

# Oder alles rückgängig machen:
# - call: [db.rollback]
```

Bis `db.commit` aufgerufen wird, sind Änderungen **nur für die aktuelle Session sichtbar**.
Andere Threads/Agenten sehen die alten Daten.

### 5.2 Frame-Transaktion (für Spiele)

Im Spiel-Modus fasst `db.begin_frame` alle Änderungen eines Frames unter einem
einzigen Zeitstempel zusammen. Das ist effizienter als viele einzelne Commits:

```aiml
- call: [db.begin_frame, { call: [time.now] }]   # Startet Frame N

# Alle game-loop-Operationen dieses Frames:
- call: [entities.insert, spawned_enemy]
- call: [entities.update, 5, {hp: 80}]
- call: [history.append, current_state]

- call: [db.end_frame]   # Alle Änderungen bekommen denselben Timestamp
```

---

## 6. Persistenz — WAL und Memory-Mapped Files

AionDB schreibt Daten in zwei Schichten:

### 6.1 WAL (Write-Ahead Log)

Bevor eine Änderung auf die eigentliche Page angewendet wird, schreibt AionDB
sie in ein **sequenzielles Log** (WAL). Das ist schnell (sequenzielles Schreiben
ist auf SSDs sehr effizient) und crash-sicher: Wenn das Programm abstürzt, kann
AionDB beim nächsten Start das WAL durchlesen und den Zustand wiederherstellen.

### 6.2 Pages (mmap)

Die eigentlichen Daten liegen in **Pages** (standardmäßig 4 KB) auf der Festplatte,
die via **mmap** in den Prozessspeicher eingeblendet werden. Das Betriebssystem
verwaltet, welche Pages im RAM sind und welche auf die Platte ausgelagert werden —
automatisch, ohne dass AionDB selbst Paging implementieren muss.

Ein `ptr<Entity>` aus einem `Store<Entity>` zeigt direkt auf diesen gemappten Speicher.
Wenn man über den Zeiger liest, liest man *direkt aus der Datenbank* ohne Kopie.

### 6.3 Checkpoint

Periodisch verdichtet AionDB das WAL in die Pages (Checkpoint). Das hält das WAL
klein und macht zukünftige Wiederherstellungen schneller:

```aiml
- call: [db.checkpoint]        # Manuell
- call: [db.auto_checkpoint, 1000]  # Automatisch alle 1000 Writes
```

---

## 7. Schema Evolution — Integration mit Hot-Reload

Das mächtigste Feature von AionDB ist die nahtlose Integration mit dem
**Klassen-Versionierungssystem** aus Kapitel 30.

Wenn eine Klasse von `version: 1` auf `version: 2` aktualisiert wird
(neues Feld hinzugefügt — immer **nur anhängen**, nie Felder entfernen oder umordnen),
erkennt AionDB das automatisch beim nächsten Öffnen des Stores:

```aiml
# Programm v1 — Entity hat 3 Felder:
- class:
    name: Entity
    version: 1
    members:
      - [id,    i32]
      - [pos_x, f32]
      - [hp,    i32]

# Programm v2 — Entity bekommt ein neues Feld (nur anhängen!):
- class:
    name: Entity
    version: 2
    members:
      - [id,       i32]
      - [pos_x,    f32]
      - [hp,       i32]
      - [mana,     i32]   # NEU — wird an bestehende Zeilen mit Defaultwert 0 gepatcht
```

AionDB vergleicht die Schema-Registry (Kapitel 30.1) mit dem gespeicherten Schema der Page.
Erkennt es eine höhere Version mit neuen Feldern, wendet es die **In-Place-Migration** an:

1. Alle bestehenden Zeilen haben durch das `padded_size`-System bereits Platz für neue Felder reserviert.
2. AionDB schreibt den Defaultwert des neuen Feldes in den Padding-Bereich.
3. Ab sofort liest `p.mana` den korrekten Wert — keine Migrationsskripte, kein Downtime.

---

## 8. Vollständige Verwendungsbeispiele

### 8.1 Game Loop mit ECS

```aiml
# --- ECS-Komponenten als model (pure data, Column-Layout automatisch) ---
- model:
    name: Position
    members: [[x, f32], [y, f32]]

- model:
    name: Velocity
    members: [[dx, f32], [dy, f32]]

- model:
    name: Health
    members: [[hp, i32]]

# --- Stores öffnen ---
- decl: [positions,  Store<Position>]
- decl: [velocities, Store<Velocity>]
- decl: [health,     Store<Health>]
- set:  [positions,  { call: [db.open, "positions"] }]
- set:  [velocities, { call: [db.open, "velocities"] }]
- set:  [health,     { call: [db.open, "health"] }]

- decl: [history, TimeSeries<Health>]
- set:  [history, { call: [db.open_timeseries, "health_history"] }]

# --- Systeme definieren ---

# Movement-System: kein eid nötig — reine Datentransformation
# Column-Layout → SIMD verarbeitet 8 pos_x-Werte gleichzeitig
- def:
    name: move_system
    args: [[pos, ptr<Position>], [vel, ptr<Velocity>]]
    body:
      - set: [pos.x, { +: [pos.x, vel.dx] }]
      - set: [pos.y, { +: [pos.y, vel.dy] }]

# Damage-System: eid nötig — löscht Entity aus mehreren Stores
- def:
    name: damage_system
    args: [[eid, i32], [h, ptr<Health>]]
    body:
      - if: [{ <=: [h.hp, 0] }, true]
        then:
          - call: [health.delete, eid]
          - call: [positions.delete, eid]
          - call: [velocities.delete, eid]

# --- Initialisierung: 1000 Enemies spawnen ---
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

# --- Game Loop ---
- def:
    name: game_loop
    return: void
    body:
      - call: [db.begin_frame, { call: [time.now] }]

      # Compiler generiert Intersection-Loop: positions ∩ velocities
      - call: [db.run_system, move_system, positions, velocities]

      # Compiler generiert Loop über health allein
      - call: [db.run_system, damage_system, health]

      - call: [db.end_frame]
```

### 8.2 KI-Anwendung: Feature Store für ML-Training

```aiml
# Trainingsdaten für einen Reinforcement-Learning-Agenten sammeln:

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

# Mini-Batch für Training (zufällige 64 Samples):
- def:
    name: sample_batch
    return: list<ptr<Observation>>
    body:
      - return: { call: [replay_buffer.sample_random, 64] }
```

### 8.3 KI-Anwendung: RAG mit Vektorsuche

```aiml
# Retrieval-Augmented Generation: Dokumente speichern und ähnliche finden

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

# Dokument einfügen + Embedding speichern:
- def:
    name: index_document
    args: [[doc, Document], [embedding, list<f32>]]
    return: void
    body:
      - call: [docs.insert, doc]
      - call: [doc_vecs.insert, doc.doc_id, embedding]

# Ähnliche Dokumente zu einer Anfrage finden (k=3):
- def:
    name: retrieve
    args: [[query_embedding, list<f32>]]
    return: list<Document>
    body:
      - decl: [matches, list<VecResult>]
      - set:  [matches, { call: [doc_vecs.search, query_embedding, 3] }]

      - decl: [result, list<Document>]
      - decl: [m, VecResult]
      - for: [m, matches]
        body:
          - decl: [d, ptr<Document>]
          - set:  [d, { call: [docs.get, m.id] }]
          - call: [result.push, d]
      - return: result
```

---

## 9. Performance-Eigenschaften

| Operation | Komplexität | Notiz |
|-----------|------------|-------|
| `store.get(id)` | O(1) | Slot-Index (kein primary_key) oder HashMap-Lookup |
| `store.insert(v)` | O(1) amortisiert | Append auf aktive Page |
| `db.query(store, ...)` | O(N) | Kann mit Index O(log N) werden |
| `db.run_system(fn, s1, s2)` | O(min(|s1|,|s2|)) | Intersection via Slot-Bitmap |
| `kv.get(key)` | O(1) | Hash-Tabelle |
| `kv.set(key, val)` | O(1) amortisiert | — |
| `timeseries.at(t)` | O(log N) | Binärsuche auf Timestamps |
| `timeseries.range(a,b)` | O(K) | K = Anzahl Ergebnisse |
| `vecstore.search(v, k)` | O(log N) | HNSW-Index |
| Column-Read (SIMD) | O(N/8) | 8× schneller als skalares Loop |

---

## 10. Offene Designfragen (Future Work)

- **Indizes**: Automatische B-Tree-Indizes für `where`-Felder — bisher noch keine Compiler-Integration.
- **Joins**: `join`-Syntax ist spezifiziert, aber noch nicht implementiert.
- **Verteilte Stores**: `db.open_remote("host:port", "store_name")` — Erweiterung für Kapitel 32 (Network).
- **Encryption**: Transparente AES-Verschlüsselung auf Page-Ebene.
- **Compression**: LZ4 oder Zstandard für wenig genutzte Pages (Cold Storage).
- **Reactive Queries**: `db.subscribe(query, callback)` — wird aufgerufen, wenn sich Ergebnismenge ändert.
