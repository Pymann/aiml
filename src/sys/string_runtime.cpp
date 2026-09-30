#include "string_runtime.h"
#include "mem_diff_runtime.h"   // AIML_MEM_DIFF leaf-allocation tracking (no-op when disabled)
#include "log_runtime.h"        // AIML_MODEL_TRACE diagnostic ring (no-op when disabled)
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <cstdio>

// Must match GenericList layout (list_impl.cpp / path_runtime.cpp)
struct AimlListRaw {
    int32_t size;
    int32_t capacity;
    void**  data;
};

// ── Handle lifecycle trace (AIML_TRACE_FRE) ─────────────────────────────────
// Debug-only: log every list/map handle new / incref / release / fre-decref with
// the handle-block pointer + rc, to pinpoint a double-free (an address whose rc
// drops to 0 and is freed, then freed again). No-op unless AIML_TRACE_FRE is set.
static inline bool aiml_trace_fre() {
    static int on = -1;
    if (on < 0) { const char* e = getenv("AIML_TRACE_FRE"); on = (e && *e) ? 1 : 0; }
    return on == 1;
}
extern "C" void aiml_trace_fre_log(const char* tag, void* h, int rc) {
    if (aiml_trace_fre() && h)
        fprintf(stderr, "[FRE] %-10s h=%p rc=%d\n", tag, h, rc);
}

// ── Refcounted string allocator (UAF protection) ─────────────────────────────
// Strings allocated via aiml_str_alloc get an 8-byte header prefixed:
//   {uint32_t magic = 'STRN', uint32_t refcount}
// Returned pointer skips the header → user code sees a normal char*.
// Decref-on-zero frees the whole allocation.
//
// Literals (.rodata) don't have the header.  incref/decref check the magic at
// `s - 8`; if it doesn't match, they no-op (literal / unmanaged buffer).
// False-positive risk: ~2^-32 per literal.
//
// Why bother: mirror's aiml_ser_free_decoded must release deserialized strings
// when a session closes (or a delta replaces them).  But user code may have
// aliased those string pointers into local variables.  Refcounting keeps the
// strings alive as long as any reference exists.
// Repro: tests/test_mirror_close_string_uaf.aiml
#define AIML_STR_MAGIC 0x5354524Eu   // "STRN" in little-endian memory

struct AimlStrHeader {
    uint32_t magic;
    uint32_t refcount;
};

extern "C" char* aiml_str_alloc(size_t len) {
    void* mem = malloc(sizeof(AimlStrHeader) + len + 1);
    if (!mem) return nullptr;
    AimlStrHeader* hdr = (AimlStrHeader*)mem;
    hdr->magic    = AIML_STR_MAGIC;
    hdr->refcount = 1;
    char* data = (char*)mem + sizeof(AimlStrHeader);
    data[len] = '\0';
    aiml_memdiff_record(data, sizeof(AimlStrHeader) + len + 1);
    return data;
}

// IMMORTAL empty string: a single static "" with the STRN header and a sentinel refcount that
// incref/decref never touch and never free. Used for the DEFAULT value of unset model string
// fields (spec/61): a `.rodata` "" literal there is unsafe under drops-on — aiml_ser_free_decoded
// decref's every string field, and decref of a raw literal reads its `-8` header (may fault at a
// page boundary, or false-positive the STRN magic and free() rodata → SIGSEGV, the drops-on
// ZoneEntry-free crash). This immortal, header-carrying "" is safe to decref (no-op), `.len`→0.
#define AIML_STR_IMMORTAL 0xFFFFFFFFu
extern "C" char* aiml_str_empty(void) {
    static struct { AimlStrHeader hdr; char data[1]; } g_empty = { { AIML_STR_MAGIC, AIML_STR_IMMORTAL }, { '\0' } };
    return g_empty.data;
}

// aiml_dbg_strcmp: retained as a thin strcmp delegate.  The compiler emits a
// direct `strcmp` for AIML string ==/!= again (the instrumented variant was a
// WASM string-corruption hunt, root-caused to a serializer stride bug — see
// container_abi.h).  Kept so the exported symbol/header decl stay valid.
extern "C" int aiml_dbg_strcmp(const char* a, const char* b) {
    return strcmp(a, b);
}

extern "C" char* aiml_str_dup(const char* s) {
    if (!s) return aiml_str_alloc(0);
    size_t len = strlen(s);
    char* dst = aiml_str_alloc(len);
    if (!dst) return nullptr;
    memcpy(dst, s, len);
    return dst;
}

