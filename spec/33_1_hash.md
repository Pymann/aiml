# Kapitel 33.1 — `hash`-Namespace

> **Leitgedanke:** Kryptografische und nicht-kryptografische Hashfunktionen als First-Class-Namespace.
> Alle Funktionen liefern standardmäßig einen hex-codierten `string`.
> Für Chaining (z.B. Bitcoin HASH160) gibt es `_raw`-Varianten die `buffer<u8>` zurückgeben.
> Implementierung: OpenSSL (`libcrypto`, bereits via `tls` gelinkt) + handrolled Keccak-256.

---

## 1. Übersicht

| Algorithmus     | Funktion            | Output-Länge (hex) | Verwendung |
|-----------------|---------------------|--------------------|------------|
| SHA-256         | `hash.sha256`       | 64 Zeichen         | Allgemein, Bitcoin PoW |
| SHA-512         | `hash.sha512`       | 128 Zeichen        | Allgemein, Passwort-Stretching |
| SHA-3-256       | `hash.sha3_256`     | 64 Zeichen         | NIST-Standard (≠ Keccak!) |
| Keccak-256      | `hash.keccak256`    | 64 Zeichen         | Ethereum — **nicht** Standard-SHA-3 |
| RIPEMD-160      | `hash.ripemd160`    | 40 Zeichen         | Bitcoin-Adressen (HASH160) |
| BLAKE2b-256     | `hash.blake2b`      | 64 Zeichen         | Zcash, schnelle sichere Hashes |
| BLAKE2s-256     | `hash.blake2s`      | 64 Zeichen         | Zcash (32-bit-Plattformen) |
| MD5             | `hash.md5`          | 32 Zeichen         | Checksums, Content-Hashing (nicht sicher!) |
| CRC32           | `hash.crc32`        | `i32`              | Checksums, Protokoll-Frames |
| HMAC-SHA256     | `hash.hmac_sha256`  | 64 Zeichen         | API-Signaturen (AWS, Webhooks) |
| HMAC-SHA512     | `hash.hmac_sha512`  | 128 Zeichen        | Token-Signing |

---

## 2. Signaturen

Alle Hash-Funktionen akzeptieren `string` **oder** `buffer<u8>` als Eingabe.

```
hash.<algo>(data)              → string     hex-codierter Digest
hash.<algo>_raw(data)          → buffer<u8> roher Digest-Bytes (für Chaining)
hash.hmac_<algo>(key, msg)     → string     hex-codierter MAC
hash.hmac_<algo>_raw(key, msg) → buffer<u8> roher MAC
hash.crc32(data)               → i32        Checksumme als vorzeichenloser 32-Bit-Wert
```

---

## 3. Beispiele

### 3.1 Einfache Hashes

```aiml
- decl: [h, string, { call: [hash.sha256, "hello world"] }]
- print: ["SHA-256: ${h}"]
# → SHA-256: b94d27b9934d3e08a52e52d7da7dabfac484efe04294e576012bf7e4a9f3674a

- decl: [h512, string, { call: [hash.sha512, "hello world"] }]
- print: ["SHA-512: ${h512}"]

- decl: [m, string, { call: [hash.md5, "hello world"] }]
- print: ["MD5: ${m}"]

- decl: [crc, i32, { call: [hash.crc32, "hello world"] }]
- print: ["CRC32: ${crc}"]
```

### 3.2 Binary-Input (buffer\<u8\>)

```aiml
- decl: [buf, buffer<u8>]
- call: [buf.write_u8, 0xDE]
- call: [buf.write_u8, 0xAD]
- call: [buf.write_u8, 0xBE]
- call: [buf.write_u8, 0xEF]

- decl: [h, string, { call: [hash.sha256, buf] }]
- print: ["SHA-256(bytes): ${h}"]
```

### 3.3 HMAC (API-Signatur)

