// spec/29 §5.4.1: chan/hashmap are IN-PROCESS handles.  The blob carries a process
// stamp after the pointer; if it does not match, the field decodes as null
// and sets the decode error -- the pointer is NEVER touched before that.
//
// Without this a server accesses a pointer that a long-finished
// process wrote (measured in datacreatures: SIGSEGV in a
// months-old KV store).
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

// Hooked into tests/unit/test_fmt_loc.cpp (one binary, one main).
int ser_handle_stamp_unit_tests() {
    UtBag source{};
    source.table = aiml_hashmap_new(0 /* string */, 0 /* i32 */);
    source.count = 7;
    aiml_hashmap_put_bits(source.table, "alpha", 0, 42);

    std::vector<uint8_t> blob = encode(source);

    // 1. In the same process: the handle arrives alive.
    UtBag same{};
    assert(aiml_ser_decode_sized((const char*)blob.data(), blob.size(), &same, kNodes, 3) == 0);
    assert(same.table == source.table);
    assert(same.count == 7);
    assert(aiml_hashmap_get_bits(same.table, "alpha", 0) == 42u);

    // 2. Blob of a FOREIGN process: the stamp after the pointer is
    //    scrambled.  Expectation: null, error reported, no access, no crash.
    std::vector<uint8_t> fremd = blob;
    const size_t zeiger = 4 /* length header */ + sizeof(void*);   // followed by the stamp
    assert(fremd.size() >= zeiger + 8);
    for (size_t i = 0; i < 8; ++i) fremd[zeiger + i] ^= 0xA5;

    UtBag other{};
    other.count = 123;
    int rc = aiml_ser_decode_sized((const char*)fremd.data(), fremd.size(), &other, kNodes, 3);
    assert(rc != 0);              // decode reports the error
    assert(other.table == nullptr);  // and does not touch the foreign pointer
    return 0;
}
