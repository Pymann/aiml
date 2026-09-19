#include "ser_runtime.h"
#include "string_runtime.h"
#include "model_runtime.h"   // AIML v2: aiml_model_alloc / dec_hits_zero / free_shell (AIML_TD_MODEL)
#include <cstring>
#include <cstdlib>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <unistd.h>

// Thread-local decode-error flag — set when a structural error (absurd count etc.) is detected.
// Reset at the start of aiml_ser_decode; checked at the end to return an error code.
static thread_local bool g_dec_error = false;

/* ── Debug logging (AIML_DEBUG=1 env var) ─────────────────────────────────── */
static int g_aiml_ser_debug = (getenv("AIML_DEBUG") != nullptr) ? 1 : 0;
#define SER_DBG(fmt, ...) do { if (g_aiml_ser_debug) fprintf(stderr, "[ser-DBG] " fmt "\n", ##__VA_ARGS__); } while(0)
// Depth-limited: only log STRUCT field traces up to depth 3 to avoid noise.
static thread_local int g_ser_depth = 0;

// ── Schema-Format-Guard ──────────────────────────────────────────────────────
// Every non-empty encoded blob carries an 8-byte header AFTER the 4-byte length
// prefix: [MAGIC(4)][schema_fp(4)].  On decode we verify both; a mismatch means
// the blob was written by an incompatible struct layout (stale KV data that
// predates a model change) — we then REFUSE to decode (leaving the caller's
// zero-initialised destination untouched → null strings/empty lists, safe) and
// log a clear, defined diagnostic instead of walking garbage → SIGSEGV.
// The fingerprint hashes the flat AimlTypeNode array (kind/param1/param2/offset/
// elem_size + count), so it changes whenever a field is added/reordered/resized.
// Cost: 8 bytes/value + one compare per decode; fp is O(n_nodes), computed per
// call but n_nodes is tiny (struct field count).  Set AIML_SER_NOGUARD=1 to
// bypass the check (emergency read of legacy data).
static const uint32_t AIML_SER_MAGIC = 0xA1F5C0DEu;
static int g_ser_noguard = (getenv("AIML_SER_NOGUARD") != nullptr) ? 1 : 0;

// The guard is OPT-IN per operation via this thread-local flag — enabled ONLY by
// the persistent-KV layer (db_runtime), where stale on-disk data written by an
// incompatible struct layout is the real hazard.  It is left OFF for other
// serializer users (e.g. WS/mirror live messages), where two peers compiled with
// different import sets legitimately produce different node arrays and must still
// interoperate without a fingerprint check.
static thread_local bool g_ser_guard = false;
// Returns the PREVIOUS value so callers can save/restore (nesting-safe RAII).
extern "C" int aiml_ser_set_guard(int on) {
    int prev = g_ser_guard ? 1 : 0;
    g_ser_guard = (on != 0);
    return prev;
}

// STRUCTURAL hash: follow the child indices and hash the STRUCTURE reached
// (kind/offset/elem_size/field-count), NOT the raw param1/param2 index VALUES —
// those are positions in the flat node array and depend on the compilation
// context (import set, type count), so hashing them would make the same logical
// struct fingerprint differently across processes/builds → false mismatches.
static void aiml_ser_fp_walk(const AimlTypeNode* nodes, int32_t n,
                             int32_t idx, uint32_t& h, int depth) {
    if (idx < 0 || idx >= n || depth > 64) { h = (h ^ 0xDEADBEEFu) * 16777619u; return; }
    const AimlTypeNode& nd = nodes[idx];
    h = (h ^ (uint32_t)nd.kind)      * 16777619u;
    h = (h ^ (uint32_t)nd.offset)    * 16777619u;
    h = (h ^ (uint32_t)nd.elem_size) * 16777619u;
    switch (nd.kind) {
        case AIML_TD_STRUCT: {
            int nf = nd.param1, first = nd.param2;
            h = (h ^ (uint32_t)nf) * 16777619u;
            for (int i = 0; i < nf; i++) aiml_ser_fp_walk(nodes, n, first + i, h, depth + 1);
            break;
        }
        case AIML_TD_LIST:
        case AIML_TD_BUFFER:
        case AIML_TD_PTR_STRUCT:
        case AIML_TD_MODEL:
            aiml_ser_fp_walk(nodes, n, nd.param1, h, depth + 1);
            break;
        case AIML_TD_MAP:
            aiml_ser_fp_walk(nodes, n, nd.param1, h, depth + 1);
            aiml_ser_fp_walk(nodes, n, nd.param2, h, depth + 1);
            break;
        case AIML_TD_RAWBYTES:
            h = (h ^ (uint32_t)nd.param1) * 16777619u;   // byte count
            break;
        default: break;                                  // primitives: kind already mixed in
    }
}
static uint32_t aiml_ser_schema_fp(const AimlTypeNode* nodes, int32_t n_nodes) {
    uint32_t h = 2166136261u;               // FNV-1a offset basis
    aiml_ser_fp_walk(nodes, n_nodes, 0, h, 0);   // root node is always index 0
    return h;
}

// ── Internal struct mirrors ────────────────────────────────────────────────────
// These MUST match the LLVM-generated layouts exactly.
// Verified from list_impl.cpp, map_impl.cpp, compress_runtime.h, code_runtime.cpp.

// GenericList: { i32 size, i32 capacity, void* data }
// x86-64: offsets 0, 4, 8 — sizeof = 16
struct SerList {
    int32_t size;
    int32_t capacity;
    void*   data;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(SerList) == 16, "SerList size mismatch — check list_impl.cpp");
