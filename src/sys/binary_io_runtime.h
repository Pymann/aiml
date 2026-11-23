#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AimlBinBuffer mirrors AimlCompressBuffer / AimlBuffer layout:
 *   { i32 size, i32 capacity, i32 read_idx, i32 write_idx, u8* data }
 * size = number of valid bytes written; data heap-allocated.
 */
typedef struct {
    int32_t  size;
    int32_t  capacity;
    int32_t  read_idx;
    int32_t  write_idx;
    uint8_t* data;
} AimlBinBuffer;

/* Open / close */
void*   aiml_io_open_binary(const char* path);   /* → FILE* (NULL on error) */
void    aiml_io_close(void* f);
int32_t aiml_buffer_write_to(const void* buf, void* f);
int32_t aiml_buffer_write_string(void* buf, const char* s);
void*   aiml_io_std_stream(int32_t fd);

/* Position control */
char*   aiml_io_read_line(void* f);   /* spec/23: one line without the line break, owned */
int64_t aiml_io_tell(void* f);
void    aiml_io_seek(void* f, int64_t pos);
void    aiml_io_skip(void* f, int64_t delta);
int32_t aiml_io_eof(void* f);

/* Scalar little-endian reads — advance cursor; return 0/0.0 on EOF */
int32_t aiml_io_read_u8(void* f);
int32_t aiml_io_read_bool(void* f);
int32_t aiml_io_read_i32_le(void* f);
int64_t aiml_io_read_u32_le(void* f);
int64_t aiml_io_read_i64_le(void* f);
float   aiml_io_read_f32_le(void* f);
double  aiml_io_read_f64_le(void* f);

/* String reads */
char*          aiml_io_read_cstring(void* f);  /* null-terminated; caller frees */

/* Bulk reads — return heap AimlBinBuffer*, caller must free via aiml_io_buf_free */
AimlBinBuffer* aiml_io_read_bytes(void* f, int32_t n);
AimlBinBuffer* aiml_io_read_at(void* f, int64_t offset, int32_t n); /* seek-safe parallel read */
void           aiml_io_buf_free(AimlBinBuffer* buf);

/* Bulk f32 array read — fills AIML list<f32> struct directly (AimlListRaw layout) */
void aiml_io_read_f32_array_fill(void* f, int32_t n, void* list_ptr);

#ifdef __cplusplus
}
#endif
