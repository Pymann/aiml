# Chapter 32.4 — WebSockets

> **Design principle:** WebSockets are implemented in AIML **without an external library** —
> directly on top of `net.*` (WS) and `tls.*` (WSS). This reduces dependencies and
> gives full control over the frame protocol (RFC 6455). The `ws_handle` is
> an opaque pointer that internally encapsulates the TCP socket, the frame parser state,
> and a send mutex (thread safety).

---

## 1. Types

| Type               | LLVM  | Description |
|--------------------|-------|-------------|
| `ws_handle`        | `i8*` | Client- or server-side WebSocket connection |
| `ws_server_handle` | `i8*` | WebSocket server (binds port, manages connections) |

---

## 2. WebSocket Client

### 2.1 Connecting and Sending Messages

```aiml
# WS client (unencrypted)
- decl: [ws_url, url]
- set: [ws_url, "ws://echo.websocket.org/"]
- decl: [ws, ws_handle]
- set: [ws, { call: [ws.connect, ws_url] }]

# Send text
- call: [ws.send, "Hello WebSocket!"]

# Receive text (blocks until a complete message arrives)
- decl: [msg, string]
- set: [msg, { call: [ws.recv] }]
- print: ["Echo: ${msg}"]

- call: [ws.close]
```

### 2.2 WSS (Encrypted via TLS)

```aiml
# URL scheme "wss://" → TLS transport automatically
- decl: [stream_url, url]
- set: [stream_url, "wss://stream.example.com/live"]
- decl: [ws, ws_handle]
- set: [ws, { call: [ws.connect, stream_url] }]

- call: [ws.send, "{\"subscribe\": \"btc_usd\"}"]

- decl: [event, string]
- set: [event, { call: [ws.recv] }]
- print: ["Event: ${event}"]

- call: [ws.close]
```

`ws.connect` detects `ws://` vs. `wss://` from the URL scheme and selects
`net.*` or `tls.*` as transport accordingly.

### 2.3 Binary Messages

```aiml
- decl: [bin_url, url]
- set: [bin_url, "ws://localhost:9001/binary"]
- decl: [ws, ws_handle]
- set: [ws, { call: [ws.connect, bin_url] }]

# Send binary (buffer)
- decl: [buf, buffer]
- call: [buf.alloc, 256]
- call: [buf.write, 1, u8]      # raw bytes: write_string stops at a 0x00 (spec/11)
- call: [buf.write, 2, u8]
- call: [ws.send_bytes, buf]

# Receive binary
- decl: [data, buffer]
- set: [data, { call: [ws.recv_bytes] }]
- decl: [sz, i32]
- set: [sz, { call: [data.size] }]
- print: ["Binary size: ${sz}"]

- call: [ws.close]
```

### 2.4 Ping / Pong

```aiml
- call: [ws.ping, "heartbeat"]   # Sends ping frame; pong response handled transparently by ws.recv
- decl: [alive, bool]
- set: [alive, { call: [ws.is_open] }]
- print: ["Connected: ${alive}"]
```

`ws.recv` processes pong frames transparently (not returned to caller).
Ping frames from the peer are automatically answered with pong.

### 2.5 `ws_handle` Methods

| Method        | Arguments       | Return   | Description |
|---------------|-----------------|----------|-------------|
| `send`        | `msg: string`   | `i32`    | Send text frame (opcode 0x1) |
| `send_bytes`  | `buf: buffer`   | `i32`    | Send binary frame (opcode 0x2) |
| `recv`        | —               | `string` | Receive next complete text message |
| `recv_bytes`  | —               | `buffer` | Receive next complete binary message |
| `ping`        | —               | `i32`    | Send ping frame |
| `close`       | —               | `i32`    | Send close frame + close connection |
| `is_open`     | —               | `bool`   | `false` after close frame or TCP error |
| `set_timeout` | `ms: i32`       | `i32`    | Connection: recv/send timeout (`0` = blocking). Listener: handshake timeout (§5.1). `0` on success, `-1` on error |
| `fd`          | —               | `i32`    | Raw TCP file descriptor |

---

## 3. WebSocket Server

### 3.1 Simple Echo Server

```aiml
- def:
    name: on_connect
    args: [[conn, ws_handle]]
    body:
      - print: ["Client connected"]

- def:
    name: on_message
    args: [[conn, ws_handle], [msg, string]]
    body:
      - print: ["Got: ${msg}"]
      - call: [conn.send, msg]   # Echo back

- def:
    name: on_close
    args: [[conn, ws_handle], [code, i32]]
    body:
      - print: ["Client disconnected (${code})"]

- decl: [srv, ws_server_handle]
- set: [srv, { call: [ws.server, "0.0.0.0", 9001] }]

- call: [srv.on_connect, on_connect]
- call: [srv.on_message, on_message]
- call: [srv.on_close, on_close]

- print: ["WS Server listening on :9001"]
- call: [srv.start]   # Blocks
```