#else
static_assert(sizeof(SerList) == 12, "SerList size mismatch (32-bit) — check list_impl.cpp");
#endif

// GenericMap: { i32 size, i32 capacity, i8* occupied, void* keys, void* values }
// x86-64: offsets 0, 4, 8, 16, 24 — sizeof = 32
struct SerMap {
    int32_t  size;
    int32_t  capacity;
    int8_t*  occupied;
    void*    keys;
    void*    values;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(SerMap) == 32, "SerMap size mismatch — check map_impl.cpp");
#else
static_assert(sizeof(SerMap) == 20, "SerMap size mismatch (32-bit) — check map_impl.cpp");
#endif

// AimlBuffer: { i32 size, i32 cap, i32 read_idx, i32 write_idx, void* data }
// x86-64: offsets 0, 4, 8, 12, 16 (pointer 8-aligned at 16) — sizeof = 24
struct SerBuffer {
    int32_t  size;
    int32_t  capacity;
    int32_t  read_idx;
    int32_t  write_idx;
    void*    data;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(SerBuffer) == 24, "SerBuffer size mismatch — check compress_runtime.h");
#else
static_assert(sizeof(SerBuffer) == 20, "SerBuffer size mismatch (32-bit) — check compress_runtime.h");
#endif

// ── Write helpers (little-endian) ─────────────────────────────────────────────

static void w_u8(std::vector<uint8_t>& b, uint8_t v) {
    b.push_back(v);
}

static void w_i32(std::vector<uint8_t>& b, int32_t v) {
    uint32_t u;
    memcpy(&u, &v, 4);
    b.push_back((uint8_t)( u        & 0xFF));
    b.push_back((uint8_t)((u >>  8) & 0xFF));
    b.push_back((uint8_t)((u >> 16) & 0xFF));
    b.push_back((uint8_t)((u >> 24) & 0xFF));
}

static void w_i64(std::vector<uint8_t>& b, int64_t v) {
    uint64_t u;
    memcpy(&u, &v, 8);
    for (int i = 0; i < 8; i++) b.push_back((uint8_t)((u >> (8*i)) & 0xFF));
}

static void w_raw(std::vector<uint8_t>& b, const void* data, size_t n) {
    const uint8_t* p = (const uint8_t*)data;
    b.insert(b.end(), p, p + n);
}

// ── Read helpers (little-endian) ──────────────────────────────────────────────

static int32_t r_i32(const uint8_t* blob, int& pos, int end) {
    if (pos + 4 > end) { g_dec_error = true; return 0; }  // truncated length/count prefix (F1)
    uint32_t u = (uint32_t)blob[pos]
              | ((uint32_t)blob[pos+1] <<  8)
              | ((uint32_t)blob[pos+2] << 16)
              | ((uint32_t)blob[pos+3] << 24);
    pos += 4;
    int32_t v; memcpy(&v, &u, 4); return v;
}

// ── Key stride helper ─────────────────────────────────────────────────────────
// Returns the byte stride per entry in map->keys for a given key type node.
static int key_stride(int key_idx, const AimlTypeNode* ns, int nn) {
    if (key_idx < 0 || key_idx >= nn) return (int)sizeof(char*);
    switch ((AimlTDKind)ns[key_idx].kind) {
    case AIML_TD_I8:       return 1;
    case AIML_TD_I16:      return 2;
    case AIML_TD_I32:      return 4;
    case AIML_TD_I64:      return 8;
    case AIML_TD_RAWBYTES: return ns[key_idx].param1;  // enum/opaque types (e.g. i32 enum = 4)
    default:               return (int)sizeof(char*);
    }
}

// ── Forward declarations ──────────────────────────────────────────────────────

static void enc(std::vector<uint8_t>& buf,
                int idx, const AimlTypeNode* ns, int nn,
                const uint8_t* data);

static void dec(const uint8_t* blob, int& pos, int end,
                int idx, const AimlTypeNode* ns, int nn,
                uint8_t* out);

static void fre(int idx, const AimlTypeNode* ns, int nn, uint8_t* data);

// ── Encode (recursive) ────────────────────────────────────────────────────────

