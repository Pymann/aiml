# Kapitel 32.4 — WebSockets

> **Leitgedanke:** WebSockets werden in AIML **ohne externe Bibliothek** implementiert —
> direkt auf Basis von `net.*` (WS) und `tls.*` (WSS). Das reduziert Abhängigkeiten und
> gibt vollständige Kontrolle über das Frame-Protokoll (RFC 6455). Der `ws_handle` ist
> ein opaker Pointer, der intern den TCP-Socket, den Frame-Parser-Zustand und einen
> Sende-Mutex (Thread-Safety) kapselt.

---

## 1. Typen

| Typ               | LLVM  | Beschreibung |
|-------------------|-------|--------------|
| `ws_handle`       | `i8*` | Client- oder Server-seitige WebSocket-Verbindung |
| `ws_server_handle`| `i8*` | WebSocket-Server (bindet Port, verwaltet Verbindungen) |

---

## 2. WebSocket-Client

### 2.1 Verbinden und Nachrichten senden

```aiml
# WS-Client (unverschlüsselt)
- decl: [ws_url, url]
- set: [ws_url, "ws://echo.websocket.org/"]
- decl: [ws, ws_handle]
- set: [ws, { call: [ws.connect, ws_url] }]

# Text senden
- call: [ws.send, "Hello WebSocket!"]

# Text empfangen (blockiert bis eine vollständige Nachricht ankommt)
- decl: [msg, string]
- set: [msg, { call: [ws.recv] }]
- print: ["Echo: ${msg}"]

- call: [ws.close]
```

### 2.2 WSS (verschlüsselt via TLS)

```aiml
# URL-Schema "wss://" → TLS-Transport automatisch
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

`ws.connect` erkennt `ws://` vs. `wss://` anhand des URL-Schemas und wählt
`net.*` bzw. `tls.*` als Transport.

### 2.3 Binäre Nachrichten

```aiml
- decl: [bin_url, url]
- set: [bin_url, "ws://localhost:9001/binary"]
- decl: [ws, ws_handle]
- set: [ws, { call: [ws.connect, bin_url] }]

# Binär senden (buffer)
- decl: [buf, buffer]
- call: [buf.alloc, 256]
- call: [buf.write_string, "\x00\x01\x02"]
- call: [ws.send_bytes, buf]

# Binär empfangen
- decl: [data, buffer]
- set: [data, { call: [ws.recv_bytes] }]
- decl: [sz, i32]
- set: [sz, { call: [data.size] }]
- print: ["Binary size: ${sz}"]

- call: [ws.close]
```

### 2.4 Ping / Pong

```aiml
- call: [ws.ping, "heartbeat"]   # Sendet Ping-Frame; Pong-Antwort verarbeitet ws.recv intern
- decl: [alive, bool]
- set: [alive, { call: [ws.is_open] }]
- print: ["Connected: ${alive}"]
```

`ws.recv` verarbeitet Pong-Frames transparent (nicht an Caller zurückgegeben).
Ping-Frames vom Peer werden automatisch mit Pong beantwortet.

### 2.5 `ws_handle`-Methoden

| Methode       | Argumente       | Rückgabe | Beschreibung |
|---------------|-----------------|----------|--------------|
| `send`        | `msg: string`   | `i32`    | Text-Frame senden (Opcode 0x1) |
| `send_bytes`  | `buf: buffer`   | `i32`    | Binary-Frame senden (Opcode 0x2) |
| `recv`        | —               | `string` | Nächste vollständige Text-Nachricht empfangen |
| `recv_bytes`  | —               | `buffer` | Nächste vollständige Binary-Nachricht empfangen |
| `ping`        | —               | `i32`    | Ping-Frame senden |
| `close`       | —               | `i32`    | Close-Frame senden + Connection schließen |
| `is_open`     | —               | `bool`   | `false` nach Close-Frame oder TCP-Fehler |
| `set_timeout` | `ms: i32`       | —        | Recv/Send-Timeout |
| `fd`          | —               | `i32`    | Raw TCP-File-Descriptor |

---

## 3. WebSocket-Server

### 3.1 Einfacher Echo-Server

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
      - call: [conn.send, msg]   # Echo zurück

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
- call: [srv.start]   # Blockiert
```

### 3.2 Broadcast-Server (alle Clients)

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
      # Broadcast: an alle verbundenen Clients senden
      - decl: [c, ws_handle]
      - for: [c, clients]
        body:
          - call: [c.send, msg]

- def:
    name: ws_on_close
    args: [[conn, ws_handle], [code, i32]]
    body:
      # TODO: conn aus clients-Liste entfernen (remove by value — Future Work)
      - print: ["Client left"]

- decl: [srv, ws_server_handle]
- set: [srv, { call: [ws.server, "0.0.0.0", 9002] }]
- call: [srv.on_connect, ws_on_connect]
- call: [srv.on_message, ws_on_message]
- call: [srv.on_close, ws_on_close]
- call: [srv.start]
```

### 3.3 WSS-Server (TLS)

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

### 3.4 `ws_server_handle`-Methoden

| Methode             | Argumente              | Rückgabe        | Beschreibung |
|---------------------|------------------------|-----------------|--------------|
| `on_connect`        | `handler: fn`          | —               | Callback bei neuer Verbindung; `fn(conn: ws_handle)` |
| `on_message`        | `handler: fn`          | —               | Callback bei Text-Nachricht; `fn(conn: ws_handle, msg: string)` |
| `on_binary`         | `handler: fn`          | —               | Callback bei Binary-Nachricht; `fn(conn: ws_handle, buf: buffer)` |
| `on_close`          | `handler: fn`          | —               | Callback bei Close; `fn(conn: ws_handle, code: i32)` |
| `on_error`          | `handler: fn`          | —               | Callback bei Fehler; `fn(conn: ws_handle, err: string)` |
| `start`             | —                      | —               | Blockierender Accept-Loop |
| `start_async`       | —                      | `thread_handle` | Nicht-blockierend |
| `stop`              | —                      | —               | Stoppt Accept-Loop |
| `set_max_frame_size`| `bytes: i32`           | —               | Max. Frame-Größe (Default: 1 MB) |

