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
#include <vector>

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
    std::string path;    // where this reader is in the document ("" = root) — for errors
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

// spec/29 §5.2a `bare`: `__type` aus dem FERTIGEN Dokument entfernen, auf jeder
// Ebene.  Nach der Serialisierung statt waehrend ihr, weil der Stil ohnehin ein
// Nachbearbeitungsschritt ist — damit bleibt der Schreibpfad (und sein IR)
// unberuehrt, und `bare` kann nichts verfaelschen ausser diesem einen Schluessel.
static void aiml_strip_type_keys(yyjson_mut_doc* doc, yyjson_mut_val* node) {
    if (!node) return;
    if (yyjson_mut_is_obj(node)) {
        yyjson_mut_obj_remove_key(node, "__type");
        yyjson_mut_obj_iter it;
        yyjson_mut_obj_iter_init(node, &it);
        yyjson_mut_val* key;
        while ((key = yyjson_mut_obj_iter_next(&it))) {
            aiml_strip_type_keys(doc, yyjson_mut_obj_iter_get_val(key));
        }
    } else if (yyjson_mut_is_arr(node)) {
        yyjson_mut_val* elem;
        yyjson_mut_arr_iter ait;
        yyjson_mut_arr_iter_init(node, &ait);
        while ((elem = yyjson_mut_arr_iter_next(&ait))) {
            aiml_strip_type_keys(doc, elem);
        }
    }
}

static std::string aiml_json_bare_cpp(const char* json) {
    yyjson_doc* in = yyjson_read(json, strlen(json), YYJSON_READ_NOFLAG);
    if (!in) return std::string(json);                 // unlesbar → unveraendert
    yyjson_mut_doc* doc = yyjson_doc_mut_copy(in, nullptr);
    yyjson_doc_free(in);
    if (!doc) return std::string(json);
    aiml_strip_type_keys(doc, yyjson_mut_doc_get_root(doc));
    char* out = yyjson_mut_write(doc, YYJSON_WRITE_NOFLAG, nullptr);
    std::string res = out ? std::string(out) : std::string(json);
    if (out) free(out);
    yyjson_mut_doc_free(doc);
    return res;
}

// spec/29 §5.2c `skip_empty`: leere MITGLIEDER entfernen — "" , [] und {}.
// Nicht 0, nicht false, nicht null: eine Null ist ein Wert, kein Fehlen (F29-18).
// Von unten nach oben, damit ein Objekt, das durch das Entfernen seiner
// Mitglieder leer wird, selbst wegfaellt (F29-19).  Array-ELEMENTE bleiben
// stehen, auch leere — ein entfernter Eintrag verschiebt alle folgenden (F29-20).
static bool aiml_json_is_empty_val(yyjson_mut_val* v) {
    if (!v) return false;
    if (yyjson_mut_is_str(v)) { const char* t = yyjson_mut_get_str(v); return t && *t == 0; }
    if (yyjson_mut_is_arr(v)) return yyjson_mut_arr_size(v) == 0;
    if (yyjson_mut_is_obj(v)) return yyjson_mut_obj_size(v) == 0;
    return false;
}

static void aiml_strip_empty(yyjson_mut_val* node) {
    if (!node) return;
    if (yyjson_mut_is_obj(node)) {
        // Erst absteigen (unten aufraeumen), dann hier entscheiden.
        {
            yyjson_mut_obj_iter it;
            yyjson_mut_obj_iter_init(node, &it);
            yyjson_mut_val* key;
            while ((key = yyjson_mut_obj_iter_next(&it)))
                aiml_strip_empty(yyjson_mut_obj_iter_get_val(key));
        }
        // Entfernen waehrend des Laufs ist nicht erlaubt — Namen sammeln,
        // danach loeschen.
        std::vector<std::string> gone;
        {
            yyjson_mut_obj_iter it;
            yyjson_mut_obj_iter_init(node, &it);
            yyjson_mut_val* key;
            while ((key = yyjson_mut_obj_iter_next(&it))) {
                yyjson_mut_val* val = yyjson_mut_obj_iter_get_val(key);
                const char* name = yyjson_mut_get_str(key);
                if (name && aiml_json_is_empty_val(val)) gone.push_back(name);
            }
        }
        for (const std::string& name : gone)
            yyjson_mut_obj_remove_key(node, name.c_str());
    } else if (yyjson_mut_is_arr(node)) {
        yyjson_mut_val* elem;
        yyjson_mut_arr_iter ait;
        yyjson_mut_arr_iter_init(node, &ait);
        while ((elem = yyjson_mut_arr_iter_next(&ait))) aiml_strip_empty(elem);
    }
}

