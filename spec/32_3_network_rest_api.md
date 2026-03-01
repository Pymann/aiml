# Kapitel 32.3 — REST API: Client und Server

> **Leitgedanke:** `rest.*` ist eine dünne Komfort-Schicht über `http.*`.
> Ein `rest_client_handle` kapselt Base-URL, gemeinsame Header (Auth-Token, Accept, etc.)
> und Connection-Pooling — so dass API-Clients ohne Boilerplate geschrieben werden können.
> Der REST-Server erweitert `http_server_handle` um deklaratives Routing mit
> typsicheren Pfad-Parametern.

---

## 1. REST-Client

### 1.1 Schnellstart — einzeilige Requests

```aiml
# Ohne Client-Handle: direkte Convenience-Funktionen
- decl: [resp, http_response_handle]
- set: [resp, { call: [rest.get,  "https://api.example.com/users/42"] }]
- set: [resp, { call: [rest.post, "https://api.example.com/users", "{\"name\":\"Alice\"}"] }]
- set: [resp, { call: [rest.put,  "https://api.example.com/users/42", "{\"name\":\"Bob\"}"] }]
- set: [resp, { call: [rest.delete, "https://api.example.com/users/42"] }]
- set: [resp, { call: [rest.patch,  "https://api.example.com/users/42", "{\"active\":false}"] }]
```

`rest.get/post/...` setzt automatisch `Content-Type: application/json` und
`Accept: application/json` sofern ein Body vorhanden.

### 1.2 `rest_client_handle` — wiederverwendbarer Client

```aiml
# Client einmalig konfigurieren
- decl: [api, rest_client_handle]
- set: [api, { call: [rest.new, "https://api.example.com/v1"] }]

- call: [api.set_header, "Accept", "application/json"]
- call: [api.set_auth_bearer, "sk-abc123"]
- call: [api.set_timeout, 15000]   # 15 s

# Requests: Pfad relativ zur Base-URL
- decl: [resp, http_response_handle]
- set: [resp, { call: [api.get, "/users"] }]
- print: ["Status: ${ call: [resp.status] }"]
- print: ["${ call: [resp.body] }"]

- set: [resp, { call: [api.post, "/users", "{\"name\":\"Alice\",\"role\":\"admin\"}"] }]
- set: [resp, { call: [api.put,  "/users/42", "{\"role\":\"user\"}"] }]
- set: [resp, { call: [api.delete, "/users/42"] }]
- set: [resp, { call: [api.patch,  "/users/42", "{\"active\":false}"] }]
```

### 1.3 `rest_client_handle`-Methoden

| Methode | Signatur | Beschreibung |
|---------|----------|--------------|
| `set_header` | `(name: string, val: string) → void` | Persistenter Header für alle Requests |
| `set_auth_bearer` | `(token: string) → void` | Setzt `Authorization: Bearer <token>` |
| `set_auth_basic` | `(user: string, pass: string) → void` | Setzt `Authorization: Basic <b64>` |
| `set_auth_apikey` | `(header: string, key: string) → void` | Beliebiger API-Key-Header |
| `set_timeout` | `(ms: i32) → void` | Timeout für alle Requests |
| `set_base_url` | `(url: string) → void` | Base-URL nachträglich ändern |
| `get` | `(path: string) → http_response_handle` | GET |
| `post` | `(path: string, body: string) → http_response_handle` | POST |
| `put` | `(path: string, body: string) → http_response_handle` | PUT |
| `delete` | `(path: string) → http_response_handle` | DELETE |
| `patch` | `(path: string, body: string) → http_response_handle` | PATCH |
| `head` | `(path: string) → http_response_handle` | HEAD |
| `request` | `(method: string, path: string) → http_request_handle` | Builder für komplexe Requests |

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
- decl: [gh, rest_client_handle]
- set: [gh, { call: [rest.new, "https://api.github.com"] }]
- call: [gh.set_header, "Accept",     "application/vnd.github.v3+json"]
- call: [gh.set_header, "User-Agent", "aiml-client/1.0"]
- call: [gh.set_auth_bearer, "ghp_yourtoken"]

