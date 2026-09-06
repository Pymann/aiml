# Security Hardening Implementation Plan

Status: in progress
Date: 2026-08-25

## Progress log (2026-08-25)

- **[DONE] Priority A #1 — TLS defaults** (`aiml_http_server_tls`, http_runtime.cpp):
  modern one-time `OPENSSL_init_ssl` (was legacy `SSL_library_init`), `SSL_CTX_set_min_proto_version(TLS1_2)`,
  `SSL_OP_NO_SSLv2/3|NO_TLSv1|NO_TLSv1_1|NO_COMPRESSION|CIPHER_SERVER_PREFERENCE`. TLS 1.0/1.1 now rejected;
  1.2/1.3 succeed; `tests/test_tls*` still green.
- **[DONE] Priority A #2 — explicit mTLS/CA config**: extracted the hardening policy into a shared
  `tls_runtime.cpp` helper `aiml_tls_harden_server_ctx(SSL_CTX*)` (single source of truth) and added
  `aiml_tls_require_client_cert(SSL_CTX*, ca_path)` (`SSL_VERIFY_PEER|FAIL_IF_NO_PEER_CERT` +
  `SSL_CTX_load_verify_locations`). Exposed to AIML as **`srv.require_client_cert(ca_path)`** on the HTTPS
  server (`aiml_http_server_require_client_cert`, returns 0/-1). `http.server_tls` now calls the shared
  helper instead of inlining flags. Spec: 32_1 §6.2a, 32_2 §3.11 + methods table. Test:
  `tests/test_http_mtls.aiml` — cert-client → 200, cert-less client → no valid response (TLS 1.3 post-
  handshake reject), bad-CA path → -1. New fixtures `tests/tls_certs/{ca,client}.{crt,key}`.
- **[DONE] Priority A #3 — WSS inherits tightened defaults** (`aiml_wss_server`, ws_runtime.cpp): now calls
  the same `aiml_tls_harden_server_ctx` and **checks cert/key load return values** (previously ignored — a
  bad cert would start a listener that fails every handshake). Covered by the existing `tests/test_ws.aiml`
  Phase C wss loopback (still green through the hardened path). Spec: 32_4 §3.3.
- **[DONE] Priority B — HTTP parser limits** (`parse_http_request`): `Content-Length` validated via
  `strtol` (malformed → **400**, negative → 400, `> 8 MiB` → **413**) — kills the unbounded
  `vector<char>(content_length)` OOM; header count (100) + total header bytes (64 KiB) → **431**;
  over-long request line → **414**. `handle_connection` sends the protocol error and skips dispatch.
  Verified with raw malformed/oversized requests (413/400/431/414 as expected); http/routes/ws/tls green.
- **[BUGFIX] Use-after-free in `aiml_http_resp_body`** (+ all `aiml_http_req_*` getters): returned the
  internal `->body`/`->method`/… pointer, which dangled after `resp.free()` / request free (garbage body —
  `tests/test_http_routes` was failing). Now return a **refcounted** copy via `aiml_str_dup` (AIML `decref`
  frees it → no leak; `xstrdup`/plain `strdup` would have leaked since AIML never frees non-refcounted returns).

- **[DONE] Priority E (auth/session primitives)** (http_runtime.cpp + hash_runtime.cpp):
  **`resp.set_cookie(name, value, [max_age], [same_site])`** builds a `Set-Cookie` with secure
  defaults (`Path=/; HttpOnly; Secure; SameSite`, Lax default; `max_age<0` = session) — stored in a
  new `AimlHttpResponse.set_cookies[]` (map holds one value/key, so multiple cookies need their own
  lines) and **guarded against header injection** (rejects control chars / cookie delimiters in
  name/value). **`req.client_ip()`** returns the trusted socket peer IP captured at accept time and
  **ignores X-Forwarded-For** (spoof-proof by default; apps opt into XFF explicitly). **`hash.const_time_eq(a,b)`**
  → i32 constant-time compare (`CRYPTO_memcmp`) for tokens/HMAC tags. Secure cookie *values* come from
  the existing `math.random_hex` CSPRNG. Builtins: `aiml_http_resp_set_cookie`, `aiml_http_req_client_ip`,
  `aiml_hash_const_time_eq`. Spec 32_2 §3.13 + 33_1; **`tests/test_http_auth_helpers.aiml`** (cookie
  attributes + injection guard + client_ip ignores XFF + const_time_eq).
