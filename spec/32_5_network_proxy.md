# Chapter 32.5 — Proxy Support

> **Motivation:** In corporate networks, internet access requires proxy support.
> AIML supports all common proxy protocols and authentication methods with
> clean separation between proxy auth, proxy certificate, and target TLS configuration —
> exactly the layering that regularly breaks in Go, Python, and curl.

---

## 1. The Problem: Layered Complexity

```
┌──────────────────────────────────────────────────────────────────────┐
│ AIML client                                                           │
│                                                                        │
│  TCP → [Proxy auth] → Proxy server:port                               │
│                │                                                       │
│                ├── HTTP CONNECT  ─── Tunnel → Target server:port      │
│                ├── SOCKS4/4a    ─── Tunnel → Target server:port      │
│                └── SOCKS5       ─── Tunnel → Target server:port      │
│                                        │                              │
│                          ┌─────────────▼──────────────────────┐      │
│                          │ Scenario A: SSL inspection (MITM)   │      │
│                          │  Proxy terminates TLS, re-signs     │      │
│                          │  target cert with corp inspection CA │      │
│                          └─────────────────────────────────────┘      │
│                          ┌─────────────────────────────────────┐      │
│                          │ Scenario B: Transparent tunnel       │      │
│                          │  TLS directly with target (mTLS ok) │      │
│                          └─────────────────────────────────────┘      │
└──────────────────────────────────────────────────────────────────────┘
```

**Why libraries fail:**
- NTLM: libraries reconnect after `407` → NTLM context lost (connection must stay open)
- Kerberos: requires GSSAPI integration, which most HTTP stacks do not include
- SSL inspection + custom target CA: two separate trust contexts that are usually coupled
- SOCKS5 + GSSAPI auth: rarely fully implemented

---

## 2. Types

| Type                   | LLVM  | Description |
|-----------------------|-------|--------------|
| `proxy_config_handle` | `i8*` | Configuration object — describes protocol, address, and auth |

`proxy_config_handle` is **not a socket**. It is created once, configured,
and then applied to any number of sockets (before `connect`).

---

## 3. Proxy Protocols

### 3.1 HTTP CONNECT (RFC 7231)

Standard protocol for web proxies. The proxy opens a TCP tunnel to the target.

```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new, "proxy.example.com", 8080] }]
# Type is HTTP CONNECT by default
```

Flow:
```
Client → Proxy:  CONNECT targethost:443 HTTP/1.1\r\nHost: targethost:443\r\n\r\n
Proxy  → Client: HTTP/1.1 200 Connection Established\r\n\r\n
Client → Proxy:  [raw TLS bytes that the proxy forwards]
```

### 3.2 SOCKS4

Simplest proxy protocol. TCP only, no auth, no IPv6, no DNS-at-proxy.

```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new_socks4, "socks4proxy.example.com", 1080] }]
```

Flow (binary protocol):
```
Client → Proxy:  VER=4, CMD=1(CONNECT), DSTPORT, DSTIP, USERID\0
Proxy  → Client: VER=0, REP=90(granted) / 91(rejected), PORT, IP
Client → Proxy:  [raw connection bytes]
```

Limitations: IPv4 addresses only; hostname must be resolved client-side.

### 3.3 SOCKS4a

Extension of SOCKS4 with DNS resolution at the proxy (important for internal hostnames).

```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new_socks4a, "socks4aproxy.example.com", 1080] }]
```

When DSTIP = `0.0.0.x` (x ≠ 0) and a hostname follows after USERID, the proxy
resolves the name. AIML automatically selects SOCKS4a when the target host is not an IPv4 literal.

### 3.4 SOCKS5 (RFC 1928)

Most complete protocol: IPv4/IPv6/hostname, auth, TCP + UDP.

```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new_socks5, "tor.local", 9050] }]
# No auth → Tor / anonymous SOCKS5 proxies

- decl: [px2, proxy_config_handle]
- set: [px2, { call: [proxy.new_socks5, "socks5.corp.com", 1080] }]
- call: [px2.set_auth_basic, "user", "pass"]
# SOCKS5 username/password auth (RFC 1929)
```