extern "C" char* aiml_str_ndup(const char* s, size_t len) {
    if (!s) return aiml_str_alloc(0);
    // Honour C strndup semantics: stop at \0 even if len is larger.
    size_t actual = 0;
    while (actual < len && s[actual] != '\0') actual++;
    char* dst = aiml_str_alloc(actual);
    if (!dst) return nullptr;
    memcpy(dst, s, actual);
    return dst;
}

extern "C" void aiml_str_incref(char* s) {
    if (!s || (uintptr_t)s < sizeof(AimlStrHeader)) return;
    AimlStrHeader* hdr = (AimlStrHeader*)((char*)s - sizeof(AimlStrHeader));
    if (hdr->magic != AIML_STR_MAGIC) return;
    if (hdr->refcount == AIML_STR_IMMORTAL) return;   // immortal "" default — never counted
    hdr->refcount++;
}

extern "C" void aiml_str_decref(char* s) {
    if (!s || (uintptr_t)s < sizeof(AimlStrHeader)) return;
    AimlStrHeader* hdr = (AimlStrHeader*)((char*)s - sizeof(AimlStrHeader));
    if (hdr->magic != AIML_STR_MAGIC) return;
    if (hdr->refcount == AIML_STR_IMMORTAL) return;   // immortal "" default — never freed
    if (hdr->refcount <= 1) {
        hdr->magic = 0;  // poison to catch use-after-free in next access
        aiml_memdiff_forget(s);
        free(hdr);
    } else {
        hdr->refcount--;
    }
}

// ── spec/61 §7 managed string-local assignment helpers ───────────────────────
// See string_runtime.h. These maintain the invariant "slot = null | one owned RC ref" and
// never dereference a header before a foreign/possibly-freed address (dup reads only content;
// the released value is the slot's own owned reference).
extern "C" int32_t aiml_str_assign_copy(char** dst, const char* src) {
    if (!dst) return 0;
    char* next = aiml_str_dup(src);   // acquire an independent RC reference BEFORE releasing old
    if (!next && src) return 0;        // allocation failure → leave slot unchanged (src!=null case)
    char* old = *dst;
    *dst = next;
    aiml_str_decref(old);              // safe: `old` was this slot's own owned ref (or null)
    return 1;
}

extern "C" void aiml_str_assign_move(char** dst, char* next) {
    if (!dst) return;
    char* old = *dst;
    *dst = next;                       // `next` is a separate owned reference (or null)
    aiml_str_decref(old);
}

// ── spec/61 §4 target: SHARE on assignment (incref), not dup (AIML_MEM_V3 gate) ──────────────
// `set: [a, b]` where b is a borrow SHARES b's buffer (incref) instead of deep-copying it.
// Secure-before-release order (§5.5): incref the source FIRST, then decref the old dst — so a
// self-assignment `set:[x,x]` (src == old) is a safe no-op (rc +1 then −1). A literal / immortal
// src has no header → incref is a no-op (still shared safely). Distinct from aiml_str_assign_copy
// (which dups): this is the immutable-string share of §4, used only under AIML_MEM_V3.
extern "C" int32_t aiml_str_assign_share(char** dst, char* src) {
    if (!dst) return 0;
    aiml_str_incref(src);              // acquire a shared reference BEFORE releasing old
    char* old = *dst;
    *dst = src;
    aiml_str_decref(old);             // safe even if old == src (net rc unchanged)
    return 1;
}

extern "C" char* aiml_str_take_local(char** src) {
    if (!src) return nullptr;
    char* value = *src;
    *src = nullptr;                    // slot nulled → its later scope-drop / decref-old is a no-op
    return value;
}

