// base64 (spec/67_identity.md §2) — RFC 4648 standard and URL-safe alphabets.
//
// Encoding takes a string or (via the compiler) a buffer<u8> pointer+length, so
// one entry point covers text and binary (style guideline §1).  Decoding is
// strict about the alphabet and lenient about padding and whitespace (line
// breaks are common in PEM bodies and SAML POST values).
#include "identity_runtime.h"

#include <cstdlib>
#include <cstring>
#include <string>

extern "C" char* aiml_str_alloc(size_t len);
extern "C" char* aiml_str_dup(const char* s);

namespace {

const char kStd[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const char kUrl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

struct BufHeader { int32_t size, cap, rIdx, wIdx; char* data; };

int sextet(unsigned char c, bool url) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (!url && c == '+') return 62;
    if (!url && c == '/') return 63;
    if (url && c == '-') return 62;
    if (url && c == '_') return 63;
    return -1;
}

std::string encode(const unsigned char* p, size_t n, bool url) {
    const char* tbl = url ? kUrl : kStd;
    std::string out;
    out.reserve(((n + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < n; i += 3) {
        uint32_t v = (uint32_t(p[i]) << 16) | (uint32_t(p[i + 1]) << 8) | p[i + 2];
        out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];  out += tbl[v & 63];
    }
    if (i < n) {
        uint32_t v = uint32_t(p[i]) << 16;
        if (i + 1 < n) v |= uint32_t(p[i + 1]) << 8;
        out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63];
        if (i + 1 < n) out += tbl[(v >> 6) & 63];
        else if (!url) out += '=';
        if (!url) out += '=';
    }
    return out;
}

// Returns false on any character outside the alphabet, misplaced padding or an
// impossible length (a single trailing sextet carries no whole byte).
bool decode(const char* s, bool url, std::string& out) {
    out.clear();
    if (!s) return false;
    uint32_t acc = 0;
    int bits = 0, groupLen = 0;
    bool padding = false;
    size_t pads = 0;
    for (const char* c = s; *c; ++c) {
        unsigned char ch = static_cast<unsigned char>(*c);
        if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') continue;
        if (ch == '=') { padding = true; ++pads; continue; }
        if (padding) return false;                 // data after padding
        int v = sextet(ch, url);
        if (v < 0) return false;
        acc = (acc << 6) | uint32_t(v);
        bits += 6;
        groupLen = (groupLen + 1) % 4;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acc >> bits) & 0xFF);
        }
    }
    if (groupLen == 1) return false;
    if (pads > 2) return false;
    if (pads && (groupLen + pads) % 4 != 0) return false;
    return true;
}

char* managed(const std::string& s) {
    char* d = aiml_str_alloc(s.size());
    if (d && !s.empty()) memcpy(d, s.data(), s.size());
    return d;
}

}  // namespace

extern "C" char* aiml_b64_encode(const void* data, int32_t len, int32_t url) {
    if (!data || len <= 0) return aiml_str_alloc(0);
    return managed(encode(static_cast<const unsigned char*>(data), size_t(len), url != 0));
}

extern "C" char* aiml_b64_encode_str(const char* s, int32_t url) {
    if (!s) return aiml_str_alloc(0);
    return managed(encode(reinterpret_cast<const unsigned char*>(s), strlen(s), url != 0));
}

extern "C" void* aiml_b64_decode_buf(const char* s, int32_t url) {
    std::string bytes;
    if (!decode(s, url != 0, bytes)) bytes.clear();
    BufHeader* b = static_cast<BufHeader*>(calloc(1, sizeof(BufHeader)));
    if (!b) return nullptr;
    b->data = static_cast<char*>(malloc(bytes.size() + 1));
    if (!b->data) { free(b); return nullptr; }
    if (!bytes.empty()) memcpy(b->data, bytes.data(), bytes.size());
    b->data[bytes.size()] = '\0';
    b->size = b->cap = b->wIdx = static_cast<int32_t>(bytes.size());
    return b;
}

extern "C" char* aiml_b64_decode_str(const char* s, int32_t url) {
    std::string bytes;
    if (!decode(s, url != 0, bytes)) return nullptr;
    return aiml_str_dup(bytes.c_str());
}
