# Chapter 44.5 — Mirror (Network Synchronization)

> **Core idea:** Mirror synchronizes AIML model instances and their fields
> automatically over the network. The transport is selected via URL scheme —
> `tcp://` for Desktop/Android (raw sockets via the `net` namespace),
> `ws://` / `wss://` for all platforms including browsers (WebSocket via the `ws` namespace).
> Game code sees neither transport nor serialization — only `session.update()` per frame
> and normal field accesses on synchronized objects.

---

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│  Game code          player.health -= 10                      │
│                     session.update()                         │
├──────────────────────────────────────────────────────────────┤
│  Mirror layer       Dirty tracking → delta packets           │
│                     RPC stubs → serialized calls             │
│                     Deserialize → apply fields to replica    │
├──────────────────────────────────────────────────────────────┤
│  Transport abstraction   tcp:// | ws:// | wss://             │
├───────────────────────────────┬──────────────────────────────┤
│  net namespace (TCP)          │  ws / wss namespace          │
│  Desktop + Android            │  Desktop + Android + Browser │
└───────────────────────────────┴──────────────────────────────┘
```

---

## 1. Types

| Type | LLVM | Description |
|-----|------|--------------|
| `mirror_session` | `i8*` | Active network session (host or client) |
| `mirror_peer` | `i8*` | Connected peer (remote party) |

---

## 2. Model Declaration with Sync

The fields to be replicated over the network are marked in the `model`
declaration with `sync:`:

```aiml
- model:
    name: Player
    sync: [pos, health, score]   # these fields are automatically replicated
    fields:
      - [pos,    vec3]
      - [health, i32]
      - [score,  i32]
      - [name,   string]         # not in sync: → local only
      - [inv,    list<string>]   # not in sync: → local only
```

- **`sync:`** — list of field names; the compiler automatically generates
  serialization/deserialization for these fields
- Fields not listed in `sync:` are never transmitted
- **`rpc:` does not belong here.**  A model has no methods, so `rpc:` on a model
  is a compile error ("models have no methods; declare rpc: on a class") — the
  methods callable over the network are listed on a **class**, see §7.1.  A class
  can carry the replicated state through `models: [Player]`
  (`tests/test_iface_list_return_after_mirror.aiml` shows that shape).

The compiler generates per `sync:` model:
- `__aiml_mirror_serialize_<Model>(obj*, buf*)` — only changed fields (delta)
- `__aiml_mirror_deserialize_<Model>(obj*, buf*)` — apply received fields

---

## 2.1 Heap Fields in sync: Models

Fields of types `string`, `list<T>`, `map<K:V>` and `Kv<string:T>` may appear in
`sync:`. The compiler automatically detects that heap serialization
(Chapter 45) is needed and emits `@__aiml_sernodes_<Model>`.

```aiml
- model:
    name: GameView
    sync: [hand_count, cards]
    fields:
      - [hand_count, i32]
      - [cards,      Kv<string:CardData>]   # ← heap field in sync:
      - [phase,      i32]
```

### Initialization of Kv Fields

A `Kv<string:T>` field is **not** automatically allocated.
The model field initially contains a null pointer.
The owner must explicitly initialize the field once — typically
in setup code, *before* the object is registered:

```aiml
- decl: [g_view, GameView]
- set:  [g_view.cards, { call: [db.open_kv, "cards"] }]   # allocates Kv
- decl: [nid, i32, { call: [sess.register, g_view] }]
```

After `session.register`, Mirror sends the initial packet with the fully
serialized Kv contents to all peers.

A `map<K:V>` field behaves analogously — it must be initialized with `{ call: [map.new] }`
(if `map.new` is available), or likewise via `db.open_kv` if persistent storage is desired.

### Dirty Detection for Kv / map (v1 limitation)

The automatic dirty detection in `session.update()` is based on `memcmp` of
the entire struct memory. For primitive and string fields this works
(pointer addresses change with every reassignment). For `Kv<string:T>` and
`map<K:V>` the pointer **never** changes — only the internal contents. `memcmp`
therefore sees no change.

**Consequence:** After every mutating Kv access (`kv.set` / `kv.delete`) the
owner must explicitly synchronize:

```aiml
- call: [g_view.cards.set, cid, card_data]   # mutates Kv — pointer unchanged
- call: [sess.sync_now, g_view]              # forces immediate delta
```

Alternatively, set the object to manual sync mode and call after every change:

```aiml
# Once after register:
- call: [sess.set_sync_mode, g_view, "manual"]

# After every Kv mutation:
- call: [g_view.cards.set, cid, card_data]
- call: [sess.sync_now, g_view]
```

> **v2:** Dirty detection will be switched to blob comparison — then
> `session.update()` detects Kv changes automatically without explicit `sync_now`.

---

## 3. Session Creation

### 3.1 Transport URL

| Scheme | Transport | Platform |
|--------|-----------|-----------|
| `tcp://host:port` | Raw TCP socket (`net` namespace) | Desktop, Android |
| `ws://host:port` | WebSocket unencrypted (`ws` namespace) | all incl. browser |
| `wss://host:port` | WebSocket TLS (`wss` namespace) | all incl. browser |

In the browser (WASM), only `ws://` and `wss://` are available.
`tcp://` on a browser target triggers a runtime error.

### 3.2 Host / Listen

```aiml
- decl: [session, mirror_session, { call: [mirror.listen, url] }]
```

