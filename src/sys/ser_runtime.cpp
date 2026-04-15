#include "ser_runtime.h"
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
static_assert(sizeof(SerList) == 16, "SerList size mismatch — check list_impl.cpp");

// GenericMap: { i32 size, i32 capacity, i8* occupied, void* keys, void* values }
// x86-64: offsets 0, 4, 8, 16, 24 — sizeof = 32
struct SerMap {
    int32_t  size;
    int32_t  capacity;
    int8_t*  occupied;
    void*    keys;
    void*    values;
};
static_assert(sizeof(SerMap) == 32, "SerMap size mismatch — check map_impl.cpp");

// AimlBuffer: { i32 size, i32 cap, i32 read_idx, i32 write_idx, void* data }
// x86-64: offsets 0, 4, 8, 12, 16 (pointer 8-aligned at 16) — sizeof = 24
struct SerBuffer {
    int32_t  size;
    int32_t  capacity;
    int32_t  read_idx;
    int32_t  write_idx;
    void*    data;
};
static_assert(sizeof(SerBuffer) == 24, "SerBuffer size mismatch — check compress_runtime.h");

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
    if (pos + 4 > end) return 0;
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
    }
}

// ── Decode (recursive) ────────────────────────────────────────────────────────

static void dec(const uint8_t* blob, int& pos, int end,
                int idx, const AimlTypeNode* ns, int nn,
                uint8_t* out) {
    if (idx < 0 || idx >= nn) return;
    const AimlTypeNode& n = ns[idx];

    switch ((AimlTDKind)n.kind) {

    case AIML_TD_I8:
        if (pos + 1 <= end) { *out = blob[pos++]; }
        break;

    case AIML_TD_I16:
        if (pos + 2 <= end) { memcpy(out, blob + pos, 2); pos += 2; }
        break;

    case AIML_TD_I32:
        if (pos + 4 <= end) { memcpy(out, blob + pos, 4); pos += 4; }
        break;

    case AIML_TD_I64:
        if (pos + 8 <= end) { memcpy(out, blob + pos, 8); pos += 8; }
        break;

    case AIML_TD_STRING: {
        int32_t len = r_i32(blob, pos, end);
        char* str;
        if (len > 0 && pos + len <= end) {
            str = (char*)malloc((size_t)len + 1);
            memcpy(str, blob + pos, (size_t)len);
            str[len] = '\0';
            pos += len;
        } else {
            str = strdup("");
        }
        memcpy(out, &str, sizeof(char*));
        break;
    }

    case AIML_TD_STRUCT: {
        int nf = n.param1, fi = n.param2;
        SER_DBG("STRUCT idx=%d nf=%d off=%d sz=%d pos=%d depth=%d",
                idx, nf, n.offset, n.elem_size, pos, g_ser_depth);
        g_ser_depth++;
        for (int i = 0; i < nf; i++) {
            const AimlTypeNode& f = ns[fi + i];
            int pos_before = pos;
            dec(blob, pos, end, fi + i, ns, nn, out + f.offset);
            if (g_aiml_ser_debug && g_ser_depth <= 3)
                fprintf(stderr, "[ser-DBG]   field[%d] kind=%d off=%d esz=%d pos=%d->%d bytes=%d\n",
                        i, f.kind, f.offset, f.elem_size, pos_before, pos, pos - pos_before);
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
        for (int i = 0; i < count; i++)
            dec(blob, pos, end, n.param1, ns, nn, base + i * esz);
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
            if (pos >= end) break;
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
        b->size       = sz;
        b->capacity   = sz > 0 ? sz : 4;
        int esz = n.elem_size > 0 ? n.elem_size : 1;
        size_t alloc = (size_t)b->capacity * esz;
        b->data = malloc(alloc);
        memset(b->data, 0, alloc);
        if (sz > 0) {
            size_t bytes = (size_t)sz * esz;
            if (pos + (int)bytes <= end) {
                memcpy(b->data, blob + pos, bytes);
                pos += (int)bytes;
            }
        }
        break;
    }

    case AIML_TD_RAWBYTES:
        if (pos + n.param1 <= end) {
            memcpy(out, blob + pos, (size_t)n.param1);
            pos += n.param1;
        }
        break;
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
        if (str) { free(str); str = nullptr; memcpy(data, &str, sizeof(char*)); }
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
    char* blob = (char*)malloc((size_t)data_len + 4);
    // write 4-byte little-endian length prefix
    blob[0] = (char)( data_len        & 0xFF);
    blob[1] = (char)((data_len >>  8) & 0xFF);
    blob[2] = (char)((data_len >> 16) & 0xFF);
    blob[3] = (char)((data_len >> 24) & 0xFF);
    if (data_len > 0) memcpy(blob + 4, payload.data(), (size_t)data_len);
    return blob;
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
    dec(b + 4, pos, data_len, 0, nodes, n_nodes, (uint8_t*)out);
    return g_dec_error ? -1 : 0;
}

extern "C" void aiml_ser_free_decoded(void*               obj,
                                       const AimlTypeNode* nodes,
                                       int32_t             n_nodes) {
    if (!obj || !nodes || n_nodes <= 0) return;
    fre(0, nodes, n_nodes, (uint8_t*)obj);
}