static void enc(std::vector<uint8_t>& buf,
                int idx, const AimlTypeNode* ns, int nn,
                const uint8_t* data) {
    if (idx < 0 || idx >= nn) return;
    const AimlTypeNode& n = ns[idx];

    switch ((AimlTDKind)n.kind) {

    case AIML_TD_I8:
        w_u8(buf, *data);
        break;

    case AIML_TD_I16:
        w_raw(buf, data, 2);
        break;

    case AIML_TD_I32:
        w_raw(buf, data, 4);
        break;

    case AIML_TD_I64:
        w_raw(buf, data, 8);
        break;

    case AIML_TD_STRING: {
        const char* str = nullptr;
        memcpy(&str, data, sizeof(char*));
        int32_t len = str ? (int32_t)strlen(str) : 0;
        w_i32(buf, len);
        if (len > 0) w_raw(buf, str, (size_t)len);
        break;
    }

    case AIML_TD_STRUCT: {
        // fields are laid out at [param2 .. param2+param1) in ns[]
        int nf = n.param1, fi = n.param2;
        for (int i = 0; i < nf; i++) {
            const AimlTypeNode& f = ns[fi + i];
            enc(buf, fi + i, ns, nn, data + f.offset);
        }
        break;
    }

    case AIML_TD_LIST: {
        // data points directly to the inline SerList struct
        const SerList* list = (const SerList*)data;
        int32_t count = (list->size > 0) ? list->size : 0;
        w_i32(buf, count);
        if (count > 0 && list->data) {
            int esz = n.elem_size;
            uint8_t* base = (uint8_t*)list->data;
            for (int i = 0; i < count; i++)
                enc(buf, n.param1, ns, nn, base + i * esz);
        }
        break;
    }

    case AIML_TD_MAP: {
        // data points directly to the inline SerMap struct
        // Dense format: write count, then for each entry 0..count-1: key then value.
        // map->keys is a dense array (no holes), indexed 0..size-1.
        const SerMap* map = (const SerMap*)data;
        int32_t count = (map->size > 0) ? map->size : 0;
        w_i32(buf, count);
        if (count > 0 && map->keys && map->values) {
            int ksz = key_stride(n.param1, ns, nn);
            int vsz = n.elem_size;
            for (int i = 0; i < count; i++) {
                enc(buf, n.param1, ns, nn, (const uint8_t*)map->keys  + i * ksz);
                enc(buf, n.param2, ns, nn, (const uint8_t*)map->values + i * vsz);
            }
        }
        break;
    }

    case AIML_TD_BUFFER: {
        // data points directly to the inline SerBuffer struct
        const SerBuffer* b = (const SerBuffer*)data;
        int32_t sz = (b->size > 0) ? b->size : 0;
        w_i32(buf, sz);
        if (sz > 0 && b->data) {
            int esz = n.elem_size > 0 ? n.elem_size : 1;
            w_raw(buf, b->data, (size_t)sz * esz);
        }
        break;
    }

    case AIML_TD_RAWBYTES:
        w_raw(buf, data, (size_t)n.param1);
        break;

    case AIML_TD_PTR_STRUCT:
    case AIML_TD_MODEL: {
        // `data` points at the pointer slot in the parent.  Follow it; if
        // null, write a 0 marker; else write 1 + the inner struct bytes.
        // (MODEL shares the wire format with PTR_STRUCT — only alloc/free differ on decode.)
        const void* obj = nullptr;
        memcpy(&obj, data, sizeof(void*));
        if (!obj) {
            w_u8(buf, 0);
        } else {
            w_u8(buf, 1);
            enc(buf, n.param1, ns, nn, (const uint8_t*)obj);
        }
        break;
    }
    }
}

// ── Decode (recursive) ────────────────────────────────────────────────────────

