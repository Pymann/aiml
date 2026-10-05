/*
 * gl_runtime.cpp — AIML gl-Namespace Implementierung
 * Backend (Desktop): GLEW + Desktop GL 3.3 Core
 * Backend (Web):     Emscripten WebGL2 via <GLES3/gl3.h> (no extension loader)
 *
 * stb_image wird von gltf_runtime.cpp bereitgestellt (kein STB_IMAGE_IMPLEMENTATION hier)
 */

#include "gl_runtime.h"

#ifdef __EMSCRIPTEN__
/* WebGL2 ≈ OpenGL ES 3.0. Emscripten provides the GLES3 headers; symbols are
 * resolved at link time from the emscripten GL library — no glewInit needed. */
#  include <GLES3/gl3.h>
#  include <GLES2/gl2ext.h>
#else
/* GLEW muss VOR allen anderen GL-Headern eingebunden werden */
#  include <GL/glew.h>
#endif

/* stb_image
 *   Native build:  declarations only — implementation lives in gltf_runtime.cpp.
 *   Web build:     gltf_runtime is not part of libaiml_wasm.a (assimp/cgltf
 *                  deps don't compile under emcc), so we own the implementation
 *                  here. */
#ifdef __EMSCRIPTEN__
#  define STB_IMAGE_IMPLEMENTATION
#endif
#include <stb/stb_image.h>

#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <string>
#include <vector>

/* WebP-Texturdecode für gl.tex:
 *   Native — libwebp (WebPDecodeRGBA), wie img_runtime; libwebp ist verlinkt.
 *   Web     — der Browser (createImageBitmap) via aiml_art_decode_texture
 *             (art_runtime); libwebp ist unter emcc NICHT verlinkt. */
#ifndef __EMSCRIPTEN__
#include <webp/decode.h>
#endif
extern "C" void* aiml_art_decode_texture(void* enc_buf);   // bytes → GL-Textur (WASM: browser)

/* ── Interne Struct-Definitionen ────────────────────────────────────────── */

/* Mirrors AIML buffer<T> layout: { i32 size(bytes), i32 cap, i32 ri, i32 wi, u8* data } */
struct AimlGLRawBuf {
    int32_t  size;
    int32_t  capacity;
    int32_t  read_idx;
    int32_t  write_idx;
    uint8_t* data;
};

struct AimlGLBuffer {
    GLuint   id;
    GLenum   target;  /* GL_ARRAY_BUFFER or GL_ELEMENT_ARRAY_BUFFER */
};

struct AimlGLVao {
    GLuint   id;
    GLuint   vbo_id;
    GLuint   ibo_id;
    bool     has_ibo;
};

struct AimlGLShader {
    GLuint   prog;
    std::unordered_map<std::string, GLint> uniforms;
    GLint location(const char* name) {
        auto it = uniforms.find(name);
        if (it != uniforms.end()) return it->second;
        GLint loc = glGetUniformLocation(prog, name);
        uniforms[name] = loc;
        return loc;
    }
};

struct AimlGLTexture {
    GLuint id;
    int    width  = 0;
    int    height = 0;
    int    channels_unused = 0;   /* keeps ui_runtime's mirror layout */
    GLenum format = 0;            /* spec/43_5 §4; 0 = legacy path */
    int    levels = 0;
    int    owned = 1;             /* spec/43_2 §5.5: 1 owned, 0 runtime-held, 2 view of an attachment */
};

struct AimlGLFramebuffer {
    GLuint fbo;
    GLuint color_tex;
    GLuint depth_rbo;
    int    w, h;
    GLenum format = GL_RGBA8;
    bool   has_depth = true;
};

/* gl_mesh: VAO + up to 3 VBOs (pos, uv, normals) + IBO */
struct AimlGLMesh {
    GLuint vao;
    GLuint vbo_pos;
    GLuint vbo_uv;
    GLuint vbo_norm;   /* 0 if no normals */
    GLuint vbo_tan;    /* 0 if no tangents (attrib 3) */
    GLuint vbo_joint;  /* 0 if not skinned (attrib 4) */
    GLuint vbo_weight; /* 0 if not skinned (attrib 5) */
    GLuint ibo;
    int32_t index_count;
};

/* ── Hilfsfunktionen ────────────────────────────────────────────────────── */

static AimlGLBuffer* make_buffer(GLenum target, const AimlGLRawBuf* src, GLenum usage) {
    AimlGLBuffer* b = new AimlGLBuffer();
    b->target = target;
    glGenBuffers(1, &b->id);
    /* For element array buffers, temporarily unbind any active VAO.
       In OpenGL Core Profile the GL_ELEMENT_ARRAY_BUFFER binding is stored
       per-VAO; glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0) at the end of this
       function would otherwise silently clear the currently-bound VAO's IBO. */
    GLint prev_vao = 0;
    if (target == GL_ELEMENT_ARRAY_BUFFER) {
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
        if (prev_vao) glBindVertexArray(0);
    }
    glBindBuffer(target, b->id);
    glBufferData(target, src ? src->size : 0, src ? src->data : nullptr, usage);
    glBindBuffer(target, 0);
    if (target == GL_ELEMENT_ARRAY_BUFFER && prev_vao)
        glBindVertexArray((GLuint)prev_vao);
    GLuint vbo_extra[4];      /* set_attribute VBOs (locations 6..15), 0 = unused */
    int32_t extra_loc[4];
    return b;
}

static void upload_texture(GLuint id, int w, int h, const uint8_t* rgba) {
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char* buf = (char*)malloc(sz + 1);
    if (buf) { fread(buf, 1, sz, f); buf[sz] = '\0'; }
    fclose(f);
    return buf;
}

