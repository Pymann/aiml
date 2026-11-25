/*
 * storage_runtime.cpp — see storage_runtime.h for the API contract.
 *
 * The native backend uses a JSON-like flat file at
 * `${XDG_DATA_HOME:-~/.local/share}/aiml/storage.json`. The on-disk format is
 * intentionally trivial:
 *
 *     key1\tvalue1
 *     key2\tvalue2
 *     ...
 *
 * Tab-separated lines, one per entry. Keys must not contain `\t` or `\n`;
 * values must not contain `\n` (the loader is line-based). This is good
 * enough for tokens and short preferences — for anything richer use the db
 * namespace instead.
 *
 * The web backend (Emscripten) bypasses all of this and forwards to
 * `localStorage` directly via EM_JS. The native file path is therefore not
 * code-coupled to the web path: web users get the browser's quota and
 * eviction semantics, native users get the file.
 */
#include "storage_runtime.h"

#include <string>
#include <unordered_map>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif

#ifdef __EMSCRIPTEN__
/* ── Emscripten backend: localStorage via EM_JS ─────────────────────────── */

EM_JS(char*, js_storage_get, (const char* key), {
    const k = UTF8ToString(key);
    let v;
    try { v = window.localStorage.getItem(k); } catch (e) { v = null; }
    if (v === null) v = "";
    const len = lengthBytesUTF8(v) + 1;
    const ptr = _malloc(len);
    stringToUTF8(v, ptr, len);
    return ptr;
});

EM_JS(void, js_storage_set, (const char* key, const char* value), {
    try {
        window.localStorage.setItem(UTF8ToString(key), UTF8ToString(value));
    } catch (e) {
        console.warn("storage.set failed:", e);
    }
});

EM_JS(void, js_storage_remove, (const char* key), {
    try { window.localStorage.removeItem(UTF8ToString(key)); }
    catch (e) {}
});

EM_JS(int, js_storage_has, (const char* key), {
    try { return window.localStorage.getItem(UTF8ToString(key)) === null ? 0 : 1; }
    catch (e) { return 0; }
});

EM_JS(void, js_storage_clear, (), {
    try { window.localStorage.clear(); }
    catch (e) {}
});

EM_JS(char*, js_storage_host, (), {
    const v = window.location.hostname || "localhost";
    const len = lengthBytesUTF8(v) + 1;
    const ptr = _malloc(len);
    stringToUTF8(v, ptr, len);
    return ptr;
});

EM_JS(int, js_storage_is_secure, (), {
    return (window.location.protocol === "https:") ? 1 : 0;
});

/* The EM_JS getter returns a malloc'd pointer; we cache it per key so the
 * returned C-string stays valid until the next call mutates that key. */
static std::unordered_map<std::string, char*>& g_emjs_cache() {
    static std::unordered_map<std::string, char*> m;
    return m;
}

extern "C" const char* aiml_storage_get(const char* key) {
    if (!key) return "";
    auto& cache = g_emjs_cache();
    auto it = cache.find(key);
    if (it != cache.end()) { free(it->second); cache.erase(it); }
    char* p = js_storage_get(key);
    if (!p) return "";
    cache[key] = p;
    return p;
}

extern "C" void aiml_storage_set(const char* key, const char* value) {
    if (!key) return;
    auto& cache = g_emjs_cache();
    auto it = cache.find(key);
    if (it != cache.end()) { free(it->second); cache.erase(it); }
    js_storage_set(key, value ? value : "");
}

extern "C" void aiml_storage_remove(const char* key) {
    if (!key) return;
    auto& cache = g_emjs_cache();
    auto it = cache.find(key);
    if (it != cache.end()) { free(it->second); cache.erase(it); }
    js_storage_remove(key);
}

extern "C" int32_t aiml_storage_has(const char* key) {
    return key ? js_storage_has(key) : 0;
}

extern "C" void aiml_storage_clear(void) {
    auto& cache = g_emjs_cache();
    for (auto& kv : cache) free(kv.second);
    cache.clear();
    js_storage_clear();
}

extern "C" const char* aiml_storage_host(void) {
    static char* last = nullptr;
    if (last) { free(last); last = nullptr; }
    last = js_storage_host();
    return last ? last : "localhost";
}

extern "C" int32_t aiml_storage_is_secure(void) {
    return js_storage_is_secure();
}


/* ── Session store and URL fragment (spec/23_2, kontor web_ui.md FW-93) ─── */

EM_JS(char*, js_session_get, (const char* key), {
    let v;
    try { v = window.sessionStorage.getItem(UTF8ToString(key)); } catch (e) { v = null; }
    if (v === null) v = "";
    const len = lengthBytesUTF8(v) + 1;
    const ptr = _malloc(len);
    stringToUTF8(v, ptr, len);
    return ptr;
});

EM_JS(void, js_session_set, (const char* key, const char* value), {
    try { window.sessionStorage.setItem(UTF8ToString(key), UTF8ToString(value)); }
    catch (e) { console.warn("storage.session_set failed:", e); }
});

EM_JS(void, js_session_remove, (const char* key), {
    try { window.sessionStorage.removeItem(UTF8ToString(key)); } catch (e) {}
});

EM_JS(char*, js_location_hash, (), {
    let v = window.location.hash || "";
    if (v.startsWith("#")) v = v.slice(1);
    const len = lengthBytesUTF8(v) + 1;
    const ptr = _malloc(len);
    stringToUTF8(v, ptr, len);
    return ptr;
});

EM_JS(void, js_set_location_hash, (const char* value), {
    const v = UTF8ToString(value);
    const base = window.location.pathname + window.location.search;
    try { history.replaceState(history.state, "", v.length ? base + "#" + v : base); }
    catch (e) {}
});

