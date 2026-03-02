#include "db_runtime.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
// Persistence (WAL + mmap)
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

// ─────────────────────────────────────────────────────────────────────────────
// .db file header (32 bytes, packed)
// ─────────────────────────────────────────────────────────────────────────────
struct AimlDbHeader {
    char     magic[4];    // "AIML"
    uint32_t version;     // 1
    int64_t  elem_size;
    int32_t  total;       // slots ever allocated (monotonically grows)
    int32_t  count;       // active slots
    int32_t  capacity;    // pre-allocated slots
    char     pad[4];
};
static_assert(sizeof(AimlDbHeader) == 32, "AimlDbHeader must be 32 bytes");

static const uint8_t WAL_INSERT = 1;
static const uint8_t WAL_DELETE = 2;

// ─────────────────────────────────────────────────────────────────────────────
// Internal C++ structs
// ─────────────────────────────────────────────────────────────────────────────

struct AimlStore {
    std::string type_name;
    int64_t     elem_size = 0;
    int32_t     total     = 0;   // used slots
    int32_t     count     = 0;   // active slots

    // ── In-Memory mode (default) ──────────────────────────────────────────────
    std::vector<uint8_t> mem_data;
    std::vector<uint8_t> mem_active;

    // ── Persistent mode (WAL + mmap) ─────────────────────────────────────────
    bool     persistent = false;
    int      db_fd  = -1;
    int      wal_fd = -1;
    void*    mmap_ptr  = nullptr;
    size_t   mmap_size = 0;
    uint8_t* data_ptr   = nullptr;   // mmap_ptr + sizeof(AimlDbHeader)
    uint8_t* active_ptr = nullptr;   // data_ptr + capacity * elem_size
    int32_t  capacity   = 0;

    // ── Column Layout mode ────────────────────────────────────────────────────
    bool     column_layout = false;
    int32_t  n_fields = 0;
    std::vector<int32_t>           col_field_sizes;
    std::vector<int32_t>           col_field_offsets;
    std::vector<std::vector<uint8_t>> col_data;   // per-field flat arrays
    std::vector<uint8_t>           col_active;
    std::vector<uint8_t>           view_buf;      // scratch for assembled get
};

struct AimlKv {
    int64_t val_size;
    std::unordered_map<std::string, std::vector<uint8_t>> map;
    // Persistence (WAL)
    bool        persistent = false;
    int         wal_fd     = -1;
    std::string wal_path;
};

struct AimlVecStore {
    int32_t dims;
    std::vector<std::string>        ids;
    std::vector<std::vector<float>> embeddings;
};

struct AimlVecResult {
    const char* id;
    float       score;
    char        _pad[4];
};
static_assert(sizeof(AimlVecResult) == 16, "AimlVecResult must be 16 bytes");
static_assert(offsetof(AimlVecResult, id)    == 0, "id at offset 0");
static_assert(offsetof(AimlVecResult, score) == 8, "score at offset 8");

struct AimlListRaw {
    int32_t size;
    int32_t capacity;
    void**  data;
};

struct AimlTimeSeries {
    int64_t              elem_size = 0;
    std::vector<uint8_t> data;        // elem_size * count bytes (flat)
    std::vector<int64_t> timestamps;  // parallel to data
    int32_t              count = 0;
    // Persistence (binary log)
    bool        persistent = false;
    int         log_fd     = -1;
    std::string log_path;
};

// ─────────────────────────────────────────────────────────────────────────────
// Persistence helpers
// ─────────────────────────────────────────────────────────────────────────────

// Extends the mmap'd .db file to new_capacity slots.
// Saves/restores the active bitmap at its new offset.
static bool store_mmap_extend(AimlStore* s, int32_t new_capacity) {
    if (new_capacity <= s->capacity) return true;
    const size_t hdr_sz = sizeof(AimlDbHeader);

    // Save active bitmap for used slots before unmapping
    std::vector<uint8_t> saved_active(new_capacity, 0);
    if (s->total > 0 && s->active_ptr)
        memcpy(saved_active.data(), s->active_ptr,
               std::min((size_t)s->total, (size_t)new_capacity));

    // Sync and unmap
    if (s->mmap_ptr && s->mmap_ptr != MAP_FAILED) {
        msync(s->mmap_ptr, s->mmap_size, MS_SYNC);
        munmap(s->mmap_ptr, s->mmap_size);
        s->mmap_ptr = nullptr;
    }

    // File layout: header | data[capacity * elem_size] | active[capacity]
    size_t new_size = hdr_sz + (size_t)new_capacity * ((size_t)s->elem_size + 1);
    if (ftruncate(s->db_fd, (off_t)new_size) != 0) return false;

    void* ptr = mmap(nullptr, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, s->db_fd, 0);
    if (ptr == MAP_FAILED) return false;

    s->mmap_ptr  = ptr;
    s->mmap_size = new_size;
    s->capacity  = new_capacity;

    uint8_t* base = static_cast<uint8_t*>(ptr);
    s->data_ptr   = base + hdr_sz;
    s->active_ptr = base + hdr_sz + (size_t)new_capacity * s->elem_size;

    // Restore active bitmap at new position (zero-init, then copy back)
    memset(s->active_ptr, 0, new_capacity);
    memcpy(s->active_ptr, saved_active.data(),
           std::min((size_t)new_capacity, saved_active.size()));

    // Update header capacity
    AimlDbHeader* hdr = static_cast<AimlDbHeader*>(ptr);
    hdr->capacity = new_capacity;

    return true;
}