static GLuint compile_stage(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; GLsizei len = 0;
        glGetShaderInfoLog(sh, sizeof(log), &len, log);
        fprintf(stderr, "[aiml_gl] shader compile error: %.*s\n", (int)len, log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

/* ── Initialisierung ────────────────────────────────────────────────────── */

void aiml_gl_init(void) {
#ifdef __EMSCRIPTEN__
    /* WebGL2: context already created by engine.create_window; no loader needed. */
#else
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        fprintf(stderr, "[aiml_gl] glewInit failed: %s\n", glewGetErrorString(err));
    }
    /* Consume spurious GL_INVALID_ENUM that GLEW generates on some drivers */
    glGetError();
#endif
}

/* ── Render-State ───────────────────────────────────────────────────────── */

void aiml_gl_viewport(int32_t x, int32_t y, int32_t w, int32_t h) {
    glViewport(x, y, w, h);
}

void aiml_gl_clear(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void aiml_gl_clear_depth(void) {
    glClear(GL_DEPTH_BUFFER_BIT);
}

void aiml_gl_clear_all(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void aiml_gl_enable_depth_test(void)  { glEnable(GL_DEPTH_TEST); }
void aiml_gl_disable_depth_test(void) { glDisable(GL_DEPTH_TEST); }

void aiml_gl_enable_blend(void) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
void aiml_gl_disable_blend(void)  { glDisable(GL_BLEND); }

void aiml_gl_enable_cull_face(void) { glEnable(GL_CULL_FACE); }
void aiml_gl_disable_cull_face(void) { glDisable(GL_CULL_FACE); }
void aiml_gl_cull_back(void)   { glCullFace(GL_BACK); }
void aiml_gl_cull_front(void)  { glCullFace(GL_FRONT); }

void aiml_gl_wireframe(int8_t on) {
#ifdef __EMSCRIPTEN__
    /* glPolygonMode is desktop-only; WebGL has no wireframe fill mode.
     * Callers can emulate via GL_LINES topology on the index buffer. */
    (void)on;
#else
    glPolygonMode(GL_FRONT_AND_BACK, on ? GL_LINE : GL_FILL);
#endif
}
void aiml_gl_polygon_offset(float factor, float units) { glPolygonOffset(factor, units); }
void aiml_gl_enable_polygon_offset_fill(void)  { glEnable(GL_POLYGON_OFFSET_FILL); }
void aiml_gl_disable_polygon_offset_fill(void) { glDisable(GL_POLYGON_OFFSET_FILL); }

/* ── Buffer ─────────────────────────────────────────────────────────────── */

void* aiml_gl_create_buffer(void* data_buf) {
    return make_buffer(GL_ARRAY_BUFFER, (AimlGLRawBuf*)data_buf, GL_STATIC_DRAW);
}

void* aiml_gl_create_buffer_dynamic(void* data_buf) {
    return make_buffer(GL_ARRAY_BUFFER, (AimlGLRawBuf*)data_buf, GL_DYNAMIC_DRAW);
}

void* aiml_gl_create_index_buffer(void* data_buf) {
    return make_buffer(GL_ELEMENT_ARRAY_BUFFER, (AimlGLRawBuf*)data_buf, GL_STATIC_DRAW);
}

void aiml_gl_buffer_update(void* buf, void* data_buf) {
    if (!buf || !data_buf) return;
    AimlGLBuffer*   b   = (AimlGLBuffer*)buf;
    AimlGLRawBuf*   src = (AimlGLRawBuf*)data_buf;
    glBindBuffer(b->target, b->id);
    glBufferSubData(b->target, 0, src->size, src->data);
    glBindBuffer(b->target, 0);
}

void aiml_gl_buffer_delete(void* buf) {
    if (!buf) return;
    AimlGLBuffer* b = (AimlGLBuffer*)buf;
    glDeleteBuffers(1, &b->id);
    delete b;
}

/* ── VAO ────────────────────────────────────────────────────────────────── */

void* aiml_gl_create_vao(void) {
    AimlGLVao* v = new AimlGLVao();
    v->vbo_id = 0; v->ibo_id = 0; v->has_ibo = false;
    glGenVertexArrays(1, &v->id);
    return v;
}

void aiml_gl_vao_bind_vbo(void* vao, void* vbo) {
    if (!vao || !vbo) return;
    AimlGLVao*    v = (AimlGLVao*)vao;
    AimlGLBuffer* b = (AimlGLBuffer*)vbo;
    v->vbo_id = b->id;
    glBindVertexArray(v->id);
    glBindBuffer(GL_ARRAY_BUFFER, b->id);
    /* Note: VAO remains bound so that subsequent attrib calls work */
}

void aiml_gl_vao_bind_ibo(void* vao, void* ibo) {
    if (!vao || !ibo) return;
    AimlGLVao*    v = (AimlGLVao*)vao;
    AimlGLBuffer* b = (AimlGLBuffer*)ibo;
    v->ibo_id  = b->id;
    v->has_ibo = true;
    glBindVertexArray(v->id);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->id);
}

void aiml_gl_vao_attrib(void* vao, int32_t idx, int32_t components,
                         int32_t stride, int32_t offset) {
    if (!vao) return;
    AimlGLVao* v = (AimlGLVao*)vao;
    glBindVertexArray(v->id);
    glBindBuffer(GL_ARRAY_BUFFER, v->vbo_id);
    glVertexAttribPointer((GLuint)idx, components, GL_FLOAT, GL_FALSE,
                          stride, (const void*)(intptr_t)offset);
    glEnableVertexAttribArray((GLuint)idx);
}

void aiml_gl_vao_attrib_i(void* vao, int32_t idx, int32_t components,
                           int32_t stride, int32_t offset) {
    if (!vao) return;
    AimlGLVao* v = (AimlGLVao*)vao;
    glBindVertexArray(v->id);
    glBindBuffer(GL_ARRAY_BUFFER, v->vbo_id);
    glVertexAttribIPointer((GLuint)idx, components, GL_INT,
                           stride, (const void*)(intptr_t)offset);
    glEnableVertexAttribArray((GLuint)idx);
}

void aiml_gl_vao_delete(void* vao) {
    if (!vao) return;
    AimlGLVao* v = (AimlGLVao*)vao;
    glDeleteVertexArrays(1, &v->id);
    delete v;
}

/* ── Shader ─────────────────────────────────────────────────────────────── */

void* aiml_gl_compile_shader(const char* vert_src, const char* frag_src) {
    GLuint vs = compile_stage(GL_VERTEX_SHADER,   vert_src ? vert_src : "");
    GLuint fs = compile_stage(GL_FRAGMENT_SHADER, frag_src ? frag_src : "");
    if (!vs || !fs) { glDeleteShader(vs); glDeleteShader(fs); return nullptr; }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]; GLsizei len = 0;
        glGetProgramInfoLog(prog, sizeof(log), &len, log);
        fprintf(stderr, "[aiml_gl] shader link error: %.*s\n", (int)len, log);
        glDeleteProgram(prog);
        return nullptr;
    }

    AimlGLShader* s = new AimlGLShader();
    s->prog = prog;
    return s;
}

void* aiml_gl_load_shader(const char* vert_path, const char* frag_path) {
    char* vs = read_file(vert_path);
    char* fs = read_file(frag_path);
    if (!vs || !fs) {
        free(vs); free(fs);
        fprintf(stderr, "[aiml_gl] load_shader: cannot open %s or %s\n", vert_path, frag_path);
        return nullptr;
    }
    void* sh = aiml_gl_compile_shader(vs, fs);
    free(vs); free(fs);
    return sh;
}

void aiml_gl_shader_use(void* shader) {
    if (!shader) return;
    glUseProgram(((AimlGLShader*)shader)->prog);
}

void aiml_gl_shader_uniform_f32(void* shader, const char* name, float v) {
    if (!shader) return;
    glUniform1f(((AimlGLShader*)shader)->location(name), v);
}

