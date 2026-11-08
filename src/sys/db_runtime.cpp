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

// ── Index-Dump (spec/31 §6.2.1) ─────────────────────────────────────────────
// Der Index liegt sonst nur im Prozess und wird bei jedem open aus der WAL
// aufgebaut.  Gemessen am cai-Wissensgraphen (18,17 Mio Schluessel): 19,5 GB
// RSS und ~5 min je Prozessstart, bei 713 MB Schluesselbytes -- 434 Byte je
// Schluessel, fast alles unordered_map-Aufwand.  Der Dump legt dieselbe
// Information sortiert auf die Platte und wird von dort BENUTZT, nicht geladen.
struct AimlKvIdxHeader {
    char     magic[8];      // "AIMLKVIX"
    uint32_t version;
    uint32_t wal_version;   // version der zugehoerigen WAL
    int64_t  val_size;      // val_size der zugehoerigen WAL
    uint64_t wal_used;      // Wasserstand: Stand der WAL beim Schreiben
    uint64_t wal_dev;       // st_dev der WAL beim Schreiben
    uint64_t wal_ino;       // st_ino der WAL beim Schreiben
    uint64_t wal_fp;        // Fingerabdruck des beschriebenen Bereichs
    uint64_t count;         // Zahl der Eintraege
};
static_assert(sizeof(AimlKvIdxHeader) == 64, "AimlKvIdxHeader must be 64 bytes");

// 24 Byte, natuerlich ausgerichtet -- kein pack(1), damit kein unausgerichteter
// 64-Bit-Zugriff entsteht.  Eintraege stehen nach Schluesselbytes sortiert,
// darum ist die Suche eine binaere Suche im mmap ohne jeden Aufbau.
struct AimlKvIdxEntry {
    uint64_t val_off;       // Offset des Werts in der WAL
    uint32_t key_off;       // Offset im Schluesselblock
    uint32_t val_len;
    uint32_t key_len;
    uint32_t pad;
};
static_assert(sizeof(AimlKvIdxEntry) == 24, "AimlKvIdxEntry must be 24 bytes");

static const char     KV_IDX_MAGIC[8] = {'A','I','M','L','K','V','I','X'};
static const uint32_t KV_IDX_VERSION  = 1;
// Grabstein im Ueberlauf-Index: verdeckt einen Dump-Eintrag.  Ohne das kaeme
// ein geloeschter Schluessel nach dem Neustart aus dem Dump zurueck.
static const uint64_t KV_IDX_TOMB     = UINT64_MAX;

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
    // ── Einblick fuer die Betriebssicht (cosmos doc/spec/99 F99-5) ──────────
    // Name und ein SCHREIBZAEHLER.  Bewusst ein Zaehler und kein Zeitstempel:
    // ein Zeitstempel kostet je Schreibvorgang einen Uhrzugriff auf dem heissen
    // Pfad, waehrend die Sicht ohnehin zwei Schnappschuesse vergleicht — die
    // Differenz sagt "bewegt sich", der Schnappschuss-Zeitpunkt sagt "wie alt".
    std::string store_name;
    uint64_t    writes = 0;
    // ── Index-Dump (spec/31 §6.2.1) ────────────────────────────────────────
    bool        index_dump = false;      // Option beim open
    std::string idx_path;                // <name>.kvidx
    void*       idx_mmap      = nullptr; // nur lesend gemappt
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

