// bytes_runtime.cpp — binary helpers on buffer<u8> (spec/68).
//
// Every function that returns a buffer hands out a malloc'd 24-byte header
// { i32 size, i32 cap, i32 read, i32 write, u8* data } (the AimlBufRaw /
// AimlCompressBuffer layout).  The compiler moves it into a buffer value
// (memV3: a fresh buffer handle) and frees the header — the same ownership
// move as io.read_bytes / base64.decode.  Inputs arrive as (data, len): the
// compiler extracts both from the buffer value, so nothing here relies on NULs.
//
// The functions are mechanisms (git and OCI tooling need them, so do others):
//   slice / concat / u8 / find / be32 / from_be32 / hex / text — byte access
//   read_file                                                  — whole file
//   zlib / zlib_at + zlib_consumed                             — zlib streams
//   crc32 / compare                                            — pack index work
//   delta_apply                                                — git pack delta
//   sha1 is a hash.<algo> (hash_runtime.cpp).
//
// Invalid ranges never read out of bounds: they yield an empty buffer / -1.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <zlib.h>

extern "C" char* aiml_str_alloc(size_t len);

namespace {

struct BufHdr { int32_t size; int32_t capacity; int32_t read_idx; int32_t write_idx; uint8_t* data; };

BufHdr* make_hdr(const uint8_t* src, int64_t len) {
    BufHdr* h = static_cast<BufHdr*>(calloc(1, sizeof(BufHdr)));
    if (!h) return nullptr;
    if (len < 0 || len > INT32_MAX) len = 0;
    if (len > 0) {
        h->data = static_cast<uint8_t*>(malloc(static_cast<size_t>(len)));
        if (!h->data) { h->size = 0; return h; }
        if (src) memcpy(h->data, src, static_cast<size_t>(len));
    }
    h->size = h->capacity = h->write_idx = static_cast<int32_t>(len);
    return h;
}

// Takes ownership of `data` (malloc'd).
BufHdr* adopt_hdr(uint8_t* data, int64_t len) {
    BufHdr* h = static_cast<BufHdr*>(calloc(1, sizeof(BufHdr)));
    if (!h) { free(data); return nullptr; }
    h->data = data;
    h->size = h->capacity = h->write_idx = static_cast<int32_t>(len);
    return h;
}

bool in_range(int32_t len, int64_t off, int64_t n) {
    return off >= 0 && n >= 0 && off <= len && n <= static_cast<int64_t>(len) - off;
}

char* managed_str(const char* s, size_t n) {
    char* out = aiml_str_alloc(n);
    if (out && n) memcpy(out, s, n);
    if (out) out[n] = '\0';
    return out;
}

thread_local int64_t g_zlib_consumed = -1;

}  // namespace