void aiml_gl_shader_uniform_i32(void* shader, const char* name, int32_t v) {
    if (!shader) return;
    glUniform1i(((AimlGLShader*)shader)->location(name), v);
}

void aiml_gl_shader_uniform_vec2(void* shader, const char* name, float* v) {
    if (!shader || !v) return;
    glUniform2fv(((AimlGLShader*)shader)->location(name), 1, v);
}

void aiml_gl_shader_uniform_vec3(void* shader, const char* name, float* v) {
    if (!shader || !v) return;
    glUniform3fv(((AimlGLShader*)shader)->location(name), 1, v);
}

void aiml_gl_shader_uniform_vec4(void* shader, const char* name, float* v) {
    if (!shader || !v) return;
    glUniform4fv(((AimlGLShader*)shader)->location(name), 1, v);
}

void aiml_gl_shader_uniform_mat4(void* shader, const char* name, float* m) {
    if (!shader || !m) return;
    glUniformMatrix4fv(((AimlGLShader*)shader)->location(name), 1, GL_FALSE, m);
}

void aiml_gl_shader_uniform_mat4_array(void* shader, const char* name,
                                        void* buf, int32_t count) {
    if (!shader || count == 0) return;
    AimlGLRawBuf* b = (AimlGLRawBuf*)buf;
    if (count < 0 || !b || !b->data || (int64_t)b->size < (int64_t)count * 16 * (int64_t)sizeof(float)) {
        fprintf(stderr, "[aiml_gl] uniform_mat4_array '%s': buffer too short for %d\n", name ? name : "", count);
        return;
    }
    glUniformMatrix4fv(((AimlGLShader*)shader)->location(name),
                       count, GL_FALSE, (const float*)b->data);
}

void aiml_gl_shader_delete(void* shader) {
    if (!shader) return;
    AimlGLShader* s = (AimlGLShader*)shader;
    glDeleteProgram(s->prog);
    delete s;
}

/* ── Textur ─────────────────────────────────────────────────────────────── */

// Headless detection: native uses glfwGetCurrentContext; WASM is always in a
// browser GL context (no GLFW), so just always upload.
#ifndef AIML_WASM_BUILD
extern "C" void* glfwGetCurrentContext(void);
extern "C" void  glfwGetFramebufferSize(void* win, int* w, int* h);
static inline bool aiml_gl_has_context() { return glfwGetCurrentContext() != nullptr; }
#else
static inline bool aiml_gl_has_context() { return true; }
#endif

void* aiml_gl_load_texture(const char* path) {
    if (!path) return nullptr;
    // Datei komplett in den Speicher lesen — so kann JEDER Codec dieselben Bytes
    // dekodieren (stb kann kein WebP; darum format-erkennend statt stbi_load(path)).
    FILE* fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "[aiml_gl] load_texture: cannot open %s\n", path); return nullptr; }
    fseek(fp, 0, SEEK_END);
    long flen = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (flen <= 0) { fclose(fp); fprintf(stderr, "[aiml_gl] load_texture: empty %s\n", path); return nullptr; }
    std::vector<uint8_t> bytes((size_t)flen);
    size_t rd = fread(bytes.data(), 1, (size_t)flen, fp);
    fclose(fp);
    if (rd != (size_t)flen) { fprintf(stderr, "[aiml_gl] load_texture: read failed %s\n", path); return nullptr; }

    const bool is_webp = (flen >= 12 &&
        memcmp(bytes.data(), "RIFF", 4) == 0 &&
        memcmp(bytes.data() + 8, "WEBP", 4) == 0);

#ifdef __EMSCRIPTEN__
    // Web: über den Browser dekodieren (createImageBitmap; PNG/JPG/WebP) — derselbe
    // bewährte Pfad wie die Card-Art, inkl. FLIP_Y auf die Engine-Konvention.
    struct EncBuf { int32_t size, cap, rIdx, wIdx; char* data; } eb;
    eb.data = (char*)bytes.data();
    eb.size = (int32_t)flen; eb.cap = (int32_t)flen; eb.rIdx = 0; eb.wIdx = (int32_t)flen;
    void* t = aiml_art_decode_texture(&eb);   // AimlArtTex* ist layout-gleich zu AimlGLTexture*
    if (!t) { fprintf(stderr, "[aiml_gl] load_texture: browser decode failed %s\n", path); return nullptr; }
    // gl.tex-Konvention an die native upload_texture angleichen: REPEAT (sonst
    // kacheln Tiling-Texturen wie Lobby-Hintergrund/Rahmen NICHT) + Mipmaps.
    // aiml_art_decode_texture setzt CLAMP_TO_EDGE (für Card-Art korrekt) — hier
    // überschreiben.
    glBindTexture(GL_TEXTURE_2D, ((AimlGLTexture*)t)->id);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    return t;
#else
    int w = 0, h = 0;
    uint8_t* px = nullptr;
    if (is_webp) {
        px = WebPDecodeRGBA(bytes.data(), (size_t)flen, &w, &h);   // top-down
        if (px) {  // auf bottom-up flippen (Engine-Konvention wie stbi flip-on-load)
            const int stride = w * 4;
            for (int y = 0; y < h / 2; ++y) {
                uint8_t* a = px + (size_t)y * stride;
                uint8_t* b = px + (size_t)(h - 1 - y) * stride;
                for (int x = 0; x < stride; ++x) { uint8_t tmp = a[x]; a[x] = b[x]; b[x] = tmp; }
            }
        }
    } else {
        stbi_set_flip_vertically_on_load(1);
        int ch = 0;
        px = stbi_load_from_memory(bytes.data(), (int)flen, &w, &h, &ch, 4);
    }
    if (!px) { fprintf(stderr, "[aiml_gl] load_texture: cannot decode %s\n", path); return nullptr; }
    AimlGLTexture* t = new AimlGLTexture();
    if (aiml_gl_has_context()) {
        glGenTextures(1, &t->id);
        upload_texture(t->id, w, h, px);
    } else {
        t->id = 0;
    }
    t->width  = w;
    t->height = h;
    if (is_webp) WebPFree(px); else stbi_image_free(px);
    return t;
#endif
}

