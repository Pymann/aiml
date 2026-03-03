# Kapitel 32.5 — Proxy-Unterstützung

> **Motivation:** In Unternehmensnetzen kommt man ohne Proxy-Unterstützung nicht ins Internet.
> AIML unterstützt alle gängigen Proxy-Protokolle und Authentifizierungsmethoden mit
> sauberer Trennung zwischen Proxy-Auth, Proxy-Zertifikat und Ziel-TLS-Konfiguration —
> genau die Schichtung, die in Go, Python und curl regelmäßig bricht.

---

## 1. Das Problem: Geschichtete Komplexität

```
┌──────────────────────────────────────────────────────────────────────┐
│ AIML-Client                                                           │
│                                                                        │
│  TCP → [Proxy-Auth] → Proxy-Server:Port                               │
│                │                                                       │
│                ├── HTTP CONNECT  ─── Tunnel → Zielserver:Port         │
│                ├── SOCKS4/4a    ─── Tunnel → Zielserver:Port         │
│                └── SOCKS5       ─── Tunnel → Zielserver:Port         │
│                                        │                              │
│                          ┌─────────────▼──────────────────────┐      │
│                          │ Szenario A: SSL-Inspection (MITM)   │      │
│                          │  Proxy terminiert TLS, re-signiert  │      │
│                          │  Ziel-Cert mit Corp-Inspection-CA   │      │
│                          └─────────────────────────────────────┘      │
│                          ┌─────────────────────────────────────┐      │
│                          │ Szenario B: Transparenter Tunnel     │      │
│                          │  TLS direkt mit Ziel (mTLS möglich) │      │
│                          └─────────────────────────────────────┘      │
└──────────────────────────────────────────────────────────────────────┘
```

**Warum Libraries versagen:**
- NTLM: Libraries reconnecten nach `407` → NTLM-Kontext verloren (Verbindung muss offen bleiben)
- Kerberos: Erfordert GSSAPI-Integration, die die meisten HTTP-Stacks nicht einbauen
- SSL-Inspection + eigene Ziel-CA: Zwei separate Trust-Kontexte, die meistens gekoppelt sind
- SOCKS5 + GSSAPI-Auth: Selten vollständig implementiert

---

## 2. Typen

| Typ                   | LLVM  | Beschreibung |
|-----------------------|-------|--------------|
| `proxy_config_handle` | `i8*` | Konfigurationsobjekt — beschreibt Protokoll, Adresse und Auth |

`proxy_config_handle` ist **kein Socket**. Es wird einmalig erstellt, konfiguriert,
und dann auf beliebig viele Sockets angewendet (vor `connect`).

---

## 3. Proxy-Protokolle

### 3.1 HTTP CONNECT (RFC 7231)

Standardprotokoll für Web-Proxies. Der Proxy öffnet einen TCP-Tunnel zum Ziel.

```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new, "proxy.example.com", 8080] }]
# Typ ist standardmäßig HTTP CONNECT
```

Ablauf:
```
Client → Proxy:  CONNECT targethost:443 HTTP/1.1\r\nHost: targethost:443\r\n\r\n
Proxy  → Client: HTTP/1.1 200 Connection Established\r\n\r\n
Client → Proxy:  [rohe TLS-Bytes, die Proxy weiterleitet]
```

### 3.2 SOCKS4

Einfachstes Proxy-Protokoll. TCP only, kein Auth, kein IPv6, kein DNS-at-Proxy.

```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new_socks4, "socks4proxy.example.com", 1080] }]
```

Ablauf (binäres Protokoll):
```
Client → Proxy:  VER=4, CMD=1(CONNECT), DSTPORT, DSTIP, USERID\0
Proxy  → Client: VER=0, REP=90(granted) / 91(rejected), PORT, IP
Client → Proxy:  [rohe Verbindungsbytes]
```

Einschränkungen: Nur IPv4-Adressen; Hostname muss Client-seitig aufgelöst werden.

### 3.3 SOCKS4a

Erweiterung von SOCKS4 mit DNS-Auflösung am Proxy (wichtig für interne Hostnamen).

```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new_socks4a, "socks4aproxy.example.com", 1080] }]
```

Wenn DSTIP = `0.0.0.x` (x ≠ 0) und nach USERID ein Hostname folgt, löst der Proxy
den Namen auf. AIML wählt SOCKS4a automatisch wenn der Ziel-Host kein IPv4-Literal ist.

### 3.4 SOCKS5 (RFC 1928)