# Repos eines Users abrufen
- decl: [resp, http_response_handle]
- set: [resp, { call: [gh.get, "/users/octocat/repos"] }]

- decl: [ok, bool]
- set: [ok, { call: [resp.ok] }]
- print: ["Success: ${ok}"]
- print: ["${ call: [resp.body] }"]

# Neues Issue erstellen
- set: [resp, { call: [gh.post, "/repos/octocat/hello-world/issues",
    "{\"title\":\"Found a bug\",\"body\":\"Reproducible in AIML!\"}"] }]
- print: ["Issue created: ${ call: [resp.status] }"]   # 201
```

---

## 2. REST-Server

### 2.1 Pfad-Parameter

REST-Server unterstützt **Pfad-Parameter** via `{name}`-Syntax:

```aiml
- def:
    name: get_user
    args: [[ctx, http_ctx_handle]]
    body:
      # Pfad-Parameter aus Route "/users/{id}"
      - decl: [id, string]
      - set: [id, { call: [ctx.param, "id"] }]
      - call: [ctx.send_json, 200, "{\"id\": \"${id}\", \"name\": \"Alice\"}"]

- def:
    name: create_user
    args: [[ctx, http_ctx_handle]]
    body:
      - decl: [body, string]
      - set: [body, { call: [ctx.body] }]
      # body enthält JSON, z.B. {"name": "Bob"}
      - call: [ctx.send_json, 201, "{\"created\": true}"]

- decl: [api, http_server_handle]
- set: [api, { call: [http.server, "0.0.0.0", 8080] }]

- call: [api.route, "GET",    "/users",      list_users]
- call: [api.route, "POST",   "/users",      create_user]
- call: [api.route, "GET",    "/users/{id}", get_user]
- call: [api.route, "PUT",    "/users/{id}", update_user]
- call: [api.route, "DELETE", "/users/{id}", delete_user]

- call: [api.start]
```

`ctx.param("id")` gibt den Wert des Pfad-Parameters zurück.
Bei mehreren Parametern: `/orgs/{org}/repos/{repo}` → `ctx.param("org")`, `ctx.param("repo")`.

### 2.2 Query-Parameter und Headers

```aiml
- def:
    name: search_users
    args: [[ctx, http_ctx_handle]]
    body:
      # URL: /users?q=alice&limit=10
      - decl: [q,     string]
      - decl: [limit, string]
      - set: [q,     { call: [ctx.query, "q"] }]
      - set: [limit, { call: [ctx.query, "limit"] }]

      - decl: [auth, string]
      - set: [auth, { call: [ctx.header, "Authorization"] }]

      - print: ["Search: ${q}, limit: ${limit}, auth: ${auth}"]
      - call: [ctx.send_json, 200, "[]"]
```

### 2.3 Erweiterte `http_ctx_handle`-Methoden (REST-spezifisch)

| Methode | Signatur | Beschreibung |
|---------|----------|--------------|
| `param` | `(name: string) → string` | Pfad-Parameter aus Route-Template |
| `query` | `(name: string) → string` | URL-Query-Parameter |
| `query_all` | `(name: string) → list<string>` | Mehrfach-Query-Parameter |
| `header` | `(name: string) → string` | Request-Header |
| `body` | `() → string` | Request-Body |
| `method` | `() → string` | HTTP-Methode |
| `path` | `() → string` | Pfad (ohne Query-String) |

### 2.4 Middleware

```aiml
# Middleware: wird vor jedem Handler aufgerufen
# Gibt true zurück → Handler wird aufgerufen
# Gibt false zurück → Middleware hat bereits geantwortet, Handler überspringen
- def:
    name: auth_middleware
    args: [[ctx, http_ctx_handle]]
    return: bool
    body:
      - decl: [tok, string]
      - set: [tok, { call: [ctx.header, "Authorization"] }]
      - if:
          condition: { call: [tok == ""] }
          body:
            - call: [ctx.send_json, 401, "{\"error\":\"unauthorized\"}"]
            - return: false
      - return: true

