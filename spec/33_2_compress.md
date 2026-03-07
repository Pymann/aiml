# Kapitel 33.2 — `compress`-Namespace

> **Leitgedanke:** Kompression und Dekompression als First-Class-Namespace.
> Alle Algorithmen arbeiten auf `buffer<u8>`. Convenience-Überladungen akzeptieren auch `string`.
> Archiv-Operationen (tar.gz, zip) arbeiten auf dem Dateisystem mit `path`.

---

## 1. Übersicht

| Format   | Kompression           | Dekompression             | Stärke | Geschwindigkeit | Externer Dep |
|----------|-----------------------|---------------------------|--------|-----------------|--------------|
| gzip     | `compress.gzip`       | `compress.gunzip`         | gut    | mittel          | zlib (Standard) |
| deflate  | `compress.deflate`    | `compress.inflate`        | gut    | mittel          | zlib (Standard) |
| zlib     | `compress.zlib`       | `compress.zlib_decompress`| gut    | mittel          | zlib (Standard) |
| brotli   | `compress.brotli`     | `compress.brotli_decompress` | sehr gut | langsam    | libbrotli |
| zstd     | `compress.zstd`       | `compress.zstd_decompress`| sehr gut | schnell       | libzstd |
| lz4      | `compress.lz4`        | `compress.lz4_decompress` | mittel | extrem schnell  | liblz4 |

Archive:

| Format   | Erstellen                | Entpacken                 | Dep |
|----------|--------------------------|---------------------------|-----|
| tar.gz   | `compress.tar_gz`        | `compress.untar_gz`       | zlib + POSIX tar |
| zip      | `compress.zip`           | `compress.unzip`          | libzip oder minizip |

---

## 2. Signaturen

```
compress.<algo>(data)                → buffer<u8>   Eingabe: string oder buffer<u8>
compress.<algo>_decompress(data)     → buffer<u8>   Eingabe: buffer<u8>
compress.<algo>_decompress_str(data) → string       Dekomprimiertes Ergebnis als UTF-8 String
compress.<algo>(data, level)         → buffer<u8>   Optional: Kompressionslevel (1–22 je nach Algo)

# Archiv-Funktionen (Dateisystem)
compress.tar_gz(src, dst)            → i32          0 = ok; src: path (Verzeichnis oder Datei), dst: path (.tar.gz)
compress.untar_gz(src, dst_dir)      → i32          0 = ok; src: .tar.gz, dst_dir: Zielverzeichnis
compress.zip(src, dst)               → i32          0 = ok; src: path, dst: .zip
compress.unzip(src, dst_dir)         → i32          0 = ok
```

---

## 3. Beispiele

### 3.1 In-Memory-Kompression

```aiml
# String komprimieren → gzip-Buffer
- decl: [data, string, "Hello World! This is a test string for compression."]
- decl: [gz, buffer<u8>, { call: [compress.gzip, data] }]
- print: ["compressed size: ${gz.len}"]

# Wieder dekomprimieren
- decl: [back, string, { call: [compress.gunzip_str, gz] }]
- print: ["decompressed: ${back}"]
```

### 3.2 Buffer-zu-Buffer (binäre Daten)

```aiml
- decl: [raw, buffer<u8>]
# ... fill raw ...
- decl: [compressed, buffer<u8>, { call: [compress.zstd, raw] }]
- decl: [decompressed, buffer<u8>, { call: [compress.zstd_decompress, compressed] }]
```

### 3.3 Kompressionslevel

```aiml
# zstd: Level 1 (schnell) bis 22 (maximal)
- decl: [c, buffer<u8>, { call: [compress.zstd, data, 3] }]   # default: 3

# brotli: Level 0 (schnell) bis 11 (maximal)
- decl: [c, buffer<u8>, { call: [compress.brotli, data, 6] }] # default: 6

# gzip/zlib: Level 1–9
- decl: [c, buffer<u8>, { call: [compress.gzip, data, 9] }]   # default: 6

# lz4: kein Level (fixed algorithm)
- decl: [c, buffer<u8>, { call: [compress.lz4, data] }]
```

### 3.4 HTTP Content-Encoding (Server-Middleware)

```aiml
- def:
    name: gzip_middleware
    args: [[req, http_request_handle], [resp, http_response_handle]]
    body:
      - decl: [accept, string, { call: [req.header, "Accept-Encoding"] }]
      # Nur komprimieren wenn Client gzip unterstützt
      - if: [{ contains: [accept, "gzip"] }, true]
        then:
          - decl: [body, string, { call: [resp.body] }]
          - decl: [gz, buffer<u8>, { call: [compress.gzip, body] }]
          - set: [resp.body_bytes, gz]
          - call: [resp.headers.put, "Content-Encoding", "gzip"]
          - call: [resp.headers.put, "Vary", "Accept-Encoding"]
```

### 3.5 LZ4 für IPC / Datenbank-Blöcke