static void store_wal_write(AimlStore* s, uint8_t op, int32_t id, const void* data) {
    if (s->wal_fd < 0) return;
    write(s->wal_fd, &op, 1);
    write(s->wal_fd, &id, 4);
    if (op == WAL_INSERT && data)
        write(s->wal_fd, data, (size_t)s->elem_size);
}

static bool store_wal_replay(AimlStore* s) {
    if (s->wal_fd < 0) return true;
    lseek(s->wal_fd, 0, SEEK_SET);

    uint8_t op;
    while (read(s->wal_fd, &op, 1) == 1) {
        int32_t id;
        if (read(s->wal_fd, &id, 4) != 4) break;
        int32_t idx = id - 1;

        if (op == WAL_INSERT) {
            std::vector<uint8_t> buf((size_t)s->elem_size);
            if (read(s->wal_fd, buf.data(), (size_t)s->elem_size) != (ssize_t)s->elem_size) break;

            if (idx >= s->capacity) {
                int32_t nc = std::max((int32_t)16, s->capacity > 0 ? s->capacity * 2 : 16);
                while (nc <= idx) nc *= 2;
                if (!store_mmap_extend(s, nc)) return false;
            }
            memcpy(s->data_ptr + (size_t)idx * s->elem_size, buf.data(), (size_t)s->elem_size);
            if (!s->active_ptr[idx]) { s->active_ptr[idx] = 1; s->count++; }
            if (id > s->total) s->total = id;

        } else if (op == WAL_DELETE) {
            if (idx >= 0 && idx < s->capacity && s->active_ptr[idx]) {
                s->active_ptr[idx] = 0;
                s->count--;
            }
        }
    }

    if (s->mmap_ptr) {
        AimlDbHeader* hdr = static_cast<AimlDbHeader*>(s->mmap_ptr);
        hdr->total = s->total;
        hdr->count = s->count;
    }
    return true;
}

// Global registry for db.checkpoint_all
static std::vector<AimlStore*>         g_persistent_stores;
static std::vector<AimlKv*>            g_persistent_kvs;
static std::vector<AimlTimeSeries*>    g_persistent_ts;

// ─────────────────────────────────────────────────────────────────────────────
// Column Layout helpers
// ─────────────────────────────────────────────────────────────────────────────

static int32_t store_col_insert(AimlStore* s, const void* elem) {
    const uint8_t* src = static_cast<const uint8_t*>(elem);
    for (int i = 0; i < s->n_fields; i++) {
        const uint8_t* fsrc = src + s->col_field_offsets[i];
        s->col_data[i].insert(s->col_data[i].end(), fsrc, fsrc + s->col_field_sizes[i]);
    }
    s->col_active.push_back(1);
    s->total++;
    s->count++;
    return s->total;
}

static void* store_col_get(AimlStore* s, int32_t idx) {
    if (idx < 0 || idx >= s->total) return nullptr;
    if (idx >= (int32_t)s->col_active.size() || !s->col_active[idx]) return nullptr;
    s->view_buf.resize((size_t)s->elem_size);
    for (int i = 0; i < s->n_fields; i++) {
        memcpy(s->view_buf.data() + s->col_field_offsets[i],
               s->col_data[i].data() + (size_t)idx * s->col_field_sizes[i],
               (size_t)s->col_field_sizes[i]);
    }
    return s->view_buf.data();
}

static void store_col_delete(AimlStore* s, int32_t idx) {
    if (idx < 0 || idx >= (int32_t)s->col_active.size()) return;
    if (s->col_active[idx]) { s->col_active[idx] = 0; s->count--; }
}

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

