#include "string_runtime.h"
#include "mem_diff_runtime.h"   // AIML_MEM_DIFF leaf-allocation tracking (no-op when disabled)
#include "log_runtime.h"        // AIML_MODEL_TRACE diagnostic ring (no-op when disabled)
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <cstdio>

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
    aiml_memdiff_record(data, sizeof(AimlStrHeader) + len + 1);
    return data;
}

// IMMORTAL empty string: a single static "" with the STRN header and a sentinel refcount that
// incref/decref never touch and never free. Used for the DEFAULT value of unset model string
// fields (spec/61): a `.rodata` "" literal there is unsafe under drops-on — aiml_ser_free_decoded
// decref's every string field, and decref of a raw literal reads its `-8` header (may fault at a
// page boundary, or false-positive the STRN magic and free() rodata → SIGSEGV, the drops-on
// ZoneEntry-free crash). This immortal, header-carrying "" is safe to decref (no-op), `.len`→0.
#define AIML_STR_IMMORTAL 0xFFFFFFFFu
extern "C" char* aiml_str_empty(void) {
    static struct { AimlStrHeader hdr; char data[1]; } g_empty = { { AIML_STR_MAGIC, AIML_STR_IMMORTAL }, { '\0' } };
    return g_empty.data;
}

// aiml_dbg_strcmp: retained as a thin strcmp delegate.  The compiler emits a
// direct `strcmp` for AIML string ==/!= again (the instrumented variant was a
// WASM string-corruption hunt, root-caused to a serializer stride bug — see
// container_abi.h).  Kept so the exported symbol/header decl stay valid.
extern "C" int aiml_dbg_strcmp(const char* a, const char* b) {
    return strcmp(a, b);
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
    if (hdr->refcount == AIML_STR_IMMORTAL) return;   // immortal "" default — never counted
    hdr->refcount++;
}

extern "C" void aiml_str_decref(char* s) {
    if (!s || (uintptr_t)s < sizeof(AimlStrHeader)) return;
    AimlStrHeader* hdr = (AimlStrHeader*)((char*)s - sizeof(AimlStrHeader));
    if (hdr->magic != AIML_STR_MAGIC) return;
    if (hdr->refcount == AIML_STR_IMMORTAL) return;   // immortal "" default — never freed
    if (hdr->refcount <= 1) {
        hdr->magic = 0;  // poison to catch use-after-free in next access
        aiml_memdiff_forget(s);
        free(hdr);
    } else {
        hdr->refcount--;
    }
}

// ── spec/61 §7 managed string-local assignment helpers ───────────────────────
// See string_runtime.h. These maintain the invariant "slot = null | one owned RC ref" and
// never dereference a header before a foreign/possibly-freed address (dup reads only content;
// the released value is the slot's own owned reference).
extern "C" int32_t aiml_str_assign_copy(char** dst, const char* src) {
    if (!dst) return 0;
    char* next = aiml_str_dup(src);   // acquire an independent RC reference BEFORE releasing old
    if (!next && src) return 0;        // allocation failure → leave slot unchanged (src!=null case)
    char* old = *dst;
    *dst = next;
    aiml_str_decref(old);              // safe: `old` was this slot's own owned ref (or null)
    return 1;
}

extern "C" void aiml_str_assign_move(char** dst, char* next) {
    if (!dst) return;
    char* old = *dst;
    *dst = next;                       // `next` is a separate owned reference (or null)
    aiml_str_decref(old);
}

extern "C" char* aiml_str_take_local(char** src) {
    if (!src) return nullptr;
    char* value = *src;
    *src = nullptr;                    // slot nulled → its later scope-drop / decref-old is a no-op
    return value;
}