Vollständigstes Protokoll: IPv4/IPv6/Hostname, Auth, TCP + UDP.

```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new_socks5, "tor.local", 9050] }]
# Kein Auth → Tor / anonyme SOCKS5-Proxies

- decl: [px2, proxy_config_handle]
- set: [px2, { call: [proxy.new_socks5, "socks5.corp.com", 1080] }]
- call: [px2.set_auth_basic, "user", "pass"]
# SOCKS5 Username/Password-Auth (RFC 1929)
```

Ablauf:
```
(1) Auth-Negotiation:
    Client → Proxy:  VER=5, NMETHODS, [METHODS...]
                     Methods: 0x00=Kein Auth, 0x02=User/Pass, 0x01=GSSAPI
    Proxy  → Client: VER=5, METHOD (gewählt)

(2) Auth (falls METHOD=0x02):
    Client → Proxy:  VER=1, ULEN, USERNAME, PLEN, PASSWORD
    Proxy  → Client: VER=1, STATUS (0x00=Erfolg)

(3) CONNECT-Request:
    Client → Proxy:  VER=5, CMD=1, RSV=0, ATYP, DST.ADDR, DST.PORT
                     ATYP: 0x01=IPv4, 0x03=Hostname, 0x04=IPv6
    Proxy  → Client: VER=5, REP=0(Erfolg), RSV, BNDATYP, BND.ADDR, BND.PORT

(4) Client → Proxy:  [rohe Verbindungsbytes]
```

### 3.5 HTTPS-Proxy (HTTP CONNECT über TLS)

Seltenerer Fall: Das Proxy-Kontrollprotokoll selbst ist TLS-verschlüsselt.

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

## 4. Authentifizierungs-Methoden

### 4.1 Kein Auth (Default)

```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new_socks5, "tor.local", 9050] }]
# kein set_auth_* → kein Auth
```

### 4.2 HTTP Basic

```aiml
- call: [px.set_auth_basic, "username", "p@ssw0rd"]
# Sendet: Proxy-Authorization: Basic <Base64(user:pass)>
# Für SOCKS5: Username/Password-Auth (RFC 1929)
```

### 4.3 HTTP Digest

```aiml
- call: [px.set_auth_digest, "username", "p@ssw0rd"]
# MD5-Challenge-Response (RFC 7616)
# Proxy sendet 407 mit Proxy-Authenticate: Digest realm=... nonce=...
# Client berechnet: MD5(MD5(user:realm:pass):nonce:MD5(method:uri))
```

### 4.4 NTLM (NTLMv2)

```aiml
- call: [px.set_auth_ntlm, "username", "p@ssw0rd"]
# Domain aus Proxy-Hostname abgeleitet

- call: [px.set_auth_ntlm, "CORP\\username", "p@ssw0rd"]
# Domain-Prefix-Syntax

- call: [px.set_auth_ntlm, "username", "p@ssw0rd", "CORP"]
# Explizite Domain als drittes Argument
```

3-Phasen-Handshake auf **persistenter Verbindung** (kein Reconnect nach 407):
```
Client → NEGOTIATE (Flags: NTLMv2, Unicode)
Proxy  → 407 + CHALLENGE (ServerChallenge 8 Bytes)
Client → AUTHENTICATE (NTProofStr = HMAC-MD5(NTHash, Challenge))
Proxy  → 200 Connection Established
```

### 4.5 Kerberos / Negotiate (GSSAPI)

```aiml
- call: [px.set_auth_kerberos, "HTTP/proxy.corp.com@CORP.LOCAL"]
# Proxy-Authorization: Negotiate <Base64(Kerberos-Token)>
# Benötigt: güliges Kerberos-Ticket (kinit oder Keytab)

- call: [px.set_auth_kerberos_keytab, "HTTP/proxy.corp.com@CORP.LOCAL",
                                       "/etc/myapp/service.keytab"]
# Mit Keytab (für Dienste ohne interaktives kinit)
```

Ablauf:
```
Client → gss_init_sec_context(service_name) → Token
Client → Proxy: Proxy-Authorization: Negotiate <Base64(Token)>
Proxy  → Client: 407 + Proxy-Authenticate: Negotiate <Base64(ServerToken)>
Client → gss_init_sec_context(ServerToken) → Token2
Client → Proxy: Proxy-Authorization: Negotiate <Base64(Token2)>
Proxy  → Client: 200 Connection Established
```

