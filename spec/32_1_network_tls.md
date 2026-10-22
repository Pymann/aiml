# Chapter 32.1 — TLS: Encrypted Sockets

> **Core idea:** `tls_socket_handle` is the direct successor of `socket_handle`,
> but with transparent TLS encryption. The API is intentionally designed so that
> code using `net.*` can be migrated to `tls.*` with minimal effort.
> Internally AIML uses OpenSSL (`libssl`, `libcrypto`) — no custom crypto implementation.

---

## 1. Types

| Type                 | LLVM  | Description |
|----------------------|-------|-------------|
| `tls_socket_handle`  | `i8*` | Opaque heap pointer to `AimlTlsSocket { SSL* ssl; int fd; int timeout_ms; }` |

`tls_socket_handle` fully replaces `socket_handle` for encrypted connections.
An unencrypted `socket_handle` **cannot** be upgraded to TLS after the fact
(create a new `tls_socket_handle` instead).

---

## 2. `tls` Namespace (Lifecycle)

### 2.1 Creating a Socket

```aiml
- decl: [sock, tls_socket_handle]
- set: [sock, { call: [tls.socket] }]
```

`tls.socket()` — no `proto` argument (TLS = always TCP).
Returns `nullptr` if OpenSSL cannot be initialized.

### 2.2 Client: Connecting (with SNI + Certificate Verification)

```aiml
- decl: [sock, tls_socket_handle]
- set: [sock, { call: [tls.socket] }]

# Standard connection: TLS 1.2 or 1.3, SNI=host, CA from system store
- decl: [rc, i32]
- set: [rc, { call: [tls.connect, sock, "api.example.com", 443] }]
- print: ["connect rc: ${rc}"]   # 0 = success

# Send data
- call: [tls.send_string, sock, "GET / HTTP/1.1\r\nHost: api.example.com\r\n\r\n"]

# Receive data
- decl: [resp, string]
- set: [resp, { call: [tls.recv_string, sock, 8192] }]
- print: ["${resp}"]

- call: [tls.close, sock]
```

`tls.connect` automatically sets **SNI** (Server Name Indication) to `host` — required
for virtual hosting on HTTPS servers.

### 2.3 Certificate Configuration (before connect/bind)

```aiml
# Custom CA bundle (e.g. for internal PKI):
- call: [tls.set_ca, sock, "/etc/myapp/ca.pem"]

# Client certificate (Mutual TLS):
- call: [tls.set_cert, sock, "/etc/myapp/client.crt", "/etc/myapp/client.key"]

# Disable verification (for tests only!):
- call: [tls.set_verify, sock, "none"]     # Default: "peer"
# Modes: "peer" (verify server), "require" (require+verify client cert), "none"

- call: [tls.connect, sock, "internal.corp", 8443]
```

### 2.4 Server: Bind + Listen + Accept

```aiml
- decl: [srv, tls_socket_handle]
- set: [srv, { call: [tls.socket] }]

# Load server certificate (must be done before bind/listen)
- call: [tls.set_cert, srv, "/etc/myapp/server.crt", "/etc/myapp/server.key"]

# Optional: require client certificates (mTLS)
- call: [tls.set_verify, srv, "require"]
- call: [tls.set_ca, srv, "/etc/myapp/ca.pem"]

- call: [tls.bind, srv, "0.0.0.0", 8443]
- call: [tls.listen, srv, 128]

# Accept: blocks until a client connects, performs TLS handshake
- decl: [client, tls_socket_handle]
- set: [client, { call: [tls.accept, srv] }]

- decl: [msg, string]
- set: [msg, { call: [tls.recv_string, client, 4096] }]
- print: ["TLS server got: ${msg}"]
- call: [tls.close, client]
- call: [tls.close, srv]
```

---

## 3. Complete `tls` Namespace Reference

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls.socket` | `() → tls_socket_handle` | Creates a new TLS socket (TCP) |
| `tls.connect` | `(sock, host: string, port: i32) → i32` | TCP+TLS handshake; SNI=host |
| `tls.bind` | `(sock, host: string, port: i32) → i32` | Binds the socket |
| `tls.listen` | `(sock, backlog: i32) → i32` | Sets listen queue |
| `tls.accept` | `(sock) → tls_socket_handle` | Blocks; performs TLS handshake |
| `tls.send` | `(sock, buf: buffer) → i32` | Sends buffer.writeIndex bytes |
| `tls.recv` | `(sock, buf: buffer) → i32` | Receives up to buf.capacity bytes |
| `tls.send_string` | `(sock, s: string) → i32` | Sends a string |
| `tls.recv_string` | `(sock, max: i32) → string` | Receives a string (malloc, caller frees) |
| `tls.close` | `(sock) → i32` | TLS shutdown + TCP close + free |
| `tls.fd` | `(sock) → i32` | Raw file descriptor |
| `tls.set_timeout` | `(sock, ms: i32) → i32` | 0=blocking; >0=timeout for connect/recv/send |
| `tls.set_cert` | `(sock, cert: path, key: path) → i32` | Loads certificate + private key (PEM). `cert` may hold the full chain (leaf first, then intermediates, e.g. Let's Encrypt `fullchain.pem`); the whole chain is sent to peers |
| `tls.set_ca` | `(sock, ca: path) → i32` | Sets CA bundle (PEM) instead of system store |
| `tls.set_verify` | `(sock, mode: string) → i32` | "peer" / "require" / "none" |

---

## 4. `tls_socket_handle` Instance Methods

```aiml
- decl: [sock, tls_socket_handle]
- set: [sock, { call: [tls.socket] }]
- call: [tls.connect, sock, "example.com", 443]

