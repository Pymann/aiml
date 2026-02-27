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

#include <cstdlib>
#include <cstring>
#include <cstdint>

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

// Concatenate all strings in list with '\n' separator. Caller must free result.
static char* aiml_list_concat(AimlList* l) {
    if (!l || l->size == 0) return strdup("");
    size_t total = 0;
    for (int i = 0; i < l->size; i++) {
        if (l->data[i]) total += strlen(l->data[i]) + 1;  // +1 for '\n'
    }
    if (total == 0) return strdup("");
    char* buf = (char*)malloc(total + 1);
    char* p = buf;
    for (int i = 0; i < l->size; i++) {
        if (!l->data[i]) continue;
        size_t len = strlen(l->data[i]);
        memcpy(p, l->data[i], len);
        p += len;
        *p++ = '\n';
    }
    *p = '\0';
    return buf;
}

// ── Public C API ──────────────────────────────────────────────────────────────
extern "C" {

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

// aiml_code_tostring(void* code) -> char* (heap-allocated, caller frees)
// Returns code_chunks + data_chunks concatenated with '\n' separator.
char* aiml_code_tostring(void* raw) {
    if (!raw) return strdup("");
    AimlCode* c = (AimlCode*)raw;
    char* code_str = aiml_list_concat(c->code_chunks);
    char* data_str = aiml_list_concat(c->data_chunks);

    bool code_empty = (code_str[0] == '\0');
    bool data_empty = (data_str[0] == '\0');

    if (code_empty && data_empty) { free(data_str); return code_str; }
    if (data_empty)               { free(data_str); return code_str; }
    if (code_empty)               { free(code_str); return data_str; }

    size_t clen = strlen(code_str);
    size_t dlen = strlen(data_str);
    char* result = (char*)malloc(clen + dlen + 1);
    memcpy(result, code_str, clen);
    memcpy(result + clen, data_str, dlen);
    result[clen + dlen] = '\0';
    free(code_str);
    free(data_str);
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
    return result;
}

} // extern "C"
