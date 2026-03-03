# Kapitel 32.2 — HTTP/S: Client und Server

> **Leitgedanke:** HTTP/1.1 wird in AIML **ohne externe Bibliotheken** auf Basis von
> `net.*` (HTTP) und `tls.*` (HTTPS) implementiert. Der Transport wird automatisch
> anhand des URL-Schemas gewählt. Der HTTP-Server ist single-threaded per Default,
> aber für jeden eingehenden Client kann mit `spawn` ein eigener Thread erzeugt werden.

---

## 1. Typen

| Typ                    | LLVM  | Beschreibung |
|------------------------|-------|--------------|
| `url`                  | `i8*` | Geparste, validierte URL (Scheme, Host, Port, Path, Query, Fragment) |
| `http_session_handle`  | `i8*` | Persistente Client-Session (Cookie-Jar, Keep-Alive, gemeinsame Headers/Auth) |
| `http_client_handle`   | `i8*` | Builder für einen einzelnen ausgehenden Request (Methode, URL, Header, Body) |
| `http_request_handle`  | `i8*` | Eingehender Server-Request (URL, Methode, Header, Body, Cookies, Pfad-Params) |
| `http_response_handle` | `i8*` | Server-Response-Writer **und** empfangene Client-Antwort |
| `http_server_handle`   | `i8*` | Gebundener HTTP-Server (Routes, Middleware, Handler) |

Handler-Signatur: immer zwei Parameter — Request und Response:

```aiml
- def:
    name: handle_root
    args: [[req, http_request_handle], [resp, http_response_handle]]
    body:
      - set: [resp.status, 200]
      - set: [resp.body, "Hello from AIML!"]
      - call: [resp.send]
```

---

## 1.1 Der `url`-Typ

`url` ist ein eigenständiger Typ — inspiriert von Pydantic's `HttpUrl` — der eine
vollständig geparste und validierte URL repräsentiert. Er wird automatisch beim
Empfang eines HTTP-Requests als Member von `http_request_handle` gesetzt und kann
auch eigenständig aus einem String erzeugt werden.

### Erzeugung

```aiml
# Aus String parsen
- decl: [u, url]
- set: [u, { call: [url.parse, "https://api.example.com:8443/v1/users?page=2&limit=10#top"] }]

# Direkte Zuweisung (auto-parse, wie der path-Typ)
- decl: [u, url]
- set: [u, "https://api.example.com/v1/users"]
```

### Member und Methoden des `url`-Typs

**Member** (direkt lesbar):

| Member       | Typ                  | Beschreibung |
|--------------|----------------------|--------------|
| `protocol`   | `string`             | Protokoll, z.B. `"https"`, `"ftp"`, `"ws"` — null wenn nicht gesetzt |
| `host`       | `string`             | Hostname, z.B. `"api.example.com"` — null wenn fehlt |
| `path`       | `string`             | Pfad-Anteil, z.B. `"/v1/users"` — null wenn fehlt |
| `fragment`   | `string`             | Fragment nach `#` — null wenn keiner |
| `port`       | `i32`                | Port; `0` = Protokoll-Default |
| `get_param`  | `map<string:string>` | Alle Query-Parameter; null wenn keine vorhanden |
| `credential` | `url_credential`     | Eingebettete Credentials (`user:pass@host`); null wenn keine |

`url_credential` hat zwei Member:

| Member | Typ      | Beschreibung |
|--------|----------|--------------|
| `user` | `string` | Benutzername; null wenn fehlt |
| `pass` | `string` | Passwort; null wenn fehlt |

**Methoden**:

| Methode     | Argumente | Rückgabe | Beschreibung |
|-------------|-----------|----------|--------------|
| `to_string` | —         | `string` | Vollständige URL als String |
| `is_https`  | —         | `bool`   | `true` wenn Protokoll `https` |

### Beispiel

```aiml
- decl: [u, url]
- set: [u, "https://api.example.com/v1/users?page=2&limit=10"]

# Member direkt lesen
- print: ["host: ${u.host}"]
- print: ["path: ${u.path}"]

# Query-Parameter: get_param ist eine map<string:string>
- decl: [pg, string]
- set: [pg, { call: [u.get_param.get, "page"] }]
- print: ["page: ${pg}"]

- decl: [limit, string]
- set: [limit, { call: [u.get_param.get, "limit"] }]
- print: ["limit: ${limit}"]

# Null-Check für optionale Felder
- if: [{ ==: [u.fragment, null] }, true]
  then:
    - print: ["kein Fragment"]
```

