// list / map helpers (spec/10 F10-20..22): sort, join, keys.
#include "collection_runtime.h"
#include "string_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

// ── spec/71 §5: .capacity — an optional maximum element count per container ──────────────
// The maximum is kept in a side table keyed by the container's address, so no container
// layout (and no serialized format) changes.  `aiml_cap_active` counts the entries: while it
// is 0 — every program that never sets a capacity — the compiled checks skip the call.
#include <mutex>
#include <unordered_map>

extern "C" { int32_t aiml_cap_active = 0; }

namespace {
std::mutex& capMutex() { static std::mutex m; return m; }
std::unordered_map<const void*, int32_t>& capTable() {
    static std::unordered_map<const void*, int32_t> t;
    return t;
}
}  // namespace

// The maximum of `c`, 0 = unbounded.
extern "C" int32_t aiml_cap_get(const void* c) {
    if (!c || aiml_cap_active == 0) return 0;
    std::lock_guard<std::mutex> g(capMutex());
    auto it = capTable().find(c);
    return it == capTable().end() ? 0 : it->second;
}

// F71-1/F71-3: set the maximum; refused (returns 1, nothing changes) below the current len.
extern "C" int32_t aiml_cap_set(const void* c, int32_t n, int32_t len) {
    if (!c) return 0;
    if (n < 0) n = 0;
    if (n > 0 && n < len) return 1;
    std::lock_guard<std::mutex> g(capMutex());
    auto& t = capTable();
    if (n == 0) t.erase(c); else t[c] = n;
    aiml_cap_active = (int32_t)t.size();
    return 0;
}

// F71-2: 1 if growing `c` to `new_len` elements exceeds its maximum.
extern "C" int32_t aiml_cap_exceeds(const void* c, int32_t new_len) {
    int32_t cap = aiml_cap_get(c);
    return (cap > 0 && new_len > cap) ? 1 : 0;
}

// A freed container takes its maximum with it, so a new container at the same address starts
// unbounded.
extern "C" void aiml_cap_forget(const void* c) {
    if (!c || aiml_cap_active == 0) return;
    std::lock_guard<std::mutex> g(capMutex());
    capTable().erase(c);
    aiml_cap_active = (int32_t)capTable().size();
}

// F71-6: a copy keeps the maximum.
extern "C" void aiml_cap_copy(const void* from, const void* to) {
    int32_t cap = aiml_cap_get(from);
    if (cap > 0) aiml_cap_set(to, cap, 0);
}

extern "C" char* aiml_capacity_message(int32_t new_len, const void* c) {
    std::string m = "capacity " + std::to_string(aiml_cap_get(c)) + " exceeded: " +
                    std::to_string(new_len) + " elements";
    return aiml_str_ndup(m.data(), m.size());
}

// spec/71 §2: string.len / .byte_len — bytes of the UTF-8 text; a null string has length 0.
extern "C" int32_t aiml_str_len_safe(const char* s) {
    return s ? static_cast<int32_t>(std::strlen(s)) : 0;
}

// spec/71 §6: a dyn_array<T> knows its length.  new_array allocates a 16-byte header in front
// of the elements and stores the element count there; the variable holds the pointer to the
// first element (16-byte aligned), so native code still receives the data pointer unchanged.
extern "C" void* aiml_darr_new(int64_t count, int64_t elem_size) {
    if (count < 0) count = 0;
    char* base = static_cast<char*>(std::malloc(16 + static_cast<size_t>(count) * static_cast<size_t>(elem_size)));
    if (!base) return nullptr;
    *reinterpret_cast<int64_t*>(base) = count;
    return base + 16;
}

extern "C" int32_t aiml_darr_len(const void* data) {
    if (!data) return 0;
    return static_cast<int32_t>(*reinterpret_cast<const int64_t*>(static_cast<const char*>(data) - 16));
}

extern "C" void aiml_darr_free(void* data) {
    if (data) std::free(static_cast<char*>(data) - 16);
}
