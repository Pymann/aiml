# Chapter 32.2 — HTTP/S: Client and Server

> **Core idea:** HTTP/1.1 is implemented in AIML **without external libraries** on top of
> `net.*` (HTTP) and `tls.*` (HTTPS). The transport is selected automatically
> based on the URL scheme. The HTTP server is single-threaded by default,
> but a dedicated thread can be spawned for each incoming client using `spawn`.

---

## 1. Types

| Type                   | LLVM  | Description |
|------------------------|-------|-------------|
| `url`                  | `i8*` | Parsed, validated URL (scheme, host, port, path, query, fragment) |
| `http_session_handle`  | `i8*` | Persistent client session (cookie jar, keep-alive, shared headers/auth) |
| `http_client_handle`   | `i8*` | Builder for a single outgoing request (method, URL, headers, body) |
| `http_request_handle`  | `i8*` | Incoming server request (URL, method, headers, body, cookies, path params) |
| `http_response_handle` | `i8*` | Server response writer **and** received client response |
| `http_server_handle`   | `i8*` | Bound HTTP server (routes, middleware, handlers) |

Handler signature: always two parameters — request and response:

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

## 1.1 The `url` Type

`url` is a standalone type — inspired by Pydantic's `HttpUrl` — that represents a
fully parsed and validated URL. It is set automatically as a member of `http_request_handle`
when an HTTP request is received, and can also be created independently from a string.

### Creation

```aiml
# Parse from string
- decl: [u, url]
- set: [u, { call: [url.parse, "https://api.example.com:8443/v1/users?page=2&limit=10#top"] }]

# Direct assignment (auto-parse, like the path type)
- decl: [u, url]
- set: [u, "https://api.example.com/v1/users"]
```

### Members and Methods of the `url` Type

**Members** (directly readable):

| Member       | Type                 | Description |
|--------------|----------------------|-------------|
| `protocol`   | `string`             | Protocol, e.g. `"https"`, `"ftp"`, `"ws"` — null if not set |
| `host`       | `string`             | Hostname, e.g. `"api.example.com"` — null if missing |
| `path`       | `string`             | Path component, e.g. `"/v1/users"` — null if missing |
| `fragment`   | `string`             | Fragment after `#` — null if none |
| `port`       | `i32`                | Port; `0` = protocol default |
| `get_param`  | `map<string:string>` | All query parameters; null if none present |
| `credential` | `url_credential`     | Embedded credentials (`user:pass@host`); null if none |

`url_credential` has two members:

| Member | Type     | Description |
|--------|----------|-------------|
| `user` | `string` | Username; null if missing |
| `pass` | `string` | Password; null if missing |

**Methods**:

| Method      | Arguments | Return   | Description |
|-------------|-----------|----------|-------------|
| `to_string` | —         | `string` | Full URL as string |
| `is_https`  | —         | `bool`   | `true` if protocol is `https` |

### Example

```aiml
- decl: [u, url]
- set: [u, "https://api.example.com/v1/users?page=2&limit=10"]

# Read members directly
- print: ["host: ${u.host}"]
- print: ["path: ${u.path}"]

# Query parameters: get_param is a map<string:string>
- decl: [pg, string]
- set: [pg, { call: [u.get_param.get, "page"] }]
- print: ["page: ${pg}"]

- decl: [limit, string]
- set: [limit, { call: [u.get_param.get, "limit"] }]
- print: ["limit: ${limit}"]

# Null check for optional fields
- if: [{ ==: [u.fragment, null] }, true]
  then:
    - print: ["no fragment"]
```

---

## 2. HTTP Client

### 2.1 Convenience Functions

```aiml
# GET
- decl: [status_url, url]
- set: [status_url, "https://api.example.com/status"]
- decl: [resp, http_response_handle]
- set: [resp, { call: [http.get, status_url] }]

- print: ["Status: ${resp.status}"]
- print: ["Body: ${resp.body}"]

# POST with JSON body
- decl: [items_url, url]
- set: [items_url, "https://api.example.com/items"]
- decl: [resp2, http_response_handle]
- set: [resp2, { call: [http.post, items_url, "{\"name\": \"test\"}"] }]

# additional methods
- decl: [item1_url, url]
- set: [item1_url, "https://api.example.com/items/1"]
- set: [resp, { call: [http.put,    item1_url, "{\"name\": \"new\"}"] }]
- set: [resp, { call: [http.delete, item1_url] }]
- set: [resp, { call: [http.patch,  item1_url, "{\"x\": 1}"] }]
- set: [resp, { call: [http.head,   status_url] }]
```

**`http.fetch_text(url: string) → string`** — a one-shot GET that returns just the
response body as text (or `""` on error). Unlike `http.get`, it takes a plain URL
**string** (no `url` object) and is available in the **WASM/browser** build, where it
uses `window.fetch` — the browser client has no `http_client` (that is native-only),
so this is the way a WASM build reads a small endpoint such as a discovery list.
Natively it wraps `http.get`. Pair it with `sys.deserialize` to parse a served model.

```aiml
- decl: [raw, string, { call: [http.fetch_text, "/servers.json"] }]   # relative → same-origin in the browser
- decl: [list, ptr<ServerList>, { call: [sys.deserialize, raw, "ServerList"] }]
```

> **HTTPS client verification (strict by default):** `http.get` / `http.fetch_text`
> over `https://` verify the server certificate — chain against the system CA store
> plus hostname match — so a self-signed, expired, or name-mismatched cert makes the
> request fail (empty body / null response). This shares the one client-TLS insecure
> toggle with the mirror wss client: `mirror.tls_insecure(1)` or `AIML_WSS_INSECURE=1`
> disables it for local dev; `AIML_WSS_CA=/path/ca.pem` trusts a private CA. Plain
> `http://` is unaffected. (Browser build: the browser enforces TLS itself.)

### 2.2 Request Builder (with headers, timeout, etc.)

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