static std::string aiml_json_lean_cpp(const char* json) {
    yyjson_doc* in = yyjson_read(json, strlen(json), YYJSON_READ_NOFLAG);
    if (!in) return std::string(json);
    yyjson_mut_doc* doc = yyjson_doc_mut_copy(in, nullptr);
    yyjson_doc_free(in);
    if (!doc) return std::string(json);
    aiml_strip_empty(yyjson_mut_doc_get_root(doc));   // die WURZEL bleibt stehen
    char* out = yyjson_mut_write(doc, YYJSON_WRITE_NOFLAG, nullptr);
    std::string res = out ? std::string(out) : std::string(json);
    if (out) free(out);
    yyjson_mut_doc_free(doc);
    return res;
}

char* aiml_json_style_format(const char* json, int pretty, const char* indent,
                             const char* newline_str, int bare, int skip_empty) {
    if (!json) return aiml_strdup_cpp("");
    std::string work = bare ? aiml_json_bare_cpp(json) : std::string(json);
    if (skip_empty) work = aiml_json_lean_cpp(work.c_str());
    if (!pretty) return aiml_strdup_cpp(work.c_str());
    return aiml_strdup_cpp(aiml_pretty_json_cpp(work.c_str(), indent, newline_str));
}

// ── Read API ──────────────────────────────────────────────────────────────────
//
// spec/29 §5.2d: reading is TOLERANT (a document that grows stays readable,
// F29-15/16) but no longer SILENT.  A missing key or a JSON `null` yields the
// caller's default; a syntax error or a value of the wrong JSON type is
// recorded in a per-thread error text (`sys.json_error`), reset by every
// top-level parse.  Numbers convert between integer and real (an `f32` field
// reads `10`, an `i32` field reads `3.0`) — before, the "wrong" kind read as 0.

static thread_local std::string g_jr_error;
static thread_local int g_jr_error_count = 0;
static const int kJrMaxErrors = 20;

static void jr_error(const std::string& msg) {
    if (g_jr_error_count < kJrMaxErrors) {
        if (!g_jr_error.empty()) g_jr_error += "\n";
        g_jr_error += msg;
    } else if (g_jr_error_count == kJrMaxErrors) {
        g_jr_error += "\n…";
    }
    ++g_jr_error_count;
}

static const char* jr_kind(yyjson_val* v) {
    if (!v) return "nothing";
    switch (yyjson_get_type(v)) {
        case YYJSON_TYPE_NULL: return "null";
        case YYJSON_TYPE_BOOL: return "a boolean";
        case YYJSON_TYPE_NUM:  return "a number";
        case YYJSON_TYPE_STR:  return "a string";
        case YYJSON_TYPE_ARR:  return "an array";
        case YYJSON_TYPE_OBJ:  return "an object";
        default:               return "a value";
    }
}

static std::string jr_join(const aiml_jr_t* r, const char* key) {
    if (!key) return r ? r->path : std::string();
    if (!r || r->path.empty()) return key;
    return r->path + "." + key;
}

static std::string jr_index(const std::string& base, int idx) {
    return (base.empty() ? std::string("$") : base) + "[" + std::to_string(idx) + "]";
}

static void jr_type_error(const std::string& where, const char* expected, yyjson_val* got) {
    jr_error((where.empty() ? std::string("$") : where) + ": expected " + expected + ", got " + jr_kind(got));
}

static aiml_jr_t* jr_new(yyjson_val* v, std::string path) {
    auto* s = new aiml_jr_t;
    s->owns_doc = false;
    s->doc = nullptr;
    s->val = v;
    s->path = std::move(path);
    return s;
}

// The member `key` of the reader's object.  A ROOT reader also accepts the
// unwrapped forms of its container (F29-23): `[…]` stands for {"values":[…]}
// and a plain object for {"entries":{…}} (unless it carries __type/entries).
static yyjson_val* jr_get(aiml_jr_t* r, const char* key) {
    if (!r || !r->val || !key) return nullptr;
    if (r->owns_doc) {
        if (yyjson_is_arr(r->val) && strcmp(key, "values") == 0) return r->val;
        if (yyjson_is_obj(r->val) && strcmp(key, "entries") == 0
            && !yyjson_obj_get(r->val, "entries") && !yyjson_obj_get(r->val, "__type"))
            return r->val;
    }
    if (!yyjson_is_obj(r->val)) return nullptr;
    return yyjson_obj_get(r->val, key);
}

