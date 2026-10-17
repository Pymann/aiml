// sha256.cpp — portable SHA-256 shared by native + WASM (see sha256.h).
//
// Compiled on both targets.  Owns `aiml_sha256_hex` (the workhorse) and
// `aiml_hash_sha256` (string form of hash.sha256).  The native OpenSSL file
// (hash_runtime.cpp) delegates its sha256 case here instead of defining its own,
// and the WASM glue (hash_wasm_runtime.cpp) does the same — one algorithm, no
// duplication, guaranteed-identical digests across web / native / ./media.

#include "sha256.h"
#include "string_runtime.h"   // aiml_str_alloc: refcounted result (rc-header, no Invalid-read/leak)
#include <cstring>
#include <cstdlib>
#include <cstdio>   // aiml_sha256_file: blockweises Lesen

namespace {

struct Ctx { uint32_t state[8]; uint64_t bitlen; uint8_t data[64]; uint32_t datalen; };

inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

void transform(Ctx* c, const uint8_t* d) {
    uint32_t m[64];
    for (uint32_t i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = (d[j] << 24) | (d[j+1] << 16) | (d[j+2] << 8) | d[j+3];
    for (uint32_t i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(m[i-15],7) ^ rotr(m[i-15],18) ^ (m[i-15] >> 3);
        uint32_t s1 = rotr(m[i-2],17) ^ rotr(m[i-2],19) ^ (m[i-2] >> 10);
        m[i] = m[i-16] + s0 + m[i-7] + s1;
    }
    uint32_t a=c->state[0],b=c->state[1],cc=c->state[2],dd=c->state[3];
    uint32_t e=c->state[4],f=c->state[5],g=c->state[6],h=c->state[7];
    for (uint32_t i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + K[i] + m[i];
        uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = S0 + maj;
        h=g; g=f; f=e; e=dd+t1; dd=cc; cc=b; b=a; a=t1+t2;
    }
    c->state[0]+=a; c->state[1]+=b; c->state[2]+=cc; c->state[3]+=dd;
    c->state[4]+=e; c->state[5]+=f; c->state[6]+=g; c->state[7]+=h;
}

void init(Ctx* c) {
    c->datalen = 0; c->bitlen = 0;
    c->state[0]=0x6a09e667; c->state[1]=0xbb67ae85; c->state[2]=0x3c6ef372; c->state[3]=0xa54ff53a;
    c->state[4]=0x510e527f; c->state[5]=0x9b05688c; c->state[6]=0x1f83d9ab; c->state[7]=0x5be0cd19;
}

void update(Ctx* c, const uint8_t* d, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        c->data[c->datalen++] = d[i];
        if (c->datalen == 64) { transform(c, c->data); c->bitlen += 512; c->datalen = 0; }
    }
}

void final(Ctx* c, uint8_t* hash) {
    uint32_t i = c->datalen;
    if (c->datalen < 56) { c->data[i++] = 0x80; while (i < 56) c->data[i++] = 0x00; }
    else { c->data[i++] = 0x80; while (i < 64) c->data[i++] = 0x00; transform(c, c->data); memset(c->data, 0, 56); }
    c->bitlen += (uint64_t)c->datalen * 8;
    for (int k = 7; k >= 0; --k) c->data[56 + (7 - k)] = (uint8_t)(c->bitlen >> (k * 8));
    transform(c, c->data);
    for (i = 0; i < 4; ++i)
        for (uint32_t j = 0; j < 8; ++j)
            hash[i + j*4] = (uint8_t)(c->state[j] >> (24 - i*8));
}

char* hex(const uint8_t* bytes, size_t len) {
    // spec/61: return a REFCOUNTED string (aiml_str_alloc → rc-header at ptr-8, NUL-terminated) —
    // aiml_sha256_hex/aiml_hash_sha256 feed the `hash.sha256` surface + card_art_uploader, whose
    // callers use aiml_str_incref/decref. A raw malloc buffer had no header, so those read/free 8
    // bytes before the allocation (Invalid read) and never released it (leak: auth legacy verify +
    // artwork upload, leak_probes/round3 §1). aiml_str_alloc allocates payload len*2 (+ NUL, rc=1).
    char* s = aiml_str_alloc(len * 2);
    if (!s) return nullptr;
    static const char hx[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) { s[i*2] = hx[bytes[i] >> 4]; s[i*2+1] = hx[bytes[i] & 0xF]; }
    s[len * 2] = '\0';
    return s;
}

} // namespace

extern "C" {

char* aiml_sha256_hex(const void* data, int32_t len) {
    Ctx c; init(&c);
    if (data && len > 0) update(&c, (const uint8_t*)data, (size_t)len);
    uint8_t digest[32];
    final(&c, digest);
    return hex(digest, 32);
}

// String form of hash.sha256 (used by the REG_HASH1B "sha256" surface for a
// string arg).  Lives here so native + WASM share one implementation.
char* aiml_hash_sha256(const char* s) {
    return aiml_sha256_hex(s, s ? (int32_t)strlen(s) : 0);
}

// Content-address of a FILE, read in 1 MiB blocks: memory use is independent of
// the file size, so a multi-hundred-MB artifact can be hashed without holding it
// (hash.sha256(buffer) would need the whole content in RAM — see spec/33_1 and
// cosmos doc/spec/92 §92.4, which needs this for its CAS blob ids).
//
// Returns the 64-char lowercase hex digest, or an EMPTY string when the file
// cannot be opened or read — never null, so AIML callers never see a null
// string.  "" is unambiguous: an empty FILE hashes to e3b0c442….
char* aiml_sha256_file(const char* path) {
    if (!path || !*path) return hex(nullptr, 0);
    FILE* fobj = fopen(path, "rbe");       // e: close-on-exec, like io.open
    if (!fobj) return hex(nullptr, 0);
    const size_t chunk = 1u << 20;         // 1 MiB
    uint8_t* buf = (uint8_t*)malloc(chunk);
    if (!buf) { fclose(fobj); return hex(nullptr, 0); }
    Ctx c; init(&c);
    size_t got;
    while ((got = fread(buf, 1, chunk, fobj)) > 0) update(&c, buf, got);
    const bool failed = ferror(fobj) != 0;
    free(buf);
    fclose(fobj);
    if (failed) return hex(nullptr, 0);
    uint8_t digest[32];
    final(&c, digest);
    return hex(digest, 32);
}

} // extern "C"