static void dec(const uint8_t* blob, int& pos, int end,
                int idx, const AimlTypeNode* ns, int nn,
                uint8_t* out) {
    if (idx < 0 || idx >= nn) return;
    const AimlTypeNode& n = ns[idx];

    switch ((AimlTDKind)n.kind) {

    // Truncated scalar = corrupt blob → flag it (F1); a valid blob always has all
    // field bytes present, so pos+N>end is never a legitimate decode.
    case AIML_TD_I8:
        if (end - pos >= 1) { *out = blob[pos++]; } else { g_dec_error = true; }
        break;

    case AIML_TD_I16:
        if (end - pos >= 2) { memcpy(out, blob + pos, 2); pos += 2; } else { g_dec_error = true; }
        break;

    case AIML_TD_I32:
        if (end - pos >= 4) { memcpy(out, blob + pos, 4); pos += 4; } else { g_dec_error = true; }
        break;

    case AIML_TD_I64:
        if (end - pos >= 8) { memcpy(out, blob + pos, 8); pos += 8; } else { g_dec_error = true; }
        break;

    case AIML_TD_STRING: {
        int32_t len = r_i32(blob, pos, end);
        char* str;
        // Overflow-safe bound (P0.1): NEVER compute pos+len — `len` is blob-controlled
        // and pos+len can signed-overflow, defeating the check → giant aiml_str_alloc /
        // OOB memcpy.  pos<=end holds by construction, so compare against remaining
        // bytes (end-pos, always >=0).
        if (len > 0 && len <= end - pos) {
            // Refcounted alloc — when aiml_ser_free_decoded later releases
            // this slot, it uses aiml_str_decref; if the user has aliased
            // the pointer to a local variable (incref'd), the string survives.
            str = aiml_str_alloc(len);
            memcpy(str, blob + pos, (size_t)len);
            pos += len;
        } else {
            // len==0 is a VALID empty string (no error).  len<0 or a length that runs
            // past the remaining bytes is corruption/truncation → flag it (F1).
            // (A truncated length prefix is already flagged inside r_i32.)
            if (len != 0) g_dec_error = true;
            str = aiml_str_alloc(0);
        }
        memcpy(out, &str, sizeof(char*));
        break;
    }

    case AIML_TD_STRUCT: {
        int nf = n.param1, fi = n.param2;
        if (g_aiml_ser_debug && g_ser_depth <= 1)
            fprintf(stderr, "[ser-DBG] STRUCT idx=%d nf=%d off=%d sz=%d pos=%d depth=%d\n",
                    idx, nf, n.offset, n.elem_size, pos, g_ser_depth);
        g_ser_depth++;
        for (int i = 0; i < nf; i++) {
            const AimlTypeNode& f = ns[fi + i];
            int pos_before = pos;
            dec(blob, pos, end, fi + i, ns, nn, out + f.offset);
            if (g_aiml_ser_debug && g_ser_depth <= 1)
                fprintf(stderr, "[ser-DBG] d%d field[%d] kind=%d off=%d esz=%d pos=%d->%d bytes=%d\n",
                        g_ser_depth, i, f.kind, f.offset, f.elem_size, pos_before, pos, pos - pos_before);
        }
        g_ser_depth--;
        break;
    }

    case AIML_TD_LIST: {
        int32_t count = r_i32(blob, pos, end);
        int esz = n.elem_size;
        if (count < 0 || count > 65536) {
            fprintf(stderr, "[ser-ERROR] absurd LIST count=%d idx=%d off=%d esz=%d pos=%d end=%d — aborting decode\n",
                    count, idx, n.offset, esz, pos - 4, end);
            g_dec_error = true;
            pos = end;  // skip remaining blob data; subsequent fields all read as 0
            count = 0;
        }
        // out points directly to the inline SerList struct
        SerList* list = (SerList*)out;
        list->size     = count;
        list->capacity = count > 0 ? count : 4;
        size_t alloc = (size_t)(list->capacity) * esz;
        list->data = malloc(alloc > 0 ? alloc : 1);
        memset(list->data, 0, alloc > 0 ? alloc : 1);
        uint8_t* base = (uint8_t*)list->data;
        for (int i = 0; i < count; i++) {
            // Abort on a prior decode error so a corrupt count/element can't drive
            // further allocations; already-decoded elements stay freeable (P0.2).
            if (g_dec_error) break;
            dec(blob, pos, end, n.param1, ns, nn, base + i * esz);
        }
        break;
    }

    case AIML_TD_MAP: {
        // Dense format: read count, then decode each key+value into slots 0..count-1.
        int32_t count = r_i32(blob, pos, end);
        if (count < 0 || count > 65536) {
            fprintf(stderr, "[ser-ERROR] absurd MAP count=%d idx=%d off=%d pos=%d end=%d — aborting decode\n",
                    count, idx, n.offset, pos - 4, end);
            g_dec_error = true;
            pos = end;
            count = 0;
        }
        // out points directly to the inline SerMap struct
        SerMap* map   = (SerMap*)out;
        int cap       = count > 0 ? count : 4;
        int ksz       = key_stride(n.param1, ns, nn);
        int vsz       = n.elem_size;
        map->size     = count;
        map->capacity = cap;
        map->occupied = (int8_t*)calloc((size_t)cap, 1);
        map->keys     = calloc((size_t)cap, (size_t)ksz > 0 ? (size_t)ksz : 1);
        map->values   = calloc((size_t)cap, (size_t)vsz > 0 ? (size_t)vsz : 1);
        for (int i = 0; i < count; i++) {
            // Fewer bytes than announced entries = truncation → flag it (was a silent
            // break).  Also abort if a sub-decode already failed (P0.2).
            if (pos >= end || g_dec_error) { g_dec_error = true; break; }
            map->occupied[i] = 1;
            dec(blob, pos, end, n.param1, ns, nn, (uint8_t*)map->keys  + i * ksz);
            dec(blob, pos, end, n.param2, ns, nn, (uint8_t*)map->values + i * vsz);
        }
        break;
    }

    case AIML_TD_BUFFER: {
        int32_t sz = r_i32(blob, pos, end);
        // out points directly to the inline SerBuffer struct
        SerBuffer* b = (SerBuffer*)out;
        memset(b, 0, sizeof(SerBuffer));
        int esz = n.elem_size > 0 ? n.elem_size : 1;
        // Validate BEFORE allocating (P0.2): non-negative size, and the payload must
        // fit in the remaining bytes.  Overflow-safe: compare via division instead of
        // computing sz*esz (which can overflow) or pos+bytes (signed overflow).
        if (sz < 0 || (sz > 0 && (size_t)sz > (size_t)(end - pos) / (size_t)esz)) {
            g_dec_error = true;
            b->size = 0; b->capacity = 4;
            b->data = calloc((size_t)b->capacity, (size_t)esz);
            break;
        }
        b->size     = sz;
        b->capacity = sz > 0 ? sz : 4;
        size_t alloc = (size_t)b->capacity * (size_t)esz;
        b->data = calloc(1, alloc > 0 ? alloc : 1);
        if (sz > 0) {
            size_t bytes = (size_t)sz * (size_t)esz;   // no overflow: bounded above
            memcpy(b->data, blob + pos, bytes);
            pos += (int)bytes;
        }
        break;
    }

    case AIML_TD_RAWBYTES:
        // Overflow-safe remaining-length check; truncated fixed-bytes field = error (P0.2).
        if (n.param1 >= 0 && n.param1 <= end - pos) {
            memcpy(out, blob + pos, (size_t)n.param1);
            pos += n.param1;
        } else {
            g_dec_error = true;
        }
        break;

    case AIML_TD_PTR_STRUCT:
    case AIML_TD_MODEL: {
        // Read marker byte.  0 → store null pointer.  1 → allocate the inner struct, decode the
        // subtree into it, store the pointer in the slot. PTR_STRUCT calloc's a plain heap struct;
        // AIML_TD_MODEL allocates a refcounted MODL shell (header + refcount 1) so the decoded
        // reference is lifetime-managed like every other v2 model.
        uint8_t marker = 0;
        if (pos < end) { marker = blob[pos++]; }
        else { g_dec_error = true; }   // missing marker byte = truncation, not valid null (P0.2)
        void* obj = nullptr;
        if (marker != 0) {
            size_t inner_sz = (size_t)(n.elem_size > 0 ? n.elem_size : (int32_t)sizeof(void*));
            obj = (n.kind == AIML_TD_MODEL) ? aiml_model_alloc(inner_sz) : calloc(1, inner_sz);
            if (obj) {
                dec(blob, pos, end, n.param1, ns, nn, (uint8_t*)obj);
            }
        }
        memcpy(out, &obj, sizeof(void*));
        break;
    }
    }
}

