#include "db_runtime.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Internal C++ structs
// ─────────────────────────────────────────────────────────────────────────────

struct AimlStore {
    std::string type_name;
    int64_t     elem_size;
    std::vector<uint8_t> data;   // elem_size * capacity bytes (flat array)
    std::vector<uint8_t> active; // 1 byte per slot: 0=deleted/unused, 1=active
    int32_t total    = 0;        // total slots ever allocated (monotonically grows)
    int32_t count    = 0;        // number of active (non-deleted) slots
};

struct AimlKv {
    int64_t val_size; // 0 = string (stored as strdup'd char*), >0 = fixed-size blob
    std::unordered_map<std::string, std::vector<uint8_t>> map;
    // For string values (val_size==0) we store 8 bytes = the char* pointer
};

struct AimlVecStore {
    int32_t dims;
    std::vector<std::string>        ids;
    std::vector<std::vector<float>> embeddings;
};

// Must match LLVM struct {i8*, f32} layout on 64-bit:
//   offset 0:  i8*   (8 bytes) = id pointer
//   offset 8:  f32   (4 bytes) = score
//   offset 12: pad   (4 bytes) = alignment padding to 16 bytes total
struct AimlVecResult {
    const char* id;     // offset 0, 8 bytes
    float       score;  // offset 8, 4 bytes
    char        _pad[4]; // offset 12, 4 bytes
};
static_assert(sizeof(AimlVecResult) == 16, "AimlVecResult must be 16 bytes");
static_assert(offsetof(AimlVecResult, id)    == 0,  "id at offset 0");
static_assert(offsetof(AimlVecResult, score) == 8,  "score at offset 8");

// AimlListRaw mirrors the GenericList LLVM struct: {i32 size, i32 cap, void** data}
struct AimlListRaw {
    int32_t size;
    int32_t capacity;
    void**  data;
};

// ─────────────────────────────────────────────────────────────────────────────
// Store<T> implementation
// ─────────────────────────────────────────────────────────────────────────────

extern "C" void* aiml_db_store_open(const char* name, const char* type_name, int64_t elem_size) {
    if (elem_size <= 0) return nullptr;
    AimlStore* s = new AimlStore();
    s->type_name = type_name ? type_name : "";
    s->elem_size = elem_size;
    return s;
}

extern "C" int32_t aiml_db_store_insert(void* store, const void* elem, int64_t elem_size) {
    if (!store || !elem) return -1;
    AimlStore* s = static_cast<AimlStore*>(store);
    if (elem_size != s->elem_size) return -1;

    // Grow flat data array and active bitmap
    s->data.resize((s->total + 1) * s->elem_size);
    s->active.resize(s->total + 1);

    uint8_t* dst = s->data.data() + s->total * s->elem_size;
    memcpy(dst, elem, s->elem_size);
    s->active[s->total] = 1;
    s->total++;
    s->count++;
    return s->total; // 1-based id
}

extern "C" void* aiml_db_store_get(void* store, int32_t id) {
    if (!store || id < 1) return nullptr;
    AimlStore* s = static_cast<AimlStore*>(store);
    int32_t idx = id - 1; // convert to 0-based
    if (idx >= s->total) return nullptr;
    if (!s->active[idx]) return nullptr;
    return s->data.data() + idx * s->elem_size;
}

extern "C" void aiml_db_store_delete(void* store, int32_t id) {
    if (!store || id < 1) return;
    AimlStore* s = static_cast<AimlStore*>(store);
    int32_t idx = id - 1;
    if (idx >= s->total) return;
    if (s->active[idx]) {
        s->active[idx] = 0;
        s->count--;
    }
}

extern "C" int32_t aiml_db_store_len(void* store) {
    if (!store) return 0;
    return static_cast<AimlStore*>(store)->count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Kv<K:V> implementation
// ─────────────────────────────────────────────────────────────────────────────

extern "C" void* aiml_db_kv_open(const char* name, int64_t val_size) {
    AimlKv* kv = new AimlKv();
    kv->val_size = val_size;
    return kv;
}

extern "C" void aiml_db_kv_set(void* kv_ptr, const char* key, const void* val, int64_t val_size) {
    if (!kv_ptr || !key) return;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);

    if (val_size == 0) {
        // string value: val is directly the char* (i8* from AIML IR)
        const char* str = val ? static_cast<const char*>(val) : "";
        char* copy = strdup(str);
        std::vector<uint8_t> buf(sizeof(char*));
        memcpy(buf.data(), &copy, sizeof(char*));
        kv->map[key] = std::move(buf);
    } else {
        std::vector<uint8_t> buf(val_size);
        if (val) memcpy(buf.data(), val, val_size);
        kv->map[key] = std::move(buf);
    }
}

