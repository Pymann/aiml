#include "db_runtime.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>
#include <list>
#include <algorithm>
#include <cmath>
#include <mutex>
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
// .kvwal file header (32 bytes, packed)
// ─────────────────────────────────────────────────────────────────────────────
// All Kv WAL files start with this header. Older format files (pre-mmap
// refactor) had no header → invalid magic → fatal error on open.
struct AimlKvWalHeader {
    char     magic[8];       // "AIMLKVWL"
    uint32_t version;        // KV_WAL_VERSION
    uint32_t flags;          // bit 0: typed (ser_nodes != nullptr at create)
    int64_t  val_size;       // 0 for string/typed, > 0 for fixed-size
    uint64_t wal_used;       // last-known end-of-data offset (best-effort)
};
static_assert(sizeof(AimlKvWalHeader) == 32, "AimlKvWalHeader must be 32 bytes");

static const char     KV_WAL_MAGIC[8] = {'A','I','M','L','K','V','W','L'};
// Version 2 (Schritt 4): Strings im WAL haben jetzt Null-Terminator
// (val_bytes = strlen+1). Macht zero-copy Reads moeglich.
static const uint32_t KV_WAL_VERSION  = 2;
static const uint32_t KV_WAL_FLAG_TYPED = 1u << 0;

// Initial sparse-reserve fuer jede Kv-WAL-Datei. Der Kernel allokiert nur
// tatsaechlich beschriebene Pages, aber das `apparent size` der Datei
// (was `ls -la`, File-Watcher, Indexer und nicht-sparse-aware Tools sehen)
// = die hier gewaehlte Reserve.
//
// 16 MB ist klein genug, dass Test-`/tmp` keinen IDE-File-Watcher kippt
// (~22 Tests × 16 MB = 352 MB apparent), und gross genug, dass kleine
// Stores ohne mremap-Wachstum auskommen. Bei Append-Overflow verdoppelt
// `kv_mmap_grow` die Reserve via `ftruncate` + `mremap(MREMAP_MAYMOVE)`.
static constexpr size_t KV_MMAP_RESERVE_INITIAL = (size_t)16 * 1024 * 1024;

// Maximale Anzahl gleichzeitig dekodierter typed-Eintraege im Cache.
// Bounded LRU (Schritt 7) verhindert unbegrenztes Wachstum von
// `decoded_cache`. Pointer aus `kv.get` werden invalid, wenn ihr Eintrag
// aus dem Cache faellt — dokumentiert in spec/migration/aiondb_kv_mmap_refactor.md.
static constexpr size_t MAX_DECODED_LRU = 1024;

// ─────────────────────────────────────────────────────────────────────────────
// Internal C++ structs
// ─────────────────────────────────────────────────────────────────────────────

struct AimlStore {
    std::mutex  mtx;             // per-store lock for concurrent insert/get/update/delete
    std::string type_name;
    int64_t     elem_size = 0;
    int32_t     total     = 0;   // used slots
    int32_t     count     = 0;   // active slots
    const AimlTypeNode* ser_nodes = nullptr;
    int32_t             ser_n     = 0;
    bool                typed_persistent = false;

    // ── In-Memory mode (default) ──────────────────────────────────────────────
    std::vector<uint8_t> mem_data;
    std::vector<uint8_t> mem_active;
    std::vector<std::vector<uint8_t>> decoded_slots;

    // ── Persistent mode (WAL + mmap) ─────────────────────────────────────────
    bool     persistent = false;
    int      db_fd  = -1;
    int      wal_fd = -1;
    void*    mmap_ptr  = nullptr;
    size_t   mmap_size = 0;
    uint8_t* data_ptr   = nullptr;   // mmap_ptr + sizeof(AimlDbHeader)
    uint8_t* active_ptr = nullptr;   // data_ptr + capacity * elem_size
    int32_t  capacity   = 0;

    // ── String-key index: field_offset → (string_value → slot_id) ────────────
    // Populated lazily on first get_by_key call for a given offset.
    std::unordered_map<int32_t, std::unordered_map<std::string, int32_t>> key_index;

    // ── Column Layout mode ────────────────────────────────────────────────────
    bool     column_layout = false;
    int32_t  n_fields = 0;
    std::vector<int32_t>           col_field_sizes;
    std::vector<int32_t>           col_field_offsets;
    std::vector<std::vector<uint8_t>> col_data;   // per-field flat arrays
    std::vector<uint8_t>           col_active;
    std::vector<uint8_t>           view_buf;      // scratch for assembled get
};

// Position+Length im mmap'd WAL — point-into-mmap statt copy.
struct AimlKvValueLoc {
    uint64_t offset;   // Byte-Offset im mmap (zeigt direkt auf val-Bytes)
    uint32_t length;   // val_bytes
};

struct AimlKv {
    std::mutex  mtx;  // per-KV lock for thread-safe access
    int64_t val_size;
    bool    blob_only = false;  // true when only kv_set_blob was used (no strdup entries)
    // ── Index (Schritt 3): source-of-truth fuer PERSISTENTE KVs ──────────────
    // key → {offset, length} im mmap. Reads geben Pointer in mmap zurueck.
    std::unordered_map<std::string, AimlKvValueLoc> index;
    // ── Storage fuer NON-PERSISTENTE KVs (legacy heap-map) ──────────────────
    // Wird in einer spaeteren Phase auf einen anonymen mmap-Buffer migriert.
    std::unordered_map<std::string, std::vector<uint8_t>> map;
    // Typed Kv: descriptor for recursive serialization of heap fields
    const AimlTypeNode* ser_nodes = nullptr;
    int32_t             ser_n     = 0;
    // Decoded cache: key → decoded struct (live heap pointers, freed on eviction).
    // Bounded LRU (Schritt 7): `decoded_lru` traegt Keys in MRU-Reihenfolge
    // (front = MRU, back = oldest). `decoded_lru_pos` mapped Key → Listen-iterator
    // fuer O(1) splice. Eviction beim Cache-Insert wenn size > MAX_DECODED_LRU.
    std::unordered_map<std::string, std::vector<uint8_t>> decoded_cache;
    std::list<std::string>                                decoded_lru;
    std::unordered_map<std::string, std::list<std::string>::iterator> decoded_lru_pos;
    // Persistence (WAL)
    bool        persistent = false;
    int         wal_fd     = -1;
    std::string wal_path;
    // ── mmap-backing (Schritt 2) ──────────────────────────────────────────────
    void*       wal_mmap      = nullptr;   // base pointer (initial KV_MMAP_RESERVE_INITIAL)
    size_t      wal_mmap_size = 0;         // grow via kv_mmap_grow
    uint64_t    wal_used      = 0;         // naechster freier Offset im WAL
};

