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
| tar, tar.gz, tar.xz, tar.zst | — | `compress.untar` (§3.7a) | zlib, liblzma, libzstd |
| ar (`.deb`) | — | `compress.unar` (§3.7a) | — |
| zip      | `compress.zip`           | `compress.unzip`          | libzip or minizip |
| ZIM (Kiwix, read-only) | — | `compress.zim_open` … (§6) | libzim ≥ 8 |

---

## 2. Signatures

```
compress.<algo>(data)                → buffer<u8>   Input: string or buffer<u8>
compress.<algo>_decompress(data)     → buffer<u8>   Input: buffer<u8>
compress.<algo>_decompress_str(data) → string       Decompressed result as UTF-8 string
compress.<algo>(data, level)         → buffer<u8>   Optional: compression level (1–22 depending on algo)

# Archive functions (filesystem)
compress.tar_gz(src, dst)            → i32          0 = ok; src: path (directory or file), dst: path (.tar.gz)
compress.untar_gz(src, dst_dir)      → i32          0 = ok; the same as compress.untar
compress.untar(src, dst_dir)         → i32          0 = ok; any tar, plain or gz/xz/zstd (§3.7a)
compress.unar(src, dst_dir)          → i32          0 = ok; members of an ar archive (§3.7a)
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
          - call: [resp.headers.set, "Content-Encoding", "gzip"]
          - call: [resp.headers.set, "Vary", "Accept-Encoding"]
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

### 3.7a Extracting any tar, and ar archives (F33-2-10 … F33-2-16)

`compress.untar(src, dst_dir)` reads a tar archive — uncompressed, gzip, xz or zstd,
recognised by the first bytes, not by the file name — and writes it below `dst_dir`
(created if missing).  It is what unpacks a Debian package's `data.tar.xz`:

```aiml
- decl: [rc, i32, { call: [compress.unar, "zlib1g_1.3.deb", "/tmp/deb"] }]   # debian-binary, control.tar.zst, data.tar.xz
- decl: [rc2, i32, { call: [compress.untar, "/tmp/deb/data.tar.xz", "/tmp/root"] }]
```

- **Formats (F33-2-10):** ustar (with the 155-byte name prefix), GNU long names and
  long link targets (`L` / `K`), pax headers (`x`; `path`, `linkpath`, `size`; global
  `g` headers are skipped), base-256 sizes.
- **Entries (F33-2-11):** files, directories, symbolic links, hard links (to an entry
  already extracted).  Character/block devices and FIFOs are skipped (they need root and
  mean nothing in a build tree).
- **Metadata (F33-2-12):** permission bits (`0777`, no setuid/setgid) and the
  modification time are restored; owners are not.
- **Safety (F33-2-13):** an entry whose path is absolute, contains `..` that leaves
  `dst_dir`, or passes through a symbolic link is refused; so is a hard link to outside.
  A refused entry stops the extraction with `-4` — an archive that tries is not trusted
  for its other entries either.
- **Result (F33-2-14):** `0` ok; `-1` bad arguments, `-2` the archive cannot be opened,
  `-3` corrupt / truncated / unsupported compression, `-4` an unsafe entry, `-5` a file
  could not be written.
- `compress.untar_gz` is the same function (kept for existing code).

`compress.unar(src, dst_dir)` extracts the members of a `!<arch>` archive (the `.deb`
container): GNU and BSD name forms, each member as a plain file directly in `dst_dir`;
a member name with `/` or `..` is refused (`-4`).  Same result codes (F33-2-15).

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

---

## 6. ZIM Archives (read-only, F33-2-20 … F33-2-34)

ZIM is the archive format of Kiwix/openZIM (offline Wikipedia and similar). A ZIM file holds
*entries* (path, title, either a redirect or an item with mimetype and content) whose contents
live in compressed *clusters* (zstd or xz). AIML reads ZIM files; it does not write them.

**Status:** implemented against libzim 8.2.1 (`tests/compress_zim/`). Reference measurement of
the access pattern (`cai:tools/zim_strom.cpp`) on `wikipedia_de_all_mini_2026-10.zim`
(5 051 687 entries): ~2 000 articles/s streamed in cluster order, against ~14 ms per article
(≈ 70/s) when the same articles are read in path order. That difference is why §6.3 exists.

### 6.1 Opening and metadata

```aiml
- decl: [zim, zim_handle, { call: [compress.zim_open, "/data/wikipedia_de_all_nopic_2026-10.zim"] }]
- print: ["${ { call: [zim.entry_count] } } entries, ${ { call: [zim.article_count] } } articles"]
- decl: [lang, string, { call: [zim.metadata, "Language"] }]    # "deu"; "" if the key is missing
- call: [zim.close]
```

| Call | Return | Description |
|------|--------|-------------|
| `compress.zim_open(path)` | `zim_handle` | Opens the file read-only. Throws `ZimError` if it cannot be opened or is not a ZIM file (F33-2-20). |
| `zim.entry_count()` | `i64` | All entries, including the metadata and internal ones (`getAllEntryCount`). |
| `zim.scan_count()` | `i64` | The user entries — items and redirects — which is how far `zim.scan` reaches (§6.3). |
| `zim.article_count()` | `i64` | Front articles only (`getArticleCount`). |
| `zim.uuid()` | `string` | 32 hex digits. |
| `zim.metadata(key)` | `string` | Metadata value (`Title`, `Language`, `Date`, …); `""` if absent. |
| `zim.checksum_ok()` | `bool` | Verifies the stored MD5 over the whole file. Reads every byte: minutes for large files (F33-2-21). |
| `zim.close()` | void | Releases the handle; also on scope end (spec/61). |

### 6.2 Entries by path or title

```aiml
- decl: [entry, zim_entry, { call: [zim.by_path, "Kupfer"] }]
- if: [{ eq: [entry.found, true] }, true]
  then:
    - if: [{ eq: [entry.is_redirect, true] }, true]
      then:
        - set: [entry, { call: [zim.resolve, entry] }]      # follows redirects (max. 16 hops)
    - decl: [html, string, { call: [zim.content_str, entry] }]
    - print: ["${ { call: [zim.path, entry] } }: ${ { call: [zim.mimetype, entry] } }"]