void* aiml_gl_create_texture(int32_t w, int32_t h, void* px_buf) {
    AimlGLTexture*  t   = new AimlGLTexture();
    t->width = w; t->height = h;
    glGenTextures(1, &t->id);
    if (px_buf) {
        AimlGLRawBuf* b = (AimlGLRawBuf*)px_buf;
        upload_texture(t->id, w, h, b->data);
    } else {
        glBindTexture(GL_TEXTURE_2D, t->id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    return t;
}

void* aiml_gl_create_texture_raw(int32_t w, int32_t h, const uint8_t* rgba) {
    AimlGLTexture* t = new AimlGLTexture();
    glGenTextures(1, &t->id);
    upload_texture(t->id, w, h, rgba);
    return t;
}

void aiml_gl_texture_set_wrap(void* tex, int32_t wrap_s, int32_t wrap_t) {
    if (!tex) return;
    glBindTexture(GL_TEXTURE_2D, ((AimlGLTexture*)tex)->id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLenum)wrap_s);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLenum)wrap_t);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void aiml_gl_texture_set_filter(void* tex, int32_t min_filter, int32_t mag_filter) {
    if (!tex) return;
    glBindTexture(GL_TEXTURE_2D, ((AimlGLTexture*)tex)->id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLenum)min_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLenum)mag_filter);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void aiml_gl_texture_gen_mipmaps(void* tex) {
    if (!tex) return;
    glBindTexture(GL_TEXTURE_2D, ((AimlGLTexture*)tex)->id);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void aiml_gl_texture_delete(void* tex) {
    if (!tex) return;
    AimlGLTexture* t = (AimlGLTexture*)tex;
    glDeleteTextures(1, &t->id);
    delete t;
}

/* spec/43_2 §5.5: owner drop / tex.delete(): deletes an owned texture, frees only a view block,
 * leaves a runtime-held texture alone. */
void aiml_gl_texture_release(void* tex) {
    if (!tex) return;
    AimlGLTexture* t = (AimlGLTexture*)tex;
    if (t->owned == 0) return;
    if (t->owned == 1) glDeleteTextures(1, &t->id);
    delete t;
}

/* The runtime (asset cache, preview) keeps the texture; the program's drop no longer deletes it. */
void aiml_gl_texture_disown(void* tex) {
    if (tex && ((AimlGLTexture*)tex)->owned == 1) ((AimlGLTexture*)tex)->owned = 0;
}

void aiml_gl_bind_texture(void* tex, int32_t slot) {
    if (!tex) return;
    glActiveTexture(GL_TEXTURE0 + (GLenum)slot);
    glBindTexture(GL_TEXTURE_2D, ((AimlGLTexture*)tex)->id);
}

/* ── Draw calls ─────────────────────────────────────────────────────────── */

void aiml_gl_draw_arrays(void* vao, int32_t count) {
    if (!vao) return;
    glBindVertexArray(((AimlGLVao*)vao)->id);
    glDrawArrays(GL_TRIANGLES, 0, count);
}

void aiml_gl_draw_elements(void* vao, int32_t count) {
    if (!vao) return;
    glBindVertexArray(((AimlGLVao*)vao)->id);
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
}

void aiml_gl_draw_lines(void* vao, int32_t count) {
    if (!vao) return;
    glBindVertexArray(((AimlGLVao*)vao)->id);
    glDrawElements(GL_LINES, count, GL_UNSIGNED_INT, nullptr);
}

void aiml_gl_draw_arrays_mode(void* vao, int32_t mode, int32_t count) {
    if (!vao) return;
    glBindVertexArray(((AimlGLVao*)vao)->id);
    glDrawArrays((GLenum)mode, 0, count);
}

/* ── Framebuffer ────────────────────────────────────────────────────────── */

void* aiml_gl_create_framebuffer(int32_t w, int32_t h) {
    AimlGLFramebuffer* fb = new AimlGLFramebuffer();
    fb->w = w; fb->h = h;

    /* Color attachment texture */
    glGenTextures(1, &fb->color_tex);
    glBindTexture(GL_TEXTURE_2D, fb->color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Depth renderbuffer */
    glGenRenderbuffers(1, &fb->depth_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, fb->depth_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    /* FBO */
    glGenFramebuffers(1, &fb->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, fb->color_tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, fb->depth_rbo);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[aiml_gl] framebuffer incomplete: 0x%x\n", status);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return fb;
}

void aiml_gl_bind_framebuffer(void* fbo) {
    if (!fbo) return;
    AimlGLFramebuffer* fb = (AimlGLFramebuffer*)fbo;
    glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
    glViewport(0, 0, fb->w, fb->h);
}

void aiml_gl_bind_default_framebuffer(void) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void* aiml_gl_framebuffer_color_texture(void* fbo) {
    if (!fbo) return nullptr;
    /* Return a lightweight AimlGLTexture wrapping the color_tex id.
     * Caller must NOT delete this — it is owned by the framebuffer. */
    AimlGLFramebuffer* fb = (AimlGLFramebuffer*)fbo;
    AimlGLTexture* t = new AimlGLTexture();
    t->id = fb->color_tex;
    t->width = fb->w; t->height = fb->h;
    t->owned = 2;
    return t;
}

void aiml_gl_framebuffer_resize(void* fbo, int32_t w, int32_t h) {
    if (!fbo) return;
    AimlGLFramebuffer* fb = (AimlGLFramebuffer*)fbo;
    fb->w = w; fb->h = h;

    glBindTexture(GL_TEXTURE_2D, fb->color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindRenderbuffer(GL_RENDERBUFFER, fb->depth_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void aiml_gl_framebuffer_delete(void* fbo) {
    if (!fbo) return;
    AimlGLFramebuffer* fb = (AimlGLFramebuffer*)fbo;
    glDeleteFramebuffers(1, &fb->fbo);
    glDeleteTextures(1, &fb->color_tex);
    glDeleteRenderbuffers(1, &fb->depth_rbo);
    delete fb;
}

/* ── gl_mesh ─────────────────────────────────────────────────────────────── */

static GLuint upload_vbo(const AimlGLRawBuf* buf) {
    if (!buf || !buf->data || buf->size <= 0) return 0;
    GLuint id = 0;
    glGenBuffers(1, &id);
    glBindBuffer(GL_ARRAY_BUFFER, id);
    glBufferData(GL_ARRAY_BUFFER, buf->size, buf->data, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return id;
}

void* aiml_gl_create_mesh(void* pos_buf, void* uvs_buf, void* normals_buf, void* idx_buf) {
    if (!pos_buf || !idx_buf) return nullptr;

    auto* p  = (AimlGLRawBuf*)pos_buf;
    auto* u  = (AimlGLRawBuf*)uvs_buf;
    auto* n  = (AimlGLRawBuf*)normals_buf;
    auto* ix = (AimlGLRawBuf*)idx_buf;

    AimlGLMesh* m = new AimlGLMesh();
    m->index_count = (ix->size > 0) ? (ix->size / (int32_t)sizeof(uint32_t)) : 0;

    /* Upload VBOs before binding VAO so GL_ARRAY_BUFFER state is independent */
    m->vbo_pos  = upload_vbo(p);
    m->vbo_uv   = u  ? upload_vbo(u) : 0;
    m->vbo_norm = n  ? upload_vbo(n) : 0;

    /* IBO */
    glGenBuffers(1, &m->ibo);

    /* VAO */
    glGenVertexArrays(1, &m->vao);
    glBindVertexArray(m->vao);

    /* Bind IBO inside VAO so it's remembered */
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, ix->size, ix->data, GL_STATIC_DRAW);

    /* Attrib 0: position (xyz, 3×f32, stride 12) */
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo_pos);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0);

    /* Attrib 1: UV (uv, 2×f32, stride 8) */
    if (m->vbo_uv) {
        glBindBuffer(GL_ARRAY_BUFFER, m->vbo_uv);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8, (void*)0);
    }

    /* Attrib 2: normal (xyz, 3×f32, stride 12) */
    if (m->vbo_norm) {
        glBindBuffer(GL_ARRAY_BUFFER, m->vbo_norm);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 12, (void*)0);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return m;
}

void aiml_gl_mesh_draw(void* mesh) {
    if (!mesh) return;
    auto* m = (AimlGLMesh*)mesh;
    glBindVertexArray(m->vao);
    glDrawElements(GL_TRIANGLES, m->index_count, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void aiml_gl_mesh_draw_count(void* mesh, int32_t count) {
    if (!mesh) return;
    auto* m = (AimlGLMesh*)mesh;
    glBindVertexArray(m->vao);
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

int32_t aiml_gl_mesh_index_count(void* mesh) {
    return mesh ? ((AimlGLMesh*)mesh)->index_count : 0;
}

/* spec/43_2 §3.4: attrib 3 = tangent (xyzw, 4×f32, stride 16), e.g. glTF TANGENT */
void aiml_gl_mesh_set_tangents(void* mesh, void* tan_buf) {
    auto* m = (AimlGLMesh*)mesh;
    auto* t = (AimlGLRawBuf*)tan_buf;
    if (!m || !t || !t->data || t->size <= 0) return;
    if (m->vbo_tan) glDeleteBuffers(1, &m->vbo_tan);
    m->vbo_tan = upload_vbo(t);
    glBindVertexArray(m->vao);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo_tan);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 16, (void*)0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

/* spec/43_2 §3.4: attrib 4 = joint indices, attrib 5 = weights (4×f32 each, stride 16), glTF skin */
void aiml_gl_mesh_set_skin(void* mesh, void* joint_buf, void* weight_buf) {
    auto* m = (AimlGLMesh*)mesh;
    auto* j = (AimlGLRawBuf*)joint_buf;
    auto* w = (AimlGLRawBuf*)weight_buf;
    if (!m || !j || !w || !j->data || !w->data || j->size <= 0 || j->size != w->size) return;
    if (m->vbo_joint)  glDeleteBuffers(1, &m->vbo_joint);
    if (m->vbo_weight) glDeleteBuffers(1, &m->vbo_weight);
    m->vbo_joint  = upload_vbo(j);
    m->vbo_weight = upload_vbo(w);
    glBindVertexArray(m->vao);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo_joint);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, 16, (void*)0);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo_weight);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, 16, (void*)0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void aiml_gl_mesh_delete(void* mesh) {
    if (!mesh) return;
    auto* m = (AimlGLMesh*)mesh;
    glDeleteVertexArrays(1, &m->vao);
    glDeleteBuffers(1, &m->ibo);
    if (m->vbo_pos)  glDeleteBuffers(1, &m->vbo_pos);
    if (m->vbo_uv)   glDeleteBuffers(1, &m->vbo_uv);
    if (m->vbo_norm) glDeleteBuffers(1, &m->vbo_norm);
    if (m->vbo_tan)  glDeleteBuffers(1, &m->vbo_tan);
    if (m->vbo_joint)  glDeleteBuffers(1, &m->vbo_joint);
    if (m->vbo_weight) glDeleteBuffers(1, &m->vbo_weight);
    delete m;
}

/* ── spec/43_5: explicit formats, HDR targets, state, capabilities, readback ── */

#ifndef GL_FRAMEBUFFER_SRGB
#  define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif

static void gl_clear_errors() { while (glGetError() != GL_NO_ERROR) {} }

static int32_t gl_max_texture_size() {
    GLint v = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v);
    return v;
}

static int32_t mip_levels(int32_t w, int32_t h) {
    int32_t m = w > h ? w : h, n = 1;
    while (m > 1) { m >>= 1; ++n; }
    return n;
}

/* Validates dims and the exact element count of `buf` (comp bytes per channel, 4 channels). */
static bool tex_input_ok(const char* fn, int32_t w, int32_t h, const AimlGLRawBuf* b, int32_t comp) {
    if (w <= 0 || h <= 0 || w > gl_max_texture_size() || h > gl_max_texture_size()) {
        fprintf(stderr, "[aiml_gl] %s: invalid size %dx%d\n", fn, w, h);
        return false;
    }
    int64_t need = (int64_t)w * h * 4 * comp;
    if (!b || !b->data || (int64_t)b->size != need) {
        fprintf(stderr, "[aiml_gl] %s: expected %lld bytes, got %d\n", fn,
                (long long)need, b ? b->size : -1);
        return false;
    }
    return true;
}

static AimlGLTexture* tex_alloc(int32_t w, int32_t h, GLenum ifmt, GLenum type,
                                const void* px, bool mipmaps) {
    GLint prev = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
    gl_clear_errors();
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)ifmt, w, h, 0, GL_RGBA, type, px);
    if (mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!mipmaps) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    GLenum err = glGetError();
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "[aiml_gl] texture upload failed: GL error 0x%x\n", err);
        glDeleteTextures(1, &id);
        return nullptr;
    }
    AimlGLTexture* t = new AimlGLTexture();
    t->id = id; t->width = w; t->height = h; t->format = ifmt;
    t->levels = mipmaps ? mip_levels(w, h) : 1;
    return t;
}

