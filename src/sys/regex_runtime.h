// regex namespace runtime (spec/09_2).  Strings are AIML managed strings;
// the *_fill functions append owned strings to a list<string>.
#pragma once
#include <cstdint>

extern "C" {
int32_t aiml_regex_match(const char* pattern, const char* text);
int32_t aiml_regex_search(const char* pattern, const char* text);
int32_t aiml_regex_find_fill(const char* pattern, const char* text, void* list);
int32_t aiml_regex_find_all_fill(const char* pattern, const char* text, void* list);
char*   aiml_regex_replace(const char* pattern, const char* text, const char* with);
int32_t aiml_regex_split_fill(const char* pattern, const char* text, void* list);
char*   aiml_regex_error(void);
}