// `mkdir -p` fuer den aufgeloesten Store-Pfad.
//
// Einstufiges `mkdir` genuegt nicht: spec/31 erlaubt `path: "sub/dir/"`
// ausdruecklich (Beispiel dort: `runs/trajectory.kvwal`), und ein solcher
// Unterbau existiert beim ersten Oeffnen noch nicht.  Das einstufige mkdir
// scheiterte dann mit ENOENT, der open-Pfad lief weiter, und der Store blieb
// still leer — gefunden 2026-10-15 an cosmos' `cas_action_test` unter einem
// frischen AIONDB_PATH.
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
            mkdir(acc.c_str(), 0755);   // existiert schon → EEXIST, egal
        }
        if (sl == std::string::npos) break;
        i = sl + 1;
    }
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
//
// spec/31 §6.7.3 Regel 1: der MRU-Eintrag bleibt stehen — das ist der Slot, den der
// laufende `get`/`get_copy` gerade angelegt hat und zurueckgeben wird.  `size() > 1`
// statt `!empty()`: lief der Cache einmal ueber die Grenze hinaus, ohne dass die
// LRU-Liste davon weiss, kommt die Schleife durch Leerraeumen der Liste NIE unter den
// Bound — sie nahm dann die ganze Liste mit, den gerade eingefuegten Eintrag zuletzt,
// und der Aufrufer bekam einen Zeiger in einen freigegebenen Slot (cai 2026-11-04:
// SIGSEGV in enc <- aiml_ser_deep_copy, strlen auf einen released String).
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
    int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
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
// Nebenindex-Dumps (spec/45 "Persistence").  Liegen beim Index-Code weiter
// unten, werden aber von hier oben gerufen -- Checkpoint schreibt sie, clear
// loescht sie.  Beide Aufrufer halten kv->mtx.
static void kv_sidx_write_all(AimlKv* kv);
static void kv_sidx_unlink_all(AimlKv* kv);

// ── Index-Dump: Lesen (spec/31 §6.2.1) ─────────────────────────────────────

// Lexikographisch wie std::string: gemeinsamer Teil, dann die Laenge.  Muss mit
// der Sortierung beim Schreiben uebereinstimmen, sonst findet die binaere Suche
// nichts -- und zwar still.
static inline int kv_idx_cmp(const char* a, size_t alen, const char* b, size_t blen) {
    const size_t n = alen < blen ? alen : blen;
    const int c = n ? memcmp(a, b, n) : 0;
    if (c) return c;
    if (alen == blen) return 0;
    return alen < blen ? -1 : 1;
}

// Fingerabdruck ueber GENAU den Bereich, den der Dump beschreibt.  Anhaengen
// liegt dahinter und aendert ihn nicht -- nur so bleibt der Ueberlauf-Pfad
// nutzbar.  Deterministisch und ganzzahlig, damit Schreiben und Pruefen auf
// jeder Maschine denselben Wert ergeben.
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

// Dump pruefen und mappen.  Gibt den Wasserstand zurueck, ab dem die WAL noch
// nachgespielt werden muss, oder 0, wenn kein brauchbarer Dump vorliegt.  Der
// Dump ist NIE maßgebend gegenueber der WAL: jeder Zweifel kostet einen vollen
// Replay, nie falsche Daten.
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

    // WAL-Version aus deren Header lesen.
    const AimlKvWalHeader* wh = (const AimlKvWalHeader*)kv->wal_mmap;
    if (h->wal_version != wh->version)               return verwerfen();

    // Dateiidentitaet: der Checkpoint schreibt die WAL als Temp-Datei und zieht
    // sie per rename herueber, die Inode wechselt also genau dann, wenn sich
    // die Offsets verschieben.
    struct stat ws{};
    if (fstat(kv->wal_fd, &ws) != 0)                 return verwerfen();
    if ((uint64_t)ws.st_dev != h->wal_dev)           return verwerfen();
    if ((uint64_t)ws.st_ino != h->wal_ino)           return verwerfen();

    // Inhalt: faellt auch auf `cp fremd ziel`, das die Inode des Ziels behaelt.
    const uint64_t fp = kv_idx_fingerprint((const uint8_t*)kv->wal_mmap,
                                           sizeof(AimlKvWalHeader), (size_t)h->wal_used);
    if (fp != h->wal_fp)                             return verwerfen();

    // Aufbau pruefen, BEVOR Zeiger gesetzt werden: Eintragsfeld und
    // Schluesselblock muessen in die Datei passen.  Ein zu kleines count waere
    // harmlos, ein zu grosses laese hinter dem Dateiende.
    const uint64_t count  = h->count;
    const size_t   arr_at = sizeof(AimlKvIdxHeader);
    if (msize < arr_at) return verwerfen();
    if (count > (msize - arr_at) / sizeof(AimlKvIdxEntry)) return verwerfen();
    const size_t keys_at = arr_at + (size_t)count * sizeof(AimlKvIdxEntry);
    if (keys_at > msize) return verwerfen();

    const AimlKvIdxEntry* ents = (const AimlKvIdxEntry*)((const char*)m + arr_at);
    const char* keys = (const char*)m + keys_at;
    const size_t keys_len = msize - keys_at;
    // Jeder Schluessel muss im Block liegen, jeder Wert in der WAL.
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