---

## 2. HTTP-Client

### 2.1 Convenience-Funktionen

```aiml
# GET
- decl: [status_url, url]
- set: [status_url, "https://api.example.com/status"]
- decl: [resp, http_response_handle]
- set: [resp, { call: [http.get, status_url] }]

- print: ["Status: ${resp.status}"]
- print: ["Body: ${resp.body}"]

# POST mit JSON-Body
- decl: [items_url, url]
- set: [items_url, "https://api.example.com/items"]
- decl: [resp2, http_response_handle]
- set: [resp2, { call: [http.post, items_url, "{\"name\": \"test\"}"] }]

# weitere Methoden
- decl: [item1_url, url]
- set: [item1_url, "https://api.example.com/items/1"]
- set: [resp, { call: [http.put,    item1_url, "{\"name\": \"neu\"}"] }]
- set: [resp, { call: [http.delete, item1_url] }]
- set: [resp, { call: [http.patch,  item1_url, "{\"x\": 1}"] }]
- set: [resp, { call: [http.head,   status_url] }]
```

### 2.2 Request-Builder (mit Headern, Timeout, etc.)

```aiml
- decl: [chat_url, url]
- set: [chat_url, "https://api.example.com/v1/chat"]
- decl: [client, http_client_handle]
- set: [client, { call: [http.new_request, "POST", chat_url] }]

- call: [client.headers.put, "Content-Type",  "application/json"]
- call: [client.headers.put, "Authorization", "Bearer sk-abc123"]
- set: [client.body, "{\"prompt\": \"Hello\"}"]
- set: [client.timeout, 10000]

- decl: [resp, http_response_handle]
- set: [resp, { call: [client.send] }]

- print: ["HTTP ${resp.status}"]
- decl: [ct, string]
- set: [ct, { call: [resp.headers.get, "Content-Type"] }]
- print: ["Content-Type: ${ct}"]
- print: ["${resp.body}"]
- print: ["ok: ${resp.ok}"]
```

### 2.3 `http_client_handle` — Member und Methoden

**Member** (direkt setzbar via `set`):

| Member       | Typ                  | Beschreibung |
|--------------|----------------------|--------------|
| `headers`    | `map<string:string>` | Request-Headers; bringt `put`, `get`, `remove` etc. mit |
| `body`       | `string`             | Request-Body (Text) |
| `body_bytes` | `buffer<u8>`             | Request-Body (binär) |
| `timeout`    | `i32`                | Timeout in ms (0 = kein Timeout) |
| `stream`     | `bool`               | Streaming-Modus: Response wird nicht gepuffert; `pipe`/`save_to` verwenden |

**Methode**:

| Methode | Argumente | Rückgabe               | Beschreibung |
|---------|-----------|------------------------|--------------|
| `send`  | —         | `http_response_handle` | Sendet Request, gibt Antwort zurück |

### 2.4 `http_response_handle` — Member und Methoden (Client — Lesen)

**Member** (direkt lesbar nach dem Request):

| Member       | Typ                  | Beschreibung |
|--------------|----------------------|--------------|
| `status`     | `i32`                | HTTP-Status-Code (200, 404, …) |
| `ok`         | `bool`               | `true` wenn Status 2xx |
| `headers`    | `map<string:string>` | Response-Headers; immer initialisiert (nie null) |
| `body`       | `string`             | Response-Body vollständig gepuffert; null bei Streaming |
| `body_bytes` | `buffer<u8>`             | Response-Body als Buffer; null bei Streaming |

**Methoden** (Streaming — kein In-Memory-Puffer):

| Methode   | Argumente  | Beschreibung |
|-----------|------------|--------------|
| `save_to` | `p: path`  | Streamt Body direkt in Datei — kein Heap-Puffer (`splice`/write-loop) |
| `pipe`    | `fn`       | Ruft `fn(chunk: buffer<u8>)` für jeden empfangenen Chunk auf |

```aiml
# Datei ohne Zwischenspeicher empfangen
- decl: [out, path]
- set: [out, "/var/data/video.mp4"]
- call: [resp.save_to, out]

# Custom: Chunk-by-Chunk in DB schreiben
- def:
    name: on_chunk
    args: [[chunk, buffer<u8>]]
    body:
      - call: [sensor_store.write_raw, chunk]

- call: [resp.pipe, on_chunk]
```

