// AimlHttpMap — a tiny string→string map with the exact memory layout AIML's
// map<K:V> expects ({ size, capacity, occupied, keys, values }). It backs both
// HTTP header/cookie maps AND `sys.env` (see mem_runtime.cpp).
//
// It lives in its own translation unit — separate from http_runtime.cpp — so the
// WASM runtime (which has no HTTP server, sockets, or OpenSSL) can still link
// `sys.env`. http_runtime.cpp is native-only; this shim is portable (malloc/str*
// only) and is compiled into both the native library (source glob) and the WASM
// runtime (explicit source list in exodus-wasm-runtime.json).

#include "http_runtime.h"   // AimlHttpMap + aiml_http_map_* declarations
#include <cstdlib>
#include <cstring>
#include <cstdint>

static char* map_strdup(const char* s) { return s ? strdup(s) : nullptr; }

extern "C" AimlHttpMap* aiml_http_map_new(void) {
    AimlHttpMap* m = static_cast<AimlHttpMap*>(calloc(1, sizeof(AimlHttpMap)));
    int init_cap = 8;
    m->capacity = init_cap;
    m->occupied = static_cast<int8_t*>(calloc(init_cap, 1));
    m->keys     = static_cast<char**>(calloc(init_cap, sizeof(char*)));
    m->values   = static_cast<char**>(calloc(init_cap, sizeof(char*)));
    return m;
}

static void map_grow(AimlHttpMap* m) {
    int new_cap = m->capacity * 2;
    m->occupied = static_cast<int8_t*>(realloc(m->occupied, new_cap));
    m->keys     = static_cast<char**>(realloc(m->keys, new_cap * sizeof(char*)));
    m->values   = static_cast<char**>(realloc(m->values, new_cap * sizeof(char*)));
    memset(m->occupied + m->capacity, 0, new_cap - m->capacity);
    m->capacity = new_cap;
}

extern "C" void aiml_http_map_put(AimlHttpMap* m, const char* key, const char* val) {
    if (!m || !key) return;
    // Update existing
    for (int i = 0; i < m->capacity; ++i) {
        if (m->occupied[i] && m->keys[i] && strcmp(m->keys[i], key) == 0) {
            free(m->values[i]);
            m->values[i] = map_strdup(val);
            return;
        }
    }
    // Insert new
    if (m->size >= m->capacity * 3 / 4) map_grow(m);
    for (int i = 0; i < m->capacity; ++i) {
        if (!m->occupied[i]) {
            m->occupied[i] = 1;
            m->keys[i]   = map_strdup(key);
            m->values[i] = map_strdup(val);
            m->size++;
            return;
        }
    }
}

extern "C" char* aiml_http_map_get(AimlHttpMap* m, const char* key) {
    if (!m || !key) return nullptr;
    for (int i = 0; i < m->capacity; ++i) {
        if (m->occupied[i] && m->keys[i] && strcmp(m->keys[i], key) == 0)
            return m->values[i];
    }
    return nullptr;
}

extern "C" int32_t aiml_http_map_len(AimlHttpMap* m) {
    return m ? m->size : 0;
}

extern "C" void aiml_http_map_free(AimlHttpMap* m) {
    if (!m) return;
    for (int i = 0; i < m->capacity; ++i) {
        if (m->occupied[i]) { free(m->keys[i]); free(m->values[i]); }
    }
    free(m->occupied); free(m->keys); free(m->values); free(m);
}