// ── Free decoded heap fields (recursive) ─────────────────────────────────────

static void fre(int idx, const AimlTypeNode* ns, int nn, uint8_t* data) {
    if (idx < 0 || idx >= nn) return;
    const AimlTypeNode& n = ns[idx];

    switch ((AimlTDKind)n.kind) {

    case AIML_TD_STRING: {
        char* str = nullptr;
        memcpy(&str, data, sizeof(char*));
        if (str) {
            // Refcount release — only actually frees when no aliased user var
            // is still holding a reference.  Was `free(str)` until the UAF
            // repro (tests/test_mirror_close_string_uaf.aiml).
            aiml_str_decref(str);
            str = nullptr;
            memcpy(data, &str, sizeof(char*));
        }
        break;
    }

    case AIML_TD_STRUCT: {
        int nf = n.param1, fi = n.param2;
        for (int i = 0; i < nf; i++) {
            const AimlTypeNode& f = ns[fi + i];
            fre(fi + i, ns, nn, data + f.offset);
        }
        break;
    }

    case AIML_TD_LIST: {
        // data points directly to the inline SerList struct
        SerList* list = (SerList*)data;
        if (list->data && list->size > 0) {
            int esz = n.elem_size;
            uint8_t* base = (uint8_t*)list->data;
            for (int i = 0; i < list->size; i++)
                fre(n.param1, ns, nn, base + i * esz);
            free(list->data);
            list->data = nullptr;
        } else if (list->data) {
            free(list->data);
            list->data = nullptr;
        }
        // do NOT free list itself — it is inline in the parent struct
        break;
    }

    case AIML_TD_MAP: {
        // data points directly to the inline SerMap struct
        // Dense: free heap allocations for entries 0..size-1, then free arrays.
        SerMap* map = (SerMap*)data;
        if (map->size > 0 && map->keys && map->values) {
            int ksz = key_stride(n.param1, ns, nn);
            int vsz = n.elem_size;
            for (int i = 0; i < map->size; i++) {
                fre(n.param1, ns, nn, (uint8_t*)map->keys  + i * ksz);
                fre(n.param2, ns, nn, (uint8_t*)map->values + i * vsz);
            }
        }
        if (map->occupied) { free(map->occupied); map->occupied = nullptr; }
        if (map->keys)     { free(map->keys);     map->keys     = nullptr; }
        if (map->values)   { free(map->values);   map->values   = nullptr; }
        map->size = 0; map->capacity = 0;
        // do NOT free map itself — it is inline in the parent struct
        break;
    }

    case AIML_TD_BUFFER: {
        // data points directly to the inline SerBuffer struct
        SerBuffer* b = (SerBuffer*)data;
        if (b->data && b->size > 0) {
            int esz = n.elem_size > 0 ? n.elem_size : 1;
            uint8_t* base = (uint8_t*)b->data;
            for (int i = 0; i < b->size; i++)
                fre(n.param1, ns, nn, base + i * esz);
            free(b->data);
        } else if (b->data) {
            free(b->data);
        }
        // do NOT free b itself — it is inline in the parent struct
        break;
    }

    case AIML_TD_PTR_STRUCT: {
        // Free the inner struct's heap allocations + the struct allocation
        // itself.  data is the pointer slot.
        void* obj = nullptr;
        memcpy(&obj, data, sizeof(void*));
        if (obj) {
            fre(n.param1, ns, nn, (uint8_t*)obj);
            free(obj);
            obj = nullptr;
            memcpy(data, &obj, sizeof(void*));
        }
        break;
    }

    case AIML_TD_MODEL: {
        // AIML v2: a refcounted MODEL reference. Decref; only when it hits 0 do we free the inner
        // heap (fre at the struct subtree n.param1 — NOT node 0, so aiml_model_decref cannot be
        // used here) and the shell. A shared reference (refcount>1) is just decremented. The slot is
        // nulled so a second free is a null no-op.
        void* obj = nullptr;
        memcpy(&obj, data, sizeof(void*));
        if (obj && aiml_model_dec_hits_zero(obj)) {
            fre(n.param1, ns, nn, (uint8_t*)obj);
            aiml_model_free_shell(obj);
        }
        obj = nullptr;
        memcpy(data, &obj, sizeof(void*));
        break;
    }

    // I8/I16/I32/I64/RAWBYTES — no heap allocations
    default:
        break;
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

extern "C" int32_t aiml_ser_encoded_size(const void*         obj,
                                          const AimlTypeNode* nodes,
                                          int32_t             n_nodes) {
    if (!obj || !nodes || n_nodes <= 0) return 0;
    std::vector<uint8_t> tmp;
    tmp.reserve(256);
    enc(tmp, 0, nodes, n_nodes, (const uint8_t*)obj);
    return (int32_t)tmp.size();
}

// ── Debug dump ───────────────────────────────────────────────────────────────
// A 4th walker (alongside enc/dec/fre): print a live struct's field values.
// String fields are magic-checked before strlen so a corrupt/garbage pointer is
// reported as **INVALID** instead of crashing the dump.  For hunting WASM decode
// corruption.  Bound to `sys.dump(model)`.
#define AIML_DUMP_STR_MAGIC 0x5354524Eu   // "STRN" — matches string_runtime.cpp
static void dump_walk(const uint8_t* data, int idx, const AimlTypeNode* ns, int nn, int depth) {
    if (!data || idx < 0 || idx >= nn || depth > 10) return;
    const AimlTypeNode& n = ns[idx];
    char ind[24]; int ni = depth * 2; if (ni > 22) ni = 22; memset(ind, ' ', ni); ind[ni] = 0;
    switch (n.kind) {
    case AIML_TD_I8:  { int8_t  v; memcpy(&v, data, 1); fprintf(stderr, "%si8=%d\n",  ind, (int)v); break; }
    case AIML_TD_I16: { int16_t v; memcpy(&v, data, 2); fprintf(stderr, "%si16=%d\n", ind, (int)v); break; }
    case AIML_TD_I32: { int32_t v; memcpy(&v, data, 4); fprintf(stderr, "%si32=%d\n", ind, v); break; }
    case AIML_TD_I64: { int64_t v; memcpy(&v, data, 8); fprintf(stderr, "%si64=%lld\n", ind, (long long)v); break; }
    case AIML_TD_STRING: {
        char* str; memcpy(&str, data, sizeof(char*));
        if (!str) { fprintf(stderr, "%sstr=NULL\n", ind); break; }
        uint32_t magic = 0; memcpy(&magic, str - 8, 4);  // AimlStrHeader {u32 magic; u32 refcount}
        if (magic != AIML_DUMP_STR_MAGIC)
            fprintf(stderr, "%sstr=**INVALID** ptr=%p magic=0x%08x (not an aiml string → corrupt)\n",
                    ind, (const void*)str, magic);
        else {
            size_t l = strlen(str);
            fprintf(stderr, "%sstr len=%zu \"%.48s\"%s\n", ind, l, str, l > 48 ? "..." : "");
        }
        break;
    }
    case AIML_TD_STRUCT: {
        int nf = n.param1, fi = n.param2;
        fprintf(stderr, "%sstruct nf=%d off=%d sz=%d {\n", ind, nf, n.offset, n.elem_size);
        for (int i = 0; i < nf; i++) {
            const AimlTypeNode& f = ns[fi + i];
            fprintf(stderr, "%s  [%d] off=%d kind=%d: ", ind, i, f.offset, f.kind);
            dump_walk(data + f.offset, fi + i, ns, nn, depth + 1);
        }
        fprintf(stderr, "%s}\n", ind);
        break;
    }
    case AIML_TD_LIST: {
        const SerList* list = (const SerList*)data;
        int esz = n.elem_size;
        fprintf(stderr, "%slist count=%d esz=%d [\n", ind, list->size, esz);
        int lim = (list->size < 16) ? list->size : 16;
        for (int i = 0; i < lim; i++) {
            fprintf(stderr, "%s  <%d> ", ind, i);
            dump_walk((const uint8_t*)list->data + (size_t)i * esz, n.param1, ns, nn, depth + 1);
        }
        if (list->size > lim) fprintf(stderr, "%s  ...(%d more)\n", ind, list->size - lim);
        fprintf(stderr, "%s]\n", ind);
        break;
    }
    case AIML_TD_MAP:    { const SerMap*    m = (const SerMap*)data;    fprintf(stderr, "%smap count=%d\n", ind, m->size); break; }
    case AIML_TD_BUFFER: { const SerBuffer* b = (const SerBuffer*)data; fprintf(stderr, "%sbuffer size=%d\n", ind, b->size); break; }
    case AIML_TD_RAWBYTES: fprintf(stderr, "%sraw %d bytes\n", ind, n.param1); break;
    case AIML_TD_PTR_STRUCT:
    case AIML_TD_MODEL: {
        const void* obj = nullptr; memcpy(&obj, data, sizeof(void*));
        if (!obj) { fprintf(stderr, "%sptr=NULL\n", ind); break; }
        fprintf(stderr, "%s%s-> ", ind, n.kind == AIML_TD_MODEL ? "model" : "ptr");
        dump_walk((const uint8_t*)obj, n.param1, ns, nn, depth + 1);
        break;
    }
    default: fprintf(stderr, "%skind=%d (unhandled)\n", ind, n.kind); break;
    }
}

extern "C" void aiml_ser_dump(const void* obj, const AimlTypeNode* nodes, int32_t n_nodes) {
    if (!obj || !nodes || n_nodes <= 0) { fprintf(stderr, "[dump] (null obj/nodes)\n"); return; }
    fprintf(stderr, "[dump] ===== BEGIN (n_nodes=%d) =====\n", n_nodes);
    dump_walk((const uint8_t*)obj, 0, nodes, n_nodes, 0);
    fprintf(stderr, "[dump] ===== END =====\n");
}

extern "C" char* aiml_ser_encode(const void*         obj,
                                  const AimlTypeNode* nodes,
                                  int32_t             n_nodes) {
    if (!obj || !nodes || n_nodes <= 0) {
        char* b = (char*)calloc(4, 1);  // [i32 0] = empty blob
        return b;
    }
    std::vector<uint8_t> payload;
    payload.reserve(256);
    enc(payload, 0, nodes, n_nodes, (const uint8_t*)obj);
    int32_t data_len = (int32_t)payload.size();
    if (g_ser_guard && !g_ser_noguard) {
        // Guarded (KV) blob: prepend [MAGIC(4)][schema_fp(4)] to the payload.  The
        // outer [len(4)][payload] envelope is unchanged (len includes the 8-byte
        // header) → storage/WAL layer unaffected; only guarded decode inspects it.
        int32_t total = data_len + 8;
        uint32_t fp = aiml_ser_schema_fp(nodes, n_nodes);
        char* blob = (char*)malloc((size_t)total + 4);
        blob[0] = (char)( total        & 0xFF);
        blob[1] = (char)((total >>  8) & 0xFF);
        blob[2] = (char)((total >> 16) & 0xFF);
        blob[3] = (char)((total >> 24) & 0xFF);
        memcpy(blob + 4, &AIML_SER_MAGIC, 4);   // [MAGIC]
        memcpy(blob + 8, &fp,             4);   // [schema_fp]
        if (data_len > 0) memcpy(blob + 12, payload.data(), (size_t)data_len);
        return blob;
    }
    // Un-guarded (WS/other): original [len(4)][payload].
    char* blob = (char*)malloc((size_t)data_len + 4);
    blob[0] = (char)( data_len        & 0xFF);
    blob[1] = (char)((data_len >>  8) & 0xFF);
    blob[2] = (char)((data_len >> 16) & 0xFF);
    blob[3] = (char)((data_len >> 24) & 0xFF);
    if (data_len > 0) memcpy(blob + 4, payload.data(), (size_t)data_len);
    return blob;
}

extern "C" void aiml_ser_deep_copy(void*               dst,
                                    const void*         src,
                                    const AimlTypeNode* nodes,
                                    int32_t             n_nodes) {
    if (!dst || !src || !nodes || n_nodes <= 0) return;
    // Encode src → blob → decode into dst. The decode allocates fresh heap
    // for every string/list/map/buffer field, so dst becomes fully
    // independent of src's heap allocations.
    //
    // Precondition: dst must be zero-initialised (all heap pointer fields
    // NULL) — see header comment.
    char* blob = aiml_ser_encode(src, nodes, n_nodes);
    if (!blob) return;
    aiml_ser_decode(blob, dst, nodes, n_nodes);
    free(blob);
}

// ── spec/61 §5.1 memoized deep-clone ──────────────────────────────────────────
// A per-clone src-pointer → dst-pointer memo for AIML_TD_MODEL references, so a
// shared sub-object reached by several owning edges is cloned once (the DAG is
// preserved) and a cycle terminates (the back-edge finds the in-progress clone).
#include <unordered_map>
static thread_local std::unordered_map<const void*, void*>* g_clone_memo = nullptr;

static void clw(int idx, const AimlTypeNode* ns, int nn,
                const uint8_t* src, uint8_t* dst) {
    if (idx < 0 || idx >= nn) return;
    const AimlTypeNode& n = ns[idx];

    switch ((AimlTDKind)n.kind) {

    case AIML_TD_I8:  *dst = *src; break;
    case AIML_TD_I16: memcpy(dst, src, 2); break;
    case AIML_TD_I32: memcpy(dst, src, 4); break;
    case AIML_TD_I64: memcpy(dst, src, 8); break;

    case AIML_TD_STRING: {
        // Immutable RC handle (§4): share by incref, store the same pointer.
        char* s = nullptr;
        memcpy(&s, src, sizeof(char*));
        if (s) aiml_str_incref(s);
        memcpy(dst, &s, sizeof(char*));
        break;
    }

    case AIML_TD_STRUCT: {
        int nf = n.param1, fi = n.param2;
        for (int i = 0; i < nf; i++) {
            const AimlTypeNode& f = ns[fi + i];
            clw(fi + i, ns, nn, src + f.offset, dst + f.offset);
        }
        break;
    }

    case AIML_TD_LIST: {
        const SerList* s = (const SerList*)src;
        SerList* d = (SerList*)dst;
        int esz = n.elem_size;
        int32_t count = (s->size > 0) ? s->size : 0;
        d->size     = count;
        d->capacity = count > 0 ? count : 4;
        size_t alloc = (size_t)d->capacity * (size_t)esz;
        d->data = malloc(alloc > 0 ? alloc : 1);
        memset(d->data, 0, alloc > 0 ? alloc : 1);
        if (count > 0 && s->data) {
            const uint8_t* sb = (const uint8_t*)s->data;
            uint8_t* db = (uint8_t*)d->data;
            for (int i = 0; i < count; i++)
                clw(n.param1, ns, nn, sb + i * esz, db + i * esz);
        }
        break;
    }

    case AIML_TD_MAP: {
        const SerMap* s = (const SerMap*)src;
        SerMap* d = (SerMap*)dst;
        int32_t count = (s->size > 0) ? s->size : 0;
        int cap = count > 0 ? count : 4;
        int ksz = key_stride(n.param1, ns, nn);
        int vsz = n.elem_size;
        d->size     = count;
        d->capacity = cap;
        d->occupied = (int8_t*)calloc((size_t)cap, 1);
        d->keys     = calloc((size_t)cap, (size_t)ksz > 0 ? (size_t)ksz : 1);
        d->values   = calloc((size_t)cap, (size_t)vsz > 0 ? (size_t)vsz : 1);
        if (count > 0 && s->keys && s->values) {
            for (int i = 0; i < count; i++) {
                d->occupied[i] = 1;
                clw(n.param1, ns, nn, (const uint8_t*)s->keys   + i * ksz, (uint8_t*)d->keys   + i * ksz);
                clw(n.param2, ns, nn, (const uint8_t*)s->values + i * vsz, (uint8_t*)d->values + i * vsz);
            }
        }
        break;
    }

    case AIML_TD_BUFFER: {
        const SerBuffer* s = (const SerBuffer*)src;
        SerBuffer* d = (SerBuffer*)dst;
        int esz = n.elem_size > 0 ? n.elem_size : 1;
        int32_t sz = (s->size > 0) ? s->size : 0;
        d->size     = sz;
        d->capacity = sz > 0 ? sz : 4;
        d->read_idx = s->read_idx;
        d->write_idx = s->write_idx;
        size_t alloc = (size_t)d->capacity * (size_t)esz;
        d->data = calloc(1, alloc > 0 ? alloc : 1);
        if (sz > 0 && s->data) memcpy(d->data, s->data, (size_t)sz * (size_t)esz);
        break;
    }

    case AIML_TD_RAWBYTES:
        memcpy(dst, src, (size_t)n.param1);
        break;

    case AIML_TD_PTR_STRUCT: {
        // Unique owner (ptr<T>): clone independently, no memo.
        const void* obj = nullptr;
        memcpy(&obj, src, sizeof(void*));
        void* nobj = nullptr;
        if (obj) {
            size_t inner_sz = (size_t)(n.elem_size > 0 ? n.elem_size : (int32_t)sizeof(void*));
            nobj = calloc(1, inner_sz);
            if (nobj) clw(n.param1, ns, nn, (const uint8_t*)obj, (uint8_t*)nobj);
        }
        memcpy(dst, &nobj, sizeof(void*));
        break;
    }

    case AIML_TD_MODEL: {
        // shared<T>/refcount reference: MEMOIZE by source pointer so the DAG is
        // preserved and cycles terminate.
        const void* obj = nullptr;
        memcpy(&obj, src, sizeof(void*));
        void* nobj = nullptr;
        if (obj) {
            auto it = g_clone_memo->find(obj);
            if (it != g_clone_memo->end()) {
                // Already cloned via another owning edge — reuse and incref.
                nobj = it->second;
                aiml_model_incref(nobj);
            } else {
                size_t inner_sz = (size_t)(n.elem_size > 0 ? n.elem_size : (int32_t)sizeof(void*));
                nobj = aiml_model_alloc(inner_sz);   // refcount 1
                if (nobj) {
                    (*g_clone_memo)[obj] = nobj;     // record BEFORE recursing (cycle-safe)
                    clw(n.param1, ns, nn, (const uint8_t*)obj, (uint8_t*)nobj);
                }
            }
        }
        memcpy(dst, &nobj, sizeof(void*));
        break;
    }
    }
}

extern "C" void aiml_clone_deep(void*               dst,
                                const void*         src,
                                const AimlTypeNode* nodes,
                                int32_t             n_nodes) {
    if (!dst || !src || !nodes || n_nodes <= 0) return;
    std::unordered_map<const void*, void*> memo;
    g_clone_memo = &memo;
    clw(0, nodes, n_nodes, (const uint8_t*)src, (uint8_t*)dst);
    g_clone_memo = nullptr;
}

extern "C" int aiml_ser_decode(const char*         blob,
                                void*               out,
                                const AimlTypeNode* nodes,
                                int32_t             n_nodes) {
    if (!blob || !out || !nodes || n_nodes <= 0) return -1;
    g_dec_error = false;
    // read 4-byte length prefix
    const uint8_t* b = (const uint8_t*)blob;
    int32_t data_len = (int32_t)((uint32_t)b[0]
                               | ((uint32_t)b[1] <<  8)
                               | ((uint32_t)b[2] << 16)
                               | ((uint32_t)b[3] << 24));
    if (data_len <= 0) return -1;
    int pos = 0;
    // Format guard: a GUARDED blob starts (after the len prefix) with
    // [MAGIC(4)][schema_fp(4)].  Verify the fingerprint so stale data written by
    // an incompatible struct layout is refused with a clear diagnostic instead of
    // being walked as garbage (→ SIGSEGV).  BACKWARD-COMPATIBLE: blobs written
    // before this feature don't start with MAGIC → decoded the legacy way, so
    // existing KV data keeps working with no migration.  New writes are guarded.
    if (g_ser_guard && !g_ser_noguard && data_len >= 8) {
        uint32_t magic = (uint32_t)b[4] | ((uint32_t)b[5] << 8)
                       | ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 24);
        if (magic == AIML_SER_MAGIC) {
            uint32_t stored_fp = (uint32_t)b[8]  | ((uint32_t)b[9]  << 8)
                               | ((uint32_t)b[10] << 16) | ((uint32_t)b[11] << 24);
            uint32_t exp_fp = aiml_ser_schema_fp(nodes, n_nodes);
            if (stored_fp != exp_fp) {
                fprintf(stderr, "[ser-FATAL] SerializationFormatException: incompatible KV "
                        "blob (schema fp stored=0x%08x expected=0x%08x, n_nodes=%d) — data "
                        "was written by a different struct layout (stale after a model "
                        "change); wipe + reseed this KV.\n", stored_fp, exp_fp, n_nodes);
                g_dec_error = true;
                return -2;   // leave `out` zero-inited → null strings/empty lists (safe, no crash)
            }
            dec(b + 12, pos, data_len - 8, 0, nodes, n_nodes, (uint8_t*)out);
            return g_dec_error ? -1 : 0;
        }
    }
    // Legacy / un-guarded blob (pre-feature): decode the original way.
    dec(b + 4, pos, data_len, 0, nodes, n_nodes, (uint8_t*)out);
    return g_dec_error ? -1 : 0;
}

extern "C" void aiml_ser_free_decoded(void*               obj,
                                       const AimlTypeNode* nodes,
                                       int32_t             n_nodes) {
    if (!obj || !nodes || n_nodes <= 0) return;
    fre(0, nodes, n_nodes, (uint8_t*)obj);
}
