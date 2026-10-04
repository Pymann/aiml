#include "compress_runtime.h"
#include "string_runtime.h"

#include <zlib.h>
#include <brotli/encode.h>
#include <brotli/decode.h>
#include <zstd.h>
#include <lz4.h>
#include <zip.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>

/* ── Buffer helpers ──────────────────────────────────────────────────────── */

static AimlCompressBuffer* make_cbuf(uint8_t* data, int32_t size) {
    AimlCompressBuffer* b = (AimlCompressBuffer*)malloc(sizeof(AimlCompressBuffer));
    if (!b) { free(data); return nullptr; }
    b->data      = data;
    b->size      = size;
    b->capacity  = size;
    b->read_idx  = 0;
    b->write_idx = size;
    return b;
}

/* Resolve input length: -1 → strlen, otherwise use given length */
static int32_t resolve_len(const char* data, int32_t len) {
    return (len < 0) ? (int32_t)strlen(data) : len;
}

/* Convert decompressed buffer to null-terminated string */
static char* cbuf_to_str(AimlCompressBuffer* b) {
    if (!b) return nullptr;
    char* s = (char*)malloc((size_t)b->size + 1);
    if (!s) return nullptr;
    memcpy(s, b->data, (size_t)b->size);
    s[b->size] = '\0';
    free(b->data);
    free(b);
    return s;
}

/* ── zlib inflate helper (handles gzip / zlib / raw deflate) ─────────────── */

static AimlCompressBuffer* inflate_all(const uint8_t* comp, int32_t comp_size, int windowBits) {
    int32_t out_size = comp_size * 4 + 4096;
    uint8_t* out = (uint8_t*)malloc((size_t)out_size);
    if (!out) return nullptr;

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, windowBits) != Z_OK) { free(out); return nullptr; }

    zs.next_in  = (Bytef*)comp;
    zs.avail_in = (uInt)comp_size;

    int rc;
    do {
        if ((int32_t)zs.total_out >= out_size) {
            int32_t new_size = out_size * 2;
            uint8_t* tmp = (uint8_t*)realloc(out, (size_t)new_size);
            if (!tmp) { inflateEnd(&zs); free(out); return nullptr; }
            out      = tmp;
            out_size = new_size;
        }
        zs.next_out  = out + zs.total_out;
        zs.avail_out = (uInt)(out_size - (int32_t)zs.total_out);
        rc = inflate(&zs, Z_NO_FLUSH);
    } while (rc == Z_OK);

    int32_t used = (int32_t)zs.total_out;
    inflateEnd(&zs);

    if (rc != Z_STREAM_END) { free(out); return nullptr; }
    return make_cbuf(out, used);
}

/* ── gzip ────────────────────────────────────────────────────────────────── */

