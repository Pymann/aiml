#include "db_runtime.h"
#include "string_runtime.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
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

// spec/61 §4: aiml-managed string dup (allocates an AimlStrHeader + rc so the string can be
// decref'd). A list<string> element MUST be aiml-managed — libc strdup has no rc header, so the
// list's teardown (aiml_str_decref) would read garbage 4 bytes before the pointer (invalid access).
extern "C" char* aiml_str_dup(const char* s);

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

// ── Index dump (spec/31 §6.2.1) ─────────────────────────────────────────────
// Otherwise the index only lives in the process and is built from the WAL on every
// open.  Measured on the cai knowledge graph (18.17 million keys): 19.5 GB
// RSS and ~5 min per process start, with 713 MB of key bytes -- 434 bytes per
// key, almost all of it unordered_map overhead.  The dump puts the same
// information sorted onto the disk and is USED from there, not loaded.
struct AimlKvIdxHeader {
    char     magic[8];      // "AIMLKVIX"
    uint32_t version;
    uint32_t wal_version;   // version of the associated WAL
    int64_t  val_size;      // val_size of the associated WAL
    uint64_t wal_used;      // high-water mark: state of the WAL when written
    uint64_t wal_dev;       // st_dev of the WAL when written
    uint64_t wal_ino;       // st_ino of the WAL when written
    uint64_t wal_fp;        // Fingerabdruck des beschriebenen Bereichs
    uint64_t count;         // number of entries
};
static_assert(sizeof(AimlKvIdxHeader) == 64, "AimlKvIdxHeader must be 64 bytes");

// 24 bytes, naturally aligned -- no pack(1), so that no unaligned
// 64-bit access arises.  Entries are sorted by key bytes,
// which is why the lookup is a binary search in the mmap without any build-up.
struct AimlKvIdxEntry {
    uint64_t val_off;       // offset of the value in the WAL
    uint32_t key_off;       // Offset im Schluesselblock
    uint32_t val_len;
    uint32_t key_len;
    uint32_t pad;
};
static_assert(sizeof(AimlKvIdxEntry) == 24, "AimlKvIdxEntry must be 24 bytes");

static const char     KV_IDX_MAGIC[8] = {'A','I','M','L','K','V','I','X'};
static const uint32_t KV_IDX_VERSION  = 1;
// Tombstone in the overflow index: hides a dump entry.  Without it a
// deleted key would come back from the dump after a restart.
static const uint64_t KV_IDX_TOMB     = UINT64_MAX;

static const char     KV_WAL_MAGIC[8] = {'A','I','M','L','K','V','W','L'};
// Version 2 (step 4): strings in the WAL now have a null terminator
// (val_bytes = strlen+1). Makes zero-copy reads possible.
static const uint32_t KV_WAL_VERSION  = 2;
static const uint32_t KV_WAL_FLAG_TYPED = 1u << 0;

// Initial sparse reserve for every Kv WAL file. The kernel only allocates
// pages that are actually written, but the `apparent size` of the file
// (what `ls -la`, file watchers, indexers and non-sparse-aware tools see)
// = the reserve chosen here.
//
// 16 MB is small enough that a test `/tmp` does not topple an IDE file watcher
// (~22 tests × 16 MB = 352 MB apparent), and large enough that small
// stores get by without mremap growth. On append overflow
// `kv_mmap_grow` doubles the reserve via `ftruncate` + `mremap(MREMAP_MAYMOVE)`.
static constexpr size_t KV_MMAP_RESERVE_INITIAL = (size_t)16 * 1024 * 1024;

// Maximum number of typed entries decoded at the same time in the cache.
// A bounded LRU (step 7) prevents unbounded growth of
// `decoded_cache`. Pointers from `kv.get` become invalid when their entry
// drops out of the cache — documented in spec/migration/aiondb_kv_mmap_refactor.md.
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

// Position+length in the mmap'd WAL — point-into-mmap instead of copy.
struct AimlKvValueLoc {
    uint64_t offset;   // byte offset in the mmap (points directly at the val bytes)
    uint32_t length;   // val_bytes
};

struct AimlKv {
    std::mutex  mtx;  // per-KV lock for thread-safe access
    int64_t val_size;
    bool    blob_only = false;  // true when only kv_set_blob was used (no strdup entries)
    // ── Index (step 3): source of truth for PERSISTENT KVs ────────────────────
    // key → {offset, length} in the mmap. Reads return pointers into the mmap.
    std::unordered_map<std::string, AimlKvValueLoc> index;
    // ── Storage for NON-PERSISTENT KVs (legacy heap map) ────────────────────
    // To be migrated to an anonymous mmap buffer in a later phase.
    std::unordered_map<std::string, std::vector<uint8_t>> map;
    // Typed Kv: descriptor for recursive serialization of heap fields
    const AimlTypeNode* ser_nodes = nullptr;
    int32_t             ser_n     = 0;
    // Decoded cache: key → decoded struct (live heap pointers, freed on eviction).
    // Bounded LRU (step 7): `decoded_lru` carries keys in MRU order
    // (front = MRU, back = oldest). `decoded_lru_pos` maps key → list iterator
    // for O(1) splice. Eviction on cache insert when size > MAX_DECODED_LRU.
    std::unordered_map<std::string, std::vector<uint8_t>> decoded_cache;
    std::list<std::string>                                decoded_lru;
    std::unordered_map<std::string, std::list<std::string>::iterator> decoded_lru_pos;
    // Persistence (WAL)
    bool        persistent = false;
    int         wal_fd     = -1;
    std::string wal_path;
    // ── mmap backing (step 2) ───────────────────────────────────────────────
    void*       wal_mmap      = nullptr;   // base pointer (initial KV_MMAP_RESERVE_INITIAL)
    size_t      wal_mmap_size = 0;         // grow via kv_mmap_grow
    uint64_t    wal_used      = 0;         // naechster freier Offset im WAL
    // ── Insight for the operations view (cosmos doc/spec/99 F99-5) ───────────
    // Name and a WRITE COUNTER.  Deliberately a counter and not a timestamp:
    // a timestamp costs a clock access on the hot path for every write,
    // while the view compares two snapshots anyway — the
    // difference says "it is moving", the snapshot time says "how old".
    std::string store_name;
    uint64_t    writes = 0;
    // ── Index-Dump (spec/31 §6.2.1) ────────────────────────────────────────
    bool        index_dump = false;      // option at open
    std::string idx_path;                // <name>.kvidx
    void*       idx_mmap      = nullptr; // mapped read-only
    size_t      idx_mmap_size = 0;
    const AimlKvIdxEntry* idx_entries = nullptr;
    const char*           idx_keys    = nullptr;
    uint64_t              idx_count   = 0;
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
// AionDB path configuration (db.set_path / AIONDB_PATH env var)
// ──────────────────────────────────────────────────────────────────────────
//
// Resolution order for the storage root:
//   1. AIONDB_PATH env var (always dominates, re-read on every open)
//   2. g_aiondb_root (set by aiml_db_set_path)
//   3. fallback "./aiondb/"
//
// Sub-path argument (`base_path` of the open functions):
//   - nullptr/empty   → use the root only
//   - absolute (/...) → as-is (backward compat for tests with /tmp/aiml_test_*)
//   - relative        → root + sub_path
static std::mutex   g_aiondb_path_mutex;
static std::string  g_aiondb_root;  // empty == not set via aiml_db_set_path

// Helper: makes sure that the path ends with '/'.
static void path_ensure_trailing_slash(std::string& s) {
    if (!s.empty() && s.back() != '/') s += '/';
}

// `mkdir -p` for the resolved store path.
//
// A single-level `mkdir` is not enough: spec/31 explicitly allows `path: "sub/dir/"`
// (example there: `runs/trajectory.kvwal`), and such a
// substructure does not exist yet on the first open.  The single-level mkdir
// then failed with ENOENT, the open path carried on, and the store stayed
// silently empty — found 2026-10-15 on cosmos' `cas_action_test` under a
// fresh AIONDB_PATH.
static void db_mkdir_p(const std::string& dir) {
    if (dir.empty()) return;
    std::string acc;
    acc.reserve(dir.size());
    if (dir[0] == '/') acc = "/";
    size_t i = (dir[0] == '/') ? 1 : 0;
    while (i < dir.size()) {
        size_t sl = dir.find('/', i);
        std::string part = dir.substr(i, (sl == std::string::npos ? dir.size() : sl) - i);
        if (!part.empty()) {
            if (!acc.empty() && acc.back() != '/') acc += "/";
            acc += part;
            mkdir(acc.c_str(), 0755);   // already exists → EEXIST, does not matter
        }
        if (sl == std::string::npos) break;
        i = sl + 1;
    }
}

// Resolve the AionDB directory for an open call.
// The caller gets a path with a trailing slash.
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

    // 2. Apply the sub-path
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

// Sets the global AionDB root. Called by db.set_path("/path").
// The value is ignored if the AIONDB_PATH env var is set (env dominates).
extern "C" void aiml_db_set_path(const char* path) {
    std::lock_guard<std::mutex> lock(g_aiondb_path_mutex);
    if (!path || !*path) {
        g_aiondb_root.clear();
        return;
    }
    g_aiondb_root = path;
}

// Returns the currently resolved root (with a trailing slash).
// Single source of truth for AIML code that needs the path
// (e.g. StoreOrganisator for targeted deletion).
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
// spec/61 class 6: the runtime owns these singletons for the whole process — the registries are
// never destroyed (no exit-time destructor drops the only reference to a live handle).
static std::mutex                      g_db_mutex;
static std::vector<AimlStore*>&        g_persistent_stores = *new std::vector<AimlStore*>();
static std::vector<AimlKv*>&           g_persistent_kvs    = *new std::vector<AimlKv*>();
static std::vector<AimlTimeSeries*>&   g_persistent_ts     = *new std::vector<AimlTimeSeries*>();
static std::vector<AimlVecStore*>&     g_persistent_vecs   = *new std::vector<AimlVecStore*>();

// Global name-based registries: base_path+name → open handle (singleton reuse)
static std::unordered_map<std::string, AimlStore*>&      g_persistent_stores_by_name = *new std::unordered_map<std::string, AimlStore*>();
static std::unordered_map<std::string, AimlKv*>&         g_persistent_kvs_by_name    = *new std::unordered_map<std::string, AimlKv*>();
static std::unordered_map<std::string, AimlTimeSeries*>& g_persistent_ts_by_name     = *new std::unordered_map<std::string, AimlTimeSeries*>();
static std::unordered_map<std::string, AimlVecStore*>&   g_persistent_vecs_by_name   = *new std::unordered_map<std::string, AimlVecStore*>();
// In-memory (non-persistent) KV registry: name → singleton handle, shared across calls.
// Ensures db.open_kv("name", {persist: false}) returns the same instance on every call.
static std::unordered_map<std::string, AimlKv*>&         g_non_persist_kvs_by_name   = *new std::unordered_map<std::string, AimlKv*>();
// spec/47 §1, spec/61 §11.10: in-memory Store/VecStore/TimeSeries are Class-6 singletons too.
static std::unordered_map<std::string, AimlStore*>&      g_non_persist_stores_by_name = *new std::unordered_map<std::string, AimlStore*>();
static std::unordered_map<std::string, AimlVecStore*>&   g_non_persist_vecs_by_name   = *new std::unordered_map<std::string, AimlVecStore*>();
static std::unordered_map<std::string, AimlTimeSeries*>& g_non_persist_ts_by_name     = *new std::unordered_map<std::string, AimlTimeSeries*>();

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
    std::string nm = name ? name : "store";
    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto it = g_non_persist_stores_by_name.find(nm);
    if (it != g_non_persist_stores_by_name.end()) return it->second;
    AimlStore* s = new AimlStore();
    s->type_name = type_name ? type_name : "";
    s->elem_size = elem_size;
    g_non_persist_stores_by_name[nm] = s;
    return s;
}

