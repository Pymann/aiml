// code_runtime.cpp — C runtime support for __aiml_code_t
// Called from LLVM IR via ctx.getFunction("aiml_code_*").
// Exported with C linkage so JIT can find them via -rdynamic.
//
// Struct layouts (must match LLVM IR exactly):
//
//   GenericList: { int32_t size, int32_t capacity, void* data }
//     → matches ListImpl::getListStructType() = { i32, i32, ptr }
//     → on x86-64: offsets 0, 4, 8 (total 16 bytes)
//
//   __aiml_code_t: { char* id, char* start_call, void* code_chunks, void* data_chunks, int8_t new_context }
//     → matches code_impl.cpp getStructType() = { i8*, i8*, i8*, i8*, i8 }
//     → on x86-64: offsets 0, 8, 16, 24, 32 (sizeof = 40 with 7 bytes padding)

#include <string>
#include <set>
#include <mutex>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "../sys/string_runtime.h"

// ── Internal struct mirrors ───────────────────────────────────────────────────

struct AimlList {
    int32_t size;
    int32_t capacity;
    char**  data;   // for list<string>: array of char*
};

struct AimlCode {
    char*     id;
    char*     start_call;
    AimlList* code_chunks;
    AimlList* data_chunks;
    int8_t    new_context;  // 0 = shared context (default), 1 = isolated context
};

// ── Internal helpers ──────────────────────────────────────────────────────────

static AimlList* aiml_list_new(int capacity) {
    AimlList* l = (AimlList*)malloc(sizeof(AimlList));
    l->size = 0;
    l->capacity = capacity < 4 ? 4 : capacity;
    l->data = (char**)malloc(sizeof(char*) * l->capacity);
    return l;
}

static void aiml_list_push_borrow(AimlList* l, char* s) {
    if (l->size >= l->capacity) {
        l->capacity *= 2;
        l->data = (char**)realloc(l->data, sizeof(char*) * l->capacity);
    }
    l->data[l->size++] = s;  // borrowed pointer; caller keeps ownership
}

// Concatenate all strings in list with '\n' separator. Caller must release
// via aiml_str_decref (or, when stored in an AIML variable, automatic).
static char* aiml_list_concat(AimlList* l) {
    if (!l || l->size == 0) return aiml_str_alloc(0);
    size_t total = 0;
    for (int i = 0; i < l->size; i++) {
        if (l->data[i]) total += strlen(l->data[i]) + 1;  // +1 for '\n'
    }
    if (total == 0) return aiml_str_alloc(0);
    // aiml_str_alloc(total) gives `total` writable bytes + trailing \0.
    char* buf = aiml_str_alloc(total);
    char* p = buf;
    for (int i = 0; i < l->size; i++) {
        if (!l->data[i]) continue;
        size_t len = strlen(l->data[i]);
        memcpy(p, l->data[i], len);
        p += len;
        *p++ = '\n';
    }
    // Note: we wrote `total` bytes ending with '\n'; aiml_str_alloc already
    // null-terminated at offset `total`, so the result is properly \0-terminated.
    return buf;
}