extern "C" {

// bytes.slice(buf, off, len) — copy of [off, off+len); len < 0 = to the end.
void* aiml_bytes_slice(const uint8_t* data, int32_t size, int64_t off, int64_t n) {
    if (n < 0) n = static_cast<int64_t>(size) - off;
    if (!data || !in_range(size, off, n)) return make_hdr(nullptr, 0);
    return make_hdr(data + off, n);
}

// bytes.concat(a, b)
void* aiml_bytes_concat(const uint8_t* a, int32_t alen, const uint8_t* b, int32_t blen) {
    if (alen < 0) alen = 0;
    if (blen < 0) blen = 0;
    int64_t total = static_cast<int64_t>(alen) + blen;
    if (total > INT32_MAX) return make_hdr(nullptr, 0);
    BufHdr* h = make_hdr(nullptr, total);
    if (!h || !h->data) return h;
    if (alen && a) memcpy(h->data, a, static_cast<size_t>(alen));
    if (blen && b) memcpy(h->data + alen, b, static_cast<size_t>(blen));
    return h;
}

// bytes.u8(buf, idx) → 0..255, or -1 outside.
int32_t aiml_bytes_u8(const uint8_t* data, int32_t size, int64_t idx) {
    if (!data || idx < 0 || idx >= size) return -1;
    return data[idx];
}

// bytes.find(buf, byte, from) → first index ≥ from holding `byte`, or -1.
int64_t aiml_bytes_find(const uint8_t* data, int32_t size, int32_t byte, int64_t from) {
    if (!data || from < 0 || from >= size) return -1;
    const void* hit = memchr(data + from, byte & 0xff, static_cast<size_t>(size - from));
    return hit ? static_cast<const uint8_t*>(hit) - data : -1;
}

// bytes.be32(buf, off) → unsigned 32-bit big-endian value, or -1 outside.
int64_t aiml_bytes_be32(const uint8_t* data, int32_t size, int64_t off) {
    if (!data || !in_range(size, off, 4)) return -1;
    const uint8_t* p = data + off;
    return (static_cast<int64_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

// bytes.from_be32(value) → 4 bytes, big-endian.
void* aiml_bytes_from_be32(int64_t v) {
    uint8_t b[4] = { static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16),
                     static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v) };
    return make_hdr(b, 4);
}

// bytes.from_u8(value) → 1 byte.
void* aiml_bytes_from_u8(int32_t v) {
    uint8_t b = static_cast<uint8_t>(v);
    return make_hdr(&b, 1);
}

// bytes.to_hex(buf, off, len) → lower-case hex; "" for an invalid range.
char* aiml_bytes_to_hex(const uint8_t* data, int32_t size, int64_t off, int64_t n) {
    if (n < 0) n = static_cast<int64_t>(size) - off;
    if (!data || !in_range(size, off, n)) return managed_str("", 0);
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.resize(static_cast<size_t>(n) * 2);
    for (int64_t i = 0; i < n; ++i) {
        out[2 * i] = digits[data[off + i] >> 4];
        out[2 * i + 1] = digits[data[off + i] & 15];
    }
    return managed_str(out.data(), out.size());
}

// bytes.from_hex(text) → bytes; empty for odd length or a non-hex digit.
void* aiml_bytes_from_hex(const char* s) {
    if (!s) return make_hdr(nullptr, 0);
    size_t n = strlen(s);
    if (n % 2) return make_hdr(nullptr, 0);
    std::string out(n / 2, '\0');
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < n / 2; ++i) {
        int hi = val(s[2 * i]), lo = val(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return make_hdr(nullptr, 0);
        out[i] = static_cast<char>((hi << 4) | lo);
    }
    return make_hdr(reinterpret_cast<const uint8_t*>(out.data()), static_cast<int64_t>(out.size()));
}

// bytes.from_string(text) → its bytes (no terminator).
void* aiml_bytes_from_string(const char* s) {
    return make_hdr(reinterpret_cast<const uint8_t*>(s ? s : ""), s ? static_cast<int64_t>(strlen(s)) : 0);
}

// bytes.text(buf, off, len) → string of that range (stops at a NUL, like every aiml string).
char* aiml_bytes_text(const uint8_t* data, int32_t size, int64_t off, int64_t n) {
    if (n < 0) n = static_cast<int64_t>(size) - off;
    if (!data || !in_range(size, off, n)) return managed_str("", 0);
    const void* nul = memchr(data + off, 0, static_cast<size_t>(n));
    if (nul) n = static_cast<const uint8_t*>(nul) - (data + off);
    return managed_str(reinterpret_cast<const char*>(data + off), static_cast<size_t>(n));
}

// bytes.read_file(path) → whole file; empty when missing/unreadable (or > 2 GiB).
void* aiml_bytes_read_file(const char* path) {
    if (!path) return make_hdr(nullptr, 0);
    FILE* f = fopen(path, "rb");
    if (!f) return make_hdr(nullptr, 0);
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return make_hdr(nullptr, 0); }
    long len = ftell(f);
    if (len < 0 || len > INT32_MAX) { fclose(f); return make_hdr(nullptr, 0); }
    fseek(f, 0, SEEK_SET);
    BufHdr* h = make_hdr(nullptr, len);
    if (h && h->data && len > 0 && fread(h->data, 1, static_cast<size_t>(len), f) != static_cast<size_t>(len)) {
        free(h->data); h->data = nullptr; h->size = h->capacity = h->write_idx = 0;
    }
    fclose(f);
    return h;
}

// compress.zlib_bytes(buf, level) → zlib stream of exactly the buffer's bytes.
void* aiml_bytes_zlib(const uint8_t* data, int32_t size, int32_t level) {
    if (size < 0) size = 0;
    uLongf bound = compressBound(static_cast<uLong>(size));
    uint8_t* out = static_cast<uint8_t*>(malloc(bound ? bound : 1));
    if (!out) return make_hdr(nullptr, 0);
    int lv = (level <= 0 || level > 9) ? Z_DEFAULT_COMPRESSION : level;
    if (compress2(out, &bound, data ? data : reinterpret_cast<const uint8_t*>(""), static_cast<uLong>(size), lv) != Z_OK) {
        free(out);
        return make_hdr(nullptr, 0);
    }
    return adopt_hdr(out, static_cast<int64_t>(bound));
}