static std::unordered_map<std::string, char*>& g_session_cache() {
    static std::unordered_map<std::string, char*> m;
    return m;
}

extern "C" const char* aiml_storage_session_get(const char* key) {
    if (!key) return "";
    auto& cache = g_session_cache();
    auto it = cache.find(key);
    if (it != cache.end()) { free(it->second); cache.erase(it); }
    char* p = js_session_get(key);
    if (!p) return "";
    cache[key] = p;
    return p;
}

extern "C" void aiml_storage_session_set(const char* key, const char* value) {
    if (!key) return;
    js_session_set(key, value ? value : "");
}

extern "C" void aiml_storage_session_remove(const char* key) {
    if (!key) return;
    js_session_remove(key);
}

extern "C" const char* aiml_storage_hash(void) {
    static char* last = nullptr;
    if (last) { free(last); last = nullptr; }
    last = js_location_hash();
    return last ? last : "";
}

extern "C" void aiml_storage_set_hash(const char* value) {
    js_set_location_hash(value ? value : "");
}

#else
/* ── Native POSIX backend: in-memory map mirrored to a flat file ────────── */

static std::mutex g_storage_mtx;
static std::unordered_map<std::string, std::string>& g_storage_map() {
    static std::unordered_map<std::string, std::string> m;
    return m;
}
static bool g_loaded = false;

static std::string storage_file_path() {
    /* Use XDG_DATA_HOME if present, else ~/.local/share. */
    const char* xdg = getenv("XDG_DATA_HOME");
    std::string base;
    if (xdg && *xdg) base = xdg;
    else {
        const char* home = getenv("HOME");
        if (!home || !*home) home = "/tmp";
        base = std::string(home) + "/.local/share";
    }
    base += "/aiml";
    /* mkdir -p the directory; ignore errors (probably already exists). */
    mkdir(base.c_str(), 0755);
    return base + "/storage.json";
}

static void load_locked_unsafe() {
    if (g_loaded) return;
    g_loaded = true;
    FILE* f = fopen(storage_file_path().c_str(), "r");
    if (!f) return;
    char line[8192];
    while (fgets(line, sizeof(line), f)) {
        char* tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        char* val = tab + 1;
        size_t vlen = strlen(val);
        if (vlen > 0 && val[vlen-1] == '\n') val[vlen-1] = '\0';
        g_storage_map()[line] = val;
    }
    fclose(f);
}

static void persist_locked_unsafe() {
    std::string p = storage_file_path();
    std::string tmp = p + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (!f) {
        fprintf(stderr, "[storage] cannot open %s for write\n", tmp.c_str());
        return;
    }
    for (auto& kv : g_storage_map()) {
        /* Skip entries with tabs/newlines in key — they would corrupt the file. */
        if (kv.first.find('\t') != std::string::npos ||
            kv.first.find('\n') != std::string::npos) continue;
        fprintf(f, "%s\t%s\n", kv.first.c_str(), kv.second.c_str());
    }
    fclose(f);
    rename(tmp.c_str(), p.c_str());
}

extern "C" const char* aiml_storage_get(const char* key) {
    if (!key) return "";
    std::lock_guard<std::mutex> lk(g_storage_mtx);
    load_locked_unsafe();
    auto it = g_storage_map().find(key);
    return it == g_storage_map().end() ? "" : it->second.c_str();
}

extern "C" void aiml_storage_set(const char* key, const char* value) {
    if (!key) return;
    std::lock_guard<std::mutex> lk(g_storage_mtx);
    load_locked_unsafe();
    g_storage_map()[key] = value ? value : "";
    persist_locked_unsafe();
}

extern "C" void aiml_storage_remove(const char* key) {
    if (!key) return;
    std::lock_guard<std::mutex> lk(g_storage_mtx);
    load_locked_unsafe();
    if (g_storage_map().erase(key)) persist_locked_unsafe();
}

extern "C" int32_t aiml_storage_has(const char* key) {
    if (!key) return 0;
    std::lock_guard<std::mutex> lk(g_storage_mtx);
    load_locked_unsafe();
    return g_storage_map().count(key) ? 1 : 0;
}

extern "C" void aiml_storage_clear(void) {
    std::lock_guard<std::mutex> lk(g_storage_mtx);
    g_storage_map().clear();
    persist_locked_unsafe();
}

extern "C" const char* aiml_storage_host(void) {
    return "localhost";
}

extern "C" int32_t aiml_storage_is_secure(void) {
    return 0;   /* native has no page protocol */
}


/* Session store: this process only, never on disk (spec/23_2). */
static std::unordered_map<std::string, std::string>& g_session_map() {
    static std::unordered_map<std::string, std::string> m;
    return m;
}
static std::string& g_hash_value() {
    static std::string h;
    return h;
}

extern "C" const char* aiml_storage_session_get(const char* key) {
    if (!key) return "";
    std::lock_guard<std::mutex> lk(g_storage_mtx);
    auto it = g_session_map().find(key);
    return it == g_session_map().end() ? "" : it->second.c_str();
}

extern "C" void aiml_storage_session_set(const char* key, const char* value) {
    if (!key) return;
    std::lock_guard<std::mutex> lk(g_storage_mtx);
    g_session_map()[key] = value ? value : "";
}

extern "C" void aiml_storage_session_remove(const char* key) {
    if (!key) return;
    std::lock_guard<std::mutex> lk(g_storage_mtx);
    g_session_map().erase(key);
}

extern "C" const char* aiml_storage_hash(void) {
    std::lock_guard<std::mutex> lk(g_storage_mtx);
    return g_hash_value().c_str();
}

extern "C" void aiml_storage_set_hash(const char* value) {
    std::lock_guard<std::mutex> lk(g_storage_mtx);
    g_hash_value() = value ? value : "";
}

#endif
