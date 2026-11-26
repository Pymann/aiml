# Buffer Type (Binary Data)

```yaml
# Declaration
- decl: [packet, buffer]
- call: [packet.alloc, 1024] # Allocate 1KB

# Unified Write Method
# Syntax: buffer.write(value, type)
# - For primitives (i32, f64, etc.): writes raw bytes (Little Endian). The value is
#   converted to `type` first: `[buf.write, 200, u8]` stores one byte, `[buf.write, 2, f32]` a float.
# - For complex types (structs, lists, maps): recursively serializes content (Deep Copy).
#   (A simple memcpy would fail for types containing pointers/references)

- decl: [x, i32, 0x12345678]
- call: [packet.write, x, i32]

- decl: [pi, f64, 3.14159]
- call: [packet.write, pi, f64]

# Strings are written as [Length: u32] + [UTF-8 Bytes].
# Example: "Hi" -> [02 00 00 00] [48 69] (Little Endian)
- decl: [msg, string, "Hello"]
- call: [packet.write, msg, string]

# Buffer Metadata
# - packet.size     : Current number of bytes written (property, i32)
# - packet.capacity : Total allocated size (property, i32)
- print: [["Bytes written: ", packet.size]]
- print: [["Total capacity: ", packet.capacity]]

# Writing Structs
- enum:
    name: TypeId
    values:
      - Player: 1
      - Enemy: 2

- class:
    name: Header
    members:
      - [id, i32]
      - [type, TypeId]

- decl: [h, Header]
- set: [h.id, 1]
- set: [h.type, TypeId.Player]

# Writes sizeof(Header) bytes directly from &h
- call: [packet.write, h, Header]

# Reading
# Syntax: val = buffer.read(type)
# Reads sizeof(type) bytes and returns the value/struct.

- decl: [h2, Header]
- set: [h2, {call: [packet.read, Header]}]

- print: [["Read Header ID: ", h2.id]]
```

## Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `alloc` | `buffer.alloc(size: i32)` | Allocates `size` bytes (malloc). Must be called before any read/write. |
| `write` | `buffer.write(value, type)` | Writes `sizeof(type)` bytes at writeIndex, advances writeIndex. |
| `write_string` | `buffer.write_string(s: string) → i32` | Appends the string's bytes (up to its terminating `0x00`, so **not** usable for embedded NUL bytes) at writeIndex, advances writeIndex and raises `len` if it grew. Returns the byte count, `0` for an empty string, or `-1` if `s` is null or the bytes do not fit in the remaining capacity (no silent truncation — `alloc` enough first). |
| `read` | `buffer.read(type) → T` | Reads `sizeof(type)` bytes at readIndex, advances readIndex. |
| `seek` | `buffer.seek(pos: i32)` | Sets both readIndex and writeIndex to `pos` (byte offset). |
| `zero` | `buffer.zero()` | `memset(data, 0, capacity)` + sets size and writeIndex to capacity. |
| `len` | `buffer.len → i32` | Returns current size (number of **bytes** written). Property access, no `call:` needed. |
| `capacity` | `buffer.capacity → i32` | Total allocated size in bytes, as `alloc` set it. Property access. |
| `resize` | `buffer.resize(bytes: i32)` | Re-allocates to `bytes`, keeping the content that still fits. |
| `append` | `buffer.append(other: buffer)` | Copies `other`'s written bytes to this buffer's write position (also §Typed buffers). |
| `read_versioned` | `buffer.read_versioned(T [, version: i32]) → T` | Reads a `T` written by an older layout version (spec/37). |
| `ws_recv_append` | `buffer.ws_recv_append(conn: ws_handle) → i32` | Like `ws_recv`, but **appends** the frame instead of replacing the content. |
| `ws_send_range` | `buffer.ws_send_range(conn: ws_handle, offset: i32, len: i32) → i32` | Sends only `[offset, offset+len)` as one binary frame. |
| `encode_source` | `buffer.encode_source(dw, dh, mode: i32, px, py: f32) → i32` | Encodes the picked source image at `dw×dh` (see `pick_webp`) with an explicit focus point. |
| `write_to` | `buffer.write_to(f: file_handle) → i32` | Writes bytes `[0, len)` unchanged (binary-safe, `0x00` included) to `f` at its current position and flushes. Returns `len` on success, `-1` if `f` is null, the write is short, or the flush fails. Does not close `f`; the buffer is unchanged. |
| `ws_send` | `buffer.ws_send(conn: ws_handle) → i32` | Send the written bytes (`wIdx`) as one WebSocket binary frame. See chapter 32_4 §5.1. |
| `ws_recv` | `buffer.ws_recv(conn: ws_handle) → i32` | Receive the next WebSocket frame into this buffer (updates size/wIdx, resets rIdx). See chapter 32_4 §5.1. |
| `decode_image` | `buffer.decode_image(src: buffer) → i64` | Decode PNG/JPG/WebP bytes in `src` into this buffer as tightly-packed RGBA. Returns `(width << 32) | height`, or `0` on failure. Native only (WASM decodes in-browser). |
| `encode_webp` | `buffer.encode_webp(src: buffer, w: i32, h: i32, quality: i32) → i32` | Encode RGBA bytes in `src` (`w*h*4`) to WebP into this buffer. `quality < 0` = lossless, `0..100` = lossy. Returns encoded byte count, or `0`. Native only. |
| `encode_png` | `buffer.encode_png(src: buffer, w: i32, h: i32) → i32` | Encode RGBA bytes in `src` (`w*h*4`, rows top to bottom) as a lossless 8-bit RGBA PNG into this buffer (zlib deflate, no filtering). This buffer must be allocated with enough capacity; `w*h*5 + 1024` bytes always suffice. Returns the PNG byte count (`len` is set to it), or `0` on invalid sizes, a too small source or target, or a compression failure. Native only. |
| `resize_crop` | `buffer.resize_crop(src: buffer, sw: i32, sh: i32, dw: i32, dh: i32, mode: i32) → i32` | Resize RGBA in `src` (`sw×sh`) to EXACTLY `dw×dh` into this buffer. `mode 0` = cover-crop (aspect kept, overflowing axis centered-cropped), `mode 1` = stretch (fill exactly, may distort). Returns `dw*dh*4`, or `0`. Native only. |
| `pick_webp` | `buffer.pick_webp(dw: i32, dh: i32, mode: i32) → i32` | Open a file picker, decode the chosen image, resize/crop it to `dw×dh` (mode as `resize_crop`) and WebP-encode it into this buffer. Returns bytes written, or `0` (cancelled/error). Cross-target: native uses a zenity/kdialog dialog + stb/libwebp, WASM a browser `<input type=file>` + canvas `toBlob`. Needs a display (client path). |

