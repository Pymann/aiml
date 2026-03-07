#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AimlHashBuffer mirrors the standard AimlBuffer layout used throughout the
 * runtime so that buffer<u8> return values are compatible with the buffer class.
 *   { i32 size, i32 capacity, i32 read_idx, i32 write_idx, u8* data }
 */
typedef struct {
    int32_t  size;
    int32_t  capacity;
    int32_t  read_idx;
    int32_t  write_idx;
    uint8_t* data;
} AimlHashBuffer;

/* ── hex-string output (heap-alloc'd char*, lowercase, no prefix) ──────── */
char*   aiml_hash_sha256    (const char* s);
char*   aiml_hash_sha512    (const char* s);
char*   aiml_hash_sha3_256  (const char* s);
char*   aiml_hash_keccak256 (const char* s);   /* Ethereum-compatible, NOT SHA-3 */
char*   aiml_hash_ripemd160 (const char* s);
char*   aiml_hash_blake2b   (const char* s);   /* BLAKE2b-512, 128 hex chars */
char*   aiml_hash_blake2s   (const char* s);   /* BLAKE2s-256, 64 hex chars  */
char*   aiml_hash_md5       (const char* s);

int32_t aiml_hash_crc32     (const char* s);

char*   aiml_hash_hmac_sha256(const char* key, const char* msg);
char*   aiml_hash_hmac_sha512(const char* key, const char* msg);

/* ── raw-bytes output (heap-alloc'd AimlHashBuffer*, cast to void*) ────── */
void*   aiml_hash_sha256_raw    (const char* s);
void*   aiml_hash_sha512_raw    (const char* s);
void*   aiml_hash_sha3_256_raw  (const char* s);
void*   aiml_hash_keccak256_raw (const char* s);
void*   aiml_hash_ripemd160_raw (const char* s);
void*   aiml_hash_blake2b_raw   (const char* s);
void*   aiml_hash_hmac_sha256_raw(const char* key, const char* msg);

#ifdef __cplusplus
}
#endif
