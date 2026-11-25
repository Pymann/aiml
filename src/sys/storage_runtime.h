/*
 * storage_runtime.h — AIML `storage` namespace
 *
 * Cross-platform key-value persistence intended for client-side session data
 * (auth tokens, user preferences). Backends:
 *
 *   - Emscripten/Web : mapped 1:1 to `window.localStorage` via EM_JS bridges.
 *                      Persists per browser origin, ~5–10 MB quota, no eviction.
 *   - Native (POSIX) : in-memory map mirrored to `${XDG_DATA_HOME:-~/.local/share}/
 *                      aiml/storage.json` (best-effort, written on every set).
 *
 * Values are always plain UTF-8 strings. Callers that need to persist binary
 * data should base64-encode it first.
 *
 * Lifetime contract: returned C-strings live until the next `set` / `remove`
 * call for the same key. Callers must copy the string if they need a longer
 * lifetime. `get` on a missing key returns an empty string ("") — never NULL.
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const char* aiml_storage_get   (const char* key);
void        aiml_storage_set   (const char* key, const char* value);
void        aiml_storage_remove(const char* key);
int32_t     aiml_storage_has   (const char* key);
void        aiml_storage_clear (void);

/* Hostname of the page currently serving this code. On the web returns
 * `window.location.hostname` (e.g. "192.168.0.142" or "localhost").
 * Natively returns "localhost".
 * Returned pointer is valid until the next call to this function. */
const char* aiml_storage_host  (void);
int32_t     aiml_storage_is_secure(void);   /* 1 if the page is served over https (WASM); 0 native */
const char* aiml_storage_session_get   (const char* key);   /* sessionStorage (WASM) / process map */
void        aiml_storage_session_set   (const char* key, const char* value);
void        aiml_storage_session_remove(const char* key);
const char* aiml_storage_hash          (void);              /* URL fragment without '#' */
void        aiml_storage_set_hash      (const char* value); /* replaceState, no navigation */

#ifdef __cplusplus
}
#endif