```aiml
# Sehr schnelle Kompression für Daten zwischen Threads
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

### 3.6 zstd für Netzwerk-Protokolle

```aiml
# Hochperformante Kompression für Websocket-Frames
- def:
    name: ws_send_compressed
    args: [[ws, ws_handle], [payload, string]]
    body:
      - decl: [compressed, buffer<u8>, { call: [compress.zstd, payload, 1] }]
      - call: [ws.send_binary, compressed]
```

### 3.7 tar.gz erstellen und entpacken

```aiml
# Verzeichnis archivieren
- decl: [src, path, "/var/data/backup"]
- decl: [dst, path, "/var/archives/backup_2024.tar.gz"]
- decl: [rc, i32, { call: [compress.tar_gz, src, dst] }]
- print: ["tar.gz rc: ${rc}"]

# Archiv entpacken
- decl: [out_dir, path, "/var/restore"]
- decl: [rc2, i32, { call: [compress.untar_gz, dst, out_dir] }]
- print: ["untar rc: ${rc2}"]
```

### 3.8 zip

```aiml
# Zip-Archiv erstellen
- decl: [src, path, "/var/www/dist"]
- decl: [dst, path, "/var/releases/app_v1.0.zip"]
- decl: [rc, i32, { call: [compress.zip, src, dst] }]

# Entpacken
- decl: [target, path, "/var/deploy"]
- decl: [rc2, i32, { call: [compress.unzip, dst, target] }]
```

### 3.9 Kombination mit `hash` — Prüfsumme nach Kompression

```aiml
- decl: [data, string, "sensitive payload"]
- decl: [compressed, buffer<u8>, { call: [compress.zstd, data] }]
- decl: [checksum, string, { call: [hash.sha256, compressed] }]
- print: ["compressed sha256: ${checksum}"]
```

---

## 4. Namespace-Referenz

### In-Memory-Funktionen

| Aufruf | Eingabe | Rückgabe | Beschreibung |
|--------|---------|----------|--------------|
| `{ call: [compress.gzip, data] }` | `string\|buffer<u8>` | `buffer<u8>` | gzip komprimieren |
| `{ call: [compress.gzip, data, level] }` | `string\|buffer<u8>, i32` | `buffer<u8>` | gzip mit Level 1–9 |
| `{ call: [compress.gunzip, data] }` | `buffer<u8>` | `buffer<u8>` | gzip dekomprimieren |
| `{ call: [compress.gunzip_str, data] }` | `buffer<u8>` | `string` | gzip → UTF-8 String |
| `{ call: [compress.deflate, data] }` | `string\|buffer<u8>` | `buffer<u8>` | raw deflate |
| `{ call: [compress.inflate, data] }` | `buffer<u8>` | `buffer<u8>` | raw deflate dekomprimieren |
| `{ call: [compress.zlib, data] }` | `string\|buffer<u8>` | `buffer<u8>` | zlib-wrapped deflate |
| `{ call: [compress.zlib_decompress, data] }` | `buffer<u8>` | `buffer<u8>` | zlib dekomprimieren |
| `{ call: [compress.brotli, data] }` | `string\|buffer<u8>` | `buffer<u8>` | brotli komprimieren |
| `{ call: [compress.brotli, data, level] }` | `string\|buffer<u8>, i32` | `buffer<u8>` | brotli mit Level 0–11 |
| `{ call: [compress.brotli_decompress, data] }` | `buffer<u8>` | `buffer<u8>` | brotli dekomprimieren |
| `{ call: [compress.brotli_decompress_str, data] }` | `buffer<u8>` | `string` | brotli → UTF-8 String |
| `{ call: [compress.zstd, data] }` | `string\|buffer<u8>` | `buffer<u8>` | zstd komprimieren |
| `{ call: [compress.zstd, data, level] }` | `string\|buffer<u8>, i32` | `buffer<u8>` | zstd mit Level 1–22 |
| `{ call: [compress.zstd_decompress, data] }` | `buffer<u8>` | `buffer<u8>` | zstd dekomprimieren |
| `{ call: [compress.zstd_decompress_str, data] }` | `buffer<u8>` | `string` | zstd → UTF-8 String |
| `{ call: [compress.lz4, data] }` | `string\|buffer<u8>` | `buffer<u8>` | lz4 komprimieren |
| `{ call: [compress.lz4_decompress, data] }` | `buffer<u8>` | `buffer<u8>` | lz4 dekomprimieren |
| `{ call: [compress.lz4_decompress_str, data] }` | `buffer<u8>` | `string` | lz4 → UTF-8 String |

### Archiv-Funktionen

| Aufruf | Argumente | Rückgabe | Beschreibung |
|--------|-----------|----------|--------------|
| `{ call: [compress.tar_gz, src, dst] }` | `path, path` | `i32` | Erstellt tar.gz-Archiv aus Datei/Verzeichnis |
| `{ call: [compress.untar_gz, src, dst_dir] }` | `path, path` | `i32` | Entpackt tar.gz in Zielverzeichnis |
| `{ call: [compress.zip, src, dst] }` | `path, path` | `i32` | Erstellt zip-Archiv aus Datei/Verzeichnis |
| `{ call: [compress.unzip, src, dst_dir] }` | `path, path` | `i32` | Entpackt zip in Zielverzeichnis |

Rückgabe `i32`: `0` = ok, negativer Wert = Fehlercode.

---

## 5. Implementierungshinweise

### 5.1 Dependencies

| Format | Library | Paket (Debian/Ubuntu) |
|--------|---------|----------------------|
| gzip, deflate, zlib | zlib | `zlib1g-dev` (meist vorinstalliert) |
| brotli | libbrotli | `libbrotli-dev` |
| zstd | libzstd | `libzstd-dev` |
| lz4 | liblz4 | `liblz4-dev` |
| tar.gz | zlib + POSIX `<archive.h>` oder handrolled POSIX tar | `libarchive-dev` (optional) |
| zip | libzip oder minizip (zlib-mitgeliefert) | `libzip-dev` |

`exodus.json`-Erweiterung:
```json
{
  "libraries": ["ssl", "crypto", "z", "brotlienc", "brotlidec", "zstd", "lz4", "zip"]
}
```

### 5.2 C-Interface

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

// Archive — Dateisystem-Operationen
int32_t aiml_compress_tar_gz(const char* src, const char* dst);
int32_t aiml_compress_untar_gz(const char* src, const char* dst_dir);
int32_t aiml_compress_zip(const char* src, const char* dst);
int32_t aiml_compress_unzip(const char* src, const char* dst_dir);
```

