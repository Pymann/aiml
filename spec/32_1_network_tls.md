# Kapitel 32.1 — TLS: Verschlüsselte Sockets

> **Leitgedanke:** `tls_socket_handle` ist der direkte Nachfolger von `socket_handle`,
> aber mit transparenter TLS-Verschlüsselung. Die API ist absichtlich so gestaltet, dass
> Code, der `net.*` verwendet, mit minimalem Aufwand auf `tls.*` migriert werden kann.
> Intern verwendet AIML OpenSSL (`libssl`, `libcrypto`) — keine eigene Krypto-Implementierung.

---

## 1. Typen

| Typ                  | LLVM  | Beschreibung |
|----------------------|-------|--------------|
| `tls_socket_handle`  | `i8*` | Opaker Heap-Pointer auf `AimlTlsSocket { SSL* ssl; int fd; int timeout_ms; }` |

`tls_socket_handle` ersetzt `socket_handle` vollständig für verschlüsselte Verbindungen.
Ein unverschlüsselter `socket_handle` kann **nicht** nachträglich auf TLS upgegraded werden
(neuen `tls_socket_handle` erstellen).

---

## 2. `tls`-Namespace (Lifecycle)

### 2.1 Socket erstellen

```aiml
- decl: [sock, tls_socket_handle]
- set: [sock, { call: [tls.socket] }]
```

`tls.socket()` — kein `proto`-Argument (TLS = immer TCP).
Gibt `nullptr` zurück, wenn OpenSSL nicht initialisiert werden kann.

### 2.2 Client: Verbinden (mit SNI + Zertifikats-Verifikation)

```aiml
- decl: [sock, tls_socket_handle]
- set: [sock, { call: [tls.socket] }]

# Standard-Verbindung: TLS 1.2 oder 1.3, SNI=host, CA aus System-Store
- decl: [rc, i32]
- set: [rc, { call: [tls.connect, sock, "api.example.com", 443] }]
- print: ["connect rc: ${rc}"]   # 0 = Erfolg

# Daten senden
- call: [tls.send_string, sock, "GET / HTTP/1.1\r\nHost: api.example.com\r\n\r\n"]

# Daten empfangen
- decl: [resp, string]
- set: [resp, { call: [tls.recv_string, sock, 8192] }]
- print: ["${resp}"]

- call: [tls.close, sock]
```

`tls.connect` setzt automatisch **SNI** (Server Name Indication) auf `host` — notwendig
für Virtual-Hosting auf HTTPS-Servern.

### 2.3 Zertifikats-Konfiguration (vor connect/bind)

```aiml
# Eigenes CA-Bundle (z.B. für interne PKI):
- call: [tls.set_ca, sock, "/etc/myapp/ca.pem"]

# Client-Zertifikat (Mutual TLS):
- call: [tls.set_cert, sock, "/etc/myapp/client.crt", "/etc/myapp/client.key"]

# Verifikation deaktivieren (nur für Tests!):
- call: [tls.set_verify, sock, "none"]     # Default: "peer"
# Modi: "peer" (verify server), "require" (require+verify client cert), "none"

- call: [tls.connect, sock, "internal.corp", 8443]
```

### 2.4 Server: Bind + Listen + Accept

```aiml
- decl: [srv, tls_socket_handle]
- set: [srv, { call: [tls.socket] }]

# Server-Zertifikat laden (muss vor bind/listen)
- call: [tls.set_cert, srv, "/etc/myapp/server.crt", "/etc/myapp/server.key"]

# Optional: Client-Zertifikate verlangen (mTLS)
- call: [tls.set_verify, srv, "require"]
- call: [tls.set_ca, srv, "/etc/myapp/ca.pem"]

- call: [tls.bind, srv, "0.0.0.0", 8443]
- call: [tls.listen, srv, 128]

# Accept: Blockiert bis Client kommt, führt TLS-Handshake durch
- decl: [client, tls_socket_handle]
- set: [client, { call: [tls.accept, srv] }]

- decl: [msg, string]
- set: [msg, { call: [tls.recv_string, client, 4096] }]
- print: ["TLS server got: ${msg}"]
- call: [tls.close, client]
- call: [tls.close, srv]
```

---

## 3. Vollständige `tls`-Namespace-Referenz

