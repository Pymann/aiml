// http.fetch_text(url) → string — a one-shot HTTP GET that returns the response
// body as text. One API, two backends:
//   - WASM (browser): window.fetch via EM_ASYNC_JS (Asyncify) — the browser client
//     has no AIML HTTP client (http_runtime is native-only), only WebSocket + fetch.
//   - Native: reuses aiml_http_get (http_runtime) and returns the body.
// Returns "" on any error (never null). The result is a refcounted AIML string.
//
// This is what lets the browser client read a discovery endpoint such as
// GET /servers.json and pick a game server, without hard-coding one.

// http.fetch(method, url, headers, body) → string and http.fetch_status() → i32
// (spec/32_2 F32-2-60…F32-2-65): the general form for a browser client of a
// REST API — any method, "Name: value" header lines, a text body; the body
// comes back whatever the status, the status from fetch_status().
#include "string_runtime.h"   // aiml_str_dup — refcounted result
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cstdint>
#include <string>

// Status of the last http.fetch on this thread (0 = no answer / none yet).
static thread_local int32_t g_fetch_status = 0;

extern "C" int32_t aiml_fetch_status(void) { return g_fetch_status; }

static std::string fetch_upper(const char* method) {
    std::string m = (method && *method) ? method : "GET";
    for (auto& ch : m) ch = (char)std::toupper((unsigned char)ch);
    return m;
}

static std::string fetch_trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

// Calls `put(name, value)` for every "Name: value" line (F32-2-61).
template <typename Put>
static void fetch_each_header(const char* headers, Put put) {
    if (!headers) return;
    std::string all = headers;
    size_t pos = 0;
    while (pos <= all.size()) {
        size_t nl = all.find('\n', pos);
        std::string line = all.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = fetch_trim(line.substr(0, colon));
            std::string value = fetch_trim(line.substr(colon + 1));
            if (!name.empty()) put(name, value);
        }
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
}

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


// Browser: window.fetch with method, headers and body; no cookies (F32-2-64).
// Returns a _malloc'd "<status>\n<body>" or 0 when there was no answer.
EM_ASYNC_JS(char*, js_fetch_full, (const char* method, const char* url, const char* headers, const char* body), {
    try {
        const m = UTF8ToString(method);
        const h = new Headers();
        for (const line of UTF8ToString(headers).split("\n")) {
            const c = line.indexOf(":");
            if (c <= 0) continue;
            const name = line.slice(0, c).trim();
            if (name) h.append(name, line.slice(c + 1).trim());
        }
        const init = { method: m, headers: h, credentials: "omit" };
        const b = UTF8ToString(body);
        if (b.length > 0 && m !== "GET" && m !== "HEAD") init.body = b;
        const resp = await fetch(UTF8ToString(url), init);
        const text = String(resp.status) + "\n" + await resp.text();
        const len = lengthBytesUTF8(text) + 1;
        const ptr = _malloc(len);
        stringToUTF8(text, ptr, len);
        return ptr;
    } catch (e) {
        return 0;
    }
});

extern "C" char* aiml_fetch(const char* method, const char* url, const char* headers, const char* body) {
    g_fetch_status = 0;
    if (!url) return aiml_str_dup("");
    std::string m = fetch_upper(method);
    char* raw = js_fetch_full(m.c_str(), url, headers ? headers : "", body ? body : "");
    if (!raw) return aiml_str_dup("");
    char* nl = strchr(raw, '\n');
    if (!nl) { free(raw); return aiml_str_dup(""); }
    *nl = '\0';
    g_fetch_status = (int32_t)atoi(raw);
    char* out = aiml_str_dup(nl + 1);
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


extern "C" char* aiml_fetch(const char* method, const char* url, const char* headers, const char* body) {
    g_fetch_status = 0;
    if (!url) return aiml_str_dup("");
    AimlUrl* parsed = aiml_url_parse(url);
    if (!parsed) return aiml_str_dup("");
    std::string m = fetch_upper(method);
    void* client = aiml_http_client_new(m.c_str(), parsed);
    if (!client) return aiml_str_dup("");
    AimlHttpMap* hmap = static_cast<AimlHttpMap*>(aiml_http_client_get_headers(client));
    fetch_each_header(headers, [&](const std::string& name, const std::string& value) {
        if (hmap) aiml_http_map_put(hmap, name.c_str(), value.c_str());
    });
    if (body && *body) aiml_http_client_set_body(client, body);
    void* resp = aiml_http_client_send(client);
    aiml_http_client_free(client);
    if (!resp) return aiml_str_dup("");
    AimlHttpResponse* r = static_cast<AimlHttpResponse*>(resp);
    g_fetch_status = r->status;
    char* out = aiml_str_dup(r->body ? r->body : "");
    aiml_http_resp_free(resp);
    return out;
}

#endif