# Methods directly on the handle:
- call: [sock.send_string, "hello"]
- decl: [r, string]
- set: [r, { call: [sock.recv_string, 1024] }]
- call: [sock.close]

# Inspect peer certificate (after successful connect/accept):
- decl: [cn, string]
- set: [cn, { call: [sock.peer_cert_cn] }]      # Common Name
- decl: [valid, bool]
- set: [valid, { call: [sock.peer_cert_valid] }] # Chain + expiry OK?
- print: ["Peer: ${cn}, valid: ${valid}"]
```

| Method | Signature | Description |
|--------|-----------|-------------|
| `close` | `() → i32` | TLS shutdown + TCP close |
| `send` | `(buf: buffer) → i32` | — |
| `recv` | `(buf: buffer) → i32` | — |
| `send_string` | `(s: string) → i32` | — |
| `recv_string` | `(max: i32) → string` | — |
| `set_timeout` | `(ms: i32) → i32` | — |
| `fd` | `() → i32` | Raw FD |
| `peer_cert_cn` | `() → string` | Common Name of the peer certificate |
| `peer_cert_valid` | `() → bool` | Checks chain + expiry |

---

## 5. Example: Mutual TLS (mTLS)

```aiml
# --- Server ---
- def:
    name: tls_server
    args: []
    body:
      - decl: [s, tls_socket_handle]
      - set: [s, { call: [tls.socket] }]
      - call: [tls.set_cert, s, "/pki/server.crt", "/pki/server.key"]
      - call: [tls.set_ca, s, "/pki/ca.crt"]
      - call: [tls.set_verify, s, "require"]   # mTLS: client must present certificate
      - call: [tls.bind, s, "127.0.0.1", 9443]
      - call: [tls.listen, s, 5]
      - decl: [c, tls_socket_handle]
      - set: [c, { call: [tls.accept, s] }]
      - decl: [cn, string]
      - set: [cn, { call: [c.peer_cert_cn] }]
      - print: ["Client CN: ${cn}"]
      - call: [tls.send_string, c, "authenticated"]
      - call: [tls.close, c]
      - call: [tls.close, s]

# --- Client ---
- decl: [srv_t, thread_handle]
- set: [srv_t, { spawn: [tls_server] }]
- call: [time.sleep_ms, 100]

- decl: [client, tls_socket_handle]
- set: [client, { call: [tls.socket] }]
- call: [tls.set_cert, client, "/pki/client.crt", "/pki/client.key"]
- call: [tls.set_ca, client, "/pki/ca.crt"]
- call: [tls.connect, client, "127.0.0.1", 9443]

- decl: [resp, string]
- set: [resp, { call: [tls.recv_string, client, 64] }]
- print: ["Server said: ${resp}"]   # "authenticated"
- call: [tls.close, client]
- call: [srv_t.join]
```

---

## 6. Implementation Notes

### 6.1 Internal Structure (`tls_runtime.h/cpp`)

```cpp
struct AimlTlsSocket {
    SSL_CTX* ctx;       // OpenSSL context (certificates, verify mode)
    SSL*     ssl;       // TLS session (nullptr before connect/accept)
    int      fd;        // TCP socket FD
    int      timeout_ms;
};
```

`ctx` and `ssl` are kept separate so that `set_cert`/`set_ca`/`set_verify` can be called
before `connect`/`accept`.

### 6.2 OpenSSL Initialization

```cpp
// Once on the first tls.socket() call (thread-safe with std::once_flag):
OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
```

The system CA store is loaded via `SSL_CTX_set_default_verify_paths(ctx)` (reads
`/etc/ssl/certs/` on Linux automatically).

### 6.2a Shared server-hardening helpers

`tls_runtime.cpp` exposes two `SSL_CTX*`-level helpers so every server transport shares one
policy instead of re-deriving it:

- `aiml_tls_harden_server_ctx(ctx)` — minimum **TLS 1.2**, disables SSLv2/SSLv3/TLS 1.0/TLS 1.1
  and TLS-layer compression (CRIME), honours the server cipher preference. Applied by both the
  HTTPS server (`http.server_tls`, chapter 32_2) and the WSS server (`ws.server_tls`, chapter 32_4).
- `aiml_tls_require_client_cert(ctx, ca_path)` — enables mTLS
  (`SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT`) against a CA bundle PEM; returns `0`/`-1`.
  This backs `srv.require_client_cert` on the HTTPS server (chapter 32_2 §3.11).

Keeping these in `tls_runtime` means the direct `tls.*` sockets, the HTTPS server, and the WSS
server can never drift to different TLS floors.

### 6.3 Link Flags

```
-lssl -lcrypto
```

Add as an additional dependency in `CMakeLists.txt` / `exodus build`.

### 6.4 Connect Timeout

Identical approach to `net.set_timeout` + `net.connect`:
`fcntl(O_NONBLOCK)` → TCP connect via `poll(POLLOUT, timeout_ms)` →
`SSL_connect()` blocks afterwards (briefly, since TCP is already connected).

---

## 7. Open Design Questions

- **ALPN** (Application-Layer Protocol Negotiation): Required for HTTP/2 —
  `tls.set_alpn(sock, ["h2", "http/1.1"])` — not yet specified.
- **Session Tickets**: TLS session resumption for faster reconnects.
- **PKCS#11**: For HSM-bound private keys via `libp11`/`pkcs11-provider` —
  later extension of `tls.set_cert`.
- **Certificate Pinning**: `sock.pin_cert(fingerprint_sha256)` — enforces
  an exact certificate fingerprint instead of CA chain.
