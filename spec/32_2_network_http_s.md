# Kapitel 32.2 — HTTP/S: Client und Server

> **Leitgedanke:** HTTP/1.1 wird in AIML **ohne externe Bibliotheken** auf Basis von
> `net.*` (HTTP) und `tls.*` (HTTPS) implementiert. `http.get("https://example.com")`
> wählt den Transport automatisch anhand des URL-Schemas. Der HTTP-Server ist
> single-threaded per Default, aber für jeden eingehenden Client kann mit `spawn` ein
> eigener Thread erzeugt werden.

---

## 1. Typen

| Typ                    | LLVM  | Beschreibung |
|------------------------|-------|--------------|
| `http_request_handle`  | `i8*` | Builder für ausgehende Requests (Methode, URL, Header, Body) |
| `http_response_handle` | `i8*` | Antwort eines HTTP-Calls (Status, Header, Body) |
| `http_server_handle`   | `i8*` | Gebundener HTTP-Server (Routes, Handler) |
| `http_ctx_handle`      | `i8*` | Pro-Request-Kontext im Server-Handler (Request lesen + Response schreiben) |

---

## 2. HTTP-Client

### 2.1 Convenience-Funktionen (einzeilig)

```aiml
# GET
- decl: [resp, http_response_handle]
- set: [resp, { call: [http.get, "https://api.example.com/status"] }]
- print: ["Status: ${ call: [resp.status] }"]
- print: ["Body: ${ call: [resp.body] }"]

# POST mit JSON-Body
- decl: [resp2, http_response_handle]
- set: [resp2, { call: [http.post, "https://api.example.com/items",
                         "{\"name\": \"test\"}"] }]

# weitere Methoden
- set: [resp, { call: [http.put,    "https://...", "{...}"] }]
- set: [resp, { call: [http.delete, "https://..."] }]
- set: [resp, { call: [http.patch,  "https://...", "{...}"] }]
- set: [resp, { call: [http.head,   "https://..."] }]
```

`http.get`, `http.post`, etc. sind Convenience-Wrapper über `http_request_handle`.
Alle senden automatisch `Content-Type: application/octet-stream` (oder `text/plain`
für String-Bodies) sofern nicht explizit gesetzt.

### 2.2 Request-Builder (mit Headern, Timeout, etc.)

```aiml
- decl: [req, http_request_handle]
- set: [req, { call: [http.new_request, "POST", "https://api.example.com/v1/chat"] }]

- call: [req.set_header, "Content-Type",  "application/json"]
- call: [req.set_header, "Authorization", "Bearer sk-abc123"]
- call: [req.set_header, "Accept",        "application/json"]
- call: [req.set_body,   "{\"prompt\": \"Hello\"}"]
- call: [req.set_timeout, 10000]   # 10 Sekunden

- decl: [resp, http_response_handle]
- set: [resp, { call: [req.send] }]

- decl: [status, i32]
- set: [status, { call: [resp.status] }]
- print: ["HTTP ${status}"]

- decl: [ct, string]
- set: [ct, { call: [resp.header, "Content-Type"] }]
- print: ["Content-Type: ${ct}"]

- decl: [body, string]
- set: [body, { call: [resp.body] }]
- print: ["${body}"]
```

### 2.3 `http_request_handle`-Methoden

| Methode | Signatur | Beschreibung |
|---------|----------|--------------|
| `set_header` | `(name: string, value: string) → void` | Header hinzufügen/überschreiben |
| `set_body` | `(body: string) → void` | Request-Body (Text) |
| `set_body_bytes` | `(buf: buffer) → void` | Request-Body (binär) |
| `set_timeout` | `(ms: i32) → void` | Timeout für gesamten Request |
| `send` | `() → http_response_handle` | Sendet Request, gibt Antwort zurück |

### 2.4 `http_response_handle`-Methoden

| Methode | Signatur | Beschreibung |
|---------|----------|--------------|
| `status` | `() → i32` | HTTP-Status-Code (200, 404, …) |
| `header` | `(name: string) → string` | Wert eines Response-Headers (leer wenn fehlt) |
| `body` | `() → string` | Response-Body als String |
| `body_bytes` | `() → buffer` | Response-Body als Buffer (für Binärdaten) |
| `ok` | `() → bool` | `true` wenn `status >= 200 && status < 300` |