- **[DONE] Priority D (connection caps + per-IP rate limiting)** (`accept_loop`, http_runtime.cpp):
  `srv.set_max_connections(n)` caps concurrent handler threads via an atomic live counter
  (`AimlHttpServer.active_conns`, decremented by a `ConnGuard` RAII on every handler return path);
  over the cap → **503** (plain HTTP) / close (TLS). `srv.set_rate_limit(max, window_secs)` is a
  per-IP fixed-window limiter (`unordered_map<ip,(window_start,count)>` under a mutex, pruned when
  it grows) → over quota **429** / close. Both reject **before** thread spawn or TLS handshake and
  log to stderr (task 4). Builtins `aiml_http_server_set_max_connections` / `set_rate_limit`. Spec
  §3.12 + methods table; **`tests/test_http_rate_limit.aiml`** (3 allowed then 429; cap does not
  block sequential traffic). Slowloris timeout (below) + partial-header cap (64 KiB, Priority B)
  already covered tasks 3.
- **[DONE] Priority D (read timeouts) — slowloris defence**: per-`recv` `SO_RCVTIMEO` on the
  connection + a total request-read **deadline** (`time()`), both driven by a configurable
  `srv.set_timeout(secs)` (`AimlHttpServer.read_timeout_secs`, default 15; new builtin +
  `aiml_http_server_set_timeout`). Stalled/partial/drip-fed requests get **408** instead of
  pinning the handler thread. Fixed the subtle bug where a recv-timeout (`recv_line` → 0) was
  mistaken for the terminating blank line (request wrongly dispatched as complete). Spec §3.10
  updated; **`tests/test_http_hardening.aiml`** added (raw-socket probes: 200/413/400/431/408, all green).

- **[DONE] Priority B (path-traversal safe static serving)** (`fs_handle`/`resolve_under`):
  request path is now **URL-decoded before** the traversal check (so `%2e%2e`/`%00` cannot
  slip past); `path_is_safe` rejects `..`, NUL, backslash, leading `/` → 400; and a
  `realpath` containment backstop (`path_within_root`) confirms the resolved file lives under
  the served dir → **symlink escapes blocked**. Spec §3.9/§3.10 updated;
  **`tests/test_http_traversal.aiml`** added (`../`/`%2e%2e`/missing → 400/400/404, all green).

- **[DONE] Priority C (security response headers)**: `srv.secure_headers(hsts)` +
  `aiml_http_server_secure_headers` adds nosniff / X-Frame-Options DENY / Referrer-Policy
  (+ HSTS when `hsts != 0`) via `default_headers` (put-if-absent, overridable by `set_header`).
  default_headers are now also copied onto **parser-reject error responses** so the bundle is
  consistent across normal + short-circuit + error paths. Spec §3.4/§3.10 updated;
  **`tests/test_http_secure_headers.aiml`** added (headers on 200 AND 413, green).

**Minimal First Milestone (plan §6) COMPLETE: #1 TLS defaults, #2 parser limits, #3 read
timeouts, #4 traversal-safe FS, #5 default nosniff/HSTS — all with spec + tests.**

**T2/T3 COMPLETE (spec + test):**
- **T2** `hash.pbkdf2(password, salt, iterations)` → PBKDF2-HMAC-SHA256, 32-byte key / 64 hex
  (OpenSSL `PKCS5_PBKDF2_HMAC`, iterations `< 1` floored to 100000). Spec `spec/33_1_hash.md` §3.8.
  RFC known-answer verified: `pbkdf2("password","salt",1)` = `120fb6cf…be17b`.
- **T3** `math.random_hex(nbytes)` → CSPRNG hex (OpenSSL `RAND_bytes`), default 16, clamped 1..1024.
  Placed in the **`math`** namespace (with `math.random`/`random_int`), spec `spec/43_0_math.md` §7.
  Impl lives in `hash_runtime.cpp` (symbol `aiml_math_random_hex`) — the WASM runtime compiles
  `math_runtime.cpp` without OpenSSL, so the crypto-random impl must stay out of that file.
- Test: `tests/test_pbkdf2_random.aiml` (known-answer, determinism, salt-sensitivity, length, distinctness).
- Note: string equality in AIML is `{ eq: [a, b] }` — there is no `.equals` string method.

**All priorities (A–E) are now DONE.** A (TLS defaults + mTLS/CA + WSS), B (parser limits +
traversal), C (secure headers), D (timeouts + connection caps + per-IP rate limiting), E
(secure cookies + client_ip + constant-time compare) — each with spec + tests.
See `doc/security_hardening.md` (in the 9rooms_v2 repo) for the app-side view.

