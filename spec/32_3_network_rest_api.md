# Kapitel 32.3 — REST API: Client und Server

> **Leitgedanke:** `rest.*` ist eine dünne Komfort-Schicht über `http.*`.
> Ein `rest_client_handle` kapselt Base-URL, gemeinsame Header (Auth-Token, Accept, etc.)
> und Connection-Pooling — so dass API-Clients ohne Boilerplate geschrieben werden können.
> Der REST-Server ist vollständig identisch mit dem HTTP-Server aus Kapitel 32.2 —
> `rest.*` fügt lediglich Convenience-Funktionen für häufige REST-Muster hinzu.

---

## 1. REST-Client

### 1.1 Schnellstart — direkte Requests

```aiml
# Ohne Client-Handle: direkte Convenience-Funktionen (url-Typ erforderlich)
- decl: [users_url, url]
- set: [users_url, "https://api.example.com/users/42"]

- decl: [resp, http_response_handle]
- set: [resp, { call: [rest.get, users_url] }]

- decl: [new_url, url]
- set: [new_url, "https://api.example.com/users"]
- set: [resp, { call: [rest.post,   new_url, "{\"name\":\"Alice\"}"] }]

- decl: [u42, url]
- set: [u42, "https://api.example.com/users/42"]
- set: [resp, { call: [rest.put,    u42, "{\"name\":\"Bob\"}"] }]
- set: [resp, { call: [rest.delete, u42] }]
- set: [resp, { call: [rest.patch,  u42, "{\"active\":false}"] }]
```

`rest.get/post/...` setzt automatisch `Content-Type: application/json` und
`Accept: application/json` sofern ein Body vorhanden.

### 1.2 `rest_client_handle` — wiederverwendbarer Client

```aiml
# Client einmalig konfigurieren
- decl: [base, url]
- set: [base, "https://api.example.com/v1"]
- decl: [api, rest_client_handle]
- set: [api, { call: [rest.new, base] }]

- call: [api.set_header, "Accept", "application/json"]
- call: [api.set_auth_bearer, "sk-abc123"]
- call: [api.set_timeout, 15000]

# Requests: Pfad relativ zur Base-URL (string)
- decl: [resp, http_response_handle]
- set: [resp, { call: [api.get, "/users"] }]

- decl: [status, i32]
- set: [status, { call: [resp.status] }]
- print: ["Status: ${status}"]

- decl: [body, string]
- set: [body, { call: [resp.body] }]
- print: ["${body}"]

- set: [resp, { call: [api.post,   "/users",    "{\"name\":\"Alice\",\"role\":\"admin\"}"] }]
- set: [resp, { call: [api.put,    "/users/42", "{\"role\":\"user\"}"] }]
- set: [resp, { call: [api.delete, "/users/42"] }]
- set: [resp, { call: [api.patch,  "/users/42", "{\"active\":false}"] }]
```

### 1.3 `rest_client_handle`-Methoden

| Methode           | Argumente                          | Rückgabe               | Beschreibung |
|-------------------|------------------------------------|------------------------|--------------|
| `set_header`      | `name: string, val: string`        | —                      | Persistenter Header für alle Requests |
| `set_auth_bearer` | `token: string`                    | —                      | Setzt `Authorization: Bearer <token>` |
| `set_auth_basic`  | `user: string, pass: string`       | —                      | Setzt `Authorization: Basic <b64>` |
| `set_auth_apikey` | `header: string, key: string`      | —                      | Beliebiger API-Key-Header |
| `set_timeout`     | `ms: i32`                          | —                      | Timeout für alle Requests |
| `set_base_url`    | `u: url`                           | —                      | Base-URL nachträglich ändern |
| `get`             | `path: string`                     | `http_response_handle` | GET |
| `post`            | `path: string, body: string`       | `http_response_handle` | POST |
| `put`             | `path: string, body: string`       | `http_response_handle` | PUT |
| `delete`          | `path: string`                     | `http_response_handle` | DELETE |
| `patch`           | `path: string, body: string`       | `http_response_handle` | PATCH |
| `head`            | `path: string`                     | `http_response_handle` | HEAD |
| `request`         | `method: string, path: string`     | `http_client_handle`   | Builder für komplexe Requests |

### 1.4 Authentifizierungs-Modi