extern "C" void* aiml_db_store_open_persist(const char* name, const char* type_name,
                                              int64_t elem_size, const char* base_path) {
    if (elem_size <= 0) return nullptr;
    AimlStore* s = new AimlStore();
    s->type_name  = type_name ? type_name : "";
    s->elem_size  = elem_size;
    s->persistent = true;

    std::string bp = base_path ? base_path : "./aiondb/";
    if (!bp.empty() && bp.back() != '/') bp += '/';
    std::string nm = name ? name : "store";
    std::string db_path  = bp + nm + ".db";
    std::string wal_path = bp + nm + ".wal";

    mkdir(bp.c_str(), 0755);  // create dir (ignore if exists)

    s->db_fd = open(db_path.c_str(), O_RDWR | O_CREAT, 0644);
    if (s->db_fd < 0) { delete s; return nullptr; }

    const size_t hdr_sz = sizeof(AimlDbHeader);
    bool valid_existing = false;

    struct stat st;
    fstat(s->db_fd, &st);
    if ((size_t)st.st_size >= hdr_sz) {
        AimlDbHeader hdr;
        pread(s->db_fd, &hdr, hdr_sz, 0);
        if (memcmp(hdr.magic, "AIML", 4) == 0 && hdr.version == 1 &&
            hdr.elem_size == elem_size && hdr.capacity > 0) {
            s->total    = hdr.total;
            s->count    = hdr.count;
            s->capacity = hdr.capacity;
            size_t esz = hdr_sz + (size_t)hdr.capacity * ((size_t)elem_size + 1);
            void* ptr = mmap(nullptr, esz, PROT_READ | PROT_WRITE, MAP_SHARED, s->db_fd, 0);
            if (ptr != MAP_FAILED) {
                s->mmap_ptr  = ptr;
                s->mmap_size = esz;
                s->data_ptr   = (uint8_t*)ptr + hdr_sz;
                s->active_ptr = (uint8_t*)ptr + hdr_sz + (size_t)s->capacity * s->elem_size;
                valid_existing = true;
            }
        }
    }

    if (!valid_existing) {
        ftruncate(s->db_fd, 0);
        int32_t init_cap = 16;
        size_t init_sz = hdr_sz + (size_t)init_cap * ((size_t)elem_size + 1);
        ftruncate(s->db_fd, (off_t)init_sz);
        void* ptr = mmap(nullptr, init_sz, PROT_READ | PROT_WRITE, MAP_SHARED, s->db_fd, 0);
        if (ptr == MAP_FAILED) { close(s->db_fd); delete s; return nullptr; }
        memset(ptr, 0, init_sz);
        AimlDbHeader* h = (AimlDbHeader*)ptr;
        memcpy(h->magic, "AIML", 4);
        h->version   = 1;
        h->elem_size = elem_size;
        h->total     = 0;
        h->count     = 0;
        h->capacity  = init_cap;
        s->mmap_ptr  = ptr;
        s->mmap_size = init_sz;
        s->capacity  = init_cap;
        s->data_ptr   = (uint8_t*)ptr + hdr_sz;
        s->active_ptr = (uint8_t*)ptr + hdr_sz + (size_t)init_cap * s->elem_size;
    }

    s->wal_fd = open(wal_path.c_str(), O_RDWR | O_CREAT, 0644);
    if (s->wal_fd >= 0) {
        struct stat ws;
        fstat(s->wal_fd, &ws);
        if (ws.st_size > 0) store_wal_replay(s);
        lseek(s->wal_fd, 0, SEEK_END);
    }

    g_persistent_stores.push_back(s);
    return s;
}

extern "C" void* aiml_db_store_open_col(const char* name, const char* type_name,
                                          int32_t n_fields, const int32_t* field_sizes,
                                          int64_t elem_size) {
    if (elem_size <= 0 || n_fields <= 0 || !field_sizes) return nullptr;
    AimlStore* s = new AimlStore();
    s->type_name    = type_name ? type_name : "";
    s->elem_size    = elem_size;
    s->column_layout = true;
    s->n_fields     = n_fields;
    s->col_data.resize(n_fields);

    int32_t offset = 0;
    for (int i = 0; i < n_fields; i++) {
        s->col_field_sizes.push_back(field_sizes[i]);
        s->col_field_offsets.push_back(offset);
        offset += field_sizes[i];
    }
    s->view_buf.resize((size_t)elem_size, 0);
    return s;
}

extern "C" int32_t aiml_db_store_insert(void* store, const void* elem, int64_t elem_size) {
    if (!store || !elem) return -1;
    AimlStore* s = static_cast<AimlStore*>(store);
    if (elem_size != s->elem_size) return -1;

    if (s->column_layout) return store_col_insert(s, elem);

    if (s->persistent) {
        if (s->total >= s->capacity) {
            int32_t nc = std::max((int32_t)16, s->capacity > 0 ? s->capacity * 2 : 16);
            if (!store_mmap_extend(s, nc)) return -1;
        }
        store_wal_write(s, WAL_INSERT, s->total + 1, elem);
        memcpy(s->data_ptr + (size_t)s->total * s->elem_size, elem, (size_t)s->elem_size);
        s->active_ptr[s->total] = 1;
        s->total++;
        s->count++;
        AimlDbHeader* hdr = (AimlDbHeader*)s->mmap_ptr;
        hdr->total = s->total;
        hdr->count = s->count;
        return s->total;
    }

    // In-memory
    s->mem_data.resize((size_t)(s->total + 1) * (size_t)s->elem_size);
    s->mem_active.resize((size_t)(s->total + 1));
    memcpy(s->mem_data.data() + (size_t)s->total * s->elem_size, elem, (size_t)s->elem_size);
    s->mem_active[s->total] = 1;
    s->total++;
    s->count++;
    return s->total;
}

