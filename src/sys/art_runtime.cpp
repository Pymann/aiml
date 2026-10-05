// art_runtime.cpp — client-side card-artwork bridge (pick + decode-to-texture).
//
// Two target-agnostic primitives keep every image codec / file-dialog divergence
// in ONE place so the AIML client (CardArtCache, CardArtUploader) stays clean:
//
//   aiml_art_decode_texture(webp_buf) -> AimlGLTexture*   (0 on failure)
//       Decode encoded image bytes (WebP/PNG/JPG) into a GPU texture.  Used by
//       the render path (CardArtCache.fetch) and the editor preview.
//
//   aiml_art_pick_webp(dst_buf, dw, dh, mode) -> int32   (bytes written, 0=cancel)
//       Open a file picker, decode the chosen image, resize/crop it to EXACTLY
//       dw×dh (mode 0 = cover-crop, 1 = stretch) and WebP-encode it into dst.
//
// NATIVE builds compose the existing extern-C image primitives (aiml_img_decode /
// _resize_crop / _encode_webp own the stb + libwebp impls) plus a zenity/kdialog
// file dialog — no codec is re-linked here.  WASM builds do the whole pipeline in
// the browser (createImageBitmap / canvas / toBlob), where WebP is native and the
// C++ codecs are stubbed out.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>

// Layout mirrors AimlImgBuf in img_runtime.cpp and the buffer runtime: the AIML
// `buffer` type is {size, cap, rIdx, wIdx, data}.
struct AimlArtBuf { int32_t size, cap, rIdx, wIdx; char* data; };

// Layout mirrors AimlGLTexture in gl_runtime.cpp (first three fields).
struct AimlArtTex { uint32_t id; int width; int height; };

// ── Cross-target extern-C helpers we build on ────────────────────────────────
extern "C" {
    int64_t aiml_img_decode(void* dst_buf, const void* src_data, int32_t src_len);
    int32_t aiml_img_encode_webp(void* dst_buf, const void* src_data, int32_t src_len,
                                 int32_t width, int32_t height, int32_t quality);
    int32_t aiml_img_resize_crop(void* dst_buf, const void* src_data, int32_t src_len,
                                 int32_t sw, int32_t sh, int32_t dw, int32_t dh, int32_t mode,
                                 float panx, float pany);
    void*   aiml_gl_create_texture(int32_t w, int32_t h, void* px_buf);
    void*   aiml_gl_create_texture_raw(int32_t w, int32_t h, const uint8_t* rgba);
    void    aiml_gl_texture_delete(void* tex);
    void    aiml_gl_texture_disown(void* tex);
}

// Allocate a scratch AimlArtBuf with `cap` bytes of data.  Returns nullptr on OOM.
static AimlArtBuf* art_scratch(int32_t cap) {
    if (cap <= 0) return nullptr;
    AimlArtBuf* b = (AimlArtBuf*)malloc(sizeof(AimlArtBuf));
    if (!b) return nullptr;
    b->data = (char*)malloc((size_t)cap);
    if (!b->data) { free(b); return nullptr; }
    b->size = 0; b->cap = cap; b->rIdx = 0; b->wIdx = 0;
    return b;
}
static void art_scratch_free(AimlArtBuf* b) {
    if (!b) return;
    if (b->data) free(b->data);
    free(b);
}

// Flip an RGBA image vertically in place (row-swap).  aiml_img_decode returns
// top-down rows, but the engine loads every other texture bottom-up
// (stbi_set_flip_vertically_on_load) and the sprite samples V-flipped, so any
// texture we build from decoded bytes must be flipped too or it renders upside down.
static void art_flip_rows(uint8_t* px, int w, int h) {
    if (!px || w <= 0 || h <= 1) return;
    int stride = w * 4;
    uint8_t* row = (uint8_t*)malloc((size_t)stride);
    if (!row) return;
    for (int y = 0; y < h / 2; ++y) {
        uint8_t* a = px + (size_t)y * stride;
        uint8_t* b = px + (size_t)(h - 1 - y) * stride;
        memcpy(row, a, stride); memcpy(a, b, stride); memcpy(b, row, stride);
    }
    free(row);
}

#ifdef __EMSCRIPTEN__
// ─── WASM (browser) ──────────────────────────────────────────────────────────
#include <emscripten.h>

