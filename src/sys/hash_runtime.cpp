#include "hash_runtime.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ── Helpers ────────────────────────────────────────────────────────────── */

static char* to_hex(const uint8_t* bytes, size_t len) {
    char* s = (char*)malloc(len * 2 + 1);
    if (!s) return nullptr;
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        s[i*2]   = hex[bytes[i] >> 4];
        s[i*2+1] = hex[bytes[i] & 0xF];
    }
    s[len * 2] = '\0';
    return s;
}

static AimlHashBuffer* make_hash_buffer(const uint8_t* bytes, int32_t len) {
    AimlHashBuffer* buf = (AimlHashBuffer*)malloc(sizeof(AimlHashBuffer));
    uint8_t* data = (uint8_t*)malloc(len);
    memcpy(data, bytes, len);
    buf->size      = len;
    buf->capacity  = len;
    buf->read_idx  = 0;
    buf->write_idx = len;
    buf->data      = data;
    return buf;
}

/* Run OpenSSL EVP digest on a null-terminated string → hex string */
static char* evp_hex(const EVP_MD* md, const char* s) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, md, nullptr);
    EVP_DigestUpdate(ctx, s, strlen(s));
    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned int dlen = 0;
    EVP_DigestFinal_ex(ctx, digest, &dlen);
    EVP_MD_CTX_free(ctx);
    return to_hex(digest, dlen);
}

/* Run OpenSSL EVP digest → raw AimlHashBuffer */
static void* evp_raw(const EVP_MD* md, const char* s) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, md, nullptr);
    EVP_DigestUpdate(ctx, s, strlen(s));
    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned int dlen = 0;
    EVP_DigestFinal_ex(ctx, digest, &dlen);
    EVP_MD_CTX_free(ctx);
    return make_hash_buffer(digest, (int32_t)dlen);
}

/* HMAC helper → hex string */
static char* hmac_hex(const EVP_MD* md, const char* key, const char* msg) {
    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned int dlen = 0;
    HMAC(md,
         (const void*)key, (int)strlen(key),
         (const unsigned char*)msg, (int)strlen(msg),
         digest, &dlen);
    return to_hex(digest, dlen);
}

/* HMAC helper → raw buffer */
static void* hmac_raw(const EVP_MD* md, const char* key, const char* msg) {
    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned int dlen = 0;
    HMAC(md,
         (const void*)key, (int)strlen(key),
         (const unsigned char*)msg, (int)strlen(msg),
         digest, &dlen);
    return make_hash_buffer(digest, (int32_t)dlen);
}

/* ── CRC32 (handrolled — no zlib dependency) ─────────────────────────── */

static uint32_t s_crc32_table[256];
static int      s_crc32_initialized = 0;

static void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c >> 1) ^ (c & 1u ? 0xEDB88320u : 0u);
        s_crc32_table[i] = c;
    }
    s_crc32_initialized = 1;
}

int32_t aiml_hash_crc32(const char* s) {
    if (!s_crc32_initialized) crc32_init();
    uint32_t crc = 0xFFFFFFFFu;
    while (*s)
        crc = (crc >> 8) ^ s_crc32_table[(crc ^ (uint8_t)*s++) & 0xFF];
    return (int32_t)(crc ^ 0xFFFFFFFFu);
}

/* ── RIPEMD-160 (handrolled — avoids OpenSSL 3.x legacy-provider issues) ─ */

#define ROL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

/* Message selection — left and right parallel lines */
static const int kRmdRL[80] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
    7,4,13,1,10,6,15,3,12,0,9,5,2,14,11,8,
    3,10,14,4,9,15,8,1,2,7,0,6,13,11,5,12,
    1,9,11,10,0,8,12,4,13,3,7,15,14,5,6,2,
    4,0,5,9,7,12,2,10,14,1,3,8,11,6,15,13
};
static const int kRmdRR[80] = {
    5,14,7,0,9,2,11,4,13,6,15,8,1,10,3,12,
    6,11,3,7,0,13,5,10,14,15,8,12,4,9,1,2,
    15,5,1,3,7,14,6,9,11,8,12,2,10,0,4,13,
    8,6,4,1,3,11,15,0,5,12,2,13,9,7,10,14,
    12,15,10,4,1,5,8,7,6,2,13,14,0,3,9,11
};
/* Rotation amounts — left and right */
static const int kRmdSL[80] = {
    11,14,15,12,5,8,7,9,11,13,14,15,6,7,9,8,
    7,6,8,13,11,9,7,15,7,12,15,9,11,7,13,12,
    11,13,6,7,14,9,13,15,14,8,13,6,5,12,7,5,
    11,12,14,15,14,15,9,8,9,14,5,6,8,6,5,12,
    9,15,5,11,6,8,13,12,5,12,13,14,11,8,5,6
};
static const int kRmdSR[80] = {
    8,9,9,11,13,15,15,5,7,7,8,11,14,14,12,6,
    9,13,15,7,12,8,9,11,7,7,12,7,6,15,13,11,
    9,7,15,11,8,6,6,14,12,13,5,14,13,13,7,5,
    15,5,8,11,14,14,6,14,6,9,12,9,12,5,15,8,
    8,5,12,9,12,5,14,6,8,13,6,5,15,13,11,11
};
/* Round constants */
static const uint32_t kRmdKL[5] = {0x00000000u,0x5A827999u,0x6ED9EBA1u,0x8F1BBCDCu,0xA953FD4Eu};
static const uint32_t kRmdKR[5] = {0x50A28BE6u,0x5C4DD124u,0x6D703EF3u,0x7A6D76E9u,0x00000000u};