| Funktion | Signatur | Beschreibung |
|----------|----------|--------------|
| `tls.socket` | `() → tls_socket_handle` | Erstellt neuen TLS-Socket (TCP) |
| `tls.connect` | `(sock, host: string, port: i32) → i32` | TCP+TLS-Handshake; SNI=host |
| `tls.bind` | `(sock, host: string, port: i32) → i32` | Bindet Socket |
| `tls.listen` | `(sock, backlog: i32) → i32` | Setzt Listen-Queue |
| `tls.accept` | `(sock) → tls_socket_handle` | Blockiert; führt TLS-Handshake durch |
| `tls.send` | `(sock, buf: buffer) → i32` | Sendet buffer.writeIndex Bytes |
| `tls.recv` | `(sock, buf: buffer) → i32` | Empfängt bis buf.capacity Bytes |
| `tls.send_string` | `(sock, s: string) → i32` | Sendet String |
| `tls.recv_string` | `(sock, max: i32) → string` | Empfängt String (malloc, Caller frees) |
| `tls.close` | `(sock) → i32` | TLS shutdown + TCP close + free |
| `tls.fd` | `(sock) → i32` | Roher File-Descriptor |
| `tls.set_timeout` | `(sock, ms: i32) → i32` | 0=blocking; >0=Timeout für connect/recv/send |
| `tls.set_cert` | `(sock, cert: path, key: path) → i32` | Lädt Zertifikat + Private Key (PEM) |
| `tls.set_ca` | `(sock, ca: path) → i32` | Setzt CA-Bundle (PEM) statt System-Store |
| `tls.set_verify` | `(sock, mode: string) → i32` | "peer" / "require" / "none" |

---

## 4. `tls_socket_handle`-Instanzmethoden

```aiml
- decl: [sock, tls_socket_handle]
- set: [sock, { call: [tls.socket] }]
- call: [tls.connect, sock, "example.com", 443]

# Methoden direkt auf dem Handle:
- call: [sock.send_string, "hello"]
- decl: [r, string]
- set: [r, { call: [sock.recv_string, 1024] }]
- call: [sock.close]

# Peer-Zertifikat prüfen (nach erfolgreichem connect/accept):
- decl: [cn, string]
- set: [cn, { call: [sock.peer_cert_cn] }]      # Common Name
- decl: [valid, bool]
- set: [valid, { call: [sock.peer_cert_valid] }] # Kette + Ablaufdatum OK?
- print: ["Peer: ${cn}, valid: ${valid}"]
```

| Methode | Signatur | Beschreibung |
|---------|----------|--------------|
| `close` | `() → i32` | TLS shutdown + TCP close |
| `send` | `(buf: buffer) → i32` | — |
| `recv` | `(buf: buffer) → i32` | — |
| `send_string` | `(s: string) → i32` | — |
| `recv_string` | `(max: i32) → string` | — |
| `set_timeout` | `(ms: i32) → i32` | — |
| `fd` | `() → i32` | Roher FD |
| `peer_cert_cn` | `() → string` | Common Name des Peer-Zertifikats |
| `peer_cert_valid` | `() → bool` | Prüft Kette + Ablauf |

---

## 5. Beispiel: Mutual TLS (mTLS)

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
      - call: [tls.set_verify, s, "require"]   # mTLS: Client muss Zertifikat vorlegen
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

## 6. Implementierungshinweise

### 6.1 Interne Struktur (`tls_runtime.h/cpp`)

```cpp
struct AimlTlsSocket {
    SSL_CTX* ctx;       // OpenSSL-Kontext (Zertifikate, Verify-Mode)
    SSL*     ssl;       // TLS-Session (nullptr vor connect/accept)
    int      fd;        // TCP-Socket-FD
    int      timeout_ms;
};
```

`ctx` und `ssl` werden getrennt gehalten, damit `set_cert`/`set_ca`/`set_verify` vor
`connect`/`accept` aufgerufen werden können.

### 6.2 OpenSSL-Initialisierung

```cpp
// Einmalig beim ersten tls.socket()-Aufruf (thread-safe mit std::once_flag):
OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
```

System-CA-Store wird via `SSL_CTX_set_default_verify_paths(ctx)` geladen (liest
`/etc/ssl/certs/` auf Linux automatisch).

### 6.3 Link-Flags

```
-lssl -lcrypto
```

In `CMakeLists.txt` / `exodus build` als zusätzliche Abhängigkeit eintragen.

### 6.4 Connect-Timeout

Identisches Verfahren wie `net.set_timeout` + `net.connect`:
`fcntl(O_NONBLOCK)` → TCP-Connect via `poll(POLLOUT, timeout_ms)` →
`SSL_connect()` blockiert danach (kurz, da TCP bereits verbunden).

---

## 7. Offene Designfragen

- **ALPN** (Application-Layer Protocol Negotiation): Für HTTP/2 notwendig —
  `tls.set_alpn(sock, ["h2", "http/1.1"])` — noch nicht spezifiziert.
- **Session-Tickets**: TLS-Session-Resumption für schnellere Reconnects.
- **PKCS#11**: Für HSM-gebundene Private Keys via `libp11`/`pkcs11-provider` —
  spätere Erweiterung von `tls.set_cert`.
- **Certificate Pinning**: `sock.pin_cert(fingerprint_sha256)` — erzwingt
  genauen Zertifikat-Fingerabdruck statt CA-Kette.