```

`zim_entry` is a value model that holds **only scalars** — an index into the open archive and
what can be answered without touching it:

| Field | Type | Description |
|-------|------|-------------|
| `found` | `bool` | `false` for a lookup that matched nothing; all other fields are then `0` |
| `index` | `i64` | Entry index in path order (stable for a given file) |
| `is_redirect` | `bool` | Redirect entry |
| `size` | `i64` | Uncompressed content size in bytes; `0` for redirects |
| `position` | `i64` | Cluster-order position, or `-1` unless it came from `zim.scan` (§6.3) |

That is what makes it a value: with no string of its own there is nothing to own, so an entry
copies freely and a `list<zim_entry>` of a million rows cannot leak. The **strings are read
through the handle**, because only the open archive can answer them:

| Call | Return | Description |
|------|--------|-------------|
| `zim.path(entry)` | `string` | Path without namespace prefix in new-scheme files (`Kupfer`), as stored in old ones (`A/Kupfer`) |
| `zim.title(entry)` | `string` | Title (`Kupfer`) |
| `zim.mimetype(entry)` | `string` | `text/html`, `image/webp`, …; `""` for a redirect |
| `zim.redirect_path(entry)` | `string` | Target path of a redirect; `""` otherwise |

Each returns a fresh owned string; for an entry that is not `found` they all return `""`.

| Call | Return | Description |
|------|--------|-------------|
| `zim.by_path(path)` | `zim_entry` | Exact path. Tries the path as given; in old-scheme files also with `A/` (F33-2-22). |
| `zim.by_title(title)` | `zim_entry` | Exact title (`getEntryByTitle`). |
| `zim.resolve(entry)` | `zim_entry` | Follows redirects. A loop or more than 16 hops throws `ZimError` (F33-2-23). |
| `zim.content(entry)` | `buffer<u8>` | Content of a non-redirect entry. Throws `ZimError` for a redirect or a missing entry (F33-2-24). |
| `zim.content_str(entry)` | `string` | Same, as UTF-8 string (invalid bytes are kept as they are; no validation). |

### 6.3 Scanning in cluster order

Reading every entry by path decompresses, for most articles, a cluster that the previous article
did not use. `zim.scan` hands out entries in *cluster order* (libzim `iterEfficient`), so every
cluster is decompressed at most once for a full pass. Positions in this order are numbered
from `0` and are stable for a given file; they are what a resumable reader stores.

```aiml
- decl: [pos, i64, 0]
- decl: [total, i64, { call: [zim.scan_count] }]
- while: [{ lt: [pos, total] }]
  body:
    - decl: [batch, list<zim_entry>, { call: [zim.scan, pos, 1000] }]   # up to 1000 entries from position pos
    - decl: [row, i32, 0]
    - while: [{ lt: [row, batch.len] }]
      body:
        - decl: [item, zim_entry, { at: [batch, row] }]
        - decl: [kind, string, { call: [zim.mimetype, item] }]
        - if: [{ and: [{ eq: [item.is_redirect, false] }, { call: [kind.starts_with, "text/html"] }] }, true]
          then:
            - decl: [html, string, { call: [zim.content_str, item] }]
            # … parse …
        - set: [row, { +: [row, 1] }]
    - set: [pos, { +: [pos, batch.len] }]