> `body` / `body_bytes` und `save_to` / `pipe` schließen sich gegenseitig aus —
> bei Streaming-Requests (`set: [client.stream, true]`) sind `body` und `body_bytes` null.

---

## 2.5 Session — `http_session_handle`

Analog zu `requests.Session` in Python: persistente Headers, Cookie-Jar und
TCP-Verbindungswiederverwendung (Keep-Alive) über mehrere Requests hinweg.

### Erzeugung

```aiml
- decl: [s, http_session_handle]
- set: [s, { call: [http.session] }]
```

### Typisches Beispiel: Login + geschützte API

```aiml
- decl: [s, http_session_handle]
- set: [s, { call: [http.session] }]

- call: [s.headers.put, "Accept", "application/json"]
- set: [s.timeout, 30000]

# Login — Set-Cookie vom Server wird automatisch im Cookie-Jar gespeichert
- decl: [login_url, url]
- set: [login_url, "https://app.example.com/login"]
- decl: [resp, http_response_handle]
- set: [resp, { call: [s.post, login_url, "{\"user\":\"alice\",\"pass\":\"secret\"}"] }]

# Folgerequest — Session-Cookie wird automatisch mitgeschickt
- decl: [data_url, url]
- set: [data_url, "https://app.example.com/api/data"]
- set: [resp, { call: [s.get, data_url] }]

- print: ["${resp.body}"]

- call: [s.close]
```

### `http_session_handle` — Member und Methoden

**Member** (direkt setzbar via `set`):

| Member    | Typ                  | Beschreibung |
|-----------|----------------------|--------------|
| `headers` | `map<string:string>` | Persistente Headers für alle Requests der Session |
| `timeout` | `i32`                | Default-Timeout in ms (0 = kein Timeout) |
| `cookies` | `map<string:string>` | Cookie-Jar — manuell les- und schreibbar |

**Methoden**:

| Methode       | Argumente                      | Rückgabe               | Beschreibung |
|---------------|--------------------------------|------------------------|--------------|
| `set_auth_bearer` | `token: string`            | —                      | Setzt `Authorization: Bearer <token>` für alle Requests |
| `set_auth_basic`  | `user: string, pass: string` | —                    | Setzt `Authorization: Basic <b64>` für alle Requests |
| `clear_cookies`   | —                          | —                      | Cookie-Jar leeren |
| `get`         | `u: url`                       | `http_response_handle` | GET mit Session-Kontext |
| `post`        | `u: url, body: string`         | `http_response_handle` | POST mit Session-Kontext |
| `put`         | `u: url, body: string`         | `http_response_handle` | PUT mit Session-Kontext |
| `delete`      | `u: url`                       | `http_response_handle` | DELETE mit Session-Kontext |
| `patch`       | `u: url, body: string`         | `http_response_handle` | PATCH mit Session-Kontext |
| `head`        | `u: url`                       | `http_response_handle` | HEAD mit Session-Kontext |
| `request`     | `method: string, u: url`       | `http_client_handle`   | Builder für komplexe Requests mit Session-Kontext |
| `close`       | —                              | —                      | Pooled Connections schließen, Session freigeben |

### Cookie-Jar-Verhalten

- Empfängt ein Response einen `Set-Cookie`-Header, wird der Cookie automatisch in `cookies` gespeichert
- Folgende Requests an dieselbe Domain senden alle passenden Cookies automatisch mit
- Domain-Matching, Path-Matching und Expiry werden berücksichtigt (HTTP Cookie-Spec)
- `cookies.put(name, val)` ermöglicht das manuelle Vorbelegen des Jars (z.B. nach Token-Refresh)

### Verhältnis zu `rest_client_handle`

`rest_client_handle` (Kapitel 32.3) baut intern auf `http_session_handle` auf und
ergänzt: Base-URL, automatisches `Content-Type: application/json` und relative Pfade.

```
http_session_handle   ← Cookie-Jar, Keep-Alive, persistente Headers
        ↑
rest_client_handle    ← + Base-URL + auto JSON-Headers + relative Pfade
```

---

## 3. HTTP-Server

### 3.1 Minimales Beispiel

