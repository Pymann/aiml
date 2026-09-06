// http.fetch_text(url) → string — a one-shot HTTP GET that returns the response
// body as text. One API, two backends:
//   - WASM (browser): window.fetch via EM_ASYNC_JS (Asyncify) — the browser client
//     has no AIML HTTP client (http_runtime is native-only), only WebSocket + fetch.
//   - Native: reuses aiml_http_get (http_runtime) and returns the body.
// Returns "" on any error (never null). The result is a refcounted AIML string.
//
// This is what lets the browser client read a discovery endpoint such as
// GET /servers.json and pick a game server, without hard-coding one.

#include "string_runtime.h"   // aiml_str_dup — refcounted result
#include <cstdlib>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Async browser fetch. Returns a _malloc'd C string (caller frees) or 0 on error.
EM_ASYNC_JS(char*, js_fetch_text, (const char* url), {
    try {
        const u = UTF8ToString(url);
        const resp = await fetch(u);
        if (!resp.ok) return 0;
        const text = await resp.text();
        const len = lengthBytesUTF8(text) + 1;
        const ptr = _malloc(len);
        stringToUTF8(text, ptr, len);
        return ptr;
    } catch (e) {
        return 0;
    }
});

extern "C" char* aiml_fetch_text(const char* url) {
    if (!url) return aiml_str_dup("");
    char* raw = js_fetch_text(url);          // JS-malloc'd
    if (!raw) return aiml_str_dup("");
    char* out = aiml_str_dup(raw);           // refcounted copy for AIML
    free(raw);
    return out;
}

#else
#include "http_runtime.h"    // aiml_url_parse / aiml_http_get / AimlHttpResponse

extern "C" char* aiml_fetch_text(const char* url) {
    if (!url) return aiml_str_dup("");
    AimlUrl* parsed = aiml_url_parse(url);       // aiml_http_get expects AimlUrl*
    if (!parsed) return aiml_str_dup("");
    void* resp = aiml_http_get(parsed);          // → AimlHttpResponse*
    if (!resp) return aiml_str_dup("");
    AimlHttpResponse* r = static_cast<AimlHttpResponse*>(resp);
    char* out = aiml_str_dup(r->body ? r->body : "");
    aiml_http_resp_free(resp);
    return out;
}

#endif
