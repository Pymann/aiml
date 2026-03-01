#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// ── Store<T> ──────────────────────────────────────────────────────────────────
// name:      logical store name (for future persistence)
// type_name: element type name string (e.g. "Entity")
// elem_size: sizeof(T) in bytes
// Returns heap-allocated AimlStore* cast to void*, or nullptr on failure.
void*   aiml_db_store_open(const char* name, const char* type_name, int64_t elem_size);

// insert: memcpy elem into store; returns 1-based id on success, -1 on error
int32_t aiml_db_store_insert(void* store, const void* elem, int64_t elem_size);

// get: returns zero-copy pointer into store data for given 1-based id, or nullptr
void*   aiml_db_store_get(void* store, int32_t id);

// delete: marks slot as inactive (logical delete, slot not reused in v1)
void    aiml_db_store_delete(void* store, int32_t id);

// len: number of active (non-deleted) elements
int32_t aiml_db_store_len(void* store);

// ── Kv<K:V> ───────────────────────────────────────────────────────────────────
// val_size: sizeof(V) — for string keys, val_size is passed as 0 (string stored as i8*)
void*   aiml_db_kv_open(const char* name, int64_t val_size);

// set: copies val_size bytes from val under key (val_size==0 → stores i8* pointer)
void    aiml_db_kv_set(void* kv, const char* key, const void* val, int64_t val_size);

// get: returns pointer to stored bytes (i8* for strings, elem ptr for structs)
//      Returns nullptr if key not found.
void*   aiml_db_kv_get(void* kv, const char* key);

// has: returns 1 if key exists, 0 otherwise
int32_t aiml_db_kv_has(void* kv, const char* key);

// len: number of entries
int32_t aiml_db_kv_len(void* kv);

// ── VecStore ──────────────────────────────────────────────────────────────────
// dims: number of float dimensions per vector
void*   aiml_db_vec_open(const char* name, int32_t dims);

// insert: id is a string label; emb is a float[dims] array
void    aiml_db_vec_insert(void* vs, const char* id, const float* emb, int32_t dims);

// search: fills result_list (AimlListRaw*) with up to k heap-alloc'd AimlVecResult*
//         (i8* pointers); caller must cast list elements to AimlVecResult*
void    aiml_db_vec_search(void* vs, const float* query, int32_t k, void* result_list);

#ifdef __cplusplus
}
#endif
