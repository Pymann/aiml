#include "string_runtime.h"
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cstdint>

// Must match GenericList layout (list_impl.cpp / path_runtime.cpp)
struct AimlListRaw {
    int32_t size;
    int32_t capacity;
    void**  data;
};

// ── Refcounted string allocator (UAF protection) ─────────────────────────────
// Strings allocated via aiml_str_alloc get an 8-byte header prefixed:
//   {uint32_t magic = 'STRN', uint32_t refcount}
// Returned pointer skips the header → user code sees a normal char*.
// Decref-on-zero frees the whole allocation.
//
// Literals (.rodata) don't have the header.  incref/decref check the magic at
// `s - 8`; if it doesn't match, they no-op (literal / unmanaged buffer).
// False-positive risk: ~2^-32 per literal.
//
// Why bother: mirror's aiml_ser_free_decoded must release deserialized strings
// when a session closes (or a delta replaces them).  But user code may have
// aliased those string pointers into local variables.  Refcounting keeps the
// strings alive as long as any reference exists.
// Repro: tests/test_mirror_close_string_uaf.aiml
#define AIML_STR_MAGIC 0x5354524Eu   // "STRN" in little-endian memory

struct AimlStrHeader {
    uint32_t magic;
    uint32_t refcount;
};

extern "C" char* aiml_str_alloc(size_t len) {
    void* mem = malloc(sizeof(AimlStrHeader) + len + 1);
    if (!mem) return nullptr;
    AimlStrHeader* hdr = (AimlStrHeader*)mem;
    hdr->magic    = AIML_STR_MAGIC;
    hdr->refcount = 1;
    char* data = (char*)mem + sizeof(AimlStrHeader);
    data[len] = '\0';
    return data;
}

extern "C" char* aiml_str_dup(const char* s) {
    if (!s) return aiml_str_alloc(0);
    size_t len = strlen(s);
    char* dst = aiml_str_alloc(len);
    if (!dst) return nullptr;
    memcpy(dst, s, len);
    return dst;
}

extern "C" char* aiml_str_ndup(const char* s, size_t len) {
    if (!s) return aiml_str_alloc(0);
    // Honour C strndup semantics: stop at \0 even if len is larger.
    size_t actual = 0;
    while (actual < len && s[actual] != '\0') actual++;
    char* dst = aiml_str_alloc(actual);
    if (!dst) return nullptr;
    memcpy(dst, s, actual);
    return dst;
}

extern "C" void aiml_str_incref(char* s) {
    if (!s || (uintptr_t)s < sizeof(AimlStrHeader)) return;
    AimlStrHeader* hdr = (AimlStrHeader*)((char*)s - sizeof(AimlStrHeader));
    if (hdr->magic != AIML_STR_MAGIC) return;
    hdr->refcount++;
}

extern "C" void aiml_str_decref(char* s) {
    if (!s || (uintptr_t)s < sizeof(AimlStrHeader)) return;
    AimlStrHeader* hdr = (AimlStrHeader*)((char*)s - sizeof(AimlStrHeader));
    if (hdr->magic != AIML_STR_MAGIC) return;
    if (hdr->refcount <= 1) {
        hdr->magic = 0;  // poison to catch use-after-free in next access
        free(hdr);
    } else {
        hdr->refcount--;
    }
}

// ── string.lower ─────────────────────────────────────────────────────────────
extern "C" char* aiml_str_lower(const char* s) {
    if (!s) return aiml_str_alloc(0);
    char* out = aiml_str_dup(s);
    for (char* p = out; *p; ++p) *p = (char)tolower((unsigned char)*p);
    return out;
}

// ── string.upper ─────────────────────────────────────────────────────────────
extern "C" char* aiml_str_upper(const char* s) {
    if (!s) return aiml_str_alloc(0);
    char* out = aiml_str_dup(s);
    for (char* p = out; *p; ++p) *p = (char)toupper((unsigned char)*p);
    return out;
}

// ── string.title ─────────────────────────────────────────────────────────────
// Python-style title case: uppercase first char of every whitespace-separated
// word, lowercase the rest.  Punctuation (including `_` and `-`) is NOT
// treated as a word break — it stays in place and only character casing
// changes.  For snake_case → display labels chain with `.replace`:
//     - decl: [pretty, string, { call: [{ call: [s.replace, "_", " "] }.title] }]
// Examples:
//   "hello world"  → "Hello World"
//   "WORKOUT"      → "Workout"
//   "foo_bar"      → "Foo_bar"   (no separator-replace; use .replace first)
extern "C" char* aiml_str_title(const char* s) {
    if (!s) return aiml_str_alloc(0);
    char* out = aiml_str_dup(s);
    bool at_word_start = true;
    for (char* p = out; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (isspace(c)) {
            at_word_start = true;
        } else if (at_word_start) {
            *p = (char)toupper(c);
            at_word_start = false;
        } else {
            *p = (char)tolower(c);
        }
    }
    return out;
}

// ── string.trim ──────────────────────────────────────────────────────────────
extern "C" char* aiml_str_trim(const char* s) {
    if (!s) return aiml_str_alloc(0);
    // skip leading whitespace
    while (*s && isspace((unsigned char)*s)) ++s;
    if (*s == '\0') return aiml_str_alloc(0);
    // find last non-whitespace
    const char* end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) --end;
    size_t len = (size_t)(end - s + 1);
    return aiml_str_ndup(s, len);
}

// ── string.replace ───────────────────────────────────────────────────────────
extern "C" char* aiml_str_replace(const char* s, const char* old_str, const char* new_str) {
    if (!s) return aiml_str_alloc(0);
    if (!old_str || old_str[0] == '\0') return aiml_str_dup(s);
    if (!new_str) new_str = "";

    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);

    // Count occurrences to pre-allocate
    size_t count = 0;
    const char* p = s;
    while ((p = strstr(p, old_str)) != nullptr) { ++count; p += old_len; }

    if (count == 0) return aiml_str_dup(s);

    size_t src_len = strlen(s);
    size_t out_len = src_len + count * (new_len - old_len);
    char* out = aiml_str_alloc(out_len);
    if (!out) return aiml_str_dup(s);

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
    // aiml_str_alloc already writes the trailing \0 at out[out_len].
    return out;
}

// ── string.split ─────────────────────────────────────────────────────────────
// Fills an AimlListRaw (same layout as GenericList) with refcounted parts.
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
            list->data[list->size++] = aiml_str_ndup(s + i, 1);
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
        list->data[list->size++] = aiml_str_ndup(p, part_len);
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