## zero — Bulk Initialization

`buffer.zero()` zeroes the entire allocated region in a single `memset` call
and marks the buffer as fully written (size = capacity).

Typical use case: pre-allocated numeric arrays (embedding tables, weight matrices)
where 100k+ individual values would otherwise need to be initialized via a `write` loop.

```yaml
# Instead of 131072 individual write calls:
- decl: [emb, buffer]
- call: [emb.alloc, 524288]    # 4096 tokens * 32 dim * 4 bytes
- call: [emb.zero]              # one memset — ~0ms instead of seconds

# Afterwards: random access via seek + write/read
- call: [emb.seek, 640]         # byte offset = (token_id * dim + d) * 4
- call: [emb.write, 1.5, f32]
- call: [emb.seek, 640]
- decl: [val, f32, { call: [emb.read, f32] }]
```

## Buffer as a Typed Array (f32/i32)

Buffers are suitable as high-performance flat arrays for numeric data.
Access is done via computed byte offsets:

```yaml
# byte_offset = (index) * sizeof(type)
# For f32: byte_offset = index * 4
# For i32: byte_offset = index * 4

# 2D-Array Pattern (e.g. embedding table):
# byte_offset = (row * cols + col) * 4
- decl: [token_id, i32, 5]
- decl: [dim, i32, 32]
- decl: [d, i32, 3]
- decl: [off, i32, { "*": [{ "+": [{ "*": [token_id, dim] }, d] }, 4] }]
- call: [emb.seek, off]
- decl: [val, f32, { call: [emb.read, f32] }]
```

## Typed buffers — `buffer<T>`

A `buffer<T>` is the same bytes with an element type attached, and that type unlocks a
second, element-wise surface beside the byte-wise one above.

| Method | Signature | Description |
|--------|-----------|-------------|
| `push` | `buffer<T>.push(value)` | Appends **one element**, converted to `T` first (a double literal becomes an `f32` in a `buffer<f32>`). Advances the write index like `write`. |
| `get` | `buffer<T>.get(index: i32) -> T` | Reads the element at **element index** `index` — the byte offset is `index * sizeof(T)`. |
| `count` | `buffer<T>.count -> i32` | Number of **elements** written: `len / sizeof(T)`. For `buffer<u8>` it equals `len`. |
| `ptr` | `buffer<T>.ptr` | The raw data pointer, for handing the block to a native call. |
| `append` | `buffer<T>.append(other: buffer<T>)` | Copies all of `other`'s written bytes to this buffer's write position and advances size and write index. |
| `byte_len` | `buffer<T>.byte_len -> i32` | **The same value as `len`.** Kept because it says what it returns. |

