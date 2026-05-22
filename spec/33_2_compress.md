# Chapter 33.2 — `compress` Namespace

> **Guiding principle:** Compression and decompression as a first-class namespace.
> All algorithms operate on `buffer<u8>`. Convenience overloads also accept `string`.
> Archive operations (tar.gz, zip) work on the filesystem with `path`.

---

## 1. Overview

| Format   | Compression           | Decompression             | Strength | Speed | External Dep |
|----------|-----------------------|---------------------------|----------|-------|--------------|
| gzip     | `compress.gzip`       | `compress.gunzip`         | good     | medium | zlib (standard) |
| deflate  | `compress.deflate`    | `compress.inflate`        | good     | medium | zlib (standard) |
| zlib     | `compress.zlib`       | `compress.zlib_decompress`| good     | medium | zlib (standard) |
| brotli   | `compress.brotli`     | `compress.brotli_decompress` | very good | slow | libbrotli |
| zstd     | `compress.zstd`       | `compress.zstd_decompress`| very good | fast  | libzstd |
| lz4      | `compress.lz4`        | `compress.lz4_decompress` | medium   | extremely fast | liblz4 |

Archives:

| Format   | Create                   | Extract                   | Dep |
|----------|--------------------------|---------------------------|-----|
| tar.gz   | `compress.tar_gz`        | `compress.untar_gz`       | zlib + POSIX tar |
| zip      | `compress.zip`           | `compress.unzip`          | libzip or minizip |

---

## 2. Signatures

```
compress.<algo>(data)                → buffer<u8>   Input: string or buffer<u8>
compress.<algo>_decompress(data)     → buffer<u8>   Input: buffer<u8>
compress.<algo>_decompress_str(data) → string       Decompressed result as UTF-8 string
compress.<algo>(data, level)         → buffer<u8>   Optional: compression level (1–22 depending on algo)

# Archive functions (filesystem)
compress.tar_gz(src, dst)            → i32          0 = ok; src: path (directory or file), dst: path (.tar.gz)
compress.untar_gz(src, dst_dir)      → i32          0 = ok; src: .tar.gz, dst_dir: target directory
compress.zip(src, dst)               → i32          0 = ok; src: path, dst: .zip
compress.unzip(src, dst_dir)         → i32          0 = ok
```

---

## 3. Examples

### 3.1 In-Memory Compression

```aiml
# Compress string → gzip buffer
- decl: [data, string, "Hello World! This is a test string for compression."]
- decl: [gz, buffer<u8>, { call: [compress.gzip, data] }]
- print: ["compressed size: ${gz.len}"]

# Decompress again
- decl: [back, string, { call: [compress.gunzip_str, gz] }]
- print: ["decompressed: ${back}"]
```

### 3.2 Buffer-to-Buffer (Binary Data)

```aiml
- decl: [raw, buffer<u8>]
# ... fill raw ...
- decl: [compressed, buffer<u8>, { call: [compress.zstd, raw] }]
- decl: [decompressed, buffer<u8>, { call: [compress.zstd_decompress, compressed] }]
```

### 3.3 Compression Level

```aiml
# zstd: level 1 (fast) to 22 (maximum)
- decl: [c, buffer<u8>, { call: [compress.zstd, data, 3] }]   # default: 3

# brotli: level 0 (fast) to 11 (maximum)
- decl: [c, buffer<u8>, { call: [compress.brotli, data, 6] }] # default: 6

# gzip/zlib: level 1–9
- decl: [c, buffer<u8>, { call: [compress.gzip, data, 9] }]   # default: 6

# lz4: no level (fixed algorithm)
- decl: [c, buffer<u8>, { call: [compress.lz4, data] }]
```

### 3.4 HTTP Content-Encoding (Server Middleware)

```aiml
- def:
    name: gzip_middleware
    args: [[req, http_request_handle], [resp, http_response_handle]]
    body:
      - decl: [accept, string, { call: [req.header, "Accept-Encoding"] }]
      # Only compress if client supports gzip
      - if: [{ contains: [accept, "gzip"] }, true]
        then:
          - decl: [body, string, { call: [resp.body] }]
          - decl: [gz, buffer<u8>, { call: [compress.gzip, body] }]
          - set: [resp.body_bytes, gz]
          - call: [resp.headers.put, "Content-Encoding", "gzip"]
          - call: [resp.headers.put, "Vary", "Accept-Encoding"]
```

### 3.5 LZ4 for IPC / Database Blocks

```aiml
# Very fast compression for data between threads
- def:
    name: serialize_and_compress
    args: [[data, buffer<u8>]]
    return: buffer<u8>
    body:
      - return: { call: [compress.lz4, data] }

- def:
    name: decompress_and_parse
    args: [[compressed, buffer<u8>]]
    return: buffer<u8>
    body:
      - return: { call: [compress.lz4_decompress, compressed] }
```

### 3.6 zstd for Network Protocols

