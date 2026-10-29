# Chapter 47 — Thread Safety Model

> **Guiding principle:** AIML has native OS threads (Chapter 26), but no ownership/borrow system.
> Instead, the language defines **three safety categories** for all runtime primitives.
> The AIML author only needs to synchronize manually for Category 3 (shared user state).

---

## 1. The Three Categories

### Category 1 — Implicitly thread-safe (runtime guarantees locking)

Runtime primitives that manage **global or shared state** are internally protected
by mutexes. AIML code sees nothing of this — analogous to `sqlite3`,
`sync.Map` (Go), or `ConcurrentHashMap` (Java).

| Namespace / Type | Operations | Guarantee |
|------------------|------------|-----------|
| `db` (Store, Kv, VecStore, TimeSeries) | open, get, set, has, delete, clear, checkpoint, query, run_system | All operations atomic per handle. Singleton pattern: same name+path → same handle |
| `log` | set_level, debug, info, warn, error | Outputs are serialized, no interleaved lines |
| `path` (intern table) | from_string, comparison (`==`) | The global intern map is mutex-protected |

**Rationale:** These primitives are infrastructure, not user code. No Python developer
writes a mutex around `logging.info()`, no Go developer locks `sql.DB.Query()`.

**Consequence for the AIML author:**
```yaml
# This is safe — even from two spawn-threads simultaneously:
- call: [cards_kv.set, id_a, card_a]   # Thread 1
- call: [cards_kv.set, id_b, card_b]   # Thread 2

# db.open_kv returns the same handle for the same name (singleton):
- set: [kv1, { call: [db.open_kv, "store_x", {persist: true}] }]  # Thread 1
- set: [kv2, { call: [db.open_kv, "store_x", {persist: true}] }]  # Thread 2
# kv1 and kv2 are the same handle → no duplicate WAL, no race
```

**Limitation:** Individual operations are atomic, but **sequences** are not:
```yaml
# NOT atomic — between has and set, another thread could delete the key:
- set: [exists, { call: [kv.has, key] }]
- if:
    condition: { eq: [exists, 0] }
    then:
      - call: [kv.set, key, val]

# For atomic read-modify-write: explicit lock (Chapter 26) required
```

---

### Category 2 — Stateless / Pure (inherently safe)

Functions without internal state. Multiple threads can call them simultaneously
without any coordination needed.

| Namespace | Operations |
|-----------|------------|
| `math` | sin, cos, sqrt, random, vec2/vec3/vec4/mat4 operations, … |
| `hash` | sha256, hmac_sha256, md5, … |
| `compress` | gzip, deflate, brotli, zstd, lz4, zip_entries, tar_entries, … |
| `time` | now, sleep, format, … |
| `sys` | args, exit, env, getenv, … |
| `io` (pure functions) | read_file, write_file, list_dir, exists, … |

**Note:** `io.write_file` is not atomic at the OS level — if two threads write to the
same file, the result is undefined. This is not an AIML problem, but a POSIX problem.
For coordinated writing: explicit lock.

---

### Category 3 — Per-Handle / User-managed (user synchronizes manually)

Handles that **belong to a connection or resource**. They are typically not shared
between threads. If they are, the user must use a `lock`.

| Type | Example | Typical Usage |
|------|---------|---------------|
| `socket_handle` | net.connect → send/recv | One thread per connection |
| `ws_handle` | ws.connect → send/recv | One thread per WebSocket |
| `tls_socket_handle` | tls.connect → send/recv | One thread per TLS connection |
| `http_request_handle` | Inside a route handler | Created by the HTTP server per request, not shared |
| `http_response_handle` | Inside a route handler | Same |
| `thread_handle` | spawn → join/detach | Controller thread holds the handle |
| `mirror_session` | mirror.listen / connect | Per session, not thread-safe |

**Rationale:** Connection handles are by definition short-lived and belong to one thread.
Implicit locking would only be overhead with no benefit here.

**If you do need to share:**
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

### Special Case: `http_server_handle`

The HTTP server is a hybrid:

- **srv.route, srv.start, srv.stop** → Category 1 (internally synchronized)
- **req/resp inside a handler** → Category 3 (per request thread, not shared)
- **Global state from within the handler** (e.g. db.open_kv) → Category 1

The HTTP server creates its own thread per incoming connection (detached pthread).
Route handlers are executed in this thread. The handler may:
- Call `db.*` operations (Category 1, safe)
- Call `log.*` operations (Category 1, safe)
- Read/write `req`/`resp` (Category 3, but per-request, so safe)
- **Not:** modify global AIML variables without a lock (Category 3)

---

## 2. Shared User State — The Only Danger

Everything in Categories 1 and 2 is safe. The **only source of data races** in AIML code
is shared mutable state:

```yaml
# DANGEROUS — global variable without lock:
- decl: [counter, i32]
- set:  [counter, 0]

- def:
    name: increment
    args: []
    body:
      - set: [counter, { +: [counter, 1] }]   # Race condition!

- set: [t1, { spawn: [increment] }]
- set: [t2, { spawn: [increment] }]
- call: [t1.join]
- call: [t2.join]
# counter can be 1 or 2 — undefined
```

**Solution:**
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

**Or better — Channels (Chapter 27):**
```yaml
- decl: [ch, chan<i32>]
# Each thread sends its deltas; one consumer aggregates
```

---

## 3. Implementation Rules for Runtime Developers

These rules apply to all C/C++ files under `src/sys/` and `src/core/`:

1. **Global registries** (e.g. `g_persistent_kvs`, `g_persistent_stores`) must
   be protected by a global mutex. `push_back` on `std::vector` is
   **not thread-safe**.

2. **Singleton pattern** for named resources: `db.open_kv("name", {persist: true})`
   must return the same handle for the same name+path.
   - Prevents WAL corruption (multiple fds on the same file)
   - Prevents memory leaks (unbounded growth of the registry)
   - Lookup + insert must happen under the same lock (TOCTOU)

3. **Per-handle mutex** for Category 1 handles: every `AimlKv`, `AimlStore`,
   `AimlVecStore`, `AimlTimeSeries` has its own `std::mutex`.
   - Protects internal maps, WAL writes, decoded cache
   - A global mutex is not sufficient — it would serialize all stores

4. **No locks in Category 2/3**: Stateless functions and per-connection handles
   get no locking. That would be pure overhead.

5. **WAL writes are serialized** by the per-handle mutex. No additional file lock
   needed, as long as Rule 2 (singleton) is followed.

---

## 4. Summary

| What | Who synchronizes | How |
|------|-----------------|-----|
| db.*, log.*, path intern | Runtime | Mutex (implicit) |
| math.*, hash.*, compress.*, time.* | Nobody (stateless) | — |
| socket/ws/tls/http handles | AIML author (if shared) | `lock` (Chapter 26) |
| Global AIML variables | AIML author | `lock` or `chan<T>` |

**Rule of thumb:** If it's a `db.` or `log.` call, it's safe.
If you write to your own variable from two threads, you need a lock.