Implementiert via `libgssapi_krb5` (`-lgssapi_krb5`).

### 4.6 Bearer / OAuth2 (Cloud-Proxies wie Zscaler)

```aiml
- call: [px.set_auth_bearer, "eyJhbGci..."]
# Proxy-Authorization: Bearer <token>
# Für Cloud-Proxies (Zscaler ZPA, Netskope, etc.)
```

---

## 5. Vollständige `proxy_config_handle`-Methoden

| Methode                    | Argumente                              | Rückgabe | Beschreibung |
|----------------------------|----------------------------------------|----------|--------------|
| `set_auth_none`            | —                                      | —        | Kein Auth (Default) |
| `set_auth_basic`           | `user: string, pass: string`           | —        | HTTP Basic / SOCKS5 User-Pass |
| `set_auth_digest`          | `user: string, pass: string`           | —        | HTTP Digest (MD5) |
| `set_auth_ntlm`            | `user: string, pass: string`           | —        | NTLMv2 |
| `set_auth_ntlm`            | `user: string, pass: string, domain: string` | — | NTLMv2 + explizite Domain |
| `set_auth_kerberos`        | `spn: string`                          | —        | Kerberos via GSSAPI (bestehendes Ticket) |
| `set_auth_kerberos_keytab` | `spn: string, keytab: path`            | —        | Kerberos mit Keytab |
| `set_auth_bearer`          | `token: string`                        | —        | Bearer Token (OAuth2) |
| `set_ca`                   | `ca: path`                             | —        | CA für Proxy-Zertifikat (SSL-Inspection oder HTTPS-Proxy) |
| `set_proxy_tls`            | `enabled: bool`                        | —        | Proxy-Kontrollkanal selbst via TLS (Default: false) |
| `set_timeout`              | `ms: i32`                              | —        | Timeout für Proxy-Verbindung + Auth-Handshake |
| `set_no_proxy`             | `hosts: string`                        | —        | Komma-sep. Hostnamen, die direkt verbunden werden |
| `set_resolve_via_proxy`    | `enabled: bool`                        | —        | DNS-Auflösung am Proxy (SOCKS4a/SOCKS5, Default: true) |

---

## 6. `proxy`-Namespace-Referenz

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `{ call: [proxy.new, host, port] }` | `proxy_config_handle` | HTTP CONNECT Proxy |
| `{ call: [proxy.new_socks4, host, port] }` | `proxy_config_handle` | SOCKS4 |
| `{ call: [proxy.new_socks4a, host, port] }` | `proxy_config_handle` | SOCKS4a (DNS at Proxy) |
| `{ call: [proxy.new_socks5, host, port] }` | `proxy_config_handle` | SOCKS5 |
| `{ call: [proxy.from_env] }` | `proxy_config_handle` | Liest `HTTPS_PROXY` / `HTTP_PROXY` / `NO_PROXY` |
| `{ call: [proxy.from_pac, pac_url] }` | `proxy_config_handle` | PAC-Datei via URL (`pac_url: url`) |
| `{ call: [proxy.from_pac_file, pac_path] }` | `proxy_config_handle` | Lokale PAC-Datei (`pac_path: path`) |
| `{ call: [proxy.none] }` | `proxy_config_handle` | Explizit kein Proxy (überschreibt Umgebung) |

### 6.1 Umgebungsvariablen (`proxy.from_env`)

```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.from_env] }]
# Liest (in Priorität):
#   HTTPS_PROXY, https_proxy  → für HTTPS-Verbindungen
#   HTTP_PROXY, http_proxy    → für HTTP-Verbindungen
#   ALL_PROXY, all_proxy      → Fallback für beide
#   NO_PROXY, no_proxy        → Ausnahmen (Komma-sep., Wildcards: *.corp.com)
# Gibt nullptr zurück wenn keine Proxy-Variable gesetzt

- decl: [sock, tls_socket_handle]
- set: [sock, { call: [tls.socket] }]
- call: [sock.set_proxy, px]   # nullptr → kein Proxy, kein Fehler
- call: [tls.connect, sock, "api.example.com", 443]
```

### 6.2 PAC-Dateien (Proxy Auto-Config)

PAC-Dateien sind JavaScript-Dateien, die per URL und Host entscheiden, welcher Proxy
verwendet werden soll (`FindProxyForURL`-Funktion). Weit verbreitet in Unternehmen.