### 2.3 `http_client_handle` — Members and Methods

**Members** (directly settable via `set`):

| Member          | Type                 | Description |
|-----------------|----------------------|-------------|
| `headers`       | `map<string:string>` | Request headers; provides `put`, `get`, `remove` etc. |
| `body`          | `string`             | Request body (text) |
| `body_bytes`    | `buffer<u8>`         | Request body (binary) |
| `body_file`     | `string`             | Request body streamed from this file at `send` (binary-safe, any size) |
| `timeout`       | `i32`                | Timeout in ms for the connect and for every read/write (0 = 10 s connect, no read limit) |
| `download_to`   | `string`             | A **2xx** body is streamed into this file (§2.3a); other answers stay in memory |
| `proxy`         | `string`             | `""` (default) = from the environment, `"direct"` = no proxy, otherwise a proxy URL `http://[user:pass@]host[:port]` (§2.3a) |
| `max_redirects` | `i32`                | Redirects followed (default **10**; `0` = a 3xx is the answer) |
| `stream`        | `bool`               | Streaming mode: response is not buffered; use `pipe`/`save_to` |

**Method**:

| Method | Arguments | Return                 | Description |
|--------|-----------|------------------------|-------------|
| `send` | —         | `http_response_handle` | Sends request, returns response; **null** when there is no answer (connection refused / unreachable, TLS failure, proxy refused, no HTTP status line, body cut off, too many redirects) — `http.last_error()` says which |
| `free` | —         | —                      | Releases the request handle (headers, body, method, body_file, download_to, proxy); the `url` and the response are separate.  Call it once, after `send`. |

**`http.last_error() → string`** — why the last request on this thread got no
answer; `""` after an answered one (an HTTP error status *is* an answer).  It names
host and port, never a proxy setting or a header value, so it is safe to log.

### 2.3a Proxy, redirects, downloads (F32-2-30 … F32-2-39)

All client requests (`http.get` … `http.head`, `client.send`) go through one path:

1. **Proxy (F32-2-30).**  With `client.proxy` unset or `""`: `HTTPS_PROXY` for
   `https://`, `HTTP_PROXY` for `http://` (each also lower-case), else `ALL_PROXY`.
   `NO_PROXY` exempts a host: `*`, `example.com` (and its subdomains),
   `.example.com`, `*.example.com`, an IP, an IPv4 CIDR `10.0.0.0/8`, each
   optionally `:port`.  `"direct"` never uses a proxy; any other text is exactly that proxy
   (NO_PROXY is not consulted).  A proxy URL without a port uses 1080; `user:pass@`
   becomes `Proxy-Authorization: Basic …` (percent-decoded).  An `https://` proxy is
   not supported (spec/32_5 §3.5) — the request gets no answer.
2. **Connection (F32-2-31).**  `https://` through a proxy: `CONNECT host:port`,
   then TLS with SNI and the usual verification inside the tunnel — a non-2xx
   CONNECT answer is "no answer".  `http://` through a proxy: the request line
   carries the absolute URL.  Direct otherwise.
3. **Redirects (F32-2-32).**  301/302/303/307/308 with a `Location` are followed up
   to `max_redirects` times; one more is "no answer".  A relative `Location` is
   resolved against the URL that answered.  303 — and 301/302 after a `POST` —
   continue as `GET` without a body (`HEAD` stays `HEAD`); 307/308 keep method and
   body.  **`Authorization` is dropped** once the redirect leaves the original
   origin (scheme + host + port) and stays dropped.  With `max_redirects` 0 the 3xx
   itself is the answer (with its body).
4. **`resp.url` (F32-2-33)** is the URL that gave the answer (after redirects).
5. **Body framing (F32-2-34).**  `Transfer-Encoding: chunked`, else
   `Content-Length` (64-bit), else the body runs until the server closes.  A body
   that ends before its length / last chunk is **no answer** (not a short body).
   HEAD, 1xx, 204 and 304 have no body.
6. **`download_to` (F32-2-35).**  A 2xx body is written to `<file>.part` while it
   arrives and renamed to `<file>` once complete — no heap copy, any size; the
   file is untouched when the transfer breaks off (the `.part` is removed).
   `resp.body` is then empty and `resp.body_len` is the byte count written.  A
   non-2xx body stays in memory (it is the error text), the file is not created.

```aiml
- decl: [src, url, "https://kontor.example/blobs/sha256:ab12"]
- decl: [req, http_client_handle, { call: [http.new_request, "GET", src] }]
- call: [req.headers.put, "Authorization", "Bearer ${token}"]   # dropped if redirected elsewhere
- set: [req.download_to, "/tmp/blob.tar"]
- decl: [resp, http_response_handle, { call: [req.send] }]
- call: [req.free]
- if: { condition: { eq: [resp, null] }, then: [{ call: [log.error, { call: [http.last_error] }] }] }
```

### 2.4 `http_response_handle` — Members and Methods (Client — Reading)

**Members** (directly readable after the request):

| Member       | Type                 | Description |
|--------------|----------------------|-------------|
| `status`     | `i32`                | HTTP status code (200, 404, …) |
| `ok`         | `bool`               | `true` if status is 2xx |
| `headers`    | `map<string:string>` | Response headers; always initialized (never null) |
| `body`       | `string`             | Response body fully buffered; null when streaming |
| `body_bytes` | `buffer<u8>`         | Response body as buffer; null when streaming |
| `body_len`   | `i64`                | Exact byte count of the body (in memory, or written by `download_to`) |
| `url`        | `string`             | The URL that answered, after redirects (§2.3a) |

**Methods** (streaming — no in-memory buffer):

| Method    | Arguments  | Description |
|-----------|------------|-------------|
| `save_to` | `p: path`  | Writes the received body byte-exact to the file (staged as `.part`, renamed); 0 ok, -1 error.  For bodies too big for memory use `client.download_to` (§2.3a) |
| `pipe`    | `fn`       | Calls `fn(chunk: buffer<u8>)` for each received chunk |