static inline uint32_t rmd_f(int r, uint32_t x, uint32_t y, uint32_t z) {
    if (r < 16) return x ^ y ^ z;
    if (r < 32) return (x & y) | (~x & z);
    if (r < 48) return (x | ~y) ^ z;
    if (r < 64) return (x & z) | (y & ~z);
    return x ^ (y | ~z);
}
static inline uint32_t rmd_fr(int r, uint32_t x, uint32_t y, uint32_t z) {
    if (r < 16) return x ^ (y | ~z);
    if (r < 32) return (x & z) | (y & ~z);
    if (r < 48) return (x | ~y) ^ z;
    if (r < 64) return (x & y) | (~x & z);
    return x ^ y ^ z;
}

static void rmd160_compress(uint32_t* h, const uint32_t* w) {
    uint32_t al=h[0],bl=h[1],cl=h[2],dl=h[3],el=h[4];
    uint32_t ar=h[0],br=h[1],cr=h[2],dr=h[3],er=h[4];
    for (int i = 0; i < 80; i++) {
        int r = i / 16;
        uint32_t tl = ROL32(al + rmd_f(i, bl, cl, dl) + w[kRmdRL[i]] + kRmdKL[r], kRmdSL[i]) + el;
        al = el; el = dl; dl = ROL32(cl, 10); cl = bl; bl = tl;
        uint32_t tr = ROL32(ar + rmd_fr(i, br, cr, dr) + w[kRmdRR[i]] + kRmdKR[r], kRmdSR[i]) + er;
        ar = er; er = dr; dr = ROL32(cr, 10); cr = br; br = tr;
    }
    uint32_t t = h[1] + cl + dr;
    h[1] = h[2] + dl + er;
    h[2] = h[3] + el + ar;
    h[3] = h[4] + al + br;
    h[4] = h[0] + bl + cr;
    h[0] = t;
}