// Number conversions with type check: `where` names the value for the error.
static bool jr_absent(yyjson_val* v) { return !v || yyjson_is_null(v); }

static int64_t jr_as_i64(yyjson_val* v, int64_t def, const std::string& where) {
    if (jr_absent(v)) return def;
    if (yyjson_is_int(v)) return yyjson_get_sint(v);
    if (yyjson_is_real(v)) return (int64_t)yyjson_get_real(v);
    if (yyjson_is_bool(v)) return yyjson_get_bool(v) ? 1 : 0;
    jr_type_error(where, "a number", v);
    return def;
}

static double jr_as_f64(yyjson_val* v, double def, const std::string& where) {
    if (jr_absent(v)) return def;
    if (yyjson_is_num(v)) return yyjson_get_num(v);
    jr_type_error(where, "a number", v);
    return def;
}

static int jr_as_bool(yyjson_val* v, int def, const std::string& where) {
    if (jr_absent(v)) return def;
    if (yyjson_is_bool(v)) return yyjson_get_bool(v) ? 1 : 0;
    jr_type_error(where, "true or false", v);
    return def;
}

static char* jr_as_str(yyjson_val* v, const char* def, const std::string& where) {
    if (jr_absent(v)) return aiml_str_dup(def ? def : "");
    if (yyjson_is_str(v)) return aiml_str_dup(yyjson_get_str(v));
    jr_type_error(where, "a string", v);
    return aiml_str_dup(def ? def : "");
}

void* aiml_jr_parse(const char* json) {
    g_jr_error.clear();
    g_jr_error_count = 0;
    if (!json) { jr_error("no document"); return nullptr; }
    auto* r = new aiml_jr_t;
    r->owns_doc = true;
    yyjson_read_err err;
    r->doc = yyjson_read_opts(const_cast<char*>(json), strlen(json), YYJSON_READ_NOFLAG, nullptr, &err);
    r->val = r->doc ? yyjson_doc_get_root(r->doc) : nullptr;
    if (!r->doc) {
        // line/column of the byte yyjson stopped at (1-based)
        size_t line = 1, col = 1;
        for (size_t i = 0; i < err.pos && json[i]; ++i) {
            if (json[i] == '\n') { ++line; col = 1; } else { ++col; }
        }
        jr_error("line " + std::to_string(line) + " column " + std::to_string(col) + ": "
                 + (err.msg ? err.msg : "invalid JSON"));
    }
    return r;
}

const char* aiml_jr_error(void) { return g_jr_error.c_str(); }

// sys.json_error(): an owned copy of the per-thread error text.
char* aiml_sys_json_error(void) { return aiml_str_dup(g_jr_error.c_str()); }

int aiml_jr_has(void* rv, const char* key) {
    return jr_absent(jr_get((aiml_jr_t*)rv, key)) ? 0 : 1;
}

void* aiml_jr_sub(void* rv, const char* key) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* sub = jr_get(r, key);
    if (jr_absent(sub)) return nullptr;
    if (!yyjson_is_obj(sub)) { jr_type_error(jr_join(r, key), "an object", sub); return nullptr; }
    return jr_new(sub, jr_join(r, key));
}

void aiml_jr_free(void* rv) {
    auto* r = (aiml_jr_t*)rv;
    if (!r) return;
    if (r->owns_doc && r->doc) yyjson_doc_free(r->doc);
    delete r;
}

// Keyed scalars, with the caller's default for a missing key / null.
int32_t aiml_jr_i32d(void* rv, const char* key, int32_t def) {
    auto* r = (aiml_jr_t*)rv;
    return (int32_t)jr_as_i64(jr_get(r, key), def, jr_join(r, key));
}
int64_t aiml_jr_i64d(void* rv, const char* key, int64_t def) {
    auto* r = (aiml_jr_t*)rv;
    return jr_as_i64(jr_get(r, key), def, jr_join(r, key));
}
float aiml_jr_f32d(void* rv, const char* key, float def) {
    auto* r = (aiml_jr_t*)rv;
    return (float)jr_as_f64(jr_get(r, key), def, jr_join(r, key));
}
double aiml_jr_f64d(void* rv, const char* key, double def) {
    auto* r = (aiml_jr_t*)rv;
    return jr_as_f64(jr_get(r, key), def, jr_join(r, key));
}
int aiml_jr_boold(void* rv, const char* key, int def) {
    auto* r = (aiml_jr_t*)rv;
    return jr_as_bool(jr_get(r, key), def, jr_join(r, key));
}
char* aiml_jr_strd(void* rv, const char* key, const char* def) {
    auto* r = (aiml_jr_t*)rv;
    return jr_as_str(jr_get(r, key), def, jr_join(r, key));
}

