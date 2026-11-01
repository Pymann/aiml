# Chapter 32.3 — REST API: Client and Server

> **Design principle:** `rest.*` is a thin convenience layer over `http.*`.
> A `rest_client_handle` encapsulates base URL, shared headers (auth token, Accept, etc.)
> and connection pooling — so API clients can be written without boilerplate.
> The REST server is fully identical to the HTTP server from Chapter 32.2 —
> `rest.*` merely adds convenience functions for common REST patterns.

---

## 1. REST Client

### 1.1 Quick Start — Direct Requests

```aiml
# Without client handle: direct convenience functions (url type required)
- decl: [users_url, url, { call: [http.parse_url, "https://api.example.com/users/42"] }]

- decl: [resp, http_response_handle]
- set: [resp, { call: [rest.get, users_url] }]

- decl: [new_url, url, { call: [http.parse_url, "https://api.example.com/users"] }]
- set: [resp, { call: [rest.post,   new_url, "{\"name\":\"Alice\"}"] }]

- decl: [u42, url, { call: [http.parse_url, "https://api.example.com/users/42"] }]
- set: [resp, { call: [rest.put,    u42, "{\"name\":\"Bob\"}"] }]
- set: [resp, { call: [rest.delete, u42] }]
- set: [resp, { call: [rest.patch,  u42, "{\"active\":false}"] }]
```

`rest.get/post/...` automatically sets `Content-Type: application/json` and
`Accept: application/json` when a body is present.

### 1.2 `rest_client_handle` — Reusable Client

```aiml
# Configure client once
- decl: [base, url, { call: [http.parse_url, "https://api.example.com/v1"] }]
- decl: [api, rest_client_handle]
- set: [api, { call: [rest.new, base] }]

- call: [api.set_header, "Accept", "application/json"]
- call: [api.set_auth_bearer, "sk-abc123"]
- call: [api.set_timeout, 15000]

# Requests: path relative to base URL (string)
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

### 1.3 `rest_client_handle` Methods

| Method            | Arguments                          | Return                 | Description |
|-------------------|------------------------------------|------------------------|-------------|
| `set_header`      | `name: string, val: string`        | —                      | Persistent header for all requests |
| `set_auth_bearer` | `token: string`                    | —                      | Sets `Authorization: Bearer <token>` |
| `set_auth_basic`  | `user: string, pass: string`       | —                      | Sets `Authorization: Basic <b64>` |
| `set_auth_apikey` | `header: string, key: string`      | —                      | Arbitrary API key header |
| `set_timeout`     | `ms: i32`                          | —                      | Timeout for all requests |
| `set_base_url`    | `u: url`                           | —                      | Change base URL after creation |
| `get`             | `path: string`                     | `http_response_handle` | GET |
| `post`            | `path: string, body: string`       | `http_response_handle` | POST |
| `put`             | `path: string, body: string`       | `http_response_handle` | PUT |
| `delete`          | `path: string`                     | `http_response_handle` | DELETE |
| `patch`           | `path: string, body: string`       | `http_response_handle` | PATCH |
| `head`            | `path: string`                     | `http_response_handle` | HEAD |
| `request`         | `method: string, path: string`     | `http_client_handle`   | Builder for complex requests |

### 1.4 Authentication Modes

```aiml
# Bearer token (OAuth 2.0, JWT)
- call: [api.set_auth_bearer, "eyJhbGci..."]

# HTTP Basic Auth
- call: [api.set_auth_basic, "admin", "s3cr3t"]

# Arbitrary API key header (e.g. GitHub, Stripe)
- call: [api.set_auth_apikey, "X-API-Key", "my-secret-key"]

# No auth (default)
# (no additional configuration needed)
```

### 1.5 Complete Example: GitHub API

```aiml
- decl: [gh_base, url, { call: [http.parse_url, "https://api.github.com"] }]
- decl: [gh, rest_client_handle]
- set: [gh, { call: [rest.new, gh_base] }]
- call: [gh.set_header, "Accept",     "application/vnd.github.v3+json"]
- call: [gh.set_header, "User-Agent", "aiml-client/1.0"]
- call: [gh.set_auth_bearer, "ghp_yourtoken"]

# Fetch repos of a user
- decl: [resp, http_response_handle]
- set: [resp, { call: [gh.get, "/users/octocat/repos"] }]

