#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// ---------------------------------------------------------------------------
// AIML JSON Write API  (aiml_jw_*)
//
// Usage pattern (for struct Vec2 { i32 x, i32 y }):
//   void* w = aiml_jw_new();
//   aiml_jw_set_type(w, "Vec2");
//   aiml_jw_set_i32(w, "x", 10);
//   aiml_jw_set_i32(w, "y", 20);
//   char* json = aiml_jw_done(w);   // heap-allocated, caller owns
// ---------------------------------------------------------------------------

void*  aiml_jw_new();
void   aiml_jw_set_type(void* w, const char* type_name);
void   aiml_jw_set_i32 (void* w, const char* key, int32_t  val);
void   aiml_jw_set_i64 (void* w, const char* key, int64_t  val);
void   aiml_jw_set_f32 (void* w, const char* key, float    val);
void   aiml_jw_set_f64 (void* w, const char* key, double   val);
void   aiml_jw_set_bool(void* w, const char* key, int      val);
void   aiml_jw_set_str (void* w, const char* key, const char* val);

// Arrays — usage: arr_begin, (arr_*  per element), arr_end
void   aiml_jw_arr_begin(void* w, const char* key);
void   aiml_jw_arr_i32  (void* w, int32_t  val);
void   aiml_jw_arr_i64  (void* w, int64_t  val);
void   aiml_jw_arr_f32  (void* w, float    val);
void   aiml_jw_arr_f64  (void* w, double   val);
void   aiml_jw_arr_bool (void* w, int      val);
void   aiml_jw_arr_str  (void* w, const char* val);
void   aiml_jw_arr_end  (void* w);

// Nested objects — usage: obj_begin, (set_* / arr_begin…arr_end per field), obj_end
void   aiml_jw_obj_begin(void* w, const char* key);
void   aiml_jw_obj_end  (void* w);

// Anonymous nested containers within arrays (no key)
void   aiml_jw_arr_obj_begin(void* w);  // open object in array
void   aiml_jw_arr_obj_end  (void* w);  // close object in array
void   aiml_jw_arr_arr_begin(void* w);  // open sub-array in array
void   aiml_jw_arr_arr_end  (void* w);  // close sub-array in array

// Map entries within an open object — key-value pairs, each key is a string
// (open with obj_begin / close with obj_end)
void   aiml_jw_map_i32  (void* w, const char* key, int32_t  val);
void   aiml_jw_map_i64  (void* w, const char* key, int64_t  val);
void   aiml_jw_map_f32  (void* w, const char* key, float    val);
void   aiml_jw_map_f64  (void* w, const char* key, double   val);
void   aiml_jw_map_bool (void* w, const char* key, int      val);
void   aiml_jw_map_str  (void* w, const char* key, const char* val);

// Finish: returns heap-allocated JSON string (caller must free() it).
char*  aiml_jw_done(void* w);

// ---------------------------------------------------------------------------
// AIML JSON Read API  (aiml_jr_*)
//
// Usage pattern:
//   void* r = aiml_jr_parse(json_str);
//   int32_t x = aiml_jr_i32(r, "x");
//   void*   sub = aiml_jr_sub(r, "pos");    // sub-object reader
//   int32_t px = aiml_jr_i32(sub, "x");
//   aiml_jr_free(sub);                       // free sub (does NOT free the doc)
//   aiml_jr_free(r);                         // free root (frees the doc)
// ---------------------------------------------------------------------------

void*    aiml_jr_parse  (const char* json);
void*    aiml_jr_sub    (void* r, const char* key);  // returns sub-reader (non-owning doc)
void     aiml_jr_free   (void* r);

int32_t  aiml_jr_i32    (void* r, const char* key);
int64_t  aiml_jr_i64    (void* r, const char* key);
float    aiml_jr_f32    (void* r, const char* key);
double   aiml_jr_f64    (void* r, const char* key);
int      aiml_jr_bool   (void* r, const char* key);
char*    aiml_jr_str    (void* r, const char* key);   // heap copy, caller frees

// Arrays
int      aiml_jr_arr_len (void* r, const char* key);
int32_t  aiml_jr_arr_i32 (void* r, const char* key, int idx);
int64_t  aiml_jr_arr_i64 (void* r, const char* key, int idx);
float    aiml_jr_arr_f32 (void* r, const char* key, int idx);
double   aiml_jr_arr_f64 (void* r, const char* key, int idx);
int      aiml_jr_arr_bool(void* r, const char* key, int idx);
char*    aiml_jr_arr_str (void* r, const char* key, int idx);  // heap copy
void*    aiml_jr_arr_sub (void* r, const char* key, int idx);  // sub-reader for obj/arr element

// Maps (string keys, homogeneous values)
int      aiml_jr_map_count  (void* r, const char* key);
char*    aiml_jr_map_key    (void* r, const char* key, int idx);  // heap copy
int32_t  aiml_jr_map_val_i32(void* r, const char* key, int idx);
int64_t  aiml_jr_map_val_i64(void* r, const char* key, int idx);
float    aiml_jr_map_val_f32(void* r, const char* key, int idx);
double   aiml_jr_map_val_f64(void* r, const char* key, int idx);
int      aiml_jr_map_val_bool(void* r, const char* key, int idx);
char*    aiml_jr_map_val_str(void* r, const char* key, int idx);  // heap copy

// Value-level array access (keyless) — for readers obtained via arr_sub
// where r->val IS the array/object itself
int      aiml_jr_val_len     (void* r);                   // array length of r->val
int32_t  aiml_jr_val_i32     (void* r, int idx);
int64_t  aiml_jr_val_i64     (void* r, int idx);
float    aiml_jr_val_f32     (void* r, int idx);
double   aiml_jr_val_f64     (void* r, int idx);
int      aiml_jr_val_bool    (void* r, int idx);
char*    aiml_jr_val_str     (void* r, int idx);           // heap copy
void*    aiml_jr_val_sub     (void* r, int idx);           // sub-reader for element

#ifdef __cplusplus
}
#endif