```aiml
# High-performance compression for WebSocket frames
- def:
    name: ws_send_compressed
    args: [[ws, ws_handle], [payload, string]]
    body:
      - decl: [compressed, buffer<u8>, { call: [compress.zstd, payload, 1] }]
      - call: [ws.send_binary, compressed]
```

### 3.7 Creating and Extracting tar.gz

```aiml
# Archive a directory
- decl: [src, path, "/var/data/backup"]
- decl: [dst, path, "/var/archives/backup_2024.tar.gz"]
- decl: [rc, i32, { call: [compress.tar_gz, src, dst] }]
- print: ["tar.gz rc: ${rc}"]

# Extract archive
- decl: [out_dir, path, "/var/restore"]
- decl: [rc2, i32, { call: [compress.untar_gz, dst, out_dir] }]
- print: ["untar rc: ${rc2}"]
```

### 3.8 zip

```aiml
# Create zip archive
- decl: [src, path, "/var/www/dist"]
- decl: [dst, path, "/var/releases/app_v1.0.zip"]
- decl: [rc, i32, { call: [compress.zip, src, dst] }]

# Extract
- decl: [target, path, "/var/deploy"]
- decl: [rc2, i32, { call: [compress.unzip, dst, target] }]
```

### 3.9 Combination with `hash` — Checksum After Compression

```aiml
- decl: [data, string, "sensitive payload"]
- decl: [compressed, buffer<u8>, { call: [compress.zstd, data] }]
- decl: [checksum, string, { call: [hash.sha256, compressed] }]
- print: ["compressed sha256: ${checksum}"]
```

---

## 4. Namespace Reference

### In-Memory Functions

| Call | Input | Return | Description |
|------|-------|--------|-------------|
| `{ call: [compress.gzip, data] }` | `string\|buffer<u8>` | `buffer<u8>` | gzip compress |
| `{ call: [compress.gzip, data, level] }` | `string\|buffer<u8>, i32` | `buffer<u8>` | gzip with level 1–9 |
| `{ call: [compress.gunzip, data] }` | `buffer<u8>` | `buffer<u8>` | gzip decompress |
| `{ call: [compress.gunzip_str, data] }` | `buffer<u8>` | `string` | gzip → UTF-8 string |
| `{ call: [compress.deflate, data] }` | `string\|buffer<u8>` | `buffer<u8>` | raw deflate |
| `{ call: [compress.inflate, data] }` | `buffer<u8>` | `buffer<u8>` | raw deflate decompress |
| `{ call: [compress.zlib, data] }` | `string\|buffer<u8>` | `buffer<u8>` | zlib-wrapped deflate |
| `{ call: [compress.zlib_decompress, data] }` | `buffer<u8>` | `buffer<u8>` | zlib decompress |
| `{ call: [compress.brotli, data] }` | `string\|buffer<u8>` | `buffer<u8>` | brotli compress |
| `{ call: [compress.brotli, data, level] }` | `string\|buffer<u8>, i32` | `buffer<u8>` | brotli with level 0–11 |
| `{ call: [compress.brotli_decompress, data] }` | `buffer<u8>` | `buffer<u8>` | brotli decompress |
| `{ call: [compress.brotli_decompress_str, data] }` | `buffer<u8>` | `string` | brotli → UTF-8 string |
| `{ call: [compress.zstd, data] }` | `string\|buffer<u8>` | `buffer<u8>` | zstd compress |
| `{ call: [compress.zstd, data, level] }` | `string\|buffer<u8>, i32` | `buffer<u8>` | zstd with level 1–22 |
| `{ call: [compress.zstd_decompress, data] }` | `buffer<u8>` | `buffer<u8>` | zstd decompress |
| `{ call: [compress.zstd_decompress_str, data] }` | `buffer<u8>` | `string` | zstd → UTF-8 string |
| `{ call: [compress.lz4, data] }` | `string\|buffer<u8>` | `buffer<u8>` | lz4 compress |
| `{ call: [compress.lz4_decompress, data] }` | `buffer<u8>` | `buffer<u8>` | lz4 decompress |
| `{ call: [compress.lz4_decompress_str, data] }` | `buffer<u8>` | `string` | lz4 → UTF-8 string |

### Archive Functions

| Call | Arguments | Return | Description |
|------|-----------|--------|-------------|
| `{ call: [compress.tar_gz, src, dst] }` | `path, path` | `i32` | Creates tar.gz archive from file/directory |
| `{ call: [compress.untar_gz, src, dst_dir] }` | `path, path` | `i32` | Extracts tar.gz to target directory |
| `{ call: [compress.zip, src, dst] }` | `path, path` | `i32` | Creates zip archive from file/directory |
| `{ call: [compress.unzip, src, dst_dir] }` | `path, path` | `i32` | Extracts zip to target directory |

Return `i32`: `0` = ok, negative value = error code.

---

## 5. Implementation Notes

### 5.1 Dependencies

