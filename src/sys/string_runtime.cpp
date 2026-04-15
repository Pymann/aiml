#include "string_runtime.h"
#include <cstring>
#include <cstdlib>
#include <cctype>

// Must match GenericList layout (list_impl.cpp / path_runtime.cpp)
struct AimlListRaw {
    int32_t size;
    int32_t capacity;
    void**  data;
};

// ── string.lower ─────────────────────────────────────────────────────────────
extern "C" char* aiml_str_lower(const char* s) {
    if (!s) return strdup("");
    char* out = strdup(s);
    for (char* p = out; *p; ++p) *p = (char)tolower((unsigned char)*p);
    return out;
}

// ── string.trim ──────────────────────────────────────────────────────────────
extern "C" char* aiml_str_trim(const char* s) {
    if (!s) return strdup("");
    // skip leading whitespace
    while (*s && isspace((unsigned char)*s)) ++s;
    if (*s == '\0') return strdup("");
    // find last non-whitespace
    const char* end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) --end;
    size_t len = (size_t)(end - s + 1);
    return strndup(s, len);
}

// ── string.replace ───────────────────────────────────────────────────────────
extern "C" char* aiml_str_replace(const char* s, const char* old_str, const char* new_str) {
    if (!s) return strdup("");
    if (!old_str || old_str[0] == '\0') return strdup(s);
    if (!new_str) new_str = "";

    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);

    // Count occurrences to pre-allocate
    size_t count = 0;
    const char* p = s;
    while ((p = strstr(p, old_str)) != nullptr) { ++count; p += old_len; }

    if (count == 0) return strdup(s);

    size_t src_len = strlen(s);
    size_t out_len = src_len + count * (new_len - old_len);
    char* out = (char*)malloc(out_len + 1);
    if (!out) return strdup(s);

    char* dst = out;
    p = s;
    while (*p) {
        const char* hit = strstr(p, old_str);
        if (!hit) {
            strcpy(dst, p);
            break;
        }
        size_t chunk = (size_t)(hit - p);
        memcpy(dst, p, chunk);
        dst += chunk;
        memcpy(dst, new_str, new_len);
        dst += new_len;
        p = hit + old_len;
    }
    out[out_len] = '\0';
    return out;
}

// ── string.split ─────────────────────────────────────────────────────────────
// Fills an AimlListRaw (same layout as GenericList) with strdup'd parts.
extern "C" int aiml_str_split_fill(const char* s, const char* delim, void* list_ptr) {
    AimlListRaw* list = static_cast<AimlListRaw*>(list_ptr);
    if (!s || !delim || !list) return 0;

    size_t delim_len = strlen(delim);
    if (delim_len == 0) {
        // Split into individual characters
        size_t slen = strlen(s);
        for (size_t i = 0; i < slen; ++i) {
            if (list->size >= list->capacity) {
                int newCap = list->capacity * 2;
                void** newData = static_cast<void**>(std::realloc(list->data, (size_t)newCap * sizeof(void*)));
                if (!newData) return list->size;
                list->data = newData;
                list->capacity = newCap;
            }
            list->data[list->size++] = strndup(s + i, 1);
        }
        return list->size;
    }

    const char* p = s;
    int count = 0;
    while (true) {
        const char* hit = strstr(p, delim);
        size_t part_len = hit ? (size_t)(hit - p) : strlen(p);

        // Grow if needed
        if (list->size >= list->capacity) {
            int newCap = list->capacity * 2;
            void** newData = static_cast<void**>(std::realloc(list->data, (size_t)newCap * sizeof(void*)));
            if (!newData) return count;
            list->data = newData;
            list->capacity = newCap;
        }
        list->data[list->size++] = strndup(p, part_len);
        ++count;

        if (!hit) break;
        p = hit + delim_len;
    }
    return count;
}

// ── string.parse_i32 ────────────────────────────────────────────────────────
extern "C" int32_t aiml_str_parse_i32(const char* s) {
    if (!s || !*s) return 0;
    return (int32_t)strtol(s, nullptr, 10);
}

// ── string.parse_f32 ────────────────────────────────────────────────────────
extern "C" float aiml_str_parse_f32(const char* s) {
    if (!s || !*s) return 0.0f;
    return strtof(s, nullptr);
}
