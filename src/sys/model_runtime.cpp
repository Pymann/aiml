#include "model_runtime.h"
#include "ser_runtime.h"        // aiml_ser_free_decoded + AimlTypeNode
#include "mem_diff_runtime.h"   // AIML_MEM_DIFF leaf-allocation tracking (no-op when disabled)
#include <cstdlib>
#include <cstring>
#include <cstdint>

// ── Reference-counted MODEL heap objects (AIML v2) ───────────────────────────
// Mirrors the refcounted-string scheme in string_runtime.cpp. 8-byte header:
//   { uint32_t magic = 'MODL', uint32_t refcount }
// The returned pointer skips the header; incref/decref read the header at
// `obj - 8` and magic-guard so a non-MODL pointer (stack model, borrowed view)
// is left untouched. See doc/aiml_v2_memory_model.md §3.

struct AimlModelHeader {
    uint32_t magic;
    uint32_t refcount;
};

// The header is 8 bytes; the struct that follows needs at most 8-byte alignment
// (pointers / i64 / double). malloc already returns max-aligned memory, and 8 is
// a multiple of the header size, so obj = base + 8 stays 8-aligned. (Mirrors the
// string header, which returns base + 8 for char data.)
static_assert(sizeof(AimlModelHeader) == 8, "MODL header must stay 8 bytes (8-byte struct alignment)");

extern "C" void* aiml_model_alloc(size_t structSize) {
    void* mem = malloc(sizeof(AimlModelHeader) + structSize);
    if (!mem) return nullptr;
    AimlModelHeader* hdr = (AimlModelHeader*)mem;
    hdr->magic    = AIML_MODEL_MAGIC;
    hdr->refcount = 1;
    void* obj = (char*)mem + sizeof(AimlModelHeader);
    memset(obj, 0, structSize);   // aiml_ser_* precondition: heap pointer fields NULL
    aiml_memdiff_record(obj, sizeof(AimlModelHeader) + structSize);
    return obj;
}

extern "C" void aiml_model_incref(void* obj) {
    if (!obj || (uintptr_t)obj < sizeof(AimlModelHeader)) return;
    AimlModelHeader* hdr = (AimlModelHeader*)((char*)obj - sizeof(AimlModelHeader));
    if (hdr->magic != AIML_MODEL_MAGIC) return;   // stack model / borrowed view — not counted
    hdr->refcount++;
}

extern "C" void aiml_model_decref(void* obj, const void* nodes, int32_t n_nodes) {
    if (!obj || (uintptr_t)obj < sizeof(AimlModelHeader)) return;
    AimlModelHeader* hdr = (AimlModelHeader*)((char*)obj - sizeof(AimlModelHeader));
    if (hdr->magic != AIML_MODEL_MAGIC) return;   // stack model / borrowed view — not freed here
    if (hdr->refcount <= 1) {
        // Deep-free the inner heap (strings decref, nested models decref, lists/maps free)
        // BEFORE poisoning + releasing the shell. aiml_ser_free_decoded frees only what is
        // reachable inside *obj*, not *obj* itself.
        if (nodes && n_nodes > 0)
            aiml_ser_free_decoded(obj, (const AimlTypeNode*)nodes, n_nodes);
        hdr->magic = 0;   // poison to catch use-after-free on the next incref/decref
        aiml_memdiff_forget(obj);
        free(hdr);
    } else {
        hdr->refcount--;
    }
}

extern "C" uint32_t aiml_model_refcount(const void* obj) {
    if (!obj || (uintptr_t)obj < sizeof(AimlModelHeader)) return 0;
    const AimlModelHeader* hdr = (const AimlModelHeader*)((const char*)obj - sizeof(AimlModelHeader));
    if (hdr->magic != AIML_MODEL_MAGIC) return 0;
    return hdr->refcount;
}