// ── spec/61 list scope-drop primitive ───────────────────────────────────────
// Frees an owned list<T> value-local's heap at scope exit (visitBlockStmt, kind 2).
// elemKind: 0 = raw/scalar (free the data buffer only); 1 = string (decref each
// element char*, then free the buffer); 2 = model (free each element struct's heap
// fields via aiml_ser_free_decoded using elemSize as the stride + serNodes, then free
// the buffer).  The list owns its elements by MOVE (ListImpl::push stores the raw value,
// no incref/copy — see list_impl.cpp), so exactly one free per element balances the
// original alloc.  Idempotent: nulls the struct so a double-drop (should not happen
// given move-detection) is a no-op, not a crash.
extern "C" void aiml_ser_free_decoded(void* obj, void* nodes, int32_t node_count);
extern "C" void aiml_ser_retain(void* obj, const void* nodes, int32_t n_nodes);
// AIML v2: decref a %M* reference element/value (frees inner heap + shell at refcount 0).
extern "C" void aiml_model_decref(void* obj, const void* nodes, int32_t n_nodes);
extern "C" void aiml_model_incref(void* obj);
extern "C" void aiml_list_free(void* list_ptr, int32_t elemKind,
                               int64_t elemSize, void* serNodes, int32_t serNodesCount) {
    if (!list_ptr) return;
    AimlListRaw* lst = static_cast<AimlListRaw*>(list_ptr);
    if (!lst->data) { lst->size = 0; lst->capacity = 0; return; }
    if (elemKind == 1) {
        char** elems = reinterpret_cast<char**>(lst->data);
        for (int32_t i = 0; i < lst->size; ++i)
            if (elems[i]) aiml_str_decref(elems[i]);
    } else if (elemKind == 2 && serNodes && elemSize > 0) {
        char* base = reinterpret_cast<char*>(lst->data);
        for (int32_t i = 0; i < lst->size; ++i)
            aiml_ser_free_decoded(base + (int64_t)i * elemSize, serNodes, serNodesCount);
    } else if (elemKind == 3 && serNodes) {
        // AIML v2 P3: elements are %M* references — decref each (order-independent, frees at 0).
        char** elems = reinterpret_cast<char**>(lst->data);
        aiml_trace_record("list_free3", list_ptr, (unsigned long)lst->data, (unsigned long)lst->size, "elemK3");
        for (int32_t i = 0; i < lst->size; ++i)
            if (elems[i]) aiml_model_decref(elems[i], serNodes, serNodesCount);
    } else if (elemKind == 4) {
        // spec/61 §5.5 (memV3): list<ptr<T>> elements are OWNING boxes ([fields], no rc). Deep-free
        // each box's heap fields (when it has a serNodes descriptor) then free the box shell. push
        // moved the box in (one owner) → exactly one free per element.
        char** elems = reinterpret_cast<char**>(lst->data);
        for (int32_t i = 0; i < lst->size; ++i)
            if (elems[i]) {
                if (serNodes) aiml_ser_free_decoded(elems[i], serNodes, serNodesCount);
                free(elems[i]);
            }
    } else if (elemKind == 5 && serNodes) {
        // spec/61 §5.7 (memV3): a NESTED CONTAINER element is a refcounted HANDLE. Each element slot
        // holds a handle pointer; release it via its handle-rooted descriptor (LIST_PTR/MAP_PTR/
        // BUFFER_PTR root) → decref the inner handle, recursively releasing its own elements at rc 0.
        // elemSize is the pointer stride; the descriptor root reads the handle at the slot address.
        char* base = reinterpret_cast<char*>(lst->data);
        for (int32_t i = 0; i < lst->size; ++i)
            aiml_ser_free_decoded(base + (int64_t)i * elemSize, serNodes, serNodesCount);
    } else if (elemKind == 6 && elemSize > 0) {
        // interface polymorphism: list<ptr<Interface>> elements are 16-byte fat pointers
        // {obj, vtable} (elemSize = sizeof(AimlFatPtr_<Iface>)). Stride by elemSize and free ONLY the
        // obj pointer (field 0) — the concrete implementation object push moved in (one owner). A
        // char** (8B) walk would misread the vtable global at odd indices and free() it → "invalid
        // pointer" (test_card_base_engine_api). The concrete type is dynamic, so there is no serNodes
        // to deep-free the obj's own heap fields here; the obj shell is freed shallow (behaviors are
        // field-less — a stateful implementor's inner heap fields would leak, not corrupt).
        char* base = reinterpret_cast<char*>(lst->data);
        for (int32_t i = 0; i < lst->size; ++i) {
            void* obj = *reinterpret_cast<void**>(base + (int64_t)i * elemSize);  // field 0 = obj
            if (obj) free(obj);
        }
    }
    if (lst->data && aiml_trace_fre())
        fprintf(stderr, "[FRE] %-10s h=%p rc=%d  (elemKind=%d size=%d)\n", "LDATA", lst->data, elemKind, elemKind, lst->size);
    free(lst->data);
    lst->data = nullptr;
    lst->size = 0;
    lst->capacity = 0;
}