extern "C" void* aiml_db_kv_get(void* kv_ptr, const char* key) {
    if (!kv_ptr || !key) return nullptr;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    auto it = kv->map.find(key);
    if (it == kv->map.end()) return nullptr;

    if (kv->val_size == 0) {
        // Returns the stored char* pointer directly
        char* ptr = nullptr;
        memcpy(&ptr, it->second.data(), sizeof(char*));
        return ptr;
    }
    return it->second.data();
}

extern "C" int32_t aiml_db_kv_has(void* kv_ptr, const char* key) {
    if (!kv_ptr || !key) return 0;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    return kv->map.count(key) ? 1 : 0;
}

extern "C" int32_t aiml_db_kv_len(void* kv_ptr) {
    if (!kv_ptr) return 0;
    return static_cast<int32_t>(static_cast<AimlKv*>(kv_ptr)->map.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// VecStore implementation
// ─────────────────────────────────────────────────────────────────────────────

extern "C" void* aiml_db_vec_open(const char* name, int32_t dims) {
    if (dims <= 0) return nullptr;
    AimlVecStore* vs = new AimlVecStore();
    vs->dims = dims;
    return vs;
}

extern "C" void aiml_db_vec_insert(void* vs_ptr, const char* id, const float* emb, int32_t dims) {
    if (!vs_ptr || !id || !emb) return;
    AimlVecStore* vs = static_cast<AimlVecStore*>(vs_ptr);
    vs->ids.push_back(id);
    vs->embeddings.emplace_back(emb, emb + dims);
}

// Brute-force cosine similarity O(N·D), top-k via partial_sort
static float cosine_similarity(const float* a, const float* b, int32_t dims) {
    float dot = 0.0f, na = 0.0f, nb = 0.0f;
    for (int32_t i = 0; i < dims; i++) {
        dot += a[i] * b[i];
        na  += a[i] * a[i];
        nb  += b[i] * b[i];
    }
    float denom = sqrtf(na) * sqrtf(nb);
    if (denom < 1e-10f) return 0.0f;
    return dot / denom;
}

extern "C" void aiml_db_vec_search(void* vs_ptr, const float* query, int32_t k, void* result_list) {
    if (!vs_ptr || !query || !result_list) return;
    AimlVecStore* vs = static_cast<AimlVecStore*>(vs_ptr);
    AimlListRaw*  list = static_cast<AimlListRaw*>(result_list);

    int32_t n = static_cast<int32_t>(vs->ids.size());
    if (n == 0 || k <= 0) return;

    // Compute all cosine scores
    std::vector<std::pair<float, int32_t>> scores(n);
    for (int32_t i = 0; i < n; i++) {
        scores[i] = { cosine_similarity(query, vs->embeddings[i].data(), vs->dims), i };
    }

    // Partial sort: top-k descending
    int32_t actual_k = k < n ? k : n;
    std::partial_sort(scores.begin(), scores.begin() + actual_k, scores.end(),
        [](const std::pair<float,int32_t>& a, const std::pair<float,int32_t>& b) {
            return a.first > b.first; // descending
        });

    // Allocate result list data array if needed
    if (list->data == nullptr || list->capacity < actual_k) {
        free(list->data);
        list->data     = static_cast<void**>(malloc(actual_k * sizeof(void*)));
        list->capacity = actual_k;
    }

    // Fill list with heap-allocated AimlVecResult* entries
    for (int32_t i = 0; i < actual_k; i++) {
        AimlVecResult* r = static_cast<AimlVecResult*>(malloc(sizeof(AimlVecResult)));
        r->id    = vs->ids[scores[i].second].c_str(); // points into vs->ids (stable)
        r->score = scores[i].first;
        memset(r->_pad, 0, 4);
        list->data[i] = r;
    }
    list->size = actual_k;
}
