#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AimlCompressBuffer mirrors the standard AimlBuffer layout:
 *   { i32 size, i32 capacity, i32 read_idx, i32 write_idx, u8* data }
 */
typedef struct {
    int32_t  size;
    int32_t  capacity;
    int32_t  read_idx;
    int32_t  write_idx;
    uint8_t* data;
} AimlCompressBuffer;

/* ── In-memory compress: (data, len, level) → AimlCompressBuffer* ───────────
 * len == -1  → treat data as null-terminated string (strlen applied internally)
 * len >= 0   → treat data as raw bytes of that length
 * level == 0 → algorithm default
 * Returns NULL on error.
 */
AimlCompressBuffer* aiml_compress_gzip   (const char* data, int32_t len, int level);
AimlCompressBuffer* aiml_compress_deflate(const char* data, int32_t len, int level);
AimlCompressBuffer* aiml_compress_zlib   (const char* data, int32_t len, int level);
AimlCompressBuffer* aiml_compress_brotli (const char* data, int32_t len, int level);
AimlCompressBuffer* aiml_compress_zstd   (const char* data, int32_t len, int level);
AimlCompressBuffer* aiml_compress_lz4    (const char* data, int32_t len);  /* no level */

/* ── Decompress: AimlCompressBuffer* → AimlCompressBuffer* ─────────────── */
AimlCompressBuffer* aiml_compress_gunzip          (AimlCompressBuffer* in);
AimlCompressBuffer* aiml_compress_inflate         (AimlCompressBuffer* in);
AimlCompressBuffer* aiml_compress_zlib_decompress (AimlCompressBuffer* in);
AimlCompressBuffer* aiml_compress_brotli_decompress(AimlCompressBuffer* in);
AimlCompressBuffer* aiml_compress_zstd_decompress (AimlCompressBuffer* in);
AimlCompressBuffer* aiml_compress_lz4_decompress  (AimlCompressBuffer* in);

/* ── Decompress to UTF-8 string: AimlCompressBuffer* → char* ────────────── */
char* aiml_compress_gunzip_str           (AimlCompressBuffer* in);
char* aiml_compress_inflate_str          (AimlCompressBuffer* in);
char* aiml_compress_zlib_decompress_str  (AimlCompressBuffer* in);
char* aiml_compress_brotli_decompress_str(AimlCompressBuffer* in);
char* aiml_compress_zstd_decompress_str  (AimlCompressBuffer* in);
char* aiml_compress_lz4_decompress_str   (AimlCompressBuffer* in);

/* ── Archive operations (filesystem): 0 = ok, negative = error ─────────── */
int32_t aiml_compress_tar_gz  (const char* src, const char* dst);
int32_t aiml_compress_untar_gz(const char* src, const char* dst_dir);
int32_t aiml_compress_zip     (const char* src, const char* dst);
int32_t aiml_compress_unzip   (const char* src, const char* dst_dir);

/* ── Archive traversal: entry structs ────────────────────────────────────
 * Structs mirror LLVM layout so the AIML compiler can GEP into them.
 * Layout must match builtin_registry.cpp class registration exactly.
 */
typedef struct {
    char*    name;            /* heap-alloc'd strdup, index 0 */
    int64_t  size;            /* uncompressed bytes,  index 1 */
    int64_t  compressed_size; /*                      index 2 */
    int32_t  is_dir;          /* 1=directory,         index 3 */
    int32_t  crc32;           /* CRC-32,              index 4 */
    int32_t  method;          /* 0=stored 8=deflated, index 5 */
    int32_t  _pad;            /* struct padding       index 6 */
} AimlZipEntry;               /* sizeof == 40 bytes           */

typedef struct {
    char*    name;    /* heap-alloc'd strdup, index 0 */
    int64_t  size;    /* file size bytes,     index 1 */
    int32_t  is_dir;  /* 1=directory,         index 2 */
    int32_t  mode;    /* unix mode (octal),   index 3 */
} AimlTarEntry;       /* sizeof == 24 bytes           */

/* Fills an AimlListRaw {int32 size, int32 capacity, void** data} with
 * heap-allocated AimlZipEntry* / AimlTarEntry* pointers. */
void aiml_compress_zip_entries_fill(const char* path, void* list_ptr);
void aiml_compress_tar_entries_fill(const char* path, void* list_ptr);

#ifdef __cplusplus
}
#endif
