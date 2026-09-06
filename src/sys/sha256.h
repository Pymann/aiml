// sha256.h — one portable SHA-256, shared by native AND WASM.
//
// SHA-256 is the only crypto hash the browser client needs (content-addressing
// uploaded card artwork), and the native OpenSSL surface (hash_runtime.cpp) is
// not compiled for WASM.  Rather than keep two SHA-256 implementations, this one
// pure-C version backs the sha256 path on BOTH targets — native `aiml_hash_hex_buf`
// / `hash.sha256` route their sha256 case here, and the WASM glue calls it too —
// so a blob hashed in the browser, on a native client, or by ./media always gets
// the identical digest.  OpenSSL still handles the richer algos (sha512, blake2,
// hmac, pbkdf2, …) on native.
#pragma once
#include <cstdint>

extern "C" {

// Lowercase-hex SHA-256 of `len` raw bytes → a malloc'd C string (borrowed by the
// runtime, not refcounted — matches the native to_hex() contract).  Binary-safe.
char* aiml_sha256_hex(const void* data, int32_t len);

}