void* aiml_gl_create_texture_format(int32_t w, int32_t h, void* px_buf, int32_t format, int32_t mipmaps) {
    if (format != GL_RGBA8 && format != GL_SRGB8_ALPHA8) {
        fprintf(stderr, "[aiml_gl] create_texture_format: unsupported format 0x%x\n", format);
        return nullptr;
    }
    auto* b = (AimlGLRawBuf*)px_buf;
    if (!tex_input_ok("create_texture_format", w, h, b, 1)) return nullptr;
    return tex_alloc(w, h, (GLenum)format, GL_UNSIGNED_BYTE, b->data, mipmaps != 0);
}

void* aiml_gl_create_texture_f32(int32_t w, int32_t h, void* px_buf, int32_t mipmaps) {
    auto* b = (AimlGLRawBuf*)px_buf;
    if (!tex_input_ok("create_texture_f32", w, h, b, 4)) return nullptr;
    const float* f = (const float*)b->data;
    for (int64_t i = 0, n = (int64_t)w * h * 4; i < n; ++i)
        if (!std::isfinite(f[i])) {
            fprintf(stderr, "[aiml_gl] create_texture_f32: non-finite component %lld\n", (long long)i);
            return nullptr;
        }
    return tex_alloc(w, h, GL_RGBA16F, GL_FLOAT, f, mipmaps != 0);
}

