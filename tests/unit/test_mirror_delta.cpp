// spec/44_5 §10 DELTA format 2: aiml_ser_encode_wire / aiml_ser_decode_delta_tx.
// A failed apply leaves the target bit-identical (compared via its wire encoding); a partial decode
// that already allocated a string frees it (run the binary under valgrind for the leak half).
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/sys/ser_runtime.h"
#include "../../src/sys/string_runtime.h"

namespace {

struct UtState {
    char*   label;
    char*   note;
    int32_t count;
};

const AimlTypeNode kNodes[] = {
    {AIML_TD_STRUCT, 3, 1, 0, (int32_t)sizeof(UtState), 0},
    {AIML_TD_STRING, 0, 0, (int32_t)offsetof(UtState, label), 0, 0},
    {AIML_TD_STRING, 0, 0, (int32_t)offsetof(UtState, note), 0, 0},
    {AIML_TD_I32,    0, 0, (int32_t)offsetof(UtState, count), 0, 0},
};
const AimlTypeNode kOtherNodes[] = {   // same layout size, different wire schema (i64 count)
    {AIML_TD_STRUCT, 3, 1, 0, (int32_t)sizeof(UtState), 0},
    {AIML_TD_STRING, 0, 0, (int32_t)offsetof(UtState, label), 0, 0},
    {AIML_TD_STRING, 0, 0, (int32_t)offsetof(UtState, note), 0, 0},
    {AIML_TD_I64,    0, 0, (int32_t)offsetof(UtState, count), 0, 0},
};

std::vector<uint8_t> wire(const UtState& st, const AimlTypeNode* nodes = kNodes) {
    char* blob = aiml_ser_encode_wire(&st, nodes, 4);
    uint32_t len;
    std::memcpy(&len, blob, 4);
    std::vector<uint8_t> out((uint8_t*)blob, (uint8_t*)blob + len + 4);
    std::free(blob);
    return out;
}

void set_len(std::vector<uint8_t>& b) {
    uint32_t len = (uint32_t)(b.size() - 4);
    std::memcpy(b.data(), &len, 4);
}

int apply_delta(UtState& target, const std::vector<uint8_t>& b) {
    return aiml_ser_decode_delta_tx((const char*)b.data(), b.size(), &target, sizeof(target), kNodes, 4);
}

void release(UtState& st) {
    aiml_ser_free_decoded(&st, kNodes, 4);
}

}  // namespace

int mirror_delta_unit_tests() {
    UtState source{aiml_str_dup("fresh-label"), aiml_str_dup("fresh-note"), 77};
    UtState target{aiml_str_dup("old-label"), aiml_str_dup("old-note"), 5};
    const std::vector<uint8_t> good = wire(source);
    const std::vector<uint8_t> before = wire(target);

    // envelope: [len][magic][fp]
    assert(good.size() > 12);
    uint32_t magic;
    std::memcpy(&magic, good.data() + 4, 4);
    assert(magic == 0xA1F5C0DFu);

    // outer length disagrees with the received size
    std::vector<uint8_t> bad = good;
    bad.pop_back();
    assert(apply_delta(target, bad) == -1);
    assert(wire(target) == before);

    // consistent outer length, inner field cut short
    bad = good;
    bad.resize(bad.size() - 2);
    set_len(bad);
    assert(apply_delta(target, bad) == -1);
    assert(wire(target) == before);

    // failure after the first string was allocated: the second string's length runs past the end
    bad = good;
    const size_t second_len_at = 12 + 4 + std::strlen("fresh-label");
    const uint32_t huge = 0x00FFFFFFu;
    std::memcpy(bad.data() + second_len_at, &huge, 4);
    assert(apply_delta(target, bad) == -1);
    assert(wire(target) == before);

    // trailing byte after a complete payload
    bad = good;
    bad.push_back(0);
    set_len(bad);
    assert(apply_delta(target, bad) == -1);
    assert(wire(target) == before);

    // wrong magic / foreign schema / untagged legacy blob
    bad = good;
    bad[4] ^= 0xFF;
    assert(apply_delta(target, bad) == -2);
    std::vector<uint8_t> foreign = wire(source, kOtherNodes);
    assert(apply_delta(target, foreign) == -2);
    char* legacy = aiml_ser_encode(&source, kNodes, 4);
    uint32_t legacy_len;
    std::memcpy(&legacy_len, legacy, 4);
    assert(aiml_ser_decode_delta_tx(legacy, legacy_len + 4, &target, sizeof(target), kNodes, 4) != 0);
    std::free(legacy);
    assert(wire(target) == before);

    // success replaces the content (old strings released), again on a second snapshot
    assert(apply_delta(target, good) == 0);
    assert(std::strcmp(target.label, "fresh-label") == 0);
    assert(std::strcmp(target.note, "fresh-note") == 0);
    assert(target.count == 77);
    assert(apply_delta(target, good) == 0);
    assert(wire(target) == good);

    // descriptors with recursive/resource leaves are not syncable
    const AimlTypeNode chan_nodes[] = {
        {AIML_TD_STRUCT, 1, 1, 0, 8, 0},
        {AIML_TD_CHAN, 0, 0, 0, 0, 0},
    };
    assert(aiml_ser_wire_syncable(kNodes, 4) == 1);
    assert(aiml_ser_wire_syncable(chan_nodes, 2) == 0);

    release(source);
    release(target);
    return 0;
}