This file is intentionally placed next to the referenced `doc/security_hardening.md`.
In the current checkout, `doc/security_hardening.md` is not present, so this plan
captures the implementation path beside the expected location and anchors it to the
already existing AIML runtime.

## 1. Existing Building Blocks We Should Reuse

The hardening work should start from the current implementation rather than
re-specifying infrastructure that already exists.

### Already implemented transport/security pieces

- TLS runtime already exists in `src/sys/tls_runtime.cpp`.
- TLS uses OpenSSL directly via `libssl` and `libcrypto`.
- `tls.connect()` already sets SNI with `SSL_set_tlsext_host_name(...)`.
- Hostname verification is already enabled with `SSL_set1_host(...)`.
- Peer validation helpers already exist:
  - `aiml_tls_peer_cert_cn`
  - `aiml_tls_peer_cert_valid`
- HTTP server TLS entrypoint already exists in `src/sys/http_runtime.cpp`:
  - `aiml_http_server_tls(...)`
- Compiler/runtime type hooks already exist:
  - `tls_socket_handle` in `src/core/context.h`
  - `http_request_handle` in `src/core/context.h`
  - TLS/HTTP builtins in `src/core/builtin_registry.cpp`

### Already imported / linked libraries

- OpenSSL headers are already used in:
  - `src/sys/tls_runtime.cpp`
  - `src/sys/http_runtime.cpp`
  - `src/sys/ws_runtime.cpp`
  - `src/sys/hash_runtime.cpp`
- Compression libraries already present and usable for HTTP hardening:
  - Brotli
  - Zstd
  - LZ4
- Threading is already present via `pthread`.
- `exodus.json` already includes the relevant native packages and libraries:
  - `libssl-dev`
  - `libbrotli-dev`
  - `libzstd-dev`
  - `liblz4-dev`
  - `pthread`

### Existing behavioral baseline

- HTTP server already supports:
  - sync start
  - async start
  - per-connection threads
  - middleware
  - route groups
  - TLS listener creation
- Specs already define the intended public contract:
  - `spec/32_1_network_tls.md`
  - `spec/32_2_network_http_s.md`
  - `spec/32_3_network_rest_api.md`
  - `spec/47_thread_safety.md`

## 2. Main Gaps To Harden

The major remaining work is not "add HTTPS". HTTPS is already there. The real gaps
are around secure defaults, parser/resource limits, abuse resistance, and testable
operational behavior.

### Priority A: TLS/server defaults

Goals:

- enforce secure protocol defaults
- remove weak/default OpenSSL behavior
- make server-side verification options explicit

Tasks:

1. In `src/sys/http_runtime.cpp`, harden `aiml_http_server_tls(...)`:
   - switch from legacy-style `SSL_library_init()` setup to the same one-time
     OpenSSL initialization model used in `src/sys/tls_runtime.cpp`
   - set minimum TLS version to TLS 1.2
   - prefer TLS 1.3 automatically when available
   - disable compression at TLS layer
2. Add explicit server TLS config helpers where needed:
   - optional client cert verification for mTLS
   - optional CA bundle override for server-side client-cert validation
3. Ensure WebSocket-over-TLS paths use the same tightened defaults.

Acceptance:

- TLS 1.0/1.1 handshakes fail
- TLS 1.2/1.3 succeed
- existing `tls.*` tests still pass

### Priority B: HTTP parser hardening

Goals:

- prevent oversized or malformed requests from consuming unbounded memory
- normalize ambiguous inputs early

Tasks:

1. Add hard limits in `src/sys/http_runtime.cpp` for:
   - request line length
   - total header bytes
   - header count
   - body size
   - URL/path length
2. Reject malformed input deterministically:
   - invalid `Content-Length`
   - duplicate conflicting `Content-Length`
   - unsupported transfer framing
   - invalid header syntax
3. Return clean protocol responses:
   - `400 Bad Request`
   - `413 Payload Too Large`
   - `431 Request Header Fields Too Large`
4. Normalize or reject dangerous path forms before filesystem serving:
   - `..`
   - repeated slash tricks
   - encoded traversal variants

Acceptance:

- malformed requests do not crash server threads
- oversized requests are rejected early
- filesystem routes cannot escape configured roots

### Priority C: Response hardening and secure defaults

Goals:

- ship safer default browser-facing behavior for HTTPS/WSS deployments

Tasks:

1. Add optional default security headers at server level:
   - `Strict-Transport-Security`
   - `X-Content-Type-Options: nosniff`
   - `X-Frame-Options: DENY` or configurable equivalent
   - `Referrer-Policy`