---

## 3. HTTP-Server

### 3.1 Minimales Beispiel

```aiml
- def:
    name: handle_root
    args: [[ctx, http_ctx_handle]]
    body:
      - call: [ctx.send, 200, "Hello from AIML!"]

- def:
    name: handle_echo
    args: [[ctx, http_ctx_handle]]
    body:
      - decl: [body, string]
      - set: [body, { call: [ctx.body] }]
      - call: [ctx.set_header, "Content-Type", "text/plain"]
      - call: [ctx.send, 200, body]

- decl: [srv, http_server_handle]
- set: [srv, { call: [http.server, "0.0.0.0", 8080] }]

- call: [srv.route, "GET",  "/",     handle_root]
- call: [srv.route, "POST", "/echo", handle_echo]

- print: ["Listening on :8080"]
- call: [srv.start]   # Blockiert (accept-loop im aktuellen Thread)
```

### 3.2 Nicht-blockierender Server mit `start_async`

```aiml
- decl: [srv, http_server_handle]
- set: [srv, { call: [http.server, "0.0.0.0", 8080] }]
- call: [srv.route, "GET", "/ping", ping_handler]

- decl: [srv_thread, thread_handle]
- set: [srv_thread, { call: [srv.start_async] }]

# ... anderer Code läuft parallel ...
- call: [time.sleep_ms, 5000]
- call: [srv.stop]         # Signalisiert Server, Accept-Loop zu beenden
- call: [srv_thread.join]
```

### 3.3 Server mit einem Thread pro Client

```aiml
- def:
    name: client_handler
    args: [[ctx, http_ctx_handle]]
    body:
      - decl: [path, string]
      - set: [path, { call: [ctx.path] }]
      - call: [ctx.send, 200, "You requested: ${path}"]

- def:
    name: dispatch
    args: [[ctx, http_ctx_handle]]
    body:
      # Aus dem Dispatch-Handler heraus einen Thread spawnen
      - decl: [t, thread_handle]
      - set: [t, { spawn: [client_handler, ctx] }]
      - call: [t.detach]   # Läuft unabhängig weiter

- decl: [srv, http_server_handle]
- set: [srv, { call: [http.server, "0.0.0.0", 8080] }]
- call: [srv.route, "GET", "/*", dispatch]   # Wildcard-Route
- call: [srv.start]
```

### 3.4 `http_server_handle`-Methoden

| Methode | Signatur | Beschreibung |
|---------|----------|--------------|
| `route` | `(method: string, path: string, handler: fn) → void` | Route registrieren |
| `start` | `() → void` | Blockierender Accept-Loop |
| `start_async` | `() → thread_handle` | Nicht-blockierend (startet Thread) |
| `stop` | `() → void` | Signalisiert Start-Loop zu stoppen |
| `set_header` | `(name: string, val: string) → void` | Globaler Default-Header für alle Antworten |

**Routing-Regeln:**
- Exakte Pfade: `"/api/v1/users"` — matcht nur genau diesen Pfad.
- Wildcard-Suffix: `"/static/*"` — matcht alle Pfade mit diesem Prefix.
- Methode `"*"` matcht alle HTTP-Methoden auf dem Pfad.
- Reihenfolge: Exakte Pfade haben Vorrang vor Wildcards.

### 3.5 `http_ctx_handle`-Methoden (im Handler)

| Methode | Signatur | Beschreibung |
|---------|----------|--------------|
| `method` | `() → string` | "GET", "POST", … |
| `path` | `() → string` | Pfad-Teil der URL, z.B. "/api/users" |
| `query` | `(name: string) → string` | URL-Query-Parameter; leer wenn fehlt |
| `header` | `(name: string) → string` | Request-Header-Wert; leer wenn fehlt |
| `body` | `() → string` | Request-Body als String |
| `body_bytes` | `() → buffer` | Request-Body als Buffer |
| `send` | `(status: i32, body: string) → void` | Response senden (Text) |
| `send_bytes` | `(status: i32, buf: buffer) → void` | Response senden (binär) |
| `send_json` | `(status: i32, json: string) → void` | Setzt Content-Type: application/json automatisch |
| `set_header` | `(name: string, val: string) → void` | Response-Header setzen |
| `set_status` | `(code: i32) → void` | Status-Code ohne sofortiges Senden |

