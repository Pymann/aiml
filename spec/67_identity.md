# 67 — Identity: base64, LDAP, SAML 2.0

Status: implemented (native targets; not in the WASM runtime).
Runtime: `src/sys/base64_runtime.cpp`, `src/sys/ldap_runtime.cpp`,
`src/sys/saml_runtime.cpp` (declarations: `src/sys/identity_runtime.h`).
Libraries: OpenLDAP `libldap`/`liblber`, `libxmlsec1` + `libxmlsec1-openssl`,
`libxml2`, `libxslt` (see §6).

## 1. Purpose

Programs that serve people — artifact stores, control planes — need the same
three things to log someone in: decode an HTTP `Basic` header, check a password
against the company directory, accept a single sign-on assertion.  These are
cross-tool infrastructure, so they live in the runtime once (style guideline
§2), not as AIML helpers copied into every program.

The builtins are **mechanisms**, not policy: which LDAP base, which group grants
which role, which IdP certificate is trusted — that is configuration of the
program that uses them.  The security rules that must never be left to a caller
(§3.2, §4.3) are fixed inside the builtins.

## 2. `base64` (RFC 4648)

| Function | Signature | Notes |
|---|---|---|
| `base64.encode` | `(x: string \| buffer<u8>) -> string` | standard alphabet, `=` padding |
| `base64.url_encode` | `(x: string \| buffer<u8>) -> string` | URL-safe alphabet (`-_`), no padding |
| `base64.decode` | `(s: string) -> buffer<u8>` | binary-safe; **empty buffer** on invalid input |
| `base64.url_decode` | `(s: string) -> buffer<u8>` | URL-safe alphabet |
| `base64.decode_str` | `(s: string, url: i32 = 0) -> string` | text convenience; **null** on invalid input; a decoded NUL byte ends the string |

- One entry point for text and binary: a `buffer<u8>` argument is encoded from
  its pointer and length (like `hash.sha256`, guideline §1).
- Decoding ignores whitespace (`\n`, `\r`, `\t`, space) and accepts missing
  padding.  It rejects characters outside the chosen alphabet, data after
  padding, more than two `=`, and a length that cannot carry whole bytes.
  Invalid input never crashes and never yields partial output.

```yaml
- decl: [auth, string, { call: [req.header, "authorization"] }]    # "Basic dXNlcjpwdw=="
- decl: [creds, string, { call: [base64.decode_str, { call: [auth.substr, 6] }] }]   # "user:pw"
- decl: [raw, buffer<u8>, { call: [base64.decode, pem_body] }]     # binary, NUL-safe
```

## 3. `ldap` (OpenLDAP libldap)

### 3.1 API

| Function | Signature | Result |
|---|---|---|
| `ldap.open` | `(url: string) -> ldap_handle` | `ldap://host:port` or `ldaps://…`; null if the URL is unusable.  Does not connect yet. |
| `h.set_timeout` | `(ms: i32)` | network + operation timeout (default 10000 ms) |
| `h.start_tls` | `() -> i32` | `1` ok, `-1` error (StartTLS on an `ldap://` connection) |
| `h.bind` | `(dn: string, password: string) -> i32` | `1` ok, `0` invalid credentials, `-1` error (unreachable, protocol) |
| `h.search_dns` | `(base: string, filter: string) -> list<string>` | DNs of the subtree matches (≤ 1000); empty on error |
| `h.attr_values` | `(dn: string, attr: string) -> list<string>` | all values of one attribute of one entry; empty on error/missing |
| `h.error` | `() -> string` | last error with the server's diagnostic text, `""` after success |
| `h.close` | `()` | unbinds and frees; the variable becomes null, a second `close` is a no-op |
| `ldap.escape_filter` | `(s: string) -> string` | RFC 4515: `*` `(` `)` `\` → `\2a` `\28` `\29` `\5c` |
| `ldap.escape_dn` | `(s: string) -> string` | RFC 4514: `, + " \ < > ; =` and a leading `#`/space or trailing space are backslash-escaped, control bytes as `\XX` |

LDAPv3 is used, referrals are not followed.  `ldap_handle` is a plain resource
(like `ssh_session`): it is **not** released at scope exit — call `close()`.