```aiml
- def:
    name: handle_root
    args: [[req, http_request_handle], [resp, http_response_handle]]
    body:
      - set: [resp.status, 200]
      - set: [resp.body, "Hello from AIML!"]
      - call: [resp.send]

- def:
    name: handle_echo
    args: [[req, http_request_handle], [resp, http_response_handle]]
    body:
      - decl: [body, string]
      - set: [body, req.body]
      - call: [resp.headers.put, "Content-Type", "text/plain"]
      - set: [resp.status, 200]
      - set: [resp.body, body]
      - call: [resp.send]

- decl: [srv, http_server_handle]
- set: [srv, { call: [http.server, "0.0.0.0", 8080] }]

- call: [srv.route, "GET",  "/",     handle_root]
- call: [srv.route, "POST", "/echo", handle_echo]

- print: ["Listening on :8080"]
- call: [srv.start]
```

### 3.2 Nicht-blockierender Server mit `start_async`

```aiml
- decl: [srv, http_server_handle]
- set: [srv, { call: [http.server, "0.0.0.0", 8080] }]
- call: [srv.route, "GET", "/ping", ping_handler]

- decl: [srv_thread, thread_handle]
- set: [srv_thread, { call: [srv.start_async] }]

- call: [time.sleep_ms, 5000]
- call: [srv.stop]
- call: [srv_thread.join]
```

### 3.3 Server mit einem Thread pro Client

```aiml
- def:
    name: client_handler
    args: [[req, http_request_handle], [resp, http_response_handle]]
    body:
      - decl: [p, string]
      - set: [p, req.path]
      - set: [resp.status, 200]
      - set: [resp.body, "You requested: ${p}"]
      - call: [resp.send]

- def:
    name: dispatch
    args: [[req, http_request_handle], [resp, http_response_handle]]
    body:
      - decl: [t, thread_handle]
      - set: [t, { spawn: [client_handler, req, resp] }]
      - call: [t.detach]

- decl: [srv, http_server_handle]
- set: [srv, { call: [http.server, "0.0.0.0", 8080] }]
- call: [srv.route, "GET", "/*", dispatch]
- call: [srv.start]
```

### 3.4 `http_server_handle`-Methoden

| Methode       | Argumente                                              | Rückgabe            | Beschreibung |
|---------------|--------------------------------------------------------|---------------------|--------------|
| `route`       | `method: string, path: string, handler: fn`            | —                   | Route registrieren |
| `use`         | `middleware: fn`                                       | —                   | Globale Middleware (vor jedem Handler) |
| `group`       | `prefix: string, middleware: fn`                       | `http_route_handle` | Gruppe mit Prefix + Guard |
| `filesystem`  | `prefix: string, dirs: list<path>, opts: fs_options`   | —                   | Statische Dateien unter `prefix` ausliefern |
| `start`       | —                                                      | —                   | Blockierender Accept-Loop |
| `start_async` | —                                                      | `thread_handle`     | Nicht-blockierend (startet Thread) |
| `stop`        | —                                                      | —                   | Signalisiert Accept-Loop zu stoppen |
| `set_header`  | `name: string, val: string`                            | —                   | Globaler Default-Header für alle Antworten |

**Routing-Regeln:**
- Exakte Pfade: `"/api/v1/users"` — matcht nur genau diesen Pfad
- Benannte Segmente: `"/users/:id"` — matcht ein Segment, Name abrufbar via `req.params`
- Wildcard-Suffix: `"/static/*"` — matcht alle Pfade mit diesem Prefix
- Methode `"*"` matcht alle HTTP-Methoden auf dem Pfad
- Vorrang: exakt > benannt > Wildcard; bei gleichem Typ: längeres Muster zuerst

### 3.4a `http_route_handle`-Methoden

`http_route_handle` wird von `srv.group` zurückgegeben. Er hält intern eine Referenz auf den
Root-`http_server_handle` und kombiniert alle Pfade automatisch mit dem Prefix.
Kein eigener Listener — `start`/`stop`/`start_async` sind nicht vorhanden.

| Methode      | Argumente                                            | Rückgabe            | Beschreibung |
|--------------|------------------------------------------------------|---------------------|--------------|
| `route`      | `method: string, path: string, handler: fn`          | —                   | Route unter Prefix registrieren |
| `use`        | `middleware: fn`                                     | —                   | Middleware für diese Gruppe |
| `group`      | `prefix: string, middleware: fn`                     | `http_route_handle` | Verschachtelte Untergruppe |
| `filesystem` | `prefix: string, dirs: list<path>, opts: fs_options` | —                   | Statische Dateien unter Prefix |