```

| Call | Return | Description |
|------|--------|-------------|
| `zim.scan(from, count)` | `list<zim_entry>` | Up to `count` entries starting at cluster-order position `from`; an empty list at or past the end (F33-2-25). |
| `zim.position(entry)` | `i64` | Cluster-order position of an entry returned by `scan`; `-1` for entries from `by_path`/`by_title`. |
| `compress.zim_cache(clusters)` | void | How many clusters the reader keeps decompressed (F33-2-26). |

- **How far a scan reaches:** positions run `0 … zim.scan_count() - 1`. That is **not**
  `zim.entry_count()`: the metadata (`Title`, `Language`, …) and the internal entries (the
  counter, the title index) are entries, but they are not in the cluster order. A loop bounded by
  `entry_count` would ask for positions that do not exist and get an empty list early.
- **Cost of `from` (F33-2-27):** any `from` costs the same. The cluster order is indexable
  (`getEntryByClusterOrder`), so a reader resuming at position 2 000 000 does not re-walk the
  file — nothing is skipped and no iterator is restarted. Only the clusters the entries actually
  live in are read.
- **Content inside a scan (F33-2-28):** `zim.content(item)` for an entry of the current batch
  uses the cluster the scan is positioned on. Fetching the contents of a batch in batch order
  therefore decompresses each cluster once.
- **Order (F33-2-29):** The cluster order is a property of the file and not an alphabetical
  order. Redirects come first in the files measured.
- **An unreadable entry (F33-2-25):** a single entry whose directory record will not read is
  left out of the batch rather than losing the batch, so `batch.len` may be shorter than
  `count` before the end. A resumable reader therefore advances by `batch.len`, as above.

### 6.4 Safety and errors

- **Untrusted content (F33-2-30):** ZIM contents are data. AIML never interprets them. Paths and
  titles are arbitrary UTF-8 and may contain `/`, `..`, tabs or newlines; code that builds file
  names from them must sanitise them. There is no extraction-to-filesystem function, so unlike
  `untar` there is no path-escape case.
- **Read-only (F33-2-31):** The file is opened read-only and is never modified or locked for
  other readers. Several handles to the same file are allowed. A handle may be used from one
  thread at a time; parallel readers each open their own handle.
- **Errors (F33-2-32):** Errors raise `ZimError`, an exception of spec/22, with `message` and
  `code`:

  | Code | Meaning |
  |------|---------|
  | 1 | cannot open |
  | 2 | not a ZIM file or unsupported version |
  | 3 | corrupt or truncated (cluster cannot be decompressed) |
  | 4 | redirect loop |
  | 5 | entry is a redirect (`content`) |
  | 6 | no such entry |

  A lookup that finds nothing is not an error (`found = false`).
- **Large files (F33-2-33):** Files above 4 GB are the normal case (German Wikipedia without
  pictures: 18.6 GB). Sizes, counts and positions are `i64`.
- **Cluster cache (F33-2-26):** `compress.zim_cache(clusters)` takes a **number of clusters**,
  not a number of bytes, and applies to archives opened **after** the call. libzim 8 has no
  setter for the cache — it reads the environment variable `ZIM_CLUSTERCACHE` while opening an
  archive, which is what this call sets. (The byte-sized `setClusterCacheMaxSize` arrived in
  libzim 9; Ubuntu 24.04 ships 8.2.1, so it is not reachable here.) A handle already open keeps
  the size it was opened with.
- **Dependency (F33-2-34):** libzim 8 (C++; Debian/Ubuntu package `libzim-dev`, 8.2.1 on
  Ubuntu 24.04). `exodus pkg install` fetches it into the cache — `exodus build` does not.

  **libaiml does not link libzim.** The ZIM code lives in a separate loadable module,
  `libaiml_zim.so`, which libaiml opens on first use of a `compress.zim_*` call.

  The reason is the link, not taste. libzim needs **libxapian** for its full-text search, which
  AIML never calls, and `DT_RUNPATH` is not searched for a dependency's own dependencies. With
  libzim linked into libaiml, every aiml program — ZIM or no ZIM — failed to link with
  `undefined reference to Xapian::…` unless it carried the libxapian paths itself. Measured on
  2026-11-03: `tests/import_norm`, `tests/bundle_socket` and the AOT configs all broke, and so
  would every project outside this repository. A module loaded on demand puts the cost only on
  programs that read a ZIM file, which is what "optional" has to mean.

  | | |
  |---|---|
  | Who links libzim | `libaiml_zim.so` only (≈ 50 KB), built by `exodus-zim.json` from `zimmod/` |
  | What libaiml holds | forwarders that `dlopen` the module on first use and call it through a table |
  | Where it is looked for | beside the calling object (`dladdr`), then `../libaiml_zim/`, then the ordinary loader search |
  | Without the module | every call raises `ZimError` naming what is missing; nothing else changes |
  | What a consumer links | `-laiml`, unchanged — no `-lzim`, no libxapian paths |

  The module exports **exactly one** symbol, `aiml_zim_module_api`, returning a table of
  function pointers matched by position. That is load-bearing: libaiml defines the same
  `aiml_zim_*` names as forwarders and the compiler binary is linked `-rdynamic`, so while the
  module's implementations were globally visible under those names the dynamic linker resolved
  the table back to the **forwarders** — `aiml_zim_open` called itself until the stack ran out.
  The module is therefore compiled `-fvisibility=hidden`, and a field in the table is only ever
  appended, never moved.

- **Tests:** `tests/compress_zim/run.sh` builds a small ZIM with the libzim **writer** (AIML
  reads ZIM files and does not write them, so the fixture has no aiml route) into `tests/data/`,
  then runs two legs: `test_compress_zim.aiml` under the JIT, and `aot_probe.aiml` compiled and
  linked against `libaiml` alone. Both legs matter because each uses a different copy of the
  forwarders, and the AOT one also shows that the link needs neither libzim nor libxapian.
  Covered: by_path, by_title, resolve, a redirect loop, a full scan with resume, content of a
  redirect (error 5), a second handle on the same file, opening something that is not a ZIM
  file, and 300 handles released by their scope drop alone.

