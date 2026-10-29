// Filesystem globbing runtime (spec/42_1).  Results are AIML managed strings;
// the *_fill functions append owned strings to a list<string>.
#pragma once
#include <cstdint>

extern "C" {
void*   aiml_glob_new(const char* pattern, int32_t max_per_step);
int32_t aiml_glob_next_fill(void* globber, void* list);
char*   aiml_glob_location(void* globber);
char*   aiml_glob_pattern(void* globber);
int32_t aiml_glob_done(void* globber);
void    aiml_glob_free(void* globber);
int32_t aiml_glob_all_fill(const char* pattern, void* list);
int32_t aiml_glob_match(const char* pattern, const char* text);
}