// spec/61 §5.4: free the OWNED heap reachable from ONE list element slot, WITHOUT touching the data
// buffer or size. Mirrors the per-element branches of aiml_list_free. Used by clear()/pop() so a
// drained element's ownership is released mid-lifetime (the pop-loop / clear() idiom) — not only at
// the list's scope drop. clear()/pop() shrink `size` after the release, so aiml_list_free never
// revisits these slots. elemKind 2 (value-model inline) is released too: since CopyRetain a value
// copy taken from the element owns its own refs and survives (tests/lifetime/73).
static void free_list_elem_at(char* slot, int32_t elemKind, int64_t /*elemSize*/,
                              void* serNodes, int32_t serNodesCount) {
    if (!slot) return;
    switch (elemKind) {
        case 1: { char* s = *reinterpret_cast<char**>(slot); if (s) aiml_str_decref(s); break; }
        case 3: { char* m = *reinterpret_cast<char**>(slot); if (m) aiml_model_decref(m, serNodes, serNodesCount); break; }
        case 4: { char* box = *reinterpret_cast<char**>(slot);
                  if (box) { if (serNodes) aiml_ser_free_decoded(box, serNodes, serNodesCount); free(box); } break; }
        case 5: { if (serNodes) aiml_ser_free_decoded(slot, serNodes, serNodesCount); break; }  // nested handle (slot-rooted)
        case 6: { void* obj = *reinterpret_cast<void**>(slot); if (obj) free(obj); break; }      // iface obj (field 0)
        case 2: { if (serNodes) aiml_ser_free_decoded(slot, serNodes, serNodesCount); break; }
        default: break;  // elemKind 0: scalar/POD, nothing to free
    }
}

// spec/61 §5.4: clear() — release every element's owned heap, then reset size to 0 KEEPING the data
// buffer/capacity for reuse.
extern "C" void aiml_list_clear_elems(void* list_ptr, int32_t elemKind, int64_t elemSize,
                                      void* serNodes, int32_t serNodesCount) {
    if (!list_ptr) return;
    AimlListRaw* lst = static_cast<AimlListRaw*>(list_ptr);
    if (lst->data && elemKind != 0) {
        char* base = reinterpret_cast<char*>(lst->data);
        int64_t stride = elemSize > 0 ? elemSize : 8;
        for (int32_t i = 0; i < lst->size; ++i)
            free_list_elem_at(base + (int64_t)i * stride, elemKind, elemSize,
                              serNodes, serNodesCount);
    }
    lst->size = 0;
}

// spec/61 §5.4: pop() whose result is DISCARDED — release the last element's owned heap, then size--.
// Same element-ownership release as clear(); keeps the buffer. (A BOUND pop — decl x = l.pop — goes
// through ListImpl::pop and the local owns/drops the result instead.)
extern "C" void aiml_list_pop_free(void* list_ptr, int32_t elemKind, int64_t elemSize,
                                   void* serNodes, int32_t serNodesCount) {
    if (!list_ptr) return;
    AimlListRaw* lst = static_cast<AimlListRaw*>(list_ptr);
    if (lst->size <= 0) return;
    if (lst->data && elemKind != 0) {
        char* base = reinterpret_cast<char*>(lst->data);
        int64_t stride = elemSize > 0 ? elemSize : 8;
        free_list_elem_at(base + (int64_t)(lst->size - 1) * stride, elemKind, elemSize,
                          serNodes, serNodesCount);
    }
    lst->size -= 1;
}