### 3.5 `http_request_handle` — Member und Methoden (Server — Lesen)

**Member** (direkt lesbar):

| Member       | Typ                  | Beschreibung |
|--------------|----------------------|--------------|
| `url`        | `url`                | Vollständig geparste Request-URL |
| `params`     | `map<string:string>` | Benannte Pfadsegmente der gematchten Route |
| `method`     | `string`             | `"GET"`, `"POST"`, … |
| `path`       | `string`             | Pfad-Anteil der URL, z.B. `"/api/users"` |
| `body`       | `string`             | Request-Body als String |
| `body_bytes` | `buffer<u8>`             | Request-Body als Buffer |

**Methoden** (mit Argument):

| Methode  | Argumente      | Rückgabe | Beschreibung |
|----------|----------------|----------|--------------|
| `header` | `name: string` | `string` | Request-Header-Wert; null wenn fehlt |
| `cookie` | `name: string` | `string` | Request-Cookie lesen; null wenn nicht vorhanden |

### 3.6 `http_response_handle` — Member und Methoden (Server — Schreiben)

**Member** (direkt setzbar via `set`):

| Member       | Typ                  | Beschreibung |
|--------------|----------------------|--------------|
| `status`     | `i32`                | HTTP-Status-Code (default: 200) |
| `headers`    | `map<string:string>` | Response-Headers; bringt `put`, `get`, `remove` etc. mit |
| `body`       | `string`             | Response-Body als Text |
| `body_bytes` | `buffer<u8>`             | Response-Body als Bytes |
| `body_file`  | `path`               | Datei als Body — wird bei `send` gestreamt (Range-Request-aware) |

**Methoden** (5):

| Methode       | Argumente       | Beschreibung |
|---------------|-----------------|--------------|
| `send`        | —               | Sendet vollständige Response (Status + Headers + Body); setzt `resp_sent` |
| `write`       | `chunk: string` | Erster Aufruf: flusht Headers, startet Chunked-Transfer; folgende: schreibt Chunk |
| `write_bytes` | `data: buffer<u8>`  | Wie `write`, aber ohne Userspace-Copy — übergibt `buf.ptr` direkt an `send()` |
| `flush`       | —               | Flusht Puffer (SSE keepalive) |
| `close`       | —               | Terminal-Chunk, beendet Chunked-Transfer; setzt `resp_sent` |

> **Hinweis:** `resp_sent` wird durch `send`, den ersten `write`-Aufruf und `close` gesetzt.
> Middleware kann damit den Handler überspringen, ohne einen Rückgabewert zu benötigen.

**Cookies und Redirects** werden über `headers` gesetzt:

```aiml
# Cookie setzen
- call: [resp.headers.put, "Set-Cookie", "session=abc123; Path=/; HttpOnly; Max-Age=3600"]

# Redirect
- decl: [loc, string]
- set: [loc, { call: [login_url.to_string] }]
- call: [resp.headers.put, "Location", loc]
- set: [resp.status, 302]
- call: [resp.send]
```

### 3.7 Benannte Pfadsegmente

Segmente mit `:name` im Route-Pattern werden beim Match in `req.params` eingetragen.

```aiml
- call: [srv.route, "GET",  "/users/:id",                  handle_user]
- call: [srv.route, "GET",  "/users/:id/orders/:order_id", handle_order]
- call: [srv.route, "POST", "/orgs/:org/repos",             handle_repos]
- call: [srv.route, "GET",  "/static/*",                   handle_static]
```

Im Handler:

```aiml
- def:
    name: handle_order
    args: [[req, http_request_handle], [resp, http_response_handle]]
    body:
      # Benannte Pfadsegmente
      - decl: [p, map<string:string>]
      - set: [p, req.params]

      - decl: [uid, string]
      - set: [uid, { call: [p.get, "id"] }]

      - decl: [oid, string]
      - set: [oid, { call: [p.get, "order_id"] }]

      # Query-Parameter über req.url
      - decl: [u, url]
      - set: [u, req.url]

      - decl: [page, string]
      - set: [page, { call: [u.get_param.get, "page"] }]

      - call: [resp.headers.put, "Content-Type", "application/json"]
      - set: [resp.status, 200]
      - set: [resp.body, "{\"user\": \"${uid}\", \"order\": \"${oid}\", \"page\": \"${page}\"}"]
      - call: [resp.send]
```

**Matching-Tabelle:**