- decl: [api, http_server_handle]
- set: [api, { call: [http.server, "0.0.0.0", 8080] }]
- call: [api.use, auth_middleware]            # Gilt für alle Routes
- call: [api.route, "GET", "/private", private_handler]
- call: [api.start]
```

`api.use(fn)` — globale Middleware, wird vor jedem Handler ausgeführt.
`api.use_path(fn, "/admin/*")` — Middleware nur für Pfade mit diesem Prefix.

---

## 3. Vollständige `rest`-Namespace-Referenz

| Funktion | Signatur | Beschreibung |
|----------|----------|--------------|
| `rest.new` | `(base_url: string) → rest_client_handle` | Client mit Base-URL |
| `rest.get` | `(url: string) → http_response_handle` | Direkt-GET |
| `rest.post` | `(url: string, body: string) → http_response_handle` | Direkt-POST |
| `rest.put` | `(url: string, body: string) → http_response_handle` | Direkt-PUT |
| `rest.delete` | `(url: string) → http_response_handle` | Direkt-DELETE |
| `rest.patch` | `(url: string, body: string) → http_response_handle` | Direkt-PATCH |

---

## 4. Fehlerbehandlung

```aiml
- decl: [resp, http_response_handle]
- set: [resp, { call: [api.get, "/users/999"] }]

- decl: [status, i32]
- set: [status, { call: [resp.status] }]

- if:
    condition: { call: [status == 404] }
    body:
      - print: ["User not found"]
- if:
    condition: { call: [status == 401] }
    body:
      - print: ["Unauthorized — check token"]
- if:
    condition: { call: [resp.ok] }
    body:
      - print: ["${ call: [resp.body] }"]
```

Netzwerkfehler (Timeout, Connection refused) geben einen `http_response_handle`
mit `status() == 0` zurück — kein Panic.

---

## 5. Implementierungshinweise

### 5.1 Beziehung zu `http.*`

`rest.*` ist vollständig auf Basis von `http.*` implementiert — keine eigene Runtime-Schicht.
`rest_client_handle` speichert intern: Base-URL-String, Header-Map, Timeout-Wert.
Pro Aufruf von `api.get(path)`:
1. Konkateniert Base-URL + path.
2. Erstellt `http_request_handle` via `http.new_request`.
3. Überträgt gespeicherte Header + Auth.
4. Ruft `req.send()` auf.

### 5.2 Pfad-Parameter-Matching

Route `"/users/{id}/posts/{post_id}"` wird zu einem Regex kompiliert:
`^/users/([^/]+)/posts/([^/]+)$`. Parameternamen werden in einer Map gespeichert;
bei Match werden die Capture-Groups zugeordnet.

### 5.3 Connection-Pooling (Future Work)

In v1: jeder API-Call öffnet eine neue TCP/TLS-Verbindung. Für High-Throughput-Szenarien
(>100 Requests/s) ist ein Connection-Pool geplant: `rest_client_handle` hält intern
einen Pool von `socket_handle`/`tls_socket_handle`, die über Requests wiederverwendet
werden (`Connection: keep-alive`).

---

## 6. Offene Designfragen

- **JSON-Parsing**: `resp.body()` gibt einen String zurück — AIML hat noch keinen
  eingebauten JSON-Parser. Kurzfristig: manuelles String-Parsen oder FFI zu `cjson`.
  Langfristig: `json.*`-Namespace mit `json.parse(str) → map<string, any>`.
- **Pagination**: `rest_client_handle` hat keine eingebaute Cursor/Pagination-Logik.
  Muss manuell mit `api.get("/users?page=2")` implementiert werden.
- **Rate Limiting**: Kein eingebautes Retry mit Backoff — Future Work.
- **OpenAPI-Code-Gen**: Aus einer OpenAPI-Spec automatisch AIML-Stubs generieren
  (Tooling-Erweiterung, nicht Core-Language).