// Decode encoded bytes at [ptr,ptr+len) into GL.textures[tex_id] via the browser.
// Returns (w<<16 | h), or 0 on failure.  The image window is ≤ 65535 per axis.
EM_ASYNC_JS(int, js_art_decode_texture, (int tex_id, const char* ptr, int len), {
    try {
        const bytes = HEAPU8.subarray(ptr, ptr + len);
        const blob  = new Blob([bytes.slice()]);        // copy out of the heap view
        const bmp   = await createImageBitmap(blob);
        const tex   = GL.textures[tex_id];
        if (!tex) { bmp.close(); return 0; }
        // Draw the bitmap into a 2D canvas and upload THAT — WebGL ignores
        // UNPACK_FLIP_Y_WEBGL for ImageBitmap sources (only the bitmap's own
        // imageOrientation counts), which left artwork upside-down.  A <canvas>
        // source honours FLIP_Y, matching the editor preview + the engine's
        // bottom-up (stbi-flipped) texture convention → renders upright.
        const cv = document.createElement('canvas');
        cv.width = bmp.width; cv.height = bmp.height;
        cv.getContext('2d').drawImage(bmp, 0, 0);
        const w = bmp.width & 0xFFFF, h = bmp.height & 0xFFFF;
        bmp.close();
        GLctx.bindTexture(GLctx.TEXTURE_2D, tex);
        GLctx.pixelStorei(GLctx.UNPACK_FLIP_Y_WEBGL, true);
        GLctx.texImage2D(GLctx.TEXTURE_2D, 0, GLctx.RGBA, GLctx.RGBA, GLctx.UNSIGNED_BYTE, cv);
        GLctx.pixelStorei(GLctx.UNPACK_FLIP_Y_WEBGL, false);
        GLctx.texParameteri(GLctx.TEXTURE_2D, GLctx.TEXTURE_MIN_FILTER, GLctx.LINEAR);
        GLctx.texParameteri(GLctx.TEXTURE_2D, GLctx.TEXTURE_MAG_FILTER, GLctx.LINEAR);
        GLctx.texParameteri(GLctx.TEXTURE_2D, GLctx.TEXTURE_WRAP_S, GLctx.CLAMP_TO_EDGE);
        GLctx.texParameteri(GLctx.TEXTURE_2D, GLctx.TEXTURE_WRAP_T, GLctx.CLAMP_TO_EDGE);
        GLctx.bindTexture(GLctx.TEXTURE_2D, null);
        return (w << 16) | h;
    } catch (e) { return 0; }
});

// Open a file picker, convert the chosen image to a dw×dh WebP, write it into the
// heap at [dst,dst+cap).  Returns bytes written (0 = cancelled / too large / error).
EM_ASYNC_JS(int, js_art_pick_webp, (char* dst, int cap, int dw, int dh, int mode), {
    try {
        const file = await new Promise((resolve) => {
            const inp = document.createElement('input');
            inp.type = 'file';
            inp.accept = 'image/*';
            inp.style.display = 'none';
            let done = false;
            const finish = (f) => { if (done) return; done = true;
                                    try { document.body.removeChild(inp); } catch (e) {}
                                    resolve(f); };
            inp.onchange = () => finish(inp.files && inp.files[0] ? inp.files[0] : null);
            inp.oncancel = () => finish(null);
            document.body.appendChild(inp);
            inp.click();
        });
        if (!file) return 0;
        const buf = await file.arrayBuffer();
        const bmp = await createImageBitmap(new Blob([buf]));
        const cv  = document.createElement('canvas');
        cv.width = dw; cv.height = dh;
        const cx = cv.getContext('2d');
        if (mode === 1) {
            cx.drawImage(bmp, 0, 0, dw, dh);            // stretch: fill target, distort
        } else {
            const sar = bmp.width / bmp.height, tar = dw / dh;   // cover-crop, centered
            let sw2 = bmp.width, sh2 = bmp.height, sx = 0, sy = 0;
            if (sar > tar) { sw2 = Math.round(bmp.height * tar); sx = ((bmp.width - sw2) / 2) | 0; }
            else           { sh2 = Math.round(bmp.width / tar);  sy = ((bmp.height - sh2) / 2) | 0; }
            cx.drawImage(bmp, sx, sy, sw2, sh2, 0, 0, dw, dh);
        }
        bmp.close();
        const outBlob = await new Promise((res) => cv.toBlob(res, 'image/webp', 0.9));
        if (!outBlob) return 0;
        const outBuf = new Uint8Array(await outBlob.arrayBuffer());
        if (outBuf.length <= 0 || outBuf.length > cap) return 0;
        HEAPU8.set(outBuf, dst);
        return outBuf.length;
    } catch (e) { return 0; }
});