| Format | Library | Package (Debian/Ubuntu) |
|--------|---------|------------------------|
| gzip, deflate, zlib | zlib | `zlib1g-dev` (usually pre-installed) |
| brotli | libbrotli | `libbrotli-dev` |
| zstd | libzstd | `libzstd-dev` |
| lz4 | liblz4 | `liblz4-dev` |
| tar.gz | zlib + POSIX `<archive.h>` or handrolled POSIX tar | `libarchive-dev` (optional) |
| zip | libzip or minizip (bundled with zlib) | `libzip-dev` |

`exodus.json` extension:
```json
{
  "libraries": ["ssl", "crypto", "z", "brotlienc", "brotlidec", "zstd", "lz4", "zip"]
}
```

### 5.2 C Interface

```c
// In-Memory — src/sys/compress_runtime.h
AimlBuffer* aiml_compress_gzip(const char* data, int32_t len, int level);
AimlBuffer* aiml_compress_gunzip(AimlBuffer* in);
char*        aiml_compress_gunzip_str(AimlBuffer* in);

AimlBuffer* aiml_compress_zstd(const char* data, int32_t len, int level);
AimlBuffer* aiml_compress_zstd_decompress(AimlBuffer* in);

AimlBuffer* aiml_compress_lz4(const char* data, int32_t len);
AimlBuffer* aiml_compress_lz4_decompress(AimlBuffer* in);

AimlBuffer* aiml_compress_brotli(const char* data, int32_t len, int level);
AimlBuffer* aiml_compress_brotli_decompress(AimlBuffer* in);

// Archive — filesystem operations
int32_t aiml_compress_tar_gz(const char* src, const char* dst);
int32_t aiml_compress_untar_gz(const char* src, const char* dst_dir);
int32_t aiml_compress_zip(const char* src, const char* dst);
int32_t aiml_compress_unzip(const char* src, const char* dst_dir);
```

String input: `aiml_compress_gzip(str, strlen(str), level)` — compiler dispatches based on argument type.
Buffer input: `aiml_compress_gzip(buf->data, buf->len, level)`.

### 5.3 Error Handling

In-memory functions: return a `null` buffer on error (corrupt input, out of memory).
Archive functions: return `i32 < 0`. Error codes are thrown as AIML exceptions when
the program uses exception handling:

```aiml
- try:
    body:
      - decl: [rc, i32, { call: [compress.untar_gz, src, dst_dir] }]
      - if: [{ <: [rc, 0] }, true]
        then:
          - throw: "untar failed: ${rc}"
  catch: [e]
    body:
      - print: ["Error: ${e.message}"]
```

### 5.4 Archive Traversal (since v1.1)

Archive entries can be inspected without extracting their contents.

```aiml
# List ZIP entries
- decl: [entries, list<ptr<zip_entry>>]
- set: [entries, { call: [compress.zip_entries, archive_path] }]
- decl: [e, ptr<zip_entry>]
- set: [e, entries-0]
- print: ["name: ${e.name}, size: ${e.size}, method: ${e.method}"]

# List tar.gz entries
- decl: [tents, list<ptr<tar_entry>>]
- set: [tents, { call: [compress.tar_entries, tgz_path] }]
- decl: [te, ptr<tar_entry>]
- set: [te, tents-0]
- print: ["name: ${te.name}, size: ${te.size}, is_dir: ${te.is_dir}"]
```

**`zip_entry` fields:**

| Field              | Type    | Description |
|--------------------|---------|-------------|
| `name`             | `string`| Entry path within the archive |
| `size`             | `i64`   | Uncompressed size in bytes |
| `compressed_size`  | `i64`   | Compressed size in bytes |
| `is_dir`           | `i32`   | 1 if directory, 0 if file |
| `crc32`            | `i32`   | CRC-32 checksum |
| `method`           | `i32`   | Compression method (0=stored, 8=deflated) |

**`tar_entry` fields:**

| Field     | Type    | Description |
|-----------|---------|-------------|
| `name`    | `string`| Entry path within the archive |
| `size`    | `i64`   | File size in bytes |
| `is_dir`  | `i32`   | 1 if directory, 0 if file |
| `mode`    | `i32`   | Unix file mode (octal) |

**New namespace functions:**

| Call | Arguments | Return | Description |
|------|-----------|--------|-------------|
| `{ call: [compress.zip_entries, path] }` | `path` | `list<ptr<zip_entry>>` | All entries of a ZIP archive |
| `{ call: [compress.tar_entries, path] }` | `path` | `list<ptr<tar_entry>>` | All entries of a tar.gz archive |

**Note:** The returned list contains `ptr<zip_entry>` / `ptr<tar_entry>` — pointers to heap-allocated entry structs (analogous to `ptr<VecResult>` in AionDB).

### 5.5 Streaming (v2)

In v1: all operations work entirely in-memory (no streaming).
For very large files: archive functions (`tar_gz`, `zip`) work in blocks internally —
the caller does not need to hold the entire file in the heap.

v2 plan: `compress_stream_handle` for incremental compression/decompression
(analogous to `resp.write` in the HTTP server).