// ── spec/61 §5 Option B: list<T> as a refcounted HANDLE (memV3 only) ──────────
// A list<T> value-local under AIML_MEM_V3 is a heap block laid out as
//   [ int32 rc ][ int32 pad ][ AimlListRaw {size,cap,data} ]
// and the pointer stored in the variable slot points at the AimlListRaw part, so
// every ListImpl op (which GEPs a GenericList) is UNCHANGED — the rc lives 8
// bytes before the returned pointer. Assignment increfs (aliasing); scope-exit
// decrefs and frees data + block at rc 0. Kept out of the OFF/deployed path
// (list stays an inline value struct there).
extern "C" void* aiml_list_handle_new() {
    // 8-byte header + AimlListRaw (16 bytes on 64-bit); calloc → size=cap=0, data=null.
    char* block = static_cast<char*>(calloc(1, 8 + sizeof(AimlListRaw)));
    if (!block) return nullptr;
    *reinterpret_cast<int32_t*>(block) = 1;   // rc = 1
    aiml_trace_fre_log("LNEW", block + 8, 1);
    return block + 8;                          // pointer at the AimlListRaw
}
extern "C" void aiml_list_handle_incref(void* h) {
    if (!h) return;
    int32_t* rc = reinterpret_cast<int32_t*>(static_cast<char*>(h) - 8);
    (*rc)++;
    aiml_trace_fre_log("LINC", h, *rc);
}
// decref; at rc 0 free the elements + data buffer (via aiml_list_free) and the block.
extern "C" void aiml_list_handle_release(void* h, int32_t elemKind,
                                         int64_t elemSize, void* serNodes,
                                         int32_t serNodesCount) {
    if (!h) return;
    int32_t* rc = reinterpret_cast<int32_t*>(static_cast<char*>(h) - 8);
    aiml_trace_fre_log("LREL", h, *rc - 1);
    if (--(*rc) > 0) return;
    aiml_trace_fre_log("LFREE", h, *rc);
    aiml_list_free(h, elemKind, elemSize, serNodes, serNodesCount);  // frees data + elements
    free(static_cast<char*>(h) - 8);                                  // free the handle block
}
// {copy: L} — new INDEPENDENT-spine handle: fresh data buffer, elements copied by
// value (referents shared; strings/models are pointers copied verbatim then
// incref'd by the caller per elemKind). elemSize = per-element stride in bytes.
extern "C" void* aiml_list_handle_copy(void* src, int64_t elemSize, int32_t elemKind) {
    void* h = aiml_list_handle_new();
    if (!h || !src) return h;
    AimlListRaw* s = static_cast<AimlListRaw*>(src);
    AimlListRaw* d = static_cast<AimlListRaw*>(h);
    int32_t n = s->size;
    if (n <= 0 || !s->data) return h;
    d->size = n; d->capacity = n;
    d->data = static_cast<void**>(malloc((size_t)n * (size_t)elemSize));
    memcpy(d->data, s->data, (size_t)n * (size_t)elemSize);
    // Shallow copy: shared referents must be retained so both spines can free.
    if (elemKind == 1) {                       // list<string>: incref each shared string
        char** e = reinterpret_cast<char**>(d->data);
        for (int32_t i = 0; i < n; ++i) if (e[i]) aiml_str_incref(e[i]);
    } else if (elemKind == 3) {                 // list<shared/%M*>: incref each shared model
        void** e = reinterpret_cast<void**>(d->data);
        for (int32_t i = 0; i < n; ++i) if (e[i]) aiml_model_incref(e[i]);
    } else if (elemKind == 5) {                 // spec/61 §5.7: nested container handle — share (incref)
        // all list/map/buffer handles carry the same [i32 rc][i32 pad] header, so a generic rc
        // bump at (handle-8) co-owns the inner container regardless of its kind (shallow copy).
        void** e = reinterpret_cast<void**>(d->data);
        for (int32_t i = 0; i < n; ++i)
            if (e[i]) ++(*reinterpret_cast<int32_t*>(static_cast<char*>(e[i]) - 8));
    }
    return h;
}

// spec/61 §5.1: after a bit-copy of inline value-model elements (shallow `copy` / `slice`), retain
// each element member-wise (strings/handles incref'd) so both lists own balanced references.
extern "C" void aiml_list_retain_elems(void* list, int64_t elemSize, const void* nodes, int32_t n_nodes) {
    if (!list || !nodes) return;
    AimlListRaw* l = static_cast<AimlListRaw*>(list);
    char* base = reinterpret_cast<char*>(l->data);
    for (int32_t i = 0; base && i < l->size; ++i) aiml_ser_retain(base + (int64_t)i * elemSize, nodes, n_nodes);
}

// ── spec/61 §5 Option B (memV3): buffer as a refcounted HANDLE ────────────────
// Buffer layout (mirrors compress_runtime.h / BufferImpl): { i32 size, i32 cap, i32 read,
// i32 write, i8* data } = 24 bytes on 64-bit. Handle = [i32 rc][i32 pad][AimlBufRaw]; the stored
// pointer targets the AimlBufRaw so every BufferImpl op is unchanged. No element ownership (raw
// bytes) → release just frees the data buffer + block. memV3-only (buffer stays inline under OFF).
struct AimlBufRaw { int32_t size; int32_t capacity; int32_t read_idx; int32_t write_idx; void* data; };
extern "C" void* aiml_buffer_handle_new() {
    char* block = static_cast<char*>(calloc(1, 8 + sizeof(AimlBufRaw)));
    if (!block) return nullptr;
    *reinterpret_cast<int32_t*>(block) = 1;   // rc = 1
    return block + 8;
}
extern "C" void aiml_buffer_handle_incref(void* h) {
    if (!h) return;
    (*reinterpret_cast<int32_t*>(static_cast<char*>(h) - 8))++;
}
extern "C" void aiml_buffer_handle_release(void* h) {
    if (!h) return;
    int32_t* rc = reinterpret_cast<int32_t*>(static_cast<char*>(h) - 8);
    if (--(*rc) > 0) return;
    AimlBufRaw* b = static_cast<AimlBufRaw*>(h);
    free(b->data); b->data = nullptr;
    free(static_cast<char*>(h) - 8);
}
// {copy: B} — new INDEPENDENT buffer: fresh data block (capacity bytes) with the bytes copied.
extern "C" void* aiml_buffer_handle_copy(void* src) {
    void* h = aiml_buffer_handle_new();
    if (!h || !src) return h;
    AimlBufRaw* s = static_cast<AimlBufRaw*>(src);
    AimlBufRaw* d = static_cast<AimlBufRaw*>(h);
    d->size = s->size; d->capacity = s->capacity;
    d->read_idx = s->read_idx; d->write_idx = s->write_idx;
    if (s->capacity > 0) {
        d->data = malloc((size_t)s->capacity);
        if (d->data && s->data) memcpy(d->data, s->data, (size_t)s->capacity);
    }
    return h;
}

