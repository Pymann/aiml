#pragma once
#include <cstdint>

extern "C" {

// string.lower() -> string — all ASCII uppercase to lowercase
char* aiml_str_lower(const char* s);

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