### 3.2 Broadcast Server (All Clients)

```aiml
- decl: [clients, list<ws_handle>]

- def:
    name: ws_on_connect
    args: [[conn, ws_handle]]
    body:
      - call: [clients.push, conn]
      - decl: [n, i32]
      - set: [n, { call: [clients.len] }]
      - print: ["Client joined. Total: ${n}"]

- def:
    name: ws_on_message
    args: [[conn, ws_handle], [msg, string]]
    body:
      # Broadcast: send to all connected clients
      - decl: [c, ws_handle]
      - for: [c, clients]
        body:
          - call: [c.send, msg]

- def:
    name: ws_on_close
    args: [[conn, ws_handle], [code, i32]]
    body:
      # TODO: remove conn from clients list (remove by value — future work)
      - print: ["Client left"]

- decl: [srv, ws_server_handle]
- set: [srv, { call: [ws.server, "0.0.0.0", 9002] }]
- call: [srv.on_connect, ws_on_connect]
- call: [srv.on_message, ws_on_message]
- call: [srv.on_close, ws_on_close]
- call: [srv.start]
```

### 3.3 WSS Server (TLS)

```aiml
- decl: [cert, path]
- set: [cert, "/etc/myapp/server.crt"]
- decl: [key, path]
- set: [key, "/etc/myapp/server.key"]

- decl: [srv, ws_server_handle]
- set: [srv, { call: [ws.server_tls, "0.0.0.0", 9443, cert, key] }]

- call: [srv.on_message, on_message]
- call: [srv.start]
```