```aiml
# Receive file without intermediate buffering
- decl: [out, path]
- set: [out, "/var/data/video.mp4"]
- call: [resp.save_to, out]

# Custom: write chunk-by-chunk to DB
- def:
    name: on_chunk
    args: [[chunk, buffer<u8>]]
    body:
      - call: [sensor_store.write_raw, chunk]

- call: [resp.pipe, on_chunk]
```

> `body` / `body_bytes` and `save_to` / `pipe` are mutually exclusive —
> for streaming requests (`set: [client.stream, true]`), `body` and `body_bytes` are null.

---

## 2.5 Session — `http_session_handle`

Analogous to `requests.Session` in Python: persistent headers, cookie jar, and
TCP connection reuse (keep-alive) across multiple requests.

### Creation

```aiml
- decl: [s, http_session_handle]
- set: [s, { call: [http.session] }]
```

### Typical Example: Login + Protected API

```aiml
- decl: [s, http_session_handle]
- set: [s, { call: [http.session] }]

- call: [s.headers.put, "Accept", "application/json"]
- set: [s.timeout, 30000]

# Login — Set-Cookie from server is automatically stored in cookie jar
- decl: [login_url, url]
- set: [login_url, "https://app.example.com/login"]
- decl: [resp, http_response_handle]
- set: [resp, { call: [s.post, login_url, "{\"user\":\"alice\",\"pass\":\"secret\"}"] }]

# Follow-up request — session cookie is automatically sent
- decl: [data_url, url]
- set: [data_url, "https://app.example.com/api/data"]
- set: [resp, { call: [s.get, data_url] }]

- print: ["${resp.body}"]

- call: [s.close]
```

### `http_session_handle` — Members and Methods

**Members** (directly settable via `set`):

| Member    | Type                 | Description |
|-----------|----------------------|-------------|
| `headers` | `map<string:string>` | Persistent headers for all requests in the session |
| `timeout` | `i32`                | Default timeout in ms (0 = no timeout) |
| `cookies` | `map<string:string>` | Cookie jar — manually readable and writable |

**Methods**:

| Method            | Arguments                      | Return                 | Description |
|-------------------|--------------------------------|------------------------|-------------|
| `set_auth_bearer` | `token: string`                | —                      | Sets `Authorization: Bearer <token>` for all requests |
| `set_auth_basic`  | `user: string, pass: string`   | —                      | Sets `Authorization: Basic <b64>` for all requests |
| `clear_cookies`   | —                              | —                      | Clear cookie jar |
| `get`             | `u: url`                       | `http_response_handle` | GET with session context |
| `post`            | `u: url, body: string`         | `http_response_handle` | POST with session context |
| `put`             | `u: url, body: string`         | `http_response_handle` | PUT with session context |
| `delete`          | `u: url`                       | `http_response_handle` | DELETE with session context |
| `patch`           | `u: url, body: string`         | `http_response_handle` | PATCH with session context |
| `head`            | `u: url`                       | `http_response_handle` | HEAD with session context |
| `request`         | `method: string, u: url`       | `http_client_handle`   | Builder for complex requests with session context |
| `close`           | —                              | —                      | Close pooled connections, release session |

### Cookie Jar Behavior

- When a response contains a `Set-Cookie` header, the cookie is automatically stored in `cookies`
- Subsequent requests to the same domain automatically send all matching cookies
- Domain matching, path matching, and expiry are respected (HTTP cookie spec)
- `cookies.put(name, val)` allows manually pre-populating the jar (e.g. after token refresh)

### Relationship to `rest_client_handle`

`rest_client_handle` (Chapter 32.3) is built internally on top of `http_session_handle` and
adds: base URL, automatic `Content-Type: application/json`, and relative paths.

```
http_session_handle   ← cookie jar, keep-alive, persistent headers
        ↑
rest_client_handle    ← + base URL + auto JSON headers + relative paths
```

---

## 3. HTTP Server

### 3.1 Minimal Example

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

`req.method`, `req.path` und `req.body` sind immer `string`.  Fehlt der Teil in der
Anfrage — und eine Anfrage ohne `Content-Length`, also jedes GET, hat keinen Rumpf —, ist
das Ergebnis der **leere** String, nie ein Nullzeiger.  Bis 18.10.2026 gab `req.body` dort
null zurueck, und ein korrektes `- set: [body, req.body]` im GET-Handler starb mit SIGSEGV.

- decl: [srv, http_server_handle]
- set: [srv, { call: [http.server, "0.0.0.0", 8080] }]

- call: [srv.route, "GET",  "/",     handle_root]
- call: [srv.route, "POST", "/echo", handle_echo]