extern "C" void* aiml_art_decode_texture(void* webp_buf) {
    if (!webp_buf) return nullptr;
    AimlArtBuf* src = (AimlArtBuf*)webp_buf;
    if (!src->data || src->size <= 0) return nullptr;
    // Create an empty GL texture (valid id + storage); the browser re-uploads the
    // decoded bitmap into GL.textures[id], resizing it to the image's real dims.
    AimlArtTex* t = (AimlArtTex*)aiml_gl_create_texture(1, 1, nullptr);
    if (!t) return nullptr;
    int wh = js_art_decode_texture((int)t->id, src->data, src->size);
    if (wh == 0) { aiml_gl_texture_delete(t); return nullptr; }
    t->width  = (wh >> 16) & 0xFFFF;
    t->height = wh & 0xFFFF;
    return t;
}

extern "C" int32_t aiml_art_pick_webp(void* dst_buf, int32_t dw, int32_t dh, int32_t mode) {
    if (!dst_buf || dw <= 0 || dh <= 0) return 0;
    AimlArtBuf* dst = (AimlArtBuf*)dst_buf;
    if (!dst->data || dst->cap <= 0) return 0;
    int32_t n = js_art_pick_webp(dst->data, dst->cap, dw, dh, mode);
    if (n <= 0) return 0;
    dst->size = n; dst->wIdx = n; dst->rIdx = 0;
    return n;
}

// ── Retained-source flow (interactive pan-crop), browser side ────────────────
// The picked ImageBitmap is stashed on Module so the editor can re-crop it live
// at any pan and encode the final WebP once, at save.

// Open a file picker, decode the chosen image, stash it as Module.__artSrc.
EM_ASYNC_JS(int, js_art_pick_source, (), {
    try {
        const file = await new Promise((resolve) => {
            const inp = document.createElement('input');
            inp.type = 'file'; inp.accept = 'image/*'; inp.style.display = 'none';
            let done = false;
            const finish = (f) => { if (done) return; done = true;
                                    try { document.body.removeChild(inp); } catch (e) {}
                                    resolve(f); };
            inp.onchange = () => finish(inp.files && inp.files[0] ? inp.files[0] : null);
            inp.oncancel = () => finish(null);
            document.body.appendChild(inp); inp.click();
        });
        if (!file) return 0;
        const buf = await file.arrayBuffer();
        const bmp = await createImageBitmap(new Blob([buf]));
        if (Module.__artSrc) { try { Module.__artSrc.close(); } catch (e) {} }
        Module.__artSrc = bmp;
        return 1;
    } catch (e) { console.warn('[art] pick_source failed:', e); return 0; }
});

// Compute a cover/stretch crop rect of the stashed source at (panx,pany) and draw
// it into a dw×dh canvas.  Shared by the texture + encode paths.  Returns the
// canvas, or null if no source.  Mirrors aiml_img_resize_crop's cover math.
EM_JS(int, js_art_source_draw, (int tex_id, int dw, int dh, int mode, double panx, double pany), {
    try {
        const bmp = Module.__artSrc;
        if (!bmp) return 0;
        const iw = bmp.width, ih = bmp.height;
        let cw = iw, ch = ih, sx = 0, sy = 0;
        if (mode !== 1) {
            if (iw * dh > ih * dw) { ch = ih; cw = Math.round(ih * dw / dh); }
            else                   { cw = iw; ch = Math.round(iw * dh / dw); }
            cw = Math.min(cw, iw); ch = Math.min(ch, ih);
            sx = Math.round((iw - cw) * panx); sy = Math.round((ih - ch) * pany);
            sx = Math.max(0, Math.min(sx, iw - cw)); sy = Math.max(0, Math.min(sy, ih - ch));
        }
        const cv = document.createElement('canvas');
        cv.width = dw; cv.height = dh;
        const cx = cv.getContext('2d');
        cx.drawImage(bmp, sx, sy, cw, ch, 0, 0, dw, dh);
        const tex = GL.textures[tex_id];
        if (!tex) return 0;
        GLctx.bindTexture(GLctx.TEXTURE_2D, tex);
        GLctx.pixelStorei(GLctx.UNPACK_FLIP_Y_WEBGL, true);
        GLctx.texImage2D(GLctx.TEXTURE_2D, 0, GLctx.RGBA, GLctx.RGBA, GLctx.UNSIGNED_BYTE, cv);
        GLctx.pixelStorei(GLctx.UNPACK_FLIP_Y_WEBGL, false);
        GLctx.texParameteri(GLctx.TEXTURE_2D, GLctx.TEXTURE_MIN_FILTER, GLctx.LINEAR);
        GLctx.texParameteri(GLctx.TEXTURE_2D, GLctx.TEXTURE_MAG_FILTER, GLctx.LINEAR);
        GLctx.texParameteri(GLctx.TEXTURE_2D, GLctx.TEXTURE_WRAP_S, GLctx.CLAMP_TO_EDGE);
        GLctx.texParameteri(GLctx.TEXTURE_2D, GLctx.TEXTURE_WRAP_T, GLctx.CLAMP_TO_EDGE);
        GLctx.bindTexture(GLctx.TEXTURE_2D, null);
        return 1;
    } catch (e) { return 0; }
});