**Secure TLS defaults:** the WSS server applies the *same* hardened `SSL_CTX` policy as the
HTTPS server (chapter 32_2 §4) via the shared `aiml_tls_harden_server_ctx` helper —
**minimum TLS 1.2**, SSLv2/SSLv3/TLS 1.0/TLS 1.1 and TLS-layer compression disabled, server
cipher preference honoured. A missing or unreadable cert/key makes `ws.server_tls` fail
(returns null) rather than starting a listener that cannot complete any handshake. `cert` may hold the full chain (leaf first, then intermediates, e.g. Let's Encrypt `fullchain.pem`); the whole chain is sent to clients, so clients that do not cache intermediates can verify it. The
`tests/test_ws.aiml` Phase C wss loopback exercises this hardened path.

### 3.4 `ws_server_handle` Methods

| Method              | Arguments              | Return          | Description |
|---------------------|------------------------|-----------------|-------------|
| `on_connect`        | `handler: fn`          | —               | Callback on new connection; `fn(conn: ws_handle)` |
| `on_message`        | `handler: fn`          | —               | Callback on text message; `fn(conn: ws_handle, msg: string)` |
| `on_binary`         | `handler: fn`          | —               | Callback on binary message; `fn(conn: ws_handle, buf: buffer)` |
| `on_close`          | `handler: fn`          | —               | Callback on close; `fn(conn: ws_handle, code: i32)` |
| `on_error`          | `handler: fn`          | —               | Callback on error; `fn(conn: ws_handle, err: string)` |
| `start`             | —                      | —               | Blocking accept loop |
| `start_async`       | —                      | `thread_handle` | Non-blocking |
| `stop`              | —                      | —               | Stop accept loop |
| `set_max_frame_size`| `bytes: i32`           | —               | Max frame size (default: 1 MB) |

---

## 4. WebSocket Upgrade from HTTP Server

A running HTTP server can upgrade individual connections to WebSocket:

```aiml
- def:
    name: ws_handler
    args: [[conn, ws_handle]]
    body:
      - decl: [msg, string]
      - set: [msg, { call: [conn.recv] }]
      - call: [conn.send, "Echo: ${msg}"]
      - call: [conn.close]

- def:
    name: upgrade_endpoint
    args: [[req, http_request_handle], [resp, http_response_handle]]
    body:
      # Checks upgrade headers, performs handshake, calls ws_handler
      - call: [resp.upgrade_ws, ws_handler]

- decl: [srv, http_server_handle]
- set: [srv, { call: [http.server, "0.0.0.0", 8080] }]
- call: [srv.route, "GET",  "/",   serve_html]
- call: [srv.route, "GET",  "/ws", upgrade_endpoint]   # WS upgrade on /ws
- call: [srv.start]
```

`ctx.upgrade_ws(handler_fn)` checks whether the request is a valid WebSocket upgrade request
(`Upgrade: websocket`, `Connection: Upgrade`, `Sec-WebSocket-Key`), responds with
`101 Switching Protocols`, and then passes a `ws_handle` to the handler.

---

## 5. Complete `ws` Namespace Reference

| Call | Return | Description |
|------|--------|-------------|
| `{ call: [ws.connect, u] }` | `ws_handle` | Connect to WS/WSS URL (`u: url`); scheme determines transport |
| `{ call: [ws.server, host, port] }` | `ws_server_handle` | Plain WS server |
| `{ call: [ws.server_tls, host, port, cert, key] }` | `ws_server_handle` | WSS server (`cert, key: path`) |

---

## 5.1 Binary Media Transport (implemented)

The callback server model of §3 (`on_connect` / `on_binary` / `start`) is **not wired**
in the current runtime.  Servers use the **imperative** model instead:

```aiml
- decl: [srv,  ws_handle, { call: [ws.server, "0.0.0.0", 8766] }]
- decl: [conn, ws_handle, { call: [ws.accept, srv] }]   # blocks for one client
- decl: [poll, i32,       { call: [ws.poll, conn, 0] }]  # 1=data ready, 0=timeout, -1=closed
```

Binary frames are moved with these primitives (all binary-safe — no NUL truncation):

| Call | Return | Description |
|------|--------|-------------|
| `{ call: [buf.ws_send, conn] }` | `i32` | Send `buf`'s written bytes (`wIdx`) as one binary frame. Bytes sent, or `-1`. |
| `{ call: [buf.ws_recv, conn] }` | `i32` | Receive the next frame **into** `buf` (updates `size`/`wIdx`, resets `rIdx=0`). Returns bytes received, `0` on close, `-1` error, `-2` if the frame exceeded `buf` capacity (buf filled to cap, remainder dropped). |
| `{ call: [ws.send_kv_blob, conn, kv, key] }` | `i32` | **Zero-copy** send: streams the KV blob at `key` straight from the mmap'd WAL region to the socket as one binary frame — no intermediate AIML buffer. Bytes sent, `0` if key absent/empty, `-1` error. Pairs with `kv.set_blob`. |
| `{ call: [buf.ws_send_range, conn, offset, len] }` | `i32` | Send `buf.data[offset .. offset+len]` as one binary frame (zero-copy slice). Chunk a large buffer into fixed-size frames without copying. |
| `{ call: [buf.ws_recv_append, conn] }` | `i32` | Receive the next frame **appended** at `buf`'s write index (advances wIdx/size). Reassemble N chunk frames: `buf.alloc(cap)` once, then `buf.ws_recv_append(conn)` N times. Returns bytes / `0` close / `-1` err / `-2` overflow. |
| `{ call: [ws.peer_ip, conn] }` | `string` | Dotted peer IP of an accepted connection (owned copy). For per-IP rate-limiting on a server's accept loop. |

**Asynchronous accept.** A listener runs the TLS handshake (wss) and the HTTP upgrade of
every incoming connection itself, non-blocking and concurrently. A client is handed out
only once its handshake is complete, so a client that connects and then stalls or
drip-feeds never delays other clients or the caller's loop (media accept loop, mirror
WS/WSS hosts — spec/44_5):

| Call | Return | Description |
|------|--------|-------------|
| `{ call: [ws.poll_listener, srv, ms] }` | `i32` | Advance pending handshakes for up to `ms` (`0` = no wait). `1` = a handshaken client is ready, `0` = none yet, `-1` = error. |
| `{ call: [ws.accept, srv] }` | `ws_handle` | Next handshaken client; blocks until one is ready (never on one particular client). `null` on listener error. |
| `{ call: [ws.set_timeout, srv, ms] }` | `i32` | On a listener: handshake timeout per connection (default **5000 ms**); a client not handshaken in time is closed. `0` on success. |

- At most **64** handshakes are pending per listener; connections beyond that are closed
  immediately.
- Pending handshakes advance (and time out) only inside `ws.poll_listener` / `ws.accept`,
  so a server should poll its listener regularly.
- `ws.close(srv)` closes all pending handshakes.

See `tests/test_ws_accept_stall.aiml`.

`buf.ws_send` / `buf.ws_recv` are **buffer methods** (not `ws.*` namespace calls) because
the buffer must be passed by its real address so `ws_recv` can write the length back — a
`ws.func(conn, buf)` form would receive the buffer struct by value and could not mutate it.

**Chunked-streaming pattern** (media server): store each 64 KiB chunk under its own key
(`blob:<hash>#<seq>`) via `kv.set_blob`, then on download loop `ws.send_kv_blob` over the
keys.  The DB chunk equals the WS frame, so nothing is reassembled or copied in memory on
the send side (mmap page → socket).  See `tests/test_ws_media.aiml`.

---

## 6. RFC 6455 — Frame Format (Implementation Reference)

