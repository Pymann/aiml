# Chapter 23.2 — Storage Namespace (Client Key/Value Persistence)

> **Core idea:** `storage.*` provides a small, cross-platform key/value store
> for client-side session data (auth tokens, user preferences). Persists per
> browser origin on the web, and to a flat file natively. Always string-typed.

---

## 1. API

| Call | Signature | Description |
|------|-----------|-------------|
| `storage.get(key)` / `storage.get(key, d)` | `(string[, string]) -> string` | Value for `key`; if missing, `d`, else the null string (spec/71 §3). Never throws. |
| `storage.set(key, value)` | `(string, string) -> void` | Write/overwrite. Value is always a UTF-8 string. |
| `storage.delete(key)` | `(string) -> i32` | Delete; `1` if `key` was there, else `0`. |
| `storage.has(key)` | `(string) -> i32` | `1` if present, `0` if not. |
| `storage.clear()` | `() -> void` | Wipe everything for this origin / namespace. |
| `storage.host()` | `() -> string` | Web: the page hostname (`window.location.hostname`); native: `"localhost"`. |
| `storage.is_secure()` | `() -> i32` | Web: `1` if the page is served over HTTPS (`location.protocol === "https:"`), else `0`; native: always `0`. |
| `storage.session_get(key)` | `(string) -> string` | Web: `window.sessionStorage` (this tab only, gone when it closes); native: an in-memory map of this process. The null string if missing. |
| `storage.session_set(key, value)` | `(string, string) -> void` | Write/overwrite in the session store. |
| `storage.session_delete(key)` | `(string) -> i32` | Delete from the session store; `1` if `key` was there, else `0`. |
| `storage.hash()` | `() -> string` | Web: the page's URL fragment without the `#` (`location.hash`), `""` if none; native: what `set_hash` last set (`""` at start). |
| `storage.set_hash(value)` | `(string) -> void` | Web: replaces the fragment **without** a navigation or a history entry (`history.replaceState`); `""` removes it including the `#`. Native: remembered for `hash()`. |

The names follow the keyed-container vocabulary of spec/71 (`set`, `get`, `has`, `delete`);
`storage.remove` and `storage.session_remove` are retired and name their replacement.

The session store and the fragment calls (added 2026-11-25, kontor `web_ui.md` FW-93)
let a web client keep a login for the life of one tab, and take a token that a server
redirect put into the fragment (`/ui/#token=…`) out of the address bar at once.

`storage.host` / `storage.is_secure` let a web build derive its server URL from the
page it was served by — e.g. pick `wss://` on an HTTPS page (a browser blocks a plain
`ws://` connection from an HTTPS page as mixed content) and `ws://` otherwise.

---

## 2. Backends

| Platform | Backend | Persistence | Quota |
|----------|---------|-------------|-------|
| Web (Emscripten) | `window.localStorage` via EM_JS | Per-origin, no expiration | ~5–10 MB browser-dependent |
| Native (POSIX)   | In-memory map mirrored to disk | Across runs, until file deleted | Unlimited |

The native file path is
`${XDG_DATA_HOME:-~/.local/share}/aiml/storage.json`, format:

```
key1\tvalue1
key2\tvalue2
```

Tab-separated lines, one per entry. Writes are atomic (tmp file + `rename`).

> **Note on keys:** Avoid `\t` and `\n` in keys on the native backend — they
> are silently dropped from the persisted file (the in-memory copy still has
> them this run, but the next start will not see them).

---

## 3. Lifetime

The pointer returned by `storage.get` is valid until the next `storage.set`
or `storage.delete` call for the **same key**. Copy the string (e.g. via
`decl: [s, string, { call: [storage.get, "k"] }]`) if you need it longer.

---

## 4. Examples

### Keep-logged-in token

```aiml
# On successful auth, when the "stay" checkbox is checked:
- call: [storage.set, "dc_token", token]
- call: [storage.set, "dc_email", email]

# On next start, try auto-login:
- if:
    condition: { neq: [{ call: [storage.has, "dc_token"] }, 0] }
    then:
      - decl: [t, string, { call: [storage.get, "dc_token"] }]
      - decl: [req, TokenReq]
      - set:  [req.token, t]
      - call: [session.send_rpc, auth_nid, "validate_token", 0, req]

# On logout:
- call: [storage.delete, "dc_token"]
- call: [storage.delete, "dc_email"]
```

### User preferences (volume, theme, etc.)

```aiml
- decl: [vol_str, string, { call: [storage.get, "audio_volume", "0.8"] }]   # default when unset
- decl: [vol, f32, { call: [vol_str.parse_f32] }]
```

---

## 5. Security Notes

- localStorage is **NOT** access-controlled within an origin. Any script on
  the same origin (including XSS payloads) can read tokens. Mitigate via
  CSP, sandboxed iframes, or moving to `httpOnly` cookies once a real auth
  story is in place.
- The native file is plain text; sensitive secrets should be encrypted
  separately. Use `db.open_kv` with a passphrase scheme if you need at-rest
  encryption.
- `storage.clear()` is destructive — guard behind explicit user actions.

---

## 6. C API

```c
const char* aiml_storage_get   (const char* key);   // NULL on missing
void        aiml_storage_set   (const char* key, const char* value);
int32_t     aiml_storage_delete(const char* key);   // 1 if it was there, else 0
int32_t     aiml_storage_has   (const char* key);   // 0 / 1
void        aiml_storage_clear (void);
```