Flow:
```
(1) Auth negotiation:
    Client → Proxy:  VER=5, NMETHODS, [METHODS...]
                     Methods: 0x00=No auth, 0x02=User/Pass, 0x01=GSSAPI
    Proxy  → Client: VER=5, METHOD (chosen)

(2) Auth (if METHOD=0x02):
    Client → Proxy:  VER=1, ULEN, USERNAME, PLEN, PASSWORD
    Proxy  → Client: VER=1, STATUS (0x00=success)

(3) CONNECT request:
    Client → Proxy:  VER=5, CMD=1, RSV=0, ATYP, DST.ADDR, DST.PORT
                     ATYP: 0x01=IPv4, 0x03=Hostname, 0x04=IPv6
    Proxy  → Client: VER=5, REP=0(success), RSV, BNDATYP, BND.ADDR, BND.PORT

(4) Client → Proxy:  [raw connection bytes]
```

### 3.5 HTTPS Proxy (HTTP CONNECT over TLS)

Less common case: the proxy control protocol itself is TLS-encrypted.

```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new, "secure-proxy.corp.com", 8443] }]
- call: [px.set_proxy_tls, true]
- decl: [proxy_ca, path]
- set: [proxy_ca, "/etc/corp/proxy_ca.crt"]
- call: [px.set_ca, proxy_ca]
- call: [px.set_auth_ntlm, "CORP\\user", "pass"]
```

---

## 4. Authentication Methods

### 4.1 No Auth (Default)

```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new_socks5, "tor.local", 9050] }]
# no set_auth_* → no auth
```

### 4.2 HTTP Basic

```aiml
- call: [px.set_auth_basic, "username", "p@ssw0rd"]
# Sends: Proxy-Authorization: Basic <Base64(user:pass)>
# For SOCKS5: username/password auth (RFC 1929)
```

### 4.3 HTTP Digest

```aiml
- call: [px.set_auth_digest, "username", "p@ssw0rd"]
# MD5 challenge-response (RFC 7616)
# Proxy sends 407 with Proxy-Authenticate: Digest realm=... nonce=...
# Client computes: MD5(MD5(user:realm:pass):nonce:MD5(method:uri))
```

### 4.4 NTLM (NTLMv2)

```aiml
- call: [px.set_auth_ntlm, "username", "p@ssw0rd"]
# Domain derived from proxy hostname

- call: [px.set_auth_ntlm, "CORP\\username", "p@ssw0rd"]
# Domain prefix syntax

- call: [px.set_auth_ntlm, "username", "p@ssw0rd", "CORP"]
# Explicit domain as third argument
```

3-phase handshake on a **persistent connection** (no reconnect after 407):
```
Client → NEGOTIATE (flags: NTLMv2, Unicode)
Proxy  → 407 + CHALLENGE (ServerChallenge 8 bytes)
Client → AUTHENTICATE (NTProofStr = HMAC-MD5(NTHash, Challenge))
Proxy  → 200 Connection Established
```

### 4.5 Kerberos / Negotiate (GSSAPI)

```aiml
- call: [px.set_auth_kerberos, "HTTP/proxy.corp.com@CORP.LOCAL"]
# Proxy-Authorization: Negotiate <Base64(Kerberos token)>
# Requires: valid Kerberos ticket (kinit or keytab)

- call: [px.set_auth_kerberos_keytab, "HTTP/proxy.corp.com@CORP.LOCAL",
                                       "/etc/myapp/service.keytab"]
# With keytab (for services without interactive kinit)
```

Flow:
```
Client → gss_init_sec_context(service_name) → Token
Client → Proxy: Proxy-Authorization: Negotiate <Base64(Token)>
Proxy  → Client: 407 + Proxy-Authenticate: Negotiate <Base64(ServerToken)>
Client → gss_init_sec_context(ServerToken) → Token2
Client → Proxy: Proxy-Authorization: Negotiate <Base64(Token2)>
Proxy  → Client: 200 Connection Established
```

Implemented via `libgssapi_krb5` (`-lgssapi_krb5`).

### 4.6 Bearer / OAuth2 (Cloud proxies like Zscaler)

```aiml
- call: [px.set_auth_bearer, "eyJhbGci..."]
# Proxy-Authorization: Bearer <token>
# For cloud proxies (Zscaler ZPA, Netskope, etc.)
```

---