extern "C" int32_t aiml_db_store_update(void* store, int32_t id, const void* elem, int64_t elem_size) {
    if (!store || !elem || id < 1) return -1;
    AimlStore* s = static_cast<AimlStore*>(store);
    int32_t idx = id - 1;

    if (s->column_layout) {
        if (idx >= (int32_t)s->col_active.size() || !s->col_active[idx]) return -1;
        const uint8_t* src = static_cast<const uint8_t*>(elem);
        for (int i = 0; i < s->n_fields; i++)
            memcpy(s->col_data[i].data() + (size_t)idx * s->col_field_sizes[i],
                   src + s->col_field_offsets[i], (size_t)s->col_field_sizes[i]);
        return 0;
    }

    if (s->persistent) {
        if (idx >= s->total || !s->active_ptr[idx]) return -1;
        store_wal_write(s, WAL_INSERT, id, elem);  // WAL_INSERT replay is idempotent for active slots
        memcpy(s->data_ptr + (size_t)idx * s->elem_size, elem, (size_t)s->elem_size);
        return 0;
    }

    if (idx >= s->total || !s->mem_active[idx]) return -1;
    memcpy(s->mem_data.data() + (size_t)idx * s->elem_size, elem, (size_t)s->elem_size);
    return 0;
}

extern "C" void* aiml_db_store_get(void* store, int32_t id) {
    if (!store || id < 1) return nullptr;
    AimlStore* s = static_cast<AimlStore*>(store);
    int32_t idx = id - 1;

    if (s->column_layout) return store_col_get(s, idx);

    if (s->persistent) {
        if (idx >= s->total || !s->active_ptr[idx]) return nullptr;
        return s->data_ptr + (size_t)idx * s->elem_size;
    }

    if (idx >= s->total || !s->mem_active[idx]) return nullptr;
    return s->mem_data.data() + (size_t)idx * s->elem_size;
}

extern "C" void* aiml_db_store_get_by_key(void* store_ptr, const void* key_ptr,
                                            int32_t key_offset, int32_t key_size,
                                            int32_t is_string) {
    if (!store_ptr || !key_ptr) return nullptr;
    AimlStore* s = static_cast<AimlStore*>(store_ptr);
    for (int32_t slot = 1; slot <= s->total; slot++) {
        void* elem = aiml_db_store_get(store_ptr, slot);
        if (!elem) continue;
        const uint8_t* field = static_cast<const uint8_t*>(elem) + key_offset;
        if (is_string) {
            const char* stored = nullptr;
            memcpy(&stored, field, sizeof(char*));
            if (stored && strcmp(stored, static_cast<const char*>(key_ptr)) == 0)
                return elem;
        } else {
            if (memcmp(field, key_ptr, (size_t)key_size) == 0)
                return elem;
        }
    }
    return nullptr;
}

extern "C" void aiml_db_store_delete(void* store, int32_t id) {
    if (!store || id < 1) return;
    AimlStore* s = static_cast<AimlStore*>(store);
    int32_t idx = id - 1;

    if (s->column_layout) { store_col_delete(s, idx); return; }

    if (s->persistent) {
        if (idx >= s->total) return;
        if (s->active_ptr[idx]) {
            store_wal_write(s, WAL_DELETE, id, nullptr);
            s->active_ptr[idx] = 0;
            s->count--;
            ((AimlDbHeader*)s->mmap_ptr)->count = s->count;
        }
        return;
    }

    if (idx >= s->total) return;
    if (s->mem_active[idx]) { s->mem_active[idx] = 0; s->count--; }
}

extern "C" int32_t aiml_db_store_len(void* store) {
    if (!store) return 0;
    return static_cast<AimlStore*>(store)->count;
}

extern "C" int32_t aiml_db_store_checkpoint(void* store) {
    if (!store) return -1;
    AimlStore* s = static_cast<AimlStore*>(store);
    if (!s->persistent || !s->mmap_ptr) return 0;
    AimlDbHeader* hdr = (AimlDbHeader*)s->mmap_ptr;
    hdr->total = s->total;
    hdr->count = s->count;
    msync(s->mmap_ptr, s->mmap_size, MS_SYNC);
    if (s->wal_fd >= 0) { ftruncate(s->wal_fd, 0); lseek(s->wal_fd, 0, SEEK_SET); }
    return 0;
}