```aiml
# Webhook-Verifizierung (z.B. GitHub, Stripe)
- decl: [secret, string, "mysecret"]
- decl: [body, string, "{\"event\": \"push\"}"]

- decl: [sig, string, { call: [hash.hmac_sha256, secret, body] }]
- print: ["HMAC-SHA256: ${sig}"]

# AWS-Signatur (HMAC-SHA256 mit binary key)
- decl: [key_bytes, buffer<u8>]
# ... fill key_bytes ...
- decl: [mac, buffer<u8>, { call: [hash.hmac_sha256_raw, key_bytes, "AWS4-HMAC-SHA256"] }]
```

### 3.4 Chaining — Bitcoin HASH160

Bitcoin-Adressen verwenden `RIPEMD-160(SHA-256(pubkey))`:

```aiml
- def:
    name: hash160
    args: [[data, buffer<u8>]]
    return: buffer<u8>
    body:
      # Schritt 1: SHA-256 → rohe Bytes
      - decl: [sha_raw, buffer<u8>, { call: [hash.sha256_raw, data] }]
      # Schritt 2: RIPEMD-160 der SHA-256-Ausgabe
      - decl: [result, buffer<u8>, { call: [hash.ripemd160_raw, sha_raw] }]
      - return: result

# Verwendung
- decl: [pubkey, buffer<u8>]
# ... fill pubkey ...
- decl: [addr, buffer<u8>, { call: [hash160, pubkey] }]
- decl: [addr_hex, string, { call: [hash.ripemd160, addr] }]
- print: ["HASH160: ${addr_hex}"]
```

### 3.5 Ethereum — Keccak-256

> **Wichtig:** Ethereum verwendet **Keccak-256**, nicht Standard-SHA-3.
> Die beiden unterscheiden sich im Padding (Keccak: `0x01`, SHA-3: `0x06`).
> `hash.keccak256` liefert Ethereum-kompatible Ergebnisse.

```aiml
# Ethereum-Adresse aus öffentlichem Schlüssel
# (letzten 20 Bytes des Keccak-256-Hashs des uncompressed pubkeys)
- decl: [pubkey_bytes, buffer<u8>]
# ... fill pubkey_bytes ...

- decl: [keccak, buffer<u8>, { call: [hash.keccak256_raw, pubkey_bytes] }]
# Ethereum-Adresse = letzte 20 Bytes → "0x" + letzten 40 Zeichen des Hex-Strings
- decl: [keccak_hex, string, { call: [hash.keccak256, pubkey_bytes] }]
- print: ["Keccak-256: ${keccak_hex}"]
```

### 3.6 Bitcoin Double-SHA-256 (PoW)

```aiml
- def:
    name: sha256d
    args: [[data, buffer<u8>]]
    return: buffer<u8>
    body:
      - decl: [first, buffer<u8>, { call: [hash.sha256_raw, data] }]
      - return: { call: [hash.sha256_raw, first] }
```

### 3.7 BLAKE2b / BLAKE2s (Zcash)

```aiml
- decl: [b2b, string, { call: [hash.blake2b, "message"] }]
- print: ["BLAKE2b: ${b2b}"]

- decl: [b2s, string, { call: [hash.blake2s, "message"] }]
- print: ["BLAKE2s: ${b2s}"]
```

---

## 4. Namespace-Referenz