String-Input: `aiml_compress_gzip(str, strlen(str), level)` — Compiler dispatcht nach Argtyp.
Buffer-Input: `aiml_compress_gzip(buf->data, buf->len, level)`.

### 5.3 Fehlerbehandlung

In-Memory-Funktionen: Geben `null`-Buffer zurück bei Fehler (korrupte Eingabe, Out-of-Memory).
Archiv-Funktionen: Geben `i32 < 0` zurück. Fehlercodes werden als AIML-Exception geworfen wenn
das Programm eine Exception-Behandlung nutzt:

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

### 5.4 Archive-Traversal (seit v1.1)

Archiv-Einträge können inspiziert werden ohne den Inhalt zu extrahieren.

```aiml
# ZIP-Einträge auflisten
- decl: [entries, list<ptr<zip_entry>>]
- set: [entries, { call: [compress.zip_entries, archive_path] }]
- decl: [e, ptr<zip_entry>]
- set: [e, entries-0]
- print: ["name: ${e.name}, size: ${e.size}, method: ${e.method}"]

# tar.gz-Einträge auflisten
- decl: [tents, list<ptr<tar_entry>>]
- set: [tents, { call: [compress.tar_entries, tgz_path] }]
- decl: [te, ptr<tar_entry>]
- set: [te, tents-0]
- print: ["name: ${te.name}, size: ${te.size}, is_dir: ${te.is_dir}"]
```

**`zip_entry`-Felder:**

| Feld               | Typ     | Beschreibung |
|--------------------|---------|--------------|
| `name`             | `string`| Eintragspfad im Archiv |
| `size`             | `i64`   | Unkomprimierte Größe in Bytes |
| `compressed_size`  | `i64`   | Komprimierte Größe in Bytes |
| `is_dir`           | `i32`   | 1 wenn Verzeichnis, 0 wenn Datei |
| `crc32`            | `i32`   | CRC-32-Prüfsumme |
| `method`           | `i32`   | Kompressionsmethode (0=stored, 8=deflated) |

**`tar_entry`-Felder:**

| Feld      | Typ     | Beschreibung |
|-----------|---------|--------------|
| `name`    | `string`| Eintragspfad im Archiv |
| `size`    | `i64`   | Dateigröße in Bytes |
| `is_dir`  | `i32`   | 1 wenn Verzeichnis, 0 wenn Datei |
| `mode`    | `i32`   | Unix-Dateimodus (Oktal) |

**Neue Namespace-Funktionen:**

| Aufruf | Argumente | Rückgabe | Beschreibung |
|--------|-----------|----------|--------------|
| `{ call: [compress.zip_entries, path] }` | `path` | `list<ptr<zip_entry>>` | Alle Einträge eines ZIP-Archivs |
| `{ call: [compress.tar_entries, path] }` | `path` | `list<ptr<tar_entry>>` | Alle Einträge eines tar.gz-Archivs |

**Hinweis:** Die zurückgegebene Liste enthält `ptr<zip_entry>` / `ptr<tar_entry>` — Pointer auf heap-allozierte Entry-Structs (analog zu `ptr<VecResult>` bei AionDB).

### 5.5 Streaming (v2)

In v1: alle Operationen arbeiten vollständig in-memory (kein Streaming).
Für sehr große Dateien: Archiv-Funktionen (`tar_gz`, `zip`) arbeiten blockweise intern —
der Aufrufer braucht nicht die gesamte Datei im Heap zu haben.

v2-Plan: `compress_stream_handle` für inkrementelle Kompression/Dekompression
(analog zu `resp.write` im HTTP-Server).
