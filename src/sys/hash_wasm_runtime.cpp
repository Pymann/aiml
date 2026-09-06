// hash_wasm_runtime.cpp — WASM-only glue for the buffer hash surface.
//
// The native hash dispatcher `aiml_hash_hex_buf` lives in hash_runtime.cpp, which
// is built on OpenSSL and therefore excluded from the WASM build.  The browser
// client only ever needs sha256 (content-addressing card artwork), so this file
// provides just that one entry point for WASM, delegating to the shared portable
// sha256 in sha256.cpp (aiml_hash_sha256 itself is already defined there, on both
// targets).  No algorithm is duplicated — only this thin, target-specific
// dispatcher exists twice, once per toolchain.

#ifdef __EMSCRIPTEN__

#include "sha256.h"
#include <cstring>
#include <cstdlib>

extern "C" {

// Length-aware hex digest over raw bytes.  WASM supports sha256 only (the sole
// algo the browser client asks for); any other algo returns "" rather than a
// silently mismatched digest.
char* aiml_hash_hex_buf(const char* algo, const void* data, int32_t len) {
    if (algo && strcmp(algo, "sha256") != 0) { char* e = (char*)malloc(1); if (e) e[0] = '\0'; return e; }
    return aiml_sha256_hex(data, len);
}

} // extern "C"

#endif // __EMSCRIPTEN__