// Binaere Suche im gemappten Dump -- kein Aufbau, keine Allokation.
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

// Ueberlauf zuerst, dann Dump.  Ein Grabstein im Ueberlauf verdeckt den Dump.
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

// ── Index-Dump: Schreiben (spec/31 §6.2.1) ─────────────────────────────────
// Nur bei checkpoint.  Dort wird die WAL neu geschrieben, alle Offsets aendern
// sich.  `items` traegt ALLE lebenden Eintraege; die Zeiger muessen bis zur
// Rueckkehr leben.  Rueckgabe true = die Datei steht.  false heisst nicht
// "Daten verloren", sondern "kein Dump": der naechste Start ist langsam, nicht
// falsch -- der Aufrufer muss dann aber den Ueberlauf tragen (spec/31 §6.2.1,
// "Nach dem Checkpoint gibt der Store den RAM-Index her", Rueckfall).
static bool kv_idx_write_items(
        AimlKv* kv,
        std::vector<std::pair<std::string, AimlKvValueLoc>>& items) {
    if (!kv->index_dump || kv->idx_path.empty()) return false;
    if (kv->wal_fd < 0 || !kv->wal_mmap) return false;

    size_t key_bytes = 0;
    for (auto& it : items) key_bytes += it.first.size();
    // key_off ist 32 Bit.  Bei mehr als 4 GiB Schluesselbytes gibt es keinen
    // Dump -- lieber kein Dump als ein stiller Ueberlauf.
    if (key_bytes > 0xFFFFFFFFull) return false;
    // An der Liste selbst sortiert, nicht an einer Zeigerliste daneben.
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

    // Eintraege, danach der Schluesselblock in derselben Reihenfolge.  Beides
    // laeuft durch einen 1-MiB-Puffer: ein eigener Eintragsvektor waere bei
    // Millionen Schluesseln eine dritte Kopie derselben Liste, und genau diese
    // Kopien soll §6.2.1 "Nach dem Checkpoint" einsparen.
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
    // Atomar einhaengen, wie der Checkpoint die WAL: ein halb geschriebener
    // Dump darf nie unter dem Namen stehen.
    if (rename(tmp.c_str(), kv->idx_path.c_str()) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    return true;
}


// ALLE lebenden Eintraege: Ueberlauf (ohne Grabsteine), dann Dump (ohne die vom
// Ueberlauf verdeckten).  Wer ueber einen persistenten Kv laeuft, MUSS das hier
// benutzen und nicht kv->index: mit Dump steht im Ueberlauf nur, was seit dem
// letzten Checkpoint geschrieben wurde -- bei einem frisch geoeffneten Store
// also nichts.  Genau daran ist das Auffuellen der Nebenindizes zuerst
// gescheitert (0 statt 682593 Fakten, gemessen 06.11.2026).
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

// Lebende Eintraege zaehlen.  Ohne Dump ist es die Groesse des Ueberlaufs, mit
// Dump dessen Zahl, korrigiert um die Eintraege, die der Ueberlauf verdeckt
// oder hinzufuegt.
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

// Rueckfallweg: Index per read() aufbauen, wenn kein mmap vorliegt.
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

// Index aus der WAL aufbauen.  Die Datei ist hier schon gemappt -- alle drei
// Aufrufstellen rufen kv_mmap_attach davor --, darum laeuft der Aufbau ueber den
// mmap-Zeiger.  Bis 06.11.2026 las diese Schleife jedes Feld mit eigenem read()
// plus lseek: fuenf Syscalls je Satz, bei 5,8 Mio Saetzen eines cai-Teilgraphen
// rund 29 Mio Syscalls fuer 63 s Startzeit -- fuer Bytes, die bereits im
// Adressraum lagen.  Gemessen war das die GESAMTE Abfragezeit: `graph stats`,
// `ask` und ein Namensschlag lagen auf demselben Wert.
static void kv_wal_replay(AimlKv* kv, size_t from) {
    const uint8_t* base = (const uint8_t*)kv->wal_mmap;
    if (!base || kv->wal_mmap_size <= sizeof(AimlKvWalHeader)) {
        kv_wal_replay_fd(kv);
        return;
    }
    const size_t n = kv->wal_mmap_size;
    // from > 0: ein brauchbarer Dump deckt alles davor ab, nachgespielt wird
    // nur der Rest (spec/31 §6.2.1).
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
                // Dump aktiv: der Schluessel kann dort stehen.  Loeschen im
                // Ueberlauf wuerde ihn nach dem Neustart wieder hervorholen --
                // es braucht einen Grabstein, der den Dump verdeckt.
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
            break;  // unbekannter op -- auch der Nullbereich einer sparse-Datei
        }
        last_good = p;
    }
    kv->wal_used = (uint64_t)last_good;
}