| Route-Pattern                      | Eingehender Pfad          | `params`-Inhalt                         |
|------------------------------------|---------------------------|-----------------------------------------|
| `/users/:id`                       | `/users/42`               | `id` → `"42"`                           |
| `/users/:id/orders/:order_id`      | `/users/7/orders/99`      | `id` → `"7"`, `order_id` → `"99"`      |
| `/orgs/:org/repos`                 | `/orgs/acme/repos`        | `org` → `"acme"`                        |
| `/static/*`                        | `/static/css/main.css`    | *(leer, kein benanntes Segment)*        |

### 3.8 Middleware und Absicherung

Middleware-Funktionen haben dieselbe Signatur wie Handler:
`args: [[req, http_request_handle], [resp, http_response_handle]]`.

Sobald eine Middleware `resp.send()`, den ersten `resp.write()` oder `resp.close()` aufruft,
wird intern `resp_sent = true` gesetzt — der eigentliche Handler wird übersprungen.

#### `srv.use(fn)` — Globale Middleware

```aiml
- def:
    name: log_middleware
    args: [[req, http_request_handle], [resp, http_response_handle]]
    body:
      - decl: [m, string]
      - set: [m, req.method]
      - decl: [u, url]
      - set: [u, req.url]
      - decl: [p, string]
      - set: [p, u.path]
      - print: ["${m} ${p}"]

- call: [srv.use, log_middleware]
```

#### `srv.group(prefix, middleware_fn)` — Geschützte Route-Gruppe

```aiml
- def:
    name: require_auth
    args: [[req, http_request_handle], [resp, http_response_handle]]
    body:
      - decl: [session, string]
      - set: [session, { call: [req.cookie, "session_token"] }]
      - if: [{ ==: [session, null] }, true]
        then:
          - decl: [login, url]
          - set: [login, "/auth/login"]
          - decl: [loc, string]
          - set: [loc, { call: [login.to_string] }]
          - call: [resp.headers.put, "Location", loc]
          - set: [resp.status, 302]
          - call: [resp.send]

- decl: [api, http_route_handle]
- set: [api, { call: [srv.group, "/api/v1", require_auth] }]

- call: [api.route, "GET",  "/users",     handle_list_users]
- call: [api.route, "POST", "/users",     handle_create_user]
- call: [api.route, "GET",  "/users/:id", handle_get_user]
```

Öffentliche Routen direkt am `srv` (kein Guard):

```aiml
- call: [srv.route, "GET",  "/health",         handle_health]
- call: [srv.route, "POST", "/auth/login",      handle_login]
- call: [srv.route, "GET",  "/auth/callback",   handle_callback]
```

#### Beispiel: Google OAuth 2.0

```aiml
# Schritt 1 — Weiterleitung zu Google
- def:
    name: handle_login
    args: [[req, http_request_handle], [resp, http_response_handle]]
    body:
      - decl: [auth_url, url]
      - set: [auth_url, "https://accounts.google.com/o/oauth2/auth?client_id=CLIENT_ID&redirect_uri=https://myapp.com/auth/callback&scope=openid%20email&response_type=code"]
      - decl: [loc, string]
      - set: [loc, { call: [auth_url.to_string] }]
      - call: [resp.headers.put, "Location", loc]
      - set: [resp.status, 302]
      - call: [resp.send]

# Schritt 2 — Callback: Code gegen Token tauschen
- def:
    name: handle_callback
    args: [[req, http_request_handle], [resp, http_response_handle]]
    body:
      - decl: [u, url]
      - set: [u, req.url]
      - decl: [code, string]
      - set: [code, { call: [u.get_param.get, "code"] }]

      - decl: [token_url, url]
      - set: [token_url, "https://oauth2.googleapis.com/token"]
      - decl: [client, http_client_handle]
      - set: [client, { call: [http.new_request, "POST", token_url] }]
      - call: [client.headers.put, "Content-Type", "application/x-www-form-urlencoded"]
      - set: [client.body, "code=${code}&client_id=CLIENT_ID&client_secret=SECRET&redirect_uri=https://myapp.com/auth/callback&grant_type=authorization_code"]

      - decl: [token_resp, http_response_handle]
      - set: [token_resp, { call: [client.send] }]
      - decl: [token_json, string]
      - set: [token_json, token_resp.body]

      # Session-Cookie setzen, zurück zur App
      - call: [resp.headers.put, "Set-Cookie", "session_token=${token_json}; Path=/; HttpOnly; Secure; SameSite=Strict; Max-Age=3600"]
      - decl: [home, url]
      - set: [home, "/dashboard"]
      - decl: [home_loc, string]
      - set: [home_loc, { call: [home.to_string] }]
      - call: [resp.headers.put, "Location", home_loc]
      - set: [resp.status, 302]
      - call: [resp.send]
```

