#pragma once
#include <cstdint>
#include <cstddef>  // size_t

extern "C" {

// ── Refcounted string allocator ──────────────────────────────────────────────
// All runtime-allocated strings should go through aiml_str_alloc/dup so that
// aiml_str_incref/decref can manage their lifetime.  Literals (rodata) work
// transparently — incref/decref no-op on them via a magic-header check.
//
// `len` is size_t to match `malloc(size_t)` and `strlen(...)` return types
// for clean drop-in replacement in `__aiml_str_concat` & friends (which use
// ctx.getSizeType() — i64 on x86_64, i32 on wasm32).  Internal storage is
// uint32 refcount, so practical max string is 4GB which we'll never hit.
char* aiml_str_alloc(size_t len);
char* aiml_str_dup(const char* s);
char* aiml_str_ndup(const char* s, size_t len);
void  aiml_str_incref(char* s);
void  aiml_str_decref(char* s);

// string.lower() -> string — all ASCII uppercase to lowercase
char* aiml_str_lower(const char* s);

// string.upper() -> string — all ASCII lowercase to uppercase
char* aiml_str_upper(const char* s);

// string.title() -> string — capitalize first char of every word, lowercase
// the rest.  Word separators: whitespace, `-`, and `_`.  Useful for turning
// snake_case ids into display labels: "smith_touch" -> "Smith Touch".
char* aiml_str_title(const char* s);

// string.trim() -> string — strip leading+trailing whitespace
char* aiml_str_trim(const char* s);

// string.replace(old, new) -> string — replace all occurrences
char* aiml_str_replace(const char* s, const char* old_str, const char* new_str);

// string.split(delimiter) -> list<string>  (fills AimlListRaw via out-pointer)
// Returns number of parts written.
int aiml_str_split_fill(const char* s, const char* delim, void* list_ptr);

// string.parse_i32() -> i32 — like atoi but returns 0 for null/empty
int32_t aiml_str_parse_i32(const char* s);

// string.parse_f32() -> f32 — like strtof but returns 0.0 for null/empty
float aiml_str_parse_f32(const char* s);

}