// In-memory store for an element type WITH heap fields (strings/containers). The ser_nodes let
// insert co-own (retain) the element's heap fields so a caller reusing/mutating the source model
// after insert (e.g. `insert(u); set:[u.name,"..."]; insert(u)`) does not free the stored record's
// strings (spec/61 §7: aggregate copy = member-wise share). Without this the in-memory memcpy shared
// the caller's string pointers → dangling after the caller's next reassignment (test_model).
extern "C" void* aiml_db_store_open_typed(const char* name, const char* type_name,
                                          int64_t elem_size,
                                          const AimlTypeNode* nodes, int32_t n_nodes) {
    if (elem_size <= 0) return nullptr;
    std::string nm = name ? name : "store";
    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto it = g_non_persist_stores_by_name.find(nm);
    if (it != g_non_persist_stores_by_name.end()) return it->second;
    AimlStore* s = new AimlStore();
    s->type_name = type_name ? type_name : "";
    s->elem_size = elem_size;
    s->ser_nodes = nodes;   // in-memory (not persistent) → typed_persistent stays false
    s->ser_n = n_nodes;
    g_non_persist_stores_by_name[nm] = s;
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

    db_mkdir_p(bp);  // create dir (ignore if exists)

    s->db_fd = open(db_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
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

    s->wal_fd = open(wal_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
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

    db_mkdir_p(dir);
    AimlStore* s = new AimlStore();
    s->type_name = type_name ? type_name : "";
    s->elem_size = elem_size;
    s->persistent = true;
    s->typed_persistent = true;
    s->ser_nodes = nodes;
    s->ser_n = n_nodes;

    std::string wal_path = dir + nm + ".wal";
    s->wal_fd = open(wal_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
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
    std::string nm = name ? name : "store";
    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto found = g_non_persist_stores_by_name.find(nm);
    if (found != g_non_persist_stores_by_name.end()) return found->second;
    AimlStore* s = new AimlStore();
    g_non_persist_stores_by_name[nm] = s;
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
    // spec/61 §7: the memcpy shares the source's heap-field pointers (strings/containers). Retain
    // them so the store co-owns the referents — else a caller that reuses/mutates the source model
    // after insert frees strings the stored record still points at (dangling → test_model). Needs
    // the element's ser_nodes (in-memory typed store via aiml_db_store_open_typed).
    if (s->ser_nodes)
        aiml_ser_retain(s->mem_data.data() + (size_t)s->total * s->elem_size, s->ser_nodes, s->ser_n);
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

// spec/31 §6.10: the typing belongs to the store, not to whoever opens it.
//
// `db.open_kv` keeps a registry per (directory, name) and returns the same
// handle on a second open -- with the val_size and descriptor of the
// FIRST open.  If the second open requests a different value type, `kv.get`
// keeps decoding with the old descriptor, while the call site reads the record as
// its own declared type: `has` says 1, the record looks plausible,
// and the first string pointer that lands on non-pointer bytes dies when
// passed on (aiml_ser_encode <- aiml_ser_deep_copy).  The fingerprint that protects every
// blob cannot see this -- blob and handle match, only the
// reader does not.  Hence the guard here, at the only point that sees both types.
//
// Found on 2026-11-11 in datacreatures (an aborted repository pilot: a Kv
// as a class member read a record with a foreign layout).  AIML_DB_ALLOW_RETYPE=1
// turns this into a warning -- for tools that deliberately read a store with
// a different layout (migration, export).
static void kv_guard_reopen(AimlKv* kv, const std::string& nm, int64_t val_size,
                            const AimlTypeNode* nodes, int32_t n_nodes) {
    if (!kv) return;
    const bool want_typed = (nodes != nullptr && n_nodes > 0);
    const bool have_typed = (kv->ser_nodes != nullptr && kv->ser_n > 0);
    const uint32_t want_fp = aiml_ser_schema_fingerprint(nodes, n_nodes);
    const uint32_t have_fp = aiml_ser_schema_fingerprint(kv->ser_nodes, kv->ser_n);
    if (want_typed == have_typed && val_size == kv->val_size && want_fp == have_fp) return;

    const char* env = getenv("AIML_DB_ALLOW_RETYPE");
    const bool erlaubt = env && *env && strcmp(env, "0") != 0;
    fprintf(stderr,
        "[AionDB] %s: KV store '%s' was first opened as %s (val_size=%lld, schema_fp=0x%08x, "
        "n_nodes=%d) and is now opened as %s (val_size=%lld, schema_fp=0x%08x, n_nodes=%d).\n"
        "[AionDB] %s: a store's value type is fixed by its FIRST open — every further open must "
        "request the same Kv<K:V> (spec/31 §6.10).  The second opener would read this store's "
        "records as a foreign layout: no compiler error, no further warning, and a crash in "
        "aiml_ser_encode as soon as the record is passed on.\n"
        "[AionDB] %s: check the store name against the declared value type at both open sites "
        "(a class member splits them: type in the class body, name in the constructor).  "
        "Set AIML_DB_ALLOW_RETYPE=1 to downgrade this to a warning.\n",
        erlaubt ? "WARN" : "FATAL", nm.c_str(),
        have_typed ? "typed" : "raw",   (long long)kv->val_size, have_fp, kv->ser_n,
        want_typed ? "typed" : "raw",   (long long)val_size,     want_fp, n_nodes,
        erlaubt ? "WARN" : "FATAL", erlaubt ? "WARN" : "FATAL");
    if (!erlaubt) _exit(1);
}

extern "C" void* aiml_db_kv_open(const char* name, int64_t val_size) {
    std::string nm = name ? name : "kv";
    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto it = g_non_persist_kvs_by_name.find(nm);
    if (it != g_non_persist_kvs_by_name.end()) {
        kv_guard_reopen(it->second, nm, val_size, nullptr, 0);   // spec/31 §6.10
        return it->second;
    }
    AimlKv* kv = new AimlKv();
    kv->val_size = val_size;
    g_non_persist_kvs_by_name[nm] = kv;
    return kv;
}

// ── Kv WAL helpers ────────────────────────────────────────────────────────────

static const uint8_t KV_WAL_SET      = 1;
static const uint8_t KV_WAL_DELETE   = 2;
static const uint8_t KV_WAL_SET_BLOB = 3;  // raw binary, no strdup on replay

// ── mmap backing (step 2) ───────────────────────────────────────────────────
// Offers zero-copy append into a sparse-extended WAL file. All writers
// hold kv->mtx, therefore `wal_used` is race-free.

// Sets up the mmap backing for a freshly opened kv->wal_fd.
// The file is sparse-extended to a suitable reserve, then mapped.
// If fstat() already shows a larger file (existing, grown
// store) the existing size is kept — otherwise INITIAL.
// `wal_used` is taken over from the header (crash recovery: best effort).
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
    // Desired reserve: keep the existing file size (grown store) or
    // INITIAL for a fresh store. On ENOMEM we fall back in several steps,
    // so that we can still open even under virtual-memory pressure.
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
        // Too small compared with the existing file content: do not try,
        // otherwise there is SIGBUS on pages beyond the mmap end.
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
        // mmap failed: reset the file size and try the next candidate
        if ((size_t)st.st_size < reserve) {
            ftruncate(kv->wal_fd, st.st_size);
        }
    }
    fprintf(stderr,
        "[AionDB] FATAL: mmap failed for '%s' at all candidate sizes "
        "(target=%zu bytes; min=%zu bytes; errno=%d) — likely RLIMIT_AS or "
        "virt-fragmentation. Aborting to avoid silent data loss.\n",
        kv->wal_path.c_str(), target, hdr_aligned, errno);
    _exit(1);  // strict: no silent skip — the caller would otherwise ignore nullptr
    return false;  // unreachable
}

// Grow via ftruncate + mremap(MREMAP_MAYMOVE).
// First tries doubling; on ENOMEM (typically due to
// virtual address space fragmentation, RLIMIT_AS or vm.max_map_count)
// degrades in several steps: 2x → +50% → +25% → +64 MB → +16 MB.
// The mapping pointer can change → write helpers re-read `kv->wal_mmap`
// after every append, index offsets are file-relative.
static bool kv_mmap_grow(AimlKv* kv, size_t need_size) {
    if (!kv->wal_mmap) return false;
    size_t cur = kv->wal_mmap_size;
    if (need_size <= cur) return true;

    // Candidate sizes, from the most optimistic to the most conservative step.
    size_t candidates[] = {
        cur * 2,
        cur + cur / 2,
        cur + cur / 4,
        cur + (size_t)64 * 1024 * 1024,
        cur + (size_t)16 * 1024 * 1024,
        cur + (size_t)4  * 1024 * 1024,
    };
    for (size_t cand : candidates) {
        if (cand < need_size) continue;  // must cover at least the target
        if (cand <= cur) continue;       // must grow strictly
        if (ftruncate(kv->wal_fd, (off_t)cand) != 0) continue;
        void* p = mremap(kv->wal_mmap, kv->wal_mmap_size, cand, MREMAP_MAYMOVE);
        if (p == MAP_FAILED) {
            // reset the file size, try the next smaller candidate
            ftruncate(kv->wal_fd, (off_t)cur);
            continue;
        }
        kv->wal_mmap      = p;
        kv->wal_mmap_size = cand;
        return true;
    }
    // Last fallback: explicit munmap → ftruncate → mmap.
    // mremap(MREMAP_MAYMOVE) briefly needs DOUBLE the virtual memory
    // (old + new range at the same time). With extreme fragmentation
    // this fails. An explicit unmap-then-mmap cycle has no
    // dual-allocation moment — the kernel can place the new range freely.
    // The data is preserved by the file backing.
    for (size_t cand : candidates) {
        if (cand < need_size) continue;
        if (cand <= cur) continue;
        size_t old_size = kv->wal_mmap_size;
        munmap(kv->wal_mmap, old_size);
        kv->wal_mmap = nullptr;
        if (ftruncate(kv->wal_fd, (off_t)cand) != 0) {
            // Try to restore the old mapping
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

// Writes the current wal_used value into the header and syncs only the first page.
static void kv_mmap_persist_used(AimlKv* kv) {
    if (!kv->wal_mmap) return;
    AimlKvWalHeader* hdr = (AimlKvWalHeader*)kv->wal_mmap;
    hdr->wal_used = kv->wal_used;
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) page = 4096;
    msync(kv->wal_mmap, (size_t)page, MS_ASYNC);
}

// Releases the mmap (e.g. before a compaction rewrite or on kv_clear).
static void kv_mmap_detach(AimlKv* kv) {
    if (kv->wal_mmap) {
        munmap(kv->wal_mmap, kv->wal_mmap_size);
        kv->wal_mmap      = nullptr;
        kv->wal_mmap_size = 0;
    }
}

// Reserves n bytes in the mmap and copies src into it. The caller holds kv->mtx.
// On reserve overflow: kv_mmap_grow() doubles via ftruncate+mremap.
static bool kv_mmap_append(AimlKv* kv, const void* src, size_t n) {
    if (!kv->wal_mmap) return false;
    if (kv->wal_used + n > kv->wal_mmap_size) {
        if (!kv_mmap_grow(kv, kv->wal_used + n)) {
            // kv_mmap_grow has already logged FATAL — abort
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
//
// spec/31 §6.7.3 rule 1: the MRU entry stays — that is the slot that the
// running `get`/`get_copy` has just created and will return.  `size() > 1`
// instead of `!empty()`: if the cache once ran over the limit without the
// LRU list knowing about it, emptying the list NEVER brings the loop under the
// bound — it then took the whole list along, the just-inserted entry last,
// and the caller got a pointer into a freed slot (cai 2026-11-04:
// SIGSEGV in enc <- aiml_ser_deep_copy, strlen on a released string).
static void kv_decoded_lru_trim(AimlKv* kv) {
    while (kv->decoded_cache.size() > MAX_DECODED_LRU
           && kv->decoded_lru.size() > 1) {
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

static void kv_wal_replay(AimlKv* kv, size_t from = 0);  // forward decl

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
static bool kv_wal_check_header(int fd, const std::string& path,
                                bool want_typed, int64_t want_val_size) {
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
    // spec/31 §6.10: the header carries typing and val_size of the CREATING
    // open.  A deviation is reported, but NOT turned into an abort:
    // an appended field (§7 schema evolution) changes val_size legitimately, and
    // on read the blob fingerprint says what to do with old records.
    // A different value type, on the other hand, is an error -- kv_guard_reopen catches it
    // as soon as the same store is opened twice in the same process.
    const bool have_typed = (hdr.flags & KV_WAL_FLAG_TYPED) != 0;
    if (have_typed != want_typed || hdr.val_size != want_val_size) {
        fprintf(stderr,
            "[AionDB] WARN: KV file '%s' was created %s with val_size=%lld, now opened %s with "
            "val_size=%lld (spec/31 §6.10).  An appended field changes val_size legitimately; a "
            "different value TYPE does not -- check the declared Kv<K:V> against this store name.\n",
            path.c_str(), have_typed ? "typed" : "raw", (long long)hdr.val_size,
            want_typed ? "typed" : "raw", (long long)want_val_size);
    }
    return true;
}

// Open a KV WAL file and either initialize a fresh header (empty file) or
// validate an existing one. Returns fd on success, -1 if open() itself
// failed. On header-mismatch this function does not return — it _exit()s.
static int kv_wal_open_with_header(const std::string& path, bool typed,
                                    int64_t val_size) {
    int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return -1; }
    if (st.st_size == 0) {
        if (!kv_wal_init_header(fd, typed, val_size)) { close(fd); return -1; }
    } else {
        // spec/31 §6.10: check the header against what is requested (loud, not fatal).
        kv_wal_check_header(fd, path, typed, val_size);  // does not return on magic/version mismatch
    }
    return fd;
}

// (kv_lazy_reload removed in step 5: evict no longer clears the index, so
//  reads find offsets directly and the kernel re-faults pages on access.)

// Replay WAL records, starting right after the file header.
// Populates kv->index (offset+length) without copying values — all reads
// go through the mmap. For typed: decoded_cache is invalidated per key.
// Secondary index dumps (spec/45 "Persistence").  They live with the index code further
// down, but are called from up here -- checkpoint writes them, clear
// deletes them.  Both callers hold kv->mtx.
static void kv_sidx_write_all(AimlKv* kv);
static void kv_sidx_unlink_all(AimlKv* kv);

// ── Index-Dump: Lesen (spec/31 §6.2.1) ─────────────────────────────────────

// Lexicographic like std::string: common part, then the length.  Must
// match the sort order used when writing, otherwise the binary search finds
// nothing -- and does so silently.
static inline int kv_idx_cmp(const char* a, size_t alen, const char* b, size_t blen) {
    const size_t n = alen < blen ? alen : blen;
    const int c = n ? memcmp(a, b, n) : 0;
    if (c) return c;
    if (alen == blen) return 0;
    return alen < blen ? -1 : 1;
}

// Fingerprint over EXACTLY the range the dump describes.  Appending
// lies beyond it and does not change it -- only that way does the overflow path stay
// usable.  Deterministic and integer-only, so that writing and checking yield
// the same value on every machine.
static uint64_t kv_idx_fingerprint(const uint8_t* base, size_t start, size_t end) {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](const uint8_t* p, size_t n) {
        for (size_t i = 0; i < n; i++) { h ^= (uint64_t)p[i]; h *= 1099511628211ULL; }
    };
    if (end <= start) return h;
    const size_t len = end - start;
    const uint64_t seed = (uint64_t)len;
    mix((const uint8_t*)&seed, sizeof(seed));
    const size_t CH = 4096;
    if (len <= 64 * 1024) { mix(base + start, len); return h; }
    for (unsigned i = 0; i < 16; i++) {
        const size_t off = start + (size_t)(((uint64_t)i * (uint64_t)(len - CH)) / 15ULL);
        mix(base + off, CH);
    }
    return h;
}

static void kv_idx_detach(AimlKv* kv) {
    if (kv->idx_mmap) munmap(kv->idx_mmap, kv->idx_mmap_size);
    kv->idx_mmap      = nullptr;
    kv->idx_mmap_size = 0;
    kv->idx_entries   = nullptr;
    kv->idx_keys      = nullptr;
    kv->idx_count     = 0;
}

// Check and map the dump.  Returns the high-water mark from which the WAL still
// has to be replayed, or 0 if there is no usable dump.  The
// dump is NEVER authoritative over the WAL: every doubt costs a full
// replay, never wrong data.
static uint64_t kv_idx_try_load(AimlKv* kv) {
    kv_idx_detach(kv);
    if (!kv->index_dump || kv->idx_path.empty() || !kv->wal_mmap) return 0;
    int fd = open(kv->idx_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    struct stat st{};
    if (fstat(fd, &st) != 0 || (size_t)st.st_size < sizeof(AimlKvIdxHeader)) { close(fd); return 0; }
    void* m = mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return 0;
    const size_t msize = (size_t)st.st_size;
    const AimlKvIdxHeader* h = (const AimlKvIdxHeader*)m;
    auto verwerfen = [&](void) -> uint64_t { munmap(m, msize); return 0; };

    if (memcmp(h->magic, KV_IDX_MAGIC, 8) != 0)      return verwerfen();
    if (h->version != KV_IDX_VERSION)                return verwerfen();
    if (h->val_size != kv->val_size)                 return verwerfen();
    if (h->wal_used < sizeof(AimlKvWalHeader))       return verwerfen();
    if (h->wal_used > kv->wal_mmap_size)             return verwerfen();

    // Read the WAL version from its header.
    const AimlKvWalHeader* wh = (const AimlKvWalHeader*)kv->wal_mmap;
    if (h->wal_version != wh->version)               return verwerfen();

    // File identity: the checkpoint writes the WAL as a temp file and moves it
    // over with rename, so the inode changes exactly when
    // the offsets shift.
    struct stat ws{};
    if (fstat(kv->wal_fd, &ws) != 0)                 return verwerfen();
    if ((uint64_t)ws.st_dev != h->wal_dev)           return verwerfen();
    if ((uint64_t)ws.st_ino != h->wal_ino)           return verwerfen();

    // Content: also catches `cp foreign target`, which keeps the target's inode.
    const uint64_t fp = kv_idx_fingerprint((const uint8_t*)kv->wal_mmap,
                                           sizeof(AimlKvWalHeader), (size_t)h->wal_used);
    if (fp != h->wal_fp)                             return verwerfen();

    // Check the layout BEFORE pointers are set: the entry array and
    // the key block must fit into the file.  Too small a count would be
    // harmless, too large a one would read beyond the end of the file.
    const uint64_t count  = h->count;
    const size_t   arr_at = sizeof(AimlKvIdxHeader);
    if (msize < arr_at) return verwerfen();
    if (count > (msize - arr_at) / sizeof(AimlKvIdxEntry)) return verwerfen();
    const size_t keys_at = arr_at + (size_t)count * sizeof(AimlKvIdxEntry);
    if (keys_at > msize) return verwerfen();

    const AimlKvIdxEntry* ents = (const AimlKvIdxEntry*)((const char*)m + arr_at);
    const char* keys = (const char*)m + keys_at;
    const size_t keys_len = msize - keys_at;
    // Every key must lie in the block, every value in the WAL.
    for (uint64_t i = 0; i < count; i++) {
        const AimlKvIdxEntry& e = ents[i];
        if ((size_t)e.key_off > keys_len || (size_t)e.key_len > keys_len - e.key_off) return verwerfen();
        if (e.val_off > h->wal_used || (uint64_t)e.val_len > h->wal_used - e.val_off) return verwerfen();
    }

    kv->idx_mmap      = m;
    kv->idx_mmap_size = msize;
    kv->idx_entries   = ents;
    kv->idx_keys      = keys;
    kv->idx_count     = count;
    return h->wal_used;
}

// Binary search in the mapped dump -- no build-up, no allocation.
static bool kv_idx_find(AimlKv* kv, const char* key, size_t key_len, AimlKvValueLoc* out) {
    if (!kv->idx_entries || kv->idx_count == 0) return false;
    uint64_t lo = 0, hi = kv->idx_count;
    while (lo < hi) {
        const uint64_t mid = lo + (hi - lo) / 2;
        const AimlKvIdxEntry& e = kv->idx_entries[mid];
        const int c = kv_idx_cmp(kv->idx_keys + e.key_off, e.key_len, key, key_len);
        if (c == 0) { if (out) *out = AimlKvValueLoc{e.val_off, e.val_len}; return true; }
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return false;
}

// Overflow first, then the dump.  A tombstone in the overflow hides the dump.
static bool kv_lookup(AimlKv* kv, const char* key, size_t key_len, AimlKvValueLoc* out) {
    const std::string k(key, key_len);
    auto it = kv->index.find(k);
    if (it != kv->index.end()) {
        if (it->second.offset == KV_IDX_TOMB) return false;
        if (out) *out = it->second;
        return true;
    }
    return kv_idx_find(kv, key, key_len, out);
}
static inline bool kv_lookup(AimlKv* kv, const std::string& k, AimlKvValueLoc* out) {
    return kv_lookup(kv, k.data(), k.size(), out);
}
static inline bool kv_lookup(AimlKv* kv, const char* key, AimlKvValueLoc* out) {
    return kv_lookup(kv, key, key ? strlen(key) : 0, out);
}

// ── Index dump: writing (spec/31 §6.2.1) ───────────────────────────────────
// Only at checkpoint.  There the WAL is rewritten, all offsets
// change.  `items` carries ALL live entries; the pointers must live until
// return.  Return true = the file is in place.  false does not mean
// "data lost" but "no dump": the next start is slow, not
// wrong -- but then the caller must carry the overflow (spec/31 §6.2.1,
// "After the checkpoint the store gives up its RAM index", fallback).
static bool kv_idx_write_items(
        AimlKv* kv,
        std::vector<std::pair<std::string, AimlKvValueLoc>>& items) {
    if (!kv->index_dump || kv->idx_path.empty()) return false;
    if (kv->wal_fd < 0 || !kv->wal_mmap) return false;

    size_t key_bytes = 0;
    for (auto& it : items) key_bytes += it.first.size();
    // key_off is 32 bits.  With more than 4 GiB of key bytes there is no
    // dump -- better no dump than a silent overflow.
    if (key_bytes > 0xFFFFFFFFull) return false;
    // Sorted on the list itself, not on a pointer list next to it.
    std::sort(items.begin(), items.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    const std::string tmp = kv->idx_path + ".tmp";
    unlink(tmp.c_str());
    int fd = open(tmp.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_TRUNC, 0644);
    if (fd < 0) return false;

    struct stat ws{};
    if (fstat(kv->wal_fd, &ws) != 0) { close(fd); unlink(tmp.c_str()); return false; }

    AimlKvIdxHeader h{};
    memcpy(h.magic, KV_IDX_MAGIC, 8);
    h.version     = KV_IDX_VERSION;
    h.wal_version = ((const AimlKvWalHeader*)kv->wal_mmap)->version;
    h.val_size    = kv->val_size;
    h.wal_used    = kv->wal_used;
    h.wal_dev     = (uint64_t)ws.st_dev;
    h.wal_ino     = (uint64_t)ws.st_ino;
    h.wal_fp      = kv_idx_fingerprint((const uint8_t*)kv->wal_mmap,
                                       sizeof(AimlKvWalHeader), (size_t)kv->wal_used);
    h.count       = (uint64_t)items.size();

    bool ok = (write(fd, &h, sizeof(h)) == (ssize_t)sizeof(h));

    // Entries, then the key block in the same order.  Both
    // run through a 1 MiB buffer: an entry vector of its own would, with
    // millions of keys, be a third copy of the same list, and exactly these
    // copies are what §6.2.1 "After the checkpoint" is meant to save.
    std::string buf;
    buf.reserve(1u << 20);
    auto spuel = [&](bool erzwingen) {
        if (!ok) return;
        if (!erzwingen && buf.size() < (1u << 20)) return;
        if (!buf.empty() && write(fd, buf.data(), buf.size()) != (ssize_t)buf.size()) ok = false;
        buf.clear();
    };
    uint32_t key_off = 0;
    for (auto& it : items) {
        if (!ok) break;
        AimlKvIdxEntry e{};
        e.val_off = it.second.offset;
        e.val_len = it.second.length;
        e.key_off = key_off;
        e.key_len = (uint32_t)it.first.size();
        e.pad     = 0;
        buf.append((const char*)&e, sizeof(e));
        spuel(false);
        key_off += (uint32_t)it.first.size();
    }
    spuel(true);
    for (auto& it : items) {
        if (!ok) break;
        buf.append(it.first);
        spuel(false);
    }
    spuel(true);
    if (!ok) { close(fd); unlink(tmp.c_str()); return false; }
    fsync(fd);
    close(fd);
    // Link in atomically, as the checkpoint does with the WAL: a half-written
    // dump must never stand under the name.
    if (rename(tmp.c_str(), kv->idx_path.c_str()) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    return true;
}


// ALL live entries: overflow (without tombstones), then the dump (without those hidden by the
// overflow).  Whoever iterates over a persistent Kv MUST use this and
// not kv->index: with a dump the overflow only holds what has been written since the
// last checkpoint -- for a freshly opened store
// therefore nothing.  Exactly this is where filling the secondary indexes first
// failed (0 instead of 682593 facts, measured 2026-11-06).
template <typename F>
static void kv_for_each_live(AimlKv* kv, F&& fn) {
    for (auto& kvp : kv->index) {
        if (kvp.second.offset == KV_IDX_TOMB) continue;
        fn(kvp.first, kvp.second);
    }
    for (uint64_t i = 0; i < kv->idx_count; i++) {
        const AimlKvIdxEntry& de = kv->idx_entries[i];
        std::string key(kv->idx_keys + de.key_off, de.key_len);
        if (kv->index.find(key) != kv->index.end()) continue;  // Ueberlauf gewinnt
        fn(key, AimlKvValueLoc{de.val_off, de.val_len});
    }
}

// Count live entries.  Without a dump it is the size of the overflow, with
// a dump the dump's count, corrected by the entries the overflow hides
// or adds.
static int32_t kv_live_count(AimlKv* kv) {
    if (!kv->idx_entries) return (int32_t)kv->index.size();
    int64_t n = (int64_t)kv->idx_count;
    for (auto& kvp : kv->index) {
        const bool im_dump = kv_idx_find(kv, kvp.first.data(), kvp.first.size(), nullptr);
        if (kvp.second.offset == KV_IDX_TOMB) { if (im_dump) n--; }
        else if (!im_dump) n++;
    }
    return n < 0 ? 0 : (int32_t)n;
}

// Fallback route: build the index with read() if there is no mmap.
static void kv_wal_replay_fd(AimlKv* kv) {
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
            // Skip values — the index is enough; reads point directly into the mmap
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

// Build the index from the WAL.  The file is already mapped here -- all three
// call sites call kv_mmap_attach before --, so the build runs via the
// mmap pointer.  Until 2026-11-06 this loop read every field with its own read()
// plus lseek: five syscalls per record, with 5.8 million records of a cai subgraph
// around 29 million syscalls for 63 s of start time -- for bytes that were already in
// the address space.  Measured, that was the ENTIRE query time: `graph stats`,
// `ask` and a name lookup were at the same value.
static void kv_wal_replay(AimlKv* kv, size_t from) {
    const uint8_t* base = (const uint8_t*)kv->wal_mmap;
    if (!base || kv->wal_mmap_size <= sizeof(AimlKvWalHeader)) {
        kv_wal_replay_fd(kv);
        return;
    }
    const size_t n = kv->wal_mmap_size;
    // from > 0: a usable dump covers everything before it, only
    // the rest is replayed (spec/31 §6.2.1).
    size_t p        = from ? from : sizeof(AimlKvWalHeader);
    if (p < sizeof(AimlKvWalHeader) || p > n) p = sizeof(AimlKvWalHeader);
    size_t last_good = p;
    while (p + 5 <= n) {
        const uint8_t op = base[p];
        uint32_t key_len;
        memcpy(&key_len, base + p + 1, 4);
        const size_t key_at = p + 5;
        if (key_len > n - key_at) break;   // abgeschnitten
        if (op == KV_WAL_SET || op == KV_WAL_SET_BLOB) {
            const size_t vb_at = key_at + key_len;
            if (vb_at + 4 > n) break;
            uint32_t val_bytes;
            memcpy(&val_bytes, base + vb_at, 4);
            const size_t value_offset = vb_at + 4;
            if (val_bytes > n - value_offset) break;
            std::string key((const char*)base + key_at, key_len);
            if (op == KV_WAL_SET_BLOB) kv->blob_only = true;
            if (kv->ser_nodes) kv_decoded_evict(kv, key);
            kv->index[key] = AimlKvValueLoc{ (uint64_t)value_offset, val_bytes };
            p = value_offset + val_bytes;
        } else if (op == KV_WAL_DELETE) {
            std::string key((const char*)base + key_at, key_len);
            if (kv->idx_entries) {
                // Dump active: the key can be in it.  Deleting in the
                // overflow would bring it back after a restart --
                // it needs a tombstone that hides the dump.
                if (kv->ser_nodes) kv_decoded_evict(kv, key);
                kv->index[key] = AimlKvValueLoc{KV_IDX_TOMB, 0};
            } else {
                auto it = kv->index.find(key);
                if (it != kv->index.end()) {
                    if (kv->ser_nodes) kv_decoded_evict(kv, key);
                    kv->index.erase(it);
                }
            }
            p = key_at + key_len;
        } else {
            break;  // unknown op -- also the zero range of a sparse file
        }
        last_good = p;
    }
    kv->wal_used = (uint64_t)last_good;
}

// Separate _idx entry points instead of a changed signature: getFunction in the
// compiler does NOT check signatures (the first declarer wins), and
// aiml_db_kv_open_persist is also declared in tensor_runtime.cpp.
static void* kv_open_persist_impl(const char* name, int64_t val_size,
                                  const char* base_path, bool index_dump) {
    std::string dir = resolve_aiondb_dir(base_path);
    std::string nm  = name ? name : "kv";
    std::string registry_key = dir + nm;

    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto it = g_persistent_kvs_by_name.find(registry_key);
    if (it != g_persistent_kvs_by_name.end()) {
        kv_guard_reopen(it->second, registry_key, val_size, nullptr, 0);   // spec/31 §6.10
        return it->second;
    }

    db_mkdir_p(dir);
    AimlKv* kv    = new AimlKv();
    kv->val_size   = val_size;
    kv->persistent = true;
    kv->wal_path   = dir + nm + ".kvwal";
    kv->index_dump = index_dump;
    kv->idx_path   = index_dump ? (dir + nm + ".kvidx") : std::string();
    int fd = kv_wal_open_with_header(kv->wal_path, /*typed=*/false, val_size);
    if (fd < 0) {
        // A persistent KV whose backing WAL cannot be opened is reported LOUDLY, but we
        // MUST NOT take down the whole server (an earlier version _exit(1)'d here — that
        // turned a single side KV owned by another user, e.g. a root-owned wallets/auc
        // .kvwal, into a boot crash that killed unrelated services like login/decksets).
        // Instead: name the offending file, and return the KV in a crippled (wal_fd<0)
        // state — reads are empty, writes are refused by aiml_db_kv_set with a loud
        // diagnostic (see there).  The server keeps running; ops fixes the ownership.
        fprintf(stderr,
            "[AionDB] ERROR: cannot open persistent KV WAL '%s' (errno=%d) — "
            "check file ownership/permissions (owned by another user?). This KV is "
            "UNAVAILABLE (reads empty, writes refused); the server continues.\n",
            kv->wal_path.c_str(), errno);
    } else {
        kv->wal_fd = fd;
        if (!kv_mmap_attach(kv)) {
            close(fd);
            kv->wal_fd = -1;
            delete kv;
            return nullptr;
        }
        // spec/31 §6.2.1: if there is a usable dump, only the rest of the WAL from its
        // high-water mark is replayed -- otherwise everything.
        kv_wal_replay(kv, kv_idx_try_load(kv));
    }
    kv->store_name = registry_key;
    g_persistent_kvs_by_name[registry_key] = kv;
    g_persistent_kvs.push_back(kv);
    return kv;
}

extern "C" void* aiml_db_kv_open_persist(const char* name, int64_t val_size,
                                           const char* base_path) {
    return kv_open_persist_impl(name, val_size, base_path, /*index_dump=*/false);
}

extern "C" void* aiml_db_kv_open_persist_idx(const char* name, int64_t val_size,
                                               const char* base_path, int32_t index_dump) {
    return kv_open_persist_impl(name, val_size, base_path, index_dump != 0);
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
    int temp_fd = open(temp_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_TRUNC, 0644);
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
    // spec/31 §6.2.1: the dump entries also point into old_mmap and
    // must be copied over as well.  If the checkpoint only iterated over the overflow,
    // it would lose everything that is ONLY in the dump -- i.e. almost everything.
    const AimlKvIdxEntry* old_ents  = kv->idx_entries;
    const char*           old_keys  = kv->idx_keys;
    const uint64_t        old_count = kv->idx_count;
    void*  old_idx_mmap      = kv->idx_mmap;
    size_t old_idx_mmap_size = kv->idx_mmap_size;
    kv->idx_entries = nullptr;
    kv->idx_keys    = nullptr;
    kv->idx_count   = 0;
    kv->idx_mmap      = nullptr;
    kv->idx_mmap_size = 0;

    kv->wal_mmap      = temp_mmap;
    kv->wal_mmap_size = KV_MMAP_RESERVE_INITIAL;
    kv->wal_used      = sizeof(AimlKvWalHeader);
    kv->wal_fd        = temp_fd;
    kv->index.clear();
    kv->blob_only = blob_only_save;  // preserve mode through rewrite

    // With a dump, the new locations are collected in a LIST instead of in the overflow: roughly
    // 60 bytes per key against 434 in the unordered_map, and the overflow
    // stays empty afterwards (spec/31 §6.2.1, "After the checkpoint the store
    // gives up its RAM index").  Without a dump the overflow stays the index.
    const bool want_dump = kv->index_dump && !kv->idx_path.empty();
    std::vector<std::pair<std::string, AimlKvValueLoc>> frisch;
    if (want_dump) frisch.reserve((size_t)old_count + old_index.size());

    // 4. Copy each record from old mmap → temp via kv_wal_write_* (uses memcpy
    //    into temp_mmap; kv_mmap_grow handles overflow within temp).
    //    First the dump, without the keys hidden by the overflow; then the
    //    overflow without tombstones.  That way every key lands exactly once.
    for (uint64_t i = 0; i < old_count; i++) {
        const AimlKvIdxEntry& e = old_ents[i];
        std::string k(old_keys + e.key_off, e.key_len);
        if (old_index.find(k) != old_index.end()) continue;  // value or tombstone in the overflow
        const void* old_val = (const char*)old_mmap + e.val_off;
        if (blob_only_save) {
            kv_wal_write_set_blob(kv, k.c_str(), old_val, e.val_len);
        } else {
            kv_wal_write_set(kv, k.c_str(), old_val, e.val_len);
        }
        const AimlKvValueLoc loc{kv->wal_used - e.val_len, e.val_len};
        if (want_dump) frisch.emplace_back(std::move(k), loc);
        else           kv->index[k] = loc;
    }
    for (auto& [k, old_loc] : old_index) {
        if (old_loc.offset == KV_IDX_TOMB) continue;   // tombstone: nothing to write
        const void* old_val = (const char*)old_mmap + old_loc.offset;
        if (blob_only_save) {
            kv_wal_write_set_blob(kv, k.c_str(), old_val, old_loc.length);
        } else {
            kv_wal_write_set(kv, k.c_str(), old_val, old_loc.length);
        }
        const AimlKvValueLoc loc{kv->wal_used - old_loc.length, old_loc.length};
        if (want_dump) frisch.emplace_back(k, loc);
        else           kv->index[k] = loc;
    }
    // The old overflow is written off.  With a dump this lowers the peak by
    // exactly the item this section wants to save.
    if (want_dump) { old_index.clear(); old_index.rehash(0); }

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
    // 8. Release the old dump (its offsets point into the file that was just
    //    deleted) and write the new one -- spec/31 §6.2.1 writes the dump
    //    exclusively here, because this is where the offsets come into being.  Only AFTER the
    //    rename, so that fstat delivers the final inode.
    if (old_idx_mmap) munmap(old_idx_mmap, old_idx_mmap_size);
    if (want_dump) {
        const bool steht = kv_idx_write_items(kv, frisch);
        // Load first; only then may the overflow stay empty.  If the
        // fresh dump does NOT load, the overflow must be filled from the list
        // before the lock is released -- a store with an empty overflow and
        // without a valid dump would have lost its entire content without
        // a single line missing (spec/31 §6.2.1, fallback).
        if (!steht || kv_idx_try_load(kv) != kv->wal_used) {
            for (auto& p : frisch) kv->index[std::move(p.first)] = p.second;
        }
    }
    // 9. And the secondary indexes (spec/45 "Writing happens at checkpoint time") --
    //    with the stamp of the WAL that was just renamed, i.e. after the primary index.
    kv_sidx_write_all(kv);
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
    // Replay WAL. Append-position is tracked in kv->wal_used,
    // do NOT SEEK_END the fd — after ftruncate(RESERVE) it points to 16 GB.
    // spec/31 §6.2.1: if there is a usable dump, only the rest from its
    // high-water mark -- exactly the case reload is meant for (another
    // process has appended, the inode is the same, the dump stays valid).
    kv_wal_replay(kv, kv_idx_try_load(kv));
}

extern "C" void* aiml_db_kv_open_typed(const char* name, int64_t val_size,
                                         const AimlTypeNode* nodes, int32_t n_nodes) {
    std::string nm = name ? name : "kv";
    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto it = g_non_persist_kvs_by_name.find(nm);
    if (it != g_non_persist_kvs_by_name.end()) {
        kv_guard_reopen(it->second, nm, val_size, nodes, n_nodes);   // spec/31 §6.10
        return it->second;
    }
    AimlKv* kv    = new AimlKv();
    kv->val_size  = val_size;
    kv->ser_nodes = nodes;
    kv->ser_n     = n_nodes;
    g_non_persist_kvs_by_name[nm] = kv;
    return kv;
}

static void* kv_open_typed_persist_impl(const char* name, int64_t val_size,
                                        const char* base_path,
                                        const AimlTypeNode* nodes, int32_t n_nodes,
                                        bool index_dump) {
    std::string dir = resolve_aiondb_dir(base_path);
    std::string nm  = name ? name : "kv";
    std::string registry_key = dir + nm;

    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto it = g_persistent_kvs_by_name.find(registry_key);
    if (it != g_persistent_kvs_by_name.end()) {
        kv_guard_reopen(it->second, registry_key, val_size, nodes, n_nodes);   // spec/31 §6.10
        return it->second;
    }

    db_mkdir_p(dir);
    AimlKv* kv    = new AimlKv();
    kv->val_size   = val_size;
    kv->ser_nodes  = nodes;
    kv->ser_n      = n_nodes;
    kv->persistent = true;
    kv->wal_path   = dir + nm + ".kvwal";
    kv->index_dump = index_dump;
    kv->idx_path   = index_dump ? (dir + nm + ".kvidx") : std::string();
    int fd = kv_wal_open_with_header(kv->wal_path, /*typed=*/true, val_size);
    if (fd >= 0) {
        kv->wal_fd = fd;
        if (!kv_mmap_attach(kv)) {
            close(fd);
            kv->wal_fd = -1;
            delete kv;
            return nullptr;
        }
        // spec/31 §6.2.1: if there is a usable dump, only the rest of the WAL from its
        // high-water mark is replayed -- otherwise everything.
        kv_wal_replay(kv, kv_idx_try_load(kv));
    }
    g_persistent_kvs_by_name[registry_key] = kv;
    g_persistent_kvs.push_back(kv);
    return kv;
}

extern "C" void* aiml_db_kv_open_typed_persist(const char* name, int64_t val_size,
                                                 const char* base_path,
                                                 const AimlTypeNode* nodes, int32_t n_nodes) {
    return kv_open_typed_persist_impl(name, val_size, base_path, nodes, n_nodes,
                                      /*index_dump=*/false);
}

extern "C" void* aiml_db_kv_open_typed_persist_idx(const char* name, int64_t val_size,
                                                     const char* base_path,
                                                     const AimlTypeNode* nodes, int32_t n_nodes,
                                                     int32_t index_dump) {
    return kv_open_typed_persist_impl(name, val_size, base_path, nodes, n_nodes,
                                      index_dump != 0);
}

extern "C" void aiml_db_kv_set(void* kv_ptr, const char* key, const void* val, int64_t val_size) {
    SerGuardOn _sg_;
    if (!kv_ptr || !key) return;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    ++kv->writes;   // doc/spec/99 F99-5: a counter, not a timestamp (no clock access)
    std::lock_guard<std::mutex> lock(kv->mtx);
    // A crippled persistent KV (its WAL could not be opened — see aiml_db_kv_open_persist)
    // has no backing store.  Refuse the write LOUDLY (once) instead of dereferencing a
    // null mmap; the server stays up but this KV cannot persist until ops fixes it.
    if (kv->persistent && kv->wal_fd < 0) {
        static thread_local bool warned_crippled = false;
        if (!warned_crippled) {
            warned_crippled = true;
            fprintf(stderr, "[AionDB] ERROR: write to UNAVAILABLE KV '%s' refused "
                    "(its WAL could not be opened — check file ownership/permissions).\n",
                    kv->wal_path.c_str());
        }
        return;
    }
    if (kv->ser_nodes && val) {
        // Typed: encode struct → blob, then invalidate decoded cache.
        //
        // Order is critical: encode MUST run before evict because `val` can
        // alias into the decoded_cache. If evict ran first it would free
        // buffers the encoder still needs to read → use-after-free in strlen.
        // See doc/issue/kv_get_mutate_set_same_key_crash.md
        char* blob = aiml_ser_encode(val, kv->ser_nodes, kv->ser_n);
        if (!blob) return;
        // Self-write: `val` is this key's own cache slot (a ref<T> from kv.get).
        // The slot is canonical and stays — the borrow remains valid and the slot
        // keeps owning its fields (spec/61 §5.5 step 0, spec/31 §6.7).
        auto dc = kv->decoded_cache.find(key);
        bool val_is_cache = (dc != kv->decoded_cache.end() && dc->second.data() == val);
        if (val_is_cache) {
            kv_decoded_touch(kv, key);
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
            // String mode, persistent: the WAL holds strlen+1 bytes incl. \0,
            // so that kv_get returns a direct mmap pointer as a C string.
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
        // index path (step 3)
        AimlKvValueLoc loc{};
        if (!kv_lookup(kv, key, &loc)) return nullptr;
        const char* val_ptr = (const char*)kv->wal_mmap + loc.offset;
        if (kv->ser_nodes) {
            // Typed: decode in cache (bounded LRU)
            auto dc = kv->decoded_cache.find(key);
            if (dc != kv->decoded_cache.end()) {
                kv_decoded_touch(kv, key);
                return dc->second.data();
            }
            std::vector<uint8_t> decoded((size_t)kv->val_size, 0);
            if (aiml_ser_decode(val_ptr, decoded.data(), kv->ser_nodes, kv->ser_n) != 0) {
                // Corrupt/schema-mismatched blob: free any partial heap and cache a
                // CLEAN zeroed value instead of publishing a half-decoded one (P0.3).
                // (Zeroed slot rather than nullptr → ptr<T> view callers don't null-deref.)
                aiml_ser_free_decoded(decoded.data(), kv->ser_nodes, kv->ser_n);
                decoded.assign((size_t)kv->val_size, 0);
                // Restore container handles — a zeroed field is never
                // valid (see get_copy_at).  Without this the first `.len` crashes.
                aiml_ser_init_empty_containers(decoded.data(), kv->ser_nodes, kv->ser_n);
            }
            auto& cached = kv->decoded_cache[key];
            cached = std::move(decoded);
            kv_decoded_touch(kv, key);
            kv_decoded_lru_trim(kv);
            return cached.data();
        }
        // String / blob / fixed: pointer directly into the mmap (zero-copy).
        // The string has its \0 terminator within the length bytes (step 4).
        return (void*)val_ptr;
    }
    // Non-persistent: legacy map path
    auto it = kv->map.find(key);
    if (it == kv->map.end()) return nullptr;
    if (kv->ser_nodes) {
        auto dc = kv->decoded_cache.find(key);
        if (dc != kv->decoded_cache.end()) {
            kv_decoded_touch(kv, key);
            return dc->second.data();
        }
        std::vector<uint8_t> decoded((size_t)kv->val_size, 0);
        if (aiml_ser_decode(reinterpret_cast<const char*>(it->second.data()),
                            decoded.data(), kv->ser_nodes, kv->ser_n) != 0) {
            aiml_ser_free_decoded(decoded.data(), kv->ser_nodes, kv->ser_n);
            decoded.assign((size_t)kv->val_size, 0);   // clean zeroed, no garbage (P0.3)
            // spec/61 §5 (memV3): zeroing nulled every container-handle field, but a
            // container FIELD is never null — the caller's next `.len`/`.at` derefs it.
            // Without this the "safe, no crash" path of a schema-mismatch (-2) still
            // SIGSEGVs on the first access (measured 2026-10-13: cosmos after adding a
            // field to PipelineStage).  Same repair as the get_copy_at path below.
            aiml_ser_init_empty_containers(decoded.data(), kv->ser_nodes, kv->ser_n);
        }
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
        AimlKvValueLoc loc{};
        if (!kv_lookup(kv, key, &loc)) return nullptr;
        const char* val_ptr = (const char*)kv->wal_mmap + loc.offset;
        if (kv->ser_nodes) {
            // Always decode fresh from WAL. Previous slot heap allocations
            // (if any from a prior get/get_copy) are freed first.
            kv_decoded_evict(kv, key);
            std::vector<uint8_t> decoded((size_t)kv->val_size, 0);
            if (aiml_ser_decode(val_ptr, decoded.data(), kv->ser_nodes, kv->ser_n) != 0) {
                // Corrupt blob: free partial heap, publish a CLEAN zeroed slot (P0.3).
                aiml_ser_free_decoded(decoded.data(), kv->ser_nodes, kv->ser_n);
                decoded.assign((size_t)kv->val_size, 0);
                // Restore container handles — a zeroed field is never
                // valid (see get_copy_at).  Without this the first `.len` crashes.
                aiml_ser_init_empty_containers(decoded.data(), kv->ser_nodes, kv->ser_n);
            }
            auto& slot = kv->decoded_cache[key];
            slot = std::move(decoded);
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
        std::vector<uint8_t> decoded((size_t)kv->val_size, 0);
        if (aiml_ser_decode(reinterpret_cast<const char*>(it->second.data()),
                            decoded.data(), kv->ser_nodes, kv->ser_n) != 0) {
            aiml_ser_free_decoded(decoded.data(), kv->ser_nodes, kv->ser_n);
            decoded.assign((size_t)kv->val_size, 0);   // clean zeroed (P0.3)
        }
        auto& slot = kv->decoded_cache[key];
        slot = std::move(decoded);
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

// get_copy_at — deep-copy the value for `key` into a CALLER-owned buffer `dst`
// (spec/31 §6.7.2).  Unlike get_copy (which allocates a fresh copy each call and
// hands back a decoded_cache slot), this reuses the caller's buffer: it frees dst's
// prior heap fields FIRST, then decodes a fresh independent copy into dst.  The
// STRUCT SHELL (dst) is reused, but decode re-allocates the nested heap fields
// (lists/strings/maps) each call and the prior ones are freed first — so there is
// no accumulation across calls into the same dst (a bounded steady-state footprint),
// NOT zero allocation.  Never touches the decoded_cache.  Returns:
//    1 = found & copied,
//    0 = missing key (dst left unchanged),
//   -1 = value present but the blob failed to decode (schema mismatch / corrupt);
//        dst is deep-freed and zero-inited (safe, but holds no valid value).
// On the first call dst must be zero-initialised (freeing zeroed fields is a no-op).
extern "C" int32_t aiml_db_kv_get_copy_at(void* kv_ptr, const char* key, void* dst) {
    SerGuardOn _sg_;
    if (!kv_ptr || !key || !dst) return 0;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    std::lock_guard<std::mutex> lock(kv->mtx);
    const char* val_ptr = nullptr;
    if (kv->persistent) {
        AimlKvValueLoc loc{};
        if (!kv_lookup(kv, key, &loc)) return 0;
        val_ptr = (const char*)kv->wal_mmap + loc.offset;
    } else {
        auto it = kv->map.find(key);
        if (it == kv->map.end()) return 0;
        val_ptr = reinterpret_cast<const char*>(it->second.data());
    }
    if (kv->ser_nodes) {
        // Free the caller buffer's prior heap (nested lists/strings/maps) before the
        // refill, then decode a fresh independent deep-copy into it.  No cache touch.
        aiml_ser_free_decoded(dst, kv->ser_nodes, kv->ser_n);
        memset(dst, 0, (size_t)kv->val_size);
        int rc = aiml_ser_decode(val_ptr, dst, kv->ser_nodes, kv->ser_n);
        if (rc != 0) {
            // Invalid blob (schema mismatch = -2, structural error = -1).  decode()
            // may have partially populated dst before failing → deep-free the partial
            // heap and re-zero so the caller never reads a half-decoded value.  Signal
            // -1 so the caller skips this record (no tick, no write-back).
            aiml_ser_free_decoded(dst, kv->ser_nodes, kv->ser_n);
            memset(dst, 0, (size_t)kv->val_size);
            // spec/61 §5 (memV3): the memset nulled every container-handle field, but a container FIELD
            // is never null — the caller's next `.len`/`.at` on dst would deref null. Re-init empty
            // handles so the caller reads a valid empty list/map/buffer (test_kv_get_copy_at F3/FAIL 10).
            aiml_ser_init_empty_containers(dst, kv->ser_nodes, kv->ser_n);
            return -1;
        }
        return 1;
    }
    // Non-serialized typed value: plain byte copy of val_size bytes.
    if (kv->val_size > 0) {
        memcpy(dst, val_ptr, (size_t)kv->val_size);
        return 1;
    }
    // val_size==0 (pointer-stored / non-typed): copy the stored pointer.
    memcpy(dst, val_ptr, sizeof(void*));
    return 1;
}

extern "C" int32_t aiml_db_kv_has(void* kv_ptr, const char* key) {
    if (!kv_ptr || !key) return 0;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    std::lock_guard<std::mutex> lock(kv->mtx);
    if (kv->persistent) return kv_lookup(kv, key, nullptr) ? 1 : 0;
    return kv->map.count(key) ? 1 : 0;
}

extern "C" int32_t aiml_db_kv_len(void* kv_ptr) {
    if (!kv_ptr) return 0;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    std::lock_guard<std::mutex> lock(kv->mtx);
    if (kv->persistent) return kv_live_count(kv);
    return (int32_t)kv->map.size();
}

extern "C" void aiml_db_kv_delete(void* kv_ptr, const char* key) {
    if (!kv_ptr || !key) return;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    std::lock_guard<std::mutex> lock(kv->mtx);
    if (kv->persistent) {
        if (!kv_lookup(kv, key, nullptr)) return;
        kv_wal_write_delete(kv, key);
        if (kv->ser_nodes) kv_decoded_evict(kv, key);
        if (kv->idx_entries) {
            // spec/31 §6.2.1: if the key is in the dump, a tombstone must
            // hide it -- an erase in the overflow would fetch it back from the dump
            // on the next lookup.
            kv->index[key] = AimlKvValueLoc{KV_IDX_TOMB, 0};
        } else {
            kv->index.erase(key);
        }
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
        AimlKvValueLoc loc{};
        if (!kv_lookup(kv, key, &loc)) { if (out_size) *out_size = 0; return nullptr; }
        if (out_size) *out_size = (int32_t)loc.length;
        return (void*)((const char*)kv->wal_mmap + loc.offset);
    }
    // Non-persistent: legacy map path
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
        // wal_used was read from the header by kv_mmap_attach
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
    // spec/31 §6.2.1: a dump left lying around would repopulate the emptied store
    // on the next open.  The fingerprint would catch it anyway,
    // but relying on a check where deletion is
    // unambiguous would be careless.
    if (kv->index_dump) {
        kv_idx_detach(kv);
        if (!kv->idx_path.empty()) unlink(kv->idx_path.c_str());
    }
    // The secondary indexes belong to the store: an emptied store must not
    // return hits any more (spec/45).  Applies without index_dump too -- then
    // there is just nothing to delete.
    kv_sidx_unlink_all(kv);
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
        // 1) Shrink the mapping to the actual end of the WAL → virtual
        //    address space is returned, the apparent file size drops.
        // 2) madvise(MADV_DONTNEED) on the remaining region → RSS drops.
        // The index stays intact; reads page the data back in on demand.
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
                    // ftruncate gives back the apparent file size — Antigravity-friendly
                    ftruncate(kv->wal_fd, (off_t)aligned_used);
                }
                // ignore mremap-shrink errors — the data stays valid,
                // only the footprint stays in its current state
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
    // NOTE: WAL fd, mmap, file and (for persistent) the index stay intact
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
    // ── Index dump (spec/31 §6.2.1) ────────────────────────────────────────
    // With a dump the overflow runs first, then the dump without the keys hidden
    // by it.  Dump keys are in the mmap WITHOUT \0, a
    // pointer to them would not be a valid C string -- hence one
    // copy per step.  Without a dump the old route stays untouched, so that the
    // lifetime of the returned pointers does not change.
    bool        dump_path = false;
    uint64_t    dump_pos  = 0;
    bool        have      = false;
    std::string cur_key;   // naechster Schluessel
    std::string ret_key;   // the last one returned, valid until the next call
};

// Provide the next live key: overflow before dump.
static void iter_kv_advance(AimlIterator* it) {
    it->have = false;
    AimlKv* kv = it->kv;
    while (it->idx_cur != it->idx_end) {
        const std::string key  = it->idx_cur->first;
        const uint64_t    off  = it->idx_cur->second.offset;
        ++it->idx_cur;
        if (off == KV_IDX_TOMB) continue;   // a tombstone is not an entry
        it->cur_key = key;
        it->have    = true;
        return;
    }
    while (it->dump_pos < kv->idx_count) {
        const AimlKvIdxEntry& de = kv->idx_entries[it->dump_pos++];
        std::string key(kv->idx_keys + de.key_off, de.key_len);
        if (kv->index.find(key) != kv->index.end()) continue;  // Ueberlauf gewinnt
        it->cur_key = std::move(key);
        it->have    = true;
        return;
    }
}

extern "C" void* aiml_iter_create_kv(void* kv_ptr) {
    if (!kv_ptr) return nullptr;
    AimlKv* kv = static_cast<AimlKv*>(kv_ptr);
    std::lock_guard<std::mutex> lock(kv->mtx);
    auto* it = new AimlIterator();
    it->type = 0;
    it->kv   = kv;
    it->persistent_path = kv->persistent;
    it->dump_path = (kv->persistent && kv->idx_entries != nullptr);
    if (it->persistent_path) {
        it->idx_cur = kv->index.begin();
        it->idx_end = kv->index.end();
        if (it->dump_path) { it->dump_pos = 0; iter_kv_advance(it); }
    } else {
        it->map_cur = kv->map.begin();
        it->map_end = kv->map.end();
    }
    return it;
}

extern "C" const char* aiml_iter_next(void* iter_ptr) {
    if (!iter_ptr) return "";
    AimlIterator* it = static_cast<AimlIterator*>(iter_ptr);
    if (it->dump_path) {
        if (!it->have) return "";
        it->ret_key = it->cur_key;
        iter_kv_advance(it);
        return it->ret_key.c_str();
    }
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
    if (it->dump_path) return it->have ? 1 : 0;
    if (it->persistent_path) return (it->idx_cur != it->idx_end) ? 1 : 0;
    return (it->map_cur != it->map_end) ? 1 : 0;
}

extern "C" void aiml_iter_reset(void* iter_ptr) {
    if (!iter_ptr) return;
    AimlIterator* it = static_cast<AimlIterator*>(iter_ptr);
    if (it->persistent_path) {
        it->idx_cur = it->kv->index.begin();
        it->idx_end = it->kv->index.end();
        if (it->dump_path) { it->dump_pos = 0; it->have = false; iter_kv_advance(it); }
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
    std::string nm = name ? name : "vec";
    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto it = g_non_persist_vecs_by_name.find(nm);
    if (it != g_non_persist_vecs_by_name.end()) return it->second;
    AimlVecStore* vs = new AimlVecStore();
    vs->dims = dims;
    g_non_persist_vecs_by_name[nm] = vs;
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

    db_mkdir_p(dir);
    AimlVecStore* vs = new AimlVecStore();
    vs->dims       = dims;
    vs->persistent = true;
    vs->wal_path   = dir + nm + ".vecwal";
    int fd = open(vs->wal_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
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
    std::string nm = name ? name : "ts";
    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto it = g_non_persist_ts_by_name.find(nm);
    if (it != g_non_persist_ts_by_name.end()) return it->second;
    AimlTimeSeries* ts = new AimlTimeSeries();
    ts->elem_size = elem_size;
    g_non_persist_ts_by_name[nm] = ts;
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

    db_mkdir_p(dir);
    AimlTimeSeries* ts = new AimlTimeSeries();
    ts->elem_size   = elem_size;
    ts->persistent  = true;
    ts->log_path    = dir + nm + ".tslog";
    int fd = open(ts->log_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
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

// A bucket of the 1:N index: the kv keys in INSERTION ORDER, plus the
// position of every key in it.  spec/45 "Cost of index maintenance": add and
// remove are amortised O(1), independent of the bucket size.
//
// Previously the bucket was a bare vector<string>: `add` searched it with
// std::find for duplicates, `remove` shifted it with std::remove — both
// O(bucket) per kv.set.  A bucket holds all rows that carry ONE field value,
// so for a field with few distinct values it grows to
// the size of the store, and a write batch became quadratic: per 4000
// sets, 487 ms with 4k rows and 4608 ms with 24k (cai 2026-11-04, German
// Wikipedia: a 69 s batch was not finished after 600 s).
//
// `remove` sets a grave instead of shifting and only compacts when the
// graves outnumber the live entries — that way the order is
// preserved, and compacting costs amortised O(1).  A grave is not recognisable by
// the empty key ("" is a legitimate kv key), but
// by its own marker.
struct AimlIndexBucket {
    // `pos` OWNS the keys, `slots` points to its nodes: the nodes
    // of an unordered_map are stable (a rehash does not move them), and so
    // every key is in memory only ONCE.  A grave is nullptr —
    // it would not be recognisable by the empty key, "" is a legitimate
    // kv key.  Whoever clears a slot clears it BEFORE the pos.erase.
    std::vector<const std::string*>         slots;  // Einfuegereihenfolge
    std::unordered_map<std::string, size_t> pos;    // Schluessel → Stelle in slots
    size_t graves = 0;

    void add(const char* kv_key) {
        auto [it, fresh] = pos.emplace(kv_key, slots.size());
        if (!fresh) return;                         // already in — the index is a set
        slots.push_back(&it->first);
    }
    // true if the bucket is empty afterwards (the caller then deletes it)
    bool remove(const char* kv_key) {
        auto it = pos.find(kv_key);
        if (it == pos.end()) return pos.empty();
        slots[it->second] = nullptr;                // before the erase, otherwise the pointer dangles
        pos.erase(it);
        ++graves;
        if (pos.empty()) return true;
        if (graves > pos.size()) compact();
        return false;
    }
    void compact() {
        size_t write = 0;
        for (size_t read = 0; read < slots.size(); ++read)
            if (slots[read]) slots[write++] = slots[read];
        slots.resize(write);
        // Only relabel existing keys — no insert, hence no
        // rehash, hence the nodes that slots points to stay valid.
        for (size_t i = 0; i < slots.size(); ++i) pos[*slots[i]] = i;
        graves = 0;
    }
};

// ── Secondary index dump: format (spec/45 "Persistence") ───────────────────
// One file per (store, field).  The entries are sorted by the NORMALISED
// field value -- that is exactly what the query searches by, and writing and searching
// must use the same order (kv_idx_cmp), otherwise the binary
// search finds nothing, and does so silently.
static const char     KV_SIDX_MAGIC[8] = {'A','I','M','L','K','V','S','X'};
static const uint32_t KV_SIDX_VERSION  = 1;
static const uint32_t KV_SIDX_F_UNIQUE = 1u << 0;
static const uint32_t KV_SIDX_F_LOWER  = 1u << 1;

struct AimlSidxHeader {     // 64 Byte, natuerlich ausgerichtet
    char     magic[8];
    uint32_t version;
    uint32_t flags;         // unique / lower -- must match the declaration
    uint64_t wal_dev;
    uint64_t wal_ino;
    uint64_t wal_used;      // stamp of the main WAL, as in spec/31 §6.2.1
    uint64_t wal_fp;
    uint32_t val_count;
    uint32_t key_count;
    uint64_t reserved;
};
struct AimlSidxVal {        // 16 Byte
    uint32_t val_off, val_len;      // Stelle im Stringblock
    uint32_t key_first, key_cnt;    // Fenster im Schluesselfeld
};
struct AimlSidxKey {        // 8 Byte
    uint32_t key_off, key_len;
};

struct AimlIndex {
    std::mutex mtx;
    bool unique = false;
    std::string mode;  // "" | "lower"
    std::unordered_map<std::string, AimlIndexBucket> map_1n;
    std::unordered_map<std::string, std::string>     map_unique;

    // Secondary index dump (spec/45 "Persistence").  As long as `loaded` is set,
    // the file mapping answers every field value that is NOT in `owned`;
    // for those in `owned` only the memory index applies.  Never both
    // at once -- otherwise a key would come twice or not at all.
    std::string path;                        // <name>__<feld>.kvsidx
    void*              dump       = nullptr;
    size_t             dump_size  = 0;
    const AimlSidxVal* dvals      = nullptr;
    const AimlSidxKey* dkeys      = nullptr;
    uint32_t           dval_count = 0;
    bool               loaded     = false;
    std::unordered_set<std::string> owned;   // im Speicher gefuehrte Feldwerte
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


// ── Nebenindex-Dump: Lesen, Kopieren, Schreiben (spec/45 "Persistence") ────

// <dir>/<name>__<field>.kvsidx, derived from the WAL path.
static std::string sidx_path(AimlKv* kv, const std::string& field) {
    std::string base = kv->wal_path;
    const std::string suf = ".kvwal";
    if (base.size() > suf.size() && base.compare(base.size() - suf.size(), suf.size(), suf) == 0)
        base.erase(base.size() - suf.size());
    return base + "__" + field + ".kvsidx";
}

static inline uint32_t sidx_flags(bool unique, const std::string& mode) {
    uint32_t f = 0;
    if (unique)         f |= KV_SIDX_F_UNIQUE;
    if (mode == "lower") f |= KV_SIDX_F_LOWER;
    return f;
}

static void sidx_detach(AimlIndex* idx) {
    if (idx->dump) munmap(idx->dump, idx->dump_size);
    idx->dump       = nullptr;
    idx->dump_size  = 0;
    idx->dvals      = nullptr;
    idx->dkeys      = nullptr;
    idx->dval_count = 0;
    idx->loaded     = false;
}

// Binary search in the mapped dump -- no build-up, no allocation.
static const AimlSidxVal* sidx_find(const AimlIndex* idx, const std::string& k) {
    if (!idx->loaded || idx->dval_count == 0) return nullptr;
    const char* base = (const char*)idx->dump;
    uint32_t lo = 0, hi = idx->dval_count;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        const AimlSidxVal& v = idx->dvals[mid];
        const int c = kv_idx_cmp(base + v.val_off, v.val_len, k.data(), k.size());
        if (c == 0) return &v;
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return nullptr;
}

// Check and map the dump.  Takes kv->mtx, so it must NOT run under the
// registry lock (the checkpoint holds kv->mtx and then reaches into the
// registry -- the other order would be a deadlock).
static bool sidx_try_load(AimlKv* kv, AimlIndex* idx, const char* field) {
    sidx_detach(idx);
    if (!kv || !field || !kv->index_dump) return false;
    std::lock_guard<std::mutex> klk(kv->mtx);
    if (kv->wal_fd < 0 || !kv->wal_mmap) return false;
    idx->path = sidx_path(kv, field);

    int fd = open(idx->path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st{};
    if (fstat(fd, &st) != 0 || (size_t)st.st_size < sizeof(AimlSidxHeader)) { close(fd); return false; }
    void* m = mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return false;
    const size_t msize = (size_t)st.st_size;
    const AimlSidxHeader* h = (const AimlSidxHeader*)m;
    auto verwerfen = [&](void) -> bool { munmap(m, msize); return false; };

    if (memcmp(h->magic, KV_SIDX_MAGIC, 8) != 0) return verwerfen();
    if (h->version != KV_SIDX_VERSION)           return verwerfen();
    // `db_indexed: true` was turned into `unique` or "lower": the old
    // dump describes a different order and is unusable.
    if (h->flags != sidx_flags(idx->unique, idx->mode)) return verwerfen();
    if (h->wal_used < sizeof(AimlKvWalHeader))   return verwerfen();
    if (h->wal_used != kv->wal_used)             return verwerfen();
    if (h->wal_used > kv->wal_mmap_size)         return verwerfen();

    struct stat ws{};
    if (fstat(kv->wal_fd, &ws) != 0)             return verwerfen();
    if ((uint64_t)ws.st_dev != h->wal_dev)       return verwerfen();
    if ((uint64_t)ws.st_ino != h->wal_ino)       return verwerfen();

    const uint64_t fp = kv_idx_fingerprint((const uint8_t*)kv->wal_mmap,
                                           sizeof(AimlKvWalHeader), (size_t)h->wal_used);
    if (fp != h->wal_fp)                         return verwerfen();

    // Check the layout BEFORE pointers are set.
    const size_t vals_at = sizeof(AimlSidxHeader);
    if (msize < vals_at) return verwerfen();
    if ((size_t)h->val_count > (msize - vals_at) / sizeof(AimlSidxVal)) return verwerfen();
    const size_t keys_at = vals_at + (size_t)h->val_count * sizeof(AimlSidxVal);
    if (keys_at > msize) return verwerfen();
    if ((size_t)h->key_count > (msize - keys_at) / sizeof(AimlSidxKey)) return verwerfen();
    const size_t str_at = keys_at + (size_t)h->key_count * sizeof(AimlSidxKey);
    if (str_at > msize) return verwerfen();

    const AimlSidxVal* vals = (const AimlSidxVal*)((const char*)m + vals_at);
    const AimlSidxKey* keys = (const AimlSidxKey*)((const char*)m + keys_at);
    // Every string must lie in the string block, with its terminating 0;
    // every key window in the key array.  Too large a count would read
    // beyond the end of the file.
    for (uint32_t i = 0; i < h->val_count; i++) {
        const AimlSidxVal& v = vals[i];
        if ((size_t)v.val_off < str_at || (size_t)v.val_off >= msize)            return verwerfen();
        if ((size_t)v.val_len > msize - v.val_off - 1)                           return verwerfen();
        if (((const char*)m)[(size_t)v.val_off + v.val_len] != '\0')             return verwerfen();
        if ((size_t)v.key_first > h->key_count)                                  return verwerfen();
        if ((size_t)v.key_cnt > (size_t)h->key_count - v.key_first)              return verwerfen();
        if (idx->unique && v.key_cnt > 1)                                        return verwerfen();
    }
    for (uint32_t i = 0; i < h->key_count; i++) {
        const AimlSidxKey& ke = keys[i];
        if ((size_t)ke.key_off < str_at || (size_t)ke.key_off >= msize)          return verwerfen();
        if ((size_t)ke.key_len > msize - ke.key_off - 1)                         return verwerfen();
        // finding_by returns this pointer as a C string -- without the 0
        // the caller reads beyond the end (spec/45 "File").
        if (((const char*)m)[(size_t)ke.key_off + ke.key_len] != '\0')           return verwerfen();
    }

    idx->dump       = m;
    idx->dump_size  = msize;
    idx->dvals      = vals;
    idx->dkeys      = keys;
    idx->dval_count = h->val_count;
    idx->loaded     = true;
    return true;
}

// Copy per field value: before EVERY change to a field value, fetch its window from the
// dump into the memory index and mark it as kept there.
// The caller holds idx->mtx.
static void sidx_materialize(AimlIndex* idx, const std::string& k) {
    if (!idx->loaded) return;
    if (!idx->owned.insert(k).second) return;      // already kept in memory
    const AimlSidxVal* v = sidx_find(idx, k);
    if (!v) return;                                 // im Dump unbekannt
    const char* base = (const char*)idx->dump;
    if (idx->unique) {
        if (v->key_cnt > 0) {
            const AimlSidxKey& ke = idx->dkeys[v->key_first];
            idx->map_unique[k].assign(base + ke.key_off, ke.key_len);
        }
        return;
    }
    AimlIndexBucket& b = idx->map_1n[k];
    for (uint32_t i = 0; i < v->key_cnt; i++) {
        const AimlSidxKey& ke = idx->dkeys[v->key_first + i];
        b.add(base + ke.key_off);                   // im Dump 0-terminiert
    }
}

// All live pairs (field value, kv key): the memory index, plus whatever in the
// dump is still responsible.  The same pattern as kv_for_each_live -- and
// the same error if one forgets it: one half silently drops out (0 instead of
// 682593 facts, measured 2026-11-06).  The caller holds idx->mtx.
template <typename F>
static void sidx_for_each(AimlIndex* idx, F&& fn) {
    if (idx->unique) {
        for (auto& kvp : idx->map_unique) fn(kvp.first, kvp.second.data(), (uint32_t)kvp.second.size());
    } else {
        for (auto& kvp : idx->map_1n)
            for (const std::string* sl : kvp.second.slots)
                if (sl) fn(kvp.first, sl->data(), (uint32_t)sl->size());
    }
    if (!idx->loaded) return;
    const char* base = (const char*)idx->dump;
    for (uint32_t i = 0; i < idx->dval_count; i++) {
        const AimlSidxVal& v = idx->dvals[i];
        const std::string val(base + v.val_off, v.val_len);
        if (idx->owned.find(val) != idx->owned.end()) continue;   // Speicher gewinnt
        for (uint32_t j = 0; j < v.key_cnt; j++) {
            const AimlSidxKey& ke = idx->dkeys[v.key_first + j];
            fn(val, base + ke.key_off, ke.key_len);
        }
    }
}

// Write one secondary index file.  Only at checkpoint; the caller holds
// kv->mtx.  If it fails, that is not an error: without a dump the next
// open is slow, not wrong.
static void sidx_write_one(AimlKv* kv, AimlIndex* idx, const std::string& field) {
    if (kv->wal_fd < 0 || !kv->wal_mmap) return;
    std::lock_guard<std::mutex> lk(idx->mtx);

    // Flat layout: one field-value array, plus all keys one after the other.
    // Pointers instead of copies -- the maps and the mapping are under the lock.
    struct Tmp { const char* val; uint32_t val_len; uint32_t first, cnt; };
    std::vector<Tmp> vals;
    std::vector<std::pair<const char*, uint32_t>> keys;
    {
        std::vector<std::vector<std::pair<const char*, uint32_t>>> sammel;
        std::unordered_map<std::string, size_t> nach_wert;
        sidx_for_each(idx, [&](const std::string& val, const char* key, uint32_t klen) {
            auto it = nach_wert.find(val);
            size_t at;
            if (it == nach_wert.end()) {
                at = vals.size();
                nach_wert.emplace(val, at);
                vals.push_back(Tmp{val.data(), (uint32_t)val.size(), 0, 0});
                sammel.emplace_back();
            } else {
                at = it->second;
            }
            sammel[at].emplace_back(key, klen);
        });
        // The field-value pointers must be those of the map keys, not those
        // of the temporary `val` copy from sidx_for_each: there the string only lives
        // until the end of the lambda.  Hence take the names from nach_wert.
        for (auto& nw : nach_wert) {
            vals[nw.second].val     = nw.first.data();
            vals[nw.second].val_len = (uint32_t)nw.first.size();
        }
        size_t total = 0;
        for (auto& sv : sammel) total += sv.size();
        keys.reserve(total);
        for (size_t i = 0; i < vals.size(); i++) {
            vals[i].first = (uint32_t)keys.size();
            vals[i].cnt   = (uint32_t)sammel[i].size();
            for (auto& ks : sammel[i]) keys.push_back(ks);
        }
        // nach_wert holds the field-value strings until the file is written:
        // vals[].val points into it.  That is why this block only ends after
        // writing -- see below.
        if (vals.size() > 0xFFFFFFFFull || keys.size() > 0xFFFFFFFFull) return;

        std::sort(vals.begin(), vals.end(), [](const Tmp& a, const Tmp& b) {
            return kv_idx_cmp(a.val, a.val_len, b.val, b.val_len) < 0;
        });

        // Compute the offsets in advance, so that nothing has to be buffered.
        const size_t vals_at = sizeof(AimlSidxHeader);
        const size_t keys_at = vals_at + vals.size() * sizeof(AimlSidxVal);
        const size_t str_at  = keys_at + keys.size() * sizeof(AimlSidxKey);
        size_t str_bytes = 0;
        for (auto& v : vals)  str_bytes += (size_t)v.val_len + 1;
        for (auto& k : keys)  str_bytes += (size_t)k.second + 1;
        if (str_at + str_bytes > 0xFFFFFFFFull) return;   // 32-Bit-Offsets

        const std::string pfad = sidx_path(kv, field);
        const std::string tmp  = pfad + ".tmp";
        unlink(tmp.c_str());
        int fd = open(tmp.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_TRUNC, 0644);
        if (fd < 0) return;
        struct stat ws{};
        if (fstat(kv->wal_fd, &ws) != 0) { close(fd); unlink(tmp.c_str()); return; }

        std::string buf;
        buf.reserve(1 << 20);
        bool ok = true;
        auto spuel = [&](bool erzwingen) {
            if (!ok) return;
            if (!erzwingen && buf.size() < (1u << 20)) return;
            if (!buf.empty() && write(fd, buf.data(), buf.size()) != (ssize_t)buf.size()) ok = false;
            buf.clear();
        };
        auto roh = [&](const void* p, size_t n) { buf.append((const char*)p, n); spuel(false); };

        AimlSidxHeader h{};
        memcpy(h.magic, KV_SIDX_MAGIC, 8);
        h.version   = KV_SIDX_VERSION;
        h.flags     = sidx_flags(idx->unique, idx->mode);
        h.wal_dev   = (uint64_t)ws.st_dev;
        h.wal_ino   = (uint64_t)ws.st_ino;
        h.wal_used  = kv->wal_used;
        h.wal_fp    = kv_idx_fingerprint((const uint8_t*)kv->wal_mmap,
                                         sizeof(AimlKvWalHeader), (size_t)kv->wal_used);
        h.val_count = (uint32_t)vals.size();
        h.key_count = (uint32_t)keys.size();
        roh(&h, sizeof(h));

        // Feldwert-Eintraege: Stringstellen laufen in Emissionsreihenfolge mit.
        uint32_t voff = (uint32_t)str_at;
        uint32_t run  = 0;
        for (auto& v : vals) {
            AimlSidxVal e{};
            e.val_off   = voff;
            e.val_len   = v.val_len;
            e.key_first = run;
            e.key_cnt   = v.cnt;
            roh(&e, sizeof(e));
            voff += v.val_len + 1;
            run  += v.cnt;
        }
        // Key entries, in the order of the sorted field values.
        uint32_t koff = voff;
        for (auto& v : vals) {
            for (uint32_t i = 0; i < v.cnt; i++) {
                AimlSidxKey ke{};
                ke.key_off = koff;
                ke.key_len = keys[v.first + i].second;
                roh(&ke, sizeof(ke));
                koff += ke.key_len + 1;
            }
        }
        // Stringblock: erst die Feldwerte, dann die Schluessel, jeder mit 0.
        for (auto& v : vals) { roh(v.val, v.val_len); buf.push_back('\0'); spuel(false); }
        for (auto& v : vals)
            for (uint32_t i = 0; i < v.cnt; i++) {
                auto& ks = keys[v.first + i];
                roh(ks.first, ks.second);
                buf.push_back('\0');
                spuel(false);
            }
        spuel(true);
        if (!ok) { close(fd); unlink(tmp.c_str()); return; }
        fsync(fd);
        close(fd);
        // Link in atomically: a half-written dump must never stand under the
        // name.
        if (rename(tmp.c_str(), pfad.c_str()) != 0) unlink(tmp.c_str());
    }
}

// All secondary indexes of this store.  Lock order:
// kv->mtx (caller) -> g_index_registry_mtx -> idx->mtx.
static void kv_sidx_for_all(AimlKv* kv, const std::function<void(AimlIndex*, const std::string&)>& fn) {
    std::lock_guard<std::mutex> lk(g_index_registry_mtx);
    for (auto& e : g_index_registry)
        if (e.first.parent_kv == (void*)kv) fn(e.second, e.first.field_name);
}

static void kv_sidx_write_all(AimlKv* kv) {
    if (!kv || !kv->index_dump) return;
    kv_sidx_for_all(kv, [&](AimlIndex* idx, const std::string& field) {
        sidx_write_one(kv, idx, field);
    });
}

// clear empties the store -- so the secondary indexes must be empty AND their
// files gone.  A dump left lying around would repopulate the emptied store on the
// next open; the stamp would catch it anyway,
// but relying on a check where deletion is unambiguous would be
// careless.
static void kv_sidx_unlink_all(AimlKv* kv) {
    if (!kv) return;
    kv_sidx_for_all(kv, [&](AimlIndex* idx, const std::string& field) {
        std::lock_guard<std::mutex> lk(idx->mtx);
        sidx_detach(idx);
        idx->owned.clear();
        idx->map_1n.clear();
        idx->map_unique.clear();
        const std::string pfad = idx->path.empty() ? sidx_path(kv, field) : idx->path;
        unlink(pfad.c_str());
    });
}

extern "C" void* aiml_db_index_open_for_kv(void* parent_kv, const char* field_name,
                                            int32_t unique, const char* mode) {
    if (!parent_kv || !field_name) return nullptr;
    {
        std::lock_guard<std::mutex> lk(g_index_registry_mtx);
        auto it = g_index_registry.find({parent_kv, field_name});
        if (it != g_index_registry.end()) return it->second;
    }
    auto* idx = new AimlIndex();
    idx->unique = (unique != 0);
    idx->mode   = mode ? mode : "";
    // Load the dump BEFORE the registry lock is taken: loading needs
    // kv->mtx, and the checkpoint holds kv->mtx and then reaches into the
    // registry -- the other order would be a deadlock (spec/45).
    sidx_try_load((AimlKv*)parent_kv, idx, field_name);

    std::lock_guard<std::mutex> lk(g_index_registry_mtx);
    auto ins = g_index_registry.emplace(IndexKey{parent_kv, field_name}, idx);
    if (!ins.second) {                      // lost the race: take the other one
        sidx_detach(idx);
        delete idx;
        return ins.first->second;
    }
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
    sidx_materialize(idx, k);               // spec/45: Kopie je Feldwert
    if (idx->unique) {
        idx->map_unique[k] = kv_key;
    } else {
        idx->map_1n[k].add(kv_key);
    }
}

extern "C" void aiml_db_index_remove(void* idx_ptr, const char* field_val, const char* kv_key) {
    if (!idx_ptr || !field_val || !kv_key) return;
    auto* idx = (AimlIndex*)idx_ptr;
    std::lock_guard<std::mutex> lk(idx->mtx);
    std::string k = idx_normalize(field_val, idx->mode);
    sidx_materialize(idx, k);               // spec/45: Kopie je Feldwert
    if (idx->unique) {
        auto it = idx->map_unique.find(k);
        if (it != idx->map_unique.end() && it->second == kv_key) {
            idx->map_unique.erase(it);
        }
    } else {
        auto it = idx->map_1n.find(k);
        if (it == idx->map_1n.end()) return;
        if (it->second.remove(kv_key)) idx->map_1n.erase(it);
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

    // Pointers to the hits, no copy of the keys — the list is built
    // entry by entry with aiml_str_dup right afterwards anyway.  Graves (spec/45)
    // are skipped, the order stays the insertion order.
    // Pointers to 0-terminated keys -- from the memory index or from
    // the file mapping.  Which route applies is decided by `owned`: a field value
    // is kept either in memory or in the dump, never in both (spec/45).
    std::vector<const char*> hits;
    const bool im_dump = idx->loaded && idx->owned.find(k) == idx->owned.end();
    if (im_dump) {
        const AimlSidxVal* v = sidx_find(idx, k);
        if (v) {
            hits.reserve(v->key_cnt);
            for (uint32_t i = 0; i < v->key_cnt; i++)
                hits.push_back((const char*)idx->dump + idx->dkeys[v->key_first + i].key_off);
        }
    } else if (idx->unique) {
        auto it = idx->map_unique.find(k);
        if (it != idx->map_unique.end()) hits.push_back(it->second.c_str());
    } else {
        auto it = idx->map_1n.find(k);
        if (it != idx->map_1n.end()) {
            hits.reserve(it->second.pos.size());
            for (const std::string* sl : it->second.slots) if (sl) hits.push_back(sl->c_str());
        }
    }
    int32_t n = (int32_t)hits.size();
    if (n == 0) return;
    if (!out->data || out->capacity < n) {
        free(out->data);
        out->data     = (void**)malloc((size_t)n * sizeof(void*));
        out->capacity = n;
    }
    for (int32_t i = 0; i < n; i++) {
        // aiml_str_dup (not libc strdup) so the returned char* is an aiml-managed string with an rc
        // header — the findings list<string> is a memV3 handle whose teardown decrefs each element
        // (libc strdup → no header → invalid read in aiml_str_decref). Survives index mutations.
        out->data[i] = aiml_str_dup(hits[(size_t)i]);
    }
    out->size = n;
}

// Field-type tags used by aiml_db_index_backfill.
// 0=string (char*), 1=i32, 2=i64, 3=list<string>.
enum AimlIndexFieldTag : int32_t {
    AIML_IDX_FT_STRING         = 0,
    AIML_IDX_FT_I32            = 1,
    AIML_IDX_FT_I64            = 2,
    AIML_IDX_FT_LIST_STR       = 3,   // OFF: field IS the inline {size,cap,data} list struct
    AIML_IDX_FT_LIST_STR_HANDLE = 4,  // memV3 (spec/61 §5): field is a POINTER to a list HANDLE
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

    // The dump IS the result of the fill, stamped onto the same
    // WAL bytes (spec/45 "Persistence") -- then there is nothing to do here.
    {
        std::lock_guard<std::mutex> lk(idx->mtx);
        if (idx->loaded) return;
    }

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
                // OFF: `field` IS the inline list struct { i32 size, i32 capacity, char** data }.
                int32_t size = *(const int32_t*)field;
                char* const* data = *(char* const* const*)(field + 8);
                if (!data || size <= 0) return;
                for (int32_t i = 0; i < size; i++) {
                    if (data[i]) aiml_db_index_add(idx, data[i], key.c_str());
                }
                break;
            }
            case AIML_IDX_FT_LIST_STR_HANDLE: {
                // spec/61 §5 (memV3): `field` holds a POINTER to a refcounted list HANDLE
                // [i32 rc][i32 pad][{ i32 size, i32 cap, char** data }]. Deref the handle first; its
                // {size,cap,data} part starts at the handle pointer (8 bytes past the block start, which
                // is exactly what the slot stores). Reading `field` inline would treat the handle
                // pointer's bytes as size/data → garbage (a string's bytes as a pointer → SIGSEGV).
                const uint8_t* handle = *(const uint8_t* const*)field;
                if (!handle) return;
                int32_t size = *(const int32_t*)handle;
                char* const* data = *(char* const* const*)(handle + 8);
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
        kv_for_each_live(kv, [&](const std::string& key, const AimlKvValueLoc& loc) {
            const uint8_t* val_ptr = (const uint8_t*)kv->wal_mmap + loc.offset;
            if (kv->ser_nodes) {
                // spec/31 §6.7.3 rule 2: only reading fills the decoded_cache.  An already
                // decoded entry is SHARED (and not touched), a row decoded here
                // belongs in a buffer of its own and is freed again right away.
                // Previously EVERY row ended up in the cache: when opening a
                // store with N rows, N decoded rows got stuck (the 1024 bound
                // then is none), and the LRU list knows about none of them — afterwards
                // kv_decoded_lru_trim clears the list and frees the slot that the next
                // read returns (cai 2026-11-04).
                // extract_and_add copies the field value and key into the index and keeps
                // no pointers to the row — freeing directly afterwards is safe.
                auto dc = kv->decoded_cache.find(key);
                if (dc != kv->decoded_cache.end()) {
                    extract_and_add(key, dc->second.data());
                    return;   // next entry (lambda, no continue)
                }
                std::vector<uint8_t> decoded((size_t)kv->val_size, 0);
                if (aiml_ser_decode((const char*)val_ptr, decoded.data(),
                                    kv->ser_nodes, kv->ser_n) != 0) {
                    // Corrupt record: free partial heap and SKIP indexing it — do
                    // not feed garbage field pointers to extract_and_add (P0.3).
                    // Partial index (bad rows omitted) rather than a hard abort.
                    aiml_ser_free_decoded(decoded.data(), kv->ser_nodes, kv->ser_n);
                    return;
                }
                extract_and_add(key, decoded.data());
                aiml_ser_free_decoded(decoded.data(), kv->ser_nodes, kv->ser_n);
            } else {
                extract_and_add(key, val_ptr);
            }
        });
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
    // The return value is a C string.  In the dump the keys therefore carry
    // a terminating 0 (spec/45 "File"), and sidx_try_load checks it --
    // otherwise the caller would read beyond the end of the mapping.
    if (idx->loaded && idx->owned.find(k) == idx->owned.end()) {
        const AimlSidxVal* v = sidx_find(idx, k);
        if (v && v->key_cnt > 0)
            return (const char*)idx->dump + idx->dkeys[v->key_first].key_off;
        return empty;
    }
    if (idx->unique) {
        auto it = idx->map_unique.find(k);
        if (it != idx->map_unique.end()) return it->second.c_str();
    } else {
        auto it = idx->map_1n.find(k);
        if (it != idx->map_1n.end()) {
            for (const std::string* sl : it->second.slots) if (sl) return sl->c_str();
        }
    }
    return empty;
}

// ─────────────────────────────────────────────────────────────────────────────
// Insight into the OPEN stores (cosmos doc/spec/99 §99.2)
//
// Only open, persistent stores are shown — a store that nobody has
// opened is not operational state but a file (F99-6).  And only
// METRICS, never contents: a UI that shows KV values will sooner
// or later show a secret (F99-7).
// ─────────────────────────────────────────────────────────────────────────────

extern "C" int32_t aiml_db_kv_open_count(void) {
    std::lock_guard<std::mutex> lock(g_db_mutex);
    return (int32_t)g_persistent_kvs.size();
}

static AimlKv* kv_at_index(int32_t idx) {
    if (idx < 0 || idx >= (int32_t)g_persistent_kvs.size()) return nullptr;
    return g_persistent_kvs[(size_t)idx];
}

extern "C" char* aiml_db_kv_open_name(int32_t idx) {
    std::lock_guard<std::mutex> lock(g_db_mutex);
    AimlKv* kv = kv_at_index(idx);
    const std::string nm = kv ? kv->store_name : std::string();
    char* out = aiml_str_alloc(nm.size());
    if (!nm.empty()) memcpy(out, nm.data(), nm.size());
    return out;
}

extern "C" char* aiml_db_kv_open_path(int32_t idx) {
    std::lock_guard<std::mutex> lock(g_db_mutex);
    AimlKv* kv = kv_at_index(idx);
    const std::string pth = kv ? kv->wal_path : std::string();
    char* out = aiml_str_alloc(pth.size());
    if (!pth.empty()) memcpy(out, pth.data(), pth.size());
    return out;
}

extern "C" int64_t aiml_db_kv_open_wal_used(int32_t idx) {
    std::lock_guard<std::mutex> lock(g_db_mutex);
    AimlKv* kv = kv_at_index(idx);
    return kv ? (int64_t)kv->wal_used : 0;
}

extern "C" int64_t aiml_db_kv_open_entries(int32_t idx) {
    std::lock_guard<std::mutex> lock(g_db_mutex);
    AimlKv* kv = kv_at_index(idx);
    if (!kv) return 0;
    return (int64_t)(kv->persistent ? kv_live_count(kv) : (int32_t)kv->map.size());
}

extern "C" int64_t aiml_db_kv_open_writes(int32_t idx) {
    std::lock_guard<std::mutex> lock(g_db_mutex);
    AimlKv* kv = kv_at_index(idx);
    return kv ? (int64_t)kv->writes : 0;
}