## 5. Complete `proxy_config_handle` Methods

| Method                    | Arguments                              | Return | Description |
|----------------------------|----------------------------------------|----------|--------------|
| `set_auth_none`            | —                                      | —        | No auth (default) |
| `set_auth_basic`           | `user: string, pass: string`           | —        | HTTP Basic / SOCKS5 user-pass |
| `set_auth_digest`          | `user: string, pass: string`           | —        | HTTP Digest (MD5) |
| `set_auth_ntlm`            | `user: string, pass: string`           | —        | NTLMv2 |
| `set_auth_ntlm`            | `user: string, pass: string, domain: string` | — | NTLMv2 + explicit domain |
| `set_auth_kerberos`        | `spn: string`                          | —        | Kerberos via GSSAPI (existing ticket) |
| `set_auth_kerberos_keytab` | `spn: string, keytab: path`            | —        | Kerberos with keytab |
| `set_auth_bearer`          | `token: string`                        | —        | Bearer token (OAuth2) |
| `set_ca`                   | `ca: path`                             | —        | CA for proxy certificate (SSL inspection or HTTPS proxy) |
| `set_proxy_tls`            | `enabled: bool`                        | —        | Proxy control channel itself via TLS (default: false) |
| `set_timeout`              | `ms: i32`                              | —        | Timeout for proxy connection + auth handshake |
| `set_no_proxy`             | `hosts: string`                        | —        | Comma-separated hostnames to connect directly |
| `set_resolve_via_proxy`    | `enabled: bool`                        | —        | DNS resolution at proxy (SOCKS4a/SOCKS5, default: true) |

---

## 6. `proxy` Namespace Reference

| Call | Return | Description |
|--------|----------|--------------|
| `{ call: [proxy.new, host, port] }` | `proxy_config_handle` | HTTP CONNECT proxy |
| `{ call: [proxy.new_socks4, host, port] }` | `proxy_config_handle` | SOCKS4 |
| `{ call: [proxy.new_socks4a, host, port] }` | `proxy_config_handle` | SOCKS4a (DNS at proxy) |
| `{ call: [proxy.new_socks5, host, port] }` | `proxy_config_handle` | SOCKS5 |
| `{ call: [proxy.from_env] }` | `proxy_config_handle` | Reads `HTTPS_PROXY` / `HTTP_PROXY` / `NO_PROXY` |
| `{ call: [proxy.from_pac, pac_url] }` | `proxy_config_handle` | PAC file via URL (`pac_url: url`) |
| `{ call: [proxy.from_pac_file, pac_path] }` | `proxy_config_handle` | Local PAC file (`pac_path: path`) |
| `{ call: [proxy.none] }` | `proxy_config_handle` | Explicitly no proxy (overrides environment) |

### 6.1 Environment Variables (`proxy.from_env`)

```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.from_env] }]
# Reads (in priority order):
#   HTTPS_PROXY, https_proxy  → for HTTPS connections
#   HTTP_PROXY, http_proxy    → for HTTP connections
#   ALL_PROXY, all_proxy      → fallback for both
#   NO_PROXY, no_proxy        → exceptions (comma-separated, wildcards: *.corp.com)
# Returns nullptr if no proxy variable is set

- decl: [sock, tls_socket_handle]
- set: [sock, { call: [tls.socket] }]
- call: [sock.set_proxy, px]   # nullptr → no proxy, no error
- call: [tls.connect, sock, "api.example.com", 443]
```

### 6.2 PAC Files (Proxy Auto-Config)

PAC files are JavaScript files that decide, based on URL and host, which proxy
to use (`FindProxyForURL` function). Widely used in enterprises.

```aiml
# Load PAC from URL (typical enterprise case: WPAD)
- decl: [wpad_url, url]
- set: [wpad_url, "http://wpad.corp.com/wpad.dat"]
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.from_pac, wpad_url] }]

# Or local PAC file
- decl: [pac_file, path]
- set: [pac_file, "/etc/myapp/proxy.pac"]
- set: [px, { call: [proxy.from_pac_file, pac_file] }]

# set_auth applies to all proxies returned by the PAC
- call: [px.set_auth_ntlm, "CORP\\user", "pass"]
- decl: [insp_ca, path]
- set: [insp_ca, "/etc/corp/inspection_ca.crt"]
- call: [px.set_ca, insp_ca]

- decl: [sock, tls_socket_handle]
- set: [sock, { call: [tls.socket] }]
- call: [sock.set_proxy, px]
- call: [tls.connect, sock, "github.com", 443]
# PAC decides internally: DIRECT or PROXY proxy1.corp.com:8080
```