2. Keep this configurable so non-browser API users are not broken.
3. For static file serving, set conservative content-type fallback and disable
   dangerous sniffing assumptions.

Acceptance:

- HTTPS server can enable secure defaults with one configuration path
- headers are applied consistently across normal, middleware-short-circuit, and error responses

### Priority D: Resource exhaustion / abuse resistance

Goals:

- make the current thread-per-connection model survivable under load

Tasks:

1. Add connection/accounting limits:
   - max concurrent connections
   - max concurrent requests per server
   - idle header-read timeout
   - body-read timeout
2. Add simple rate limiting primitives:
   - per-IP request counters
   - token bucket or fixed window
   - configurable ban/cooldown window
3. Protect slowloris-style behavior:
   - timeout while reading request line and headers
   - cap partially received header state
4. Make logging/metrics hooks visible for rejected requests.

Acceptance:

- slow clients time out
- large numbers of idle clients do not create unbounded thread/resource growth

### Priority E: Auth/session primitives for REST usage

Goals:

- support common secure patterns already implied by the REST docs

Tasks:

1. Add first-class helpers for:
   - secure cookie generation/parsing
   - SameSite/HttpOnly/Secure cookie attributes
   - constant-time token compare for auth middleware
2. Add request helpers for trusted proxy handling only when explicitly configured.
3. Avoid silently trusting forwarded headers by default.

Acceptance:

- auth middleware examples no longer depend on ad-hoc string handling only
- cookie defaults are safe on HTTPS

## 3. Recommended Implementation Order

### Phase 1: Tighten what already exists

- harden `src/sys/http_runtime.cpp`
- reuse existing OpenSSL/TLS runtime choices
- add parser bounds
- add timeout handling
- add safer default headers

This phase gives the best security gain with the least API churn.

### Phase 2: Add explicit server TLS/auth configuration

- mTLS controls for `http.server_tls`
- CA override support
- cookie/auth helpers
- proxy-trust configuration

### Phase 3: Abuse resistance and operational controls

- rate limiting
- connection caps
- request accounting
- better rejection logging

## 4. Concrete File-Level Work

### `src/sys/http_runtime.cpp`

Primary hardening target.

- centralize TLS init
- add SSL_CTX hardening flags
- add parser byte/count limits
- add read timeouts
- add safer filesystem path validation
- add default security headers
- add connection/rate-limit bookkeeping

### `src/sys/tls_runtime.cpp`

Reuse and align behavior.

- keep current SNI + hostname verification
- mirror minimum-version / no-compression policy
- add any shared TLS helper used by both direct TLS sockets and HTTP TLS server

### `src/sys/ws_runtime.cpp`

- verify WSS path inherits the same TLS defaults
- ensure HTTP upgrade path respects parser/size/time limits

### `src/core/builtin_registry.cpp`

- expose any new server hardening config surface to AIML
- keep API additions narrow and explicit

### `spec/32_1_network_tls.md`
### `spec/32_2_network_http_s.md`
### `spec/32_3_network_rest_api.md`

- document secure defaults
- document limits
- document opt-in insecure/test-only modes clearly

## 5. Test Plan

Add or extend tests around the existing runtime, not just happy-path specs.

### TLS

- valid TLS 1.2 and 1.3 handshake
- reject weak protocol versions
- hostname verification success/failure
- mTLS success/failure

### HTTP parser

- oversized request line
- oversized headers
- duplicate conflicting headers
- invalid content length
- truncated body
- malformed CRLF handling

### Static file serving

- path traversal via plain `..`
- path traversal via percent-encoding
- null-byte / odd suffix handling if relevant to current parser path

### Abuse resistance

- slowloris timeout
- too many concurrent clients
- rate limit reached

### Regression

- existing `tests/test_tls.aiml`
- existing HTTP tests
- existing WS/WSS tests

## 6. Suggested Minimal First Milestone

If we want the smallest useful hardening slice, do this first:

1. Harden `aiml_http_server_tls(...)` to use secure TLS defaults.
2. Add parser limits for request line, headers, and body.
3. Add header/body read timeouts.
4. Add traversal-safe filesystem serving checks.
5. Add default `nosniff` and optional HSTS.
6. Add regression tests for malformed/oversized requests.

This gives a meaningful security step without redesigning the public AIML API.

## 7. Explicit Non-Goals For The First Pass

- full HTTP/2
- ALPN feature surface
- full WAF behavior
- distributed/global rate limiting
- OAuth/JWT validation framework
- certificate pinning API

Those can come later after the basic transport and parser hardening is stable.
