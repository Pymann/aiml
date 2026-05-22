# Chapter 33.1 — `hash` Namespace

> **Core idea:** Cryptographic and non-cryptographic hash functions as a first-class namespace.
> All functions return a hex-encoded `string` by default.
> For chaining (e.g. Bitcoin HASH160) there are `_raw` variants that return `buffer<u8>`.
> Implementation: OpenSSL (`libcrypto`, already linked via `tls`) + handrolled Keccak-256.

---

## 1. Overview

| Algorithm       | Function            | Output length (hex) | Use case |
|-----------------|---------------------|---------------------|----------|
| SHA-256         | `hash.sha256`       | 64 characters       | General, Bitcoin PoW |
| SHA-512         | `hash.sha512`       | 128 characters      | General, password stretching |
| SHA-3-256       | `hash.sha3_256`     | 64 characters       | NIST standard (≠ Keccak!) |
| Keccak-256      | `hash.keccak256`    | 64 characters       | Ethereum — **not** standard SHA-3 |
| RIPEMD-160      | `hash.ripemd160`    | 40 characters       | Bitcoin addresses (HASH160) |
| BLAKE2b-256     | `hash.blake2b`      | 64 characters       | Zcash, fast secure hashes |
| BLAKE2s-256     | `hash.blake2s`      | 64 characters       | Zcash (32-bit platforms) |
| MD5             | `hash.md5`          | 32 characters       | Checksums, content hashing (not secure!) |
| CRC32           | `hash.crc32`        | `i32`               | Checksums, protocol frames |
| HMAC-SHA256     | `hash.hmac_sha256`  | 64 characters       | API signatures (AWS, webhooks) |
| HMAC-SHA512     | `hash.hmac_sha512`  | 128 characters      | Token signing |

---

## 2. Signatures

All hash functions accept `string` **or** `buffer<u8>` as input.

```
hash.<algo>(data)              → string     hex-encoded digest
hash.<algo>_raw(data)          → buffer<u8> raw digest bytes (for chaining)
hash.hmac_<algo>(key, msg)     → string     hex-encoded MAC
hash.hmac_<algo>_raw(key, msg) → buffer<u8> raw MAC
hash.crc32(data)               → i32        checksum as unsigned 32-bit value
```

---

## 3. Examples

### 3.1 Simple Hashes

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

### 3.2 Binary Input (buffer\<u8\>)

```aiml
- decl: [buf, buffer<u8>]
- call: [buf.write_u8, 0xDE]
- call: [buf.write_u8, 0xAD]
- call: [buf.write_u8, 0xBE]
- call: [buf.write_u8, 0xEF]

- decl: [h, string, { call: [hash.sha256, buf] }]
- print: ["SHA-256(bytes): ${h}"]
```

### 3.3 HMAC (API Signature)

```aiml
# Webhook verification (e.g. GitHub, Stripe)
- decl: [secret, string, "mysecret"]
- decl: [body, string, "{\"event\": \"push\"}"]

- decl: [sig, string, { call: [hash.hmac_sha256, secret, body] }]
- print: ["HMAC-SHA256: ${sig}"]

# AWS signature (HMAC-SHA256 with binary key)
- decl: [key_bytes, buffer<u8>]
# ... fill key_bytes ...
- decl: [mac, buffer<u8>, { call: [hash.hmac_sha256_raw, key_bytes, "AWS4-HMAC-SHA256"] }]
```

### 3.4 Chaining — Bitcoin HASH160

Bitcoin addresses use `RIPEMD-160(SHA-256(pubkey))`:

```aiml
- def:
    name: hash160
    args: [[data, buffer<u8>]]
    return: buffer<u8>
    body:
      # Step 1: SHA-256 → raw bytes
      - decl: [sha_raw, buffer<u8>, { call: [hash.sha256_raw, data] }]
      # Step 2: RIPEMD-160 of the SHA-256 output
      - decl: [result, buffer<u8>, { call: [hash.ripemd160_raw, sha_raw] }]
      - return: result

# Usage
- decl: [pubkey, buffer<u8>]
# ... fill pubkey ...
- decl: [addr, buffer<u8>, { call: [hash160, pubkey] }]
- decl: [addr_hex, string, { call: [hash.ripemd160, addr] }]
- print: ["HASH160: ${addr_hex}"]
```