```aiml
# Bearer Token (OAuth 2.0, JWT)
- call: [api.set_auth_bearer, "eyJhbGci..."]

# HTTP Basic Auth
- call: [api.set_auth_basic, "admin", "s3cr3t"]

# Beliebiger API-Key-Header (z.B. GitHub, Stripe)
- call: [api.set_auth_apikey, "X-API-Key", "my-secret-key"]

# Kein Auth (Default)
# (keine zusätzliche Konfiguration nötig)
```

### 1.5 Vollständiges Beispiel: GitHub API

```aiml
- decl: [gh_base, url]
- set: [gh_base, "https://api.github.com"]
- decl: [gh, rest_client_handle]
- set: [gh, { call: [rest.new, gh_base] }]
- call: [gh.set_header, "Accept",     "application/vnd.github.v3+json"]
- call: [gh.set_header, "User-Agent", "aiml-client/1.0"]
- call: [gh.set_auth_bearer, "ghp_yourtoken"]

# Repos eines Users abrufen
- decl: [resp, http_response_handle]
- set: [resp, { call: [gh.get, "/users/octocat/repos"] }]

- decl: [ok, bool]
- set: [ok, { call: [resp.ok] }]
- print: ["Success: ${ok}"]

- decl: [body, string]
- set: [body, { call: [resp.body] }]
- print: ["${body}"]

# Neues Issue erstellen
- set: [resp, { call: [gh.post, "/repos/octocat/hello-world/issues",
    "{\"title\":\"Found a bug\",\"body\":\"Reproducible in AIML!\"}"] }]

- decl: [issue_status, i32]
- set: [issue_status, { call: [resp.status] }]
- print: ["Issue created: ${issue_status}"]
```

---

## 2. REST-Server

Handler-Signatur: immer `req: http_request_handle` und `resp: http_response_handle`
(identisch mit Kapitel 32.2).

### 2.1 Pfad-Parameter mit `:name`-Syntax

```aiml
- def:
    name: get_user
    args: [[req, http_request_handle], [resp, http_response_handle]]
    body:
      - decl: [p, map<string:string>]
      - set: [p, req.params]
      - decl: [id, string]
      - set: [id, { call: [p.get, "id"] }]
      - call: [resp.headers.put, "Content-Type", "application/json"]
      - set: [resp.status, 200]
      - set: [resp.body, "{\"id\": \"${id}\", \"name\": \"Alice\"}"]
      - call: [resp.send]

- def:
    name: create_user
    args: [[req, http_request_handle], [resp, http_response_handle]]
    body:
      - decl: [body, string]
      - set: [body, req.body]
      - call: [resp.headers.put, "Content-Type", "application/json"]
      - set: [resp.status, 201]
      - set: [resp.body, "{\"created\": true}"]
      - call: [resp.send]

- decl: [api, http_server_handle]
- set: [api, { call: [http.server, "0.0.0.0", 8080] }]

- call: [api.route, "GET",    "/users",      list_users]
- call: [api.route, "POST",   "/users",      create_user]
- call: [api.route, "GET",    "/users/:id",  get_user]
- call: [api.route, "PUT",    "/users/:id",  update_user]
- call: [api.route, "DELETE", "/users/:id",  delete_user]

- call: [api.start]
```

### 2.2 Query-Parameter und Headers

```aiml
- def:
    name: search_users
    args: [[req, http_request_handle], [resp, http_response_handle]]
    body:
      # URL: /users?q=alice&limit=10
      - decl: [u, url]
      - set: [u, req.url]

      - decl: [q, string]
      - set: [q, { call: [u.get_param.get, "q"] }]

      - decl: [limit, string]
      - set: [limit, { call: [u.get_param.get, "limit"] }]

      - decl: [auth, string]
      - set: [auth, { call: [req.header, "Authorization"] }]

      - print: ["Search: ${q}, limit: ${limit}, auth: ${auth}"]
      - call: [resp.headers.put, "Content-Type", "application/json"]
      - set: [resp.status, 200]
      - set: [resp.body, "[]"]
      - call: [resp.send]
```

### 2.3 Middleware (Absicherung)

Middleware hat dieselbe Signatur wie Handler. Ruft die Middleware `resp.send()` auf,
wird der eigentliche Handler übersprungen.