### `len` counts BYTES, `get` counts ELEMENTS (normative)

This is the one thing about buffers worth remembering:

```yaml
- decl: [pts, buffer<f32>]
- call: [pts.alloc, 16]          # 16 BYTES = room for four f32
- call: [pts.push, 1.0]
- call: [pts.push, 2.0]
# pts.len     == 8   (bytes)
# pts.count   == 2   (elements)
# pts.get(1)  == 2.0 (element index, NOT byte 1)
```

`alloc` is in bytes for every buffer, typed or not. `len`, `size` and `byte_len` are in
bytes. `get`, `push` and `count` are in elements. A loop written as
`idx < buf.len` with `buf.get(idx)` therefore runs four times too far on a
`buffer<f32>` and reads past the data **without raising anything** — which is why `count`
exists and why this paragraph is here rather than in a footnote. The same confusion in C
made `ui.draw_polyline` (spec/44_3 §4.6) draw a quarter of its segments.

`byte_len` is an alias for `len`, not its counterpart: the name suggests an
element-counting `len`, and there is none. Use `count` for elements.

## Exception Behaviour

| Condition | Exception |
|-----------|-----------|
| `write()` when `size + sizeof(value) > capacity` | `BufferOverflowException` |
| `read()` when read position > written data size | `BufferUnderflowException` |

## Proposed Binary View Extensions (Not Implemented Yet)

The existing `buffer.read(type)` / `buffer.write(value, type)` API is useful for
sequential binary IO. For large model and embedding imports, additional random-access
binary view helpers are proposed.

Status: **proposed, not implemented**

```yaml
# Random-access little-endian reads from an existing byte buffer
- decl: [buf, buffer<u8>]

- decl: [b0, i32, { call: [buf.read_u8, 0] }]
- decl: [magic, i32, { call: [buf.read_i32_le, 0] }]
- decl: [rows, i64, { call: [buf.read_i64_le, 128] }]
- decl: [val, f32, { call: [buf.read_f32_le, 256] }]
- decl: [d, f64, { call: [buf.read_f64_le, 512] }]
- decl: [flag, i32, { call: [buf.read_bool, 1024] }]

# Slice without copying semantics are preferred, but not yet specified.
- decl: [hdr, buffer<u8>, { call: [buf.slice, 0, 64] }]

# Null-terminated string read from buffer contents
- decl: [word, string, { call: [buf.read_cstring, 2048] }]
```

### Proposed Methods

| Method | Signature | Status | Description |
|--------|-----------|--------|-------------|
| `read_u8` | `buffer.read_u8(offset: i32) -> i32` | not implemented | Reads one unsigned byte at `offset`. |
| `read_bool` | `buffer.read_bool(offset: i32) -> i32` | not implemented | Reads one stored boolean byte at `offset` and returns `0/1`. |
| `read_i32_le` | `buffer.read_i32_le(offset: i32) -> i32` | not implemented | Reads a 32-bit little-endian signed integer from `offset`. |
| `read_u32_le` | `buffer.read_u32_le(offset: i32) -> i64` | not implemented | Reads a 32-bit little-endian unsigned integer from `offset`. |
| `read_i64_le` | `buffer.read_i64_le(offset: i32) -> i64` | not implemented | Reads a 64-bit little-endian signed integer from `offset`. |
| `read_u64_le` | `buffer.read_u64_le(offset: i32) -> i64` | not implemented | Reads a 64-bit little-endian unsigned integer from `offset`. |
| `read_f32_le` | `buffer.read_f32_le(offset: i32) -> f32` | not implemented | Reads one IEEE754 `f32` from `offset`. |
| `read_f64_le` | `buffer.read_f64_le(offset: i32) -> f64` | not implemented | Reads one IEEE754 `f64` from `offset`. |
| `read_cstring` | `buffer.read_cstring(offset: i32) -> string` | not implemented | Reads bytes until `0x00` and decodes as string. |
| `slice` | `buffer.slice(offset: i32, len: i32) -> buffer<u8>` | not implemented | Returns a sub-view or copy of the byte range. |

### Motivation

These methods are intended for:

- model importers (fastText, GGUF, future custom formats)
- efficient binary header parsing
- section-based parsing where bytes are loaded once and then interpreted many times

The design goal is:

- `io` is responsible for obtaining bytes from files or streams
- `buffer` is responsible for interpreting bytes already in memory
