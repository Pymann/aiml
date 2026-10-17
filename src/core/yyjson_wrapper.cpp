// yyjson_wrapper.cpp
// C-callable wrapper around yyjson for use from LLVM JIT IR.
// All functions are exported with C linkage so ctx.getFunction() can find them.

#define YYJSON_DISABLE_READER 0
#define YYJSON_DISABLE_WRITER 0
#include "yyjson.h"
#include "yyjson_wrapper.h"
#include "../sys/string_runtime.h"
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <string>

// ── Internal types ────────────────────────────────────────────────────────────

// Writer context: stack-based JSON builder
struct aiml_jw_t {
    yyjson_mut_doc* doc;
    // Stack of current containers: objects or arrays
    static const int MAX_DEPTH = 64;
    yyjson_mut_val* stack[MAX_DEPTH];
    int depth;  // index of current top of stack

    yyjson_mut_val* top() { return stack[depth]; }
};

// Reader context: wraps a yyjson_val* (current object/array being read)
struct aiml_jr_t {
    yyjson_doc* doc;     // non-null only for root; sub-readers borrow parent's doc
    yyjson_val* val;     // current object/array value
    bool owns_doc;
};

// ── Helpers ───────────────────────────────────────────────────────────────────

static yyjson_mut_val* jw_cur(aiml_jw_t* w) { return w->stack[w->depth]; }

static void jw_push(aiml_jw_t* w, yyjson_mut_val* v) {
    assert(w->depth + 1 < aiml_jw_t::MAX_DEPTH);
    w->stack[++w->depth] = v;
}

static void jw_pop(aiml_jw_t* w) {
    assert(w->depth > 0);
    --w->depth;
}

// Add a key-value pair to the current object on the stack
static void jw_add_kv(aiml_jw_t* w, const char* key, yyjson_mut_val* val) {
    yyjson_mut_obj_add(jw_cur(w), yyjson_mut_str(w->doc, key), val);
}

// Append a value to the current array on the stack
static void jw_arr_append(aiml_jw_t* w, yyjson_mut_val* val) {
    yyjson_mut_arr_append(jw_cur(w), val);
}


static char* aiml_strdup_cpp(const std::string& s) {
    // Refcounted alloc — returns to AIML user code (aiml_json_* / aiml_jr_*
    // getters), which manages lifetime via aiml_str_decref on rebind/scope-exit.
    char* out = aiml_str_alloc(s.size());
    if (!out) return nullptr;
    memcpy(out, s.c_str(), s.size());
    return out;
}