| Call | Return | Description |
|--------|----------|--------------|
| `mirror.listen(url: string)` | `mirror_session` | Listen for incoming connections; non-blocking. **`null` on failure** (port in use, among others) — the return value MUST be checked with `peq: [sess, null]`, otherwise the server keeps running as a zombie: it logs and ticks, but has no socket and answers nothing. |
| `mirror.listen_max(url: string, max_peers: i32)` | `mirror_session` | With peer limit |
| `mirror.listen_tls(url: string, cert: string, key: string)` | `mirror_session` | `wss://` host with a server cert (PEM cert + key paths) |

**Address family.** The host part of a listen URL selects no address family.
`"0.0.0.0"`, `"::"`, `"*"` and an empty host all mean *wildcard* and open a single
**dual-stack** listener that serves IPv4 and IPv6 at once; a literal address binds the
family it names. Clients resolve with `AF_UNSPEC` and try every resolved address in
order. The rules, and why an `AF_INET`-only bind silently refuses every IPv6 client,
are in **spec/32_4 §1.1**.

**TLS hosting (`wss://`)** requires a server certificate — a plain `mirror.listen`
on a `wss://` URL fails, because the WSS server cannot complete a handshake without
one. Use `mirror.listen_tls(url, cert, key)`; the underlying WSS server applies the
hardened TLS defaults (min TLS 1.2, no SSLv2/3/TLS1.0/1.1, no compression — see
chapter 32_4 §3.3). WS/WSS hosts accept asynchronously (chapter 32_4 §5.1):
`session.update` never waits on a connecting client's handshake, so a stalled client
cannot block the host tick (`tests/test_mirror_wss_stall.aiml`). Example:

```aiml
- decl: [session, mirror_session,
    { call: [mirror.listen_tls, "wss://0.0.0.0:8765", "server.crt", "server.key"] }]
```