- print: ["Listening on :8080"]
- call: [srv.start]
```

### 3.2 Non-blocking Server with `start_async`

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

### 3.3 Server with One Thread per Client

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

### 3.4 `http_server_handle` Methods

| Method        | Arguments                                              | Return              | Description |
|---------------|--------------------------------------------------------|---------------------|-------------|
| `route`       | `method: string, path: string, handler: fn`            | —                   | Register route |
| `use`         | `middleware: fn`                                       | —                   | Global middleware (before every handler) |
| `group`       | `prefix: string, middleware: fn`                       | `http_route_handle` | Group with prefix + guard |
| `filesystem`  | `prefix: string, dirs: list<path>, opts: fs_options`   | —                   | Serve static files under `prefix` |
| `start`       | —                                                      | —                   | Blocking accept loop |
| `start_async` | —                                                      | `thread_handle`     | Non-blocking (starts thread) |
| `stop`        | —                                                      | —                   | Signals accept loop to stop |
| `set_header`  | `name: string, val: string`                            | —                   | Global default header for all responses |
| `set_timeout` | `secs: i32`                                            | —                   | Request-read timeout in seconds (slowloris defence; default 15) |
| `secure_headers` | `hsts: i32`                                         | —                   | Add nosniff / X-Frame-Options / Referrer-Policy (+ HSTS if `hsts != 0`) to every response |
| `require_client_cert` | `ca_path: string`                             | `i32`               | Enable mTLS: require + verify a client cert against the CA bundle PEM; `0` on success, `-1` on error (TLS server only) — see §3.11 |
| `set_max_connections` | `n: i32`                                      | —                   | Cap concurrent handler threads (`0` = unlimited); excess connections get `503` and close — see §3.12 |
| `set_max_body` | `bytes: i64` | — | Request body cap in bytes (default 8 MiB, clamped to ~2 GiB); larger bodies get `413` — see §3.10 |
| `set_spool` | `dir: string, threshold: i64, max: i64` | — | Bodies above `threshold` bytes go to a temp file in `dir` (`req.body_path`) instead of memory; `max` caps them (0 = `set_max_body` cap) — see §3.10a |
| `set_rate_limit`      | `max: i32, window_secs: i32`                  | —                   | Per-IP fixed-window limit; excess connections get `429` and close (`max <= 0` disables) — see §3.12 |

**Routing rules:**
- Exact paths: `"/api/v1/users"` — matches only this exact path
- Named segments: `"/users/:id"` — matches one segment, name accessible via `req.params`
- Wildcard suffix: `"/static/*"` — matches all paths with this prefix
- Method `"*"` matches all HTTP methods on the path
- Priority: exact > named > wildcard; for the same type: longer pattern first

### 3.4a `http_route_handle` Methods

`http_route_handle` is returned by `srv.group`. It holds an internal reference to the
root `http_server_handle` and automatically combines all paths with the prefix.
No own listener — `start`/`stop`/`start_async` are not present.

| Method       | Arguments                                            | Return              | Description |
|--------------|------------------------------------------------------|---------------------|-------------|
| `route`      | `method: string, path: string, handler: fn`          | —                   | Register route under prefix |
| `use`        | `middleware: fn`                                     | —                   | Middleware for this group |
| `group`      | `prefix: string, middleware: fn`                     | `http_route_handle` | Nested subgroup |
| `filesystem` | `prefix: string, dirs: list<path>, opts: fs_options` | —                   | Static files under prefix |

### 3.5 `http_request_handle` — Members and Methods (Server — Reading)

**Members** (directly readable):

| Member       | Type                 | Description |
|--------------|----------------------|-------------|
| `url`        | `url`                | Fully parsed request URL |
| `params`     | `map<string:string>` | Named path segments of the matched route |
| `method`     | `string`             | `"GET"`, `"POST"`, … |
| `path`       | `string`             | Path component of the URL, e.g. `"/api/users"` |
| `body`       | `string`             | Request body as string |
| `body_bytes` | `buffer<u8>`         | Request body as buffer (a spooled body is loaded from its file) |
| `body_path`  | `string`             | Temp file of a spooled body (`srv.set_spool`), else null — removed after the handler unless moved (`io.rename`) |
| `body_len`   | `i64`                | Exact body length in bytes (in memory or spooled) |

**Methods** (with argument):

| Method   | Arguments      | Return   | Description |
|----------|----------------|----------|-------------|
| `header` | `name: string` | `string` | Request header value; null if missing |
| `cookie` | `name: string` | `string` | Read request cookie; null if not present |
| `client_ip` | —           | `string` | Trusted socket peer IP (dotted); **ignores** `X-Forwarded-For`/`X-Real-IP` — see §3.13 |

### 3.6 `http_response_handle` — Members and Methods (Server — Writing)

**Members** (directly settable via `set`):

| Member       | Type                 | Description |
|--------------|----------------------|-------------|
| `status`     | `i32`                | HTTP status code (default: 200) |
| `headers`    | `map<string:string>` | Response headers; provides `put`, `get`, `remove` etc. |
| `body`       | `string`             | Response body as text |
| `body_bytes` | `buffer<u8>`         | Response body as bytes |
| `body_file`  | `path`               | File as body — streamed during `send` (sendfile, range-request-aware, ETag); a `content-type` set by the handler is kept, otherwise it is derived from the extension |

**Methods** (6):

| Method        | Arguments          | Description |
|---------------|--------------------|-------------|
| `send`        | —                  | Sends complete response (status + headers + body); sets `resp_sent` |
| `write`       | `chunk: string`    | First call: flushes headers, starts chunked transfer; subsequent calls: write chunk |
| `write_bytes` | `data: buffer<u8>` | Like `write`, but without userspace copy — passes `buf.ptr` directly to `send()` |
| `flush`       | —                  | Flushes buffer (SSE keepalive) |
| `close`       | —                  | Terminal chunk, ends chunked transfer; sets `resp_sent` |
| `set_cookie`  | `name, value: string, [max_age: i32], [same_site: string]` | Append a `Set-Cookie` with secure defaults (`Path=/; HttpOnly; Secure; SameSite`) — see §3.13 |

> **Note:** `resp_sent` is set by `send`, the first `write` call, and `close`.
> Middleware can use this to skip the handler without needing a return value.

**Cookies and redirects** are set via `headers`:

```aiml
# Set cookie
- call: [resp.headers.put, "Set-Cookie", "session=abc123; Path=/; HttpOnly; Max-Age=3600"]