// ── spec/61 list scope-drop primitive ───────────────────────────────────────
// Frees an owned list<T> value-local's heap at scope exit (visitBlockStmt, kind 2).
// elemKind: 0 = raw/scalar (free the data buffer only); 1 = string (decref each
// element char*, then free the buffer); 2 = model (free each element struct's heap
// fields via aiml_ser_free_decoded using elemSize as the stride + serNodes, then free
// the buffer).  The list owns its elements by MOVE (ListImpl::push stores the raw value,
// no incref/copy — see list_impl.cpp), so exactly one free per element balances the
// original alloc.  Idempotent: nulls the struct so a double-drop (should not happen
// given move-detection) is a no-op, not a crash.
extern "C" void aiml_ser_free_decoded(void* obj, void* nodes, int32_t node_count);
// AIML v2: decref a %M* reference element/value (frees inner heap + shell at refcount 0).
extern "C" void aiml_model_decref(void* obj, const void* nodes, int32_t n_nodes);
extern "C" void aiml_list_free(void* list_ptr, int32_t elemKind,
                               int64_t elemSize, void* serNodes, int32_t serNodesCount) {
    if (!list_ptr) return;
    AimlListRaw* lst = static_cast<AimlListRaw*>(list_ptr);
    if (!lst->data) { lst->size = 0; lst->capacity = 0; return; }
    if (elemKind == 1) {
        char** elems = reinterpret_cast<char**>(lst->data);
        for (int32_t i = 0; i < lst->size; ++i)
            if (elems[i]) aiml_str_decref(elems[i]);
    } else if (elemKind == 2 && serNodes && elemSize > 0) {
        char* base = reinterpret_cast<char*>(lst->data);
        for (int32_t i = 0; i < lst->size; ++i)
            aiml_ser_free_decoded(base + (int64_t)i * elemSize, serNodes, serNodesCount);
    } else if (elemKind == 3 && serNodes) {
        // AIML v2 P3: elements are %M* references — decref each (order-independent, frees at 0).
        char** elems = reinterpret_cast<char**>(lst->data);
        aiml_trace_record("list_free3", list_ptr, (unsigned long)lst->data, (unsigned long)lst->size, "elemK3");
        for (int32_t i = 0; i < lst->size; ++i)
            if (elems[i]) aiml_model_decref(elems[i], serNodes, serNodesCount);
    }
    free(lst->data);
    lst->data = nullptr;
    lst->size = 0;
    lst->capacity = 0;
}

// ── spec/61 map scope-drop primitive ────────────────────────────────────────
// Layout mirrors MapImpl (map_impl.cpp): { i32 size, i32 capacity, uint8_t* occ,
// void* keys, void* vals }.  occ[i]==1 marks slot i occupied.  Frees the occ/keys/vals
// buffers and, per occupied slot, decref's string keys/values (kind 1) and frees model
// values' heap (kind 2, via valStride + valNodes).  Same safety profile as list-drop:
// a map<K:V> value-local is a stack struct (by-ref), it does not escape by design.
struct AimlMapRaw { int32_t size; int32_t capacity; uint8_t* occ; void* keys; void* vals; };
extern "C" void aiml_map_free(void* map_ptr, int32_t keyKind, int64_t keyStride,
                              int32_t valKind, int64_t valStride,
                              void* valNodes, int32_t valNodesCount) {
    if (!map_ptr) return;
    AimlMapRaw* m = static_cast<AimlMapRaw*>(map_ptr);
    // MapImpl (map_impl.cpp) is a DENSE store: put appends at keys[size]/vals[size] and bumps
    // size; get/contains/remove linear-scan [0,size). The `occ` bitmap is vestigial (never written
    // by put — walking it read past the never-grown occ buffer, the old "Invalid read of size 1").
    // Reclaim over the dense range [0,size): put OWNS its string keys/values via dup (§8), so one
    // decref per live entry balances that dup exactly.
    if (keyKind == 1 || valKind == 1 || valKind == 2) {
        char* keyBase = reinterpret_cast<char*>(m->keys);
        char* valBase = reinterpret_cast<char*>(m->vals);
        for (int32_t i = 0; i < m->size; ++i) {
            if (keyKind == 1 && keyBase) {
                char* k = *reinterpret_cast<char**>(keyBase + (int64_t)i * keyStride);
                if (k) aiml_str_decref(k);
            }
            if (valKind == 1 && valBase) {
                char* v = *reinterpret_cast<char**>(valBase + (int64_t)i * valStride);
                if (v) aiml_str_decref(v);
            } else if (valKind == 2 && valBase && valNodes) {
                aiml_ser_free_decoded(valBase + (int64_t)i * valStride, valNodes, valNodesCount);
            } else if (valKind == 3 && valBase && valNodes) {
                // AIML v2 P3: map<K:Model> value is a %M* reference — decref it.
                char* v = *reinterpret_cast<char**>(valBase + (int64_t)i * valStride);
                aiml_trace_record("map_free3", map_ptr, (unsigned long)v, (unsigned long)i, "valK3");
                if (v) aiml_model_decref(v, valNodes, valNodesCount);
            }
        }
    }
    free(m->occ);  m->occ  = nullptr;
    free(m->keys); m->keys = nullptr;
    free(m->vals); m->vals = nullptr;
    m->size = 0; m->capacity = 0;
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