// ── spec/61 map scope-drop primitive ────────────────────────────────────────
// Layout mirrors MapImpl (map_impl.cpp): { i32 size, i32 capacity, uint8_t* occ,
// void* keys, void* vals }.  occ[i]==1 marks slot i occupied.  Frees the occ/keys/vals
// buffers and, per occupied slot, decref's string keys/values (kind 1) and frees model
// values' heap (kind 2, via valStride + valNodes).  Same safety profile as list-drop:
// a map<K:V> value-local is a stack struct (by-ref), it does not escape by design.
struct AimlMapRaw { int32_t size; int32_t capacity; uint8_t* occ; void* keys; void* vals; };
extern "C" void aiml_map_free(void* map_ptr, int32_t keyKind, int64_t keyStride,
                              int32_t valKind, int64_t valStride,
                              void* valNodes, int32_t valNodesCount) {
    if (!map_ptr) return;
    AimlMapRaw* m = static_cast<AimlMapRaw*>(map_ptr);
    // MapImpl (map_impl.cpp) is a DENSE store: put appends at keys[size]/vals[size] and bumps
    // size; get/contains/remove linear-scan [0,size). The `occ` bitmap is vestigial (never written
    // by put — walking it read past the never-grown occ buffer, the old "Invalid read of size 1").
    // Reclaim over the dense range [0,size): put OWNS its string keys/values via dup (§8), so one
    // decref per live entry balances that dup exactly.
    // Release pass whenever a key OR value kind owns something (independent of the key type).
    bool valueNeedsRelease = (valKind == 1 || valKind == 2 || valKind == 3 || valKind == 5);
    if (keyKind == 1 || valueNeedsRelease) {
        char* keyBase = reinterpret_cast<char*>(m->keys);
        char* valBase = reinterpret_cast<char*>(m->vals);
        for (int32_t i = 0; i < m->size; ++i) {
            if (keyKind == 1 && keyBase) {
                char* k = *reinterpret_cast<char**>(keyBase + (int64_t)i * keyStride);
                if (k) aiml_str_decref(k);
            }
            if (valKind == 1 && valBase) {
                char* v = *reinterpret_cast<char**>(valBase + (int64_t)i * valStride);
                if (v) aiml_str_decref(v);
            } else if (valKind == 2 && valBase && valNodes) {
                aiml_ser_free_decoded(valBase + (int64_t)i * valStride, valNodes, valNodesCount);
            } else if (valKind == 5 && valBase) {
                // spec/61 §6.1: owning ptr<T> box value → free fields (if any) + shell
                void* v = *reinterpret_cast<void**>(valBase + (int64_t)i * valStride);
                if (v) { if (valNodes) aiml_ser_free_decoded(v, valNodes, valNodesCount); free(v); }
            } else if (valKind == 3 && valBase && valNodes) {
                // AIML v2 P3: map<K:Model> value is a %M* reference — decref it.
                char* v = *reinterpret_cast<char**>(valBase + (int64_t)i * valStride);
                aiml_trace_record("map_free3", map_ptr, (unsigned long)v, (unsigned long)i, "valK3");
                if (v) aiml_model_decref(v, valNodes, valNodesCount);
            }
        }
    }
    if (m->keys) aiml_trace_fre_log("MKEYS", m->keys, valKind);
    free(m->occ);  m->occ  = nullptr;
    free(m->keys); m->keys = nullptr;
    free(m->vals); m->vals = nullptr;
    m->size = 0; m->capacity = 0;
}