---

## 4. WebSocket-Upgrade aus HTTP-Server

Ein laufender HTTP-Server kann einzelne Verbindungen auf WebSocket upgraden:

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
      # Prüft Upgrade-Headers, führt Handshake durch, ruft ws_handler auf
      - call: [resp.upgrade_ws, ws_handler]

- decl: [srv, http_server_handle]
- set: [srv, { call: [http.server, "0.0.0.0", 8080] }]
- call: [srv.route, "GET",  "/",   serve_html]
- call: [srv.route, "GET",  "/ws", upgrade_endpoint]   # WS-Upgrade auf /ws
- call: [srv.start]
```

`ctx.upgrade_ws(handler_fn)` prüft, ob der Request ein gültiger WebSocket-Upgrade-Request ist
(`Upgrade: websocket`, `Connection: Upgrade`, `Sec-WebSocket-Key`), antwortet mit
`101 Switching Protocols` und übergibt dann einen `ws_handle` an den Handler.

---

## 5. Vollständige `ws`-Namespace-Referenz

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `{ call: [ws.connect, u] }` | `ws_handle` | Verbindet mit WS/WSS-URL (`u: url`); Schema bestimmt Transport |
| `{ call: [ws.server, host, port] }` | `ws_server_handle` | Plain-WS-Server |
| `{ call: [ws.server_tls, host, port, cert, key] }` | `ws_server_handle` | WSS-Server (`cert, key: path`) |

---

## 6. RFC 6455 — Frame-Format (Implementierungsreferenz)

WebSocket-Frames haben folgendes Wire-Format:

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
|              Payload Data (XOR mit Masking-key)               |
+---------------------------------------------------------------+
```

| Opcode | Wert | Bedeutung |
|--------|------|-----------|
| Continuation | 0x0 | Fortsetzung eines fragmentierten Frames |
| Text | 0x1 | UTF-8 Text-Payload |
| Binary | 0x2 | Binär-Payload |
| Close | 0x8 | Verbindung schließen (optional 2-Byte Status-Code) |
| Ping | 0x9 | Keepalive-Request |
| Pong | 0xA | Antwort auf Ping |

**Masking:**
- Client → Server: **Immer maskiert** (MASK=1, 4-Byte Masking-Key via CSPRNG)
- Server → Client: **Nie maskiert** (MASK=0)
- Masking: `payload[i] ^= masking_key[i % 4]`

**Payload-Länge:**
- 0–125: direkt im 7-Bit-Feld
- 126: nächste 2 Bytes als Big-Endian `uint16`
- 127: nächste 8 Bytes als Big-Endian `uint64`

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

Server antwortet:
```
HTTP/1.1 101 Switching Protocols\r\n
Upgrade: websocket\r\n
Connection: Upgrade\r\n
Sec-WebSocket-Accept: <SHA1(key + "258EAFA5-...")  Base64>\r\n
\r\n
```

`Sec-WebSocket-Accept` = `Base64(SHA1(Sec-WebSocket-Key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))`.
Diese Berechnung erfolgt in `ws_runtime.cpp` mit OpenSSL's `SHA1()` + `EVP_EncodeBlock()`.

### 6.2 Fragmentierung

Lange Nachrichten können über mehrere Frames fragmentiert werden:
- Frame 1: `FIN=0, opcode=0x1` (Text, nicht final)
- Frame 2..N-1: `FIN=0, opcode=0x0` (Continuation)
- Frame N: `FIN=1, opcode=0x0` (Continuation, final)

`ws.recv()` reassembliert Fragmente intern — Caller bekommt immer eine vollständige Nachricht.

---

## 7. Interne Struktur (`ws_runtime.h/cpp`)

```cpp
struct AimlWsConn {
    // Transport (eines von beiden ist gesetzt):
    AimlSocket*    plain_sock;   // nullptr wenn TLS
    AimlTlsSocket* tls_sock;    // nullptr wenn plain

    // Frame-Zustand
    bool     is_client;         // Client: Masking erforderlich
    bool     closed;
    uint8_t  mask_key[4];       // Masking-Key für nächsten Send
    int      max_frame_size;    // Default: 1 MB

    // Recv-Buffer (für Fragment-Reassemblierung)
    uint8_t* frag_buf;
    int32_t  frag_len;
    int32_t  frag_cap;
    uint8_t  frag_opcode;       // Opcode des ersten Fragments
};
```

---

## 8. Offene Designfragen

- **Per-Message Compression** (`permessage-deflate`): Komprimierung via `zlib` auf Frame-Ebene —
  reduziert Bandbreite bei repetitiven JSON-Payloads erheblich.
- **Subprotokolle**: `ws.connect(url, {protocol: "mqtt"})` → `Sec-WebSocket-Protocol`-Header.
- **Event-Loop-Integration**: Der aktuelle blockierende `ws.recv()` blockiert den Thread.
  Für viele parallele Verbindungen wäre ein `epoll`-basierter Event-Loop effizienter —
  Erweiterung für ein späteres `async`-Modell.
- **Binary-Callback im Server**: `srv.on_binary(fn)` — getrennte Callbacks für Text und Binary
  oder ein gemeinsamer Callback mit Typ-Flag?
- **Close-Code-Enum**: Status-Codes (1000=Normal, 1001=Going Away, 1008=Policy Violation, …)
  könnten als AIML-Enum definiert werden.
