// hashmap<K:V> runtime (spec/69_hashmap.md).
//
// A hashmap is an opaque, refcounted heap object (spec/61 Class 5, like chan<T>): copies of the
// handle co-own it (retain), the last owner's drop frees it (release).  Lookup is a real hash
// table (std::unordered_map) — O(1) expected per get/put/contains/remove, unlike map<K:V>
// (map_impl.cpp: linear search over parallel key/value arrays).
//
// Keys:   key_kind 0 = string (std::string copy), 1 = integer (int64).
// Values: val_kind 0 = integer/bool (int64 bits), 1 = float (double bits), 2 = string.
//         String values are stored as owned RC references (aiml_str_dup) and handed out as
//         BORROWS (get) — a binding local increfs them, so they stay valid after a later put/remove.
// A missing key reads as 0 / 0.0 / "" (immortal empty string), never as null.
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

extern "C" char* aiml_str_dup(const char* s);
extern "C" void  aiml_str_decref(char* s);
extern "C" char* aiml_str_empty(void);

namespace {

struct AimlHashMap {
    std::atomic<int32_t> rc{1};
    int32_t key_kind = 0;
    int32_t val_kind = 0;
    std::unordered_map<std::string, uint64_t> skeys;
    std::unordered_map<int64_t, uint64_t> ikeys;
};

struct ListRaw { int32_t size; int32_t capacity; void* data; };

inline AimlHashMap* as_map(void* h) { return static_cast<AimlHashMap*>(h); }

inline void release_value(const AimlHashMap* m, uint64_t bits) {
    if (m->val_kind == 2 && bits) aiml_str_decref(reinterpret_cast<char*>(static_cast<uintptr_t>(bits)));
}

inline uint64_t* find_slot(AimlHashMap* m, const char* sk, int64_t ik) {
    if (m->key_kind == 0) {
        auto it = m->skeys.find(sk ? std::string(sk) : std::string());
        return it == m->skeys.end() ? nullptr : &it->second;
    }
    auto it = m->ikeys.find(ik);
    return it == m->ikeys.end() ? nullptr : &it->second;
}

// Slot for key, created (value 0) if missing.
inline uint64_t& slot_for(AimlHashMap* m, const char* sk, int64_t ik) {
    if (m->key_kind == 0) return m->skeys[sk ? std::string(sk) : std::string()];
    return m->ikeys[ik];
}

void clear_values(AimlHashMap* m) {
    if (m->val_kind == 2) {
        for (auto& kv : m->skeys) release_value(m, kv.second);
        for (auto& kv : m->ikeys) release_value(m, kv.second);
    }
    m->skeys.clear();
    m->ikeys.clear();
}

}  // namespace

extern "C" void* aiml_hashmap_new(int32_t key_kind, int32_t val_kind) {
    auto* m = new AimlHashMap();
    m->key_kind = key_kind;
    m->val_kind = val_kind;
    return m;
}

extern "C" void aiml_hashmap_retain(void* h) {
    if (h) as_map(h)->rc.fetch_add(1, std::memory_order_relaxed);
}

extern "C" void aiml_cap_forget(const void* c);
extern "C" void aiml_cap_copy(const void* from, const void* to);
extern "C" void aiml_hashmap_release(void* h) {
    if (!h) return;
    AimlHashMap* m = as_map(h);
    if (m->rc.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        aiml_cap_forget(h);   // spec/71 §5
        clear_values(m);
        delete m;
    }
}

// Deep copy (spec/61 clone): independent table, string values duplicated.
extern "C" void* aiml_hashmap_clone(void* h) {
    if (!h) return nullptr;
    AimlHashMap* src = as_map(h);
    auto* m = new AimlHashMap();
    m->key_kind = src->key_kind;
    m->val_kind = src->val_kind;
    m->skeys = src->skeys;
    m->ikeys = src->ikeys;
    if (m->val_kind == 2) {
        auto dup = [](uint64_t& bits) {
            const char* s = reinterpret_cast<const char*>(static_cast<uintptr_t>(bits));
            bits = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(aiml_str_dup(s ? s : "")));
        };
        for (auto& kv : m->skeys) dup(kv.second);
        for (auto& kv : m->ikeys) dup(kv.second);
    }
    aiml_cap_copy(h, m);   // spec/71 F71-6: a copy keeps the capacity
    return m;
}

extern "C" void aiml_hashmap_put_bits(void* h, const char* sk, int64_t ik, uint64_t bits) {
    if (!h) return;
    slot_for(as_map(h), sk, ik) = bits;
}