// ── spec/61 §5 Option B (memV3): map<K:V> as a refcounted HANDLE ──────────────
// Same discipline as the list handle: a heap block [i32 rc][i32 pad][AimlMapRaw] with the
// stored pointer at the AimlMapRaw part, so every MapImpl op is unchanged. Assignment increfs
// (aliasing); scope-exit decrefs and frees at rc 0. memV3-only (map stays inline under OFF).
extern "C" void* aiml_map_handle_new() {
    char* block = static_cast<char*>(calloc(1, 8 + sizeof(AimlMapRaw)));
    if (!block) return nullptr;
    *reinterpret_cast<int32_t*>(block) = 1;   // rc = 1
    aiml_trace_fre_log("MNEW", block + 8, 1);
    return block + 8;
}
extern "C" void aiml_map_handle_incref(void* h) {
    if (!h) return;
    (*reinterpret_cast<int32_t*>(static_cast<char*>(h) - 8))++;
    aiml_trace_fre_log("MINC", h, *reinterpret_cast<int32_t*>(static_cast<char*>(h) - 8));
}
extern "C" void aiml_map_handle_release(void* h, int32_t keyKind, int64_t keyStride,
                                        int32_t valKind, int64_t valStride,
                                        void* valNodes, int32_t valNodesCount) {
    if (!h) return;
    int32_t* rc = reinterpret_cast<int32_t*>(static_cast<char*>(h) - 8);
    aiml_trace_fre_log("MREL", h, *rc - 1);
    if (--(*rc) > 0) return;
    aiml_trace_fre_log("MFREE", h, *rc);
    aiml_map_free(h, keyKind, keyStride, valKind, valStride, valNodes, valNodesCount);
    free(static_cast<char*>(h) - 8);
}
// {copy: M} — new INDEPENDENT-spine map: fresh dense arrays, entries value-copied; shared string/
// model referents incref'd (both spines can then free). keyKind/valKind: 0 scalar, 1 string, 3 %M*.
extern "C" void* aiml_map_handle_copy(void* src, int64_t keyStride, int64_t valStride,
                                      int32_t keyKind, int32_t valKind) {
    void* h = aiml_map_handle_new();
    if (!h || !src) return h;
    AimlMapRaw* s = static_cast<AimlMapRaw*>(src);
    AimlMapRaw* d = static_cast<AimlMapRaw*>(h);
    int32_t n = s->size;
    if (n <= 0) return h;
    d->size = n; d->capacity = n;
    d->occ  = static_cast<uint8_t*>(calloc((size_t)n, 1));
    d->keys = malloc((size_t)n * (size_t)keyStride);
    d->vals = malloc((size_t)n * (size_t)valStride);
    if (s->keys) memcpy(d->keys, s->keys, (size_t)n * (size_t)keyStride);
    if (s->vals) memcpy(d->vals, s->vals, (size_t)n * (size_t)valStride);
    if (keyKind == 1 && d->keys) {
        char* kb = reinterpret_cast<char*>(d->keys);
        for (int32_t i = 0; i < n; ++i) { char* k = *reinterpret_cast<char**>(kb + (int64_t)i*keyStride); if (k) aiml_str_incref(k); }
    }
    if (valKind == 1 && d->vals) {
        char* vb = reinterpret_cast<char*>(d->vals);
        for (int32_t i = 0; i < n; ++i) { char* v = *reinterpret_cast<char**>(vb + (int64_t)i*valStride); if (v) aiml_str_incref(v); }
    } else if (valKind == 3 && d->vals) {
        char* vb = reinterpret_cast<char*>(d->vals);
        for (int32_t i = 0; i < n; ++i) { void* v = *reinterpret_cast<void**>(vb + (int64_t)i*valStride); if (v) aiml_model_incref(v); }
    } else if (valKind == 4 && d->vals) {
        // spec/61 §5.7: shallow copy shares each nested container handle (rc at h-8 for list/map/buffer).
        char* vb = reinterpret_cast<char*>(d->vals);
        for (int32_t i = 0; i < n; ++i) { void* v = *reinterpret_cast<void**>(vb + (int64_t)i*valStride); if (v) aiml_list_handle_incref(v); }
    }
    return h;
}

// ── string.lower ─────────────────────────────────────────────────────────────
extern "C" char* aiml_str_lower(const char* s) {
    if (!s) return aiml_str_alloc(0);
    char* out = aiml_str_dup(s);
    for (char* p = out; *p; ++p) *p = (char)tolower((unsigned char)*p);
    return out;
}

// ── string.upper ─────────────────────────────────────────────────────────────
extern "C" char* aiml_str_upper(const char* s) {
    if (!s) return aiml_str_alloc(0);
    char* out = aiml_str_dup(s);
    for (char* p = out; *p; ++p) *p = (char)toupper((unsigned char)*p);
    return out;
}

