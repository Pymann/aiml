# Kapitel 47 — Thread-Safety-Modell

> **Leitgedanke:** AIML hat native OS-Threads (Kapitel 26), aber kein Ownership-/Borrow-System.
> Stattdessen definiert die Sprache **drei Sicherheitskategorien** für alle Runtime-Primitives.
> Der AIML-Autor muss nur für Kategorie 3 (shared user state) selbst synchronisieren.

---

## 1. Die drei Kategorien

### Kategorie 1 — Implizit thread-safe (Runtime garantiert Locking)

Runtime-Primitives, die **globalen oder geteilten Zustand** verwalten, sind intern
durch Mutexe geschützt. Der AIML-Code sieht davon nichts — analog zu `sqlite3`,
`sync.Map` (Go), oder `ConcurrentHashMap` (Java).

| Namespace / Typ | Operationen | Garantie |
|-----------------|-------------|----------|
| `db` (Store, Kv, VecStore, TimeSeries) | open, get, set, has, delete, clear, checkpoint, query, run_system | Alle Operationen atomar pro Handle. Singleton-Pattern: gleicher Name+Pfad → gleicher Handle |
| `log` | set_level, debug, info, warn, error | Ausgaben sind serialisiert, keine verschränkten Zeilen |
| `path` (Intern-Tabelle) | from_string, Vergleich (`==`) | Die globale Intern-Map ist mutex-geschützt |

**Begründung:** Diese Primitives sind Infrastruktur, kein User-Code. Kein Python-Entwickler
schreibt einen Mutex um `logging.info()`, kein Go-Entwickler lockt `sql.DB.Query()`.

**Konsequenz für den AIML-Autor:**
```yaml
# Das ist sicher — auch aus zwei spawn-Threads gleichzeitig:
- call: [cards_kv.set, id_a, card_a]   # Thread 1
- call: [cards_kv.set, id_b, card_b]   # Thread 2

# db.open_kv gibt bei gleichem Namen denselben Handle zurück (Singleton):
- set: [kv1, { call: [db.open_kv, "store_x", {persist: true}] }]  # Thread 1
- set: [kv2, { call: [db.open_kv, "store_x", {persist: true}] }]  # Thread 2
# kv1 und kv2 sind derselbe Handle → kein doppelter WAL, kein Race
```

**Einschränkung:** Einzelne Operationen sind atomar, aber **Sequenzen** sind es nicht:
```yaml
# NICHT atomar — zwischen has und set kann ein anderer Thread den Key löschen:
- set: [exists, { call: [kv.has, key] }]
- if:
    condition: { "==": [exists, 0] }
    then:
      - call: [kv.set, key, val]

# Für atomare Read-Modify-Write: expliziter Lock (Kapitel 26) nötig
```

---

### Kategorie 2 — Stateless / Pure (inhärent sicher)

Funktionen ohne internen Zustand. Mehrere Threads können sie gleichzeitig aufrufen,
ohne dass irgendetwas koordiniert werden muss.

| Namespace | Operationen |
|-----------|-------------|
| `math` | sin, cos, sqrt, random, vec2/vec3/vec4/mat4-Operationen, … |
| `hash` | sha256, hmac_sha256, md5, … |
| `compress` | gzip, deflate, brotli, zstd, lz4, zip_entries, tar_entries, … |
| `time` | now, sleep, format, … |
| `sys` | args, exit, env, getenv, … |
| `io` (reine Funktionen) | read_file, write_file, list_dir, exists, … |

**Hinweis:** `io.write_file` ist auf OS-Ebene nicht atomar — wenn zwei Threads dieselbe
Datei schreiben, ist das Ergebnis undefiniert. Das ist kein AIML-Problem, sondern ein
POSIX-Problem. Für koordiniertes Schreiben: expliziter Lock.

---

### Kategorie 3 — Per-Handle / User-managed (Nutzer synchronisiert selbst)

Handles, die **einer Connection oder Ressource** gehören. Sie werden typischerweise
nicht zwischen Threads geteilt. Wenn doch, muss der Nutzer einen `lock` verwenden.

| Typ | Beispiel | Typischer Gebrauch |
|-----|----------|-------------------|
| `socket_handle` | net.connect → send/recv | Ein Thread pro Connection |
| `ws_handle` | ws.connect → send/recv | Ein Thread pro WebSocket |
| `tls_socket_handle` | tls.connect → send/recv | Ein Thread pro TLS-Verbindung |
| `http_request_handle` | Innerhalb eines Route-Handlers | Wird vom HTTP-Server pro Request erzeugt, nicht geteilt |
| `http_response_handle` | Innerhalb eines Route-Handlers | Ditto |
| `thread_handle` | spawn → join/detach | Controller-Thread hält den Handle |
| `mirror_session` | mirror.listen / connect | Per-Session, nicht thread-safe |