| Aufruf | Eingabe | Rückgabe | Beschreibung |
|--------|---------|----------|--------------|
| `{ call: [hash.sha256, data] }` | `string\|buffer<u8>` | `string` | SHA-256 hex |
| `{ call: [hash.sha256_raw, data] }` | `string\|buffer<u8>` | `buffer<u8>` | SHA-256 raw bytes |
| `{ call: [hash.sha512, data] }` | `string\|buffer<u8>` | `string` | SHA-512 hex |
| `{ call: [hash.sha512_raw, data] }` | `string\|buffer<u8>` | `buffer<u8>` | SHA-512 raw bytes |
| `{ call: [hash.sha3_256, data] }` | `string\|buffer<u8>` | `string` | NIST SHA-3-256 hex |
| `{ call: [hash.keccak256, data] }` | `string\|buffer<u8>` | `string` | Keccak-256 hex (Ethereum) |
| `{ call: [hash.keccak256_raw, data] }` | `string\|buffer<u8>` | `buffer<u8>` | Keccak-256 raw bytes |
| `{ call: [hash.ripemd160, data] }` | `string\|buffer<u8>` | `string` | RIPEMD-160 hex |
| `{ call: [hash.ripemd160_raw, data] }` | `string\|buffer<u8>` | `buffer<u8>` | RIPEMD-160 raw bytes |
| `{ call: [hash.blake2b, data] }` | `string\|buffer<u8>` | `string` | BLAKE2b-256 hex |
| `{ call: [hash.blake2b_raw, data] }` | `string\|buffer<u8>` | `buffer<u8>` | BLAKE2b-256 raw bytes |
| `{ call: [hash.blake2s, data] }` | `string\|buffer<u8>` | `string` | BLAKE2s-256 hex |
| `{ call: [hash.md5, data] }` | `string\|buffer<u8>` | `string` | MD5 hex (legacy) |
| `{ call: [hash.crc32, data] }` | `string\|buffer<u8>` | `i32` | CRC32-Checksumme |
| `{ call: [hash.hmac_sha256, key, msg] }` | `string, string` | `string` | HMAC-SHA256 hex |
| `{ call: [hash.hmac_sha256_raw, key, msg] }` | `string\|buf, string\|buf` | `buffer<u8>` | HMAC-SHA256 raw bytes |
| `{ call: [hash.hmac_sha512, key, msg] }` | `string, string` | `string` | HMAC-SHA512 hex |

---

## 5. Implementierungshinweise

### 5.1 Backend

| Algorithmus | Backend |
|-------------|---------|
| SHA-256, SHA-512, SHA-3-256 | OpenSSL `EVP_DigestX` (`libcrypto`) |
| RIPEMD-160 | OpenSSL `EVP_ripemd160()` |
| BLAKE2b, BLAKE2s | OpenSSL `EVP_blake2bX()` |
| HMAC | OpenSSL `HMAC()` |
| MD5 | OpenSSL `EVP_md5()` |
| CRC32 | zlib `crc32()` (oder handrolled — kein externer Dep) |
| Keccak-256 | **Handrolled** — OpenSSL implementiert SHA-3 (≠ Keccak), Ethereum-kompatible Variante |

### 5.2 String vs. Buffer\<u8\>-Dispatch

Runtime-Dispatch via Tag-Byte am Anfang des `i8*`-Pointers — oder einfacher:
zwei C-Funktionen pro Algorithmus:

```c
char* aiml_hash_sha256_str(const char* s);              // string input → hex string
char* aiml_hash_sha256_buf(AimlBuffer* buf);            // buffer input → hex string
AimlBuffer* aiml_hash_sha256_raw_str(const char* s);    // string input → raw bytes
AimlBuffer* aiml_hash_sha256_raw_buf(AimlBuffer* buf);  // buffer input → raw bytes
```

Der AIML-Compiler dispatcht anhand des Argument-Typs im `visitCall`.

### 5.3 hex-Encoding

Alle `string`-Rückgaben sind lowercase hex ohne Prefix (`"b94d27..."`, nicht `"0xB94D..."`).
Für Ethereum-Adressen wird das `0x`-Prefix typischerweise manuell per Stringinterpolation ergänzt:

```aiml
- decl: [addr, string, "0x${keccak_hex}"]
```

### 5.4 HMAC mit Buffer-Key (AWS-Signatur)

AWS Signature V4 verwendet HMAC-SHA256 mit binary keys (vorherige HMAC-Ausgaben als nächsten Key).
`hash.hmac_sha256_raw(buffer<u8> key, string msg)` ermöglicht das direkt:

```aiml
# AWS SigV4 Key-Derivation
- decl: [k1, buffer<u8>, { call: [hash.hmac_sha256_raw, "AWS4secret", "20240101"] }]
- decl: [k2, buffer<u8>, { call: [hash.hmac_sha256_raw, k1, "us-east-1"] }]
- decl: [k3, buffer<u8>, { call: [hash.hmac_sha256_raw, k2, "s3"] }]
- decl: [signing_key, buffer<u8>, { call: [hash.hmac_sha256_raw, k3, "aws4_request"] }]
```
