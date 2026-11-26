#pragma once
#include <cstdint>
#include "ser_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Global path configuration ────────────────────────────────────────────────
// Sets the global AionDB root. Subsequent open calls with a relative
// `base_path` append their sub-path to this root.
//
// Resolution order:
//   1. AIONDB_PATH env var (always dominates, re-read on every open)
//   2. value set via aiml_db_set_path()
//   3. fallback "./aiondb/"
//
// Sub-path argument of the open functions:
//   - nullptr/empty   → use the root directly
//   - absolute (/...) → as-is (backward compat for tests with /tmp/aiml_test_*)
//   - relative         → root + sub
void    aiml_db_set_path(const char* path);

// Returns the currently resolved AionDB root with a trailing slash.
// Resolution order as for every open call:
//   1. AIONDB_PATH env (live re-read)
//   2. value last set via aiml_db_set_path()
//   3. fallback "./aiondb/"
// Return: heap-allocated char* (caller-owned; the AIML runtime manages the lifetime
// implicitly, the function is rarely called, no pool needed).
const char* aiml_db_get_path(void);

// ── Store<T> ──────────────────────────────────────────────────────────────────
void*   aiml_db_store_open(const char* name, const char* type_name, int64_t elem_size);
int32_t aiml_db_store_insert(void* store, const void* elem, int64_t elem_size);
int32_t aiml_db_store_update(void* store, int32_t id, const void* elem, int64_t elem_size);
void*   aiml_db_store_get(void* store, int32_t id);
// get_by_key: linear scan for elem where field at key_offset matches key_ptr.
// is_string=1: strcmp; is_string=0: memcmp(key_size bytes).
void*   aiml_db_store_get_by_key(void* store, const void* key_ptr,
                                  int32_t key_offset, int32_t key_size, int32_t is_string);
int32_t aiml_db_store_delete(void* store, int32_t id);
int32_t aiml_db_store_len(void* store);

// ── Store<T> — Persistent (WAL + mmap) ───────────────────────────────────────
// base_path: directory for .db/.wal files (e.g. "./aiondb/")
void*   aiml_db_store_open_persist(const char* name, const char* type_name,
                                    int64_t elem_size, const char* base_path);
void*   aiml_db_store_open_typed_persist(const char* name, const char* type_name,
                                          int64_t elem_size, const char* base_path,
                                          const AimlTypeNode* nodes, int32_t n_nodes);
// checkpoint: msync pages + truncate WAL
int32_t aiml_db_store_checkpoint(void* store);
// checkpoint_all: checkpoint every persistent store opened in this session
int32_t aiml_db_checkpoint_all(void);

// ── Store<T> — Iteration helpers (for db.run_system / db.query) ───────────────
// total:     number of slots ever allocated (1-based loop bound for run_system)
// is_active: 1 if slot (1-based) is occupied, 0 otherwise
// get_ptr:   direct pointer to slot data
int32_t aiml_db_store_total(void* store);
int32_t aiml_db_store_is_active(void* store, int32_t slot);
void*   aiml_db_store_get_ptr(void* store, int32_t slot);

// query: iterate store, apply optional predicate, collect results into AimlListRaw.
// pred_fn: (int32_t)(const void*) — NULL = no filter.
// ord_off/ord_sz/ord_dir: field offset/size/direction for ordering (-1 = no order).
// limit/offset: -1/0 = unlimited/no skip.
void    aiml_db_store_query(void* store, void* pred_fn,
                             int32_t ord_off, int32_t ord_sz, int32_t ord_dir,
                             int32_t limit, int32_t offset,
                             void* result_list);

// ── Store<T> — Column Layout ──────────────────────────────────────────────────
// n_fields:    number of struct fields
// field_sizes: array of sizeof per field (compile-time constant, n_fields entries)
// elem_size:   total sizeof(T)
void*   aiml_db_store_open_col(const char* name, const char* type_name,
                                int32_t n_fields, const int32_t* field_sizes,
                                int64_t elem_size);

// ── Kv<K:V> ───────────────────────────────────────────────────────────────────
void*   aiml_db_kv_open(const char* name, int64_t val_size);
// Persistent Kv: WAL-backed append-only log (.kvwal file).
void*   aiml_db_kv_open_persist(const char* name, int64_t val_size, const char* base_path);
// spec/31 §6.2.1: as above, plus an index dump next to the WAL.
void*   aiml_db_kv_open_persist_idx(const char* name, int64_t val_size,
                                    const char* base_path, int32_t index_dump);
// Typed Kv: value type contains heap fields (strings, lists, …).
// nodes/n_nodes: descriptor emitted by the compiler as @__aiml_sernodes_<Type>.
// set/get use aiml_ser_encode / aiml_ser_decode so the WAL stores safe blobs.
void*   aiml_db_kv_open_typed(const char* name, int64_t val_size,
                               const AimlTypeNode* nodes, int32_t n_nodes);
void*   aiml_db_kv_open_typed_persist(const char* name, int64_t val_size,
                                       const char* base_path,
                                       const AimlTypeNode* nodes, int32_t n_nodes);
void*   aiml_db_kv_open_typed_persist_idx(const char* name, int64_t val_size,
                                           const char* base_path,
                                           const AimlTypeNode* nodes, int32_t n_nodes,
                                           int32_t index_dump);