### 3.2 Fixed rules

- **An empty password is rejected before contacting the server** (`bind`
  returns `0`, `error()` says why).  A simple bind with a DN and an empty
  password is an *unauthenticated bind* (RFC 4513 §5.1.2) that many servers
  answer with success — the classic login bypass.  An empty DN is rejected the
  same way (anonymous bind).
- Everything a user typed goes through `escape_filter` / `escape_dn` before it
  becomes part of a filter or DN.  Without it, a login of `*` matches every
  entry, and `x)(uid=*` rewrites the filter.

### 3.3 Threads

libldap 2.6 is thread-safe, a single `ldap_handle` is not meant to be shared by
concurrent callers.  Use one handle per thread (e.g. open, bind, close inside an
HTTP handler).

### 3.4 Typical login (search, then bind as the user)

```yaml
- decl: [conn, ldap_handle, { call: [ldap.open, "ldaps://ldap.example.org"] }]
- call: [conn.bind, service_dn, service_pw]                    # read access for the search
- decl: [who, string, { call: [ldap.escape_filter, login] }]
- decl: [dns, list<string>, { call: [conn.search_dns, "ou=people,dc=example,dc=org", "(uid=${who})"] }]
- if:
    condition: { eq: [dns.len, 1] }
    then:
      - decl: [user_dn, string, { at: [dns, 0] }]
      - decl: [ok, i32, { call: [conn.bind, user_dn, password] }]          # 1 = authenticated
      - decl: [groups, list<string>, { call: [conn.attr_values, user_dn, "memberOf"] }]
- call: [conn.close]
```

## 4. `saml` (SAML 2.0, service-provider side)

### 4.1 API

| Function | Signature | Result |
|---|---|---|
| `saml.new_request_id` | `() -> string` | `"_"` + 40 hex chars from the OS CSPRNG |
| `saml.authn_request_url` | `(idp_sso_url, sp_entity_id, acs_url, request_id, relay_state = "") -> string` | IdP URL with `SAMLRequest` (and `RelayState`) for the **HTTP-Redirect** binding.  An empty `request_id` generates one. |
| `saml.verify_response` | `(saml_response_b64, idp_cert_pem, sp_entity_id, acs_url, expected_request_id) -> map<string:string>` | see §4.2 |
| `saml.set_clock_skew` | `(seconds: i32)` | tolerance for time checks, process-wide, default 120, max 3600 |

The AuthnRequest is **unsigned** (IdPs accept unsigned requests from a
registered SP; the security of the exchange rests on the signed response).  It
asks for the response by **HTTP-POST** to `acs_url`.  `relay_state` comes back
unchanged with the response (typically: where to go after login).

### 4.2 The result map

`verify_response` returns a `map<string:string>`:

| Key | Value |
|---|---|
| `ok` | `"1"` verified and valid, `"0"` otherwise |
| `error` | reason when `ok` is `"0"` (never contains secrets), `""` otherwise |
| `name_id` | `Subject/NameID` text |
| `name_id_format` | its `Format` attribute |
| `issuer` | the Assertion's `Issuer` (compare it to the configured IdP entity id) |
| `session_index` | `AuthnStatement/@SessionIndex` |
| `request_id` | `InResponseTo` (Response, else SubjectConfirmationData) |
| `not_on_or_after` | `Conditions/@NotOnOrAfter` |
| `signed` | `"response"`, `"assertion"` or `"both"` |
| `attr.<Name>` | each `Attribute` by its `Name`; several `AttributeValue`s are joined with `"\n"` |

On failure the map contains only `ok` and `error`.  `expected_request_id` is the
ID of the AuthnRequest this login started with; pass `""` to accept an
IdP-initiated response (then any `InResponseTo` is reported but not checked).

```yaml
- decl: [res, map<string:string>, { call: [saml.verify_response, form_value, idp_cert, sp_id, acs, pending_id] }]
- if:
    condition: { eq: [{ call: [res.get, "ok"] }, "1"] }
    then:
      - decl: [user, string, { call: [res.get, "name_id"] }]
      - decl: [group_text, string, { call: [res.get, "attr.groups"] }]
      - decl: [groups, list<string>, { call: [group_text.split, "\n"] }]
```