PAC implementation: minimal JavaScript interpreter (only `FindProxyForURL` +
standard helper functions like `isInNet`, `dnsDomainIs`, `shExpMatch`).

---

## 7. Extensions to Socket Types

Both handle types gain:

| Method | Signature | Description |
|---------|----------|--------------|
| `set_proxy` | `(proxy: proxy_config_handle) → void` | Apply proxy configuration |
| `add_ca` | `(ca_path: path) → void` | Add additional CA (cumulative) |

`add_ca` (as opposed to `set_ca`, which replaces the system store) **adds** —
needed when proxy CA and target CA should both be trusted simultaneously:

```aiml
- call: [tls.set_ca, sock, "/etc/corp/inspection_ca.crt"]   # replaces system store
- call: [sock.add_ca, "/etc/myapp/target_ca.crt"]           # in addition
```

---

## 8. Common Proxy Products: Compatibility Table

| Product | Protocol | Auth Methods | SSL Inspection | Notes |
|---------|-----------|---------------|----------------|-----------|
| **Squid** | HTTP CONNECT | Basic, Digest, NTLM, Kerberos | Optional (with ssl-bump) | Most common open-source proxy |
| **BlueCoat / Symantec ProxySG** | HTTP CONNECT | NTLM, Kerberos, RADIUS | Yes (standard) | Corporate appliance |
| **Zscaler ZIA** | HTTP CONNECT | Bearer/OAuth2, SAML, Kerberos | Yes | Cloud proxy; requires bearer token |
| **Netskope** | HTTP CONNECT | Bearer/OAuth2, SAML | Yes | Cloud proxy |
| **Cisco WSA** | HTTP CONNECT | NTLM, Kerberos, LDAP, Basic | Optional | Web Security Appliance |
| **F5 BIG-IP** | HTTP CONNECT | NTLM, Kerberos, Basic | Optional | Forward proxy module |
| **Microsoft ISA / TMG** | HTTP CONNECT | NTLM, Kerberos, Basic | No | Outdated, still in use |
| **Privoxy** | HTTP CONNECT | No auth | No | Privacy proxy; often behind Tor |
| **Tor** | SOCKS5 | No auth | No | Anonymity; DNS at proxy (important!) |
| **SSH -D** | SOCKS5 | No auth (SSH auth before) | No | `ssh -D 1080 user@bastion` |
| **Dante** | SOCKS4/5 | Basic, GSSAPI | No | Open-source SOCKS server |
| **Shadowsocks** | SOCKS5 | No auth (own encryption) | No | Obfuscated proxy |
| **HAProxy** | SOCKS5, HTTP | Basic | No | Load balancer with proxy mode |
| **Charles / Burp Suite** | HTTP CONNECT | Basic | Yes (dev/testing) | Development proxies |
| **nginx** | HTTP CONNECT | Basic | Optional | With `ngx_stream_proxy_module` |

### 8.1 Configuration Examples for Common Products

**Squid with NTLM:**
```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new, "squid.corp.local", 3128] }]
- call: [px.set_auth_ntlm, "CORP\\user", "pass"]
```

**BlueCoat / Symantec ProxySG (SSL inspection + Kerberos):**
```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new, "bluecoat.corp.com", 8080] }]
- call: [px.set_auth_kerberos, "HTTP/bluecoat.corp.com@CORP.COM"]
- call: [px.set_ca, "/etc/corp/bluecoat_inspection_ca.crt"]
```

**Zscaler ZIA (cloud proxy, bearer token):**
```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new, "gateway.corp.zscaler.net", 80] }]
- call: [px.set_auth_bearer, "eyJhbGci...zscaler-token"]
- call: [px.set_ca, "/etc/corp/zscaler_root_ca.crt"]
# Zscaler signs re-inspected certs with its own root CA
```