// ── Public C API ──────────────────────────────────────────────────────────────
extern "C" {

// aiml_code_id(void* code) -> const char* (FNV-1a of the code_chunks)
// spec/62 F62-85: the content address of a bundle is the part of the
// step key that captures the BODY — for composed
// pipelines it takes the place of the source text hash.
const char* aiml_code_id(void* raw) {
    if (!raw) return "";
    AimlCode* c = (AimlCode*)raw;
    return c->id ? c->id : "";
}

// aiml_code_contains(void* code, const char* needle) -> int (1/0)
// Searches code_chunks for needle.
int aiml_code_contains(void* raw, const char* needle) {
    if (!raw || !needle) return 0;
    AimlCode* c = (AimlCode*)raw;
    AimlList* cc = c->code_chunks;
    if (cc) {
        for (int i = 0; i < cc->size; i++) {
            if (cc->data[i] && strstr(cc->data[i], needle)) return 1;
        }
    }
    return 0;
}

/* spec/62 F62-102/F62-103: which bundles were checked with `pure: true`.
 * A side set instead of a field in the struct, because the struct layout must
 * match the LLVM IR exactly (see the head of this file) — an
 * additional field would be an ABI change for a mere note. */
static std::set<std::string>& pure_ids() {
    static std::set<std::string> ids;
    return ids;
}
static std::mutex& pure_mu() {
    static std::mutex mu;
    return mu;
}

extern "C" void aiml_code_mark_pure(const char* id) {
    if (!id) return;
    std::lock_guard<std::mutex> g(pure_mu());
    pure_ids().insert(id);
}

extern "C" int32_t aiml_code_is_pure(void* raw) {
    if (!raw) return 0;
    AimlCode* c = (AimlCode*)raw;
    if (!c->id) return 0;
    std::lock_guard<std::mutex> g(pure_mu());
    return pure_ids().count(c->id) ? 1 : 0;
}

// aiml_code_tostring(void* code) -> char* (refcounted; caller eventually
// releases via aiml_str_decref).  Returns code_chunks + data_chunks
// concatenated with '\n' separator.
// Assemble chunks as a YAML SEQUENCE, not as a text chain.
//
// Every chunk is a top-level mapping (`def:` ...).  If you glue two
// of them together, you get ONE mapping with the key `def:` twice —
// when read back only the last one survives, and the receiver reports
// `Unknown function` for everything else.  Noticed when the transitive
// collection (spec/29_2 §16) made bundles with more than one chunk normal:
// the socket test sent run_pipeline + lib_greet and lost run_pipeline.
// An aiml program IS a sequence, so the sequence is also the right
// serialisation (spec/29_2 §21).  exec_chunks_to_yaml does the same.
static std::string chunks_as_sequence(AimlList* lst) {
    std::string out;
    if (!lst) return out;
    for (int i = 0; i < lst->size; i++) {
        const char* raw = lst->data[i];
        if (!raw || !*raw) continue;
        std::string chunk(raw);
        while (!chunk.empty() && (chunk.back() == '\n' || chunk.back() == '\r')) chunk.pop_back();
        if (chunk.empty()) continue;
        size_t head = chunk.find_first_not_of(" \t");
        // Already a sequence element (e.g. a whole program from code_from)?
        // Then take it over unchanged, otherwise there would be a sequence inside
        // a sequence.
        if (head != std::string::npos && chunk.compare(head, 2, "- ") == 0) {
            out += chunk;
            out += "\n";
            continue;
        }
        out += "- ";
        for (char ch : chunk) {
            out += ch;
            if (ch == '\n') out += "  ";
        }
        out += "\n";
    }
    return out;
}

char* aiml_code_tostring(void* raw) {
    if (!raw) return aiml_str_alloc(0);
    AimlCode* c = (AimlCode*)raw;
    std::string code_seq = chunks_as_sequence(c->code_chunks);
    std::string data_seq = chunks_as_sequence(c->data_chunks);
    char* code_str = aiml_str_alloc(code_seq.size());
    memcpy(code_str, code_seq.data(), code_seq.size());
    char* data_str = aiml_str_alloc(data_seq.size());
    memcpy(data_str, data_seq.data(), data_seq.size());

    bool code_empty = (code_str[0] == '\0');
    bool data_empty = (data_str[0] == '\0');

    if (code_empty && data_empty) { aiml_str_decref(data_str); return code_str; }
    if (data_empty)               { aiml_str_decref(data_str); return code_str; }
    if (code_empty)               { aiml_str_decref(code_str); return data_str; }

    size_t clen = strlen(code_str);
    size_t dlen = strlen(data_str);
    char* result = aiml_str_alloc(clen + dlen);
    memcpy(result, code_str, clen);
    memcpy(result + clen, data_str, dlen);
    aiml_str_decref(code_str);
    aiml_str_decref(data_str);
    return result;
}

// aiml_code_merge(void* a, void* b) -> void* (new heap-allocated AimlCode)
// Merges code_chunks and data_chunks; start_call inherited from a.
void* aiml_code_merge(void* a_raw, void* b_raw) {
    if (!a_raw) return b_raw;
    if (!b_raw) return a_raw;
    AimlCode* a = (AimlCode*)a_raw;
    AimlCode* b = (AimlCode*)b_raw;

    int ca = (a->code_chunks) ? a->code_chunks->size : 0;
    int cb = (b->code_chunks) ? b->code_chunks->size : 0;
    AimlList* cc = aiml_list_new(ca + cb + 1);
    if (a->code_chunks) for (int i = 0; i < a->code_chunks->size; i++) aiml_list_push_borrow(cc, a->code_chunks->data[i]);
    if (b->code_chunks) for (int i = 0; i < b->code_chunks->size; i++) aiml_list_push_borrow(cc, b->code_chunks->data[i]);

    int da = (a->data_chunks) ? a->data_chunks->size : 0;
    int db = (b->data_chunks) ? b->data_chunks->size : 0;
    AimlList* dc = aiml_list_new(da + db + 1);
    if (a->data_chunks) for (int i = 0; i < a->data_chunks->size; i++) aiml_list_push_borrow(dc, a->data_chunks->data[i]);
    if (b->data_chunks) for (int i = 0; i < b->data_chunks->size; i++) aiml_list_push_borrow(dc, b->data_chunks->data[i]);

    AimlCode* result = (AimlCode*)malloc(sizeof(AimlCode));
    result->id          = a->id;
    result->start_call  = a->start_call;
    result->code_chunks = cc;
    result->data_chunks = dc;
    /* new_context used to stay UNinitialised here — malloc garbage in a
     * field that decides isolated-vs-shared.  Inherit from the first operand,
     * as start_call does too. */
    result->new_context = a->new_context;
    return result;
}

/* aiml_code_from_string — counterpart to aiml_code_tostring (spec/29_2 §13).
 * Builds a code object again from aiml source text, so that a bundle can travel over a
 * connection: the sender serialises with toString, the receiver
 * restores it here and runs it with sys.exec.
 *   start    — function name that is called after compiling ("" = none)
 *   new_ctx  — 1 = isolated context (the only thing that works in an AOT binary) */
void* aiml_code_from_string(const char* src, const char* start, int8_t new_ctx) {
    if (!src) return nullptr;
    AimlList* cc = aiml_list_new(1);
    aiml_list_push_borrow(cc, aiml_str_dup(src));
    AimlList* dc = aiml_list_new(1);

    AimlCode* c    = (AimlCode*)malloc(sizeof(AimlCode));
    c->id          = aiml_str_dup("from_string");
    if (start && start[0] != '\0') {
        std::string sc = std::string("{\"call\":[\"") + start + "\"]}";
        c->start_call = aiml_str_dup(sc.c_str());
    } else {
        c->start_call = aiml_str_dup("");
    }
    c->code_chunks = cc;
    c->data_chunks = dc;
    c->new_context = new_ctx;
    return c;
}

} // extern "C"