// ── string.title ─────────────────────────────────────────────────────────────
// Python-style title case: uppercase first char of every whitespace-separated
// word, lowercase the rest.  Punctuation (including `_` and `-`) is NOT
// treated as a word break — it stays in place and only character casing
// changes.  For snake_case → display labels chain with `.replace`:
//     - decl: [pretty, string, { call: [{ call: [s.replace, "_", " "] }.title] }]
// Examples:
//   "hello world"  → "Hello World"
//   "WORKOUT"      → "Workout"
//   "foo_bar"      → "Foo_bar"   (no separator-replace; use .replace first)
extern "C" char* aiml_str_title(const char* s) {
    if (!s) return aiml_str_alloc(0);
    char* out = aiml_str_dup(s);
    bool at_word_start = true;
    for (char* p = out; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (isspace(c)) {
            at_word_start = true;
        } else if (at_word_start) {
            *p = (char)toupper(c);
            at_word_start = false;
        } else {
            *p = (char)tolower(c);
        }
    }
    return out;
}

// ── string.trim ──────────────────────────────────────────────────────────────
extern "C" char* aiml_str_trim(const char* s) {
    if (!s) return aiml_str_alloc(0);
    // skip leading whitespace
    while (*s && isspace((unsigned char)*s)) ++s;
    if (*s == '\0') return aiml_str_alloc(0);
    // find last non-whitespace
    const char* end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) --end;
    size_t len = (size_t)(end - s + 1);
    return aiml_str_ndup(s, len);
}

// ── string.replace ───────────────────────────────────────────────────────────
extern "C" char* aiml_str_replace(const char* s, const char* old_str, const char* new_str) {
    if (!s) return aiml_str_alloc(0);
    if (!old_str || old_str[0] == '\0') return aiml_str_dup(s);
    if (!new_str) new_str = "";

    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);

    // Count occurrences to pre-allocate
    size_t count = 0;
    const char* p = s;
    while ((p = strstr(p, old_str)) != nullptr) { ++count; p += old_len; }

    if (count == 0) return aiml_str_dup(s);

    size_t src_len = strlen(s);
    size_t out_len = src_len + count * (new_len - old_len);
    char* out = aiml_str_alloc(out_len);
    if (!out) return aiml_str_dup(s);

    char* dst = out;
    p = s;
    while (*p) {
        const char* hit = strstr(p, old_str);
        if (!hit) {
            strcpy(dst, p);
            break;
        }
        size_t chunk = (size_t)(hit - p);
        memcpy(dst, p, chunk);
        dst += chunk;
        memcpy(dst, new_str, new_len);
        dst += new_len;
        p = hit + old_len;
    }
    // aiml_str_alloc already writes the trailing \0 at out[out_len].
    return out;
}

// ── string.split ─────────────────────────────────────────────────────────────
// Fills an AimlListRaw (same layout as GenericList) with refcounted parts.
extern "C" int aiml_str_split_fill(const char* s, const char* delim, void* list_ptr) {
    AimlListRaw* list = static_cast<AimlListRaw*>(list_ptr);
    if (!s || !delim || !list) return 0;

    size_t delim_len = strlen(delim);
    if (delim_len == 0) {
        // Split into individual characters
        size_t slen = strlen(s);
        for (size_t i = 0; i < slen; ++i) {
            if (list->size >= list->capacity) {
                int newCap = list->capacity * 2;
                void** newData = static_cast<void**>(std::realloc(list->data, (size_t)newCap * sizeof(void*)));
                if (!newData) return list->size;
                list->data = newData;
                list->capacity = newCap;
            }
            list->data[list->size++] = aiml_str_ndup(s + i, 1);
        }
        return list->size;
    }

    const char* p = s;
    int count = 0;
    while (true) {
        const char* hit = strstr(p, delim);
        size_t part_len = hit ? (size_t)(hit - p) : strlen(p);

        // Grow if needed
        if (list->size >= list->capacity) {
            int newCap = list->capacity * 2;
            void** newData = static_cast<void**>(std::realloc(list->data, (size_t)newCap * sizeof(void*)));
            if (!newData) return count;
            list->data = newData;
            list->capacity = newCap;
        }
        list->data[list->size++] = aiml_str_ndup(p, part_len);
        ++count;

        if (!hit) break;
        p = hit + delim_len;
    }
    return count;
}

// ── string.parse_i32 ────────────────────────────────────────────────────────
extern "C" int32_t aiml_str_parse_i32(const char* s) {
    if (!s || !*s) return 0;
    return (int32_t)strtol(s, nullptr, 10);
}

// ── string.parse_f32 ────────────────────────────────────────────────────────
extern "C" float aiml_str_parse_f32(const char* s) {
    if (!s || !*s) return 0.0f;
    return strtof(s, nullptr);
}