extern "C" void aiml_hashmap_put_str(void* h, const char* sk, int64_t ik, const char* value) {
    if (!h) return;
    AimlHashMap* m = as_map(h);
    char* owned = aiml_str_dup(value ? value : "");   // acquire before releasing the old value
    uint64_t& slot = slot_for(m, sk, ik);
    uint64_t old = slot;
    slot = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(owned));
    release_value(m, old);
}

extern "C" uint64_t aiml_hashmap_get_bits(void* h, const char* sk, int64_t ik) {
    if (!h) return 0;
    uint64_t* slot = find_slot(as_map(h), sk, ik);
    return slot ? *slot : 0;
}

extern "C" const char* aiml_hashmap_get_str(void* h, const char* sk, int64_t ik) {
    // spec/71 §3: a missing key yields the zero value of string — the null pointer.
    if (!h) return nullptr;
    uint64_t* slot = find_slot(as_map(h), sk, ik);
    if (!slot || !*slot) return nullptr;
    return reinterpret_cast<const char*>(static_cast<uintptr_t>(*slot));
}

// Integer values only: slot += delta (missing key counts as 0); returns the new value.
extern "C" int64_t aiml_hashmap_add(void* h, const char* sk, int64_t ik, int64_t delta) {
    if (!h) return 0;
    uint64_t& slot = slot_for(as_map(h), sk, ik);
    int64_t next = static_cast<int64_t>(slot) + delta;
    slot = static_cast<uint64_t>(next);
    return next;
}

extern "C" int32_t aiml_hashmap_contains(void* h, const char* sk, int64_t ik) {
    if (!h) return 0;
    return find_slot(as_map(h), sk, ik) ? 1 : 0;
}

extern "C" int32_t aiml_hashmap_remove(void* h, const char* sk, int64_t ik) {
    if (!h) return 0;
    AimlHashMap* m = as_map(h);
    if (m->key_kind == 0) {
        auto it = m->skeys.find(sk ? std::string(sk) : std::string());
        if (it == m->skeys.end()) return 0;
        release_value(m, it->second);
        m->skeys.erase(it);
        return 1;
    }
    auto it = m->ikeys.find(ik);
    if (it == m->ikeys.end()) return 0;
    release_value(m, it->second);
    m->ikeys.erase(it);
    return 1;
}

extern "C" int32_t aiml_hashmap_len(void* h) {
    if (!h) return 0;
    AimlHashMap* m = as_map(h);
    return static_cast<int32_t>(m->key_kind == 0 ? m->skeys.size() : m->ikeys.size());
}

extern "C" void aiml_hashmap_clear(void* h) {
    if (h) clear_values(as_map(h));
}

// keys() -> list<K>, sorted (deterministic regardless of hash order).  `list_ptr` is an
// initialised list handle {i32 size, i32 cap, data}; elem_size is 8 for string/i64, 4 for i32.
extern "C" int32_t aiml_hashmap_keys_fill(void* h, void* list_ptr, int32_t elem_size) {
    if (!h || !list_ptr) return 0;
    AimlHashMap* m = as_map(h);
    auto* list = static_cast<ListRaw*>(list_ptr);
    auto reserve = [&](int32_t need) -> bool {
        if (need <= list->capacity) return true;
        int32_t cap = list->capacity > 0 ? list->capacity : 8;
        while (cap < need) cap *= 2;
        void* grown = std::realloc(list->data, static_cast<size_t>(cap) * static_cast<size_t>(elem_size));
        if (!grown) return false;
        list->data = grown;
        list->capacity = cap;
        return true;
    };
    if (m->key_kind == 0) {
        std::vector<const std::string*> keys;
        keys.reserve(m->skeys.size());
        for (auto& kv : m->skeys) keys.push_back(&kv.first);
        std::sort(keys.begin(), keys.end(), [](const std::string* a, const std::string* b) { return *a < *b; });
        if (!reserve(list->size + static_cast<int32_t>(keys.size()))) return list->size;
        char** out = static_cast<char**>(list->data);
        for (const std::string* k : keys) out[list->size++] = aiml_str_dup(k->c_str());
        return list->size;
    }
    std::vector<int64_t> keys;
    keys.reserve(m->ikeys.size());
    for (auto& kv : m->ikeys) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());
    if (!reserve(list->size + static_cast<int32_t>(keys.size()))) return list->size;
    for (int64_t k : keys) {
        if (elem_size == 4) static_cast<int32_t*>(list->data)[list->size++] = static_cast<int32_t>(k);
        else static_cast<int64_t*>(list->data)[list->size++] = k;
    }
    return list->size;
}
