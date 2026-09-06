// img_runtime.cpp — in-memory image decode/encode for card artwork.
//
// Decode: WebP (libwebp) + PNG/JPG (stb_image; the STB_IMAGE_IMPLEMENTATION
//   lives in gltf_runtime.cpp, we only use the declarations here).
// Encode: WebP lossless / lossy (libwebp).
//
// All functions operate on AIML buffers and need NO GL context (headless-safe).
// The browser (WASM) decodes WebP natively via createImageBitmap and encodes
// via Canvas, so these are native-only; WASM gets no-op stubs.

#include <cstdint>
#include <cstring>
#include <cstdlib>

// AIML buffer layout (shared, single compiler): { i32 size, cap, rIdx, wIdx; i8* data }
struct AimlImgBuf { int32_t size, cap, rIdx, wIdx; char* data; };

#ifdef __EMSCRIPTEN__

extern "C" {
int64_t aiml_img_decode(void*, const void*, int32_t) { return 0; }
int32_t aiml_img_encode_webp(void*, const void*, int32_t, int32_t, int32_t, int32_t) { return 0; }
int32_t aiml_img_resize_crop(void*, const void*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, float, float) { return 0; }
}

#else // ─── native ───────────────────────────────────────────────────────────

#include <stb/stb_image.h>          // declarations only (impl in gltf_runtime.cpp)
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_resize.h>   // impl owned here (no other TU defines it)
#include <webp/decode.h>
#include <webp/encode.h>