```aiml
- def:
    name: auth_middleware
    args: [[req, http_request_handle], [resp, http_response_handle]]
    body:
      - decl: [tok, string]
      - set: [tok, { call: [req.header, "Authorization"] }]
      - if: [{ call: [tok.is_empty] }, true]
        then:
          - call: [resp.headers.put, "Content-Type", "application/json"]
          - set: [resp.status, 401]
          - set: [resp.body, "{\"error\":\"unauthorized\"}"]
          - call: [resp.send]

- decl: [api, http_server_handle]
- set: [api, { call: [http.server, "0.0.0.0", 8080] }]
- call: [api.use, auth_middleware]
- call: [api.route, "GET", "/private", private_handler]
- call: [api.start]
```

Für Middleware, die nur auf bestimmte Pfade zutrifft, `srv.group` verwenden
(siehe Kapitel 32.2 §3.8).

---

## 3. Namespace-Referenz

### `rest`-Namespace

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `{ call: [rest.new, base_url] }` | `rest_client_handle` | Client mit Base-URL (`base_url: url`) |
| `{ call: [rest.get, u] }` | `http_response_handle` | Direkt-GET (`u: url`) |
| `{ call: [rest.post, u, body] }` | `http_response_handle` | Direkt-POST (`u: url`) |
| `{ call: [rest.put, u, body] }` | `http_response_handle` | Direkt-PUT (`u: url`) |
| `{ call: [rest.delete, u] }` | `http_response_handle` | Direkt-DELETE (`u: url`) |
| `{ call: [rest.patch, u, body] }` | `http_response_handle` | Direkt-PATCH (`u: url`) |

---

## 4. Fehlerbehandlung

```aiml
- decl: [resp, http_response_handle]
- set: [resp, { call: [api.get, "/users/999"] }]

- decl: [status, i32]
- set: [status, { call: [resp.status] }]

- if: [{ ==: [status, 404] }, true]
  then:
    - print: ["User not found"]

- if: [{ ==: [status, 401] }, true]
  then:
    - print: ["Unauthorized — check token"]

- decl: [ok, bool]
- set: [ok, { call: [resp.ok] }]
- if: [ok, true]
  then:
    - decl: [body, string]
    - set: [body, { call: [resp.body] }]
    - print: ["${body}"]
```

Netzwerkfehler (Timeout, Connection refused) geben einen `http_response_handle`
mit `status() == 0` zurück — kein Panic.

---

## 5. Implementierungshinweise

### 5.1 Beziehung zu `http.*` und `http_session_handle`

`rest_client_handle` baut intern auf `http_session_handle` auf:

```
http_session_handle        ← Cookie-Jar, Keep-Alive, persistente Headers/Auth
        ↑
rest_client_handle         ← + Base-URL + auto JSON-Headers + relative Pfade
```

Pro Aufruf von `api.get(path)`:
1. Konkateniert Base-URL + path → neues `url`-Objekt.
2. Delegiert an die interne `http_session_handle` mit Session-Kontext.
3. Cookie-Jar und Keep-Alive werden transparent von der Session verwaltet.

Wer kein REST-Interface braucht (z.B. Web-Scraping, OAuth-Flows), nutzt
`http_session_handle` direkt (Kapitel 32.2 §2.5).

### 5.2 Pfad-Parameter-Matching

Route `"/users/:id/posts/:post_id"` matcht Segmente zwischen `/`. Parameternamen werden
in `req.params` (`map<string:string>`) eingetragen — identisch mit Kapitel 32.2 §3.7.

### 5.3 Connection-Pooling

`rest_client_handle` nutzt über `http_session_handle` automatisch Keep-Alive:
`Connection: keep-alive` wird gesetzt, TCP-Verbindungen werden pro Domain wiederverwendet.
In v1 ist der Pool auf eine Verbindung pro Domain begrenzt.

---

## 6. Offene Designfragen

- **JSON-Parsing**: `resp.body` gibt einen String zurück — AIML hat noch keinen
  eingebauten JSON-Parser. Kurzfristig: manuelles String-Parsen oder FFI zu `cjson`.
  Langfristig: `json.*`-Namespace mit `json.parse(str) → map<string:any>`.
- **Pagination**: `rest_client_handle` hat keine eingebaute Cursor/Pagination-Logik.
  Muss manuell mit `api.get("/users?page=2")` implementiert werden.