# Redirect
- decl: [loc, string]
- set: [loc, { call: [login_url.to_string] }]
- call: [resp.headers.put, "Location", loc]
- set: [resp.status, 302]
- call: [resp.send]
```

### 3.7 Named Path Segments

Segments with `:name` in the route pattern are stored in `req.params` on match.

```aiml
- call: [srv.route, "GET",  "/users/:id",                  handle_user]
- call: [srv.route, "GET",  "/users/:id/orders/:order_id", handle_order]
- call: [srv.route, "POST", "/orgs/:org/repos",             handle_repos]
- call: [srv.route, "GET",  "/static/*",                   handle_static]
```

In the handler:

```aiml
- def:
    name: handle_order
    args: [[req, http_request_handle], [resp, http_response_handle]]
    body:
      # Named path segments
      - decl: [p, map<string:string>]
      - set: [p, req.params]

      - decl: [uid, string]
      - set: [uid, { call: [p.get, "id"] }]

      - decl: [oid, string]
      - set: [oid, { call: [p.get, "order_id"] }]

      # Query parameters via req.url
      - decl: [u, url]
      - set: [u, req.url]

      - decl: [page, string]
      - set: [page, { call: [u.get_param.get, "page"] }]

      - call: [resp.headers.put, "Content-Type", "application/json"]
      - set: [resp.status, 200]
      - set: [resp.body, "{\"user\": \"${uid}\", \"order\": \"${oid}\", \"page\": \"${page}\"}"]
      - call: [resp.send]
```

**Matching table:**

| Route pattern                      | Incoming path             | `params` content                        |
|------------------------------------|---------------------------|-----------------------------------------|
| `/users/:id`                       | `/users/42`               | `id` → `"42"`                           |
| `/users/:id/orders/:order_id`      | `/users/7/orders/99`      | `id` → `"7"`, `order_id` → `"99"`      |
| `/orgs/:org/repos`                 | `/orgs/acme/repos`        | `org` → `"acme"`                        |
| `/static/*`                        | `/static/css/main.css`    | *(empty, no named segment)*             |

### 3.8 Middleware and Authorization

Middleware functions have the same signature as handlers:
`args: [[req, http_request_handle], [resp, http_response_handle]]`.

Once middleware calls `resp.send()`, the first `resp.write()`, or `resp.close()`,
`resp_sent = true` is set internally — the actual handler is skipped.

#### `srv.use(fn)` — Global Middleware

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

#### `srv.group(prefix, middleware_fn)` — Protected Route Group

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

Public routes directly on `srv` (no guard):

```aiml
- call: [srv.route, "GET",  "/health",         handle_health]
- call: [srv.route, "POST", "/auth/login",      handle_login]
- call: [srv.route, "GET",  "/auth/callback",   handle_callback]
```

#### Example: Google OAuth 2.0

```aiml
# Step 1 — Redirect to Google
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

# Step 2 — Callback: exchange code for token
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

      # Set session cookie, redirect back to app
      - call: [resp.headers.put, "Set-Cookie", "session_token=${token_json}; Path=/; HttpOnly; Secure; SameSite=Strict; Max-Age=3600"]
      - decl: [home, url]
      - set: [home, "/dashboard"]
      - decl: [home_loc, string]
      - set: [home_loc, { call: [home.to_string] }]
      - call: [resp.headers.put, "Location", home_loc]
      - set: [resp.status, 302]
      - call: [resp.send]
```

### 3.9 Static Files — `srv.filesystem`

`srv.filesystem` serves files from one or more directories.
The directories are searched in order — first match wins.
File resolution (`<dir> + request_path`) happens dynamically per request;
the `list<path>` is passed once at startup.

#### `fs_options` — Members

| Member          | Type     | Default         | Description |
|-----------------|----------|-----------------|-------------|
| `index`         | `path`   | `"index.html"`  | File served for directory requests |
| `spa`           | `bool`   | `false`         | All unknown paths → `index` file (single-page app) |
| `cache_max_age` | `i32`    | `0`             | `Cache-Control: max-age=N` in seconds; 0 = no cache header |

#### Example: SPA + Assets

```aiml
- decl: [dirs, list<path>]
- call: [dirs.push, "/var/www/dist"]

- decl: [opts, fs_options]
- set: [opts.spa, true]
- set: [opts.cache_max_age, 3600]

- call: [srv.filesystem, "/", dirs, opts]
- call: [srv.start]
```

#### Example: Multiple Directories (Fallback Chain)

```aiml
- decl: [dirs, list<path>]
- call: [dirs.push, "/var/www/theme"]     # theme overrides first
- call: [dirs.push, "/var/www/default"]   # then defaults

- decl: [opts, fs_options]

- call: [srv.filesystem, "/static", dirs, opts]
```

#### Byte-Range Requests (resumable / chunked downloads)

Served files support HTTP byte ranges. Every response advertises
`Accept-Ranges: bytes`. A request carrying a single `Range: bytes=START-END`
header is answered with **`206 Partial Content`** — `Content-Range: bytes
START-END/TOTAL` and a `Content-Length` covering just the requested slice.
`START` and `END` may each be omitted (`bytes=1000-` = from 1000 to the end;
`bytes=-500` = the last 500 bytes). A range wholly past the end returns
**`416 Range Not Satisfiable`** with `Content-Range: bytes */TOTAL`. Multi-range
requests (a comma-separated set) are not split — the whole file is sent as `200`.

This lets clients **stream a large asset in chunks** instead of one giant fetch.
The WASM client does exactly this for its multi-hundred-MB `client.data` bundle:
a single `fetch().arrayBuffer()` of the whole file is buffered twice (stream +
final `ArrayBuffer` ≈ 2× peak) and mobile browsers abort it under memory
pressure (the server then logs a mid-stream broken pipe and the page stays
black). Downloading 8 MB ranged chunks into one pre-allocated buffer keeps peak
memory at ≈ `total + one chunk` and makes a transient abort cost a single
chunk retry rather than the entire download. Test: `tests/test_http_range.aiml`.

#### Example: Protected Downloads via `srv.group`

```aiml
- decl: [secure_dirs, list<path>]
- call: [secure_dirs.push, "/data/downloads"]

- decl: [opts, fs_options]

