/* CRC32 — extracted from hash_runtime.cpp so it can be linked into the WASM
 * build (libaiml_wasm.a) without dragging in the OpenSSL dependency that the
 * SHA/HMAC paths in hash_runtime.cpp require.
 *
 * The desktop build keeps hash_runtime.cpp; both files define aiml_hash_crc32
 * with identical semantics — but only ONE of them is compiled per output
 * artifact (desktop excludes this file, WASM excludes hash_runtime.cpp). */
#include <stdint.h>
#include <stddef.h>

#ifdef AIML_WASM_BUILD

extern "C" {

static uint32_t s_crc32_table[256];
static int      s_crc32_initialized = 0;

static void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c >> 1) ^ (c & 1u ? 0xEDB88320u : 0u);
        s_crc32_table[i] = c;
    }
    s_crc32_initialized = 1;
}

int32_t aiml_hash_crc32(const char* s) {
    if (!s) return 0;
    if (!s_crc32_initialized) crc32_init();
    uint32_t crc = 0xFFFFFFFFu;
    while (*s)
        crc = (crc >> 8) ^ s_crc32_table[(crc ^ (uint8_t)*s++) & 0xFF];
    return (int32_t)(crc ^ 0xFFFFFFFFu);
}

} // extern "C"

#endif /* AIML_WASM_BUILD */