int32_t aiml_db_kv_checkpoint(void* kv);  // compact WAL to current state
void    aiml_db_kv_reload(void* kv);      // re-read WAL from disk (cross-process sync)
void    aiml_db_kv_set(void* kv, const char* key, const void* val, int64_t val_size);
void*   aiml_db_kv_get(void* kv, const char* key);
// Deep-copy variant: returned struct has independent heap allocations.
// Safe for mutate-and-set-same-key. See doc/issue/kv_get_mutate_set_same_key_crash.md
void*   aiml_db_kv_get_copy(void* kv, const char* key);
// Reusable deep-copy into a caller-owned buffer `dst` (spec/31 §6.7.2): frees dst's
// prior heap, then decodes a fresh independent copy into it.  Returns 1=copied, 0=missing.
int32_t aiml_db_kv_get_copy_at(void* kv, const char* key, void* dst);
int32_t aiml_db_kv_has(void* kv, const char* key);
int32_t aiml_db_kv_len(void* kv);
int32_t aiml_db_kv_keys_fill(void* kv, void* list);
void    aiml_db_kv_delete(void* kv, const char* key);
void    aiml_db_kv_clear(void* kv);
void    aiml_db_kv_evict(void* kv);    // free in-memory map, keep WAL on disk
void    aiml_db_kv_set_blob(void* kv, const char* key, const void* data, int32_t size);
void*   aiml_db_kv_get_blob(void* kv, const char* key, int32_t* out_size);

// ── Secondary Index (db_indexed modifier, spec/45) ──────────────────────────
// Opaque handle to an in-memory secondary index keyed by a value-type field.
// Registered per (parent_kv, field_name); subsequent opens of the same pair
// return the existing handle (idempotent).  `unique` = 0 for 1:N
// (findings_by → list<string>) or 1 for unique (finding_by → string).
// `mode` is "" (default) or "lower" (case-insensitive).
void* aiml_db_index_open_for_kv(void* parent_kv, const char* field_name,
                                int32_t unique, const char* mode);
// Returns existing index for (parent_kv, field_name) or nullptr.
void* aiml_db_index_lookup(void* parent_kv, const char* field_name);
// 1:N: add kv_key under field_val (idempotent).
// unique: overwrite the single mapping.
void  aiml_db_index_add(void* idx, const char* field_val, const char* kv_key);
// 1:N: remove kv_key from field_val's list (no-op if absent).
// unique: clear if current mapping points to kv_key.
void  aiml_db_index_remove(void* idx, const char* field_val, const char* kv_key);
// Fills out_list (AimlListRaw*) with all kv_keys mapped to field_val.
// For unique-mode index the list contains 0 or 1 element.
void  aiml_db_index_findings(void* idx, const char* field_val, void* out_list);
// Returns mapped kv_key for field_val (unique-mode); for 1:N returns the
// first element (or "" if none).  Pointer lives as long as the index entry.
const char* aiml_db_index_finding(void* idx, const char* field_val);
// Walks existing entries in the parent kv and populates the index from the
// field at byte-offset `field_offset`.  `field_tag`: 0=string, 1=i32, 2=i64,
// 3=list<string>.  Called once at open-time when the index is empty but the
// main Kv already has data (cross-process reopen, first-time db_indexed).
void aiml_db_index_backfill(void* kv, void* idx, int32_t field_offset, int32_t field_tag);

// ── Iterator ─────────────────────────────────────────────────────────────────
// Generic iterator handle.  Currently supports KV key iteration (type=0).
// Future: list (type=1), store (type=2), map (type=3), …
void*   aiml_iter_create_kv(void* kv);          // snapshot KV keys → iterator
const char* aiml_iter_next(void* iter);         // next key (or "" when done)
int32_t aiml_iter_has_next(void* iter);         // 1 if more elements, 0 otherwise
void    aiml_iter_reset(void* iter);            // rewind to beginning
void    aiml_iter_free(void* iter);             // release snapshot memory

// ── VecStore ──────────────────────────────────────────────────────────────────
void*   aiml_db_vec_open(const char* name, int32_t dims);
void*   aiml_db_vec_open_persist(const char* name, int32_t dims, const char* base_path);
int32_t aiml_db_vec_checkpoint(void* vs);
void    aiml_db_vec_set(void* vs, const char* id, const float* emb, int32_t dims);
int32_t aiml_db_vec_delete(void* vs, const char* id);
void    aiml_db_vec_search(void* vs, const float* query, int32_t k, void* result_list);
int32_t aiml_db_vec_len(void* vs);

// ── TimeSeries<T> ─────────────────────────────────────────────────────────────
// Append-only log: each entry has a monotone int64 timestamp.
void*   aiml_db_ts_open(const char* name, int64_t elem_size);
// Persistent TimeSeries: binary log file (.tslog), replayed on open.
void*   aiml_db_ts_open_persist(const char* name, int64_t elem_size, const char* base_path);
int32_t aiml_db_ts_checkpoint(void* ts);  // fsync the log file
void    aiml_db_ts_append(void* ts, const void* elem, int64_t elem_size, int64_t timestamp);
// at: returns ptr to last entry with timestamp <= t, or nullptr
void*   aiml_db_ts_at(void* ts, int64_t timestamp);
// range: fills result_list (AimlListRaw*) with void* ptrs for entries in [from, to]
void    aiml_db_ts_range(void* ts, int64_t from, int64_t to, void* result_list);
int32_t aiml_db_ts_len(void* ts);

#ifdef __cplusplus
}
#endif