// Crop the stashed source at (panx,pany) → WebP bytes into the heap at [dst,dst+cap).
EM_ASYNC_JS(int, js_art_encode_source, (char* dst, int cap, int dw, int dh, int mode, double panx, double pany), {
    try {
        const bmp = Module.__artSrc;
        if (!bmp) { console.warn('[art] encode_source: no picked source'); return 0; }
        const iw = bmp.width, ih = bmp.height;
        let cw = iw, ch = ih, sx = 0, sy = 0;
        if (mode !== 1) {
            if (iw * dh > ih * dw) { ch = ih; cw = Math.round(ih * dw / dh); }
            else                   { cw = iw; ch = Math.round(iw * dh / dw); }
            cw = Math.min(cw, iw); ch = Math.min(ch, ih);
            sx = Math.round((iw - cw) * panx); sy = Math.round((ih - ch) * pany);
            sx = Math.max(0, Math.min(sx, iw - cw)); sy = Math.max(0, Math.min(sy, ih - ch));
        }
        const cv = document.createElement('canvas');
        cv.width = dw; cv.height = dh;
        cv.getContext('2d').drawImage(bmp, sx, sy, cw, ch, 0, 0, dw, dh);
        const outBlob = await new Promise((res) => cv.toBlob(res, 'image/webp', 0.9));
        if (!outBlob) { console.warn('[art] encode_source: canvas.toBlob(image/webp) returned null'); return 0; }
        if (outBlob.type !== 'image/webp')
            console.warn('[art] encode_source: browser produced ' + outBlob.type + ' instead of image/webp');
        const outBuf = new Uint8Array(await outBlob.arrayBuffer());
        if (outBuf.length <= 0 || outBuf.length > cap) {
            console.warn('[art] encode_source: ' + outBuf.length + ' bytes, buffer capacity ' + cap);
            return 0;
        }
        HEAPU8.set(outBuf, dst);
        return outBuf.length;
    } catch (e) { console.warn('[art] encode_source failed:', e); return 0; }
});

extern "C" int32_t aiml_art_pick_source(void) { return js_art_pick_source(); }

static void* g_art_preview_tex = nullptr;
extern "C" void* aiml_art_source_texture(int32_t dw, int32_t dh, int32_t mode,
                                         float panx, float pany) {
    if (dw <= 0 || dh <= 0) return nullptr;
    // Reuse one preview texture across drag updates (no per-frame GPU leak).
    if (g_art_preview_tex) { aiml_gl_texture_delete(g_art_preview_tex); g_art_preview_tex = nullptr; }
    AimlArtTex* t = (AimlArtTex*)aiml_gl_create_texture(1, 1, nullptr);
    if (!t) return nullptr;
    if (!js_art_source_draw((int)t->id, dw, dh, mode, (double)panx, (double)pany)) {
        aiml_gl_texture_delete(t); return nullptr;
    }
    t->width = dw; t->height = dh;
    aiml_gl_texture_disown(t);
    g_art_preview_tex = t;
    return t;
}

extern "C" int32_t aiml_art_encode_source(void* dst_buf, int32_t dw, int32_t dh, int32_t mode,
                                          float panx, float pany) {
    if (!dst_buf || dw <= 0 || dh <= 0) return 0;
    AimlArtBuf* dst = (AimlArtBuf*)dst_buf;
    if (!dst->data || dst->cap <= 0) return 0;
    int32_t n = js_art_encode_source(dst->data, dst->cap, dw, dh, mode, (double)panx, (double)pany);
    if (n <= 0) return 0;
    dst->size = n; dst->wIdx = n; dst->rIdx = 0;
    return n;
}