int32_t aiml_gl_texture_width (void* tex) { return tex ? ((AimlGLTexture*)tex)->width : 0; }
int32_t aiml_gl_texture_height(void* tex) { return tex ? ((AimlGLTexture*)tex)->height : 0; }
int32_t aiml_gl_texture_format(void* tex) {
    if (!tex) return 0;
    auto* t = (AimlGLTexture*)tex;
    return t->format ? (int32_t)t->format : (t->id ? GL_RGBA8 : 0);
}
int32_t aiml_gl_texture_levels(void* tex) { return tex ? ((AimlGLTexture*)tex)->levels : 0; }

/* Allocates color (+ optional depth) storage for fb's current ids; false + GL objects intact on error. */
static bool fb_storage(GLuint color, GLuint depth, int32_t w, int32_t h, GLenum fmt) {
    gl_clear_errors();
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)fmt, w, h, 0, GL_RGBA,
                 fmt == GL_RGBA16F ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    if (depth) {
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }
    return glGetError() == GL_NO_ERROR;
}

struct GlBindSave {
    GLint tex = 0, draw = 0, read = 0;
    GlBindSave() {
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    }
    ~GlBindSave() {
        glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
    }
};

/* Builds a complete fbo for (w,h,fmt,depth); all-or-nothing. */
static bool fb_build(AimlGLFramebuffer* fb, int32_t w, int32_t h) {
    GLuint color = 0, depth = 0, fbo = 0;
    glGenTextures(1, &color);
    if (fb->has_depth) glGenRenderbuffers(1, &depth);
    bool ok = fb_storage(color, depth, w, h, fb->format);
    if (ok) {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        if (depth)
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (st != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "[aiml_gl] framebuffer incomplete: 0x%x\n", st);
            ok = false;
        }
    }
    if (!ok) {
        if (fbo) glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &color);
        if (depth) glDeleteRenderbuffers(1, &depth);
        return false;
    }
    fb->fbo = fbo; fb->color_tex = color; fb->depth_rbo = depth; fb->w = w; fb->h = h;
    return true;
int32_t aiml_gl_mesh_set_attribute(void* mesh, int32_t location, int32_t components, void* data_buf) {
    auto* m = (AimlGLMesh*)mesh;
    auto* d = (AimlGLRawBuf*)data_buf;
    if (!m || !d || !d->data || d->size <= 0) return 0;
    if (location < 6 || location > 15 || components < 1 || components > 4) return 0;
    if (d->size % (components * 4) != 0) return 0;
    int slot = -1;
    for (int i = 0; i < 4; ++i) if (m->vbo_extra[i] && m->extra_loc[i] == location) slot = i;
    if (slot < 0) for (int i = 0; i < 4 && slot < 0; ++i) if (!m->vbo_extra[i]) slot = i;
    if (slot < 0) return 0;
    GLuint vbo = upload_vbo(d);
    if (!vbo) return 0;
    if (m->vbo_extra[slot]) glDeleteBuffers(1, &m->vbo_extra[slot]);
    m->vbo_extra[slot] = vbo;
    m->extra_loc[slot] = location;
    GLint prev_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
    glBindVertexArray(m->vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray((GLuint)location);
    glVertexAttribPointer((GLuint)location, components, GL_FLOAT, GL_FALSE, components * 4, (void*)0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray((GLuint)prev_vao);
    return 1;
}

}

void* aiml_gl_create_framebuffer_format(int32_t w, int32_t h, int32_t format, int32_t depth) {
    for (int i = 0; i < 4; ++i) if (m->vbo_extra[i]) glDeleteBuffers(1, &m->vbo_extra[i]);
    if (format != GL_RGBA8 && format != GL_RGBA16F) {
        fprintf(stderr, "[aiml_gl] create_framebuffer_format: unsupported format 0x%x\n", format);
        return nullptr;
    }
    if (w <= 0 || h <= 0 || w > gl_max_texture_size() || h > gl_max_texture_size()) {
        fprintf(stderr, "[aiml_gl] create_framebuffer_format: invalid size %dx%d\n", w, h);
        return nullptr;
    }
    GlBindSave save;
    AimlGLFramebuffer* fb = new AimlGLFramebuffer();
    fb->format = (GLenum)format;
    fb->has_depth = depth != 0;
    if (!fb_build(fb, w, h)) { delete fb; return nullptr; }
    return fb;
}

void aiml_gl_framebuffer_bind_color_texture(void* fbo, int32_t unit) {
    if (!fbo) return;
    auto* fb = (AimlGLFramebuffer*)fbo;
    GLint draw = 0, units = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
    if ((GLuint)draw == fb->fbo) {
        fprintf(stderr, "[aiml_gl] bind_color_texture: framebuffer is the current draw target\n");
        return;
    }
    if (unit < 0 || unit >= units) {
        fprintf(stderr, "[aiml_gl] bind_color_texture: unit %d out of range\n", unit);
        return;
    }
    glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
    glBindTexture(GL_TEXTURE_2D, fb->color_tex);
}

int32_t aiml_gl_framebuffer_resize_checked(void* fbo, int32_t w, int32_t h) {
    if (!fbo) return 0;
    auto* fb = (AimlGLFramebuffer*)fbo;
    if (w <= 0 || h <= 0 || w > gl_max_texture_size() || h > gl_max_texture_size()) return 0;
    GlBindSave save;
    AimlGLFramebuffer fresh = *fb;
    if (!fb_build(&fresh, w, h)) return 0;
    glDeleteFramebuffers(1, &fb->fbo);
    glDeleteTextures(1, &fb->color_tex);
    if (fb->depth_rbo) glDeleteRenderbuffers(1, &fb->depth_rbo);
    *fb = fresh;
    return 1;
}

int32_t aiml_gl_framebuffer_width (void* fbo) { return fbo ? ((AimlGLFramebuffer*)fbo)->w : 0; }
int32_t aiml_gl_framebuffer_height(void* fbo) { return fbo ? ((AimlGLFramebuffer*)fbo)->h : 0; }
int32_t aiml_gl_framebuffer_format(void* fbo) { return fbo ? (int32_t)((AimlGLFramebuffer*)fbo)->format : 0; }

void aiml_gl_depth_write(int32_t on) { glDepthMask(on ? GL_TRUE : GL_FALSE); }
void aiml_gl_depth_func(int32_t mode) {
    if (mode != GL_LESS && mode != GL_LEQUAL) {
        fprintf(stderr, "[aiml_gl] depth_func: unsupported mode 0x%x\n", mode);
        return;
    }
    glDepthFunc((GLenum)mode);
}
void aiml_gl_front_face(int32_t mode) {
    if (mode != GL_CW && mode != GL_CCW) {
        fprintf(stderr, "[aiml_gl] front_face: unsupported mode 0x%x\n", mode);
        return;
    }
    glFrontFace((GLenum)mode);
}
static bool blend_factor_ok(int32_t f) {
    return f == GL_ZERO || f == GL_ONE || f == GL_SRC_ALPHA || f == GL_ONE_MINUS_SRC_ALPHA;
}
void aiml_gl_blend_func_separate(int32_t src_rgb, int32_t dst_rgb, int32_t src_a, int32_t dst_a) {
    if (!blend_factor_ok(src_rgb) || !blend_factor_ok(dst_rgb) ||
        !blend_factor_ok(src_a) || !blend_factor_ok(dst_a)) {
        fprintf(stderr, "[aiml_gl] blend_func_separate: unsupported factor\n");
        return;
    }
    glBlendFuncSeparate((GLenum)src_rgb, (GLenum)dst_rgb, (GLenum)src_a, (GLenum)dst_a);
}
int32_t aiml_gl_framebuffer_srgb(int32_t on) {
#ifdef __EMSCRIPTEN__
    return on ? 0 : 1;
#else
    if (on) glEnable(GL_FRAMEBUFFER_SRGB); else glDisable(GL_FRAMEBUFFER_SRGB);
    return 1;
#endif
}

int32_t aiml_gl_shader_uniform_vec3_array(void* shader, const char* name, void* buf, int32_t count) {
    if (!shader || !name || count < 0) {
        fprintf(stderr, "[aiml_gl] uniform_vec3_array: invalid arguments\n");
        return 0;
    }
    if (count == 0) return 1;
    auto* b = (AimlGLRawBuf*)buf;
    if (!b || !b->data || (int64_t)b->size < (int64_t)count * 3 * (int64_t)sizeof(float)) {
        fprintf(stderr, "[aiml_gl] uniform_vec3_array '%s': buffer too short for %d\n", name, count);
        return 0;
    }
    GLint loc = ((AimlGLShader*)shader)->location(name);
    if (loc >= 0) glUniform3fv(loc, count, (const float*)b->data);
    return 1;
}

/* Probes a complete RGBA16F target once per context. */
static int32_t probe_rgba16f_renderable() {
    static void* probed_ctx = nullptr;
    static int32_t result = 0;
#ifndef AIML_WASM_BUILD
    void* cur = glfwGetCurrentContext();
#else
    void* cur = (void*)1;
#endif
    if (cur == probed_ctx) return result;
    GlBindSave save;
    AimlGLFramebuffer fb;
    fb.format = GL_RGBA16F; fb.has_depth = false;
    result = fb_build(&fb, 4, 4) ? 1 : 0;
    if (result) { glDeleteFramebuffers(1, &fb.fbo); glDeleteTextures(1, &fb.color_tex); }
    probed_ctx = cur;
    return result;
}

static bool has_gl_extension(const char* name) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i) {
        const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (e && !strcmp(e, name)) return true;
    }
    return false;
}