AimlCompressBuffer* aiml_compress_gzip(const char* data, int32_t len, int level) {
    if (!data) return nullptr;
    int32_t in_len = resolve_len(data, len);

    uLong bound = compressBound((uLong)in_len) + 64;
    uint8_t* out = (uint8_t*)malloc((size_t)bound);
    if (!out) return nullptr;

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    int lv = (level <= 0) ? Z_DEFAULT_COMPRESSION : level;
    if (deflateInit2(&zs, lv, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        free(out); return nullptr;
    }
    zs.next_in   = (Bytef*)data;
    zs.avail_in  = (uInt)in_len;
    zs.next_out  = out;
    zs.avail_out = (uInt)bound;
    int rc = deflate(&zs, Z_FINISH);
    deflateEnd(&zs);
    if (rc != Z_STREAM_END) { free(out); return nullptr; }
    return make_cbuf(out, (int32_t)zs.total_out);
}

AimlCompressBuffer* aiml_compress_gunzip(AimlCompressBuffer* in) {
    if (!in || !in->data || in->size <= 0) return nullptr;
    return inflate_all(in->data, in->size, 15 | 32); /* 32 = auto gzip/zlib */
}

char* aiml_compress_gunzip_str(AimlCompressBuffer* in) {
    return cbuf_to_str(aiml_compress_gunzip(in));
}

/* ── raw deflate ─────────────────────────────────────────────────────────── */

AimlCompressBuffer* aiml_compress_deflate(const char* data, int32_t len, int level) {
    if (!data) return nullptr;
    int32_t in_len = resolve_len(data, len);

    uLong bound = compressBound((uLong)in_len);
    uint8_t* out = (uint8_t*)malloc((size_t)bound);
    if (!out) return nullptr;

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    int lv = (level <= 0) ? Z_DEFAULT_COMPRESSION : level;
    if (deflateInit2(&zs, lv, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        free(out); return nullptr;
    }
    zs.next_in   = (Bytef*)data;
    zs.avail_in  = (uInt)in_len;
    zs.next_out  = out;
    zs.avail_out = (uInt)bound;
    int rc = deflate(&zs, Z_FINISH);
    deflateEnd(&zs);
    if (rc != Z_STREAM_END) { free(out); return nullptr; }
    return make_cbuf(out, (int32_t)zs.total_out);
}

AimlCompressBuffer* aiml_compress_inflate(AimlCompressBuffer* in) {
    if (!in || !in->data || in->size <= 0) return nullptr;
    return inflate_all(in->data, in->size, -15); /* raw deflate */
}

char* aiml_compress_inflate_str(AimlCompressBuffer* in) {
    return cbuf_to_str(aiml_compress_inflate(in));
}

/* ── zlib-wrapped deflate ────────────────────────────────────────────────── */

AimlCompressBuffer* aiml_compress_zlib(const char* data, int32_t len, int level) {
    if (!data) return nullptr;
    int32_t in_len = resolve_len(data, len);

    uLong bound = compressBound((uLong)in_len);
    uint8_t* out = (uint8_t*)malloc((size_t)bound);
    if (!out) return nullptr;

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    int lv = (level <= 0) ? Z_DEFAULT_COMPRESSION : level;
    if (deflateInit(&zs, lv) != Z_OK) { free(out); return nullptr; }
    zs.next_in   = (Bytef*)data;
    zs.avail_in  = (uInt)in_len;
    zs.next_out  = out;
    zs.avail_out = (uInt)bound;
    int rc = deflate(&zs, Z_FINISH);
    deflateEnd(&zs);
    if (rc != Z_STREAM_END) { free(out); return nullptr; }
    return make_cbuf(out, (int32_t)zs.total_out);
}

AimlCompressBuffer* aiml_compress_zlib_decompress(AimlCompressBuffer* in) {
    if (!in || !in->data || in->size <= 0) return nullptr;
    return inflate_all(in->data, in->size, 15); /* zlib format */
}

char* aiml_compress_zlib_decompress_str(AimlCompressBuffer* in) {
    return cbuf_to_str(aiml_compress_zlib_decompress(in));
}

/* ── brotli ──────────────────────────────────────────────────────────────── */

AimlCompressBuffer* aiml_compress_brotli(const char* data, int32_t len, int level) {
    if (!data) return nullptr;
    int32_t in_len = resolve_len(data, len);

    size_t out_size = BrotliEncoderMaxCompressedSize((size_t)in_len);
    uint8_t* out = (uint8_t*)malloc(out_size);
    if (!out) return nullptr;

    int lv = (level <= 0) ? BROTLI_DEFAULT_QUALITY : level;
    if (!BrotliEncoderCompress(lv, BROTLI_DEFAULT_WINDOW, BROTLI_DEFAULT_MODE,
                               (size_t)in_len, (const uint8_t*)data,
                               &out_size, out)) {
        free(out); return nullptr;
    }
    return make_cbuf(out, (int32_t)out_size);
}

AimlCompressBuffer* aiml_compress_brotli_decompress(AimlCompressBuffer* in) {
    if (!in || !in->data || in->size <= 0) return nullptr;

    size_t out_size = (size_t)in->size * 8 + 4096;
    uint8_t* out = (uint8_t*)malloc(out_size);
    if (!out) return nullptr;

    BrotliDecoderResult res = BrotliDecoderDecompress(
        (size_t)in->size, in->data, &out_size, out);
    if (res != BROTLI_DECODER_RESULT_SUCCESS) { free(out); return nullptr; }
    return make_cbuf(out, (int32_t)out_size);
}

char* aiml_compress_brotli_decompress_str(AimlCompressBuffer* in) {
    return cbuf_to_str(aiml_compress_brotli_decompress(in));
}

/* ── zstd ────────────────────────────────────────────────────────────────── */

AimlCompressBuffer* aiml_compress_zstd(const char* data, int32_t len, int level) {
    if (!data) return nullptr;
    int32_t in_len = resolve_len(data, len);

    int lv = (level <= 0) ? 3 : level;
    size_t bound = ZSTD_compressBound((size_t)in_len);
    uint8_t* out = (uint8_t*)malloc(bound);
    if (!out) return nullptr;

    size_t result = ZSTD_compress(out, bound, data, (size_t)in_len, lv);
    if (ZSTD_isError(result)) { free(out); return nullptr; }
    return make_cbuf(out, (int32_t)result);
}

AimlCompressBuffer* aiml_compress_zstd_decompress(AimlCompressBuffer* in) {
    if (!in || !in->data || in->size <= 0) return nullptr;

    unsigned long long orig = ZSTD_getFrameContentSize(in->data, (size_t)in->size);
    int32_t out_size;
    if (orig == ZSTD_CONTENTSIZE_UNKNOWN || orig == ZSTD_CONTENTSIZE_ERROR)
        out_size = in->size * 8 + 4096;
    else
        out_size = (int32_t)orig;

    uint8_t* out = (uint8_t*)malloc((size_t)out_size);
    if (!out) return nullptr;

    size_t result = ZSTD_decompress(out, (size_t)out_size, in->data, (size_t)in->size);
    if (ZSTD_isError(result)) { free(out); return nullptr; }
    return make_cbuf(out, (int32_t)result);
}

char* aiml_compress_zstd_decompress_str(AimlCompressBuffer* in) {
    return cbuf_to_str(aiml_compress_zstd_decompress(in));
}

/* ── lz4 ─────────────────────────────────────────────────────────────────── */
/*
 * Frame format: [4 bytes LE: original size] [compressed data]
 * This simple header is sufficient for our in-memory round-trips.
 */

AimlCompressBuffer* aiml_compress_lz4(const char* data, int32_t len) {
    if (!data) return nullptr;
    int32_t in_len = resolve_len(data, len);

    int bound = LZ4_compressBound(in_len);
    if (bound <= 0) return nullptr;
    uint8_t* out = (uint8_t*)malloc((size_t)bound + 4);
    if (!out) return nullptr;

    /* Store original size as 4-byte LE header */
    memcpy(out, &in_len, 4);
    int comp_size = LZ4_compress_default(data, (char*)(out + 4), in_len, bound);
    if (comp_size <= 0) { free(out); return nullptr; }
    return make_cbuf(out, comp_size + 4);
}

AimlCompressBuffer* aiml_compress_lz4_decompress(AimlCompressBuffer* in) {
    if (!in || !in->data || in->size < 4) return nullptr;

    int32_t orig_size;
    memcpy(&orig_size, in->data, 4);
    if (orig_size <= 0 || orig_size > 256 * 1024 * 1024) return nullptr;

    uint8_t* out = (uint8_t*)malloc((size_t)orig_size);
    if (!out) return nullptr;

    int result = LZ4_decompress_safe((const char*)(in->data + 4), (char*)out,
                                      in->size - 4, orig_size);
    if (result < 0) { free(out); return nullptr; }
    return make_cbuf(out, result);
}

char* aiml_compress_lz4_decompress_str(AimlCompressBuffer* in) {
    return cbuf_to_str(aiml_compress_lz4_decompress(in));
}

/* ══════════════════════════════════════════════════════════════════════════
 * tar.gz  (ustar format + gzip via zlib)
 * ════════════════════════════════════════════════════════════════════════ */

#define USTAR_BLOCK 512

static void compute_tar_checksum(char hdr[USTAR_BLOCK]) {
    memset(hdr + 148, ' ', 8);
    unsigned sum = 0;
    for (int i = 0; i < USTAR_BLOCK; i++)
        sum += (unsigned char)hdr[i];
    snprintf(hdr + 148, 7, "%06o", sum);
    hdr[154] = '\0';
    hdr[155] = ' ';
}

static void fill_ustar_header(char hdr[USTAR_BLOCK], const char* name,
                               mode_t mode, uint64_t size,
                               time_t mtime, char type) {
    memset(hdr, 0, USTAR_BLOCK);
    strncpy(hdr,          name, 99);
    snprintf(hdr + 100,  8,  "%07o",    (unsigned)(mode & 07777));
    snprintf(hdr + 108,  8,  "%07o",    0u);  /* uid */
    snprintf(hdr + 116,  8,  "%07o",    0u);  /* gid */
    snprintf(hdr + 124,  12, "%011llo",  (unsigned long long)size);
    snprintf(hdr + 136,  12, "%011llo",  (unsigned long long)(unsigned long)mtime);
    hdr[156] = type;
    memcpy(hdr + 257, "ustar", 5);
    hdr[263] = '0'; hdr[264] = '0';
    compute_tar_checksum(hdr);
}

/* Simple dynamic string vector */
typedef struct { char** items; int n; int cap; } StrVec;

static void sv_push(StrVec* v, const char* s) {
    if (v->n >= v->cap) {
        v->cap  = v->cap ? v->cap * 2 : 32;
        v->items = (char**)realloc(v->items, (size_t)v->cap * sizeof(char*));
    }
    v->items[v->n++] = strdup(s);
}

static void sv_free(StrVec* v) {
    for (int i = 0; i < v->n; i++) free(v->items[i]);
    free(v->items);
    v->items = nullptr; v->n = v->cap = 0;
}

/* Recursively collect dirs and regular files relative to base_root */
static void collect_entries(const char* base_root, const char* rel,
                             StrVec* dirs, StrVec* files) {
    char full[4096];
    if (rel && rel[0])
        snprintf(full, sizeof(full), "%s/%s", base_root, rel);
    else
        snprintf(full, sizeof(full), "%s", base_root);

    struct stat st;
    if (lstat(full, &st) != 0) return;

    if (S_ISDIR(st.st_mode)) {
        if (rel && rel[0]) sv_push(dirs, rel);
        DIR* d = opendir(full);
        if (!d) return;
        struct dirent* ent;
        while ((ent = readdir(d))) {
            if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
            char child[4096];
            if (rel && rel[0])
                snprintf(child, sizeof(child), "%s/%s", rel, ent->d_name);
            else
                snprintf(child, sizeof(child), "%s", ent->d_name);
            collect_entries(base_root, child, dirs, files);
        }
        closedir(d);
    } else if (S_ISREG(st.st_mode)) {
        if (rel && rel[0]) sv_push(files, rel);
    }
}

/* mkdir -p implementation */
static void mkdir_p(const char* path) {
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') tmp[--len] = '\0';
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

int32_t aiml_compress_tar_gz(const char* src, const char* dst) {
    if (!src || !dst) return -1;

    struct stat src_stat;
    if (lstat(src, &src_stat) != 0) return -2;

    /* Derive base_root (parent dir of src) and arc_name (name in archive) */
    char base_root[4096];
    char arc_name[256];
    const char* slash = strrchr(src, '/');
    if (slash && slash[1] != '\0') {
        size_t plen = (size_t)(slash - src);
        memcpy(base_root, src, plen);
        base_root[plen] = '\0';
        snprintf(arc_name, sizeof(arc_name), "%s", slash + 1);
    } else {
        snprintf(base_root, sizeof(base_root), ".");
        snprintf(arc_name,  sizeof(arc_name),  "%s", src);
    }

    gzFile gz = gzopen(dst, "wb");
    if (!gz) return -3;

    char hdr[USTAR_BLOCK];
    char zero_block[USTAR_BLOCK];
    memset(zero_block, 0, USTAR_BLOCK);

    if (S_ISREG(src_stat.st_mode)) {
        fill_ustar_header(hdr, arc_name, src_stat.st_mode,
                          (uint64_t)src_stat.st_size, src_stat.st_mtime, '0');
        gzwrite(gz, hdr, USTAR_BLOCK);

        FILE* f = fopen(src, "rb");
        if (!f) { gzclose(gz); return -4; }
        char buf[65536];
        size_t total = 0, n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            gzwrite(gz, buf, (unsigned)n);
            total += n;
        }
        fclose(f);
        size_t pad = (USTAR_BLOCK - (total % USTAR_BLOCK)) % USTAR_BLOCK;
        if (pad > 0) gzwrite(gz, zero_block, (unsigned)pad);

    } else if (S_ISDIR(src_stat.st_mode)) {
        StrVec dirs = {}, files = {};
        collect_entries(base_root, arc_name, &dirs, &files);

        /* Root directory entry */
        char dname[260];
        snprintf(dname, sizeof(dname), "%s/", arc_name);
        fill_ustar_header(hdr, dname, src_stat.st_mode, 0, src_stat.st_mtime, '5');
        gzwrite(gz, hdr, USTAR_BLOCK);

        for (int i = 0; i < dirs.n; i++) {
            char full[4096];
            snprintf(full, sizeof(full), "%s/%s", base_root, dirs.items[i]);
            struct stat st; lstat(full, &st);
            snprintf(dname, sizeof(dname), "%s/", dirs.items[i]);
            fill_ustar_header(hdr, dname, st.st_mode, 0, st.st_mtime, '5');
            gzwrite(gz, hdr, USTAR_BLOCK);
        }

        for (int i = 0; i < files.n; i++) {
            char full[4096];
            snprintf(full, sizeof(full), "%s/%s", base_root, files.items[i]);
            struct stat st; lstat(full, &st);
            fill_ustar_header(hdr, files.items[i], st.st_mode,
                              (uint64_t)st.st_size, st.st_mtime, '0');
            gzwrite(gz, hdr, USTAR_BLOCK);
            FILE* f = fopen(full, "rb");
            if (!f) continue;
            char buf[65536];
            size_t total = 0, n;
            while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
                gzwrite(gz, buf, (unsigned)n);
                total += n;
            }
            fclose(f);
            size_t pad = (USTAR_BLOCK - (total % USTAR_BLOCK)) % USTAR_BLOCK;
            if (pad > 0) gzwrite(gz, zero_block, (unsigned)pad);
        }

        sv_free(&dirs);
        sv_free(&files);
    } else {
        gzclose(gz);
        return -5; /* unsupported type */
    }

    /* Two zero end-of-archive blocks */
    gzwrite(gz, zero_block, USTAR_BLOCK);
    gzwrite(gz, zero_block, USTAR_BLOCK);
    gzclose(gz);
    return 0;
}

int32_t aiml_compress_untar_gz(const char* src, const char* dst_dir) {
    if (!src || !dst_dir) return -1;

    gzFile gz = gzopen(src, "rb");
    if (!gz) return -2;

    char hdr[USTAR_BLOCK];
    int zero_count = 0;

    for (;;) {
        if (gzread(gz, hdr, USTAR_BLOCK) != USTAR_BLOCK) break;

        /* End-of-archive: two consecutive zero blocks */
        bool all_zero = true;
        for (int i = 0; i < USTAR_BLOCK; i++) {
            if (hdr[i]) { all_zero = false; break; }
        }
        if (all_zero) { if (++zero_count >= 2) break; continue; }
        zero_count = 0;

        /* Parse entry */
        char name[101] = {};
        memcpy(name, hdr, 100);
        uint64_t size = (uint64_t)strtoull(hdr + 124, nullptr, 8);
        char type = hdr[156];
        uint64_t data_blocks = (size + USTAR_BLOCK - 1) / USTAR_BLOCK;

        /* Sanitise path: strip leading /, ./ */
        const char* rel = name;
        while (*rel == '/') rel++;
        if (rel[0] == '.' && rel[1] == '/') rel += 2;

        char out_path[4096];
        snprintf(out_path, sizeof(out_path), "%s/%s", dst_dir, rel);

        if (type == '5') {
            mkdir_p(out_path);
        } else if (type == '0' || type == '\0' || type == ' ') {
            /* Create parent dirs */
            char parent[4096];
            snprintf(parent, sizeof(parent), "%s", out_path);
            char* sl = strrchr(parent, '/');
            if (sl) { *sl = '\0'; mkdir_p(parent); }

            FILE* f = fopen(out_path, "wb");
            uint64_t remaining = size;
            char buf[USTAR_BLOCK];
            for (uint64_t i = 0; i < data_blocks; i++) {
                if (gzread(gz, buf, USTAR_BLOCK) != USTAR_BLOCK) break;
                size_t wn = (remaining >= USTAR_BLOCK) ? USTAR_BLOCK : (size_t)remaining;
                if (f) fwrite(buf, 1, wn, f);
                remaining = (remaining > USTAR_BLOCK) ? remaining - USTAR_BLOCK : 0;
            }
            if (f) fclose(f);
        } else {
            /* Skip data for other entry types */
            char skip[USTAR_BLOCK];
            for (uint64_t i = 0; i < data_blocks; i++) gzread(gz, skip, USTAR_BLOCK);
        }
    }

    gzclose(gz);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * zip  (libzip)
 * ════════════════════════════════════════════════════════════════════════ */

static void add_dir_to_zip(zip_t* za, const char* base_root, const char* rel_dir) {
    char full[4096];
    if (rel_dir && rel_dir[0])
        snprintf(full, sizeof(full), "%s/%s", base_root, rel_dir);
    else
        snprintf(full, sizeof(full), "%s", base_root);

    DIR* d = opendir(full);
    if (!d) return;

    struct dirent* ent;
    while ((ent = readdir(d))) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;

        char child_rel[4096];
        if (rel_dir && rel_dir[0])
            snprintf(child_rel, sizeof(child_rel), "%s/%s", rel_dir, ent->d_name);
        else
            snprintf(child_rel, sizeof(child_rel), "%s", ent->d_name);

        char child_full[4096];
        snprintf(child_full, sizeof(child_full), "%s/%s", full, ent->d_name);

        struct stat st;
        if (lstat(child_full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            char dir_entry[4096];
            snprintf(dir_entry, sizeof(dir_entry), "%s/", child_rel);
            zip_dir_add(za, dir_entry, ZIP_FL_ENC_UTF_8);
            add_dir_to_zip(za, base_root, child_rel);
        } else if (S_ISREG(st.st_mode)) {
            zip_source_t* zs = zip_source_file(za, child_full, 0, -1);
            if (zs)
                zip_file_add(za, child_rel, zs, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE);
        }
    }
    closedir(d);
}

int32_t aiml_compress_zip(const char* src, const char* dst) {
    if (!src || !dst) return -1;

    struct stat src_stat;
    if (lstat(src, &src_stat) != 0) return -2;

    int err;
    zip_t* za = zip_open(dst, ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!za) return -3;

    if (S_ISREG(src_stat.st_mode)) {
        const char* base = strrchr(src, '/');
        const char* name = base ? base + 1 : src;
        zip_source_t* zs = zip_source_file(za, src, 0, -1);
        if (zs) zip_file_add(za, name, zs, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE);

    } else if (S_ISDIR(src_stat.st_mode)) {
        const char* slash    = strrchr(src, '/');
        const char* arc_base = (slash && slash[1] != '\0') ? slash + 1 : src;

        char base_root[4096];
        if (slash && slash[1] != '\0') {
            size_t plen = (size_t)(slash - src);
            memcpy(base_root, src, plen);
            base_root[plen] = '\0';
        } else {
            snprintf(base_root, sizeof(base_root), ".");
        }

        char dir_entry[512];
        snprintf(dir_entry, sizeof(dir_entry), "%s/", arc_base);
        zip_dir_add(za, dir_entry, ZIP_FL_ENC_UTF_8);
        add_dir_to_zip(za, base_root, arc_base);
    } else {
        zip_close(za);
        return -4;
    }

    if (zip_close(za) != 0) return -5;
    return 0;
}

int32_t aiml_compress_unzip(const char* src, const char* dst_dir) {
    if (!src || !dst_dir) return -1;

    int err;
    zip_t* za = zip_open(src, ZIP_RDONLY, &err);
    if (!za) return -2;

    zip_int64_t count = zip_get_num_entries(za, 0);
    for (zip_int64_t i = 0; i < count; i++) {
        zip_stat_t st;
        if (zip_stat_index(za, (zip_uint64_t)i, 0, &st) != 0) continue;
        if (!st.name) continue;

        /* Security: block path traversal */
        if (strstr(st.name, "..")) continue;

        char out_path[4096];
        snprintf(out_path, sizeof(out_path), "%s/%s", dst_dir, st.name);

        size_t name_len = strlen(st.name);
        if (name_len > 0 && st.name[name_len - 1] == '/') {
            mkdir_p(out_path);
        } else {
            /* Create parent directories */
            char parent[4096];
            snprintf(parent, sizeof(parent), "%s", out_path);
            char* sl = strrchr(parent, '/');
            if (sl) { *sl = '\0'; mkdir_p(parent); }

            zip_file_t* zf = zip_fopen_index(za, (zip_uint64_t)i, 0);
            if (!zf) continue;

            FILE* f = fopen(out_path, "wb");
            if (f) {
                char buf[65536];
                zip_int64_t n;
                while ((n = zip_fread(zf, buf, sizeof(buf))) > 0)
                    fwrite(buf, 1, (size_t)n, f);
                fclose(f);
            }
            zip_fclose(zf);
        }
    }

    zip_close(za);
    return 0;
}

/* ── Archive traversal ──────────────────────────────────────────────────── */

#include <stddef.h>  /* for offsetof */
static_assert(sizeof(AimlZipEntry) == 40, "AimlZipEntry size mismatch");
static_assert(sizeof(AimlTarEntry) == 24, "AimlTarEntry size mismatch");
static_assert(offsetof(AimlZipEntry, name)            == 0,  "");
static_assert(offsetof(AimlZipEntry, size)            == 8,  "");
static_assert(offsetof(AimlZipEntry, compressed_size) == 16, "");
static_assert(offsetof(AimlZipEntry, is_dir)          == 24, "");
static_assert(offsetof(AimlZipEntry, crc32)           == 28, "");
static_assert(offsetof(AimlZipEntry, method)          == 32, "");
static_assert(offsetof(AimlTarEntry, name)   == 0,  "");
static_assert(offsetof(AimlTarEntry, size)   == 8,  "");
static_assert(offsetof(AimlTarEntry, is_dir) == 16, "");
static_assert(offsetof(AimlTarEntry, mode)   == 20, "");

/* AimlListRaw mirrors AIML list struct: { i32 size, i32 capacity, void** data } */
struct AimlArchiveListRaw {
    int32_t  size;
    int32_t  capacity;
    void**   data;
};

extern "C" void aiml_compress_zip_entries_fill(const char* path, void* list_ptr) {
    if (!path || !list_ptr) return;
    AimlArchiveListRaw* list = static_cast<AimlArchiveListRaw*>(list_ptr);

    int err = 0;
    zip_t* za = zip_open(path, ZIP_RDONLY, &err);
    if (!za) return;

    int64_t n = zip_get_num_entries(za, 0);
    if (n <= 0) { zip_close(za); return; }

    /* Allocate pointer array */
    void** data = static_cast<void**>(malloc((size_t)n * sizeof(void*)));
    if (!data) { zip_close(za); return; }
    int32_t count = 0;

    for (int64_t i = 0; i < n; i++) {
        struct zip_stat st;
        zip_stat_init(&st);
        if (zip_stat_index(za, (zip_uint64_t)i, 0, &st) != 0) continue;

        AimlZipEntry* e = static_cast<AimlZipEntry*>(malloc(sizeof(AimlZipEntry)));
        if (!e) continue;

        size_t nlen = st.name ? strlen(st.name) : 0;
        e->name = aiml_str_dup(st.name ? st.name : "");   // spec/61 §7: owned rc string
        if (!e->name) { free(e); continue; }

        e->size            = (st.valid & ZIP_STAT_SIZE)      ? (int64_t)st.size      : 0;
        e->compressed_size = (st.valid & ZIP_STAT_COMP_SIZE) ? (int64_t)st.comp_size : 0;
        e->is_dir          = (nlen > 0 && e->name[nlen - 1] == '/') ? 1 : 0;
        e->crc32           = (st.valid & ZIP_STAT_CRC)       ? (int32_t)st.crc       : 0;
        e->method          = (st.valid & ZIP_STAT_COMP_METHOD) ? (int32_t)st.comp_method : 0;
        e->_pad            = 0;

        data[count++] = e;
    }
    zip_close(za);

    /* Replace list data */
    free(list->data);
    list->data     = data;
    list->size     = count;
    list->capacity = (int32_t)n;
}

extern "C" void aiml_compress_tar_entries_fill(const char* path, void* list_ptr) {
    if (!path || !list_ptr) return;
    AimlArchiveListRaw* list = static_cast<AimlArchiveListRaw*>(list_ptr);

    gzFile gz = gzopen(path, "rb");
    if (!gz) return;

    int   cap   = 16;
    void** data = static_cast<void**>(malloc((size_t)cap * sizeof(void*)));
    int   count = 0;
    char  hdr[USTAR_BLOCK];
    int   zero_count = 0;

    for (;;) {
        if (gzread(gz, hdr, USTAR_BLOCK) != USTAR_BLOCK) break;

        bool all_zero = true;
        for (int i = 0; i < USTAR_BLOCK; i++) { if (hdr[i]) { all_zero = false; break; } }
        if (all_zero) { if (++zero_count >= 2) break; continue; }
        zero_count = 0;

        char name[101] = {};
        memcpy(name, hdr, 100);
        uint64_t sz   = (uint64_t)strtoull(hdr + 124, nullptr, 8);
        uint32_t mode = (uint32_t)strtoull(hdr + 100, nullptr, 8);
        char     type = hdr[156];

        AimlTarEntry* e = static_cast<AimlTarEntry*>(malloc(sizeof(AimlTarEntry)));
        if (!e) break;
        e->name   = aiml_str_dup(name);   // spec/61 §7: owned rc string
        e->size   = (int64_t)sz;
        e->is_dir = (type == '5') ? 1 : 0;
        e->mode   = (int32_t)mode;

        if (count >= cap) {
            cap *= 2;
            void** nd = static_cast<void**>(realloc(data, (size_t)cap * sizeof(void*)));
            if (!nd) { aiml_str_decref(e->name); free(e); break; }
            data = nd;
        }
        data[count++] = e;

        /* Skip data blocks without reading content */
        uint64_t data_blocks = (sz + USTAR_BLOCK - 1) / USTAR_BLOCK;
        char skip[USTAR_BLOCK];
        for (uint64_t i = 0; i < data_blocks; i++) gzread(gz, skip, USTAR_BLOCK);
    }
    gzclose(gz);

    free(list->data);
    list->data     = data;
    list->size     = count;
    list->capacity = cap;
}