WebSocket frames have the following wire format:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-------+-+-------------+-------------------------------+
|F|R|R|R| opcode|M| Payload len |    Extended payload length    |
|I|S|S|S|  (4)  |A|     (7)    |             (16/64)           |
|N|V|V|V|       |S|             |   (if payload len==126/127)   |
| |1|2|3|       |K|             |                               |
+-+-+-+-+-------+-+-------------+-------------------------------+
|     Extended payload length continued, if payload len == 127  |
+---------------------------------------------------------------+
|                  Masking-key (if MASK set)                    |
+---------------------------------------------------------------+
|              Payload Data (XOR with masking key)              |
+---------------------------------------------------------------+
```

| Opcode | Value | Meaning |
|--------|-------|---------|
| Continuation | 0x0 | Continuation of a fragmented frame |
| Text | 0x1 | UTF-8 text payload |
| Binary | 0x2 | Binary payload |
| Close | 0x8 | Close connection (optional 2-byte status code) |
| Ping | 0x9 | Keepalive request |
| Pong | 0xA | Response to ping |

**Masking:**
- Client → Server: **Always masked** (MASK=1, 4-byte masking key via CSPRNG)
- Server → Client: **Never masked** (MASK=0)
- Masking: `payload[i] ^= masking_key[i % 4]`

**Payload length:**
- 0–125: directly in the 7-bit field
- 126: next 2 bytes as big-endian `uint16`
- 127: next 8 bytes as big-endian `uint64`

**Hard frame-size ceiling (memory safety).** The frame reader rejects any frame whose declared
payload length exceeds a hard ceiling (64 MiB) **before** allocating for it: the receive returns
"no frame" and the caller (a `ws` server, or the Mirror transport) drops that connection. This is
a non-negotiable floor independent of `set_max_frame_size` (which may lower, never raise it). It
stops a single forged header — e.g. the 127 marker with a `2^64-1` length — from driving a
`malloc` of the attacker-declared size (an unauthenticated remote crash; regression
`tests/test_ws_oversized_frame.aiml`). An application layer that needs a tighter bound (Mirror
caps RPC/object payloads at 8 MiB, spec/44_5 §10) still enforces its own limit on top.

### 6.1 Handshake (Client → Server)

```
GET /chat HTTP/1.1\r\n
Host: server.example.com\r\n
Upgrade: websocket\r\n
Connection: Upgrade\r\n
Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n
Sec-WebSocket-Version: 13\r\n
\r\n
```

Server responds:
```
HTTP/1.1 101 Switching Protocols\r\n
Upgrade: websocket\r\n
Connection: Upgrade\r\n
Sec-WebSocket-Accept: <SHA1(key + "258EAFA5-...")  Base64>\r\n
\r\n
```

`Sec-WebSocket-Accept` = `Base64(SHA1(Sec-WebSocket-Key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))`.
This computation is done in `ws_runtime.cpp` using OpenSSL's `SHA1()` + `EVP_EncodeBlock()`.

### 6.2 Fragmentation

Long messages can be fragmented across multiple frames:
- Frame 1: `FIN=0, opcode=0x1` (text, not final)
- Frame 2..N-1: `FIN=0, opcode=0x0` (continuation)
- Frame N: `FIN=1, opcode=0x0` (continuation, final)

`ws.recv()` reassembles fragments internally — the caller always receives a complete message.

---

## 7. Internal Structure (`ws_runtime.h/cpp`)

```cpp
struct AimlWsConn {
    // Transport (one of the two is set):
    AimlSocket*    plain_sock;   // nullptr if TLS
    AimlTlsSocket* tls_sock;    // nullptr if plain

    // Frame state
    bool     is_client;         // Client: masking required
    bool     closed;
    uint8_t  mask_key[4];       // Masking key for next send
    int      max_frame_size;    // Default: 1 MB

    // Recv buffer (for fragment reassembly)
    uint8_t* frag_buf;
    int32_t  frag_len;
    int32_t  frag_cap;
    uint8_t  frag_opcode;       // Opcode of the first fragment
};
```

---

## 8. Open Design Questions

- **Per-message compression** (`permessage-deflate`): compression via `zlib` at frame level —
  significantly reduces bandwidth for repetitive JSON payloads.
- **Subprotocols**: `ws.connect(url, {protocol: "mqtt"})` → `Sec-WebSocket-Protocol` header.
- **Event loop integration**: the current blocking `ws.recv()` blocks the thread.
  For many parallel connections an `epoll`-based event loop would be more efficient —
  extension for a future `async` model.
- **Binary callback in server**: `srv.on_binary(fn)` — separate callbacks for text and binary
  or a shared callback with a type flag?
- **Close code enum**: status codes (1000=Normal, 1001=Going Away, 1008=Policy Violation, …)
  could be defined as an AIML enum.