> **Client verification (strict by default):** `mirror.connect` to a `wss://` host
> **verifies the server certificate** — chain against the system CA store plus hostname
> match (SNI + `SSL_set1_host`) — so it defeats an **active** man-in-the-middle, not just
> passive eavesdropping. A cert that is self-signed, expired, or whose name doesn't match
> the host makes the connection fail.
> - Production: deploy a CA-signed certificate (e.g. Let's Encrypt) for the host name
>   clients connect to — verification then passes with no extra config.
> - A private/internal CA: point `AIML_WSS_CA=/path/to/ca.pem` at its bundle.
> - Local dev with a self-signed cert: `mirror.tls_insecure(1)` (or env
>   `AIML_WSS_INSECURE=1`) disables verification, with a stderr warning. **Never in
>   production.** (In the browser build the browser's own TLS stack enforces this;
>   `mirror.tls_insecure` is a native no-op there.)

### 3.3 Client / Connect

```aiml
- decl: [session, mirror_session, { call: [mirror.connect, url] }]
```

| Call | Return | Description |
|--------|----------|--------------|
| `mirror.connect(url: string)` | `mirror_session` | Establish connection (blocks until connected or error) |
| `mirror.connect_timeout(url: string, ms: i32)` | `mirror_session` | With timeout in ms |

`ms` bounds the **entire** connect — address resolution, TCP, the TLS handshake for
`wss://`, and the HTTP upgrade — not just the TCP part. A peer that completes the TCP
connect and then stays silent (a host whose session is never pumped, a wedged process,
a middlebox dropping the upgrade) **MUST NOT** stall the caller past `ms`; returning
`null` late is a bug, returning `null` on time is the contract. Across several resolved
addresses (spec/32_4 §1.1) the budget is divided, never multiplied.

### 3.4 Closing a Session

| Call | Description |
|--------|--------------|
| `session.close()` | Close session and all peer connections |
| `session.is_connected()` | `bool` — session active and at least one peer connected |
| `session.peer_count()` | `i32` — number of connected peers |
| `session.peer_ip(peer_id: i32)` | `string` — dotted IP of a connected peer, captured at accept; `""` if unknown |

**`session.peer_ip`** gives a handler the **trusted source IP** of an RPC sender —
`peer_id` comes from `rpc_event.peer_id`. It is read from the accepted socket, so it
cannot be spoofed by the client (unlike anything in the RPC payload). Use it for
server-side rate-limiting, per-IP bans, or audit logging. For TCP and WS/WSS transports
the IP is the peer socket address; behind a reverse proxy it is the proxy's address
(read a forwarded header yourself only if you control that proxy).

---

## 4. Registering Objects

```aiml
- decl: [net_id, i32, { call: [session.register, obj] }]
```

| Call | Return | Description |
|--------|----------|--------------|
| `session.register(obj)` | `i32` | Register object; returns a network-wide ID |
| `session.register_id(obj, id: i32)` | — | Register object with fixed ID (for predefined objects such as world entities) |
| `session.unregister(obj)` | — | Remove object from synchronization |
| `session.snapshot_revision(net_id: i32)` | `u64` | Number of remote snapshots successfully applied to this registration (0 = none yet) |
| `session.snapshot_error(net_id: i32)` | `i32` | Last snapshot result: 0 = ok / none, −1 = decode error, −2 = schema or format mismatch, −3 = unknown registration |

Registration automatically notifies peers that this object exists.
The registering peer is the **owner** — only they send updates.
Other peers receive updates and may only read `sync:` fields.

**Objects.**  `register`/`register_id` take an addressable model object (a variable, global,
`ptr<>`, `ref<>` or `shared<>` — the object itself, never a pointer slot or a temporary value; a
temporary is a compile error).  Every serializable model is synchronized through its serializer
descriptor (§10, DELTA format 2), plain-number models included; the raw byte path exists only for
the legacy C API.

**Ownership.**  The session **borrows** the object: registration, unregistration and session
close never free its fields; the normal lifetime (scope end, `free`, last `shared` release) stays
the owner.  The object must outlive its registration (until `unregister` or `close`).

**Readiness.**  A replica is up to date once `snapshot_revision(net_id) > 0` — not when
`object_registered` arrives, not after `register_id`, and not after a number of `update()` calls.
A snapshot is applied completely or not at all (§10); a rejected snapshot leaves the replica and its
revision unchanged and sets `snapshot_error`, the next success clears it.  `register_id` before the
first snapshot is allowed: the object keeps its own initial values until a snapshot is applied.
Both queries are answered from the session thread (no implicit thread safety).

---

## 5. Frame Update

```aiml
- call: [session.update]
```

`session.update()` is called once per frame (typically at the beginning of `update(dt)`):

1. Receive and process incoming packets
2. Send changed `sync:` fields of owned objects as delta packets
3. Execute RPC calls from peers
4. Enqueue peer events (connect/disconnect)

> **"Once per frame" is a recommendation, not a guarantee — and the
> blocking convenience calls undermine it.** `session.send_and_await`,
> `mirror.connect` and `connect_timeout` pump the session **themselves**, in
> 2 ms ticks, until their answer is there (§7, Request/Response). If one of them
> is called from a frame loop, `update()` therefore runs dozens of times **per frame**
> and at an unpredictable point — incoming snapshots and RPCs are
> then applied in the middle of the render pass, not at a defined
> frame boundary. Half the scene can show the old state, the other half the new
> one.
>
> For applications with a frame loop (game, UI) this means: **make your own requests via
> `send_request` + `take_response`** (§7, "For asynchronous clients"),
> call `update()` exactly once at the start of the frame and leave the blocking variants to
> tools, scripts and tests. `send_and_await` is right there,
> where there is no frame it could stall.

---

## 6. Sync Modes

| Call | Description |
|--------|--------------|
| `session.set_sync_rate(obj, hz: f32)` | Sync frequency for this object in Hz; default = `session.update()` rate |
| `session.sync_now(obj)` | Send immediate delta packet (independent of rate) |
| `session.set_sync_mode(obj, mode: string)` | `"auto"` (on every `update` if dirty) or `"manual"` (only via `sync_now`) |

---

## 7. RPCs

### 7.1 RPC methods (automatic object RPCs)

A **class** lists the methods that may be called across the network:

```aiml
- class:
    name: Player
    rpc: [take_damage, say]
    members:
      - [health, i32, 100]
    methods:
      - def:
          name: take_damage
          args: [[amount, i32]]
          body:
            - set: [<.health, { -: [<.health, amount] }]
```

```aiml
# any peer, normal method-call syntax:
- call: [player.take_damage, 25]
```

**Rules (compile time).**  Every `rpc:` entry must be a method defined in that class (a subclass
inherits the parent's RPC methods); it must return `void` and all parameters must be serializable
values (Chapter 45) — anything else is a compile error.  Return values stay with the explicit
request/response API (`send_and_await`, `reply_rpc`).  `rpc:` on a `model` is a compile error
(models have no methods).

**Calls.**  Direct calls, inherited calls and interface (vtable) calls all go through the same
routing step; receiver and arguments are evaluated exactly once.  An object that is **not
registered** with a mirror session behaves like a plain local call.

**Targets** (`session.set_rpc_target(obj, method, target)`; default `"owner"`).  Objects are owned
by the host that registered them (the host assigns the network ids; clients hold replicas):

| target | executes on |
|--------|-------------|
| `"owner"` | the owner (the host) — locally if the caller is the host |
| `"server"` | the host — locally on the host |
| `"all"` | every participating peer, including the caller, exactly once |
| `"others"` | every participating peer except the caller (the host included when it is not the caller) |

A client never sends to other clients: it sends an **object call** (RPC v3 `kind = object_call`,
header `net_id` = object id) to the host, and only the host forwards.  The host routes by **its own**
target setting for that object/method — a client's local `set_rpc_target` cannot widen it — and
only for registered objects with that method; the sender identity is the connection.  On receipt
the arguments are decoded (size-aware) and the method body runs directly (never the network
wrapper again, so nothing is re-sent).  Object calls are never queued for `poll_rpc`.
`set_rpc_target` with an unknown method or target, or on an unregistered object, reports an error
and changes nothing.  Registering the same object in two sessions is rejected for RPC routing.

### 7.2 Low-level RPCs

### RPC Queue (host-side, low-level)

Incoming RPCs can alternatively be read manually from the queue
if the target method should not be executed automatically on the local object
(e.g. for game-phase coordination on a game server):

```aiml
model:
  name: rpc_event
  fields:
    - [method, string]       # name of the called method; "" when queue is empty (poll_rpc sentinel)
    - [net_id, i32]          # net_id of the target object (client-assigned, may collide across peers)
    - [arg, i32]             # legacy i32 argument / fast discriminator
    - [arg_kind, i32]        # 0 = i32 only, 1 = typed model payload
    - [arg_blob, ptr]        # internal blob pointer; AIML code uses decode_rpc instead
    - [arg_blob_len, i32]    # length of the payload blob in bytes
    - [peer_id, i32]         # unique per-connection sender id (host-assigned at accept) — use this, NOT net_id, to identify the sender in multi-client servers
    - [kind, i32]            # 0 push, 1 request, 2 response, 3 object_call (RPC v3; v2 packets arrive as push)
    - [request_id, i64]      # request/response correlation id (0 = none); answer requests with reply_rpc
```

**Routing (no implicit relay).**  A low-level RPC goes from a client **only to the server**; the
host never forwards a client's RPC to other clients.  Server handlers decide explicitly about
answers (`reply_rpc`) and fan-out (`send_rpc` per receiver, or target `0` = broadcast).

**Queue.**  `next_rpc` / `poll_rpc` hand out events in arrival order; `take_rpc`, `take_response` and
`send_and_await` remove only their match — every other event keeps its position.  Per sender peer at
most 4096 events / 64 MiB may be queued; a peer exceeding this is disconnected with a diagnostic
(events of other consumers are never dropped to make room).

> **Abandoned responses must be cancelled, not forgotten.** Because nothing is dropped to make
> room, a queued response that no consumer will ever take counts against that limit forever. This
> happens whenever an application stops caring about a request — a screen closes, a view changes,
> a timeout elapses — and simply discards the request id: the response arrives, is queued, and
> `take_response` can no longer reach it (it checks the id against the outstanding set first). An
> application that does this often enough eventually gets its own peer disconnected.
> Therefore: **call `cancel_request(id)` on every request you stop waiting for** — on timeout, on
> view change, on match exit. It removes the queued response too (§7.2 table). A `poll_rpc` drain
> loop is the other way out, but only if the application actually runs one; a client that uses
> `take_response` exclusively has no drain and must cancel.

### Multi-Client Safety: `peer_id` vs `net_id`

`net_id` in `rpc_event` (and `mirror_event`) is the **object-id** that the
client assigned when registering its object (`session.register(obj)`).
Each client's `register` call independently allocates from its own
`next_net_id = 1` counter, so two clients connecting to the same host
will BOTH typically assign `net_id = 1` for their first registered
object (e.g. `AuthRequest`).  The host's `by_net_id` map keeps the
first registration and silently ignores the colliding second one — but
RPCs from BOTH clients continue to carry `net_id = 1`, making them
indistinguishable by that field alone.

To identify which client a packet came from, use **`peer_id`**:

- `peer_id` is assigned by the host the moment a connection is accepted from a
  per-session counter (1, 2, 3, …)
- It is unique within the session and never reused — after a disconnect the old id addresses
  nobody
- It is populated on all `mirror_event` (`peer_connected`,
  `peer_disconnected`, `object_registered`, `object_unregistered`) and
  on every `rpc_event` emitted by the host's `process_packet`
- A non-host client sees `peer_id = 0` on inbound events (only the host
  observes peer-level identity)

**Rule:** any per-client state on the host (auth-token maps, online-
presence tracking, session-scoped resources) MUST be keyed by `peer_id`,
not `net_id`.  Using `net_id` works for single-client setups but breaks
silently the moment a second client connects.

### Targeted Send: `send_rpc(peer_id, …)`

On the host side, `session.send_rpc(target, method, …)` uses the first
argument as a **peer-routing key**:

- `target > 0` → the packet is sent **only to that peer**; if no connected peer has that id,
  nothing is sent (never a broadcast).
- `target == 0` → explicit broadcast to all connected peers.
- `target < 0` → rejected (nothing sent, diagnostic).

Answering a request uses `reply_rpc`, which takes the sender from the connection and echoes the
request id (§ Request / Response below):

```aiml
- call: [sess.reply_rpc, rpc, "set_result", 0, result]   # answer exactly this request
```

For multi-recipient fanout (e.g. realm chat), iterate the known peer
ids and call `send_rpc` once per receiver — that produces N targeted
packets, not 1 broadcast.  Targeted-only is the safer default because
broadcast leaks per-client state (creature details, private chat
messages) to ALL clients, which is the exact bug multi-client servers
need to avoid.

| Call | Return | Description |
|--------|----------|--------------|
| `session.has_rpc()` | `i32` | Number of pending RPCs in the queue |
| `session.next_rpc()` | `rpc_event` | Consume next RPC (queue must not be empty — UB if empty) |
| `session.poll_rpc()` | `rpc_event` | Consume next RPC, or empty sentinel (`method == ""`) if queue is empty |
| `session.take_rpc(method, target_model)` | `i32` | Remove and decode the **first queued push** with `method` whose payload is of `target_model`'s type; every other event keeps its position. `1` = taken (the target's previous heap is released), `0` = none (target untouched). A queue filter for pushes — it never consumes correlated responses and is no request correlation. |
| `session.send_rpc(target, method, arg)` | - | Send a push RPC.  Host: `target > 0` = exactly that peer (unknown id → nothing sent), `0` = broadcast, `< 0` = rejected.  Clients always send to the server. |
| `session.reply_rpc(ev, method, arg[, model])` | - | Answer the request `ev`: sent to `ev`'s sender peer (from the connection), as a response carrying `ev.request_id`.  If `ev` was not a request, the reply is a plain push to the sender. |
| `session.send_request(target, method, arg, model)` | `i64` | Async request: returns the request id (`> 0`), or `0` if it could not be sent (no peer with RPC v3 yet). |
| `session.take_response(id, target_model)` | `i32` | `1` = the response of request `id` was decoded into `target_model`; `0` = not arrived yet; `-1` = protocol error (wrong method/type, undecodable — the request is finished). |
| `session.cancel_request(id)` | - | Drop interest in request `id`: a response that has **already arrived and been queued** is removed and freed, and a late one is discarded on arrival. After it, `take_response(id, …)` reports `-1` (the request is finished). Cancelling an unknown or already-finished id does nothing. |
| `session.send_rpc(target, method, arg, model)` | - | Send RPC with `i32` plus typed model payload (same routing rule as above) |
| `session.decode_rpc(ev, target_model)` | `i32` | Decode model payload from `ev` into `target_model`, transactionally: `0` = ok (the target's previous heap is released), `-1` = wrong type / decode error (target untouched) |
| `session.send_and_await(target, method, req_model, out_model, timeout_ms)` | `i32` | Client convenience: send `req_model` as a request and wait up to `timeout_ms` (monotonic clock, including update/decode time) for **its** response — matched by request id and responding peer, then checked for `method=="set_result"` and `out_model`'s type.  `1` = decoded into `out_model`; `0` = timeout, peer without RPC v3, or a correlated but unusable response (finishes the request — no endless rescanning).  A response arriving after the timeout is discarded.  **Unrelated inbound RPCs keep their position.** |

### Request / Response (RPC v3)

Correlation is by **request id**, never by method name or payload schema (those are additional
checks).  Request ids are per session, monotonic, never 0 and never reused while outstanding.

- A request (`send_and_await`, `send_request`) is only sent to a peer whose HELLO announced RPC
  v3 (spec §10).  Before that, `send_and_await` keeps pumping `update()` (up to 1.5 s or the
  timeout); a peer without HELLO is v2 and the call reports missing support instead of falling back
  to unsafe method matching.
- The server answers with `reply_rpc(ev, …)`: target = `ev.peer_id`, `kind = response`,
  `request_id = ev.request_id`.  A plain `send_rpc` is a push and never fulfils a request.
- On the requester, a response is accepted only for an outstanding request id; late, duplicate and
  unknown responses are dropped on arrival (logged) without touching pushes.  An already queued
  response of the same type can never fulfil a *new* request (different id).

### RPC Payload v2

Mirror RPCs carry exactly one logical argument in two possible forms:

- **Legacy / trivial:** only `arg: i32`
- **Typed:** `arg: i32` plus serialized model payload

The `i32` is retained even on the typed path. It can be used as a fast
discriminator, slot index, or compatibility field.
Complex data should be in the model, not in manually packed integers.

The wire payload for `PKT_RPC` is:

```text
[method_len:u8][method:N][arg_kind:u8][arg_i32:i32le][blob_len:u32le][blob:M]
```

- `arg_kind = 0`: only `arg_i32`, `blob_len` must be `0`
- `arg_kind = 1`: `arg_i32` plus serialized model (`blob`)
- `blob` is the complete heap serialization blob including the 4-byte length prefix from Chapter 45

`poll_rpc()` is the recommended approach for game server loops, because no explicit
`has_rpc` predicate is needed:

```aiml
# Recommended pattern (poll_rpc):
- call: [sess.update]
- decl: [rpc, rpc_event, { call: [sess.poll_rpc] }]
- decl: [rm, string, rpc.method]
- if:
    condition: { eq: [rm, "pass_turn"] }
    then:
      - set: [done, 1]
- if:
    condition: { eq: [rm, "draw"] }
    then:
      - call: [handle_draw, rpc.arg]
- if:
    condition: { eq: [done, 0] }
    then:
      - call: [time.sleep_ms, 16]

# If rm == "" (empty queue): all eq checks fail -> sleep -> next iteration.
# No separate has_rpc predicate needed.
```

### Decoding a Typed Payload

```aiml
- decl: [rpc, rpc_event, { call: [sess.poll_rpc] }]
- if:
    condition: { eq: [rpc.method, "cast"] }
    then:
      - if:
          condition: { eq: [rpc.arg_kind, 1] }
          then:
            - decl: [req, CastRequest]
            - decl: [rc, i32, { call: [sess.decode_rpc, rpc, req] }]
            - if:
                condition: { eq: [rc, 0] }
                then:
                  - call: [handle_cast, rpc.arg, req]
```

Notes:

- `decode_rpc(...)` expects a caller-allocated model as the target variable.
- The target model is internally zero-initialized before decoding.
- Models with `string`, `list<T>` or `map<K:V>` are allowed; decoding uses heap serialization from Chapter 45.
- If `rpc.arg_kind != 1`, the model type does not match, or the blob is corrupt, `decode_rpc(...)` returns `-1`.
- Type match: every typed RPC payload carries a structural schema fingerprint (field kinds and nesting —
  not offsets or sizes, so a 32-bit WASM peer and a 64-bit native peer agree). A payload of another
  model type is rejected with `-1` and the target model stays empty (valid empty containers); `send_and_await` keeps
  waiting for a reply of the expected type. (The KV fingerprint, spec/45, is separate and unchanged.)

### Convenience: One-Shot Send + Await

For the dominant request/response client-pattern there is `send_and_await`,
which collapses the send + poll-loop + decode triple into one call:

```aiml
- decl: [req,  CreateCardReq]
- set: [req.token, my_tok]
- set: [req.name,  "Stone Golem"]
- decl: [resp, ptr<CardResp>, { new: CardResp }]
- decl: [ok,   i32, { call: [session.send_and_await, 0, "create_card", req, resp, 4000] }]
- if: { condition: { eq: [ok, 0] }, then: [<timeout-handler>] }
- if: { condition: { neq: [resp.success, 1] }, then: [<error-handler>] }
```

Internally sends a v3 request and pumps `update()` in 2ms ticks until the response with the
request's id arrives from the target peer or `timeout_ms` (monotonic clock) elapses.  Unrelated
RPCs received during the wait keep their position in the queue, so in-band server→client
push-notifications interleaved with request/response are safe: a message that arrives
mid-`send_and_await` is not lost and shows up on the next `has_rpc()` / `poll_rpc()` drain.

The server side answers with `reply_rpc` (never with a plain `send_rpc`):

```aiml
- decl: [rpc, rpc_event, { call: [sess.poll_rpc] }]
- if:
    condition: { eq: [rpc.method, "create_card"] }
    then:
      - decl: [req, CreateCardReq]
      - if: { condition: { eq: [{ call: [sess.decode_rpc, rpc, req] }, 0] }, then: [ ... ] }
      - call: [sess.reply_rpc, rpc, "set_result", 0, resp]
```

For asynchronous clients (no blocking per frame) use `send_request` + `take_response`:

```aiml
- set: [<.req_id, { call: [sess.send_request, 0, "list_decks", 0, req] }]
# … every frame:
- decl: [got, i32, { call: [sess.take_response, <.req_id, decks_resp] }]
- if: { condition: { eq: [got, 1] }, then: [ ... ] }
```  A typical
event-driven client therefore uses `send_and_await` for its own requests and,
each frame, drains `poll_rpc()` for server pushes (chat, presence, live match
updates) — no polling of the server required.

Both `req` and `out_model` must be variables of registered model types so
the compiler can attach their `serNodes`/`serNodesCount` to the runtime
call.  `out_model` may be a plain model or `ptr<Model>` — both work.

---

## 8. Peer Events

| Call | Return | Description |
|--------|----------|--------------|
| `session.has_event()` | `i32` | Event queue not empty (>0) |
| `session.next_event()` | `mirror_event` | Consume next event (queue must not be empty — UB if empty) |
| `session.poll_event()` | `mirror_event` | Consume next event, or empty sentinel (`type == ""`) if queue is empty |

```aiml
model:
  name: mirror_event
  fields:
    - [type,    string]      # "peer_connected" | "peer_disconnected" | "object_registered" | "object_unregistered"
                             # "" when queue is empty (poll_event sentinel)
    - [peer,    mirror_peer] # affected peer (null for object_* events)
    - [net_id,  i32]         # affected object ID (for object_*); 0 for peer_* events
    - [peer_id, i32]         # unique per-connection peer id (host-assigned at accept); use this to identify clients in multi-tab/multi-client servers
```

**Ordering between the event and RPC queues.** Events and RPCs are separate queues, so their
relative order is not preserved: RPCs a peer sent just before its connection dropped can still be
in the RPC queue when `peer_disconnected` is polled — draining events first then hands them out
*after* the disconnect. A server that keys state by `peer_id` must therefore ignore (or answer
without effect) RPCs from a `peer_id` it has already seen disconnect; a reconnecting client gets a
new `peer_id` and re-sends. (`peer_id`s are never reused, so this check is exact.)

### When a peer counts as disconnected

`peer_disconnected` is a statement about the **connection**, not about a single failed call.  An
implementation MUST raise it only when one of these is true:

- the peer closed (TCP FIN, or a WebSocket CLOSE frame — `recv` returned 0),
- the transport reported a real error (`recv`/`poll`/`SSL_read` failed for a reason other than the
  two below),
- the peer was marked dead by a protocol rule of this chapter (invalid frame §10, queue or backlog
  overflow §7.2/§11).

It MUST NOT raise it when the call was merely **interrupted or would block**:

- `EINTR` — a signal arrived while the process sat in `poll()` or `recv()`.  The connection is
  untouched; the call is retried.  A process that receives signals at all (a window system, an
  audio thread, a child process exiting) would otherwise drop healthy peers at random intervals,
  and the drop looks exactly like a network failure.
- `EAGAIN` / `EWOULDBLOCK` / `SSL_ERROR_WANT_READ` / `SSL_ERROR_WANT_WRITE` — no data yet.  Outside
  a frame there is nothing to report; mid frame the read is resumed as long as the frame keeps
  making progress.

  A frame that stops **mid-way** is the one exception.  Once a peer has announced a frame length and
  then delivers nothing more for the duration of the implementation's read timeout, that peer counts
  as a real transport error under the second bullet above and is dropped, naming the reason
  (`recv failed: Resource temporarily unavailable`).  Without that bound a single-threaded server
  reading a frame with blocking `recv` waits forever and stops serving every other peer (measured on
  the cosmos controller, 18.10.2026: log ends mid-tick, 13 kB unread in one peer's queue).

  The bound is a stopgap, not the design.  What is owed here is an incremental, non-blocking frame
  reader with a per-connection buffer (spec/32_4 §Lesen), which resumes a stalled frame instead of
  dropping the peer.  Until that exists an implementation MUST keep the timeout far above any delay
  a healthy link can produce, and MUST name it in the drop line — a mid-frame stall and a real reset
  are then told apart in the log, not guessed at.

**Every drop carries a diagnostic.**  Exactly one line per dropped peer, on stderr, naming the peer
and the reason:

```
[mirror] peer 2: peer closed — disconnecting
[mirror] peer 7: recv failed: Connection reset by peer — disconnecting
[mirror] peer 9: invalid frame — disconnecting
```

A silent drop is a defect: the application sees a dead session and has nothing to go on.  The
reason text is diagnostic only — programs branch on the event, never on the wording.

### `type_enum()` — typed event kind

`mirror_event` also exposes a method `type_enum()` returning the built-in enum
`MirrorEventType`, the typed companion to the `type` string field:

```
enum MirrorEventType:
  none = 0                 # empty/sentinel (queue empty)
  peer_connected = 1
  peer_disconnected = 2
  object_registered = 3
  object_unregistered = 4
```

Prefer it over a string switch on `evt.type` — it is enum-checked and avoids the
`str_cmp` static rule (spec/49 §16):

```aiml
- decl: [ev,   mirror_event,    { call: [sess.poll_event] }]
- decl: [kind, MirrorEventType, { call: [ev.type_enum] }]
- if: { condition: { eq: [kind, MirrorEventType.peer_connected] }, then: [ ... ] }
```

`MirrorEventType` is a built-in enum (always available, no declaration needed).

`poll_event()` simplifies event loops when only a specific event type is expected:

```aiml
# has_event + next_event (classic):
- if:
    condition: { call: [sess.has_event] }
    then:
      - decl: [ev, mirror_event, { call: [sess.next_event] }]
      - if:
          condition: { eq: [ev.type, "peer_connected"] }
          then:
            - call: [on_connected]

# poll_event (more concise):
- decl: [ev, mirror_event, { call: [sess.poll_event] }]
- if:
    condition: { eq: [ev.type, "peer_connected"] }
    then:
      - call: [on_connected]
# If ev.type == "": queue was empty, no call needed.
```

### Waiting for a Specific Event

If the code should block until a specific event (e.g. handshake),
a simple poll loop works well:

```aiml
# Wait for "object_registered", up to 300 ticks × 16 ms:
- decl: [found_nid, i32, -1]
- decl: [ticks, i32, 0]
- while:
    condition: { "&&": [{ eq: [found_nid, -1] }, { "<": [ticks, 300] }] }
    body:
      - call: [sess.update]
      - decl: [ev, mirror_event, { call: [sess.poll_event] }]
      - if:
          condition: { eq: [ev.type, "object_registered"] }
          then:
            - set: [found_nid, ev.net_id]
      - call: [time.sleep_ms, 16]
      - set: [ticks, { +: [ticks, 1] }]
```

---

## 9. Complete Example

### Host (Server)

```aiml
# Open session on TCP and WS simultaneously
- decl: [tcp_session, mirror_session, { call: [mirror.listen, "tcp://0.0.0.0:7777"] }]
- decl: [ws_session,  mirror_session, { call: [mirror.listen, "ws://0.0.0.0:7778"]  }]

# Create and register player objects
- decl: [p1, Player]
- set:  [p1.name, "Server-Player"]
- set:  [p1.pos,  { call: [math.vec3, 0.0, 1.0, 0.0] }]
- set:  [p1.health, 100]
- decl: [p1_id, i32, { call: [tcp_session.register, p1] }]

- def:
    name: update
    args: [[dt, f32]]
    body:
      # Process incoming packets, send delta updates
      - call: [tcp_session.update]
      - call: [ws_session.update]

      # Process peer events
      - while:
          condition: { call: [tcp_session.has_event] }
          body:
            - decl: [ev, mirror_event, { call: [tcp_session.next_event] }]
            - if: [{ eq: [ev.type, "peer_connected"] }, true]
              then:
                - print: ["New peer connected"]

      # Move player (automatically sent to all peers because pos is in sync:)
      - decl: [fwd, f32, { call: [input.axis, "forward"] }]
      - set:  [p1.pos, { call: [math.vec3,
                          { "+": [p1.pos.x, { "*": [fwd, dt] }] },
                          p1.pos.y,
                          p1.pos.z] }]
```

### Client (browser-compatible via WebSocket)

```aiml
# Browser: use ws:// (no tcp:// in browser)
- decl: [platform, string, { call: [engine.platform] }]
- decl: [session,  mirror_session]

- if: [{ eq: [platform, "web"] }, true]
  then:
    - set: [session, { call: [mirror.connect, "ws://gameserver.com:7778"] }]
  else:
    - set: [session, { call: [mirror.connect, "tcp://gameserver.com:7777"] }]

- decl: [my_player, Player]
- set:  [my_player.name, "Client-Player"]
- decl: [my_id, i32, { call: [session.register, my_player] }]

- def:
    name: update
    args: [[dt, f32]]
    body:
      - call: [session.update]

      # Replicated server objects are up to date after session.update
      # p1 (server player) is updated automatically — no extra logic needed
      - call: [render_player, my_player]

      # RPC: take damage → forwarded to server
      - decl: [hit, bool, { call: [check_hit] }]
      - if: [{ eq: [hit, true] }, true]
        then:
          - call: [my_player.take_damage, 10]   # → RPC to owner (server)
```

---

## 10. Packet Format (binary)

All integers are little-endian.  Every packet starts with the same 6-byte header:

```text
[net_id:u32][type:u8][reserved:u8]
```

| type | name | body after the header | total length |
|------|------|------------------------|--------------|
| 0x01 | DELTA (typed, `reserved` = format 2) | `[payload_len:u32][wire_magic:u32][schema_fp:u32][encoded fields]` | 10 + `payload_len` |
| 0x01 | DELTA (raw, `reserved` = 0, legacy C API only) | raw struct bytes | 6 + registered object size (TCP); the whole message (WebSocket) |
| 0x02 | RPC v2 | `[method_len:u8][method][arg_kind:u8][arg_i32:i32][blob_len:u32][blob]` | 6 + 1 + N + 9 + M |
| 0x03 | REGISTER | `[net_id:i32][obj_size:i32]` | 14 |
| 0x04 | UNREGISTER | — | 6 |
| 0x05 | RPC v3 (`reserved` = version 3) | `[total_len:u32][kind:u8][flags:u8][request_id:u64][method_len:u8][method][arg_kind:u8][arg_i32:i32][blob_len:u32][blob]` | `total_len` = 30 + N + M |
| 0x06 | HELLO (`reserved` = highest supported RPC version) | — | 6 |

**Validation (every RPC, v2 and v3).**  A frame is accepted only if all of these hold, otherwise
the sending peer is disconnected with a diagnostic (`[mirror] invalid frame …`); nothing of the
frame is applied:

- the frame length is exactly the computed total length (a WebSocket message carries exactly one
  packet; trailing bytes are an error); total length ≤ 8 MiB + 512;
- `method_len` is 1–255 and the method contains no NUL byte (senders refuse longer names instead
  of truncating);
- `arg_kind` is 0 or 1; `arg_kind = 0` requires `blob_len = 0`; `arg_kind = 1` requires
  `blob_len ≥ 4` and the blob's own length prefix to equal `blob_len − 4`;
- v3 only: `kind` ∈ {0 push, 1 request, 2 response, 3 object_call}, `flags = 0`, and a typed blob
  must carry the RPC magic + structural fingerprint (spec 44_5 §7 "Type match").  v2 typed blobs
  without the tag are accepted as legacy.

**DELTA (format 2).**  The body is the serialization blob in wire mode: the RPC envelope
(`wire_magic = 0xA1F5C0DF` + structural fingerprint, §7 "Type match") followed by the encoded
fields; `payload_len` counts everything after itself (≥ 8, ≤ 8 MiB).  Persisted KV blobs are not
affected.  Rules:

- a registration bound with a serializer descriptor accepts only format 2; format 0 (raw) and the
  untagged former format 1 are rejected (`snapshot_error = −2`) — raw bytes never reach a typed
  object; a raw registration rejects format 2; any other `reserved` value is an invalid frame;
- the magic and the fingerprint must match the receiver's descriptor (−2), then the payload is
  decoded completely, bounded and transactionally (below; −1 on failure); only then is the
  replica's previous content released and replaced, and its `snapshot_revision` incremented;
- the fingerprint covers serializer kinds and nesting, not field names or every semantic type
  (`i32`/`f32` share a kind) and is no authentication: wire-schema compatibility is guaranteed, model
  versions that differ in their wire schema are rejected; migration between model versions is not
  part of the mirror;
- a DELTA that arrives before `register_id` is kept (with its format) and applied with the same
  rules at binding time;
- the host forwards a client's DELTA only if it accepted it itself, or if it holds just a
  placeholder for that net id (the bound receiver validates);
- dirty tracking compares exactly the blob that is sent.

DELTA format 2 needs both peers on the same version: there is no capability negotiation for it (RPC
v3 / HELLO does not imply it) and no fallback — client and host (native and WASM, including cached
browser builds) are upgraded together.

**Test hook (optional, production code).**  With `AIML_MIRROR_TEST_HOLD_DELTA=<path>` set in a
process, its sessions hold every outgoing DELTA (in order, per peer) while the file `<path>` does
not exist; REGISTER and all other packets pass.  Creating the file releases the held DELTAs on the
next `update()`.  Off by default; meant only for tests that need a deterministic gap between
REGISTER and the first snapshot.

**Decoding** of any network blob (RPC payloads and typed DELTAs) is size-aware: the blob is decoded
with its received length as the hard bound, every byte must be consumed, container counts may not
exceed the remaining bytes, nesting is limited to 64 levels and one decode may allocate at most
64 MiB.  A failed decode never mutates the destination (transactional decode into a temporary).

**TCP framing.**  TCP peers keep a per-peer receive buffer; complete packets are consumed by the
same parser as WebSocket messages, partial packets wait for the next `update()`, so several packets
per read and packets split across reads are both handled.  Partial sends are buffered per peer and
flushed on the next `update()`; a peer whose unsent backlog exceeds 64 MiB is disconnected.

**HELLO / capabilities.**  Both sides send HELLO right after the connection is established.  RPC v3
(request/response correlation, §7) is used towards a peer only after its HELLO announced
version ≥ 3; until then a client keeps pumping `update()` inside `send_and_await`.  A peer without
HELLO is treated as v2: one-way pushes work, `send_and_await` reports missing support and returns 0
instead of falling back to unsafe method matching.

## 11. Implementation Notes

### 11.1 Dirty Tracking

For each owned object the mirror layer keeps the last sent DELTA blob; `session.update()`
encodes the object (format 2) and sends a DELTA only if the blob differs from the last one sent
(this also detects deep changes inside lists and nested models).

### 11.2 Transport Abstraction

```cpp
struct AimlMirrorTransport {
    enum Type { TCP, WS, WSS } type;
    union {
        int tcp_fd;          // AimlSocket.fd
        AimlWs* ws;          // WebSocket handle
    };
    bool is_listener;
};
```

`mirror_send` / `mirror_recv` delegate internally to
`aiml_net_send` / `aiml_ws_send` depending on `type`.

### 11.3 Object Lookup

```cpp
unordered_map<uint32_t, void*>   net_id_to_obj;   // net_id → object pointer
unordered_map<void*, uint32_t>   obj_to_net_id;   // object pointer → net_id
unordered_map<uint32_t, AimlMirrorMeta*> meta;    // net_id → serialize/deserialize fn-ptrs
```

`AimlMirrorMeta` contains the compiler-generated function pointers for
serialization, deserialization, and RPC dispatch.

### 11.4 Interpolation (optional, v2)

For `vec3`/`vec2`/`f32` fields with high sync rate, client-side
linear interpolation between received values can be enabled:

```aiml
- call: [session.set_interpolation, obj, "pos", true]
```

In v1, values are applied immediately (no smoothing).
