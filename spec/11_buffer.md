# Buffer Type (Binary Data)

```yaml
# Declaration
- decl: [packet, buffer]
- call: [packet.alloc, 1024] # Allocate 1KB

# Unified Write Method
# Syntax: buffer.write(value, type)
# - For primitives (i32, f64, etc.): writes raw bytes (Little Endian).
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
| `read` | `buffer.read(type) → T` | Reads `sizeof(type)` bytes at readIndex, advances readIndex. |
| `seek` | `buffer.seek(pos: i32)` | Sets both readIndex and writeIndex to `pos` (byte offset). |
| `zero` | `buffer.zero()` | `memset(data, 0, capacity)` + sets size and writeIndex to capacity. |
| `len` | `buffer.len → i32` | Returns current size (number of bytes written). Property access, no `call:` needed. |

## zero — Bulk-Initialisierung

`buffer.zero()` nullt den gesamten allokierten Bereich in einem einzigen `memset`-Call
und markiert den Buffer als vollständig beschrieben (size = capacity).

Typischer Einsatz: Pre-allokierte numerische Arrays (Embedding-Tables, Gewichtsmatrizen),
bei denen 100k+ Einzelwerte sonst per `write`-Schleife initialisiert werden müssten.

```yaml
# Statt 131072 einzelner write-Calls:
- decl: [emb, buffer]
- call: [emb.alloc, 524288]    # 4096 tokens * 32 dim * 4 bytes
- call: [emb.zero]              # ein memset — ~0ms statt Sekunden

# Danach: random access via seek + write/read
- call: [emb.seek, 640]         # byte offset = (token_id * dim + d) * 4
- call: [emb.write, 1.5, f32]
- call: [emb.seek, 640]
- decl: [val, f32, { call: [emb.read, f32] }]
```

## Buffer als typisiertes Array (f32/i32)

Buffer eignen sich als performante flat arrays für numerische Daten.
Der Zugriff erfolgt über berechnete Byte-Offsets:

```yaml
# byte_offset = (index) * sizeof(type)
# Für f32: byte_offset = index * 4
# Für i32: byte_offset = index * 4

# 2D-Array Pattern (z.B. Embedding-Table):
# byte_offset = (row * cols + col) * 4
- decl: [token_id, i32, 5]
- decl: [dim, i32, 32]
- decl: [d, i32, 3]
- decl: [off, i32, { "*": [{ "+": [{ "*": [token_id, dim] }, d] }, 4] }]
- call: [emb.seek, off]
- decl: [val, f32, { call: [emb.read, f32] }]
```

## Exception Behaviour

| Condition | Exception |
|-----------|-----------|
| `write()` when `size + sizeof(value) > capacity` | `BufferOverflowException` |
| `read()` when read position > written data size | `BufferUnderflowException` |