### 3.9 Statische Dateien — `srv.filesystem`

`srv.filesystem` liefert Dateien aus einem oder mehreren Verzeichnissen aus.
Die Verzeichnisse werden in Reihenfolge durchsucht — erstes Match gewinnt.
Die Datei-Auflösung (`<dir> + request_path`) erfolgt dynamisch pro Request;
die `list<path>` wird einmalig beim Start übergeben.

#### `fs_options` — Member

| Member          | Typ      | Default         | Beschreibung |
|-----------------|----------|-----------------|--------------|
| `index`         | `path`   | `"index.html"`  | Datei, die bei Directory-Requests ausgeliefert wird |
| `spa`           | `bool`   | `false`         | Alle unbekannten Pfade → `index`-Datei (Single-Page-App) |
| `cache_max_age` | `i32`    | `0`             | `Cache-Control: max-age=N` in Sekunden; 0 = kein Cache-Header |

#### Beispiel: SPA + Assets

```aiml
- decl: [dirs, list<path>]
- call: [dirs.push, "/var/www/dist"]

- decl: [opts, fs_options]
- set: [opts.spa, true]
- set: [opts.cache_max_age, 3600]

- call: [srv.filesystem, "/", dirs, opts]
- call: [srv.start]
```

#### Beispiel: Mehrere Verzeichnisse (Fallback-Chain)

```aiml
- decl: [dirs, list<path>]
- call: [dirs.push, "/var/www/theme"]     # zuerst Theme-Overrides
- call: [dirs.push, "/var/www/default"]   # dann Defaults

- decl: [opts, fs_options]

- call: [srv.filesystem, "/static", dirs, opts]
```

#### Beispiel: Geschützte Downloads via `srv.group`

```aiml
- decl: [secure_dirs, list<path>]
- call: [secure_dirs.push, "/data/downloads"]

- decl: [opts, fs_options]

- decl: [secure, http_route_handle]
- set: [secure, { call: [srv.group, "/downloads", require_auth] }]
- call: [secure.filesystem, "/", secure_dirs, opts]
```

> **Hinweis:** `filesystem` kombiniert sich mit `srv.group` für Middleware-Absicherung.
> Range-Requests (`Content-Range`) und `ETag`/`Last-Modified` werden automatisch behandelt.

---

## 4. HTTPS-Server

```aiml
- decl: [cert, path]
- set: [cert, "/etc/myapp/server.crt"]
- decl: [key, path]
- set: [key, "/etc/myapp/server.key"]

- decl: [srv, http_server_handle]
- set: [srv, { call: [http.server_tls, "0.0.0.0", 8443, cert, key] }]

- call: [srv.route, "GET", "/", handle_root]
- call: [srv.start]
```

`http.server_tls` kombiniert intern `tls.*` für den Transport mit dem gleichen
HTTP/1.1-Parser wie `http.server`.

---

## 5. Namespace-Referenz

### `url`-Namespace

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `{ call: [url.parse, s] }` | `url` | Parst und validiert URL-String |

### `http`-Namespace

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `{ call: [http.session] }` | `http_session_handle` | Neue persistente Session (Cookie-Jar, Keep-Alive) |
| `{ call: [http.get, u] }` | `http_response_handle` | Einzel-GET ohne Session (`u: url`) |
| `{ call: [http.post, u, body] }` | `http_response_handle` | Einzel-POST ohne Session (`u: url`) |
| `{ call: [http.put, u, body] }` | `http_response_handle` | Einzel-PUT ohne Session (`u: url`) |
| `{ call: [http.delete, u] }` | `http_response_handle` | Einzel-DELETE ohne Session (`u: url`) |
| `{ call: [http.patch, u, body] }` | `http_response_handle` | Einzel-PATCH ohne Session (`u: url`) |
| `{ call: [http.head, u] }` | `http_response_handle` | Einzel-HEAD ohne Session (`u: url`) |
| `{ call: [http.new_request, method, u] }` | `http_client_handle` | Client-Request-Builder (`u: url`) |
| `{ call: [http.server, host, port] }` | `http_server_handle` | Plain-HTTP-Server |
| `{ call: [http.server_tls, host, port, cert, key] }` | `http_server_handle` | HTTPS-Server (`cert, key: path`) |

