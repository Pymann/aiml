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

Fields and methods to be mirrored over the network are marked directly in the
`model` declaration:

```aiml
- model: Player
  sync: [pos, health, score]     # these fields are automatically replicated
  rpc:  [take_damage, respawn]   # these methods are forwarded to peers
  fields:
    - [pos,    vec3]
    - [health, i32]
    - [score,  i32]
    - [name,   string]           # not in sync: → local only
    - [inv,    list<string>]     # not in sync: → local only
```

- **`sync:`** — list of field names; the compiler automatically generates
  serialization/deserialization for these fields
- **`rpc:`** — list of method names; calls are forwarded to the owner
  (or all peers) with encoded arguments
- Fields not listed in `sync:` are never transmitted

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
| `mirror.listen(url: string)` | `mirror_session` | Listen for incoming connections; non-blocking |
| `mirror.listen_max(url: string, max_peers: i32)` | `mirror_session` | With peer limit |

### 3.3 Client / Connect

```aiml
- decl: [session, mirror_session, { call: [mirror.connect, url] }]
```

| Call | Return | Description |
|--------|----------|--------------|
| `mirror.connect(url: string)` | `mirror_session` | Establish connection (blocks until connected or error) |
| `mirror.connect_timeout(url: string, ms: i32)` | `mirror_session` | With timeout in ms |

### 3.4 Closing a Session

| Call | Description |
|--------|--------------|
| `session.close()` | Close session and all peer connections |
| `session.is_connected()` | `bool` — session active and at least one peer connected |
| `session.peer_count()` | `i32` — number of connected peers |

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

Registration automatically notifies peers that this object exists.
The registering peer is the **owner** — only they send updates.
Other peers receive updates and may only read `sync:` fields.

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

---

## 6. Sync Modes

| Call | Description |
|--------|--------------|
| `session.set_sync_rate(obj, hz: f32)` | Sync frequency for this object in Hz; default = `session.update()` rate |
| `session.sync_now(obj)` | Send immediate delta packet (independent of rate) |
| `session.set_sync_mode(obj, mode: string)` | `"auto"` (on every `update` if dirty) or `"manual"` (only via `sync_now`) |

---

## 7. RPCs

RPC methods are called like regular methods.
Internally, the compiler-generated stub serializes the arguments and sends them.

```aiml
# On the calling peer (owner or any peer depending on RPC target):
- call: [player.take_damage, 25]

# On the target peer, take_damage(25) is executed
```

### RPC Direction

| Call | Description |
|--------|--------------|
| `session.set_rpc_target(obj, method: string, target: string)` | `"owner"` (default), `"all"`, `"others"`, `"server"` |

- **`"owner"`**: RPC is forwarded to the owner of the object (most common case: client → server)
- **`"all"`**: RPC is sent to all peers including local execution
- **`"others"`**: RPC is sent to all other peers, not executed locally
- **`"server"`**: always to the host (even if the caller itself is the host → local)

### RPC Queue (host-side, low-level)

Incoming RPCs can alternatively be read manually from the queue
if the target method should not be executed automatically on the local object
(e.g. for game-phase coordination on a game server):

```aiml
model:
  name: rpc_event
  fields:
    - [method, string]       # name of the called method; "" when queue is empty (poll_rpc sentinel)
    - [net_id, i32]          # net_id of the target object
    - [arg, i32]             # legacy i32 argument / fast discriminator
    - [arg_kind, i32]        # 0 = i32 only, 1 = typed model payload
    - [arg_blob, ptr]        # internal blob pointer; AIML code uses decode_rpc instead
    - [arg_blob_len, i32]    # length of the payload blob in bytes
```

| Call | Return | Description |
|--------|----------|--------------|
| `session.has_rpc()` | `i32` | Number of pending RPCs in the queue |
| `session.next_rpc()` | `rpc_event` | Consume next RPC (queue must not be empty — UB if empty) |
| `session.poll_rpc()` | `rpc_event` | Consume next RPC, or empty sentinel (`method == ""`) if queue is empty |
| `session.send_rpc(net_id, method, arg)` | - | Send legacy RPC with pure `i32` argument |
| `session.send_rpc(net_id, method, arg, model)` | - | Send RPC with `i32` plus typed model payload |
| `session.decode_rpc(ev, target_model)` | `i32` | Decode model payload from `ev` into `target_model`; `0` = ok, `-1` = wrong type / decode error |

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
    condition: { "==": [done, 0] }
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
```

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
    condition: { "&&": [{ "==": [found_nid, -1] }, { "<": [ticks, 300] }] }
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
      - if: [{ ==: [hit, true] }, true]
        then:
          - call: [my_player.take_damage, 10]   # → RPC to owner (server)
```

---

## 10. Packet Format (binary)

```
Header (6 bytes):
  [net_id:   u32]   Object ID
  [type:     u8]    0x01=delta, 0x02=rpc, 0x03=register, 0x04=unregister
  [reserved: u8]

Delta packet:
  [field_mask: u32]   Bitmask of which fields are included (max. 32 sync fields)
  [field_data: ...]   Serialized field values in declaration order

RPC packet:
  [method_id: u8]     Index of the RPC method in the rpc: list
  [arg_data:  ...]    Serialized arguments
```

Primitive types are encoded little-endian.
`vec3` = 3 × f32 (12 bytes), `mat4` = 16 × f32 (64 bytes), `string` = u16 length + UTF-8 bytes.

---

## 11. Implementation Notes

### 11.1 Dirty Tracking

For each registered object, the mirror layer maintains a shadow buffer
of the last sent `sync:` field values. `session.update()` compares
the current state with the shadow → produces a `field_mask` for changed fields →
sends a delta packet only if at least one field changed.

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