#else
// ─── native ──────────────────────────────────────────────────────────────────

// Decode WebP/PNG/JPG bytes → RGBA → GPU texture.  Composes aiml_img_decode +
// aiml_gl_create_texture_raw; no codec is re-linked here.
extern "C" void* aiml_art_decode_texture(void* webp_buf) {
    if (!webp_buf) return nullptr;
    AimlArtBuf* src = (AimlArtBuf*)webp_buf;
    if (!src->data || src->size <= 0) return nullptr;
    // Worst case RGBA size is unknown before decode; the art window is bounded,
    // so a 16 MiB scratch (≈2048×2048 RGBA) covers any sane card artwork.
    AimlArtBuf* rgba = art_scratch(16 * 1024 * 1024);
    if (!rgba) return nullptr;
    int64_t wh = aiml_img_decode(rgba, src->data, src->size);
    if (wh <= 0) { art_scratch_free(rgba); return nullptr; }
    int w = (int)(wh >> 32), h = (int)(uint32_t)wh;
    art_flip_rows((uint8_t*)rgba->data, w, h);   // match the engine's bottom-up convention
    void* tex = aiml_gl_create_texture_raw(w, h, (const uint8_t*)rgba->data);
    art_scratch_free(rgba);
    return tex;
}

// Try one file-dialog command; write the chosen path into `out` (out_cap incl.
// NUL).  Returns 1 on success (non-empty path), 0 otherwise.
static int art_run_dialog(const char* cmd, char* out, size_t out_cap) {
    FILE* p = popen(cmd, "r");
    if (!p) return 0;
    char* line = fgets(out, (int)out_cap, p);
    pclose(p);
    if (!line) return 0;
    size_t n = strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = '\0';
    return n > 0 ? 1 : 0;
}

// Read a whole file into a scratch buffer (caller frees).  nullptr on error.
static AimlArtBuf* art_read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 64 * 1024 * 1024) { fclose(f); return nullptr; }  // 64 MiB cap
    AimlArtBuf* b = art_scratch((int32_t)sz);
    if (!b) { fclose(f); return nullptr; }
    size_t rd = fread(b->data, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) { art_scratch_free(b); return nullptr; }
    b->size = (int32_t)sz; b->wIdx = (int32_t)sz;
    return b;
}

extern "C" int32_t aiml_art_pick_webp(void* dst_buf, int32_t dw, int32_t dh, int32_t mode) {
    if (!dst_buf || dw <= 0 || dh <= 0) return 0;
    AimlArtBuf* dst = (AimlArtBuf*)dst_buf;
    if (!dst->data || dst->cap <= 0) return 0;

    // Pick a file via whichever dialog is installed (zenity → kdialog).
    char path[4096] = {0};
    int got = art_run_dialog(
        "zenity --file-selection --title='Card Artwork' "
        "--file-filter='Images | *.png *.jpg *.jpeg *.webp *.bmp *.gif' 2>/dev/null",
        path, sizeof(path));
    if (!got)
        got = art_run_dialog("kdialog --getopenfilename . 'image/*' 2>/dev/null",
                             path, sizeof(path));
    if (!got) return 0;

    AimlArtBuf* raw = art_read_file(path);
    if (!raw) return 0;

    // decode → RGBA (sw×sh)
    AimlArtBuf* rgba = art_scratch(16 * 1024 * 1024);
    if (!rgba) { art_scratch_free(raw); return 0; }
    int64_t wh = aiml_img_decode(rgba, raw->data, raw->size);
    art_scratch_free(raw);
    if (wh <= 0) { art_scratch_free(rgba); return 0; }
    int sw = (int)(wh >> 32), sh = (int)(uint32_t)wh;

    // resize/crop → EXACT dw×dh RGBA
    AimlArtBuf* fit = art_scratch(dw * dh * 4);
    if (!fit) { art_scratch_free(rgba); return 0; }
    int32_t fn = aiml_img_resize_crop(fit, rgba->data, rgba->size, sw, sh, dw, dh, mode, 0.5f, 0.5f);
    art_scratch_free(rgba);
    if (fn <= 0) { art_scratch_free(fit); return 0; }

    // encode → WebP (quality 90) straight into the caller's dst buffer
    int32_t n = aiml_img_encode_webp(dst, fit->data, fit->size, dw, dh, 90);
    art_scratch_free(fit);
    if (n <= 0) return 0;
    dst->size = n; dst->wIdx = n; dst->rIdx = 0;
    return n;
}