// Getrennte _idx-Einsprungpunkte statt geaenderter Signatur: getFunction im
// Compiler prueft Signaturen NICHT (erster Deklarierer gewinnt), und
// aiml_db_kv_open_persist wird auch in tensor_runtime.cpp deklariert.
static void* kv_open_persist_impl(const char* name, int64_t val_size,
                                  const char* base_path, bool index_dump) {
    std::string dir = resolve_aiondb_dir(base_path);
    std::string nm  = name ? name : "kv";
    std::string registry_key = dir + nm;

    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto it = g_persistent_kvs_by_name.find(registry_key);
    if (it != g_persistent_kvs_by_name.end()) return it->second;

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
        // spec/31 §6.2.1: liegt ein brauchbarer Dump vor, wird nur der WAL-Rest
        // ab seinem Wasserstand nachgespielt -- sonst alles.
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
    // spec/31 §6.2.1: die Dump-Eintraege zeigen ebenfalls in old_mmap und
    // muessen mit umkopiert werden.  Wuerde der Checkpoint nur den Ueberlauf
    // durchlaufen, verlore er alles, was NUR im Dump steht -- also fast alles.
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

    // Mit Dump sammeln die neuen Orte in einer LISTE statt im Ueberlauf: grob
    // 60 Byte je Schluessel gegen 434 in der unordered_map, und der Ueberlauf
    // bleibt danach leer (spec/31 §6.2.1, "Nach dem Checkpoint gibt der Store
    // den RAM-Index her").  Ohne Dump bleibt der Ueberlauf der Index.
    const bool want_dump = kv->index_dump && !kv->idx_path.empty();
    std::vector<std::pair<std::string, AimlKvValueLoc>> frisch;
    if (want_dump) frisch.reserve((size_t)old_count + old_index.size());

    // 4. Copy each record from old mmap → temp via kv_wal_write_* (uses memcpy
    //    into temp_mmap; kv_mmap_grow handles overflow within temp).
    //    Zuerst der Dump, ohne die vom Ueberlauf verdeckten Schluessel; dann der
    //    Ueberlauf ohne Grabsteine.  Jeder Schluessel landet so genau einmal.
    for (uint64_t i = 0; i < old_count; i++) {
        const AimlKvIdxEntry& e = old_ents[i];
        std::string k(old_keys + e.key_off, e.key_len);
        if (old_index.find(k) != old_index.end()) continue;  // Wert oder Grabstein im Ueberlauf
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
        if (old_loc.offset == KV_IDX_TOMB) continue;   // Grabstein: nichts zu schreiben
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
    // Der alte Ueberlauf ist abgeschrieben.  Mit Dump senkt das die Spitze um
    // genau den Posten, den dieser Abschnitt sparen will.
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
    // 8. Alten Dump freigeben (seine Offsets zeigen in die eben geloeschte
    //    Datei) und den neuen schreiben -- spec/31 §6.2.1 schreibt den Dump
    //    ausschliesslich hier, weil hier die Offsets entstehen.  Erst NACH dem
    //    rename, damit fstat die endgueltige Inode liefert.
    if (old_idx_mmap) munmap(old_idx_mmap, old_idx_mmap_size);
    if (want_dump) {
        const bool steht = kv_idx_write_items(kv, frisch);
        // Erst laden, dann erst darf der Ueberlauf leer bleiben.  Laedt der
        // frische Dump NICHT, muss der Ueberlauf aus der Liste aufgefuellt
        // werden, bevor die Sperre faellt -- ein Store mit leerem Ueberlauf und
        // ohne gueltigen Dump haette seinen ganzen Inhalt verloren, ohne dass
        // eine Zeile fehlt (spec/31 §6.2.1, Rueckfall).
        if (!steht || kv_idx_try_load(kv) != kv->wal_used) {
            for (auto& p : frisch) kv->index[std::move(p.first)] = p.second;
        }
    }
    // 9. Und die Nebenindizes (spec/45 "Geschrieben wird beim Checkpoint") --
    //    mit dem Stempel der eben umbenannten WAL, also nach dem Primaerindex.
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
    // spec/31 §6.2.1: liegt ein brauchbarer Dump vor, nur den Rest ab seinem
    // Wasserstand -- genau der Fall, fuer den reload gedacht ist (ein anderer
    // Prozess hat angehaengt, die Inode ist dieselbe, der Dump bleibt gueltig).
    kv_wal_replay(kv, kv_idx_try_load(kv));
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

static void* kv_open_typed_persist_impl(const char* name, int64_t val_size,
                                        const char* base_path,
                                        const AimlTypeNode* nodes, int32_t n_nodes,
                                        bool index_dump) {
    std::string dir = resolve_aiondb_dir(base_path);
    std::string nm  = name ? name : "kv";
    std::string registry_key = dir + nm;

    std::lock_guard<std::mutex> lock(g_db_mutex);
    auto it = g_persistent_kvs_by_name.find(registry_key);
    if (it != g_persistent_kvs_by_name.end()) return it->second;

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
        // spec/31 §6.2.1: liegt ein brauchbarer Dump vor, wird nur der WAL-Rest
        // ab seinem Wasserstand nachgespielt -- sonst alles.
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
    ++kv->writes;   // doc/spec/99 F99-5: Zaehler, kein Zeitstempel (kein Uhrzugriff)
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
                // Container-Handles wiederherstellen — ein genulltes Feld ist nie
                // gueltig (siehe get_copy_at).  Ohne das stuerzt der erste `.len` ab.
                aiml_ser_init_empty_containers(decoded.data(), kv->ser_nodes, kv->ser_n);
            }
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
                // Container-Handles wiederherstellen — ein genulltes Feld ist nie
                // gueltig (siehe get_copy_at).  Ohne das stuerzt der erste `.len` ab.
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
            // spec/31 §6.2.1: steht der Schluessel im Dump, muss ein Grabstein
            // ihn verdecken -- ein erase im Ueberlauf wuerde ihn beim naechsten
            // Lookup aus dem Dump zurueckholen.
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
    // spec/31 §6.2.1: ein liegengebliebener Dump wuerde den geleerten Store
    // beim naechsten Oeffnen wieder bevoelkern.  Der Fingerabdruck faellt zwar
    // ohnehin darauf, aber sich auf eine Pruefung zu verlassen, wo Loeschen
    // eindeutig ist, waere nachlaessig.
    if (kv->index_dump) {
        kv_idx_detach(kv);
        if (!kv->idx_path.empty()) unlink(kv->idx_path.c_str());
    }
    // Die Nebenindizes gehoeren zum Store: ein geleerter Store darf keine
    // Treffer mehr liefern (spec/45).  Gilt auch ohne index_dump -- dann ist
    // nur nichts zu loeschen.
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
    // ── Index-Dump (spec/31 §6.2.1) ────────────────────────────────────────
    // Mit Dump laeuft erst der Ueberlauf, dann der Dump ohne die von ihm
    // verdeckten Schluessel.  Dump-Schluessel stehen im mmap OHNE \0, ein
    // Zeiger darauf waere keine gueltige C-Zeichenkette -- darum je Schritt
    // eine Kopie.  Ohne Dump bleibt der alte Weg unberuehrt, damit sich die
    // Haltbarkeit der zurueckgegebenen Zeiger nicht aendert.
    bool        dump_path = false;
    uint64_t    dump_pos  = 0;
    bool        have      = false;
    std::string cur_key;   // naechster Schluessel
    std::string ret_key;   // zuletzt zurueckgegebener, bis zum naechsten Aufruf
};

// Naechsten lebenden Schluessel bereitstellen: Ueberlauf vor Dump.
static void iter_kv_advance(AimlIterator* it) {
    it->have = false;
    AimlKv* kv = it->kv;
    while (it->idx_cur != it->idx_end) {
        const std::string key  = it->idx_cur->first;
        const uint64_t    off  = it->idx_cur->second.offset;
        ++it->idx_cur;
        if (off == KV_IDX_TOMB) continue;   // Grabstein ist kein Eintrag
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

// Ein Bucket des 1:N-Index: die kv-Schluessel in EINFUEGEREIHENFOLGE, dazu die
// Stelle jedes Schluessels darin.  spec/45 "Cost of index maintenance": add und
// remove sind O(1) amortisiert, unabhaengig von der Bucketgroesse.
//
// Vorher war der Bucket ein nackter vector<string>: `add` suchte ihn mit
// std::find nach Duplikaten ab, `remove` verschob ihn mit std::remove — beides
// O(Bucket) je kv.set.  Ein Bucket haelt alle Zeilen, die EINEN Feldwert
// tragen, also waechst er bei einem Feld mit wenigen verschiedenen Werten auf
// die Groesse des Speichers, und ein Schreibstapel wurde quadratisch: je 4000
// set 487 ms bei 4k Zeilen und 4608 ms bei 24k (cai 2026-11-04, deutsche
// Wikipedia: ein 69-s-Stapel war nach 600 s nicht fertig).
//
// `remove` setzt ein Grab statt zu verschieben und verdichtet erst, wenn die
// Graeber die lebenden Eintraege ueberwiegen — damit bleibt die Reihenfolge
// erhalten, und das Verdichten kostet amortisiert O(1).  Ein Grab ist nicht am
// leeren Schluessel erkennbar ("" ist ein zulaessiger kv-Schluessel), sondern
// an seiner eigenen Marke.
struct AimlIndexBucket {
    // `pos` BESITZT die Schluessel, `slots` zeigt auf seine Knoten: die Knoten
    // einer unordered_map sind stabil (ein Rehash verschiebt sie nicht), und so
    // liegt jeder Schluessel nur EINMAL im Speicher.  Ein Grab ist nullptr —
    // am leeren Schluessel waere es nicht erkennbar, "" ist ein zulaessiger
    // kv-Schluessel.  Wer einen Slot raeumt, raeumt ihn VOR dem pos.erase.
    std::vector<const std::string*>         slots;  // Einfuegereihenfolge
    std::unordered_map<std::string, size_t> pos;    // Schluessel → Stelle in slots
    size_t graves = 0;

    void add(const char* kv_key) {
        auto [it, fresh] = pos.emplace(kv_key, slots.size());
        if (!fresh) return;                         // schon drin — der Index ist eine Menge
        slots.push_back(&it->first);
    }
    // true, wenn der Bucket danach leer ist (der Aufrufer loescht ihn dann)
    bool remove(const char* kv_key) {
        auto it = pos.find(kv_key);
        if (it == pos.end()) return pos.empty();
        slots[it->second] = nullptr;                // vor dem erase, sonst haengt der Zeiger
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
        // Nur vorhandene Schluessel neu beschriften — kein insert, also kein
        // Rehash, also bleiben die Knoten gueltig, auf die slots zeigt.
        for (size_t i = 0; i < slots.size(); ++i) pos[*slots[i]] = i;
        graves = 0;
    }
};

// ── Nebenindex-Dump: Format (spec/45 "Persistence") ───────────────────────
// Eine Datei je (Store, Feld).  Die Eintraege sind nach dem NORMALISIERTEN
// Feldwert sortiert -- genau danach sucht die Abfrage, und Schreiben und Suchen
// muessen dieselbe Ordnung benutzen (kv_idx_cmp), sonst findet die binaere
// Suche nichts, und zwar still.
static const char     KV_SIDX_MAGIC[8] = {'A','I','M','L','K','V','S','X'};
static const uint32_t KV_SIDX_VERSION  = 1;
static const uint32_t KV_SIDX_F_UNIQUE = 1u << 0;
static const uint32_t KV_SIDX_F_LOWER  = 1u << 1;

struct AimlSidxHeader {     // 64 Byte, natuerlich ausgerichtet
    char     magic[8];
    uint32_t version;
    uint32_t flags;         // unique / lower -- muss zur Deklaration passen
    uint64_t wal_dev;
    uint64_t wal_ino;
    uint64_t wal_used;      // Stempel der Haupt-WAL, wie spec/31 §6.2.1
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

    // Nebenindex-Dump (spec/45 "Persistence").  Solange `loaded` steht,
    // beantwortet die Datei-Abbildung jeden Feldwert, der NICHT in `owned`
    // steht; fuer die in `owned` gilt nur noch der Speicher-Index.  Nie beide
    // zugleich -- sonst kaeme ein Schluessel doppelt oder gar nicht.
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

// <dir>/<name>__<feld>.kvsidx, abgeleitet aus dem WAL-Pfad.
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

// Binaere Suche im gemappten Dump -- kein Aufbau, keine Allokation.
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

// Dump pruefen und mappen.  Nimmt kv->mtx, darf also NICHT unter der
// Registry-Sperre laufen (der Checkpoint haelt kv->mtx und greift dann in die
// Registry -- die andere Reihenfolge waere eine Verklemmung).
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
    // Aus `db_indexed: true` ein `unique` oder ein "lower" gemacht: der alte
    // Dump beschreibt eine andere Ordnung und ist unbrauchbar.
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

    // Aufbau pruefen, BEVOR Zeiger gesetzt werden.
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
    // Jeder String muss im Stringblock liegen, mit seiner abschliessenden 0;
    // jedes Schluesselfenster im Schluesselfeld.  Ein zu grosses count laese
    // hinter dem Dateiende.
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
        // finding_by gibt diesen Zeiger als C-String zurueck -- ohne die 0
        // liest der Aufrufer hinter das Ende (spec/45 "Datei").
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

// Kopie je Feldwert: vor JEDER Aenderung an einem Feldwert sein Fenster aus dem
// Dump in den Speicher-Index holen und ihn als dort gefuehrt vermerken.
// Aufrufer haelt idx->mtx.
static void sidx_materialize(AimlIndex* idx, const std::string& k) {
    if (!idx->loaded) return;
    if (!idx->owned.insert(k).second) return;      // schon im Speicher gefuehrt
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

// Alle lebenden Paare (Feldwert, kv-Schluessel): Speicher-Index, dazu was im
// Dump noch zustaendig ist.  Dasselbe Muster wie kv_for_each_live -- und
// derselbe Fehler, wenn man es vergisst: eine Haelfte faellt still weg (0 statt
// 682593 Fakten, gemessen 06.11.2026).  Aufrufer haelt idx->mtx.
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

// Eine Nebenindex-Datei schreiben.  Nur beim Checkpoint; Aufrufer haelt
// kv->mtx.  Scheitert es, ist das kein Fehler: ohne Dump ist das naechste
// Oeffnen langsam, nicht falsch.
static void sidx_write_one(AimlKv* kv, AimlIndex* idx, const std::string& field) {
    if (kv->wal_fd < 0 || !kv->wal_mmap) return;
    std::lock_guard<std::mutex> lk(idx->mtx);

    // Flaches Layout: ein Feldwert-Feld, dazu alle Schluessel hintereinander.
    // Zeiger statt Kopien -- die Maps und die Abbildung stehen unter der Sperre.
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
        // Die Feldwert-Zeiger muessen die der Map-Schluessel sein, nicht die
        // der temporaeren `val`-Kopie aus sidx_for_each: dort lebt der String
        // nur bis zum Lambda-Ende.  Darum die Namen aus nach_wert nehmen.
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
        // nach_wert haelt die Feldwert-Strings, bis die Datei geschrieben ist:
        // vals[].val zeigt hinein.  Darum endet dieser Block erst nach dem
        // Schreiben -- siehe unten.
        if (vals.size() > 0xFFFFFFFFull || keys.size() > 0xFFFFFFFFull) return;

        std::sort(vals.begin(), vals.end(), [](const Tmp& a, const Tmp& b) {
            return kv_idx_cmp(a.val, a.val_len, b.val, b.val_len) < 0;
        });

        // Offsets vorab rechnen, damit nichts gepuffert werden muss.
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
        // Schluessel-Eintraege, in der Reihenfolge der sortierten Feldwerte.
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
        // Atomar einhaengen: ein halb geschriebener Dump darf nie unter dem
        // Namen stehen.
        if (rename(tmp.c_str(), pfad.c_str()) != 0) unlink(tmp.c_str());
    }
}

// Alle Nebenindizes dieses Stores.  Reihenfolge der Sperren:
// kv->mtx (Aufrufer) -> g_index_registry_mtx -> idx->mtx.
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

// clear leert den Store -- damit muessen die Nebenindizes leer sein UND ihre
// Dateien weg.  Ein liegengebliebener Dump wuerde den geleerten Store beim
// naechsten Oeffnen wieder bevoelkern; der Stempel faellt zwar ohnehin darauf,
// aber sich auf eine Pruefung zu verlassen, wo Loeschen eindeutig ist, waere
// nachlaessig.
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
    // Dump laden, BEVOR die Registry-Sperre genommen wird: das Laden braucht
    // kv->mtx, und der Checkpoint haelt kv->mtx und greift dann in die
    // Registry -- andere Reihenfolge waere eine Verklemmung (spec/45).
    sidx_try_load((AimlKv*)parent_kv, idx, field_name);

    std::lock_guard<std::mutex> lk(g_index_registry_mtx);
    auto ins = g_index_registry.emplace(IndexKey{parent_kv, field_name}, idx);
    if (!ins.second) {                      // Rennen verloren: den fremden nehmen
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

    // Zeiger auf die Treffer, keine Kopie der Schluessel — die Liste wird gleich
    // ohnehin Eintrag fuer Eintrag mit aiml_str_dup aufgebaut.  Graeber (spec/45)
    // werden uebersprungen, die Reihenfolge bleibt die Einfuegereihenfolge.
    // Zeiger auf 0-terminierte Schluessel -- aus dem Speicher-Index oder aus
    // der Datei-Abbildung.  Welcher Weg gilt, entscheidet `owned`: ein Feldwert
    // ist entweder im Speicher gefuehrt oder im Dump, nie in beidem (spec/45).
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

    // Der Dump IST das Ergebnis des Auffuellens, gestempelt auf dieselben
    // WAL-Bytes (spec/45 "Persistence") -- dann gibt es hier nichts zu tun.
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
                // spec/31 §6.7.3 Regel 2: nur Lesen fuellt den decoded_cache.  Ein schon
                // dekodierter Eintrag wird MITBENUTZT (und nicht angefasst), eine selbst
                // dekodierte Zeile gehoert in einen eigenen Puffer und wird gleich wieder
                // freigegeben.  Vorher landete JEDE Zeile im Cache: beim Oeffnen eines
                // Speichers mit N Zeilen haengen N dekodierte Zeilen fest (der 1024er-Bound
                // ist dann keiner), und die LRU-Liste weiss von keiner davon — danach raeumt
                // kv_decoded_lru_trim die Liste leer und gibt den Slot frei, den der naechste
                // Lesevorgang zurueckgibt (cai 2026-11-04).
                // extract_and_add kopiert Feldwert und Schluessel in den Index und behaelt
                // keine Zeiger auf die Zeile — direkt danach freigeben ist sicher.
                auto dc = kv->decoded_cache.find(key);
                if (dc != kv->decoded_cache.end()) {
                    extract_and_add(key, dc->second.data());
                    return;   // naechster Eintrag (Lambda, kein continue)
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
    // Der Rueckgabewert ist ein C-String.  Im Dump tragen die Schluessel darum
    // eine abschliessende 0 (spec/45 "Datei"), und sidx_try_load prueft sie --
    // sonst laese der Aufrufer hinter das Ende der Abbildung.
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
// Einblick in die GEOEFFNETEN Speicher (cosmos doc/spec/99 §99.2)
//
// Gezeigt werden nur offene, persistente Speicher — ein Speicher, den niemand
// geoeffnet hat, ist kein Betriebszustand, sondern eine Datei (F99-6).  Und nur
// KENNZAHLEN, nie Inhalte: eine Oberflaeche, die KV-Werte zeigt, zeigt frueher
// oder spaeter ein Geheimnis (F99-7).
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