static std::string aiml_pretty_json_cpp(const char* json, const char* indent, const char* newline_str) {
    std::string src = json ? json : "";
    std::string indentUnit = (indent && *indent) ? indent : "  ";
    std::string newline = (newline_str && *newline_str) ? newline_str : "\n";
    std::string out;
    out.reserve(src.size() + src.size() / 2);
    int depth = 0;
    bool inString = false;
    bool escaped = false;

    auto append_indent = [&]() {
        for (int i = 0; i < depth; ++i) out += indentUnit;
    };

    for (size_t i = 0; i < src.size(); ++i) {
        char c = src[i];
        if (inString) {
            out += c;
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        switch (c) {
            case '"':
                inString = true;
                out += c;
                break;
            case '{':
            case '[':
                out += c;
                if (i + 1 < src.size() && !((c == '{' && src[i + 1] == '}') || (c == '[' && src[i + 1] == ']'))) {
                    out += newline;
                    ++depth;
                    append_indent();
                }
                break;
            case '}':
            case ']':
                if (!out.empty() && out.size() >= newline.size() && out.compare(out.size() - newline.size(), newline.size(), newline) != 0) {
                    out += newline;
                }
                depth = depth > 0 ? depth - 1 : 0;
                append_indent();
                out += c;
                break;
            case ',':
                out += c;
                out += newline;
                append_indent();
                break;
            case ':':
                out += ": ";
                break;
            case ' ':
            case '\n':
            case '\r':
            case '\t':
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

// ── Write API ─────────────────────────────────────────────────────────────────

extern "C" {

void* aiml_jw_new() {
    auto* w = (aiml_jw_t*)malloc(sizeof(aiml_jw_t));
    w->doc   = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(w->doc);
    yyjson_mut_doc_set_root(w->doc, root);
    w->depth = 0;
    w->stack[0] = root;
    return w;
}

void aiml_jw_set_type(void* wv, const char* type_name) {
    auto* w = (aiml_jw_t*)wv;
    jw_add_kv(w, "__type", yyjson_mut_str(w->doc, type_name));
}

void aiml_jw_set_i32(void* wv, const char* key, int32_t val) {
    auto* w = (aiml_jw_t*)wv;
    jw_add_kv(w, key, yyjson_mut_int(w->doc, (int64_t)val));
}

void aiml_jw_set_i64(void* wv, const char* key, int64_t val) {
    auto* w = (aiml_jw_t*)wv;
    jw_add_kv(w, key, yyjson_mut_int(w->doc, val));
}

void aiml_jw_set_f32(void* wv, const char* key, float val) {
    auto* w = (aiml_jw_t*)wv;
    jw_add_kv(w, key, yyjson_mut_real(w->doc, (double)val));
}

void aiml_jw_set_f64(void* wv, const char* key, double val) {
    auto* w = (aiml_jw_t*)wv;
    jw_add_kv(w, key, yyjson_mut_real(w->doc, val));
}

void aiml_jw_set_bool(void* wv, const char* key, int val) {
    auto* w = (aiml_jw_t*)wv;
    jw_add_kv(w, key, yyjson_mut_bool(w->doc, val != 0));
}

void aiml_jw_set_str(void* wv, const char* key, const char* val) {
    auto* w = (aiml_jw_t*)wv;
    jw_add_kv(w, key, yyjson_mut_str(w->doc, val ? val : ""));
}

// Arrays
void aiml_jw_arr_begin(void* wv, const char* key) {
    auto* w = (aiml_jw_t*)wv;
    yyjson_mut_val* arr = yyjson_mut_arr(w->doc);
    jw_add_kv(w, key, arr);
    jw_push(w, arr);
}

void aiml_jw_arr_i32(void* wv, int32_t val) {
    auto* w = (aiml_jw_t*)wv;
    jw_arr_append(w, yyjson_mut_int(w->doc, (int64_t)val));
}

void aiml_jw_arr_i64(void* wv, int64_t val) {
    auto* w = (aiml_jw_t*)wv;
    jw_arr_append(w, yyjson_mut_int(w->doc, val));
}

void aiml_jw_arr_f32(void* wv, float val) {
    auto* w = (aiml_jw_t*)wv;
    jw_arr_append(w, yyjson_mut_real(w->doc, (double)val));
}

void aiml_jw_arr_f64(void* wv, double val) {
    auto* w = (aiml_jw_t*)wv;
    jw_arr_append(w, yyjson_mut_real(w->doc, val));
}

void aiml_jw_arr_bool(void* wv, int val) {
    auto* w = (aiml_jw_t*)wv;
    jw_arr_append(w, yyjson_mut_bool(w->doc, val != 0));
}

void aiml_jw_arr_str(void* wv, const char* val) {
    auto* w = (aiml_jw_t*)wv;
    jw_arr_append(w, yyjson_mut_str(w->doc, val ? val : ""));
}

void aiml_jw_arr_end(void* wv) {
    auto* w = (aiml_jw_t*)wv;
    jw_pop(w);
}

// Nested objects
void aiml_jw_obj_begin(void* wv, const char* key) {
    auto* w = (aiml_jw_t*)wv;
    yyjson_mut_val* obj = yyjson_mut_obj(w->doc);
    jw_add_kv(w, key, obj);
    jw_push(w, obj);
}

void aiml_jw_obj_end(void* wv) {
    auto* w = (aiml_jw_t*)wv;
    jw_pop(w);
}

// Anonymous nested containers within arrays (no key)
void aiml_jw_arr_obj_begin(void* wv) {
    auto* w = (aiml_jw_t*)wv;
    yyjson_mut_val* obj = yyjson_mut_obj(w->doc);
    jw_arr_append(w, obj);
    jw_push(w, obj);
}
void aiml_jw_arr_obj_end(void* wv) { jw_pop((aiml_jw_t*)wv); }

void aiml_jw_arr_arr_begin(void* wv) {
    auto* w = (aiml_jw_t*)wv;
    yyjson_mut_val* arr = yyjson_mut_arr(w->doc);
    jw_arr_append(w, arr);
    jw_push(w, arr);
}
void aiml_jw_arr_arr_end(void* wv) { jw_pop((aiml_jw_t*)wv); }

// Map entries (same as object entries — convenience aliases)
void aiml_jw_map_i32 (void* wv, const char* key, int32_t val) { aiml_jw_set_i32(wv, key, val); }
void aiml_jw_map_i64 (void* wv, const char* key, int64_t val) { aiml_jw_set_i64(wv, key, val); }
void aiml_jw_map_f32 (void* wv, const char* key, float   val) { aiml_jw_set_f32(wv, key, val); }
void aiml_jw_map_f64 (void* wv, const char* key, double  val) { aiml_jw_set_f64(wv, key, val); }
void aiml_jw_map_bool(void* wv, const char* key, int     val) { aiml_jw_set_bool(wv, key, val); }
void aiml_jw_map_str (void* wv, const char* key, const char* val) { aiml_jw_set_str(wv, key, val); }

char* aiml_jw_done(void* wv) {
    auto* w = (aiml_jw_t*)wv;
    size_t len = 0;
    // yyjson_mut_write allocates a string via the default allocator (malloc) — a RAW buffer with no
    // rc-header. The only callers are the aiml `sys.serialize` codegen (gJwDone), which treats the
    // result as an aiml rc-string (aiml_str_incref/decref reading the header at ptr-8). Returning the
    // raw yyjson buffer made those read/free 8 bytes before the allocation (Invalid read) and never
    // release it (leak: HTTP /servers.json etc., leak_probes/round3 §3). Copy into a refcounted
    // string and free the yyjson buffer exactly once.
    char* json = yyjson_mut_write(w->doc, YYJSON_WRITE_NOFLAG, &len);
    yyjson_mut_doc_free(w->doc);
    free(w);
    if (!json) return aiml_str_alloc(0);
    char* rc = aiml_str_alloc(len);      // rc-header + NUL, refcount=1
    if (rc) memcpy(rc, json, len);       // aiml_str_alloc already NUL-terminates at [len]
    free(json);                          // release the raw yyjson buffer once
    return rc;
}

char* aiml_json_style_format(const char* json, int pretty, const char* indent, const char* newline_str) {
    if (!json) return aiml_strdup_cpp("");
    if (!pretty) return aiml_strdup_cpp(json);
    return aiml_strdup_cpp(aiml_pretty_json_cpp(json, indent, newline_str));
}

// ── Read API ──────────────────────────────────────────────────────────────────

void* aiml_jr_parse(const char* json) {
    if (!json) return nullptr;
    auto* r = (aiml_jr_t*)malloc(sizeof(aiml_jr_t));
    r->owns_doc = true;
    r->doc = yyjson_read(json, strlen(json), YYJSON_READ_NOFLAG);
    r->val = r->doc ? yyjson_doc_get_root(r->doc) : nullptr;
    return r;
}

void* aiml_jr_sub(void* rv, const char* key) {
    auto* r = (aiml_jr_t*)rv;
    if (!r || !r->val) return nullptr;
    yyjson_val* sub = yyjson_obj_get(r->val, key);
    if (!sub || !yyjson_is_obj(sub)) return nullptr;
    auto* s = (aiml_jr_t*)malloc(sizeof(aiml_jr_t));
    s->owns_doc = false;
    s->doc = nullptr;
    s->val = sub;
    return s;
}

void aiml_jr_free(void* rv) {
    auto* r = (aiml_jr_t*)rv;
    if (!r) return;
    if (r->owns_doc && r->doc) yyjson_doc_free(r->doc);
    free(r);
}

int32_t aiml_jr_i32(void* rv, const char* key) {
    auto* r = (aiml_jr_t*)rv;
    if (!r || !r->val) return 0;
    yyjson_val* v = yyjson_obj_get(r->val, key);
    return v ? (int32_t)yyjson_get_int(v) : 0;
}

// yyjson_get_sint, NICHT yyjson_get_int: letzteres liefert ein 32-Bit `int`
// und schnitt jeden i64-Wert ab — ein Zeitstempel kam als Unsinn zurueck, ohne
// dass irgendetwas scheiterte (gemessen 2026-10-13:
// 1760000000000 -> -936591360).  Ein stiller Zahlenfehler ueber die Leitung ist
// die unangenehmste Sorte: nichts stuerzt ab, alles sieht aus wie Daten.
int64_t aiml_jr_i64(void* rv, const char* key) {
    auto* r = (aiml_jr_t*)rv;
    if (!r || !r->val) return 0;
    yyjson_val* v = yyjson_obj_get(r->val, key);
    return v ? yyjson_get_sint(v) : 0;
}

float aiml_jr_f32(void* rv, const char* key) {
    auto* r = (aiml_jr_t*)rv;
    if (!r || !r->val) return 0.0f;
    yyjson_val* v = yyjson_obj_get(r->val, key);
    return v ? (float)yyjson_get_real(v) : 0.0f;
}

double aiml_jr_f64(void* rv, const char* key) {
    auto* r = (aiml_jr_t*)rv;
    if (!r || !r->val) return 0.0;
    yyjson_val* v = yyjson_obj_get(r->val, key);
    return v ? yyjson_get_real(v) : 0.0;
}

int aiml_jr_bool(void* rv, const char* key) {
    auto* r = (aiml_jr_t*)rv;
    if (!r || !r->val) return 0;
    yyjson_val* v = yyjson_obj_get(r->val, key);
    return (v && yyjson_get_bool(v)) ? 1 : 0;
}

char* aiml_jr_str(void* rv, const char* key) {
    auto* r = (aiml_jr_t*)rv;
    if (!r || !r->val) return aiml_str_dup("");
    yyjson_val* v = yyjson_obj_get(r->val, key);
    const char* s = v ? yyjson_get_str(v) : nullptr;
    return s ? aiml_str_dup(s) : aiml_str_dup("");
}

// ── Array reads ───────────────────────────────────────────────────────────────

static yyjson_val* jr_arr(aiml_jr_t* r, const char* key) {
    if (!r || !r->val) return nullptr;
    yyjson_val* v = yyjson_obj_get(r->val, key);
    return (v && yyjson_is_arr(v)) ? v : nullptr;
}

int aiml_jr_arr_len(void* rv, const char* key) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* arr = jr_arr(r, key);
    return arr ? (int)yyjson_arr_size(arr) : 0;
}

int32_t aiml_jr_arr_i32(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* v = yyjson_arr_get(jr_arr(r, key), (size_t)idx);
    return v ? (int32_t)yyjson_get_int(v) : 0;
}

int64_t aiml_jr_arr_i64(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* v = yyjson_arr_get(jr_arr(r, key), (size_t)idx);
    return v ? yyjson_get_sint(v) : 0;
}

float aiml_jr_arr_f32(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* v = yyjson_arr_get(jr_arr(r, key), (size_t)idx);
    return v ? (float)yyjson_get_real(v) : 0.0f;
}

double aiml_jr_arr_f64(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* v = yyjson_arr_get(jr_arr(r, key), (size_t)idx);
    return v ? yyjson_get_real(v) : 0.0;
}

int aiml_jr_arr_bool(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* v = yyjson_arr_get(jr_arr(r, key), (size_t)idx);
    return (v && yyjson_get_bool(v)) ? 1 : 0;
}

char* aiml_jr_arr_str(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* v = yyjson_arr_get(jr_arr(r, key), (size_t)idx);
    const char* s = v ? yyjson_get_str(v) : nullptr;
    return s ? aiml_str_dup(s) : aiml_str_dup("");
}

void* aiml_jr_arr_sub(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* v = yyjson_arr_get(jr_arr(r, key), (size_t)idx);
    if (!v) return nullptr;
    auto* s = (aiml_jr_t*)malloc(sizeof(aiml_jr_t));
    s->owns_doc = false;
    s->doc = nullptr;
    s->val = v;
    return s;
}

// ── Map reads ─────────────────────────────────────────────────────────────────
// Maps are stored as JSON objects under a key.
// We iterate by index using yyjson_obj_iter.

static yyjson_val* jr_map_obj(aiml_jr_t* r, const char* key) {
    if (!r || !r->val) return nullptr;
    yyjson_val* v = yyjson_obj_get(r->val, key);
    return (v && yyjson_is_obj(v)) ? v : nullptr;
}

static yyjson_val* jr_map_nth_key(yyjson_val* obj, int idx) {
    if (!obj) return nullptr;
    yyjson_obj_iter iter = yyjson_obj_iter_with(obj);
    yyjson_val* key = nullptr;
    for (int i = 0; i <= idx; i++) {
        key = yyjson_obj_iter_next(&iter);
        if (!key) return nullptr;
    }
    return key;
}

static yyjson_val* jr_map_nth_val(yyjson_val* obj, int idx) {
    yyjson_val* k = jr_map_nth_key(obj, idx);
    return k ? yyjson_obj_iter_get_val(k) : nullptr;
}

int aiml_jr_map_count(void* rv, const char* key) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* obj = jr_map_obj(r, key);
    return obj ? (int)yyjson_obj_size(obj) : 0;
}

char* aiml_jr_map_key(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* obj = jr_map_obj(r, key);
    yyjson_val* k = jr_map_nth_key(obj, idx);
    const char* s = k ? yyjson_get_str(k) : nullptr;
    return s ? aiml_str_dup(s) : aiml_str_dup("");
}

int32_t aiml_jr_map_val_i32(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* v = jr_map_nth_val(jr_map_obj(r, key), idx);
    return v ? (int32_t)yyjson_get_int(v) : 0;
}

int64_t aiml_jr_map_val_i64(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* v = jr_map_nth_val(jr_map_obj(r, key), idx);
    return v ? yyjson_get_sint(v) : 0;
}

float aiml_jr_map_val_f32(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* v = jr_map_nth_val(jr_map_obj(r, key), idx);
    return v ? (float)yyjson_get_real(v) : 0.0f;
}

double aiml_jr_map_val_f64(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* v = jr_map_nth_val(jr_map_obj(r, key), idx);
    return v ? yyjson_get_real(v) : 0.0;
}

int aiml_jr_map_val_bool(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* v = jr_map_nth_val(jr_map_obj(r, key), idx);
    return (v && yyjson_get_bool(v)) ? 1 : 0;
}

char* aiml_jr_map_val_str(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* v = jr_map_nth_val(jr_map_obj(r, key), idx);
    const char* s = v ? yyjson_get_str(v) : nullptr;
    return s ? aiml_str_dup(s) : aiml_str_dup("");
}

// ── Value-level (keyless) array access ────────────────────────────────────────
// For sub-readers obtained via arr_sub / val_sub where r->val IS the array.

int aiml_jr_val_len(void* rv) {
    auto* r = (aiml_jr_t*)rv;
    if (!r || !r->val || !yyjson_is_arr(r->val)) return 0;
    return (int)yyjson_arr_size(r->val);
}

int32_t aiml_jr_val_i32(void* rv, int idx) {
    auto* r = (aiml_jr_t*)rv;
    if (!r || !r->val) return 0;
    yyjson_val* v = yyjson_arr_get(r->val, (size_t)idx);
    return v ? (int32_t)yyjson_get_int(v) : 0;
}

int64_t aiml_jr_val_i64(void* rv, int idx) {
    auto* r = (aiml_jr_t*)rv;
    if (!r || !r->val) return 0;
    yyjson_val* v = yyjson_arr_get(r->val, (size_t)idx);
    return v ? yyjson_get_sint(v) : 0;
}

float aiml_jr_val_f32(void* rv, int idx) {
    auto* r = (aiml_jr_t*)rv;
    if (!r || !r->val) return 0.0f;
    yyjson_val* v = yyjson_arr_get(r->val, (size_t)idx);
    return v ? (float)yyjson_get_real(v) : 0.0f;
}

double aiml_jr_val_f64(void* rv, int idx) {
    auto* r = (aiml_jr_t*)rv;
    if (!r || !r->val) return 0.0;
    yyjson_val* v = yyjson_arr_get(r->val, (size_t)idx);
    return v ? yyjson_get_real(v) : 0.0;
}

int aiml_jr_val_bool(void* rv, int idx) {
    auto* r = (aiml_jr_t*)rv;
    if (!r || !r->val) return 0;
    yyjson_val* v = yyjson_arr_get(r->val, (size_t)idx);
    return (v && yyjson_get_bool(v)) ? 1 : 0;
}

char* aiml_jr_val_str(void* rv, int idx) {
    auto* r = (aiml_jr_t*)rv;
    if (!r || !r->val) return aiml_str_dup("");
    yyjson_val* v = yyjson_arr_get(r->val, (size_t)idx);
    const char* s = v ? yyjson_get_str(v) : nullptr;
    return s ? aiml_str_dup(s) : aiml_str_dup("");
}

void* aiml_jr_val_sub(void* rv, int idx) {
    auto* r = (aiml_jr_t*)rv;
    if (!r || !r->val) return nullptr;
    yyjson_val* v = yyjson_arr_get(r->val, (size_t)idx);
    if (!v) return nullptr;
    auto* s = (aiml_jr_t*)malloc(sizeof(aiml_jr_t));
    s->owns_doc = false;
    s->doc = nullptr;
    s->val = v;
    return s;
}

} // extern "C"