int32_t aiml_jr_i32(void* rv, const char* key)  { return aiml_jr_i32d(rv, key, 0); }
// i64 through yyjson's signed 64-bit read (an `int` read truncated timestamps,
// measured 2026-10-13: 1760000000000 -> -936591360).
int64_t aiml_jr_i64(void* rv, const char* key)  { return aiml_jr_i64d(rv, key, 0); }
float   aiml_jr_f32(void* rv, const char* key)  { return aiml_jr_f32d(rv, key, 0.0f); }
double  aiml_jr_f64(void* rv, const char* key)  { return aiml_jr_f64d(rv, key, 0.0); }
int     aiml_jr_bool(void* rv, const char* key) { return aiml_jr_boold(rv, key, 0); }
char*   aiml_jr_str(void* rv, const char* key)  { return aiml_jr_strd(rv, key, ""); }

// ── Array reads ───────────────────────────────────────────────────────────────

static yyjson_val* jr_arr(aiml_jr_t* r, const char* key) {
    yyjson_val* v = jr_get(r, key);
    if (jr_absent(v)) return nullptr;
    if (!yyjson_is_arr(v)) { jr_type_error(jr_join(r, key), "an array", v); return nullptr; }
    return v;
}

// Element access does not repeat the "not an array" error of jr_arr: the
// length read (always first) reports it once.
static yyjson_val* jr_arr_quiet(aiml_jr_t* r, const char* key) {
    yyjson_val* v = jr_get(r, key);
    return (v && yyjson_is_arr(v)) ? v : nullptr;
}

int aiml_jr_arr_len(void* rv, const char* key) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* arr = jr_arr(r, key);
    return arr ? (int)yyjson_arr_size(arr) : 0;
}

int32_t aiml_jr_arr_i32(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    return (int32_t)jr_as_i64(yyjson_arr_get(jr_arr_quiet(r, key), (size_t)idx), 0, jr_index(jr_join(r, key), idx));
}
int64_t aiml_jr_arr_i64(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    return jr_as_i64(yyjson_arr_get(jr_arr_quiet(r, key), (size_t)idx), 0, jr_index(jr_join(r, key), idx));
}
float aiml_jr_arr_f32(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    return (float)jr_as_f64(yyjson_arr_get(jr_arr_quiet(r, key), (size_t)idx), 0.0, jr_index(jr_join(r, key), idx));
}
double aiml_jr_arr_f64(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    return jr_as_f64(yyjson_arr_get(jr_arr_quiet(r, key), (size_t)idx), 0.0, jr_index(jr_join(r, key), idx));
}
int aiml_jr_arr_bool(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    return jr_as_bool(yyjson_arr_get(jr_arr_quiet(r, key), (size_t)idx), 0, jr_index(jr_join(r, key), idx));
}
char* aiml_jr_arr_str(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    return jr_as_str(yyjson_arr_get(jr_arr_quiet(r, key), (size_t)idx), "", jr_index(jr_join(r, key), idx));
}

void* aiml_jr_arr_sub(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* v = yyjson_arr_get(jr_arr_quiet(r, key), (size_t)idx);
    if (jr_absent(v)) return nullptr;
    std::string where = jr_index(jr_join(r, key), idx);
    if (!yyjson_is_obj(v) && !yyjson_is_arr(v)) { jr_type_error(where, "an object or array", v); return nullptr; }
    return jr_new(v, where);
}

// ── Map reads ─────────────────────────────────────────────────────────────────
// Maps are stored as JSON objects under a key.
// We iterate by index using yyjson_obj_iter.

