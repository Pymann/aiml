// spec/29 §5.4.1: chan/hashmap sind IN-PROZESS-Handles.  Der Blob traegt hinter
// dem Zeiger einen Prozess-Stempel; passt er nicht, dekodiert das Feld als null
// und setzt den Decode-Fehler -- der Zeiger wird NIE vorher angefasst.
//
// Ohne das greift ein Server auf einen Zeiger zu, den ein laengst beendeter
// Prozess geschrieben hat (gemessen in datacreatures: SIGSEGV in einem
// Monate alten KV-Store).
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../../src/sys/ser_runtime.h"

extern "C" void* aiml_hashmap_new(int32_t key_kind, int32_t val_kind);
extern "C" void     aiml_hashmap_put_bits(void* h, const char* sk, int64_t ik, uint64_t bits);
extern "C" uint64_t aiml_hashmap_get_bits(void* h, const char* sk, int64_t ik);

namespace {

struct UtBag {
    void*   table;     // hashmap<string:i32>
    int32_t count;
};

const AimlTypeNode kNodes[] = {
    {AIML_TD_STRUCT,  2, 1, 0, (int32_t)sizeof(UtBag), 0},
    {AIML_TD_HASHMAP, 0, 0, (int32_t)offsetof(UtBag, table), 0, 0},
    {AIML_TD_I32,     0, 0, (int32_t)offsetof(UtBag, count), 0, 0},
};

std::vector<uint8_t> encode(const UtBag& bag) {
    char* blob = aiml_ser_encode(&bag, kNodes, 3);
    uint32_t len;
    std::memcpy(&len, blob, 4);
    std::vector<uint8_t> out((uint8_t*)blob, (uint8_t*)blob + len + 4);
    std::free(blob);
    return out;
}

}  // namespace

// Eingehaengt in tests/unit/test_fmt_loc.cpp (ein Binary, ein main).
int ser_handle_stamp_unit_tests() {
    UtBag source{};
    source.table = aiml_hashmap_new(0 /* string */, 0 /* i32 */);
    source.count = 7;
    aiml_hashmap_put_bits(source.table, "alpha", 0, 42);

    std::vector<uint8_t> blob = encode(source);

    // 1. Im selben Prozess: der Handle kommt lebendig an.
    UtBag same{};
    assert(aiml_ser_decode_sized((const char*)blob.data(), blob.size(), &same, kNodes, 3) == 0);
    assert(same.table == source.table);
    assert(same.count == 7);
    assert(aiml_hashmap_get_bits(same.table, "alpha", 0) == 42u);

    // 2. Blob eines FREMDEN Prozesses: der Stempel hinter dem Zeiger wird
    //    verdreht.  Erwartung: null, Fehler gemeldet, kein Zugriff, kein Sturz.
    std::vector<uint8_t> fremd = blob;
    const size_t zeiger = 4 /* Laengenkopf */ + sizeof(void*);   // danach der Stempel
    assert(fremd.size() >= zeiger + 8);
    for (size_t i = 0; i < 8; ++i) fremd[zeiger + i] ^= 0xA5;

    UtBag other{};
    other.count = 123;
    int rc = aiml_ser_decode_sized((const char*)fremd.data(), fremd.size(), &other, kNodes, 3);
    assert(rc != 0);              // Decode meldet den Fehler
    assert(other.table == nullptr);  // und fasst den fremden Zeiger nicht an
    return 0;
}