```aiml
# PAC von URL laden (typischer Unternehmensfall: WPAD)
- decl: [wpad_url, url]
- set: [wpad_url, "http://wpad.corp.com/wpad.dat"]
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.from_pac, wpad_url] }]

# Oder lokale PAC-Datei
- decl: [pac_file, path]
- set: [pac_file, "/etc/myapp/proxy.pac"]
- set: [px, { call: [proxy.from_pac_file, pac_file] }]

# set_auth gilt dann für alle Proxies die das PAC zurückgibt
- call: [px.set_auth_ntlm, "CORP\\user", "pass"]
- decl: [insp_ca, path]
- set: [insp_ca, "/etc/corp/inspection_ca.crt"]
- call: [px.set_ca, insp_ca]

- decl: [sock, tls_socket_handle]
- set: [sock, { call: [tls.socket] }]
- call: [sock.set_proxy, px]
- call: [tls.connect, sock, "github.com", 443]
# PAC entscheidet intern: DIRECT oder PROXY proxy1.corp.com:8080
```

PAC-Implementierung: minimaler JavaScript-Interpreter (nur `FindProxyForURL` +
Standard-Hilfsfunktionen wie `isInNet`, `dnsDomainIs`, `shExpMatch`).

---

## 7. Erweiterungen an Socket-Typen

Beide Handle-Typen bekommen:

| Methode | Signatur | Beschreibung |
|---------|----------|--------------|
| `set_proxy` | `(proxy: proxy_config_handle) → void` | Proxy-Konfiguration anwenden |
| `add_ca` | `(ca_path: path) → void` | Zusätzliche CA hinzufügen (kumulativ) |

`add_ca` (im Gegensatz zu `set_ca`, das den System-Store ersetzt) **fügt hinzu** —
nötig wenn Proxy-CA und Ziel-CA gleichzeitig vertraut werden sollen:

```aiml
- call: [tls.set_ca, sock, "/etc/corp/inspection_ca.crt"]   # ersetzt System-Store
- call: [sock.add_ca, "/etc/myapp/target_ca.crt"]           # zusätzlich
```

---

## 8. Gängige Proxy-Produkte: Kompatibilitätstabelle

| Produkt | Protokoll | Auth-Methoden | SSL-Inspection | Anmerkung |
|---------|-----------|---------------|----------------|-----------|
| **Squid** | HTTP CONNECT | Basic, Digest, NTLM, Kerberos | Optional (mit ssl-bump) | Häufigster Open-Source-Proxy |
| **BlueCoat / Symantec ProxySG** | HTTP CONNECT | NTLM, Kerberos, RADIUS | Ja (Standard) | Corporate Appliance |
| **Zscaler ZIA** | HTTP CONNECT | Bearer/OAuth2, SAML, Kerberos | Ja | Cloud-Proxy; braucht Bearer-Token |
| **Netskope** | HTTP CONNECT | Bearer/OAuth2, SAML | Ja | Cloud-Proxy |
| **Cisco WSA** | HTTP CONNECT | NTLM, Kerberos, LDAP, Basic | Optional | Web Security Appliance |
| **F5 BIG-IP** | HTTP CONNECT | NTLM, Kerberos, Basic | Optional | Forward Proxy Modul |
| **Microsoft ISA / TMG** | HTTP CONNECT | NTLM, Kerberos, Basic | Nein | Veraltet, noch in Betrieb |
| **Privoxy** | HTTP CONNECT | Kein Auth | Nein | Privacy-Proxy; häufig hinter Tor |
| **Tor** | SOCKS5 | Kein Auth | Nein | Anonymität; DNS am Proxy (wichtig!) |
| **SSH -D** | SOCKS5 | Kein Auth (SSH-Auth davor) | Nein | `ssh -D 1080 user@bastion` |
| **Dante** | SOCKS4/5 | Basic, GSSAPI | Nein | Open-Source SOCKS-Server |
| **Shadowsocks** | SOCKS5 | Kein Auth (eigene Verschlüsselung) | Nein | Obfuskierter Proxy |
| **HAProxy** | SOCKS5, HTTP | Basic | Nein | Load-Balancer mit Proxy-Modus |
| **Charles / Burp Suite** | HTTP CONNECT | Basic | Ja (Dev/Testing) | Entwicklungs-Proxies |
| **nginx** | HTTP CONNECT | Basic | Optional | Mit `ngx_stream_proxy_module` |

### 8.1 Konfigurationsbeispiele für typische Produkte

**Squid mit NTLM:**
```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new, "squid.corp.local", 3128] }]
- call: [px.set_auth_ntlm, "CORP\\user", "pass"]
```