### 4.3 Fixed rules (what makes a response valid)

1. **Trust only the configured certificate.**  The signature is verified with
   `idp_cert_pem` (an X.509 certificate in PEM form); `<KeyInfo>` inside the
   document is ignored.
2. **Signature wrapping is closed by structure.**  The document must contain
   exactly one `<Assertion>`, as a direct child of `<Response>`; all `ID`
   attributes must be unique; the verified signature must be enveloped in the
   element named by its single `Reference` (`URI="#"` + that element's `ID`).
   A signature on the Response covers its one Assertion; otherwise the
   Assertion's own signature must verify.  A present but invalid signature
   always fails, even if the other one is valid.  Values are read only from
   that Assertion.
3. **No DTD, no entities, no comments, no processing instructions.**  Comments
   matter: exclusive C14N drops them, so `alice@example.org<!---->.evil` would
   verify while a naive reader saw a different name.
4. **Algorithms:** reference transforms enveloped-signature and (exclusive)
   C14N without comments; digests SHA-256/384/512; signatures RSA or ECDSA with
   SHA-256/384/512.  SHA-1, XSLT and remote references are rejected.
5. **Protocol checks:** Status `Success`; `Destination` (if present) equals
   `acs_url`; an `AudienceRestriction` admits `sp_entity_id`; `NotBefore` /
   `NotOnOrAfter` hold within the clock skew; a bearer `SubjectConfirmation`
   whose `Recipient` (if present) is `acs_url`, not expired, and whose
   `InResponseTo` matches; with a non-empty `expected_request_id` at least one
   `InResponseTo` must be present and equal.
6. **Encrypted assertions are not supported** and are rejected explicitly.

Replay protection (remembering consumed assertion IDs or request IDs until
they expire) is the caller's job: the builtin is stateless.  A request ID that
is removed from the caller's pending list after one successful login gives the
same protection for SP-initiated logins.

### 4.4 Threads

xmlsec is initialised once per process (thread-safe guard); `verify_response`
may be called from several threads.

## 5. Memory (spec/61, memV3)

Every returned string is an aiml-managed string; `search_dns`/`attr_values`
return real list handles with managed elements; `base64.decode` returns an
owned buffer; `verify_response` returns a map handle (the `sys.env`
representation).  All are released by the normal scope drops — only
`ldap_handle` needs an explicit `close()`.  Probes:
`tests/_mem_sanitize/identity_{base64,ldap_offline,saml}.aiml`.

## 6. Build

`exodus.json` / `exodus2.json` list the packages (`libldap-dev`, `libldap2`,
`libxmlsec1-dev`, `libxmlsec1t64`, `libxmlsec1t64-openssl`, `libxml2-dev`,
`libxml2`, `libxslt1-dev`, `libxslt1.1`), the include paths
(`/usr/include`, `/usr/include/libxml2`, `/usr/include/xmlsec1`) and the
libraries `ldap lber xmlsec1-openssl xmlsec1 xml2 xslt`.  An AOT program
linked against `libaiml.so` needs the same libraries at run time (they are
`NEEDED` entries of `libaiml.so`).

`base64` needs no library and is part of the wasm32 runtime too
(`exodus-wasm-runtime.json`), so a browser client can build an HTTP Basic
`Authorization` header (kontor's web UI does).  `ldap` and `saml` are native
only.

## 7. Tests

| Test | What |
|---|---|
| `tests/test_base64.aiml` | RFC 4648 vectors, URL alphabet, binary round trip with NULs, invalid input |
| `tests/test_ldap_escape.aiml` | RFC 4515 / 4514 escaping |
| `tests/ldap/run.sh` | throwaway `slapd`: bind ok / wrong / empty password / unknown user, search-then-bind, wildcard injection, group members, special-character DNs, unreachable server |
| `tests/test_saml.aiml` | fixtures from `tests/_saml/gen.sh`: valid (Response / Assertion / both signed, IdP-initiated), wrong certificate, tampering, wrapping, unsigned, expired, audience, destination, InResponseTo, comment injection, SHA-1, foreign reference, duplicate IDs, encrypted assertion, DTD; AuthnRequest URL |