// ── Retained-source flow (interactive pan-crop) ──────────────────────────────
// pick_source stashes the FULL decoded RGBA so the editor can re-crop it live at
// any pan offset (preview) and encode the final WebP once, at save.
static AimlArtBuf* g_art_src = nullptr;   // full decoded RGBA of the picked image
static int         g_art_src_w = 0, g_art_src_h = 0;

// Open a file picker, decode the chosen image, and stash it as the pan source.
// Returns 1 on success, 0 on cancel/failure.
extern "C" int32_t aiml_art_pick_source(void) {
    char path[4096] = {0};
    int got = art_run_dialog(
        "zenity --file-selection --title='Card Artwork' "
        "--file-filter='Images | *.png *.jpg *.jpeg *.webp *.bmp *.gif' 2>/dev/null",
        path, sizeof(path));
    if (!got)
        got = art_run_dialog("kdialog --getopenfilename . 'image/*' 2>/dev/null", path, sizeof(path));
    if (!got) return 0;

    AimlArtBuf* raw = art_read_file(path);
    if (!raw) return 0;
    AimlArtBuf* rgba = art_scratch(64 * 1024 * 1024);   // up to ~4096² RGBA
    if (!rgba) { art_scratch_free(raw); return 0; }
    int64_t wh = aiml_img_decode(rgba, raw->data, raw->size);
    art_scratch_free(raw);
    if (wh <= 0) { art_scratch_free(rgba); return 0; }
    if (g_art_src) art_scratch_free(g_art_src);
    g_art_src = rgba;
    g_art_src_w = (int)(wh >> 32);
    g_art_src_h = (int)(uint32_t)wh;
    return 1;
}

// Crop the stashed source at (panx,pany) into a dw×dh GPU texture for live preview.
// Reuses a single preview texture (deletes the previous) so repeated drag updates
// don't leak GPU textures — the caller always gets a fresh, valid handle.
static void* g_art_preview_tex = nullptr;
extern "C" void* aiml_art_source_texture(int32_t dw, int32_t dh, int32_t mode,
                                         float panx, float pany) {
    if (!g_art_src || dw <= 0 || dh <= 0) return nullptr;
    AimlArtBuf* fit = art_scratch(dw * dh * 4);
    if (!fit) return nullptr;
    int32_t fn = aiml_img_resize_crop(fit, g_art_src->data, g_art_src->size,
                                      g_art_src_w, g_art_src_h, dw, dh, mode, panx, pany);
    if (fn <= 0) { art_scratch_free(fit); return nullptr; }
    art_flip_rows((uint8_t*)fit->data, dw, dh);
    if (g_art_preview_tex) { aiml_gl_texture_delete(g_art_preview_tex); g_art_preview_tex = nullptr; }
    void* tex = aiml_gl_create_texture_raw(dw, dh, (const uint8_t*)fit->data);
    art_scratch_free(fit);
    aiml_gl_texture_disown(tex);
    g_art_preview_tex = tex;
    return tex;
}

// Crop the stashed source at (panx,pany) and WebP-encode it into `dst_buf`.
// Returns byte count, 0 on failure.  Used for the final upload at save.
extern "C" int32_t aiml_art_encode_source(void* dst_buf, int32_t dw, int32_t dh, int32_t mode,
                                          float panx, float pany) {
    if (!g_art_src || !dst_buf || dw <= 0 || dh <= 0) return 0;
    AimlArtBuf* dst = (AimlArtBuf*)dst_buf;
    if (!dst->data || dst->cap <= 0) return 0;
    AimlArtBuf* fit = art_scratch(dw * dh * 4);
    if (!fit) return 0;
    int32_t fn = aiml_img_resize_crop(fit, g_art_src->data, g_art_src->size,
                                      g_art_src_w, g_art_src_h, dw, dh, mode, panx, pany);
    if (fn <= 0) { art_scratch_free(fit); return 0; }
    int32_t n = aiml_img_encode_webp(dst, fit->data, fit->size, dw, dh, 90);
    art_scratch_free(fit);
    if (n <= 0) return 0;
    dst->size = n; dst->wIdx = n; dst->rIdx = 0;
    return n;
}

#endif