int32_t aiml_gl_supports(const char* feature) {
    if (!feature) return 0;
#ifdef __EMSCRIPTEN__
    const bool desktop = false;
#else
    const bool desktop = true;
#endif
    if (!strcmp(feature, "srgb_texture"))       return 1;
    if (!strcmp(feature, "rgba16f_renderable")) return probe_rgba16f_renderable();
    if (!strcmp(feature, "rgba16f_filterable")) return desktop || has_gl_extension("OES_texture_float_linear") || has_gl_extension("GL_OES_texture_float_linear") ? 1 : 0;
    if (!strcmp(feature, "cubemap_float"))      return 1;
    if (!strcmp(feature, "timer_query"))        return desktop ? 1 : 0;   // WebGL disjoint-timer path not implemented
    if (!strcmp(feature, "framebuffer_srgb"))   return desktop ? 1 : 0;
    fprintf(stderr, "[aiml_gl] supports: unknown feature '%s'\n", feature);
    return 0;
}

int32_t aiml_gl_limit(const char* name) {
    GLint v = -1;
    if (!name) return -1;
    if      (!strcmp(name, "max_texture_size"))            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v);
    else if (!strcmp(name, "max_cube_size"))               glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &v);
    else if (!strcmp(name, "max_fragment_texture_units"))  glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &v);
    else if (!strcmp(name, "max_vertex_uniform_components")) glGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &v);
    else fprintf(stderr, "[aiml_gl] limit: unknown name '%s'\n", name);
    return v;
}

/* Pixel size of the bound read framebuffer (attachment 0 or the default drawable). */
static void read_target_size(int32_t* w, int32_t* h) {
    GLint rfb = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &rfb);
#ifndef AIML_WASM_BUILD
    if (rfb == 0) {
        int fw = 0, fh = 0;
        if (void* win = glfwGetCurrentContext()) glfwGetFramebufferSize(win, &fw, &fh);
        *w = fw; *h = fh;
        return;
    }
    GLint type = 0, name = 0;
    glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
    if (type == GL_TEXTURE) {
        GLint prev = 0, tw = 0, th = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
        glBindTexture(GL_TEXTURE_2D, (GLuint)name);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
        glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
        *w = tw; *h = th;
        return;
    }
#endif
    GLint vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, vp);
    *w = vp[0] + vp[2]; *h = vp[1] + vp[3];
}

/* Owned buffer<u8> of w*h*4 bytes, top row first; empty on invalid input. */
void* aiml_gl_read_rgba8(int32_t x, int32_t y, int32_t w, int32_t h) {
    auto* out = (AimlGLRawBuf*)calloc(1, sizeof(AimlGLRawBuf));
    int32_t lim_w = 0, lim_h = 0;
    read_target_size(&lim_w, &lim_h);
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || (int64_t)x + w > lim_w || (int64_t)y + h > lim_h) {
        fprintf(stderr, "[aiml_gl] read_rgba8: rectangle %d,%d %dx%d outside %dx%d\n", x, y, w, h, lim_w, lim_h);
        return out;
    }
    size_t row = (size_t)w * 4, n = row * (size_t)h;
    uint8_t* px = (uint8_t*)malloc(n);
    uint8_t* tmp = (uint8_t*)malloc(row);
    if (!px || !tmp) { free(px); free(tmp); return out; }
    GLint pack = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glPixelStorei(GL_PACK_ALIGNMENT, pack);
    for (int32_t r = 0; r < h / 2; ++r) {
        memcpy(tmp, px + (size_t)r * row, row);
        memcpy(px + (size_t)r * row, px + (size_t)(h - 1 - r) * row, row);
        memcpy(px + (size_t)(h - 1 - r) * row, tmp, row);
    }
    free(tmp);
    out->data = px; out->size = (int32_t)n; out->capacity = (int32_t)n; out->write_idx = (int32_t)n;
    return out;
}

/* ── spec/43_5 §5: cubemaps (RGBA16F, Class 4) ──────────────────────────── */