static yyjson_val* jr_map_obj(aiml_jr_t* r, const char* key) {
    yyjson_val* v = jr_get(r, key);
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

static std::string jr_map_where(aiml_jr_t* r, const char* key, int idx) {
    yyjson_val* k = jr_map_nth_key(jr_map_obj(r, key), idx);
    const char* name = k ? yyjson_get_str(k) : nullptr;
    return jr_join(r, key) + "." + (name ? name : "?");
}

int aiml_jr_map_count(void* rv, const char* key) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* v = jr_get(r, key);
    if (jr_absent(v)) return 0;
    if (!yyjson_is_obj(v)) { jr_type_error(jr_join(r, key), "an object", v); return 0; }
    return (int)yyjson_obj_size(v);
}

char* aiml_jr_map_key(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* k = jr_map_nth_key(jr_map_obj(r, key), idx);
    const char* s = k ? yyjson_get_str(k) : nullptr;
    return s ? aiml_str_dup(s) : aiml_str_dup("");
}

int32_t aiml_jr_map_val_i32(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    return (int32_t)jr_as_i64(jr_map_nth_val(jr_map_obj(r, key), idx), 0, jr_map_where(r, key, idx));
}
int64_t aiml_jr_map_val_i64(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    return jr_as_i64(jr_map_nth_val(jr_map_obj(r, key), idx), 0, jr_map_where(r, key, idx));
}
float aiml_jr_map_val_f32(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    return (float)jr_as_f64(jr_map_nth_val(jr_map_obj(r, key), idx), 0.0, jr_map_where(r, key, idx));
}
double aiml_jr_map_val_f64(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    return jr_as_f64(jr_map_nth_val(jr_map_obj(r, key), idx), 0.0, jr_map_where(r, key, idx));
}
int aiml_jr_map_val_bool(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    return jr_as_bool(jr_map_nth_val(jr_map_obj(r, key), idx), 0, jr_map_where(r, key, idx));
}
char* aiml_jr_map_val_str(void* rv, const char* key, int idx) {
    auto* r = (aiml_jr_t*)rv;
    return jr_as_str(jr_map_nth_val(jr_map_obj(r, key), idx), "", jr_map_where(r, key, idx));
}

// ── Value-level (keyless) array access ────────────────────────────────────────
// For sub-readers obtained via arr_sub / val_sub where r->val IS the array.

int aiml_jr_val_len(void* rv) {
    auto* r = (aiml_jr_t*)rv;
    if (!r || !r->val) return 0;
    if (!yyjson_is_arr(r->val)) { jr_type_error(r->path, "an array", r->val); return 0; }
    return (int)yyjson_arr_size(r->val);
}

static yyjson_val* jr_val_at(aiml_jr_t* r, int idx) {
    return (r && r->val && yyjson_is_arr(r->val)) ? yyjson_arr_get(r->val, (size_t)idx) : nullptr;
}

int32_t aiml_jr_val_i32(void* rv, int idx) {
    auto* r = (aiml_jr_t*)rv;
    return (int32_t)jr_as_i64(jr_val_at(r, idx), 0, jr_index(r ? r->path : "", idx));
}
int64_t aiml_jr_val_i64(void* rv, int idx) {
    auto* r = (aiml_jr_t*)rv;
    return jr_as_i64(jr_val_at(r, idx), 0, jr_index(r ? r->path : "", idx));
}
float aiml_jr_val_f32(void* rv, int idx) {
    auto* r = (aiml_jr_t*)rv;
    return (float)jr_as_f64(jr_val_at(r, idx), 0.0, jr_index(r ? r->path : "", idx));
}
double aiml_jr_val_f64(void* rv, int idx) {
    auto* r = (aiml_jr_t*)rv;
    return jr_as_f64(jr_val_at(r, idx), 0.0, jr_index(r ? r->path : "", idx));
}
int aiml_jr_val_bool(void* rv, int idx) {
    auto* r = (aiml_jr_t*)rv;
    return jr_as_bool(jr_val_at(r, idx), 0, jr_index(r ? r->path : "", idx));
}
char* aiml_jr_val_str(void* rv, int idx) {
    auto* r = (aiml_jr_t*)rv;
    return jr_as_str(jr_val_at(r, idx), "", jr_index(r ? r->path : "", idx));
}

void* aiml_jr_val_sub(void* rv, int idx) {
    auto* r = (aiml_jr_t*)rv;
    yyjson_val* v = jr_val_at(r, idx);
    if (jr_absent(v)) return nullptr;
    std::string where = jr_index(r ? r->path : "", idx);
    if (!yyjson_is_obj(v) && !yyjson_is_arr(v)) { jr_type_error(where, "an object or array", v); return nullptr; }
    return jr_new(v, where);
}

} // extern "C"
