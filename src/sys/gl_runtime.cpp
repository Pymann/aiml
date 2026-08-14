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
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <string>

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
};

struct AimlGLFramebuffer {
    GLuint fbo;
    GLuint color_tex;
    GLuint depth_rbo;
    int    w, h;
};

/* gl_mesh: VAO + up to 3 VBOs (pos, uv, normals) + IBO */
struct AimlGLMesh {
    GLuint vao;
    GLuint vbo_pos;
    GLuint vbo_uv;
    GLuint vbo_norm;   /* 0 if no normals */
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
    if (!shader || !buf || count <= 0) return;
    AimlGLRawBuf* b = (AimlGLRawBuf*)buf;
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
static inline bool aiml_gl_has_context() { return glfwGetCurrentContext() != nullptr; }
#else
static inline bool aiml_gl_has_context() { return true; }
#endif

void* aiml_gl_load_texture(const char* path) {
    if (!path) return nullptr;
    stbi_set_flip_vertically_on_load(1);
    int w, h, ch;
    uint8_t* px = stbi_load(path, &w, &h, &ch, 4);
    if (!px) {
        fprintf(stderr, "[aiml_gl] load_texture: cannot load %s\n", path);
        return nullptr;
    }
    AimlGLTexture* t = new AimlGLTexture();
    if (aiml_gl_has_context()) {
        glGenTextures(1, &t->id);
        upload_texture(t->id, w, h, px);
    } else {
        t->id = 0;
    }
    t->width  = w;
    t->height = h;
    stbi_image_free(px);
    return t;
}

void* aiml_gl_create_texture(int32_t w, int32_t h, void* px_buf) {
    AimlGLTexture*  t   = new AimlGLTexture();
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

void aiml_gl_mesh_delete(void* mesh) {
    if (!mesh) return;
    auto* m = (AimlGLMesh*)mesh;
    glDeleteVertexArrays(1, &m->vao);
    glDeleteBuffers(1, &m->ibo);
    if (m->vbo_pos)  glDeleteBuffers(1, &m->vbo_pos);
    if (m->vbo_uv)   glDeleteBuffers(1, &m->vbo_uv);
    if (m->vbo_norm) glDeleteBuffers(1, &m->vbo_norm);
    delete m;
}
