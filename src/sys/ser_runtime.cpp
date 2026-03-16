#include "ser_runtime.h"
#include <cstring>
#include <cstdlib>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <unistd.h>

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
        const SerMap* map = (const SerMap*)data;
        int32_t cap   = map->capacity;
        int32_t count = map->size;
        w_i32(buf, cap);
        w_i32(buf, count);
        if (count > 0 && map->occupied) {
            int vsz = n.elem_size;  // value element stride
            for (int i = 0; i < cap; i++) {
                uint8_t occ = (uint8_t)map->occupied[i];
                w_u8(buf, occ);
                if (occ) {
                    // key: always char* (map<string:X> only in v1)
                    const char* key = nullptr;
                    memcpy(&key, (uint8_t*)map->keys + i * sizeof(char*), sizeof(char*));
                    int32_t klen = key ? (int32_t)strlen(key) : 0;
                    w_i32(buf, klen);
                    if (klen > 0) w_raw(buf, key, (size_t)klen);
                    // value
                    uint8_t* vp = (uint8_t*)map->values + i * vsz;
                    enc(buf, n.param2, ns, nn, vp);
                }
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
        for (int i = 0; i < nf; i++) {
            const AimlTypeNode& f = ns[fi + i];
            dec(blob, pos, end, fi + i, ns, nn, out + f.offset);
        }
        break;
    }

    case AIML_TD_LIST: {
        int32_t count = r_i32(blob, pos, end);
        // out points directly to the inline SerList struct
        SerList* list = (SerList*)out;
        list->size     = count;
        list->capacity = count > 0 ? count : 4;
        int esz = n.elem_size;
        size_t alloc = (size_t)(list->capacity) * esz;
        list->data = malloc(alloc > 0 ? alloc : 1);
        memset(list->data, 0, alloc > 0 ? alloc : 1);
        uint8_t* base = (uint8_t*)list->data;
        for (int i = 0; i < count; i++)
            dec(blob, pos, end, n.param1, ns, nn, base + i * esz);
        break;
    }

    case AIML_TD_MAP: {
        int32_t cap   = r_i32(blob, pos, end);
        int32_t count = r_i32(blob, pos, end);
        if (cap <= 0) cap = 4;
        // out points directly to the inline SerMap struct
        SerMap* map   = (SerMap*)out;
        map->size     = count;
        map->capacity = cap;
        map->occupied = (int8_t*)calloc((size_t)cap, 1);
        map->keys     = calloc((size_t)cap, sizeof(char*));
        int vsz       = n.elem_size;
        map->values   = calloc((size_t)cap, (size_t)vsz > 0 ? (size_t)vsz : 1);
        // Only read slot data if count > 0 (encoder skips slot data when count == 0)
        int readCap = (count > 0) ? cap : 0;
        for (int i = 0; i < readCap; i++) {
            if (pos >= end) break;
            uint8_t occ = blob[pos++];
            map->occupied[i] = (int8_t)occ;
            if (occ) {
                // key (char*)
                int32_t klen = r_i32(blob, pos, end);
                char* key;
                if (klen > 0 && pos + klen <= end) {
                    key = (char*)malloc((size_t)klen + 1);
                    memcpy(key, blob + pos, (size_t)klen);
                    key[klen] = '\0';
                    pos += klen;
                } else {
                    key = strdup("");
                }
                memcpy((uint8_t*)map->keys + i * sizeof(char*), &key, sizeof(char*));
                // value
                uint8_t* vout = (uint8_t*)map->values + i * vsz;
                dec(blob, pos, end, n.param2, ns, nn, vout);
            }
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
        if (str) free(str);
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
        } else if (list->data) {
            free(list->data);
        }
        // do NOT free list itself — it is inline in the parent struct
        break;
    }

    case AIML_TD_MAP: {
        // data points directly to the inline SerMap struct
        SerMap* map = (SerMap*)data;
        if (map->occupied) {
            int vsz = n.elem_size;
            for (int i = 0; i < map->capacity; i++) {
                if (!map->occupied[i]) continue;
                // key
                char* key = nullptr;
                memcpy(&key, (uint8_t*)map->keys + i * sizeof(char*), sizeof(char*));
                if (key) free(key);
                // value
                fre(n.param2, ns, nn, (uint8_t*)map->values + i * vsz);
            }
            free(map->occupied);
        }
        if (map->keys)   free(map->keys);
        if (map->values) free(map->values);
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

extern "C" void aiml_ser_decode(const char*         blob,
                                 void*               out,
                                 const AimlTypeNode* nodes,
                                 int32_t             n_nodes) {
    if (!blob || !out || !nodes || n_nodes <= 0) return;
    // read 4-byte length prefix
    const uint8_t* b = (const uint8_t*)blob;
    int32_t data_len = (int32_t)((uint32_t)b[0]
                               | ((uint32_t)b[1] <<  8)
                               | ((uint32_t)b[2] << 16)
                               | ((uint32_t)b[3] << 24));
    if (data_len <= 0) return;
    int pos = 0;
    dec(b + 4, pos, data_len, 0, nodes, n_nodes, (uint8_t*)out);
}

extern "C" void aiml_ser_free_decoded(void*               obj,
                                       const AimlTypeNode* nodes,
                                       int32_t             n_nodes) {
    if (!obj || !nodes || n_nodes <= 0) return;
    fre(0, nodes, n_nodes, (uint8_t*)obj);
}