---

## 4. HTTPS-Server

HTTPS-Server kombiniert `tls.*` für Transport und `http.*` für Protokoll.
Das AIML-Programm übergibt dem Server einen `tls_socket_handle` als Basis:

```aiml
- decl: [srv, http_server_handle]
- set: [srv, { call: [http.server_tls, "0.0.0.0", 8443,
                       "/etc/myapp/server.crt",
                       "/etc/myapp/server.key"] }]

- call: [srv.route, "GET", "/", handle_root]
- call: [srv.start]
```

`http.server_tls(host, port, cert_path, key_path)` ist äquivalent zu:
1. Intern `tls.socket()` + `tls.set_cert()` + `tls.bind()` + `tls.listen()`
2. Im Accept-Loop `tls.accept()` statt `net.accept()`
3. Danach gleicher HTTP/1.1-Parser wie beim Plain-HTTP-Server.

---

## 5. Vollständige `http`-Namespace-Referenz

| Funktion | Signatur | Beschreibung |
|----------|----------|--------------|
| `http.get` | `(url: string) → http_response_handle` | GET-Request |
| `http.post` | `(url: string, body: string) → http_response_handle` | POST-Request |
| `http.put` | `(url: string, body: string) → http_response_handle` | PUT-Request |
| `http.delete` | `(url: string) → http_response_handle` | DELETE-Request |
| `http.patch` | `(url: string, body: string) → http_response_handle` | PATCH-Request |
| `http.head` | `(url: string) → http_response_handle` | HEAD-Request |
| `http.new_request` | `(method: string, url: string) → http_request_handle` | Builder |
| `http.server` | `(host: string, port: i32) → http_server_handle` | Plain-HTTP-Server |
| `http.server_tls` | `(host, port, cert, key) → http_server_handle` | HTTPS-Server |

---

## 6. Implementierungshinweise

### 6.1 URL-Parsing

`http.get("https://api.example.com:8443/v1/status?token=abc")` zerlegt die URL intern:

| Komponente | Ergebnis |
|------------|---------|
| Schema | `https` → TLS-Transport |
| Host | `api.example.com` (SNI) |
| Port | `8443` (Default: 80 für http, 443 für https) |
| Pfad | `/v1/status` |
| Query | `token=abc` |

### 6.2 HTTP/1.1-Parser

Implementiert in `http_runtime.cpp`:
- Request-Line parsen: `GET /path HTTP/1.1\r\n`
- Header parsen: `Name: Value\r\n` bis `\r\n\r\n`
- Body: `Content-Length`-basiert oder bis Connection-Close
- Kein Chunked-Transfer-Encoding in v1 (Future Work)

### 6.3 Keine Abhängigkeit zu libcurl

Alle HTTP-Operationen laufen über `net.*`/`tls.*` — kein libcurl, kein libuv.
Vorteil: kontrollierte Abhängigkeiten, kleinere Binary.
Nachteil: kein HTTP/2, kein Proxy-Support (Future Work).

### 6.4 Keep-Alive

HTTP/1.1 Keep-Alive ist **nicht** standardmäßig aktiv in v1 — jeder Request
öffnet und schließt eine neue TCP-Verbindung. Für persistente Verbindungen
(z.B. REST-API-Clients mit vielen Requests) → `rest_client_handle` (Kapitel 32.3)
pooled Verbindungen automatisch.

---

## 7. Offene Designfragen

- **Chunked Transfer Encoding**: Für Streaming-Responses notwendig.
- **HTTP/2**: Requires ALPN + binary framing — langfristige Erweiterung.
- **Compression**: `Accept-Encoding: gzip` — via `zlib` in `http_runtime.cpp`.
- **Static File Serving**: `srv.serve_static("/", "/var/www")` — automatisches
  Content-Type-Mapping, Range-Requests.
- **Middleware**: `srv.use(middleware_fn)` — wird vor jedem Handler aufgerufen.
- **WebSocket-Upgrade**: `ctx.upgrade_ws(...)` — definiert in Kapitel 32.4.