**Begründung:** Connection-Handles sind per Definition kurzlebig und gehören einem Thread.
Implizites Locking wäre hier nur Overhead ohne Nutzen.

**Wenn man doch teilen muss:**
```yaml
- decl: [shared_ws, ws_handle]
- set:  [shared_ws, { call: [ws.connect, "ws://..."] }]
- decl: [ws_lock, lock]
- call: [ws_lock.init]

- def:
    name: writer_thread
    args: [[msg, string]]
    body:
      - call: [ws_lock.acquire]
      - call: [shared_ws.send, msg]
      - call: [ws_lock.release]
```

---

### Sonderfall: `http_server_handle`

Der HTTP-Server ist ein Hybrid:

- **srv.route, srv.start, srv.stop** → Kategorie 1 (intern synchronisiert)
- **req/resp innerhalb eines Handlers** → Kategorie 3 (pro Request-Thread, nicht geteilt)
- **Globaler State aus dem Handler** (z.B. db.open_kv) → Kategorie 1

Der HTTP-Server erzeugt pro eingehender Connection einen eigenen Thread (detached pthread).
Route-Handler werden in diesem Thread ausgeführt. Der Handler darf:
- `db.*`-Operationen aufrufen (Kategorie 1, sicher)
- `log.*`-Operationen aufrufen (Kategorie 1, sicher)
- `req`/`resp` lesen/schreiben (Kategorie 3, aber pro-Request, also sicher)
- **Nicht:** globale AIML-Variablen ohne Lock modifizieren (Kategorie 3)

---

## 2. Shared User State — die einzige Gefahr

Alles in Kategorien 1 und 2 ist sicher. Die **einzige Quelle für Data Races** in AIML-Code
ist geteilter veränderbarer Zustand:

```yaml
# GEFÄHRLICH — globale Variable ohne Lock:
- decl: [counter, i32]
- set:  [counter, 0]

- def:
    name: increment
    args: []
    body:
      - set: [counter, { +: [counter, 1] }]   # Race Condition!

- set: [t1, { spawn: [increment] }]
- set: [t2, { spawn: [increment] }]
- call: [t1.join]
- call: [t2.join]
# counter kann 1 oder 2 sein — undefiniert
```

**Lösung:**
```yaml
- decl: [counter, i32, 0]
- decl: [mtx, lock]
- call: [mtx.init]

- def:
    name: increment
    args: []
    body:
      - call: [mtx.acquire]
      - set: [counter, { +: [counter, 1] }]
      - call: [mtx.release]
```

**Oder besser — Channels (Kapitel 27):**
```yaml
- decl: [ch, chan<i32>]
# Jeder Thread sendet seine Deltas; ein Consumer aggregiert
```

---

## 3. Implementierungsregeln für Runtime-Entwickler

Diese Regeln gelten für alle C/C++-Dateien unter `src/sys/` und `src/core/`:

1. **Globale Registries** (z.B. `g_persistent_kvs`, `g_persistent_stores`) müssen
   durch einen globalen Mutex geschützt sein. `push_back` auf `std::vector` ist
   **nicht thread-safe**.

2. **Singleton-Pattern** für benannte Ressourcen: `db.open_kv("name", {persist: true})`
   muss bei gleichem Name+Pfad denselben Handle zurückgeben.
   - Verhindert WAL-Korruption (mehrere fd's auf dieselbe Datei)
   - Verhindert Memory-Leaks (unbegrenztes Wachstum der Registry)
   - Lookup + Insert muss unter demselben Lock passieren (TOCTOU)

3. **Per-Handle-Mutex** für Kategorie-1-Handles: Jeder `AimlKv`, `AimlStore`,
   `AimlVecStore`, `AimlTimeSeries` hat einen eigenen `std::mutex`.
   - Schützt interne Maps, WAL-Writes, Decoded-Cache
   - Globaler Mutex reicht nicht — er würde alle Stores serialisieren

4. **Keine Locks in Kategorie 2/3**: Stateless-Funktionen und per-Connection-Handles
   bekommen kein Locking. Das wäre reiner Overhead.

5. **WAL-Writes sind serialisiert** durch den Per-Handle-Mutex. Kein zusätzlicher
   File-Lock nötig, solange Regel 2 (Singleton) eingehalten wird.

---

## 4. Zusammenfassung

| Was | Wer synchronisiert | Wie |
|-----|-------------------|-----|
| db.*, log.*, path-Intern | Runtime | Mutex (implizit) |
| math.*, hash.*, compress.*, time.* | Niemand (stateless) | — |
| socket/ws/tls/http-Handles | AIML-Autor (falls geteilt) | `lock` (Kapitel 26) |
| Globale AIML-Variablen | AIML-Autor | `lock` oder `chan<T>` |

**Faustregel:** Wenn es ein `db.`- oder `log.`-Aufruf ist, ist es sicher.
Wenn du eine eigene Variable aus zwei Threads schreibst, brauchst du einen Lock.