**Tor (SOCKS5, no auth, DNS at proxy):**
```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new_socks5, "127.0.0.1", 9050] }]
# DNS resolution happens at the Tor exit node (not locally) → privacy!
# set_resolve_via_proxy is true by default for SOCKS5

- decl: [sock, tls_socket_handle]
- set: [sock, { call: [tls.socket] }]
- call: [sock.set_proxy, px]
- call: [tls.connect, sock, "duckduckgo.com", 443]
```

**SSH SOCKS5 tunnel:**
```aiml
# Before: ssh -D 1080 -N user@bastion.corp.com
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new_socks5, "127.0.0.1", 1080] }]

- decl: [sock, tls_socket_handle]
- set: [sock, { call: [tls.socket] }]
- call: [sock.set_proxy, px]
- call: [tls.connect, sock, "internal-api.corp.com", 443]
# Connection goes: local → SSH tunnel → bastion → internal-api
```

**WPAD (automatic proxy detection in enterprise):**
```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.from_pac, "http://wpad/wpad.dat"] }]
- call: [px.set_auth_ntlm, "CORP\\user", "pass"]
# PAC file decides per target URL: DIRECT / PROXY host:port
```

---

## 9. NTLM Implementation Detail

### 9.1 Why No External Library

- `libntlm`: outdated, NTLMv1 only
- `gss-ntlmssp`: Kerberos dependency, complex to link
- OpenSSL provides all needed primitives: `MD4`, `HMAC-MD5`, `RAND_bytes`, `EVP_EncodeBlock`
- Custom NTLMv2 implementation: ~400 lines of C++ in `src/sys/ntlm.cpp`

### 9.2 NTLMv2 Hash

```
NTHash    = MD4(UTF-16LE(password))
NTHashV2  = HMAC-MD5(NTHash, uppercase(user+domain) as UTF-16LE)
NTProofStr= HMAC-MD5(NTHashV2,
              ServerChallenge‖ClientChallenge‖Timestamp‖TargetInfo)
NTResponse= NTProofStr ‖ Blob(ClientChallenge, Timestamp, TargetInfo)
```

### 9.3 Critical: Persistent Connection

```
Step 1: TCP CONNECT to proxy
Step 2: CONNECT target HTTP/1.1 + Proxy-Authorization: NTLM <NEGOTIATE>
Step 3: 407 + Proxy-Authenticate: NTLM <CHALLENGE>  ← keep connection OPEN
Step 4: CONNECT target HTTP/1.1 + Proxy-Authorization: NTLM <AUTHENTICATE>
Step 5: 200 Connection Established
```

Libraries that reconnect after step 3 → NTLM breaks. AIML performs all 5 steps
on the same file descriptor.

---

## 10. Internal Structure (`proxy_runtime.h/cpp`)

```cpp
enum class ProxyProtocol { HttpConnect, Socks4, Socks4a, Socks5 };
enum class ProxyAuth     { None, Basic, Digest, NTLMv2, Kerberos, Bearer };

struct AimlProxy {
    ProxyProtocol protocol;
    char*         host;
    int           port;
    bool          proxy_tls;    // control channel itself via TLS
    char*         ca_path;      // CA for proxy (inspection or HTTPS)
    ProxyAuth     auth;
    char*         username;
    char*         password;
    char*         domain;       // NTLM
    char*         spn;          // Kerberos Service Principal Name
    char*         keytab_path;  // Kerberos keytab
    char*         bearer_token; // OAuth2
    int           timeout_ms;
    char*         no_proxy;     // comma-separated
    bool          resolve_via_proxy; // DNS at proxy (SOCKS4a/5)
    // PAC
    char*         pac_url;
    char*         pac_js;       // cached PAC content
};

// Main function: builds tunnel on fd (TCP to proxy already connected)
int aiml_proxy_tunnel(AimlProxy* proxy, int fd,
                      const char* target_host, int target_port);
```

`aiml_proxy_tunnel` dispatches internally to:
- `proxy_http_connect_tunnel()` — HTTP CONNECT + auth (Basic/Digest/NTLM/Kerberos/Bearer)
- `proxy_socks4_tunnel()` — SOCKS4 binary protocol
- `proxy_socks4a_tunnel()` — SOCKS4a with hostname
- `proxy_socks5_tunnel()` — SOCKS5 auth negotiation + CONNECT

