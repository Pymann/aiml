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
| PBKDF2-HMAC-SHA256 | `hash.pbkdf2`    | 64 characters       | **Password storage** (deliberately slow) |

> **Password storage:** use `hash.pbkdf2`, never a bare `hash.sha256`/`hash.sha512`.
> Plain digests are far too fast and let an attacker brute-force a stolen hash
> table. `hash.pbkdf2` stretches the work by a caller-chosen iteration count and
> takes a per-user random salt (`math.random_hex`, spec 43_0). See §3.8.

---

## 2. Signatures

All hash functions accept `string` **or** `buffer<u8>` as input. For the hex digests
`sha256`, `sha512`, `sha3_256`, `blake2b`, `blake2s`, `md5` a `buffer<u8>` argument is hashed
**binary-safe over its exact byte length** (pointer + `size`), not as a C string — so bytes
containing `0x00` hash correctly (verified by `tests/test_hash_buffer.aiml`). This makes
`hash.sha256(buf)` the content-address of raw bytes without a per-type hash method. (The `_raw`
variants and `keccak256`/`ripemd160` currently take `string` only.)

```
hash.<algo>(data)              → string     hex-encoded digest
hash.<algo>_raw(data)          → buffer<u8> raw digest bytes (for chaining)
hash.hmac_<algo>(key, msg)     → string     hex-encoded MAC
hash.hmac_<algo>_raw(key, msg) → buffer<u8> raw MAC
hash.crc32(data)               → i32        checksum as unsigned 32-bit value
hash.pbkdf2(password, salt, iterations) → string  PBKDF2-HMAC-SHA256, 32-byte key as 64 hex chars
hash.const_time_eq(a, b)       → i32        constant-time string compare: 1 = equal, 0 = not
```

`hash.const_time_eq` compares two strings in **constant time** (`CRYPTO_memcmp`), so the compare
time does not reveal how many leading bytes matched. Use it to compare secrets — session tokens,
API keys, HMAC/CSRF tags — where a short-circuiting `eq` would leak information to a timing attack.
Differing lengths return `0`.

`hash.pbkdf2` takes two `string` arguments plus an `i32` iteration count. The
iteration count is a work factor: higher is slower and more brute-force-resistant
(a value `< 1` is floored to `100000`). The derived key is always 32 bytes
(SHA-256 output size), returned as 64 lowercase hex characters.

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

### 3.8 Password Storage (PBKDF2)

`hash.pbkdf2` is the correct primitive for storing user passwords. On sign-up,
generate a random salt, derive the key, and store **salt + iterations + key**
(never the password). On login, re-derive with the stored salt/iterations and
compare.

```aiml
# ── Sign-up: derive and store ────────────────────────────────────────────
- decl: [pw_salt,  string, { call: [math.random_hex, 16] }]        # 128-bit random salt
- decl: [pw_iters, i32, 100000]                                    # work factor
- decl: [pw_key,   string, { call: [hash.pbkdf2, "s3cret!", pw_salt, pw_iters] }]
# persist { pw_salt, pw_iters, pw_key } for the user

# ── Login: re-derive with the SAME salt + iterations and compare ──────────
- decl: [try_key, string, { call: [hash.pbkdf2, "s3cret!", pw_salt, pw_iters] }]
- if:
    condition: { eq: [try_key, pw_key] }                           # string equality
    then:
      - print: ["login ok"]
    else:
      - print: ["login rejected"]
```

Deterministic for the same `(password, salt, iterations)`; a different salt (or
password) yields a completely different key. Known-answer check
(`hash.pbkdf2("password", "salt", 1)`):
`120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b`.

> ⚠️ Compare with a constant-time equality where available. Always use a fresh
> per-user salt from `math.random_hex` — never a shared or hard-coded salt.

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
| `{ call: [hash.pbkdf2, password, salt, iterations] }` | `string, string, i32` | `string` | PBKDF2-HMAC-SHA256, 32-byte key as 64 hex chars |
| `{ call: [hash.const_time_eq, a, b] }` | `string, string` | `i32` | Constant-time compare (1 = equal); for tokens/HMAC tags |

---

## 5. Implementation Notes

### 5.1 Backend

| Algorithm | Backend |
|-----------|---------|
| SHA-256, SHA-512, SHA-3-256 | OpenSSL `EVP_DigestX` (`libcrypto`) |
| RIPEMD-160 | OpenSSL `EVP_ripemd160()` |
| BLAKE2b, BLAKE2s | OpenSSL `EVP_blake2bX()` |
| HMAC | OpenSSL `HMAC()` |
| PBKDF2 | OpenSSL `PKCS5_PBKDF2_HMAC()` with `EVP_sha256()`, 32-byte output |
| const_time_eq | OpenSSL `CRYPTO_memcmp()` (length-checked) |
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