extern "C" int32_t aiml_db_kv_checkpoint(void* kv_ptr);
extern "C" int32_t aiml_db_ts_checkpoint(void* ts_ptr);

extern "C" int32_t aiml_db_checkpoint_all(void) {
    int32_t result = 0;
    for (AimlStore*      s  : g_persistent_stores) if (aiml_db_store_checkpoint(s)  != 0) result = -1;
    for (AimlKv*         kv : g_persistent_kvs)    if (aiml_db_kv_checkpoint(kv)    != 0) result = -1;
    for (AimlTimeSeries* ts : g_persistent_ts)     if (aiml_db_ts_checkpoint(ts)    != 0) result = -1;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Kv<K:V> implementation
// ─────────────────────────────────────────────────────────────────────────────

extern "C" void* aiml_db_kv_open(const char* name, int64_t val_size) {
    AimlKv* kv = new AimlKv();
    kv->val_size = val_size;
    return kv;
}

// ── Kv WAL helpers ────────────────────────────────────────────────────────────

static const uint8_t KV_WAL_SET    = 1;
static const uint8_t KV_WAL_DELETE = 2;

// Write a SET record: op(1) + key_len(4) + key + val_bytes(4) + val
static void kv_wal_write_set(AimlKv* kv, const char* key, const void* val, uint32_t val_bytes) {
    uint32_t key_len = (uint32_t)strlen(key);
    write(kv->wal_fd, &KV_WAL_SET, 1);
    write(kv->wal_fd, &key_len,    4);
    write(kv->wal_fd,  key,        key_len);
    write(kv->wal_fd, &val_bytes,  4);
    if (val_bytes > 0 && val) write(kv->wal_fd, val, val_bytes);
}

// Write a DELETE record: op(1) + key_len(4) + key
static void kv_wal_write_delete(AimlKv* kv, const char* key) {
    uint32_t key_len = (uint32_t)strlen(key);
    write(kv->wal_fd, &KV_WAL_DELETE, 1);
    write(kv->wal_fd, &key_len,       4);
    write(kv->wal_fd,  key,           key_len);
}

// Replay WAL from current fd position (file must be seeked to start)
static void kv_wal_replay(AimlKv* kv) {
    lseek(kv->wal_fd, 0, SEEK_SET);
    while (true) {
        uint8_t op;
        if (read(kv->wal_fd, &op, 1) != 1) break;
        uint32_t key_len;
        if (read(kv->wal_fd, &key_len, 4) != 4) break;
        std::string key(key_len, '\0');
        if ((uint32_t)read(kv->wal_fd, key.data(), key_len) != key_len) break;
        if (op == KV_WAL_SET) {
            uint32_t val_bytes;
            if (read(kv->wal_fd, &val_bytes, 4) != 4) break;
            if (kv->val_size == 0) {
                // String value stored as raw chars (no null terminator in WAL)
                std::string sval(val_bytes, '\0');
                if ((uint32_t)read(kv->wal_fd, sval.data(), val_bytes) != val_bytes) break;
                char* copy = strdup(sval.c_str());
                std::vector<uint8_t> buf(sizeof(char*));
                memcpy(buf.data(), &copy, sizeof(char*));
                kv->map[key] = std::move(buf);
            } else {
                std::vector<uint8_t> buf(val_bytes);
                if ((uint32_t)read(kv->wal_fd, buf.data(), val_bytes) != val_bytes) break;
                kv->map[key] = std::move(buf);
            }
        } else if (op == KV_WAL_DELETE) {
            auto it = kv->map.find(key);
            if (it != kv->map.end()) {
                if (kv->val_size == 0) {
                    char* ptr = nullptr;
                    memcpy(&ptr, it->second.data(), sizeof(char*));
                    free(ptr);
                }
                kv->map.erase(it);
            }
        } else {
            break; // unknown op
        }
    }
}

extern "C" void* aiml_db_kv_open_persist(const char* name, int64_t val_size,
                                           const char* base_path) {
    std::string dir = base_path ? base_path : "./aiondb/";
    mkdir(dir.c_str(), 0755);
    AimlKv* kv    = new AimlKv();
    kv->val_size   = val_size;
    kv->persistent = true;
    kv->wal_path   = dir + std::string(name) + ".kvwal";
    int fd = open(kv->wal_path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd >= 0) {
        kv->wal_fd = fd;
        kv_wal_replay(kv);
        lseek(fd, 0, SEEK_END); // append mode
    }
    g_persistent_kvs.push_back(kv);
    return kv;
}

extern "C" int32_t aiml_db_kv_checkpoint(void* kv_ptr) {
    if (!kv_ptr) return -1;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    if (!kv->persistent || kv->wal_fd < 0) return 0;
    // Compact: truncate + rewrite all current entries
    ftruncate(kv->wal_fd, 0);
    lseek(kv->wal_fd, 0, SEEK_SET);
    for (auto& [k, v] : kv->map) {
        if (kv->val_size == 0) {
            char* ptr = nullptr;
            memcpy(&ptr, v.data(), sizeof(char*));
            const char* str = ptr ? ptr : "";
            kv_wal_write_set(kv, k.c_str(), str, (uint32_t)strlen(str));
        } else {
            kv_wal_write_set(kv, k.c_str(), v.data(), (uint32_t)kv->val_size);
        }
    }
    fsync(kv->wal_fd);
    return 0;
}

extern "C" void aiml_db_kv_set(void* kv_ptr, const char* key, const void* val, int64_t val_size) {
    if (!kv_ptr || !key) return;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    if (val_size == 0) {
        const char* str = val ? static_cast<const char*>(val) : "";
        if (kv->persistent && kv->wal_fd >= 0)
            kv_wal_write_set(kv, key, str, (uint32_t)strlen(str));
        char* copy = strdup(str);
        std::vector<uint8_t> buf(sizeof(char*));
        memcpy(buf.data(), &copy, sizeof(char*));
        kv->map[key] = std::move(buf);
    } else {
        if (kv->persistent && kv->wal_fd >= 0)
            kv_wal_write_set(kv, key, val, val ? (uint32_t)val_size : 0);
        std::vector<uint8_t> buf((size_t)val_size);
        if (val) memcpy(buf.data(), val, (size_t)val_size);
        kv->map[key] = std::move(buf);
    }
}

extern "C" void* aiml_db_kv_get(void* kv_ptr, const char* key) {
    if (!kv_ptr || !key) return nullptr;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    auto it = kv->map.find(key);
    if (it == kv->map.end()) return nullptr;
    if (kv->val_size == 0) {
        char* ptr = nullptr;
        memcpy(&ptr, it->second.data(), sizeof(char*));
        return ptr;
    }
    return it->second.data();
}

extern "C" int32_t aiml_db_kv_has(void* kv_ptr, const char* key) {
    if (!kv_ptr || !key) return 0;
    return static_cast<AimlKv*>(kv_ptr)->map.count(key) ? 1 : 0;
}

extern "C" int32_t aiml_db_kv_len(void* kv_ptr) {
    if (!kv_ptr) return 0;
    return (int32_t)static_cast<AimlKv*>(kv_ptr)->map.size();
}

extern "C" void aiml_db_kv_delete(void* kv_ptr, const char* key) {
    if (!kv_ptr || !key) return;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    auto it = kv->map.find(key);
    if (it == kv->map.end()) return;
    if (kv->persistent && kv->wal_fd >= 0)
        kv_wal_write_delete(kv, key);
    if (kv->val_size == 0) {
        char* ptr = nullptr;
        memcpy(&ptr, it->second.data(), sizeof(char*));
        free(ptr);
    }
    kv->map.erase(it);
}

extern "C" void aiml_db_kv_clear(void* kv_ptr) {
    if (!kv_ptr) return;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    if (kv->persistent && kv->wal_fd >= 0) {
        // Truncate WAL — clear is atomic
        ftruncate(kv->wal_fd, 0);
        lseek(kv->wal_fd, 0, SEEK_SET);
    }
    if (kv->val_size == 0) {
        for (auto& [k, v] : kv->map) {
            char* ptr = nullptr;
            memcpy(&ptr, v.data(), sizeof(char*));
            free(ptr);
        }
    }
    kv->map.clear();
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

static float cosine_similarity(const float* a, const float* b, int32_t dims) {
    float dot = 0.0f, na = 0.0f, nb = 0.0f;
    for (int32_t i = 0; i < dims; i++) {
        dot += a[i] * b[i];
        na  += a[i] * a[i];
        nb  += b[i] * b[i];
    }
    float denom = sqrtf(na) * sqrtf(nb);
    return (denom < 1e-10f) ? 0.0f : dot / denom;
}

extern "C" void aiml_db_vec_search(void* vs_ptr, const float* query, int32_t k, void* result_list) {
    if (!vs_ptr || !query || !result_list) return;
    AimlVecStore* vs   = static_cast<AimlVecStore*>(vs_ptr);
    AimlListRaw*  list = static_cast<AimlListRaw*>(result_list);

    int32_t n = (int32_t)vs->ids.size();
    if (n == 0 || k <= 0) return;

    std::vector<std::pair<float, int32_t>> scores(n);
    for (int32_t i = 0; i < n; i++)
        scores[i] = { cosine_similarity(query, vs->embeddings[i].data(), vs->dims), i };

    int32_t actual_k = k < n ? k : n;
    std::partial_sort(scores.begin(), scores.begin() + actual_k, scores.end(),
        [](const std::pair<float,int32_t>& a, const std::pair<float,int32_t>& b) {
            return a.first > b.first;
        });

    if (!list->data || list->capacity < actual_k) {
        free(list->data);
        list->data     = (void**)malloc((size_t)actual_k * sizeof(void*));
        list->capacity = actual_k;
    }
    for (int32_t i = 0; i < actual_k; i++) {
        AimlVecResult* r = (AimlVecResult*)malloc(sizeof(AimlVecResult));
        r->id    = vs->ids[scores[i].second].c_str();
        r->score = scores[i].first;
        memset(r->_pad, 0, 4);
        list->data[i] = r;
    }
    list->size = actual_k;
}

extern "C" int32_t aiml_db_vec_len(void* vs_ptr) {
    if (!vs_ptr) return 0;
    return (int32_t)static_cast<AimlVecStore*>(vs_ptr)->ids.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// TimeSeries<T> implementation
// ─────────────────────────────────────────────────────────────────────────────

extern "C" void* aiml_db_ts_open(const char* name, int64_t elem_size) {
    if (elem_size <= 0) return nullptr;
    AimlTimeSeries* ts = new AimlTimeSeries();
    ts->elem_size = elem_size;
    return ts;
}

extern "C" void* aiml_db_ts_open_persist(const char* name, int64_t elem_size,
                                           const char* base_path) {
    if (elem_size <= 0) return nullptr;
    std::string dir = base_path ? base_path : "./aiondb/";
    mkdir(dir.c_str(), 0755);
    AimlTimeSeries* ts = new AimlTimeSeries();
    ts->elem_size   = elem_size;
    ts->persistent  = true;
    ts->log_path    = dir + std::string(name) + ".tslog";
    int fd = open(ts->log_path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd >= 0) {
        ts->log_fd = fd;
        // Replay: each record = [int64 timestamp][elem_size bytes]
        lseek(fd, 0, SEEK_SET);
        std::vector<uint8_t> buf((size_t)elem_size);
        int64_t ts_val;
        while (read(fd, &ts_val, sizeof(int64_t)) == (ssize_t)sizeof(int64_t)) {
            ssize_t n = read(fd, buf.data(), (size_t)elem_size);
            if (n != (ssize_t)elem_size) break;
            ts->data.insert(ts->data.end(), buf.begin(), buf.end());
            ts->timestamps.push_back(ts_val);
            ts->count++;
        }
        lseek(fd, 0, SEEK_END); // append mode
    }
    g_persistent_ts.push_back(ts);
    return ts;
}

extern "C" int32_t aiml_db_ts_checkpoint(void* ts_ptr) {
    if (!ts_ptr) return -1;
    AimlTimeSeries* ts = static_cast<AimlTimeSeries*>(ts_ptr);
    if (!ts->persistent || ts->log_fd < 0) return 0;
    fsync(ts->log_fd);
    return 0;
}

extern "C" void aiml_db_ts_append(void* ts_ptr, const void* elem,
                                   int64_t elem_size, int64_t timestamp) {
    if (!ts_ptr || !elem) return;
    AimlTimeSeries* ts = static_cast<AimlTimeSeries*>(ts_ptr);
    if (ts->persistent && ts->log_fd >= 0) {
        write(ts->log_fd, &timestamp, sizeof(int64_t));
        write(ts->log_fd, elem, (size_t)ts->elem_size);
    }
    const uint8_t* src = static_cast<const uint8_t*>(elem);
    ts->data.insert(ts->data.end(), src, src + (size_t)ts->elem_size);
    ts->timestamps.push_back(timestamp);
    ts->count++;
}

extern "C" void* aiml_db_ts_at(void* ts_ptr, int64_t timestamp) {
    if (!ts_ptr) return nullptr;
    AimlTimeSeries* ts = static_cast<AimlTimeSeries*>(ts_ptr);
    if (ts->count == 0) return nullptr;
    // Last entry with timestamp <= t
    auto it = std::upper_bound(ts->timestamps.begin(), ts->timestamps.end(), timestamp);
    if (it == ts->timestamps.begin()) return nullptr;
    --it;
    int32_t idx = (int32_t)(it - ts->timestamps.begin());
    return ts->data.data() + (size_t)idx * (size_t)ts->elem_size;
}

extern "C" void aiml_db_ts_range(void* ts_ptr, int64_t from, int64_t to, void* result_list) {
    if (!ts_ptr || !result_list) return;
    AimlTimeSeries* ts   = static_cast<AimlTimeSeries*>(ts_ptr);
    AimlListRaw*    list = static_cast<AimlListRaw*>(result_list);

    if (ts->count == 0) return;
    auto begin_it = std::lower_bound(ts->timestamps.begin(), ts->timestamps.end(), from);
    auto end_it   = std::upper_bound(ts->timestamps.begin(), ts->timestamps.end(), to);
    int32_t n = (int32_t)(end_it - begin_it);
    if (n <= 0) return;

    if (!list->data || list->capacity < n) {
        free(list->data);
        list->data     = (void**)malloc((size_t)n * sizeof(void*));
        list->capacity = n;
    }
    int32_t base_idx = (int32_t)(begin_it - ts->timestamps.begin());
    for (int32_t i = 0; i < n; i++)
        list->data[i] = ts->data.data() + (size_t)(base_idx + i) * (size_t)ts->elem_size;
    list->size = n;
}

extern "C" int32_t aiml_db_ts_len(void* ts_ptr) {
    if (!ts_ptr) return 0;
    return static_cast<AimlTimeSeries*>(ts_ptr)->count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Store iteration helpers — used by db.run_system and db.query
// ─────────────────────────────────────────────────────────────────────────────

extern "C" int32_t aiml_db_store_total(void* store_ptr) {
    if (!store_ptr) return 0;
    return static_cast<AimlStore*>(store_ptr)->total;
}

extern "C" int32_t aiml_db_store_is_active(void* store_ptr, int32_t slot) {
    if (!store_ptr || slot < 1) return 0;
    AimlStore* s = static_cast<AimlStore*>(store_ptr);
    int32_t idx = slot - 1;
    if (s->column_layout) {
        if (idx >= (int32_t)s->col_active.size()) return 0;
        return s->col_active[(size_t)idx] ? 1 : 0;
    } else if (s->persistent) {
        if (idx >= s->total || !s->active_ptr) return 0;
        return s->active_ptr[(size_t)idx] ? 1 : 0;
    } else {
        if (idx >= (int32_t)s->mem_active.size()) return 0;
        return s->mem_active[(size_t)idx] ? 1 : 0;
    }
}

extern "C" void* aiml_db_store_get_ptr(void* store_ptr, int32_t slot) {
    return aiml_db_store_get(store_ptr, slot);
}

typedef int32_t (*AimlPredicateFn)(const void*);

extern "C" void aiml_db_store_query(void* store_ptr, void* pred_fn,
                                     int32_t ord_off, int32_t ord_sz, int32_t ord_dir,
                                     int32_t limit, int32_t offset,
                                     void* result_list) {
    if (!store_ptr || !result_list) return;
    AimlStore*      s    = static_cast<AimlStore*>(store_ptr);
    AimlListRaw*    list = static_cast<AimlListRaw*>(result_list);
    AimlPredicateFn pred = reinterpret_cast<AimlPredicateFn>(pred_fn);

    // 1. Collect all matching elements (predicate only, no limit yet)
    std::vector<void*> matches;
    for (int32_t slot = 1; slot <= s->total; slot++) {
        if (!aiml_db_store_is_active(store_ptr, slot)) continue;
        void* ptr = aiml_db_store_get(store_ptr, slot);
        if (!ptr) continue;
        if (pred && !pred(ptr)) continue;
        matches.push_back(ptr);
    }

    // 2. Sort by field at ord_off (signed integer comparison, 4 or 8 bytes; else memcmp)
    if (ord_off >= 0 && ord_sz > 0 && matches.size() > 1) {
        std::sort(matches.begin(), matches.end(),
            [ord_off, ord_sz, ord_dir](const void* a, const void* b) {
                const uint8_t* fa = static_cast<const uint8_t*>(a) + ord_off;
                const uint8_t* fb = static_cast<const uint8_t*>(b) + ord_off;
                int cmp = 0;
                if (ord_sz == 4) {
                    int32_t va, vb;
                    memcpy(&va, fa, 4); memcpy(&vb, fb, 4);
                    cmp = (va < vb) ? -1 : (va > vb) ? 1 : 0;
                } else if (ord_sz == 8) {
                    int64_t va, vb;
                    memcpy(&va, fa, 8); memcpy(&vb, fb, 8);
                    cmp = (va < vb) ? -1 : (va > vb) ? 1 : 0;
                } else {
                    cmp = memcmp(fa, fb, (size_t)ord_sz);
                }
                return ord_dir == 0 ? cmp < 0 : cmp > 0;
            });
    }

    // 3. Apply offset + limit
    int32_t start     = (offset > 0) ? offset : 0;
    int32_t available = (int32_t)matches.size() - start;
    if (available <= 0) { list->size = 0; return; }
    int32_t n = (limit >= 0 && limit < available) ? limit : available;
    if (n <= 0) return;

    if (!list->data || list->capacity < n) {
        free(list->data);
        list->data     = (void**)malloc((size_t)n * sizeof(void*));
        list->capacity = n;
    }
    for (int32_t i = 0; i < n; i++) list->data[i] = matches[(size_t)(start + i)];
    list->size = n;
}