**BlueCoat / Symantec ProxySG (SSL-Inspection + Kerberos):**
```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new, "bluecoat.corp.com", 8080] }]
- call: [px.set_auth_kerberos, "HTTP/bluecoat.corp.com@CORP.COM"]
- call: [px.set_ca, "/etc/corp/bluecoat_inspection_ca.crt"]
```

**Zscaler ZIA (Cloud-Proxy, Bearer-Token):**
```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new, "gateway.corp.zscaler.net", 80] }]
- call: [px.set_auth_bearer, "eyJhbGci...zscaler-token"]
- call: [px.set_ca, "/etc/corp/zscaler_root_ca.crt"]
# Zscaler signiert re-inspizierte Certs mit eigenem Root-CA
```

**Tor (SOCKS5, kein Auth, DNS am Proxy):**
```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new_socks5, "127.0.0.1", 9050] }]
# DNS-Auflösung passiert am Tor-Exit-Node (nicht lokal) → privacy!
# set_resolve_via_proxy ist für SOCKS5 standardmäßig true

- decl: [sock, tls_socket_handle]
- set: [sock, { call: [tls.socket] }]
- call: [sock.set_proxy, px]
- call: [tls.connect, sock, "duckduckgo.com", 443]
```

**SSH SOCKS5-Tunnel:**
```aiml
# Vorher: ssh -D 1080 -N user@bastion.corp.com
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.new_socks5, "127.0.0.1", 1080] }]

- decl: [sock, tls_socket_handle]
- set: [sock, { call: [tls.socket] }]
- call: [sock.set_proxy, px]
- call: [tls.connect, sock, "internal-api.corp.com", 443]
# Verbindung geht: lokal → SSH-Tunnel → bastion → internal-api
```

**WPAD (automatische Proxy-Erkennung im Unternehmen):**
```aiml
- decl: [px, proxy_config_handle]
- set: [px, { call: [proxy.from_pac, "http://wpad/wpad.dat"] }]
- call: [px.set_auth_ntlm, "CORP\\user", "pass"]
# PAC-Datei entscheidet pro Ziel-URL: DIRECT / PROXY host:port
```

---

## 9. NTLM-Implementierungsdetail

### 9.1 Warum keine externe Library

- `libntlm`: veraltet, nur NTLMv1
- `gss-ntlmssp`: Kerberos-Abhängigkeit, komplex zu linken
- OpenSSL bietet alle nötigen Primitive: `MD4`, `HMAC-MD5`, `RAND_bytes`, `EVP_EncodeBlock`
- Eigenimplementierung NTLMv2: ca. 400 Zeilen C++ in `src/sys/ntlm.cpp`

### 9.2 NTLMv2-Hash

```
NTHash    = MD4(UTF-16LE(password))
NTHashV2  = HMAC-MD5(NTHash, uppercase(user+domain) als UTF-16LE)
NTProofStr= HMAC-MD5(NTHashV2,
              ServerChallenge‖ClientChallenge‖Timestamp‖TargetInfo)
NTResponse= NTProofStr ‖ Blob(ClientChallenge, Timestamp, TargetInfo)
```

### 9.3 Kritisch: Persistente Verbindung

```
Schritt 1: TCP CONNECT zu Proxy
Schritt 2: CONNECT target HTTP/1.1 + Proxy-Authorization: NTLM <NEGOTIATE>
Schritt 3: 407 + Proxy-Authenticate: NTLM <CHALLENGE>  ← Verbindung OFFEN LASSEN
Schritt 4: CONNECT target HTTP/1.1 + Proxy-Authorization: NTLM <AUTHENTICATE>
Schritt 5: 200 Connection Established
```

Libraries die nach Schritt 3 reconnecten → NTLM bricht. AIML führt alle 5 Schritte
auf demselben File-Descriptor durch.

---

## 10. Interne Struktur (`proxy_runtime.h/cpp`)

