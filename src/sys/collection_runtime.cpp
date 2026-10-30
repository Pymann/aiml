// list / map helpers (spec/10 F10-20..22): sort, join, keys.
#include "collection_runtime.h"
#include "string_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace {

struct ListRaw { int32_t size; int32_t capacity; void* data; };            // ListImpl layout
struct MapRaw  { int32_t size; int32_t capacity; uint8_t* occ; void* keys; void* vals; };  // MapImpl layout

template <typename T>
void sort_numbers(void* data, int32_t n, bool descending) {
    T* first = static_cast<T*>(data);
    // a before b?  NaN goes last in both directions.
    std::stable_sort(first, first + n, [descending](T a, T b) {
        if constexpr (std::is_floating_point<T>::value) {
            if (std::isnan(a)) return false;
            if (std::isnan(b)) return true;
        }
        return descending ? b < a : a < b;
    });
}

}  // namespace

extern "C" void aiml_list_sort(void* listPtr, int32_t kind, int32_t descending) {
    auto* list = static_cast<ListRaw*>(listPtr);
    if (!list || list->size < 2 || !list->data) return;
    const bool desc = descending != 0;
    switch (kind) {
        case AIML_SORT_I8:  sort_numbers<int8_t>(list->data, list->size, desc); break;
        case AIML_SORT_U8:  sort_numbers<uint8_t>(list->data, list->size, desc); break;
        case AIML_SORT_I16: sort_numbers<int16_t>(list->data, list->size, desc); break;
        case AIML_SORT_U16: sort_numbers<uint16_t>(list->data, list->size, desc); break;
        case AIML_SORT_I32: sort_numbers<int32_t>(list->data, list->size, desc); break;
        case AIML_SORT_U32: sort_numbers<uint32_t>(list->data, list->size, desc); break;
        case AIML_SORT_I64: sort_numbers<int64_t>(list->data, list->size, desc); break;
        case AIML_SORT_U64: sort_numbers<uint64_t>(list->data, list->size, desc); break;
        case AIML_SORT_F32: sort_numbers<float>(list->data, list->size, desc); break;
        case AIML_SORT_F64: sort_numbers<double>(list->data, list->size, desc); break;
        case AIML_SORT_STRING: {
            char** first = static_cast<char**>(list->data);
            auto less = [](const char* a, const char* b) { return std::strcmp(a ? a : "", b ? b : "") < 0; };
            if (desc) std::stable_sort(first, first + list->size, [&](const char* a, const char* b) { return less(b, a); });
            else      std::stable_sort(first, first + list->size, less);
            break;
        }
        default: break;
    }
}

extern "C" char* aiml_list_join(void* listPtr, const char* sep) {
    auto* list = static_cast<ListRaw*>(listPtr);
    std::string out;
    if (list && list->data) {
        char** items = static_cast<char**>(list->data);
        for (int32_t i = 0; i < list->size; ++i) {
            if (i > 0 && sep) out += sep;
            if (items[i]) out += items[i];
        }
    }
    return aiml_str_ndup(out.data(), out.size());
}

extern "C" int32_t aiml_map_keys_fill(void* mapPtr, void* out) {
    auto* map = static_cast<MapRaw*>(mapPtr);
    std::vector<const char*> keys;
    if (map && map->keys) {                   // MapImpl keeps entries dense in [0, size)
        char** slots = static_cast<char**>(map->keys);
        for (int32_t i = 0; i < map->size; ++i)
            if (slots[i]) keys.push_back(slots[i]);
    }
    std::sort(keys.begin(), keys.end(), [](const char* a, const char* b) { return std::strcmp(a, b) < 0; });
    for (const char* k : keys) aiml_list_push_str_copy(out, k, std::strlen(k));
    return (int32_t)keys.size();
}
