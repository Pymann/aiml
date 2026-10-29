// list / map helpers implemented in the runtime (spec/10 F10-20..22).
#pragma once
#include <cstdint>

// Element kinds for aiml_list_sort (the compiler maps the element type).
enum AimlSortKind {
    AIML_SORT_I8 = 1, AIML_SORT_U8, AIML_SORT_I16, AIML_SORT_U16, AIML_SORT_I32, AIML_SORT_U32,
    AIML_SORT_I64, AIML_SORT_U64, AIML_SORT_F32, AIML_SORT_F64, AIML_SORT_STRING
};

extern "C" {
// Stable in-place sort of a list's elements, O(n log n); NaN sorts last.
void    aiml_list_sort(void* list, int32_t kind, int32_t descending);
// list<string>.join(sep) → a new managed string.
char*   aiml_list_join(void* list, const char* sep);
// map<string:V>.keys() → every key (copies) appended to `out`, in byte order.
int32_t aiml_map_keys_fill(void* map, void* out);
}