#ifndef GL_TEXTURE_CUBE_MAP_SEAMLESS
#  define GL_TEXTURE_CUBE_MAP_SEAMLESS 0x884F
#endif

struct AimlGLCubemap {
    GLuint id;
    int    size;
    int    levels;
};

static GLenum cube_face_target(int32_t face) {
    if (face >= 0 && face < 6) return GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face;
    if (face >= (int32_t)GL_TEXTURE_CUBE_MAP_POSITIVE_X && face <= (int32_t)GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
        return (GLenum)face;
    return 0;
}

void* aiml_gl_create_cubemap(int32_t size, int32_t levels) {
    GLint max_cube = 0;
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &max_cube);
    if (size <= 0 || size > max_cube || levels < 1 || levels > mip_levels(size, size)) {
        fprintf(stderr, "[aiml_gl] create_cubemap: invalid size %d / levels %d\n", size, levels);
        return nullptr;
    }
    GLint prev = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &prev);
    gl_clear_errors();
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id);
    std::vector<float> zeros((size_t)size * size * 4, 0.0f);
    for (int lvl = 0; lvl < levels; ++lvl) {
        int dim = size >> lvl; if (dim < 1) dim = 1;
        for (int f = 0; f < 6; ++f)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, lvl, GL_RGBA16F, dim, dim, 0, GL_RGBA, GL_FLOAT, zeros.data());
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, levels - 1);
#ifndef __EMSCRIPTEN__
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
#endif
    GLenum err = glGetError();
    glBindTexture(GL_TEXTURE_CUBE_MAP, (GLuint)prev);
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "[aiml_gl] create_cubemap failed: GL error 0x%x\n", err);
        glDeleteTextures(1, &id);
        return nullptr;
    }
    AimlGLCubemap* c = new AimlGLCubemap();
    c->id = id; c->size = size; c->levels = levels;
    return c;
}

int32_t aiml_gl_cubemap_upload_face(void* cube, int32_t face, int32_t level, void* px_buf) {
    auto* c = (AimlGLCubemap*)cube;
    GLenum target = cube_face_target(face);
    if (!c || !target || level < 0 || level >= c->levels) {
        fprintf(stderr, "[aiml_gl] upload_face: invalid face %d / level %d\n", face, level);
        return 0;
    }
    int dim = c->size >> level; if (dim < 1) dim = 1;
    auto* b = (AimlGLRawBuf*)px_buf;
    int64_t need = (int64_t)dim * dim * 4 * (int64_t)sizeof(float);
    if (!b || !b->data || (int64_t)b->size != need) {
        fprintf(stderr, "[aiml_gl] upload_face: expected %lld bytes, got %d\n", (long long)need, b ? b->size : -1);
        return 0;
    }
    const float* f = (const float*)b->data;
    for (int64_t i = 0; i < (int64_t)dim * dim * 4; ++i)
        if (!std::isfinite(f[i])) { fprintf(stderr, "[aiml_gl] upload_face: non-finite value\n"); return 0; }
    GLint prev = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &prev);
    gl_clear_errors();
    glBindTexture(GL_TEXTURE_CUBE_MAP, c->id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(target, level, 0, 0, dim, dim, GL_RGBA, GL_FLOAT, f);
    GLenum err = glGetError();
    glBindTexture(GL_TEXTURE_CUBE_MAP, (GLuint)prev);
    return err == GL_NO_ERROR ? 1 : 0;
}

void aiml_gl_bind_cubemap(void* cube, int32_t unit) {
    if (!cube) return;
    GLint units = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
    if (unit < 0 || unit >= units) {
        fprintf(stderr, "[aiml_gl] bind_cubemap: unit %d out of range\n", unit);
        return;
    }
    glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, ((AimlGLCubemap*)cube)->id);
}

/* Owner drop / cube.delete(): null-safe single close. */
#ifndef __EMSCRIPTEN__
// spec/43_5 §9: TIME_ELAPSED queries; one active at a time, results never waited for.
struct AimlGLTimerQuery { GLuint id; int state; int64_t result; };   // state 0 idle, 1 active, 2 pending, 3 done
static AimlGLTimerQuery* g_active_timer = nullptr;

void* aiml_gl_create_timer_query(void) {
    if (!aiml_gl_supports("timer_query")) return nullptr;
    auto* q = new AimlGLTimerQuery{0, 0, -1};
    glGenQueries(1, &q->id);
    if (!q->id) { delete q; return nullptr; }
    return q;
}

int32_t aiml_gl_timer_query_begin(void* query) {
    auto* q = (AimlGLTimerQuery*)query;
    if (!q || g_active_timer || q->state == 2) return 0;
    glBeginQuery(GL_TIME_ELAPSED, q->id);
    q->state = 1; q->result = -1;
    g_active_timer = q;
    return 1;
}

int32_t aiml_gl_timer_query_end(void* query) {
    auto* q = (AimlGLTimerQuery*)query;
    if (!q || q->state != 1 || g_active_timer != q) return 0;
    glEndQuery(GL_TIME_ELAPSED);
    q->state = 2;
    g_active_timer = nullptr;
    return 1;
}

int32_t aiml_gl_timer_query_ready(void* query) {
    auto* q = (AimlGLTimerQuery*)query;
    if (!q || q->state == 0 || q->state == 1) return 0;
    if (q->state == 3) return 1;
    GLint avail = 0;
    glGetQueryObjectiv(q->id, GL_QUERY_RESULT_AVAILABLE, &avail);
    if (!avail) return 0;
    GLuint64 ns = 0;
    glGetQueryObjectui64v(q->id, GL_QUERY_RESULT, &ns);
    q->result = (int64_t)ns;
    q->state = 3;
    return 1;
}

int64_t aiml_gl_timer_query_result_ns(void* query) {
    auto* q = (AimlGLTimerQuery*)query;
    if (!q || !aiml_gl_timer_query_ready(q)) return -1;
    return q->result;
}

void aiml_gl_timer_query_release(void* query) {
    if (!query) return;
    auto* q = (AimlGLTimerQuery*)query;
    if (g_active_timer == q) { glEndQuery(GL_TIME_ELAPSED); g_active_timer = nullptr; }
    glDeleteQueries(1, &q->id);
    delete q;
}
#else
void* aiml_gl_create_timer_query(void) { return nullptr; }
int32_t aiml_gl_timer_query_begin(void*) { return 0; }
int32_t aiml_gl_timer_query_end(void*) { return 0; }
int32_t aiml_gl_timer_query_ready(void*) { return 0; }
int64_t aiml_gl_timer_query_result_ns(void*) { return -1; }
void aiml_gl_timer_query_release(void*) {}
#endif

void aiml_gl_cubemap_release(void* cube) {
    if (!cube) return;
    auto* c = (AimlGLCubemap*)cube;
    glDeleteTextures(1, &c->id);
    delete c;
}