- decl: [ok, bool]
- set: [ok, { call: [resp.ok] }]
- print: ["Success: ${ok}"]

- decl: [body, string]
- set: [body, { call: [resp.body] }]
- print: ["${body}"]

# Create new issue
- set: [resp, { call: [gh.post, "/repos/octocat/hello-world/issues",
    "{\"title\":\"Found a bug\",\"body\":\"Reproducible in AIML!\"}"] }]

- decl: [issue_status, i32]
- set: [issue_status, { call: [resp.status] }]
- print: ["Issue created: ${issue_status}"]
```

---

## 2. REST Server

Handler signature: always `req: http_request_handle` and `resp: http_response_handle`
(identical to Chapter 32.2).

### 2.1 Path Parameters with `:name` Syntax

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

### 2.2 Query Parameters and Headers

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
      # ein fehlender Header ist der LEERE String, nie ein Nullzeiger

      - print: ["Search: ${q}, limit: ${limit}, auth: ${auth}"]
      - call: [resp.headers.put, "Content-Type", "application/json"]
      - set: [resp.status, 200]
      - set: [resp.body, "[]"]
      - call: [resp.send]
```

### 2.3 Middleware (Security)

Middleware has the same signature as handlers. If middleware calls `resp.send()`,
the actual handler is skipped.

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

For middleware that applies only to specific paths, use `srv.group`
(see Chapter 32.2 §3.8).

---

## 3. Namespace Reference

### `rest` Namespace

| Call | Return | Description |
|------|--------|-------------|
| `{ call: [rest.new, base_url] }` | `rest_client_handle` | Client with base URL (`base_url: url`) |
| `{ call: [rest.get, u] }` | `http_response_handle` | Direct GET (`u: url`) |
| `{ call: [rest.post, u, body] }` | `http_response_handle` | Direct POST (`u: url`) |
| `{ call: [rest.put, u, body] }` | `http_response_handle` | Direct PUT (`u: url`) |
| `{ call: [rest.delete, u] }` | `http_response_handle` | Direct DELETE (`u: url`) |
| `{ call: [rest.patch, u, body] }` | `http_response_handle` | Direct PATCH (`u: url`) |

---

## 4. Error Handling

```aiml
- decl: [resp, http_response_handle]
- set: [resp, { call: [api.get, "/users/999"] }]

- decl: [status, i32]
- set: [status, { call: [resp.status] }]

- if: [{ eq: [status, 404] }, true]
  then:
    - print: ["User not found"]

- if: [{ eq: [status, 401] }, true]
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

Network errors (timeout, connection refused) return an `http_response_handle`
with `status() == 0` — no panic.

---

## 5. Implementation Notes

### 5.1 Relationship to `http.*` and `http_session_handle`

`rest_client_handle` is built internally on top of `http_session_handle`:

```
http_session_handle        ← cookie jar, keep-alive, persistent headers/auth
        ↑
rest_client_handle         ← + base URL + auto JSON headers + relative paths
```

Per call to `api.get(path)`:
1. Concatenates base URL + path → new `url` object.
2. Delegates to the internal `http_session_handle` with session context.
3. Cookie jar and keep-alive are transparently managed by the session.

Those who don't need a REST interface (e.g. web scraping, OAuth flows) use
`http_session_handle` directly (Chapter 32.2 §2.5).

### 5.2 Path Parameter Matching

Route `"/users/:id/posts/:post_id"` matches segments between `/`. Parameter names are
stored in `req.params` (`map<string:string>`) — identical to Chapter 32.2 §3.7.

### 5.3 Connection Pooling

`rest_client_handle` automatically uses keep-alive via `http_session_handle`:
`Connection: keep-alive` is set, TCP connections are reused per domain.
In v1 the pool is limited to one connection per domain.

---

## 6. Open Design Questions

- **JSON parsing**: `resp.body` returns a string — AIML does not yet have a built-in
  JSON parser. Short term: manual string parsing or FFI to `cjson`.
  Long term: `json.*` namespace with `json.parse(str) → map<string:any>`.
- **Pagination**: `rest_client_handle` has no built-in cursor/pagination logic.
  Must be implemented manually with `api.get("/users?page=2")`.