---

## 6. Implementierungshinweise

### 6.1 URL-Parsing

Interne C-Struktur (`AimlUrl`):

```cpp
struct AimlUrl {
    char* protocol;  // z.B. "https", "ws", "ftp" — NULL wenn nicht gesetzt
    char* host;      // NULL wenn fehlt
    int   port;      // 0 = Protokoll-Default
    char* path;      // NULL wenn fehlt
    char* fragment;  // NULL wenn keiner
    AimlMap* get_param; // Query-Parameter als Map — NULL wenn keine vorhanden
                        // lazy geparst beim ersten Zugriff auf get_param
};
```

Jeder eingehende HTTP-Request wird automatisch in ein `AimlUrl` geparst und über
`req.url` bereitgestellt.

### 6.2 HTTP/1.1-Parser (`http_runtime.cpp`)

- Request-Line: `GET /path?query HTTP/1.1\r\n`
- Header: `Name: Value\r\n` bis `\r\n\r\n`
- Body: `Content-Length`-basiert (kein Chunked-Transfer-Encoding in v1)

### 6.3 Keine externen Abhängigkeiten

Alle HTTP-Operationen laufen über `net.*` / `tls.*` — kein libcurl, kein libuv.

### 6.5 Zero-Copy-Pfade

| Fall | Mechanismus | Userspace-Copy |
|------|-------------|----------------|
| `resp.body_file` + `send` | `sendfile(out_fd, in_fd, ...)` — Kernel Page Cache → Socket | keine |
| `resp.body_bytes` + `send` | `send(fd, buf.ptr, buf.len, 0)` | keine |
| `resp.write_bytes(buf)` | `send(fd, buf.ptr, buf.len, MSG_MORE)` | keine |
| `resp.write(chunk)` | `send(fd, chunk.ptr, chunk.len, MSG_MORE)` | keine (string ist bereits `char*`) |
| `resp.body` + `send` | `send(fd, body.ptr, body.len, 0)` | keine |
| `{ call: [store.raw_bytes] }` + `write_bytes` | View in Store-Speicher → `send(fd, ptr, ...)` | keine (nur value-type Structs) |

HTTPS-Pfad (`tls.*`): `SSL_write` kopiert intern in den TLS-Record-Puffer — ein Userspace-Copy ist bei verschlüsselten Verbindungen unvermeidlich.

### 6.4 Keep-Alive

HTTP/1.1 Keep-Alive ist in v1 nicht aktiv — jeder Request öffnet und schließt
eine neue TCP-Verbindung.

---

## 7. Offene Designfragen

- **HTTP/2**: ALPN + binary framing — langfristige Erweiterung.
- **Compression**: `Accept-Encoding: gzip` via `zlib`.
- **Static File Serving**: `srv.serve_static("/", "/var/www")`.
- **WebSocket-Upgrade**: `resp.upgrade_ws(...)` — definiert in Kapitel 32.4.
- **Multipart-Cookies**: `resp.headers` ist `map<string:string>` (ein Wert pro Key).
  Für mehrere `Set-Cookie`-Header in einer Response: ggf. `map<string:list<string>>` oder
  dediziertes `cookies`-Member prüfen (v1-Einschränkung).
- **DB-Streaming**: Ob Zero-Copy möglich ist, hängt vom Struct-Inhalt ab:
  - **Nur Primitive** (`i32`, `f32`, …): Die gespeicherten Bytes sind direkt streambar.
    `{ call: [store.raw_bytes] }` gibt einen `buffer<u8>` zurück, der direkt in den internen
    Store-Speicher zeigt (kein Copy); anschließend `write_bytes` → `send`. Fehlend in v1:
    `raw_bytes`-Methode und Umgang mit inactive Slots (Bitmap mitschicken oder kompaktieren).

    ```aiml
    - decl: [raw, buffer<u8>]
    - set: [raw, { call: [positions.raw_bytes] }]
    - call: [resp.write_bytes, raw]
    - call: [resp.close]
    ```

  - **Pointer-Felder** (`string`, Handles): Rohdaten enthalten prozesslokale Adressen —
    nicht netzwerkfähig, Serialisierung bleibt nötig.