extern "C" {

// Decode PNG/JPG/WebP bytes into dst as tightly-packed RGBA (w*h*4).
// Returns ((int64)w << 32) | (uint32)h, or 0 on failure.  dst must be
// pre-allocated; if it cannot hold w*h*4 bytes the decode is rejected.
int64_t aiml_img_decode(void* dst_buf, const void* src_data, int32_t src_len) {
    if (!dst_buf || !src_data || src_len <= 0) return 0;
    AimlImgBuf* dst = (AimlImgBuf*)dst_buf;
    if (!dst->data || dst->cap <= 0) return 0;

    int w = 0, h = 0;
    uint8_t* rgba = nullptr;
    bool is_webp = (src_len >= 12 &&
        memcmp(src_data, "RIFF", 4) == 0 &&
        memcmp((const char*)src_data + 8, "WEBP", 4) == 0);
    if (is_webp) {
        rgba = WebPDecodeRGBA((const uint8_t*)src_data, (size_t)src_len, &w, &h);
    } else {
        int ch = 0;
        rgba = stbi_load_from_memory((const stbi_uc*)src_data, src_len, &w, &h, &ch, 4);
    }
    if (!rgba) return 0;

    int64_t need = (int64_t)w * (int64_t)h * 4;
    if (need <= 0 || need > (int64_t)dst->cap) {
        if (is_webp) WebPFree(rgba); else stbi_image_free(rgba);
        return 0;
    }
    memcpy(dst->data, rgba, (size_t)need);
    dst->size = (int32_t)need; dst->wIdx = (int32_t)need; dst->rIdx = 0;
    if (is_webp) WebPFree(rgba); else stbi_image_free(rgba);
    return ((int64_t)w << 32) | (int64_t)(uint32_t)h;
}

// Encode tightly-packed RGBA (w*h*4 bytes) to WebP into dst.
// quality < 0  → lossless;  0..100 → lossy quality.  Returns encoded byte
// count, or 0 on failure.  dst must be pre-allocated large enough.
int32_t aiml_img_encode_webp(void* dst_buf, const void* src_data, int32_t src_len,
                             int32_t width, int32_t height, int32_t quality) {
    if (!dst_buf || !src_data) return 0;
    AimlImgBuf* dst = (AimlImgBuf*)dst_buf;
    if (!dst->data || dst->cap <= 0) return 0;
    if (width <= 0 || height <= 0) return 0;
    if ((int64_t)width * (int64_t)height * 4 > (int64_t)src_len) return 0;

    uint8_t* out = nullptr;
    size_t n = 0;
    int stride = width * 4;
    if (quality < 0)
        n = WebPEncodeLosslessRGBA((const uint8_t*)src_data, width, height, stride, &out);
    else
        n = WebPEncodeRGBA((const uint8_t*)src_data, width, height, stride,
                           (float)quality, &out);
    if (!out || n == 0) return 0;
    if ((int64_t)n > (int64_t)dst->cap) { WebPFree(out); return 0; }
    memcpy(dst->data, out, n);
    dst->size = (int32_t)n; dst->wIdx = (int32_t)n; dst->rIdx = 0;
    WebPFree(out);
    return (int32_t)n;
}

// Resize RGBA `src` (sw×sh) to EXACTLY dw×dh into `dst`.  mode 0 = COVER-crop
// (aspect preserved, overflowing axis cropped, whole target filled); mode 1 =
// STRETCH (scale the whole image to the target, ignores aspect — may distort,
// but keeps every edge).  `panx`/`pany` in [0,1] position the crop window along
// each axis (0 = left/top edge, 0.5 = centered, 1 = right/bottom edge); they are
// ignored in stretch mode and on any axis that does not overflow.  Enforces the
// canonical artwork dimensions.  Returns dst byte count (dw*dh*4), or 0.
int32_t aiml_img_resize_crop(void* dst_buf, const void* src_data, int32_t src_len,
                             int32_t sw, int32_t sh, int32_t dw, int32_t dh, int32_t mode,
                             float panx, float pany) {
    if (!dst_buf || !src_data) return 0;
    AimlImgBuf* dst = (AimlImgBuf*)dst_buf;
    if (!dst->data || dst->cap <= 0) return 0;
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return 0;
    int64_t need = (int64_t)dw * dh * 4;
    if (need > (int64_t)dst->cap) return 0;
    if ((int64_t)sw * sh * 4 > (int64_t)src_len) return 0;
    if (panx < 0.0f) panx = 0.0f; if (panx > 1.0f) panx = 1.0f;
    if (pany < 0.0f) pany = 0.0f; if (pany > 1.0f) pany = 1.0f;

    int ok;
    if (mode == 1) {
        // STRETCH: resize the whole source to the target (no crop, may distort).
        ok = stbir_resize_uint8((const unsigned char*)src_data, sw, sh, sw * 4,
                                (unsigned char*)dst->data, dw, dh, dw * 4, 4);
    } else {
        // COVER-crop: crop rectangle of the source matching the target aspect,
        // positioned by panx/pany along the overflowing axis.
        int cw, ch, cx, cy;
        if ((int64_t)sw * dh > (int64_t)sh * dw) {   // source is wider → crop width
            ch = sh; cw = (int)((int64_t)sh * dw / dh);
        } else {                                     // source is taller → crop height
            cw = sw; ch = (int)((int64_t)sw * dh / dw);
        }
        if (cw <= 0) cw = 1;
        if (ch <= 0) ch = 1;
        if (cw > sw) cw = sw;
        if (ch > sh) ch = sh;
        cx = (int)((float)(sw - cw) * panx + 0.5f);
        cy = (int)((float)(sh - ch) * pany + 0.5f);
        if (cx < 0) cx = 0; if (cx > sw - cw) cx = sw - cw;
        if (cy < 0) cy = 0; if (cy > sh - ch) cy = sh - ch;
        const unsigned char* in = (const unsigned char*)src_data
                                + ((size_t)cy * sw + cx) * 4;
        ok = stbir_resize_uint8(in, cw, ch, sw * 4,
                                (unsigned char*)dst->data, dw, dh, dw * 4, 4);
    }
    if (!ok) return 0;
    dst->size = (int32_t)need; dst->wIdx = (int32_t)need; dst->rIdx = 0;
    return (int32_t)need;
}

} // extern "C"

#endif
