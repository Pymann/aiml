#include "model_runtime.h"
#include "ser_runtime.h"        // aiml_ser_free_decoded + AimlTypeNode
#include "mem_diff_runtime.h"   // AIML_MEM_DIFF leaf-allocation tracking (no-op when disabled)
#include "log_runtime.h"        // AIML_MODEL_TRACE diagnostic ring + loud UAF reports
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unordered_map>

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

// A freed shell's header magic is overwritten with this instead of 0, so a later
// incref/decref/read on a dangling model pointer is recognisably a use-after-free
// ("FREE" in little-endian memory) rather than an ambiguous zero. See the loud
// reports below and doc/aiml_v2_impl_plan.md (heisenbug hunt).
static const uint32_t AIML_MODEL_FREED_MAGIC = 0x45455246u;  // 'F''R''E''E'

// ── Trace bookkeeping (AIML_MODEL_TRACE only) ────────────────────────────────
// A side table of live shell sizes, populated at alloc and consulted at free so
// quarantine (level >= 2) can poison the exact struct extent without widening the
// 8-byte header. Guarded by its own mutex; touched only when tracing is on.
namespace {
std::mutex                              g_model_sizes_mutex;
std::unordered_map<const void*, size_t> g_model_sizes;

void note_alloc_size(const void* obj, size_t structSize) {
    std::lock_guard<std::mutex> lock(g_model_sizes_mutex);
    g_model_sizes[obj] = structSize;
}
// Returns the recorded size and forgets it; 0 if unknown.
size_t take_alloc_size(const void* obj) {
    std::lock_guard<std::mutex> lock(g_model_sizes_mutex);
    auto it = g_model_sizes.find(obj);
    if (it == g_model_sizes.end()) return 0;
    size_t s = it->second;
    g_model_sizes.erase(it);
    return s;
}

// Report a use-after-free on a poisoned (already-freed) shell: dump the object's
// recent lifetime history so the premature decref that freed it is visible.
void report_uaf(const char* op, void* obj, const AimlModelHeader* hdr) {
    char buf[96];
    snprintf(buf, sizeof(buf), "v2 model use-after-free: %s on freed shell (magic=FREE, rc=%u)",
             op, hdr->refcount);
    aiml_log_error(buf, "model_runtime");
    aiml_trace_record("UAF", obj, hdr->magic, hdr->refcount, op);
    aiml_trace_dump(obj, buf);
}
} // namespace

extern "C" void* aiml_model_alloc(size_t structSize) {
    void* mem = malloc(sizeof(AimlModelHeader) + structSize);
    if (!mem) return nullptr;
    AimlModelHeader* hdr = (AimlModelHeader*)mem;
    hdr->magic    = AIML_MODEL_MAGIC;
    hdr->refcount = 1;
    void* obj = (char*)mem + sizeof(AimlModelHeader);
    memset(obj, 0, structSize);   // aiml_ser_* precondition: heap pointer fields NULL
    aiml_memdiff_record(obj, sizeof(AimlModelHeader) + structSize);
    if (aiml_trace_level()) {
        note_alloc_size(obj, structSize);
        aiml_trace_record("ALLOC", obj, (unsigned long)structSize, 1, "size");
    }
    return obj;
}

extern "C" void aiml_model_incref(void* obj) {
    if (!obj || (uintptr_t)obj < sizeof(AimlModelHeader)) return;
    AimlModelHeader* hdr = (AimlModelHeader*)((char*)obj - sizeof(AimlModelHeader));
    if (hdr->magic == AIML_MODEL_MAGIC) {
        hdr->refcount++;
        aiml_trace_record("incref", obj, AIML_MODEL_MAGIC, hdr->refcount, nullptr);
        return;
    }
    if (hdr->magic == AIML_MODEL_FREED_MAGIC) { report_uaf("incref", obj, hdr); return; }
    // stack model / borrowed view — not counted
    aiml_trace_record("incref?", obj, hdr->magic, 0, "non-MODL");
}

// Shared tail for the two decref entry points: validates the header, reports a
// use-after-free on a poisoned shell, and returns 1 iff the caller should free
// (refcount reached 0, header now poisoned). Returns 0 for still-referenced,
// non-MODL, or already-freed pointers.
static int model_dec_common(void* obj, const char* op) {
    if (!obj || (uintptr_t)obj < sizeof(AimlModelHeader)) return 0;
    AimlModelHeader* hdr = (AimlModelHeader*)((char*)obj - sizeof(AimlModelHeader));
    if (hdr->magic == AIML_MODEL_FREED_MAGIC) { report_uaf(op, obj, hdr); return 0; }
    if (hdr->magic != AIML_MODEL_MAGIC) {       // stack model / borrowed view — not freed here
        aiml_trace_record("decref?", obj, hdr->magic, 0, "non-MODL");
        return 0;
    }
    if (hdr->refcount <= 1) {
        hdr->magic = AIML_MODEL_FREED_MAGIC;    // poison to catch use-after-free
        aiml_trace_record("FREE", obj, AIML_MODEL_FREED_MAGIC, 0, op);
        return 1;
    }
    hdr->refcount--;
    aiml_trace_record("decref", obj, AIML_MODEL_MAGIC, hdr->refcount, op);
    return 0;
}

// Poison the freed shell's struct bytes (quarantine, level >= 2) so a later
// dangling auto-deref read hits 0xDD… (crash at 0xdddddddddddddddd, unmistakable)
// instead of silently reused memory. Returns true if the shell was quarantined
// (kept, not returned to the allocator).
static bool quarantine_shell(void* obj) {
    if (aiml_trace_level() < 2) return false;
    size_t sz = take_alloc_size(obj);
    if (sz) memset(obj, 0xDD, sz);
    return true;   // keep the allocation; header stays FREE-poisoned
}

extern "C" void aiml_model_decref(void* obj, const void* nodes, int32_t n_nodes) {
    if (!model_dec_common(obj, "decref")) return;
    // refcount hit 0: deep-free the inner heap BEFORE releasing the shell.
    if (nodes && n_nodes > 0)
        aiml_ser_free_decoded(obj, (const AimlTypeNode*)nodes, n_nodes);
    aiml_memdiff_forget(obj);
    if (quarantine_shell(obj)) return;           // level >= 2: keep poisoned shell
    if (aiml_trace_level()) take_alloc_size(obj); // level 1: drop the size record
    free((char*)obj - sizeof(AimlModelHeader));
}

extern "C" uint32_t aiml_model_refcount(const void* obj) {
    if (!obj || (uintptr_t)obj < sizeof(AimlModelHeader)) return 0;
    const AimlModelHeader* hdr = (const AimlModelHeader*)((const char*)obj - sizeof(AimlModelHeader));
    if (hdr->magic != AIML_MODEL_MAGIC) return 0;
    return hdr->refcount;
}

extern "C" int aiml_model_dec_hits_zero(void* obj) {
    // Same validation + poisoning as decref, but the caller frees the inner heap
    // (its field descriptors start at a subtree index, not node 0) and the shell.
    return model_dec_common(obj, "dec_hits_zero");
}

extern "C" void aiml_model_free_shell(void* obj) {
    if (!obj || (uintptr_t)obj < sizeof(AimlModelHeader)) return;
    aiml_memdiff_forget(obj);
    if (quarantine_shell(obj)) return;           // level >= 2: keep poisoned shell
    if (aiml_trace_level()) take_alloc_size(obj); // level 1: drop the size record
    free((char*)obj - sizeof(AimlModelHeader));
}