// compress.zlib_at(buf, off) — inflate ONE zlib stream starting at `off` and
// stop at its end (git packs put streams back to back without lengths).
// compress.zlib_consumed() then tells how many input bytes the stream took
// (-1 after an error).  Empty buffer on error.
void* aiml_bytes_zlib_at(const uint8_t* data, int32_t size, int64_t off) {
    g_zlib_consumed = -1;
    if (!data || off < 0 || off >= size) return make_hdr(nullptr, 0);
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit(&zs) != Z_OK) return make_hdr(nullptr, 0);
    zs.next_in = const_cast<Bytef*>(data + off);
    zs.avail_in = static_cast<uInt>(size - off);
    size_t cap = 4096, used = 0;
    uint8_t* out = static_cast<uint8_t*>(malloc(cap));
    int rc = Z_OK;
    while (out) {
        if (used == cap) {
            if (cap > static_cast<size_t>(INT32_MAX) / 2) { free(out); out = nullptr; break; }
            uint8_t* grown = static_cast<uint8_t*>(realloc(out, cap * 2));
            if (!grown) { free(out); out = nullptr; break; }
            out = grown; cap *= 2;
        }
        zs.next_out = out + used;
        zs.avail_out = static_cast<uInt>(cap - used);
        rc = inflate(&zs, Z_NO_FLUSH);
        used = cap - zs.avail_out;
        if (rc == Z_STREAM_END) break;
        if (rc != Z_OK) break;
        if (zs.avail_in == 0 && zs.avail_out != 0) { rc = Z_DATA_ERROR; break; }  // truncated
    }
    int64_t consumed = static_cast<int64_t>(zs.total_in);
    inflateEnd(&zs);
    if (!out || rc != Z_STREAM_END) { free(out); return make_hdr(nullptr, 0); }
    g_zlib_consumed = consumed;
    return adopt_hdr(out, static_cast<int64_t>(used));
}

int64_t aiml_bytes_zlib_consumed() { return g_zlib_consumed; }

// bytes.crc32(buf, off, len) → CRC-32 (zlib) of the range as an unsigned value; -1 outside.
int64_t aiml_bytes_crc32(const uint8_t* data, int32_t size, int64_t off, int64_t n) {
    if (n < 0) n = static_cast<int64_t>(size) - off;
    if (!in_range(size, off, n)) return -1;
    uLong c = crc32(0L, Z_NULL, 0);
    if (n > 0 && data) c = crc32(c, data + off, static_cast<uInt>(n));
    return static_cast<int64_t>(c);
}

// bytes.compare(a, a_off, b, b_off, len) → -1 / 0 / 1 (memcmp order); 2 when a range is outside.
int32_t aiml_bytes_compare(const uint8_t* a, int32_t alen, int64_t aoff,
                           const uint8_t* b, int32_t blen, int64_t boff, int64_t n) {
    if (!in_range(alen, aoff, n) || !in_range(blen, boff, n) || (n > 0 && (!a || !b))) return 2;
    int r = n > 0 ? memcmp(a + aoff, b + boff, static_cast<size_t>(n)) : 0;
    return r < 0 ? -1 : (r > 0 ? 1 : 0);
}

// bytes.delta_apply(base, delta) — git pack delta (pack-format.txt
// "Deltified representation"): two size varints, then copy (MSB set) and
// insert (1..127) instructions.  Empty buffer when the delta is malformed or
// does not fit the base.
void* aiml_bytes_delta_apply(const uint8_t* base, int32_t base_len, const uint8_t* delta, int32_t delta_len) {
    if (!delta || delta_len <= 0) return make_hdr(nullptr, 0);
    int64_t pos = 0;
    auto varint = [&](int64_t& out) -> bool {
        out = 0; int shift = 0;
        while (pos < delta_len) {
            uint8_t c = delta[pos++];
            out |= static_cast<int64_t>(c & 0x7f) << shift;
            if (!(c & 0x80)) return true;
            shift += 7;
            if (shift > 56) return false;
        }
        return false;
    };
    int64_t src_size = 0, dst_size = 0;
    if (!varint(src_size) || !varint(dst_size)) return make_hdr(nullptr, 0);
    if (src_size != base_len || dst_size < 0 || dst_size > INT32_MAX) return make_hdr(nullptr, 0);
    uint8_t* out = static_cast<uint8_t*>(malloc(dst_size ? static_cast<size_t>(dst_size) : 1));
    if (!out) return make_hdr(nullptr, 0);
    int64_t w = 0;
    while (pos < delta_len) {
        uint8_t op = delta[pos++];
        if (op & 0x80) {
            int64_t cp_off = 0, cp_len = 0;
            for (int i = 0; i < 4; ++i)
                if (op & (1 << i)) { if (pos >= delta_len) goto bad; cp_off |= static_cast<int64_t>(delta[pos++]) << (8 * i); }
            for (int i = 0; i < 3; ++i)
                if (op & (0x10 << i)) { if (pos >= delta_len) goto bad; cp_len |= static_cast<int64_t>(delta[pos++]) << (8 * i); }
            if (cp_len == 0) cp_len = 0x10000;
            if (!base || cp_off < 0 || cp_off + cp_len > base_len || w + cp_len > dst_size) goto bad;
            memcpy(out + w, base + cp_off, static_cast<size_t>(cp_len));
            w += cp_len;
        } else if (op) {
            if (pos + op > delta_len || w + op > dst_size) goto bad;
            memcpy(out + w, delta + pos, op);
            pos += op; w += op;
        } else {
            goto bad;  // opcode 0 is reserved
        }
    }
    if (w != dst_size) goto bad;
    return adopt_hdr(out, dst_size);
bad:
    free(out);
    return make_hdr(nullptr, 0);
}

}  // extern "C"