```cpp
enum class ProxyProtocol { HttpConnect, Socks4, Socks4a, Socks5 };
enum class ProxyAuth     { None, Basic, Digest, NTLMv2, Kerberos, Bearer };

struct AimlProxy {
    ProxyProtocol protocol;
    char*         host;
    int           port;
    bool          proxy_tls;    // Kontrollkanal selbst via TLS
    char*         ca_path;      // CA für Proxy (Inspection oder HTTPS)
    ProxyAuth     auth;
    char*         username;
    char*         password;
    char*         domain;       // NTLM
    char*         spn;          // Kerberos Service Principal Name
    char*         keytab_path;  // Kerberos Keytab
    char*         bearer_token; // OAuth2
    int           timeout_ms;
    char*         no_proxy;     // Komma-sep.
    bool          resolve_via_proxy; // DNS am Proxy (SOCKS4a/5)
    // PAC
    char*         pac_url;
    char*         pac_js;       // gecachter PAC-Inhalt
};

// Hauptfunktion: baut Tunnel auf fd auf (TCP zu Proxy schon verbunden)
int aiml_proxy_tunnel(AimlProxy* proxy, int fd,
                      const char* target_host, int target_port);
```

`aiml_proxy_tunnel` dispatcht intern auf:
- `proxy_http_connect_tunnel()` — HTTP CONNECT + Auth (Basic/Digest/NTLM/Kerberos/Bearer)
- `proxy_socks4_tunnel()` — SOCKS4 binäres Protokoll
- `proxy_socks4a_tunnel()` — SOCKS4a mit Hostname
- `proxy_socks5_tunnel()` — SOCKS5 Auth-Negotiation + CONNECT

---

## 11. Vollständiges Enterprise-Beispiel

```aiml
# Situation:
# - Windows-Firmennetz, WPAD-basierte Proxy-Erkennung
# - Proxy: BlueCoat ProxySG, NTLM-Auth, SSL-Inspection
# - Ziel: Vault-API-Server intern, Bearer-Token-Auth

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

## 12. Kombinationsmatrix

| Proxy-Protokoll | Auth | Proxy-CA | Ziel-Cert | Unterstützt? |
|----------------|------|----------|-----------|-------------|
| HTTP CONNECT | Kein | — | System-Store | ✓ |
| HTTP CONNECT | Basic | — | System-Store | ✓ |
| HTTP CONNECT | Digest | — | System-Store | ✓ |
| HTTP CONNECT | NTLM | — | System-Store | ✓ |
| HTTP CONNECT | NTLM | Corp-Inspection-CA | (Proxy re-signiert) | ✓ |
| HTTP CONNECT | Kerberos | Corp-Inspection-CA | (Proxy re-signiert) | ✓ |
| HTTP CONNECT | Bearer | Corp-Inspection-CA | (Cloud-Proxy) | ✓ |
| HTTP CONNECT | NTLM | — | Private Ziel-CA + add_ca | ✓ |
| HTTP CONNECT | NTLM | — | mTLS (Client-Cert) | ✓ Transparent |
| SOCKS4 | Kein | — | System-Store | ✓ |
| SOCKS4a | Kein | — | System-Store | ✓ |
| SOCKS5 | Kein | — | System-Store | ✓ (Tor etc.) |
| SOCKS5 | Basic | — | System-Store | ✓ |
| SOCKS5 | GSSAPI | — | System-Store | ✓ |
| PAC-Datei | (je nach PAC) | — | — | ✓ |
| mTLS + SSL-Inspection | NTLM | Corp-CA | Client-Cert | ⚠ Nur wenn Proxy mTLS-Pass-Through |

---

## 13. Offene Designfragen

- **SOCKS5-UDP**: `UDP ASSOCIATE`-Kommando für UDP-fähige Proxies — nötig für QUIC/DNS.
- **Proxy-Chaining**: Mehrere Proxies hintereinander (`proxy.chain(px1, px2)`).
- **NTLM SSO auf Windows**: Aktuelle Windows-Credentials des Prozesses via SSPI —
  ohne Passwort im Code. Auf Linux: via Kerberos-Ticket-Cache (`KRB5CCNAME`).
- **PAC-JavaScript-Interpreter**: Minimaler Subset von ECMAScript (nur `FindProxyForURL`
  + Standard-Helper-Funktionen). Optionen: `duktape` (3 Dateien, MIT-Lizenz) oder
  eigene Minimal-Implementierung.
- **Transparent Proxy Detection**: Automatisch erkennen ob Proxy TLS-Inspection macht
  (via `sock.peer_cert_cn()` — wenn CN dem Ziel-Hostnamen entspricht → transparent;
  wenn CN der Corp-CA-CN ist → Inspection).
- **Proxy-Fehlerdiagnose**: `proxy.test(target_url) → proxy_diag_handle` — Schritt-für-Schritt-Diagnose welche Phase fehlschlägt (TCP, Auth, CONNECT, TLS).