static void rmd160_hash(const uint8_t* data, size_t len, uint8_t out[20]) {
    uint32_t h[5] = {0x67452301u,0xEFCDAB89u,0x98BADCFEu,0x10325476u,0xC3D2E1F0u};
    /* MD-padding: same as MD4/MD5 */
    size_t padlen = ((len + 8) / 64 + 1) * 64;
    uint8_t* buf = (uint8_t*)calloc(padlen, 1);
    memcpy(buf, data, len);
    buf[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    memcpy(buf + padlen - 8, &bits, 8); /* little-endian on LE host */
    for (size_t i = 0; i < padlen; i += 64) {
        uint32_t w[16];
        memcpy(w, buf + i, 64);
        rmd160_compress(h, w);
    }
    free(buf);
    memcpy(out, h, 20);
}

char* aiml_hash_ripemd160(const char* s) {
    uint8_t digest[20];
    rmd160_hash((const uint8_t*)s, strlen(s), digest);
    return to_hex(digest, 20);
}

void* aiml_hash_ripemd160_raw(const char* s) {
    uint8_t digest[20];
    rmd160_hash((const uint8_t*)s, strlen(s), digest);
    return make_hash_buffer(digest, 20);
}

/* ── Keccak-256 (handrolled — Ethereum-compatible, NOT NIST SHA-3) ────── */
/* Uses the "tiny_keccak" cycle-based ρ+π approach; RC / rotc / piln verified
 * against the Keccak Team reference implementation.                         */

static const uint64_t kKeccakRC[24] = {
    0x0000000000000001ULL,0x0000000000008082ULL,0x800000000000808AULL,0x8000000080008000ULL,
    0x000000000000808BULL,0x0000000080000001ULL,0x8000000080008081ULL,0x8000000000008009ULL,
    0x000000000000008AULL,0x0000000000000088ULL,0x0000000080008009ULL,0x000000008000000AULL,
    0x000000008000808BULL,0x800000000000008BULL,0x8000000000008089ULL,0x8000000000008003ULL,
    0x8000000000008002ULL,0x8000000000000080ULL,0x000000000000800AULL,0x800000008000000AULL,
    0x8000000080008081ULL,0x8000000000008080ULL,0x0000000080000001ULL,0x8000000080008008ULL
};
/* piln / rotc cycle: starting from lane[1], traces the 24 non-trivial π destinations */
static const int kKeccakPiln[24] = {10,7,11,17,18,3,5,16,8,21,24,4,15,23,19,13,12,2,20,14,22,9,6,1};
static const int kKeccakRotc[24] = {1,3,6,10,15,21,28,36,45,55,2,14,27,41,56,8,25,43,62,18,39,61,20,44};

#define ROL64(x,n) (((x)<<(n))|((x)>>(64-(n))))

static void keccak_f1600(uint64_t st[25]) {
    int i, j, r;
    uint64_t t, bc[5];
    for (r = 0; r < 24; r++) {
        /* θ */
        for (i = 0; i < 5; i++)
            bc[i] = st[i] ^ st[i+5] ^ st[i+10] ^ st[i+15] ^ st[i+20];
        for (i = 0; i < 5; i++) {
            t = bc[(i+4)%5] ^ ROL64(bc[(i+1)%5], 1);
            for (j = 0; j < 25; j += 5) st[j+i] ^= t;
        }
        /* ρ + π (cycle-based) */
        t = st[1];
        for (i = 0; i < 24; i++) {
            j = kKeccakPiln[i];
            bc[0] = st[j];
            st[j] = ROL64(t, kKeccakRotc[i]);
            t = bc[0];
        }
        /* χ */
        for (j = 0; j < 25; j += 5) {
            for (i = 0; i < 5; i++) bc[i] = st[j+i];
            for (i = 0; i < 5; i++) st[j+i] ^= (~bc[(i+1)%5]) & bc[(i+2)%5];
        }
        /* ι */
        st[0] ^= kKeccakRC[r];
    }
}

static void keccak256_hash(const uint8_t* data, size_t len, uint8_t out[32]) {
    const size_t rate = 136; /* 1088 bits */
    uint64_t A[25];
    memset(A, 0, sizeof(A));
    /* Absorb */
    size_t off = 0;
    while (off + rate <= len) {
        for (size_t i = 0; i < rate/8; i++) {
            uint64_t w; memcpy(&w, data + off + i*8, 8);
            A[i] ^= w;
        }
        keccak_f1600(A);
        off += rate;
    }
    /* Pad (Keccak: 0x01, NOT 0x06 as in SHA-3) */
    uint8_t last[136];
    size_t rem = len - off;
    memset(last, 0, rate);
    memcpy(last, data + off, rem);
    last[rem] ^= 0x01;
    last[rate - 1] ^= 0x80;
    for (size_t i = 0; i < rate/8; i++) {
        uint64_t w; memcpy(&w, last + i*8, 8);
        A[i] ^= w;
    }
    keccak_f1600(A);
    memcpy(out, A, 32);
}

char* aiml_hash_keccak256(const char* s) {
    uint8_t digest[32];
    keccak256_hash((const uint8_t*)s, strlen(s), digest);
    return to_hex(digest, 32);
}

void* aiml_hash_keccak256_raw(const char* s) {
    uint8_t digest[32];
    keccak256_hash((const uint8_t*)s, strlen(s), digest);
    return make_hash_buffer(digest, 32);
}

/* ── OpenSSL-backed functions ────────────────────────────────────────── */

char* aiml_hash_sha256    (const char* s) { return evp_hex(EVP_sha256(),    s); }
char* aiml_hash_sha512    (const char* s) { return evp_hex(EVP_sha512(),    s); }
char* aiml_hash_sha3_256  (const char* s) { return evp_hex(EVP_sha3_256(),  s); }
char* aiml_hash_blake2b   (const char* s) { return evp_hex(EVP_blake2b512(), s); }
char* aiml_hash_blake2s   (const char* s) { return evp_hex(EVP_blake2s256(), s); }
char* aiml_hash_md5       (const char* s) { return evp_hex(EVP_md5(),        s); }

char* aiml_hash_hmac_sha256(const char* key, const char* msg) { return hmac_hex(EVP_sha256(), key, msg); }
char* aiml_hash_hmac_sha512(const char* key, const char* msg) { return hmac_hex(EVP_sha512(), key, msg); }

void* aiml_hash_sha256_raw    (const char* s) { return evp_raw(EVP_sha256(),    s); }
void* aiml_hash_sha512_raw    (const char* s) { return evp_raw(EVP_sha512(),    s); }
void* aiml_hash_sha3_256_raw  (const char* s) { return evp_raw(EVP_sha3_256(),  s); }
void* aiml_hash_blake2b_raw   (const char* s) { return evp_raw(EVP_blake2b512(), s); }
void* aiml_hash_hmac_sha256_raw(const char* key, const char* msg) { return hmac_raw(EVP_sha256(), key, msg); }