---

## 11. Complete Enterprise Example

```aiml
# Situation:
# - Windows corporate network, WPAD-based proxy detection
# - Proxy: BlueCoat ProxySG, NTLM auth, SSL inspection
# - Target: Vault API server internal, bearer token auth

- def:
    name: vault_get_secret
    args: [[path, string], [vault_token, string]]
    return: string
    body:
      - decl: [px, proxy_config_handle]
      - set: [px, { call: [proxy.from_pac, "http://wpad.corp.local/wpad.dat"] }]
      - call: [px.set_auth_ntlm, "svc-myapp", "V@ult!2024", "CORP"]
      - call: [px.set_ca, "/etc/corp/bluecoat_inspection.crt"]
      - call: [px.set_timeout, 20000]

      - decl: [sock, tls_socket_handle]
      - set: [sock, { call: [tls.socket] }]
      - call: [sock.set_proxy, px]
      - call: [tls.set_timeout, sock, 10000]

      - decl: [rc, i32]
      - set: [rc, { call: [tls.connect, sock, "vault.prod.corp.com", 8200] }]
      - if:
          condition: { call: [rc != 0] }
          body:
            - return: ""

      - decl: [req, string]
      - set: [req, "GET /v1/secret/data/${path} HTTP/1.1\r\nHost: vault.prod.corp.com\r\nX-Vault-Token: ${vault_token}\r\nAccept: application/json\r\n\r\n"]
      - call: [tls.send_string, sock, req]

      - decl: [resp, string]
      - set: [resp, { call: [tls.recv_string, sock, 65536] }]
      - call: [tls.close, sock]
      - return: resp

- decl: [secret, string]
- set: [secret, { call: [vault_get_secret, "prod/db/password", "s.abc123"] }]
- print: ["${secret}"]
```

---

## 12. Combination Matrix

| Proxy Protocol | Auth | Proxy CA | Target Cert | Supported? |
|----------------|------|----------|-----------|-------------|
| HTTP CONNECT | None | — | System store | ✓ |
| HTTP CONNECT | Basic | — | System store | ✓ |
| HTTP CONNECT | Digest | — | System store | ✓ |
| HTTP CONNECT | NTLM | — | System store | ✓ |
| HTTP CONNECT | NTLM | Corp inspection CA | (proxy re-signs) | ✓ |
| HTTP CONNECT | Kerberos | Corp inspection CA | (proxy re-signs) | ✓ |
| HTTP CONNECT | Bearer | Corp inspection CA | (cloud proxy) | ✓ |
| HTTP CONNECT | NTLM | — | Private target CA + add_ca | ✓ |
| HTTP CONNECT | NTLM | — | mTLS (client cert) | ✓ Transparent |
| SOCKS4 | None | — | System store | ✓ |
| SOCKS4a | None | — | System store | ✓ |
| SOCKS5 | None | — | System store | ✓ (Tor etc.) |
| SOCKS5 | Basic | — | System store | ✓ |
| SOCKS5 | GSSAPI | — | System store | ✓ |
| PAC file | (per PAC) | — | — | ✓ |
| mTLS + SSL inspection | NTLM | Corp CA | Client cert | ⚠ Only if proxy supports mTLS pass-through |

---

## 13. Open Design Questions

- **SOCKS5 UDP**: `UDP ASSOCIATE` command for UDP-capable proxies — needed for QUIC/DNS.
- **Proxy chaining**: Multiple proxies in sequence (`proxy.chain(px1, px2)`).
- **NTLM SSO on Windows**: Current process Windows credentials via SSPI —
  no password in code. On Linux: via Kerberos ticket cache (`KRB5CCNAME`).
- **PAC JavaScript interpreter**: Minimal subset of ECMAScript (only `FindProxyForURL`
  + standard helper functions). Options: `duktape` (3 files, MIT license) or
  custom minimal implementation.
- **Transparent proxy detection**: Automatically detect whether proxy performs TLS inspection
  (via `sock.peer_cert_cn()` — if CN matches target hostname → transparent;
  if CN is corp CA CN → inspection).
- **Proxy error diagnosis**: `proxy.test(target_url) → proxy_diag_handle` — step-by-step diagnosis of which phase fails (TCP, auth, CONNECT, TLS).