### 3.5 Ethereum — Keccak-256

> **Important:** Ethereum uses **Keccak-256**, not standard SHA-3.
> The two differ in padding (Keccak: `0x01`, SHA-3: `0x06`).
> `hash.keccak256` returns Ethereum-compatible results.

```aiml
# Ethereum address from public key
# (last 20 bytes of the Keccak-256 hash of the uncompressed pubkey)
- decl: [pubkey_bytes, buffer<u8>]
# ... fill pubkey_bytes ...

- decl: [keccak, buffer<u8>, { call: [hash.keccak256_raw, pubkey_bytes] }]
# Ethereum address = last 20 bytes → "0x" + last 40 characters of the hex string
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

## 4. Namespace Reference

| Call | Input | Return | Description |
|------|-------|--------|-------------|
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
| `{ call: [hash.crc32, data] }` | `string\|buffer<u8>` | `i32` | CRC32 checksum |
| `{ call: [hash.hmac_sha256, key, msg] }` | `string, string` | `string` | HMAC-SHA256 hex |
| `{ call: [hash.hmac_sha256_raw, key, msg] }` | `string\|buf, string\|buf` | `buffer<u8>` | HMAC-SHA256 raw bytes |
| `{ call: [hash.hmac_sha512, key, msg] }` | `string, string` | `string` | HMAC-SHA512 hex |

---

## 5. Implementation Notes

### 5.1 Backend

| Algorithm | Backend |
|-----------|---------|
| SHA-256, SHA-512, SHA-3-256 | OpenSSL `EVP_DigestX` (`libcrypto`) |
| RIPEMD-160 | OpenSSL `EVP_ripemd160()` |
| BLAKE2b, BLAKE2s | OpenSSL `EVP_blake2bX()` |
| HMAC | OpenSSL `HMAC()` |
| MD5 | OpenSSL `EVP_md5()` |
| CRC32 | zlib `crc32()` (or handrolled — no external dep) |
| Keccak-256 | **Handrolled** — OpenSSL implements SHA-3 (≠ Keccak), Ethereum-compatible variant |

### 5.2 String vs. Buffer\<u8\> Dispatch

Runtime dispatch via tag byte at the start of the `i8*` pointer — or simpler:
two C functions per algorithm:

```c
char* aiml_hash_sha256_str(const char* s);              // string input → hex string
char* aiml_hash_sha256_buf(AimlBuffer* buf);            // buffer input → hex string
AimlBuffer* aiml_hash_sha256_raw_str(const char* s);    // string input → raw bytes
AimlBuffer* aiml_hash_sha256_raw_buf(AimlBuffer* buf);  // buffer input → raw bytes
```

The AIML compiler dispatches based on the argument type in `visitCall`.

### 5.3 Hex Encoding

All `string` return values are lowercase hex without prefix (`"b94d27..."`, not `"0xB94D..."`).
For Ethereum addresses the `0x` prefix is typically added manually via string interpolation:

```aiml
- decl: [addr, string, "0x${keccak_hex}"]
```

### 5.4 HMAC with Buffer Key (AWS Signature)

AWS Signature V4 uses HMAC-SHA256 with binary keys (previous HMAC outputs as the next key).
`hash.hmac_sha256_raw(buffer<u8> key, string msg)` enables this directly:

```aiml
# AWS SigV4 key derivation
- decl: [k1, buffer<u8>, { call: [hash.hmac_sha256_raw, "AWS4secret", "20240101"] }]
- decl: [k2, buffer<u8>, { call: [hash.hmac_sha256_raw, k1, "us-east-1"] }]
- decl: [k3, buffer<u8>, { call: [hash.hmac_sha256_raw, k2, "s3"] }]
- decl: [signing_key, buffer<u8>, { call: [hash.hmac_sha256_raw, k3, "aws4_request"] }]
```
