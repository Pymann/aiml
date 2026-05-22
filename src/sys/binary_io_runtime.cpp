#include "binary_io_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ── open / close ─────────────────────────────────────────────────────────── */

extern "C" void* aiml_io_open_binary(const char* path) {
    return fopen(path, "rb");
}

extern "C" void aiml_io_close(void* f) {
    if (f) fclose((FILE*)f);
}

/* ── position control ─────────────────────────────────────────────────────── */

extern "C" int64_t aiml_io_tell(void* f) {
    if (!f) return -1;
    return (int64_t)ftell((FILE*)f);
}

extern "C" void aiml_io_seek(void* f, int64_t pos) {
    if (f) fseek((FILE*)f, (long)pos, SEEK_SET);
}

extern "C" void aiml_io_skip(void* f, int64_t delta) {
    if (f) fseek((FILE*)f, (long)delta, SEEK_CUR);
}

extern "C" int32_t aiml_io_eof(void* f) {
    if (!f) return 1;
    return feof((FILE*)f) ? 1 : 0;
}

/* ── scalar little-endian reads ──────────────────────────────────────────── */
/* x86-64 is little-endian — direct fread is sufficient.                      */

extern "C" int32_t aiml_io_read_u8(void* f) {
    if (!f) return 0;
    uint8_t v = 0;
    fread(&v, 1, 1, (FILE*)f);
    return (int32_t)v;
}

extern "C" int32_t aiml_io_read_bool(void* f) {
    return aiml_io_read_u8(f) != 0 ? 1 : 0;
}

extern "C" int32_t aiml_io_read_i32_le(void* f) {
    if (!f) return 0;
    int32_t v = 0;
    fread(&v, 4, 1, (FILE*)f);
    return v;
}

extern "C" int64_t aiml_io_read_u32_le(void* f) {
    if (!f) return 0;
    uint32_t v = 0;
    fread(&v, 4, 1, (FILE*)f);
    return (int64_t)v;
}

extern "C" int64_t aiml_io_read_i64_le(void* f) {
    if (!f) return 0;
    int64_t v = 0;
    fread(&v, 8, 1, (FILE*)f);
    return v;
}

extern "C" float aiml_io_read_f32_le(void* f) {
    if (!f) return 0.0f;
    float v = 0.0f;
    fread(&v, 4, 1, (FILE*)f);
    return v;
}

extern "C" double aiml_io_read_f64_le(void* f) {
    if (!f) return 0.0;
    double v = 0.0;
    fread(&v, 8, 1, (FILE*)f);
    return v;
}

/* ── string reads ─────────────────────────────────────────────────────────── */

/* Read null-terminated string from current position; returns strdup'd copy. */
extern "C" char* aiml_io_read_cstring(void* f) {
    if (!f) return strdup("");
    char buf[4096];
    int len = 0;
    int c;
    while (len < (int)sizeof(buf) - 1 && (c = fgetc((FILE*)f)) != EOF && c != '\0') {
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    return strdup(buf);
}

/* ── helpers for AimlBinBuffer ─────────────────────────────────────────────── */

static AimlBinBuffer* aiml_binbuf_alloc(int32_t n) {
    AimlBinBuffer* b = (AimlBinBuffer*)malloc(sizeof(AimlBinBuffer));
    if (!b) return nullptr;
    b->data = (uint8_t*)malloc((size_t)(n > 0 ? n : 1));
    if (!b->data) { free(b); return nullptr; }
    b->size      = 0;
    b->capacity  = n;
    b->read_idx  = 0;
    b->write_idx = 0;
    return b;
}

extern "C" void aiml_io_buf_free(AimlBinBuffer* buf) {
    if (!buf) return;
    free(buf->data);
    free(buf);
}

/* ── bulk reads ───────────────────────────────────────────────────────────── */

extern "C" AimlBinBuffer* aiml_io_read_bytes(void* f, int32_t n) {
    AimlBinBuffer* b = aiml_binbuf_alloc(n);
    if (!b) return nullptr;
    if (f && n > 0) {
        size_t got = fread(b->data, 1, (size_t)n, (FILE*)f);
        b->size      = (int32_t)got;
        b->write_idx = (int32_t)got;
    }
    return b;
}

/* AimlListRaw layout: { int32_t size, int32_t capacity, void** data } */
typedef struct { int32_t size; int32_t capacity; void* data; } AimlListRaw;

/*
 * Fill an AIML list<f32> alloca with n floats read from f.
 * data is a heap buffer of float; realloc'd if capacity < n.
 */
extern "C" void aiml_io_read_f32_array_fill(void* f, int32_t n, void* list_ptr) {
    if (!list_ptr || n <= 0) return;
    AimlListRaw* list = (AimlListRaw*)list_ptr;
    /* Ensure capacity (data was malloc'd by initList, may be too small) */
    if (list->capacity < n) {
        list->data     = realloc(list->data, (size_t)n * sizeof(float));
        list->capacity = n;
    }
    if (f && list->data) {
        size_t got = fread(list->data, sizeof(float), (size_t)n, (FILE*)f);
        list->size = (int32_t)got;
    }
}

/* Parallel-friendly: uses pread-style seek+read without touching shared cursor. */
extern "C" AimlBinBuffer* aiml_io_read_at(void* f, int64_t offset, int32_t n) {
    AimlBinBuffer* b = aiml_binbuf_alloc(n);
    if (!b) return nullptr;
    if (f && n > 0) {
        int64_t saved = (int64_t)ftell((FILE*)f);
        fseek((FILE*)f, (long)offset, SEEK_SET);
        size_t got = fread(b->data, 1, (size_t)n, (FILE*)f);
        fseek((FILE*)f, (long)saved, SEEK_SET);
        b->size      = (int32_t)got;
        b->write_idx = (int32_t)got;
    }
    return b;
}
