# Chapter 23.2 — Storage Namespace (Client Key/Value Persistence)

> **Core idea:** `storage.*` provides a small, cross-platform key/value store
> for client-side session data (auth tokens, user preferences). Persists per
> browser origin on the web, and to a flat file natively. Always string-typed.

---

## 1. API

| Call | Signature | Description |
|------|-----------|-------------|
| `storage.get(key)` | `(string) -> string` | Value for `key`, or `""` if missing (never `null`). |
| `storage.set(key, value)` | `(string, string) -> void` | Write/overwrite. Value is always a UTF-8 string. |
| `storage.remove(key)` | `(string) -> void` | Delete; no-op if missing. |
| `storage.has(key)` | `(string) -> i32` | `1` if present, `0` if not. |
| `storage.clear()` | `() -> void` | Wipe everything for this origin / namespace. |

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
or `storage.remove` call for the **same key**. Copy the string (e.g. via
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
- call: [storage.remove, "dc_token"]
- call: [storage.remove, "dc_email"]
```

### User preferences (volume, theme, etc.)

```aiml
- decl: [vol_str, string, { call: [storage.get, "audio_volume"] }]
- decl: [vol, f32, 0.8]
- if:
    condition: { neq: [vol_str.len, 0] }
    then:
      - set: [vol, { call: [string.to_f32, vol_str] }]
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
const char* aiml_storage_get   (const char* key);   // "" on missing
void        aiml_storage_set   (const char* key, const char* value);
void        aiml_storage_remove(const char* key);
int32_t     aiml_storage_has   (const char* key);   // 0 / 1
void        aiml_storage_clear (void);
```