- decl: [secure, http_route_handle]
- set: [secure, { call: [srv.group, "/downloads", require_auth] }]
- call: [secure.filesystem, "/", secure_dirs, opts]
```

> **Note:** `filesystem` combines with `srv.group` for middleware authorization.
> Range requests (`Content-Range`) and `ETag`/`Last-Modified` are handled automatically.

> **Path-traversal safety:** the request path is URL-decoded and then rejected if it
> contains `..`, a NUL byte, a backslash, or a leading `/` → `400 Bad Request`. As a
> definitive backstop the resolved file is `realpath`-canonicalized and must lie under
> the served directory, so **symlinks cannot escape the root**. Verified by
> `tests/test_http_traversal.aiml` (`../`, `%2e%2e`, missing → 400/400/404).

---

## 3.10 Security Hardening (secure defaults)

The server applies conservative limits so a single malicious/broken client cannot
exhaust memory or pin a handler thread. Malformed or oversized requests get a clean
protocol error and are **not** dispatched to any handler.

| Condition | Limit (default) | Response |
|---|---|---|
| Request line / URI too long | 8 KiB | `414 URI Too Long` |
| Too many header lines | 100 | `431 Request Header Fields Too Large` |
| Total header bytes | 64 KiB | `431 Request Header Fields Too Large` |
| Malformed / negative `Content-Length` | — | `400 Bad Request` |
| `Content-Length` over cap | 8 MiB (`srv.set_max_body`) | `413 Payload Too Large` |
| Slow/stalled request read (slowloris) | `set_timeout`, default **15 s** | `408 Request Timeout` |
| Stalled TLS handshake (`http.server_tls`) | `set_timeout`, default **15 s** | connection closed |

- The read timeout is both a per-`recv` socket timeout **and** a total request-read
  deadline, so byte-at-a-time drip-feeding cannot hold a connection open indefinitely.
  Tune it with `srv.set_timeout(secs)` (e.g. `1` in tests, `15`–`30` in production).
- The accept loop never reads from a client. On `http.server_tls` the TLS handshake runs
  in the connection's handler thread with the same timeout as a **total** handshake
  deadline, so a client that connects and sends nothing (or drip-feeds its handshake)
  cannot block other connections or hold a connection slot beyond the timeout.
- A validated `Content-Length` is required before the body buffer is allocated — an
  attacker cannot make the server allocate an arbitrary amount of memory.

```aiml
- call: [srv.set_timeout, 20]     # 20s read timeout for public hosting
```

Services that accept large uploads (artifact repositories, file drops) raise the
body cap explicitly. The body is stored binary-safe; read it via `req.body_bytes`
(`buffer<u8>`) — `req.body` is a string view that ends at the first NUL byte.

```aiml
- call: [srv.set_max_body, 1073741824]   # accept uploads up to 1 GiB
```

**Security response headers** (opt-in): `srv.secure_headers(hsts)` adds a conservative
browser-facing bundle to **every** response — normal, middleware-short-circuit, **and**
parser-reject errors:

| Header | Value |
|---|---|
| `X-Content-Type-Options` | `nosniff` |
| `X-Frame-Options` | `DENY` |
| `Referrer-Policy` | `no-referrer` |
| `Strict-Transport-Security` | `max-age=31536000; includeSubDomains` (only when `hsts != 0`; use behind HTTPS) |

Values already set via `set_header` win (put-if-absent), so any of these can be overridden.
Opt-in because e.g. `X-Frame-Options: DENY` breaks legitimate framing and HSTS is sticky.

```aiml
- call: [srv.secure_headers, 1]   # nosniff + X-Frame-Options + Referrer-Policy + HSTS
```

See `tests/test_http_hardening.aiml` (413/400/431/408), `tests/test_http_traversal.aiml`
(path traversal), `tests/test_http_secure_headers.aiml` (headers on 200 + errors),
`tests/test_http_tls_handshake_stall.aiml` (silent client does not block HTTPS).

## 3.10a Request bodies: chunked, spooled, HEAD; connection handling

- **Transfer-Encoding: chunked** request bodies are decoded (chunk extensions
  and trailers ignored) under the same cap and read deadline as Content-Length
  bodies.  `Content-Length` **and** `Transfer-Encoding` together are refused
  with `400` (request-smuggling ambiguity, RFC 9112 §6.1); any coding other than
  `chunked` gets `501`; a malformed chunk size gets `400`.
- **Spooling** (`srv.set_spool(dir, threshold, max)`): a body larger than
  `threshold` — or a chunked body once it grows past it — is written to a temp
  file in `dir` while it is read, never held in memory.  The handler sees
  `req.body_path` (and `req.body_len`); `req.body` is null.  Keep the upload by
  moving the file (`io.rename`), otherwise it is deleted when the request ends.
  `max` caps spooled bodies (`413` above it); a write error gives `507`.
- **HEAD** requests get the same status and headers as GET — including the
  `Content-Length` of a `body_file` — but no body (RFC 9110 §9.3.2).
- The status line carries the standard reason phrase (`HTTP/1.1 202 Accepted`).
- **One request per connection**: every response carries `connection: close`
  and the server closes afterwards.  Without the header HTTP/1.1 clients (Go's
  `net/http`: docker, skopeo, podman) keep the connection and send their next
  request on it, then read EOF.
- A **TLS ClientHello** (first byte `0x16`) on a plain `http.server` is closed
  at once without a response, so clients that try https first fall back to
  http immediately rather than after their handshake timeout.

```aiml
- call: [srv.set_max_body, 8388608]                          # in-memory cap
- call: [srv.set_spool, "/var/tmp/uploads", 1048576, 10737418240]   # >1 MiB → file, ≤10 GiB
```

See `tests/test_http_chunked_spool.aiml`.

## 3.11 Mutual TLS — `srv.require_client_cert`

By default an HTTPS server authenticates itself to the client but accepts any client.
`srv.require_client_cert(ca_path)` turns on **mutual TLS**: every client must present a
certificate signed by the CA bundle at `ca_path`, or the TLS handshake is rejected.
Use it for machine-to-machine APIs and internal services where clients hold issued certs.

- Must be called on a server created with `http.server_tls` (a plain `http.server` has no
  `SSL_CTX`); returns `0` on success, `-1` if the server is non-TLS or the CA cannot be loaded.
- `ca_path` is a PEM bundle of the CA(s) allowed to issue client certs — typically **not**
  a public CA, but your own internal issuing CA.
- Verification uses `SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT`, so a client with no
  cert cannot complete the handshake.

```aiml
- decl: [srv, http_server_handle]
- set: [srv, { call: [http.server_tls, "0.0.0.0", 8443, cert, key] }]
- decl: [rc, i32, { call: [srv.require_client_cert, "/etc/myapp/client-ca.crt"] }]
# rc == 0 → mTLS active; every request must now carry a CA-signed client cert
- call: [srv.route, "GET", "/", handle_root]
- call: [srv.start]
```

> **TLS 1.3 note:** with TLS 1.3, `SSL_connect` on a client that omits its certificate may
> return *before* the server evaluates client auth (client auth completes after the client's
> Finished). The rejection then surfaces on the client's first read — a cert-less client can
> open the socket but never receives a valid response. `tests/test_http_mtls.aiml` asserts
> exactly this: a cert-bearing client gets `200`, a cert-less client gets no valid response.

## 3.12 Abuse Resistance (connection caps + rate limiting)

The server uses one thread per connection. Two opt-in controls keep that model
survivable under load and against floods. Both reject **before** a handler thread or
TLS handshake runs, and both log the rejection to `stderr`.

| Method | Arguments | Effect |
|---|---|---|
| `set_max_connections` | `n: i32` | At most `n` concurrent handler threads; over the cap a new connection is rejected (`503 Service Unavailable` on plain HTTP, immediate close on TLS). `0` = unlimited. |
| `set_rate_limit` | `max: i32, window_secs: i32` | Per-IP fixed window: at most `max` connections per `window_secs` from one client IP; excess gets `429 Too Many Requests` (plain HTTP) / close (TLS). `max <= 0` disables; `window_secs <= 0` → 60. |

```aiml
- call: [srv.set_max_connections, 512]   # bound thread growth
- call: [srv.set_rate_limit, 60, 60]     # ≤ 60 requests/minute per IP
```

- The connection cap is an atomic live-thread counter, decremented when each handler
  returns — sequential traffic (each connection closes before the next) is never
  falsely rejected; only genuine concurrency beyond `n` is shed.
- The rate limiter is a fixed window keyed by client IP; the window itself acts as the
  cooldown. Its table is pruned of stale windows when it grows, so a flood of unique
  source IPs cannot grow it without bound.
- On a TLS listener the client speaks TLS first, so a rejected connection is closed
  rather than sent a plaintext status; the plaintext `503`/`429` applies to `http.server`.
- These stack with the slowloris read timeout (§3.10): slow clients time out, floods are
  rate-limited, and total concurrency is capped.

See `tests/test_http_rate_limit.aiml` (3 allowed then `429`; cap does not block sequential traffic).

## 3.13 Auth / Session Primitives

First-class helpers for the secure patterns the REST docs already imply, so auth code
does not rely on ad-hoc string handling.

### Secure cookies — `resp.set_cookie`

`resp.set_cookie(name, value, [max_age], [same_site])` appends a `Set-Cookie` header with
safe-by-default attributes: **`Path=/; HttpOnly; Secure; SameSite`**.

- `max_age` (seconds): `>= 0` → persistent cookie with `Max-Age`; omitted or `< 0` → session cookie.
- `same_site`: `"Strict"` / `"Lax"` / `"None"`; omitted or invalid → `"Lax"`.
- `HttpOnly` and `Secure` are **always** set (JS cannot read the cookie; it is sent over HTTPS
  only). For a deliberately JS-readable cookie, set the header manually via `resp.headers`.
- Multiple calls emit multiple `Set-Cookie` lines (the header map holds one value per key, so
  cookies are tracked separately).
- **Header-injection safe:** a `name`/`value` containing control chars or cookie delimiters
  (`; , space CR LF`) is rejected (the call is a no-op) — an attacker-controlled value cannot
  smuggle extra headers (HTTP response splitting).

```aiml
- decl: [token, string, { call: [math.random_hex, 32] }]   # CSPRNG session id (spec 43_0)
- call: [resp.set_cookie, "sid", token, 3600, "Strict"]     # persistent, 1h, strict
- call: [resp.set_cookie, "theme", "dark"]                   # session cookie, SameSite=Lax
```

### Constant-time secret compare — `hash.const_time_eq`

`hash.const_time_eq(a, b) → i32` returns `1` if the two strings are equal, `0` otherwise,
using `CRYPTO_memcmp` so the comparison time does not leak how many leading bytes matched.
Use it in auth middleware to compare session tokens / API keys / HMAC tags — a normal `eq`
short-circuits on the first mismatching byte and is exploitable by timing.

```aiml
- decl: [ok, i32, { call: [hash.const_time_eq, presented_token, stored_token] }]
- if: { condition: { neq: [ok, 1] }, then: [ { set: [resp.status, 401] }, { call: [resp.send] } ] }
```

### Trusted client IP — `req.client_ip`

`req.client_ip()` returns the **socket peer IP** captured at accept time. It deliberately
**ignores `X-Forwarded-For` / `X-Real-IP`**: those headers are attacker-controlled and must
never be trusted unless the app explicitly reads and validates them behind a known trusted
proxy. This gives rate-limiting/audit/authorization code a spoof-proof source address by
default; read `req.header("x-forwarded-for")` yourself only when you control the proxy in front.

```aiml
- decl: [ip, string, { call: [req.client_ip] }]   # e.g. "203.0.113.7" — never the XFF value
```

See `tests/test_http_auth_helpers.aiml` (secure cookie attributes + injection guard + client_ip
ignores XFF + `const_time_eq`).

---

## 4. HTTPS Server

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

`http.server_tls` combines `tls.*` internally for the transport with the same
HTTP/1.1 parser as `http.server`. `cert` may hold the full chain (leaf first, then intermediates, e.g. Let's Encrypt `fullchain.pem`); the whole chain is sent to clients, so clients that do not cache intermediates can verify it.

**Secure TLS defaults:** the `SSL_CTX` enforces a **minimum of TLS 1.2** (TLS 1.3 is
negotiated automatically when the client supports it); SSLv2/SSLv3/TLS 1.0/TLS 1.1 and
TLS-layer compression (CRIME) are disabled, and the server cipher preference is honoured.
The same parser limits and read timeout as `http.server` (§3.10) apply over TLS.

This exact policy is a single shared helper (`aiml_tls_harden_server_ctx`) applied to
**both** the HTTPS server and the WebSocket-over-TLS server (`wss.server`, chapter 32_4),
so the two transports never drift apart. For client-certificate authentication, see
§3.11 (`srv.require_client_cert`).

---

## 5. Namespace Reference

### `url` Namespace

| Call | Return | Description |
|------|--------|-------------|
| `{ call: [url.parse, s] }` | `url` | Parses and validates URL string |

### `http` Namespace

| Call | Return | Description |
|------|--------|-------------|
| `{ call: [http.session] }` | `http_session_handle` | New persistent session (cookie jar, keep-alive) |
| `{ call: [http.get, u] }` | `http_response_handle` | Single GET without session (`u: url`) |
| `{ call: [http.fetch_text, url_str] }` | `string` | One-shot GET → body text; takes a string URL; WASM-capable (browser `fetch`) |
| `{ call: [http.post, u, body] }` | `http_response_handle` | Single POST without session (`u: url`) |
| `{ call: [http.put, u, body] }` | `http_response_handle` | Single PUT without session (`u: url`) |
| `{ call: [http.delete, u] }` | `http_response_handle` | Single DELETE without session (`u: url`) |
| `{ call: [http.patch, u, body] }` | `http_response_handle` | Single PATCH without session (`u: url`) |
| `{ call: [http.head, u] }` | `http_response_handle` | Single HEAD without session (`u: url`) |
| `{ call: [http.new_request, method, u] }` | `http_client_handle` | Client request builder (`u: url`) |
| `{ call: [http.server, host, port] }` | `http_server_handle` | Plain HTTP server |
| `{ call: [http.server_tls, host, port, cert, key] }` | `http_server_handle` | HTTPS server (`cert, key: path`) |

---

## 6. Implementation Notes

### 6.1 URL Parsing

Internal C structure (`AimlUrl`):

```cpp
struct AimlUrl {
    char* protocol;  // e.g. "https", "ws", "ftp" — NULL if not set
    char* host;      // NULL if missing
    int   port;      // 0 = protocol default
    char* path;      // NULL if missing
    char* fragment;  // NULL if none
    AimlMap* get_param; // query parameters as map — NULL if none present
                        // lazily parsed on first access to get_param
};
```

Every incoming HTTP request is automatically parsed into an `AimlUrl` and made
available via `req.url`.

### 6.2 HTTP/1.1 Parser (`http_runtime.cpp`)

- Request line: `GET /path?query HTTP/1.1\r\n`
- Headers: `Name: Value\r\n` until `\r\n\r\n`
- Body: `Content-Length`-based (no chunked transfer encoding in v1)

### 6.3 No External Dependencies

All HTTP operations run through `net.*` / `tls.*` — no libcurl, no libuv.

### 6.5 Zero-Copy Paths

| Case | Mechanism | Userspace copy |
|------|-----------|----------------|
| `resp.body_file` + `send` | `sendfile(out_fd, in_fd, ...)` — kernel page cache → socket | none |
| `resp.body_bytes` + `send` | `send(fd, buf.ptr, buf.len, 0)` | none |
| `resp.write_bytes(buf)` | `send(fd, buf.ptr, buf.len, MSG_MORE)` | none |
| `resp.write(chunk)` | `send(fd, chunk.ptr, chunk.len, MSG_MORE)` | none (string is already `char*`) |
| `resp.body` + `send` | `send(fd, body.ptr, body.len, 0)` | none |
| `{ call: [store.raw_bytes] }` + `write_bytes` | View into store memory → `send(fd, ptr, ...)` | none (value-type structs only) |

HTTPS path (`tls.*`): `SSL_write` copies internally into the TLS record buffer — one userspace copy is unavoidable for encrypted connections.

### 6.4 Keep-Alive

HTTP/1.1 keep-alive is not active in v1 — each request opens and closes
a new TCP connection.

---

## 7. Open Design Questions

- **HTTP/2**: ALPN + binary framing — long-term extension.
- **Compression**: `Accept-Encoding: gzip` via `zlib`.
- **Static file serving**: `srv.serve_static("/", "/var/www")`.
- **WebSocket upgrade**: `resp.upgrade_ws(...)` — defined in Chapter 32.4.
- **Multipart cookies**: `resp.headers` is `map<string:string>` (one value per key).
  For multiple `Set-Cookie` headers in one response: consider `map<string:list<string>>` or
  a dedicated `cookies` member (v1 limitation).
- **DB streaming**: Whether zero-copy is possible depends on struct contents:
  - **Primitives only** (`i32`, `f32`, …): The stored bytes are directly streamable.
    `{ call: [store.raw_bytes] }` returns a `buffer<u8>` that points directly into the internal
    store memory (no copy); then `write_bytes` → `send`. Missing in v1:
    `raw_bytes` method and handling of inactive slots (send bitmap alongside or compact).

    ```aiml
    - decl: [raw, buffer<u8>]
    - set: [raw, { call: [positions.raw_bytes] }]
    - call: [resp.write_bytes, raw]
    - call: [resp.close]
    ```

  - **Pointer fields** (`string`, handles): Raw data contains process-local addresses —
    not network-transferable, serialization remains necessary.