struct AimlVecStore {
    int32_t dims;
    std::vector<std::string> ids;
    std::vector<float>       embeddings;  // flat: ids.size() * dims floats (cache-friendly)
    std::vector<float>       norms;       // precomputed L2 norm per embedding
    // Persist fields
    bool        persistent = false;
    int         wal_fd     = -1;
    std::string wal_path;
    std::mutex  mtx;
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
    std::mutex           mtx;  // per-handle lock for thread-safe access
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

// RAII: enable the serializer schema-format guard for the duration of a KV
// serialize/deserialize (persistent-storage path only), then restore the prior
// value.  Save/restore makes it nesting-safe (e.g. kv_set → store_wal_write).
// The guard makes stale on-disk blobs (written by an incompatible struct layout)
// fail with a clear diagnostic instead of decoding as garbage → SIGSEGV.
namespace { struct SerGuardOn { int prev; SerGuardOn() { prev = aiml_ser_set_guard(1); } ~SerGuardOn() { aiml_ser_set_guard(prev); } }; }

static void store_wal_write(AimlStore* s, uint8_t op, int32_t id, const void* data) {
    SerGuardOn _sg_;
    if (s->wal_fd < 0) return;
    char* blob = nullptr;
    uint32_t total_blob = 0;
    if (op == WAL_INSERT && data && s->typed_persistent && s->ser_nodes) {
        blob = aiml_ser_encode(data, s->ser_nodes, s->ser_n);
        if (!blob) return;
        int32_t blob_len = 0;
        memcpy(&blob_len, blob, 4);
        total_blob = (uint32_t)(4 + blob_len);
    }
    write(s->wal_fd, &op, 1);
    write(s->wal_fd, &id, 4);
    if (op == WAL_INSERT && data) {
        if (blob) {
            write(s->wal_fd, &total_blob, 4);
            write(s->wal_fd, blob, total_blob);
            free(blob);
        } else {
            write(s->wal_fd, data, (size_t)s->elem_size);
        }
    }
}

static void store_typed_free_slot(AimlStore* s, int32_t idx) {
    if (!s || !s->typed_persistent || !s->ser_nodes || idx < 0) return;
    if (idx >= (int32_t)s->decoded_slots.size()) return;
    auto& slot = s->decoded_slots[(size_t)idx];
    if (slot.empty()) return;
    aiml_ser_free_decoded(slot.data(), s->ser_nodes, s->ser_n);
    slot.clear();
}

static bool store_typed_assign_blob(AimlStore* s, int32_t idx, const char* blob) {
    SerGuardOn _sg_;
    if (!s || !s->typed_persistent || !s->ser_nodes || idx < 0 || !blob) return false;
    if ((int32_t)s->decoded_slots.size() <= idx) s->decoded_slots.resize((size_t)idx + 1);
    if ((int32_t)s->mem_active.size() <= idx) s->mem_active.resize((size_t)idx + 1, 0);
    store_typed_free_slot(s, idx);
    auto& slot = s->decoded_slots[(size_t)idx];
    slot.assign((size_t)s->elem_size, 0);
    if (aiml_ser_decode(blob, slot.data(), s->ser_nodes, s->ser_n) != 0) {
        slot.clear();
        return false;
    }
    return true;
}

static bool store_typed_assign_value(AimlStore* s, int32_t idx, const void* elem) {
    SerGuardOn _sg_;
    if (!s || !elem) return false;
    char* blob = aiml_ser_encode(elem, s->ser_nodes, s->ser_n);
    if (!blob) return false;
    bool ok = store_typed_assign_blob(s, idx, blob);
    free(blob);
    return ok;
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
            if (s->typed_persistent && s->ser_nodes) {
                uint32_t blob_bytes = 0;
                if (read(s->wal_fd, &blob_bytes, 4) != 4) break;
                std::vector<char> blob(blob_bytes);
                if (blob_bytes > 0 &&
                    (uint32_t)read(s->wal_fd, blob.data(), blob_bytes) != blob_bytes) break;
                if (!store_typed_assign_blob(s, idx, blob.data())) return false;
                if (!s->mem_active[(size_t)idx]) { s->mem_active[(size_t)idx] = 1; s->count++; }
                if (id > s->total) s->total = id;
            } else {
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
            }

        } else if (op == WAL_DELETE) {
            if (s->typed_persistent && s->ser_nodes) {
                if (idx >= 0 && idx < (int32_t)s->mem_active.size() && s->mem_active[(size_t)idx]) {
                    store_typed_free_slot(s, idx);
                    s->mem_active[(size_t)idx] = 0;
                    s->count--;
                }
            } else if (idx >= 0 && idx < s->capacity && s->active_ptr[idx]) {
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

// ─────────────────────────────────────────────────────────────────────────────
// AionDB Pfad-Konfiguration (db.set_path / AIONDB_PATH env var)
// ─────────────────────────────────────────────────────────────────────────────
//
// Resolution-Reihenfolge fuer den Speicher-Root:
//   1. AIONDB_PATH env var (dominiert immer, bei jedem Open neu gelesen)
//   2. g_aiondb_root (gesetzt durch aiml_db_set_path)
//   3. Fallback "./aiondb/"
//
// Sub-Pfad-Argument (`base_path` der open-Funktionen):
//   - nullptr/empty   → reine Root nutzen
//   - absolut (/...)  → as-is (backward-compat fuer Tests mit /tmp/aiml_test_*)
//   - relativ          → root + sub_path
static std::mutex   g_aiondb_path_mutex;
static std::string  g_aiondb_root;  // empty == not set via aiml_db_set_path

// Helper: stellt sicher dass der Pfad mit '/' endet.
static void path_ensure_trailing_slash(std::string& s) {
    if (!s.empty() && s.back() != '/') s += '/';
}

// Resolve AionDB-Verzeichnis fuer einen open-Call.
// Caller bekommt einen Pfad mit Trailing-Slash.
static std::string resolve_aiondb_dir(const char* sub_path) {
    // 1. Root bestimmen
    std::string root;
    const char* env = std::getenv("AIONDB_PATH");
    if (env && *env) {
        root = env;
    } else {
        std::lock_guard<std::mutex> lock(g_aiondb_path_mutex);
        root = g_aiondb_root.empty() ? std::string("./aiondb/") : g_aiondb_root;
    }
    path_ensure_trailing_slash(root);

    // 2. Sub-Pfad anwenden
    if (!sub_path || !*sub_path) return root;
    if (sub_path[0] == '/') {
        // Absolute Pfade haben Vorrang (backward compat).
        std::string abs = sub_path;
        path_ensure_trailing_slash(abs);
        return abs;
    }
    std::string combined = root + sub_path;
    path_ensure_trailing_slash(combined);
    return combined;
}

// Setzt den globalen AionDB-Root. Wird von db.set_path("/path") gerufen.
// Wert wird ignoriert wenn AIONDB_PATH env var gesetzt ist (env dominiert).
extern "C" void aiml_db_set_path(const char* path) {
    std::lock_guard<std::mutex> lock(g_aiondb_path_mutex);
    if (!path || !*path) {
        g_aiondb_root.clear();
        return;
    }
    g_aiondb_root = path;
}

// Gibt den aktuell aufgeloesten Root zurueck (mit Trailing-Slash).
// Single Source of Truth fuer AIML-Code, das den Pfad braucht
// (z. B. StoreOrganisator zum gezielten Loeschen).
extern "C" const char* aiml_db_get_path(void) {
    std::string root;
    const char* env = std::getenv("AIONDB_PATH");
    if (env && *env) {
        root = env;
    } else {
        std::lock_guard<std::mutex> lock(g_aiondb_path_mutex);
        root = g_aiondb_root.empty() ? std::string("./aiondb/") : g_aiondb_root;
    }
    path_ensure_trailing_slash(root);
    return strdup(root.c_str());
}

// Global registry for db.checkpoint_all — protected by g_db_mutex
static std::mutex                      g_db_mutex;
static std::vector<AimlStore*>         g_persistent_stores;
static std::vector<AimlKv*>            g_persistent_kvs;
static std::vector<AimlTimeSeries*>    g_persistent_ts;
static std::vector<AimlVecStore*>      g_persistent_vecs;

// Global name-based registries: base_path+name → open handle (singleton reuse)
static std::unordered_map<std::string, AimlStore*>      g_persistent_stores_by_name;
static std::unordered_map<std::string, AimlKv*>         g_persistent_kvs_by_name;
static std::unordered_map<std::string, AimlTimeSeries*> g_persistent_ts_by_name;
static std::unordered_map<std::string, AimlVecStore*>   g_persistent_vecs_by_name;
// In-memory (non-persistent) KV registry: name → singleton handle, shared across calls.
// Ensures db.open_kv("name", {persist: false}) returns the same instance on every call.
static std::unordered_map<std::string, AimlKv*>         g_non_persist_kvs_by_name;

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

    std::string bp = resolve_aiondb_dir(base_path);
    std::string nm = name ? name : "store";

    // Return existing handle if already open (same process, same name+path)
    std::string registry_key = bp + nm;
    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto it = g_persistent_stores_by_name.find(registry_key);
    if (it != g_persistent_stores_by_name.end()) return it->second;

    AimlStore* s = new AimlStore();
    s->type_name  = type_name ? type_name : "";
    s->elem_size  = elem_size;
    s->persistent = true;

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

    g_persistent_stores_by_name[registry_key] = s;
    g_persistent_stores.push_back(s);
    return s;
}

extern "C" void* aiml_db_store_open_typed_persist(const char* name, const char* type_name,
                                                    int64_t elem_size, const char* base_path,
                                                    const AimlTypeNode* nodes, int32_t n_nodes) {
    std::string dir = resolve_aiondb_dir(base_path);
    std::string nm = name ? name : "store";
    std::string registry_key = dir + nm;

    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto it = g_persistent_stores_by_name.find(registry_key);
    if (it != g_persistent_stores_by_name.end()) return it->second;

    mkdir(dir.c_str(), 0755);
    AimlStore* s = new AimlStore();
    s->type_name = type_name ? type_name : "";
    s->elem_size = elem_size;
    s->persistent = true;
    s->typed_persistent = true;
    s->ser_nodes = nodes;
    s->ser_n = n_nodes;

    std::string wal_path = dir + nm + ".wal";
    s->wal_fd = open(wal_path.c_str(), O_RDWR | O_CREAT, 0644);
    if (s->wal_fd >= 0) {
        struct stat ws;
        fstat(s->wal_fd, &ws);
        if (ws.st_size > 0 && !store_wal_replay(s)) {
            close(s->wal_fd);
            delete s;
            return nullptr;
        }
        lseek(s->wal_fd, 0, SEEK_END);
    }

    g_persistent_stores_by_name[registry_key] = s;
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
    std::lock_guard<std::mutex> lock(s->mtx);
    s->key_index.clear();  // invalidate string-key index on write
    if (elem_size != s->elem_size) return -1;

    if (s->column_layout) return store_col_insert(s, elem);

    if (s->persistent) {
        if (s->typed_persistent && s->ser_nodes) {
            int32_t idx = s->total;
            if (!store_typed_assign_value(s, idx, elem)) return -1;
            store_wal_write(s, WAL_INSERT, s->total + 1, elem);
            s->mem_active[(size_t)idx] = 1;
            s->total++;
            s->count++;
            return s->total;
        }
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
    std::lock_guard<std::mutex> lock(s->mtx);
    s->key_index.clear();  // invalidate string-key index on write
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
        if (s->typed_persistent && s->ser_nodes) {
            if (idx >= s->total || idx >= (int32_t)s->mem_active.size() || !s->mem_active[(size_t)idx]) return -1;
            if (!store_typed_assign_value(s, idx, elem)) return -1;
            store_wal_write(s, WAL_INSERT, id, elem);
            return 0;
        }
        if (idx >= s->total || !s->active_ptr[idx]) return -1;
        store_wal_write(s, WAL_INSERT, id, elem);  // WAL_INSERT replay is idempotent for active slots
        memcpy(s->data_ptr + (size_t)idx * s->elem_size, elem, (size_t)s->elem_size);
        return 0;
    }

    if (idx >= s->total || !s->mem_active[idx]) return -1;
    memcpy(s->mem_data.data() + (size_t)idx * s->elem_size, elem, (size_t)s->elem_size);
    return 0;
}

// Unlocked internal get — caller must hold s->mtx
static void* store_get_unlocked(AimlStore* s, int32_t id) {
    if (id < 1) return nullptr;
    int32_t idx = id - 1;
    if (s->column_layout) return store_col_get(s, idx);
    if (s->persistent) {
        if (s->typed_persistent && s->ser_nodes) {
            if (idx >= s->total || idx >= (int32_t)s->mem_active.size() || !s->mem_active[(size_t)idx]) return nullptr;
            if (idx >= (int32_t)s->decoded_slots.size()) return nullptr;
            auto& slot = s->decoded_slots[(size_t)idx];
            return slot.empty() ? nullptr : slot.data();
        }
        if (idx >= s->total || !s->active_ptr[idx]) return nullptr;
        return s->data_ptr + (size_t)idx * s->elem_size;
    }
    if (idx >= s->total || !s->mem_active[idx]) return nullptr;
    return s->mem_data.data() + (size_t)idx * s->elem_size;
}

extern "C" void* aiml_db_store_get(void* store, int32_t id) {
    if (!store || id < 1) return nullptr;
    AimlStore* s = static_cast<AimlStore*>(store);
    std::lock_guard<std::mutex> lock(s->mtx);
    return store_get_unlocked(s, id);
}

extern "C" void* aiml_db_store_get_by_key(void* store_ptr, const void* key_ptr,
                                            int32_t key_offset, int32_t key_size,
                                            int32_t is_string) {
    if (!store_ptr || !key_ptr) return nullptr;
    AimlStore* s = static_cast<AimlStore*>(store_ptr);
    std::lock_guard<std::mutex> lock(s->mtx);

    if (is_string) {
        // Use hash index: build lazily for this key_offset if not present
        auto& idx = s->key_index[key_offset];
        if (idx.empty()) {
            for (int32_t slot = 1; slot <= s->total; slot++) {
                void* elem = store_get_unlocked(s, slot);
                if (!elem) continue;
                const char* stored = nullptr;
                memcpy(&stored, static_cast<const uint8_t*>(elem) + key_offset, sizeof(char*));
                if (stored) idx[stored] = slot;
            }
        }
        const char* needle = static_cast<const char*>(key_ptr);
        auto it = idx.find(needle);
        if (it == idx.end()) return nullptr;
        return store_get_unlocked(s, it->second);
    }

    // Non-string: linear scan (rare path)
    for (int32_t slot = 1; slot <= s->total; slot++) {
        void* elem = store_get_unlocked(s, slot);
        if (!elem) continue;
        if (memcmp(static_cast<const uint8_t*>(elem) + key_offset, key_ptr, (size_t)key_size) == 0)
            return elem;
    }
    return nullptr;
}

extern "C" void aiml_db_store_delete(void* store, int32_t id) {
    if (!store || id < 1) return;
    AimlStore* s = static_cast<AimlStore*>(store);
    std::lock_guard<std::mutex> lock(s->mtx);
    s->key_index.clear();  // invalidate string-key index on write
    int32_t idx = id - 1;

    if (s->column_layout) { store_col_delete(s, idx); return; }

    if (s->persistent) {
        if (s->typed_persistent && s->ser_nodes) {
            if (idx >= s->total || idx >= (int32_t)s->mem_active.size()) return;
            if (s->mem_active[(size_t)idx]) {
                store_wal_write(s, WAL_DELETE, id, nullptr);
                store_typed_free_slot(s, idx);
                s->mem_active[(size_t)idx] = 0;
                s->count--;
            }
            return;
        }
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
    AimlStore* s = static_cast<AimlStore*>(store);
    std::lock_guard<std::mutex> lock(s->mtx);
    return s->count;
}

extern "C" int32_t aiml_db_store_checkpoint(void* store) {
    if (!store) return -1;
    AimlStore* s = static_cast<AimlStore*>(store);
    std::lock_guard<std::mutex> lock(s->mtx);
    if (s->typed_persistent && s->ser_nodes) {
        if (!s->persistent || s->wal_fd < 0) return 0;
        ftruncate(s->wal_fd, 0);
        lseek(s->wal_fd, 0, SEEK_SET);
        for (int32_t slot = 1; slot <= s->total; slot++) {
            int32_t idx = slot - 1;
            if (idx >= (int32_t)s->mem_active.size() || !s->mem_active[(size_t)idx]) continue;
            if (idx >= (int32_t)s->decoded_slots.size()) continue;
            auto& decoded = s->decoded_slots[(size_t)idx];
            if (decoded.empty()) continue;
            store_wal_write(s, WAL_INSERT, slot, decoded.data());
        }
        fsync(s->wal_fd);
        return 0;
    }
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
extern "C" int32_t aiml_db_vec_checkpoint(void* vs_ptr);

extern "C" int32_t aiml_db_checkpoint_all(void) {
    std::lock_guard<std::mutex> lock(g_db_mutex);
    int32_t result = 0;
    for (AimlStore*      s  : g_persistent_stores) if (aiml_db_store_checkpoint(s)  != 0) result = -1;
    for (AimlKv*         kv : g_persistent_kvs)    if (aiml_db_kv_checkpoint(kv)    != 0) result = -1;
    for (AimlTimeSeries* ts : g_persistent_ts)     if (aiml_db_ts_checkpoint(ts)    != 0) result = -1;
    for (AimlVecStore*  vs : g_persistent_vecs)   if (aiml_db_vec_checkpoint(vs)   != 0) result = -1;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Kv<K:V> implementation
// ─────────────────────────────────────────────────────────────────────────────

extern "C" void* aiml_db_kv_open(const char* name, int64_t val_size) {
    std::string nm = name ? name : "kv";
    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto it = g_non_persist_kvs_by_name.find(nm);
    if (it != g_non_persist_kvs_by_name.end()) return it->second;
    AimlKv* kv = new AimlKv();
    kv->val_size = val_size;
    g_non_persist_kvs_by_name[nm] = kv;
    return kv;
}

// ── Kv WAL helpers ────────────────────────────────────────────────────────────

static const uint8_t KV_WAL_SET      = 1;
static const uint8_t KV_WAL_DELETE   = 2;
static const uint8_t KV_WAL_SET_BLOB = 3;  // raw binary, no strdup on replay

// ── mmap-Backing (Schritt 2) ────────────────────────────────────────────────
// Bietet zero-copy Append in eine sparse-extended WAL-Datei. Alle Schreiber
// halten die kv->mtx, daher ist `wal_used` race-frei.

// Setzt das mmap-Backing fuer einen frisch geoeffneten kv->wal_fd auf.
// File wird auf eine geeignete Reserve sparse-extended, dann gemappt.
// Wenn fstat() schon eine groessere Datei zeigt (bestehender, gewachsener
// Store) wird die existierende Groesse beibehalten — sonst INITIAL.
// `wal_used` wird aus dem Header uebernommen (Crash-Recovery: best-effort).
// Returns true on success.
static bool kv_mmap_attach(AimlKv* kv) {
    if (kv->wal_fd < 0) return false;
    struct stat st;
    if (fstat(kv->wal_fd, &st) != 0) {
        fprintf(stderr,
            "[AionDB] FATAL: fstat failed for '%s' (errno=%d)\n",
            kv->wal_path.c_str(), errno);
        return false;
    }
    // Wunsch-Reserve: bestehende Dateigroesse behalten (gewachsener Store) oder
    // INITIAL bei frischem Store. Bei ENOMEM fallen wir in mehreren Stufen
    // zurueck, damit wir auch unter virt-Druck noch oeffnen koennen.
    size_t target = (size_t)st.st_size > KV_MMAP_RESERVE_INITIAL
                    ? (size_t)st.st_size
                    : KV_MMAP_RESERVE_INITIAL;
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) page = 4096;
    size_t hdr_aligned = ((size_t)sizeof(AimlKvWalHeader) + (size_t)page - 1)
                         & ~((size_t)page - 1);
    size_t candidates[] = {
        target,
        (size_t)4 * 1024 * 1024,
        (size_t)1 * 1024 * 1024,
        (size_t)256 * 1024,
        hdr_aligned,
    };
    for (size_t reserve : candidates) {
        // Zu klein gegenueber existierendem File-Inhalt: nicht versuchen,
        // sonst gibt's SIGBUS auf Seiten ueber dem mmap-Ende.
        if (reserve < (size_t)st.st_size) continue;
        if ((size_t)st.st_size < reserve) {
            if (ftruncate(kv->wal_fd, (off_t)reserve) != 0) {
                continue;  // try next, smaller candidate
            }
        }
        void* p = mmap(nullptr, reserve,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_NORESERVE,
                       kv->wal_fd, 0);
        if (p != MAP_FAILED) {
            kv->wal_mmap      = p;
            kv->wal_mmap_size = reserve;
            AimlKvWalHeader hdr;
            memcpy(&hdr, p, sizeof(hdr));
            kv->wal_used = (hdr.wal_used >= sizeof(hdr)) ? hdr.wal_used : sizeof(hdr);
            return true;
        }
        // mmap-fail: Datei-Groesse zuruecksetzen und naechsten Kandidaten probieren
        if ((size_t)st.st_size < reserve) {
            ftruncate(kv->wal_fd, st.st_size);
        }
    }
    fprintf(stderr,
        "[AionDB] FATAL: mmap failed for '%s' at all candidate sizes "
        "(target=%zu bytes; min=%zu bytes; errno=%d) — likely RLIMIT_AS or "
        "virt-fragmentation. Aborting to avoid silent data loss.\n",
        kv->wal_path.c_str(), target, hdr_aligned, errno);
    _exit(1);  // strikt: kein silent skip — Caller wuerde sonst nullptr ignorieren
    return false;  // unreachable
}

// Wachsen via ftruncate + mremap(MREMAP_MAYMOVE).
// Versucht zuerst Verdopplung; bei ENOMEM (typisch durch
// virt-Address-Space-Fragmentation, RLIMIT_AS oder vm.max_map_count)
// degradiert in mehreren Stufen: 2x → +50% → +25% → +64 MB → +16 MB.
// Mapping-Pointer kann sich aendern → Schreib-Helper lesen `kv->wal_mmap`
// nach jedem Append neu, Index-Offsets sind file-relativ.
static bool kv_mmap_grow(AimlKv* kv, size_t need_size) {
    if (!kv->wal_mmap) return false;
    size_t cur = kv->wal_mmap_size;
    if (need_size <= cur) return true;

    // Kandidaten-Groessen, vom optimistischsten zum konservativsten Schritt.
    size_t candidates[] = {
        cur * 2,
        cur + cur / 2,
        cur + cur / 4,
        cur + (size_t)64 * 1024 * 1024,
        cur + (size_t)16 * 1024 * 1024,
        cur + (size_t)4  * 1024 * 1024,
    };
    for (size_t cand : candidates) {
        if (cand < need_size) continue;  // muss mindestens das Ziel decken
        if (cand <= cur) continue;       // muss strikt wachsen
        if (ftruncate(kv->wal_fd, (off_t)cand) != 0) continue;
        void* p = mremap(kv->wal_mmap, kv->wal_mmap_size, cand, MREMAP_MAYMOVE);
        if (p == MAP_FAILED) {
            // Datei-Groesse zuruecksetzen, naechsten kleineren Kandidaten probieren
            ftruncate(kv->wal_fd, (off_t)cur);
            continue;
        }
        kv->wal_mmap      = p;
        kv->wal_mmap_size = cand;
        return true;
    }
    // Letzter Fallback: explizit munmap → ftruncate → mmap.
    // mremap(MREMAP_MAYMOVE) braucht kurzzeitig DOPPELTEN virt-Speicher
    // (alter + neuer Bereich gleichzeitig). Bei extremer Fragmentation
    // schlaegt das fehl. Ein expliziter unmap-then-mmap-Zyklus hat keinen
    // Dual-Allokations-Moment — der Kernel kann den neuen Bereich frei plazieren.
    // Daten bleiben durch das File-Backing erhalten.
    for (size_t cand : candidates) {
        if (cand < need_size) continue;
        if (cand <= cur) continue;
        size_t old_size = kv->wal_mmap_size;
        munmap(kv->wal_mmap, old_size);
        kv->wal_mmap = nullptr;
        if (ftruncate(kv->wal_fd, (off_t)cand) != 0) {
            // Versuche, den alten Mapping wiederherzustellen
            kv->wal_mmap = mmap(nullptr, old_size, PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_NORESERVE, kv->wal_fd, 0);
            if (kv->wal_mmap == MAP_FAILED) kv->wal_mmap = nullptr;
            kv->wal_mmap_size = (kv->wal_mmap ? old_size : 0);
            continue;
        }
        void* p = mmap(nullptr, cand, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_NORESERVE, kv->wal_fd, 0);
        if (p == MAP_FAILED) {
            ftruncate(kv->wal_fd, (off_t)old_size);
            kv->wal_mmap = mmap(nullptr, old_size, PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_NORESERVE, kv->wal_fd, 0);
            if (kv->wal_mmap == MAP_FAILED) kv->wal_mmap = nullptr;
            kv->wal_mmap_size = (kv->wal_mmap ? old_size : 0);
            continue;
        }
        kv->wal_mmap      = p;
        kv->wal_mmap_size = cand;
        return true;
    }
    fprintf(stderr,
        "[AionDB] FATAL: kv_mmap_grow exhausted all strategies for '%s' "
        "(cur=%zu, need=%zu) — virt-fragmentation or RLIMIT_AS reached. "
        "Workaround: reduce n_workers or increase the AS limit.\n",
        kv->wal_path.c_str(), cur, need_size);
    return false;
}

// Schreibt aktuellen wal_used Wert in den Header und syncs nur die erste Page.
static void kv_mmap_persist_used(AimlKv* kv) {
    if (!kv->wal_mmap) return;
    AimlKvWalHeader* hdr = (AimlKvWalHeader*)kv->wal_mmap;
    hdr->wal_used = kv->wal_used;
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) page = 4096;
    msync(kv->wal_mmap, (size_t)page, MS_ASYNC);
}

// Loest mmap auf (z.B. vor Compaction-Rewrite oder bei kv_clear).
static void kv_mmap_detach(AimlKv* kv) {
    if (kv->wal_mmap) {
        munmap(kv->wal_mmap, kv->wal_mmap_size);
        kv->wal_mmap      = nullptr;
        kv->wal_mmap_size = 0;
    }
}

// Reserviert n Bytes im mmap und kopiert src hinein. Caller haelt kv->mtx.
// Bei Reserve-Ueberlauf: kv_mmap_grow() verdoppelt via ftruncate+mremap.
static bool kv_mmap_append(AimlKv* kv, const void* src, size_t n) {
    if (!kv->wal_mmap) return false;
    if (kv->wal_used + n > kv->wal_mmap_size) {
        if (!kv_mmap_grow(kv, kv->wal_used + n)) {
            // kv_mmap_grow hat bereits FATAL geloggt — abbrechen
            _exit(1);
        }
    }
    memcpy((char*)kv->wal_mmap + kv->wal_used, src, n);
    kv->wal_used += n;
    return true;
}

// Write a SET record: op(1) + key_len(4) + key + val_bytes(4) + val
static void kv_wal_write_set(AimlKv* kv, const char* key, const void* val, uint32_t val_bytes) {
    uint32_t key_len = (uint32_t)strlen(key);
    kv_mmap_append(kv, &KV_WAL_SET, 1);
    kv_mmap_append(kv, &key_len,    4);
    kv_mmap_append(kv,  key,        key_len);
    kv_mmap_append(kv, &val_bytes,  4);
    if (val_bytes > 0 && val) kv_mmap_append(kv, val, val_bytes);
}

// Write a SET_BLOB record: same format as SET but op=3 (raw binary on replay)
static void kv_wal_write_set_blob(AimlKv* kv, const char* key, const void* val, uint32_t val_bytes) {
    uint32_t key_len = (uint32_t)strlen(key);
    kv_mmap_append(kv, &KV_WAL_SET_BLOB, 1);
    kv_mmap_append(kv, &key_len,    4);
    kv_mmap_append(kv,  key,        key_len);
    kv_mmap_append(kv, &val_bytes,  4);
    if (val_bytes > 0 && val) kv_mmap_append(kv, val, val_bytes);
}

// Write a DELETE record: op(1) + key_len(4) + key
static void kv_wal_write_delete(AimlKv* kv, const char* key) {
    uint32_t key_len = (uint32_t)strlen(key);
    kv_mmap_append(kv, &KV_WAL_DELETE, 1);
    kv_mmap_append(kv, &key_len,       4);
    kv_mmap_append(kv,  key,           key_len);
}

// Helper: free decoded_cache entry and remove it from cache + LRU.
static void kv_decoded_evict(AimlKv* kv, const std::string& key) {
    auto dc = kv->decoded_cache.find(key);
    if (dc != kv->decoded_cache.end()) {
        aiml_ser_free_decoded(dc->second.data(), kv->ser_nodes, kv->ser_n);
        kv->decoded_cache.erase(dc);
    }
    auto pos = kv->decoded_lru_pos.find(key);
    if (pos != kv->decoded_lru_pos.end()) {
        kv->decoded_lru.erase(pos->second);
        kv->decoded_lru_pos.erase(pos);
    }
}

// Mark key as most-recently-used. Caller has already inserted into decoded_cache.
// Splices existing list entry to front, or push_front if new.
// Returns nothing — caller must update decoded_lru_pos[key].
static void kv_decoded_touch(AimlKv* kv, const std::string& key) {
    auto pos = kv->decoded_lru_pos.find(key);
    if (pos != kv->decoded_lru_pos.end()) {
        // Existing → splice to front
        kv->decoded_lru.splice(kv->decoded_lru.begin(), kv->decoded_lru, pos->second);
    } else {
        // New → push_front, record iterator
        kv->decoded_lru.push_front(key);
        kv->decoded_lru_pos[key] = kv->decoded_lru.begin();
    }
}

// Free all decoded entries and clear LRU bookkeeping. Used by evict/clear/reload.
static void kv_decoded_clear_all(AimlKv* kv) {
    if (kv->ser_nodes) {
        for (auto& [k, v] : kv->decoded_cache)
            aiml_ser_free_decoded(v.data(), kv->ser_nodes, kv->ser_n);
    }
    kv->decoded_cache.clear();
    kv->decoded_lru.clear();
    kv->decoded_lru_pos.clear();
}

// After inserting a fresh decoded entry, evict the oldest entries until
// the cache is within MAX_DECODED_LRU. Frees decoded heap data.
static void kv_decoded_lru_trim(AimlKv* kv) {
    while (kv->decoded_cache.size() > MAX_DECODED_LRU
           && !kv->decoded_lru.empty()) {
        const std::string old_key = kv->decoded_lru.back();
        auto dc = kv->decoded_cache.find(old_key);
        if (dc != kv->decoded_cache.end()) {
            aiml_ser_free_decoded(dc->second.data(), kv->ser_nodes, kv->ser_n);
            kv->decoded_cache.erase(dc);
        }
        kv->decoded_lru_pos.erase(old_key);
        kv->decoded_lru.pop_back();
    }
}

static void kv_wal_replay(AimlKv* kv);  // forward decl

// Write a fresh header to an empty WAL file. fd is left positioned right
// after the header. Returns true on success.
static bool kv_wal_init_header(int fd, bool typed, int64_t val_size) {
    AimlKvWalHeader hdr = {};
    memcpy(hdr.magic, KV_WAL_MAGIC, 8);
    hdr.version  = KV_WAL_VERSION;
    hdr.flags    = typed ? KV_WAL_FLAG_TYPED : 0u;
    hdr.val_size = val_size;
    hdr.wal_used = sizeof(hdr);
    if (lseek(fd, 0, SEEK_SET) < 0) return false;
    ssize_t n = write(fd, &hdr, sizeof(hdr));
    if (n != (ssize_t)sizeof(hdr)) return false;
    fsync(fd);
    return true;
}

// Validate the header of an existing WAL file. On magic/version mismatch,
// prints a fatal message and exits — old-format files are unrecoverable.
// Returns true if the header is well-formed.
static bool kv_wal_check_header(int fd, const std::string& path) {
    AimlKvWalHeader hdr;
    if (lseek(fd, 0, SEEK_SET) < 0) return false;
    ssize_t n = read(fd, &hdr, sizeof(hdr));
    if (n != (ssize_t)sizeof(hdr)) {
        fprintf(stderr,
            "[AionDB] FATAL: KV file '%s' too short for header (got %zd bytes).\n"
            "[AionDB] FATAL: Likely an old-format file. Delete it and re-create:\n"
            "[AionDB] FATAL:   rm '%s'\n",
            path.c_str(), n, path.c_str());
        _exit(1);
    }
    if (memcmp(hdr.magic, KV_WAL_MAGIC, 8) != 0) {
        fprintf(stderr,
            "[AionDB] FATAL: KV file '%s' has invalid magic (expected 'AIMLKVWL').\n"
            "[AionDB] FATAL: This is an old-format file. Delete it and re-create:\n"
            "[AionDB] FATAL:   rm '%s'\n",
            path.c_str(), path.c_str());
        _exit(1);
    }
    if (hdr.version != KV_WAL_VERSION) {
        fprintf(stderr,
            "[AionDB] FATAL: KV file '%s' has unsupported version %u (this build supports %u).\n"
            "[AionDB] FATAL:   rm '%s'\n",
            path.c_str(), hdr.version, KV_WAL_VERSION, path.c_str());
        _exit(1);
    }
    return true;
}

// Open a KV WAL file and either initialize a fresh header (empty file) or
// validate an existing one. Returns fd on success, -1 if open() itself
// failed. On header-mismatch this function does not return — it _exit()s.
static int kv_wal_open_with_header(const std::string& path, bool typed,
                                    int64_t val_size) {
    int fd = open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return -1; }
    if (st.st_size == 0) {
        if (!kv_wal_init_header(fd, typed, val_size)) { close(fd); return -1; }
    } else {
        kv_wal_check_header(fd, path);  // does not return on mismatch
    }
    return fd;
}

// (kv_lazy_reload removed in Schritt 5: evict no longer clears the index, so
//  reads find offsets directly and the kernel re-faults pages on access.)

// Replay WAL records, starting right after the file header.
// Populiert kv->index (offset+length) ohne Werte zu kopieren — alle Reads
// gehen ueber mmap. Bei Typed: decoded_cache wird invalidiert pro key.
static void kv_wal_replay(AimlKv* kv) {
    lseek(kv->wal_fd, (off_t)sizeof(AimlKvWalHeader), SEEK_SET);
    off_t last_good_pos = (off_t)sizeof(AimlKvWalHeader);
    while (true) {
        uint8_t op;
        if (read(kv->wal_fd, &op, 1) != 1) break;
        uint32_t key_len;
        if (read(kv->wal_fd, &key_len, 4) != 4) break;
        std::string key(key_len, '\0');
        if ((uint32_t)read(kv->wal_fd, key.data(), key_len) != key_len) break;
        if (op == KV_WAL_SET || op == KV_WAL_SET_BLOB) {
            uint32_t val_bytes;
            if (read(kv->wal_fd, &val_bytes, 4) != 4) break;
            off_t value_offset = lseek(kv->wal_fd, 0, SEEK_CUR);
            if (op == KV_WAL_SET_BLOB) kv->blob_only = true;
            if (kv->ser_nodes) kv_decoded_evict(kv, key);
            // Werte ueberspringen — Index reicht; Reads pointen direkt in mmap
            if (lseek(kv->wal_fd, val_bytes, SEEK_CUR) < 0) break;
            kv->index[key] = AimlKvValueLoc{ (uint64_t)value_offset, val_bytes };
        } else if (op == KV_WAL_DELETE) {
            auto it = kv->index.find(key);
            if (it != kv->index.end()) {
                if (kv->ser_nodes) kv_decoded_evict(kv, key);
                kv->index.erase(it);
            }
        } else {
            break; // unknown op
        }
        last_good_pos = lseek(kv->wal_fd, 0, SEEK_CUR);
    }
    kv->wal_used = (uint64_t)last_good_pos;
}

extern "C" void* aiml_db_kv_open_persist(const char* name, int64_t val_size,
                                           const char* base_path) {
    std::string dir = resolve_aiondb_dir(base_path);
    std::string nm  = name ? name : "kv";
    std::string registry_key = dir + nm;

    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto it = g_persistent_kvs_by_name.find(registry_key);
    if (it != g_persistent_kvs_by_name.end()) return it->second;

    mkdir(dir.c_str(), 0755);
    AimlKv* kv    = new AimlKv();
    kv->val_size   = val_size;
    kv->persistent = true;
    kv->wal_path   = dir + nm + ".kvwal";
    int fd = kv_wal_open_with_header(kv->wal_path, /*typed=*/false, val_size);
    if (fd >= 0) {
        kv->wal_fd = fd;
        if (!kv_mmap_attach(kv)) {
            close(fd);
            kv->wal_fd = -1;
            delete kv;
            return nullptr;
        }
        kv_wal_replay(kv);
    }
    g_persistent_kvs_by_name[registry_key] = kv;
    g_persistent_kvs.push_back(kv);
    return kv;
}

extern "C" int32_t aiml_db_kv_checkpoint(void* kv_ptr) {
    if (!kv_ptr) return -1;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    std::lock_guard<std::mutex> lock(kv->mtx);
    if (!kv->persistent || kv->wal_fd < 0) return 0;

    // Crash-safe Compaction via Temp-File + atomic rename.
    // Old file stays intact until rename succeeds.
    bool typed = (kv->ser_nodes != nullptr);
    bool blob_only_save = kv->blob_only;
    std::string temp_path = kv->wal_path + ".compact";

    // 1. Cleanup any orphan from a previously interrupted checkpoint
    unlink(temp_path.c_str());

    // 2. Open temp file, write header, ftruncate to initial reserve, mmap
    int temp_fd = open(temp_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (temp_fd < 0) {
        fprintf(stderr,
            "[AionDB] FATAL: kv_checkpoint: cannot create temp '%s' (errno=%d)\n",
            temp_path.c_str(), errno);
        _exit(1);
    }
    if (!kv_wal_init_header(temp_fd, typed, kv->val_size)) {
        close(temp_fd); unlink(temp_path.c_str());
        fprintf(stderr,
            "[AionDB] FATAL: kv_checkpoint: header write failed for '%s'\n",
            temp_path.c_str());
        _exit(1);
    }
    if (ftruncate(temp_fd, (off_t)KV_MMAP_RESERVE_INITIAL) != 0) {
        close(temp_fd); unlink(temp_path.c_str());
        fprintf(stderr,
            "[AionDB] FATAL: kv_checkpoint: ftruncate(temp) failed (errno=%d)\n", errno);
        _exit(1);
    }
    void* temp_mmap = mmap(nullptr, KV_MMAP_RESERVE_INITIAL,
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_NORESERVE,
                           temp_fd, 0);
    if (temp_mmap == MAP_FAILED) {
        close(temp_fd); unlink(temp_path.c_str());
        fprintf(stderr,
            "[AionDB] FATAL: kv_checkpoint: mmap(temp) failed (errno=%d)\n", errno);
        _exit(1);
    }

    // 3. Save old state, point kv at temp for the rewrite loop
    void*  old_mmap      = kv->wal_mmap;
    size_t old_mmap_size = kv->wal_mmap_size;
    int    old_fd        = kv->wal_fd;
    auto   old_index     = std::move(kv->index);

    kv->wal_mmap      = temp_mmap;
    kv->wal_mmap_size = KV_MMAP_RESERVE_INITIAL;
    kv->wal_used      = sizeof(AimlKvWalHeader);
    kv->wal_fd        = temp_fd;
    kv->index.clear();
    kv->blob_only = blob_only_save;  // preserve mode through rewrite

    // 4. Copy each record from old mmap → temp via kv_wal_write_* (uses memcpy
    //    into temp_mmap; kv_mmap_grow handles overflow within temp).
    for (auto& [k, old_loc] : old_index) {
        const void* old_val = (const char*)old_mmap + old_loc.offset;
        if (blob_only_save) {
            kv_wal_write_set_blob(kv, k.c_str(), old_val, old_loc.length);
        } else {
            kv_wal_write_set(kv, k.c_str(), old_val, old_loc.length);
        }
        uint64_t new_offset = kv->wal_used - old_loc.length;
        kv->index[k] = AimlKvValueLoc{new_offset, old_loc.length};
    }

    // 5. Persist + sync temp
    kv_mmap_persist_used(kv);
    fsync(temp_fd);

    // 6. Detach old mmap + close old fd (data fully captured in temp now)
    if (old_mmap) munmap(old_mmap, old_mmap_size);
    if (old_fd >= 0) close(old_fd);

    // 7. Atomic rename temp → real path
    if (rename(temp_path.c_str(), kv->wal_path.c_str()) != 0) {
        fprintf(stderr,
            "[AionDB] FATAL: kv_checkpoint: rename '%s' → '%s' failed (errno=%d)\n",
            temp_path.c_str(), kv->wal_path.c_str(), errno);
        _exit(1);
    }
    return 0;
}

// Reload: re-read WAL from the beginning so cross-process writes become visible.
extern "C" void aiml_db_kv_reload(void* kv_ptr) {
    if (!kv_ptr) return;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    std::lock_guard<std::mutex> lock(kv->mtx);
    if (!kv->persistent || kv->wal_path.empty()) return;
    // Close existing fd and reopen fresh so we see any bytes written by other processes
    kv_mmap_detach(kv);
    if (kv->wal_fd >= 0) close(kv->wal_fd);
    bool typed = (kv->ser_nodes != nullptr);
    kv->wal_fd = kv_wal_open_with_header(kv->wal_path, typed, kv->val_size);
    if (kv->wal_fd < 0) {
        fprintf(stderr,
            "[AionDB] FATAL: kv_reload: cannot reopen '%s'\n",
            kv->wal_path.c_str());
        _exit(1);
    }
    if (!kv_mmap_attach(kv)) {
        // kv_mmap_attach already printed a FATAL message; refuse to leave
        // the store with an open fd but no mmap (silent write-loss zone).
        _exit(1);
    }
    // Clear current in-memory state (persistent path)
    kv_decoded_clear_all(kv);
    kv->index.clear();
    kv->blob_only = false;  // reset — WAL replay will set it again if needed
    // Replay WAL from start. Append-position is tracked in kv->wal_used,
    // do NOT SEEK_END the fd — after ftruncate(RESERVE) it points to 16 GB.
    kv_wal_replay(kv);
}

extern "C" void* aiml_db_kv_open_typed(const char* name, int64_t val_size,
                                         const AimlTypeNode* nodes, int32_t n_nodes) {
    std::string nm = name ? name : "kv";
    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto it = g_non_persist_kvs_by_name.find(nm);
    if (it != g_non_persist_kvs_by_name.end()) return it->second;
    AimlKv* kv    = new AimlKv();
    kv->val_size  = val_size;
    kv->ser_nodes = nodes;
    kv->ser_n     = n_nodes;
    g_non_persist_kvs_by_name[nm] = kv;
    return kv;
}

extern "C" void* aiml_db_kv_open_typed_persist(const char* name, int64_t val_size,
                                                 const char* base_path,
                                                 const AimlTypeNode* nodes, int32_t n_nodes) {
    std::string dir = resolve_aiondb_dir(base_path);
    std::string nm  = name ? name : "kv";
    std::string registry_key = dir + nm;

    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto it = g_persistent_kvs_by_name.find(registry_key);
    if (it != g_persistent_kvs_by_name.end()) return it->second;

    mkdir(dir.c_str(), 0755);
    AimlKv* kv    = new AimlKv();
    kv->val_size   = val_size;
    kv->ser_nodes  = nodes;
    kv->ser_n      = n_nodes;
    kv->persistent = true;
    kv->wal_path   = dir + nm + ".kvwal";
    int fd = kv_wal_open_with_header(kv->wal_path, /*typed=*/true, val_size);
    if (fd >= 0) {
        kv->wal_fd = fd;
        if (!kv_mmap_attach(kv)) {
            close(fd);
            kv->wal_fd = -1;
            delete kv;
            return nullptr;
        }
        kv_wal_replay(kv);
    }
    g_persistent_kvs_by_name[registry_key] = kv;
    g_persistent_kvs.push_back(kv);
    return kv;
}

extern "C" void aiml_db_kv_set(void* kv_ptr, const char* key, const void* val, int64_t val_size) {
    SerGuardOn _sg_;
    if (!kv_ptr || !key) return;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    std::lock_guard<std::mutex> lock(kv->mtx);
    if (kv->ser_nodes && val) {
        // Typed: encode struct → blob, then invalidate decoded cache.
        //
        // Order is critical: encode MUST run before evict because `val` can
        // alias into the decoded_cache (decl: [p, ptr<T>, kv.get] = direct
        // pointer; decl: [p, T, kv.get] = shallow memcpy → shared heap
        // pointers). If evict ran first it would free buffers the encoder
        // still needs to read → use-after-free SIGSEGV in strlen.
        // See doc/issue/kv_get_mutate_set_same_key_crash.md
        char* blob = aiml_ser_encode(val, kv->ser_nodes, kv->ser_n);
        if (!blob) return;
        // Detect "val is the cache slot for this key" — the ptr<T> sync
        // pattern. In that case the cache is already canonical (all
        // mutations went through the pointer), and freeing it would also
        // free user-pushed string literals or other non-decoded data the
        // user wrote into it. Just drop the cache entry without freeing
        // its heap allocs — they're effectively owned by the user now;
        // next kv.get will re-decode fresh from WAL.
        auto dc = kv->decoded_cache.find(key);
        bool val_is_cache = (dc != kv->decoded_cache.end() && dc->second.data() == val);
        if (val_is_cache) {
            kv->decoded_cache.erase(dc);
            auto pos = kv->decoded_lru_pos.find(key);
            if (pos != kv->decoded_lru_pos.end()) {
                kv->decoded_lru.erase(pos->second);
                kv->decoded_lru_pos.erase(pos);
            }
        } else {
            kv_decoded_evict(kv, key);
        }
        int32_t data_len;
        memcpy(&data_len, blob, 4);
        uint32_t total_blob = (uint32_t)(4 + data_len);
        if (kv->persistent && kv->wal_fd >= 0) {
            kv_wal_write_set(kv, key, blob, total_blob);
            uint64_t value_offset = kv->wal_used - total_blob;
            kv->index[key] = AimlKvValueLoc{value_offset, total_blob};
        } else {
            // Non-persistent typed: blob in kv->map (legacy path)
            std::vector<uint8_t> buf(total_blob);
            memcpy(buf.data(), blob, total_blob);
            kv->map[key] = std::move(buf);
        }
        free(blob);
    } else if (val_size == 0) {
        const char* str = val ? static_cast<const char*>(val) : "";
        uint32_t slen_with_nul = (uint32_t)strlen(str) + 1;  // include \0
        if (kv->persistent && kv->wal_fd >= 0) {
            // String-Mode persistent: WAL haelt strlen+1 Bytes inkl. \0,
            // damit kv_get einen direkten mmap-Pointer als C-String liefert.
            kv_wal_write_set(kv, key, str, slen_with_nul);
            uint64_t value_offset = kv->wal_used - slen_with_nul;
            kv->index[key] = AimlKvValueLoc{value_offset, slen_with_nul};
        } else {
            // Non-persistent string: kv->map mit strdup'd char* (legacy)
            auto old_it = kv->map.find(key);
            if (old_it != kv->map.end()) {
                char* old_ptr = nullptr;
                memcpy(&old_ptr, old_it->second.data(), sizeof(char*));
                free(old_ptr);
            }
            char* copy = strdup(str);
            std::vector<uint8_t> buf(sizeof(char*));
            memcpy(buf.data(), &copy, sizeof(char*));
            kv->map[key] = std::move(buf);
        }
    } else {
        // Fixed-size mode
        if (kv->persistent && kv->wal_fd >= 0) {
            kv_wal_write_set(kv, key, val, val ? (uint32_t)val_size : 0);
            uint64_t value_offset = kv->wal_used - (uint32_t)val_size;
            kv->index[key] = AimlKvValueLoc{value_offset, (uint32_t)val_size};
        } else {
            std::vector<uint8_t> buf((size_t)val_size);
            if (val) memcpy(buf.data(), val, (size_t)val_size);
            kv->map[key] = std::move(buf);
        }
    }
}

extern "C" void* aiml_db_kv_get(void* kv_ptr, const char* key) {
    SerGuardOn _sg_;
    if (!kv_ptr || !key) return nullptr;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    std::lock_guard<std::mutex> lock(kv->mtx);
    if (kv->persistent) {
        // Index-Pfad (Schritt 3)
        auto it = kv->index.find(key);
        if (it == kv->index.end()) return nullptr;
        const AimlKvValueLoc& loc = it->second;
        const char* val_ptr = (const char*)kv->wal_mmap + loc.offset;
        if (kv->ser_nodes) {
            // Typed: decode in cache (bounded LRU)
            auto dc = kv->decoded_cache.find(key);
            if (dc != kv->decoded_cache.end()) {
                kv_decoded_touch(kv, key);
                return dc->second.data();
            }
            std::vector<uint8_t> decoded((size_t)kv->val_size, 0);
            aiml_ser_decode(val_ptr, decoded.data(), kv->ser_nodes, kv->ser_n);
            auto& cached = kv->decoded_cache[key];
            cached = std::move(decoded);
            kv_decoded_touch(kv, key);
            kv_decoded_lru_trim(kv);
            return cached.data();
        }
        // String / Blob / Fixed: Pointer direkt in mmap (zero-copy).
        // String hat \0-Terminator innerhalb der length Bytes (Schritt 4).
        return (void*)val_ptr;
    }
    // Non-persistent: legacy map-Pfad
    auto it = kv->map.find(key);
    if (it == kv->map.end()) return nullptr;
    if (kv->ser_nodes) {
        auto dc = kv->decoded_cache.find(key);
        if (dc != kv->decoded_cache.end()) {
            kv_decoded_touch(kv, key);
            return dc->second.data();
        }
        std::vector<uint8_t> decoded((size_t)kv->val_size, 0);
        aiml_ser_decode(reinterpret_cast<const char*>(it->second.data()),
                        decoded.data(), kv->ser_nodes, kv->ser_n);
        auto& cached = kv->decoded_cache[key];
        cached = std::move(decoded);
        kv_decoded_touch(kv, key);
        kv_decoded_lru_trim(kv);
        return cached.data();
    }
    if (kv->val_size == 0) {
        char* ptr = nullptr;
        memcpy(&ptr, it->second.data(), sizeof(char*));
        return ptr;
    }
    return it->second.data();
}

// Like aiml_db_kv_get but returns a freshly deep-copied struct (typed only).
// Caller (typically `kv.get(k, {copy: true})`) gets an instance whose
// strings/lists/maps/buffers are independent of the kv's decoded_cache —
// safe for mutate-and-set-same-key. See doc/issue/kv_get_mutate_set_same_key_crash.md.
//
// Lifetime contract: the returned pointer is valid until the next
// get_copy / set / delete on the same key. Subsequent get_copy on the same
// key frees the previous copy's heap allocations.
extern "C" void* aiml_db_kv_get_copy(void* kv_ptr, const char* key) {
    SerGuardOn _sg_;
    if (!kv_ptr || !key) return nullptr;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    std::lock_guard<std::mutex> lock(kv->mtx);
    if (kv->persistent) {
        auto it = kv->index.find(key);
        if (it == kv->index.end()) return nullptr;
        const AimlKvValueLoc& loc = it->second;
        const char* val_ptr = (const char*)kv->wal_mmap + loc.offset;
        if (kv->ser_nodes) {
            // Always decode fresh from WAL. Previous slot heap allocations
            // (if any from a prior get/get_copy) are freed first.
            kv_decoded_evict(kv, key);
            auto& slot = kv->decoded_cache[key];
            slot.assign((size_t)kv->val_size, 0);
            aiml_ser_decode(val_ptr, slot.data(), kv->ser_nodes, kv->ser_n);
            kv_decoded_touch(kv, key);
            kv_decoded_lru_trim(kv);
            return slot.data();
        }
        return (void*)val_ptr;  // non-typed: same as get (zero-copy from mmap)
    }
    // Non-persistent legacy path
    auto it = kv->map.find(key);
    if (it == kv->map.end()) return nullptr;
    if (kv->ser_nodes) {
        kv_decoded_evict(kv, key);
        auto& slot = kv->decoded_cache[key];
        slot.assign((size_t)kv->val_size, 0);
        aiml_ser_decode(reinterpret_cast<const char*>(it->second.data()),
                        slot.data(), kv->ser_nodes, kv->ser_n);
        kv_decoded_touch(kv, key);
        kv_decoded_lru_trim(kv);
        return slot.data();
    }
    if (kv->val_size == 0) {
        char* ptr = nullptr;
        memcpy(&ptr, it->second.data(), sizeof(char*));
        return ptr;
    }
    return it->second.data();
}

extern "C" int32_t aiml_db_kv_has(void* kv_ptr, const char* key) {
    if (!kv_ptr || !key) return 0;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    std::lock_guard<std::mutex> lock(kv->mtx);
    if (kv->persistent) return kv->index.count(key) ? 1 : 0;
    return kv->map.count(key) ? 1 : 0;
}

extern "C" int32_t aiml_db_kv_len(void* kv_ptr) {
    if (!kv_ptr) return 0;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    std::lock_guard<std::mutex> lock(kv->mtx);
    if (kv->persistent) return (int32_t)kv->index.size();
    return (int32_t)kv->map.size();
}

extern "C" void aiml_db_kv_delete(void* kv_ptr, const char* key) {
    if (!kv_ptr || !key) return;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    std::lock_guard<std::mutex> lock(kv->mtx);
    if (kv->persistent) {
        auto it = kv->index.find(key);
        if (it == kv->index.end()) return;
        kv_wal_write_delete(kv, key);
        if (kv->ser_nodes) kv_decoded_evict(kv, key);
        kv->index.erase(it);
        return;
    }
    // Non-persistent
    auto it = kv->map.find(key);
    if (it == kv->map.end()) return;
    if (kv->ser_nodes) {
        kv_decoded_evict(kv, key);
    } else if (kv->val_size == 0 && !kv->blob_only) {
        char* ptr = nullptr;
        memcpy(&ptr, it->second.data(), sizeof(char*));
        free(ptr);
    }
    kv->map.erase(it);
}

// ── Blob operations for buffer persistence ──────────────────────────────────
// set_blob: stores raw binary data as a single KV entry (uses WAL like normal set)
extern "C" void aiml_db_kv_set_blob(void* kv_ptr, const char* key,
                                      const void* data, int32_t size) {
    if (!kv_ptr || !key || !data || size <= 0) return;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    std::lock_guard<std::mutex> lock(kv->mtx);
    kv->blob_only = true;  // mark as blob-mode (no strdup entries to free)
    if (kv->persistent && kv->wal_fd >= 0) {
        kv_wal_write_set_blob(kv, key, data, (uint32_t)size);
        uint64_t value_offset = kv->wal_used - (uint32_t)size;
        kv->index[key] = AimlKvValueLoc{value_offset, (uint32_t)size};
    } else {
        std::vector<uint8_t> buf((size_t)size);
        memcpy(buf.data(), data, (size_t)size);
        kv->map[key] = std::move(buf);
    }
}

// get_blob: returns pointer to raw data + writes size to out_size
// Returns nullptr if key not found. Data valid until next set/delete on same key.
extern "C" void* aiml_db_kv_get_blob(void* kv_ptr, const char* key, int32_t* out_size) {
    if (!kv_ptr || !key) { if (out_size) *out_size = 0; return nullptr; }
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    std::lock_guard<std::mutex> lock(kv->mtx);
    if (kv->persistent) {
        auto it = kv->index.find(key);
        if (it == kv->index.end()) { if (out_size) *out_size = 0; return nullptr; }
        if (out_size) *out_size = (int32_t)it->second.length;
        return (void*)((const char*)kv->wal_mmap + it->second.offset);
    }
    // Non-persistent: legacy map-Pfad
    auto it = kv->map.find(key);
    if (it == kv->map.end()) { if (out_size) *out_size = 0; return nullptr; }
    if (out_size) *out_size = (int32_t)it->second.size();
    return it->second.data();
}

extern "C" void aiml_db_kv_clear(void* kv_ptr) {
    if (!kv_ptr) return;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    std::lock_guard<std::mutex> lock(kv->mtx);
    if (kv->persistent && kv->wal_fd >= 0) {
        // Truncate WAL — clear is atomic. Detach mmap first (truncate would
        // invalidate the mapping), re-init header, re-attach mmap.
        kv_mmap_detach(kv);
        ftruncate(kv->wal_fd, 0);
        bool typed = (kv->ser_nodes != nullptr);
        if (!kv_wal_init_header(kv->wal_fd, typed, kv->val_size)) {
            fprintf(stderr,
                "[AionDB] FATAL: kv_clear: cannot rewrite header for '%s'\n",
                kv->wal_path.c_str());
            _exit(1);
        }
        if (!kv_mmap_attach(kv)) {
            // kv_mmap_attach already printed a FATAL message; refuse to leave
            // the store in a zombie state where writes silently fail.
            _exit(1);
        }
        // wal_used wurde von kv_mmap_attach aus dem Header gelesen
    }
    // Decoded-Cache leeren (typed)
    kv_decoded_clear_all(kv);
    // Non-persistent: kv->map mit strdup'd Strings leeren
    if (!kv->persistent && kv->val_size == 0 && !kv->blob_only) {
        for (auto& [k, v] : kv->map) {
            char* ptr = nullptr;
            memcpy(&ptr, v.data(), sizeof(char*));
            free(ptr);
        }
    }
    kv->index.clear();
    kv->map.clear();
    kv->blob_only = false;
}

// Evict: drop in-memory data while keeping the index intact.
// Persistent: madvise(MADV_DONTNEED) returns mmap'd pages to the OS;
// kernel re-faults them transparently on the next read via the index.
// Non-persistent: kv->map is cleared (no WAL → data is lost).
// Decoded-cache (typed) is always cleared.
extern "C" void aiml_db_kv_evict(void* kv_ptr) {
    if (!kv_ptr) return;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    std::lock_guard<std::mutex> lock(kv->mtx);
    // Decoded-cache (typed) freigeben
    kv_decoded_clear_all(kv);
    if (kv->persistent) {
        // 1) Mapping auf das tatsaechliche WAL-Ende schrumpfen → virt
        //    address space wird zurueckgegeben, apparent file size sinkt.
        // 2) Auf der verbleibenden Region madvise(MADV_DONTNEED) → RSS faellt.
        // Index bleibt intakt; Reads paginate die Daten on-demand zurueck.
        long page = sysconf(_SC_PAGESIZE);
        if (page <= 0) page = 4096;
        if (kv->wal_mmap && kv->wal_used > 0) {
            size_t aligned_used = ((size_t)kv->wal_used + (size_t)page - 1)
                                  & ~((size_t)page - 1);
            // Mindestens header-grosse Page beibehalten
            if (aligned_used < (size_t)page) aligned_used = (size_t)page;
            if (aligned_used < kv->wal_mmap_size) {
                void* p = mremap(kv->wal_mmap, kv->wal_mmap_size,
                                 aligned_used, 0);
                if (p != MAP_FAILED) {
                    kv->wal_mmap      = p;
                    kv->wal_mmap_size = aligned_used;
                    // ftruncate gibt apparent file size zurueck — Antigravity-friendly
                    ftruncate(kv->wal_fd, (off_t)aligned_used);
                }
                // mremap-shrink Fehler ignorieren — Daten bleiben gueltig,
                // nur der Footprint bleibt im current state
            }
            (void)madvise(kv->wal_mmap, kv->wal_mmap_size, MADV_DONTNEED);
        }
    } else {
        // Non-persistent: strdup'd Strings im map freigeben + map leeren
        if (kv->val_size == 0 && !kv->blob_only) {
            for (auto& [k, v] : kv->map) {
                char* ptr = nullptr;
                memcpy(&ptr, v.data(), sizeof(char*));
                free(ptr);
            }
        }
        kv->index.clear();
        kv->map.clear();
    }
    // NOTE: WAL fd, mmap, Datei und (fuer persistent) der Index bleiben intakt
}


// ─────────────────────────────────────────────────────────────────────────────
// Iterator implementation
// ─────────────────────────────────────────────────────────────────────────────

struct AimlIterator {
    int type;               // 0 = kv_keys (future: 1=list, 2=store, 3=map)
    // KV live iterator — no copy, points directly into either index (persistent)
    // or map (non-persistent). Caller must NOT modify the KV during iteration.
    AimlKv* kv;
    bool persistent_path;   // true → use idx_cur; false → use map_cur
    std::unordered_map<std::string, AimlKvValueLoc>::iterator idx_cur;
    std::unordered_map<std::string, AimlKvValueLoc>::iterator idx_end;
    std::unordered_map<std::string, std::vector<uint8_t>>::iterator map_cur;
    std::unordered_map<std::string, std::vector<uint8_t>>::iterator map_end;
};

extern "C" void* aiml_iter_create_kv(void* kv_ptr) {
    if (!kv_ptr) return nullptr;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    std::lock_guard<std::mutex> lock(kv->mtx);
    auto* it = new AimlIterator();
    it->type = 0;
    it->kv   = kv;
    it->persistent_path = kv->persistent;
    if (it->persistent_path) {
        it->idx_cur = kv->index.begin();
        it->idx_end = kv->index.end();
    } else {
        it->map_cur = kv->map.begin();
        it->map_end = kv->map.end();
    }
    return it;
}

extern "C" const char* aiml_iter_next(void* iter_ptr) {
    if (!iter_ptr) return "";
    AimlIterator* it = static_cast<AimlIterator*>(iter_ptr);
    if (it->persistent_path) {
        if (it->idx_cur == it->idx_end) return "";
        const char* key = it->idx_cur->first.c_str();
        ++it->idx_cur;
        return key;
    }
    if (it->map_cur == it->map_end) return "";
    const char* key = it->map_cur->first.c_str();
    ++it->map_cur;
    return key;
}

extern "C" int32_t aiml_iter_has_next(void* iter_ptr) {
    if (!iter_ptr) return 0;
    AimlIterator* it = static_cast<AimlIterator*>(iter_ptr);
    if (it->persistent_path) return (it->idx_cur != it->idx_end) ? 1 : 0;
    return (it->map_cur != it->map_end) ? 1 : 0;
}

extern "C" void aiml_iter_reset(void* iter_ptr) {
    if (!iter_ptr) return;
    AimlIterator* it = static_cast<AimlIterator*>(iter_ptr);
    if (it->persistent_path) {
        it->idx_cur = it->kv->index.begin();
        it->idx_end = it->kv->index.end();
    } else {
        it->map_cur = it->kv->map.begin();
        it->map_end = it->kv->map.end();
    }
}

extern "C" void aiml_iter_free(void* iter_ptr) {
    if (!iter_ptr) return;
    delete static_cast<AimlIterator*>(iter_ptr);
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

// WAL format per record: [id_len:u32][id:N bytes][dims * sizeof(float) bytes]
static void vec_wal_write_insert(AimlVecStore* vs, const char* id, const float* emb) {
    if (vs->wal_fd < 0) return;
    uint32_t id_len = (uint32_t)strlen(id);
    write(vs->wal_fd, &id_len, 4);
    write(vs->wal_fd, id, id_len);
    write(vs->wal_fd, emb, (size_t)vs->dims * sizeof(float));
}

static void vec_wal_replay(AimlVecStore* vs) {
    if (vs->wal_fd < 0) return;
    lseek(vs->wal_fd, 0, SEEK_SET);
    size_t emb_bytes = (size_t)vs->dims * sizeof(float);
    std::vector<float> buf((size_t)vs->dims);
    uint32_t id_len;
    while (read(vs->wal_fd, &id_len, 4) == 4) {
        if (id_len > 1024 * 1024) break; // sanity limit
        std::string id(id_len, '\0');
        if (id_len > 0 && (uint32_t)read(vs->wal_fd, id.data(), id_len) != id_len) break;
        if (read(vs->wal_fd, buf.data(), emb_bytes) != (ssize_t)emb_bytes) break;
        vs->ids.push_back(std::move(id));
        vs->embeddings.insert(vs->embeddings.end(), buf.begin(), buf.end());
        float norm = 0.0f;
        for (int32_t d = 0; d < vs->dims; d++) norm += buf[d] * buf[d];
        vs->norms.push_back(sqrtf(norm));
    }
}

extern "C" void* aiml_db_vec_open_persist(const char* name, int32_t dims,
                                            const char* base_path) {
    if (dims <= 0) return nullptr;
    std::string dir = resolve_aiondb_dir(base_path);
    std::string nm = name ? name : "vec";
    std::string registry_key = dir + nm;

    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto it = g_persistent_vecs_by_name.find(registry_key);
    if (it != g_persistent_vecs_by_name.end()) return it->second;

    mkdir(dir.c_str(), 0755);
    AimlVecStore* vs = new AimlVecStore();
    vs->dims       = dims;
    vs->persistent = true;
    vs->wal_path   = dir + nm + ".vecwal";
    int fd = open(vs->wal_path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd >= 0) {
        vs->wal_fd = fd;
        vec_wal_replay(vs);
        lseek(fd, 0, SEEK_END); // append mode
    }
    g_persistent_vecs_by_name[registry_key] = vs;
    g_persistent_vecs.push_back(vs);
    return vs;
}

extern "C" int32_t aiml_db_vec_checkpoint(void* vs_ptr) {
    if (!vs_ptr) return -1;
    AimlVecStore* vs = static_cast<AimlVecStore*>(vs_ptr);
    std::lock_guard<std::mutex> lock(vs->mtx);
    if (!vs->persistent || vs->wal_fd < 0) return 0;
    // Compact: truncate + rewrite all entries
    ftruncate(vs->wal_fd, 0);
    lseek(vs->wal_fd, 0, SEEK_SET);
    for (size_t i = 0; i < vs->ids.size(); i++) {
        uint32_t id_len = (uint32_t)vs->ids[i].size();
        write(vs->wal_fd, &id_len, 4);
        write(vs->wal_fd, vs->ids[i].data(), id_len);
        write(vs->wal_fd, vs->embeddings.data() + (size_t)i * vs->dims, (size_t)vs->dims * sizeof(float));
    }
    fsync(vs->wal_fd);
    return 0;
}

extern "C" void aiml_db_vec_insert(void* vs_ptr, const char* id, const float* emb, int32_t dims) {
    if (!vs_ptr || !id || !emb) return;
    AimlVecStore* vs = static_cast<AimlVecStore*>(vs_ptr);
    std::lock_guard<std::mutex> lock(vs->mtx);
    vs->ids.push_back(id);
    vs->embeddings.insert(vs->embeddings.end(), emb, emb + dims);
    float norm = 0.0f;
    for (int32_t d = 0; d < dims; d++) norm += emb[d] * emb[d];
    vs->norms.push_back(sqrtf(norm));
    if (vs->persistent) vec_wal_write_insert(vs, id, emb);
}

// dot-only similarity helper — norms are precomputed
static float cosine_sim_prenorm(const float* a, float norm_a,
                                const float* b, float norm_b,
                                int32_t dims) {
    float dot = 0.0f;
    for (int32_t i = 0; i < dims; i++) dot += a[i] * b[i];
    float denom = norm_a * norm_b;
    return (denom < 1e-10f) ? 0.0f : dot / denom;
}

extern "C" void aiml_db_vec_search(void* vs_ptr, const float* query, int32_t k, void* result_list) {
    if (!vs_ptr || !query || !result_list) return;
    AimlVecStore* vs   = static_cast<AimlVecStore*>(vs_ptr);
    AimlListRaw*  list = static_cast<AimlListRaw*>(result_list);

    std::lock_guard<std::mutex> lock(vs->mtx);

    int32_t n = (int32_t)vs->ids.size();
    if (n == 0 || k <= 0) return;

    // Compute query norm once
    float qnorm = 0.0f;
    for (int32_t d = 0; d < vs->dims; d++) qnorm += query[d] * query[d];
    qnorm = sqrtf(qnorm);

    std::vector<std::pair<float, int32_t>> scores(n);
    for (int32_t i = 0; i < n; i++) {
        const float* emb = vs->embeddings.data() + (size_t)i * vs->dims;
        scores[i] = { cosine_sim_prenorm(query, qnorm, emb, vs->norms[i], vs->dims), i };
    }

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
        // strdup instead of c_str() — result must not dangle if the VecStore reallocs
        r->id    = strdup(vs->ids[scores[i].second].c_str());
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
    std::string dir = resolve_aiondb_dir(base_path);
    std::string nm  = name ? name : "ts";
    std::string registry_key = dir + nm;

    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto it = g_persistent_ts_by_name.find(registry_key);
    if (it != g_persistent_ts_by_name.end()) return it->second;

    mkdir(dir.c_str(), 0755);
    AimlTimeSeries* ts = new AimlTimeSeries();
    ts->elem_size   = elem_size;
    ts->persistent  = true;
    ts->log_path    = dir + nm + ".tslog";
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
    g_persistent_ts_by_name[registry_key] = ts;
    g_persistent_ts.push_back(ts);
    return ts;
}

extern "C" int32_t aiml_db_ts_checkpoint(void* ts_ptr) {
    if (!ts_ptr) return -1;
    AimlTimeSeries* ts = static_cast<AimlTimeSeries*>(ts_ptr);
    std::lock_guard<std::mutex> lock(ts->mtx);
    if (!ts->persistent || ts->log_fd < 0) return 0;
    fsync(ts->log_fd);
    return 0;
}

extern "C" void aiml_db_ts_append(void* ts_ptr, const void* elem,
                                   int64_t elem_size, int64_t timestamp) {
    if (!ts_ptr || !elem) return;
    AimlTimeSeries* ts = static_cast<AimlTimeSeries*>(ts_ptr);
    std::lock_guard<std::mutex> lock(ts->mtx);
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
    std::lock_guard<std::mutex> lock(ts->mtx);
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
    std::lock_guard<std::mutex> lock(ts->mtx);
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
    AimlTimeSeries* ts = static_cast<AimlTimeSeries*>(ts_ptr);
    std::lock_guard<std::mutex> lock(ts->mtx);
    return ts->count;
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
        if (s->typed_persistent && s->ser_nodes) {
            if (idx >= (int32_t)s->mem_active.size()) return 0;
            return s->mem_active[(size_t)idx] ? 1 : 0;
        }
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

// ─────────────────────────────────────────────────────────────────────────────
// Secondary Index (db_indexed modifier, spec/45_db_indexed_fields.md)
// ─────────────────────────────────────────────────────────────────────────────
//
// Indexes are pure in-memory state in V1.  Each (parent_kv, field_name) pair
// owns one AimlIndex; the registry is global so that findings_by/finding_by
// can resolve the handle from just the parent_kv variable and a field literal.
//
// For unique:true the index is a flat map<field_val, kv_key>; otherwise it's
// a 1:N map<field_val, vector<kv_key>> with set semantics (idempotent add,
// no duplicate kv_keys per field_val — see backlog Edge-Cases).

struct AimlIndex {
    std::mutex mtx;
    bool unique = false;
    std::string mode;  // "" | "lower"
    std::unordered_map<std::string, std::vector<std::string>> map_1n;
    std::unordered_map<std::string, std::string>              map_unique;
};

namespace {
struct IndexKey {
    void* parent_kv;
    std::string field_name;
    bool operator==(const IndexKey& o) const noexcept {
        return parent_kv == o.parent_kv && field_name == o.field_name;
    }
};
struct IndexKeyHash {
    size_t operator()(const IndexKey& k) const noexcept {
        return std::hash<void*>{}(k.parent_kv) ^
               (std::hash<std::string>{}(k.field_name) << 1);
    }
};
std::unordered_map<IndexKey, AimlIndex*, IndexKeyHash> g_index_registry;
std::mutex g_index_registry_mtx;

std::string idx_normalize(const std::string& v, const std::string& mode) {
    if (mode != "lower") return v;
    std::string r = v;
    for (char& c : r) c = (char)std::tolower((unsigned char)c);
    return r;
}
}  // namespace

extern "C" void* aiml_db_index_open_for_kv(void* parent_kv, const char* field_name,
                                            int32_t unique, const char* mode) {
    if (!parent_kv || !field_name) return nullptr;
    std::lock_guard<std::mutex> lk(g_index_registry_mtx);
    IndexKey k{parent_kv, field_name};
    auto it = g_index_registry.find(k);
    if (it != g_index_registry.end()) return it->second;
    auto* idx = new AimlIndex();
    idx->unique = (unique != 0);
    idx->mode   = mode ? mode : "";
    g_index_registry[k] = idx;
    return idx;
}

extern "C" void* aiml_db_index_lookup(void* parent_kv, const char* field_name) {
    if (!parent_kv || !field_name) return nullptr;
    std::lock_guard<std::mutex> lk(g_index_registry_mtx);
    auto it = g_index_registry.find({parent_kv, field_name});
    return it == g_index_registry.end() ? nullptr : (void*)it->second;
}

extern "C" void aiml_db_index_add(void* idx_ptr, const char* field_val, const char* kv_key) {
    if (!idx_ptr || !field_val || !kv_key) return;
    auto* idx = (AimlIndex*)idx_ptr;
    std::lock_guard<std::mutex> lk(idx->mtx);
    std::string k = idx_normalize(field_val, idx->mode);
    if (idx->unique) {
        idx->map_unique[k] = kv_key;
    } else {
        auto& lst = idx->map_1n[k];
        if (std::find(lst.begin(), lst.end(), kv_key) == lst.end()) {
            lst.emplace_back(kv_key);
        }
    }
}

extern "C" void aiml_db_index_remove(void* idx_ptr, const char* field_val, const char* kv_key) {
    if (!idx_ptr || !field_val || !kv_key) return;
    auto* idx = (AimlIndex*)idx_ptr;
    std::lock_guard<std::mutex> lk(idx->mtx);
    std::string k = idx_normalize(field_val, idx->mode);
    if (idx->unique) {
        auto it = idx->map_unique.find(k);
        if (it != idx->map_unique.end() && it->second == kv_key) {
            idx->map_unique.erase(it);
        }
    } else {
        auto it = idx->map_1n.find(k);
        if (it == idx->map_1n.end()) return;
        auto& lst = it->second;
        lst.erase(std::remove(lst.begin(), lst.end(), kv_key), lst.end());
        if (lst.empty()) idx->map_1n.erase(it);
    }
}

extern "C" void aiml_db_index_findings(void* idx_ptr, const char* field_val, void* out_list_v) {
    auto* out = (AimlListRaw*)out_list_v;
    if (!out) return;
    out->size = 0;
    if (!idx_ptr || !field_val) return;
    auto* idx = (AimlIndex*)idx_ptr;
    std::lock_guard<std::mutex> lk(idx->mtx);
    std::string k = idx_normalize(field_val, idx->mode);

    std::vector<std::string> hits;
    if (idx->unique) {
        auto it = idx->map_unique.find(k);
        if (it != idx->map_unique.end()) hits.push_back(it->second);
    } else {
        auto it = idx->map_1n.find(k);
        if (it != idx->map_1n.end()) hits = it->second;
    }
    int32_t n = (int32_t)hits.size();
    if (n == 0) return;
    if (!out->data || out->capacity < n) {
        free(out->data);
        out->data     = (void**)malloc((size_t)n * sizeof(void*));
        out->capacity = n;
    }
    for (int32_t i = 0; i < n; i++) {
        // strdup so the returned char* survives later index mutations.
        out->data[i] = strdup(hits[(size_t)i].c_str());
    }
    out->size = n;
}

// Field-type tags used by aiml_db_index_backfill.
// 0=string (char*), 1=i32, 2=i64, 3=list<string>.
enum AimlIndexFieldTag : int32_t {
    AIML_IDX_FT_STRING    = 0,
    AIML_IDX_FT_I32       = 1,
    AIML_IDX_FT_I64       = 2,
    AIML_IDX_FT_LIST_STR  = 3,
};

// Walks the main Kv (`kv`), extracting `field_name` from each decoded record
// at byte offset `field_offset` and populating the index.  Used at open time
// when the index is empty but the Kv already has entries (cross-process reopen,
// or first-time enabling of `db_indexed:` on an existing model).
extern "C" void aiml_db_index_backfill(void* kv_ptr, void* idx_ptr,
                                        int32_t field_offset, int32_t field_tag) {
    SerGuardOn _sg_;
    if (!kv_ptr || !idx_ptr) return;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    auto* idx  = (AimlIndex*)idx_ptr;

    auto extract_and_add = [&](const std::string& key, const uint8_t* row) {
        const uint8_t* field = row + field_offset;
        switch (field_tag) {
            case AIML_IDX_FT_STRING: {
                const char* p = *(const char* const*)field;
                if (!p) return;
                aiml_db_index_add(idx, p, key.c_str());
                break;
            }
            case AIML_IDX_FT_I32: {
                int32_t v = *(const int32_t*)field;
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d", v);
                aiml_db_index_add(idx, buf, key.c_str());
                break;
            }
            case AIML_IDX_FT_I64: {
                int64_t v = *(const int64_t*)field;
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%lld", (long long)v);
                aiml_db_index_add(idx, buf, key.c_str());
                break;
            }
            case AIML_IDX_FT_LIST_STR: {
                // List layout: { i32 size, i32 capacity, char** data }
                int32_t size = *(const int32_t*)field;
                char* const* data = *(char* const* const*)(field + 8);
                if (!data || size <= 0) return;
                for (int32_t i = 0; i < size; i++) {
                    if (data[i]) aiml_db_index_add(idx, data[i], key.c_str());
                }
                break;
            }
        }
    };

    std::lock_guard<std::mutex> lock(kv->mtx);
    if (kv->persistent) {
        for (auto& [key, loc] : kv->index) {
            const uint8_t* val_ptr = (const uint8_t*)kv->wal_mmap + loc.offset;
            if (kv->ser_nodes) {
                // Decoded form may live in cache; fall through to decode if missing.
                auto dc = kv->decoded_cache.find(key);
                if (dc == kv->decoded_cache.end()) {
                    std::vector<uint8_t> decoded((size_t)kv->val_size, 0);
                    aiml_ser_decode((const char*)val_ptr, decoded.data(),
                                    kv->ser_nodes, kv->ser_n);
                    auto& cached = kv->decoded_cache[key];
                    cached = std::move(decoded);
                    dc = kv->decoded_cache.find(key);
                }
                extract_and_add(key, dc->second.data());
            } else {
                extract_and_add(key, val_ptr);
            }
        }
    } else {
        for (auto& [key, bytes] : kv->map) {
            extract_and_add(key, bytes.data());
        }
    }
}

extern "C" const char* aiml_db_index_finding(void* idx_ptr, const char* field_val) {
    static const char* empty = "";
    if (!idx_ptr || !field_val) return empty;
    auto* idx = (AimlIndex*)idx_ptr;
    std::lock_guard<std::mutex> lk(idx->mtx);
    std::string k = idx_normalize(field_val, idx->mode);
    if (idx->unique) {
        auto it = idx->map_unique.find(k);
        if (it != idx->map_unique.end()) return it->second.c_str();
    } else {
        auto it = idx->map_1n.find(k);
        if (it != idx->map_1n.end() && !it->second.empty()) return it->second.front().c_str();
    }
    return empty;
}
