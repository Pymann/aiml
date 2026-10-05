#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Initialisation ────────────────────────────────────────────────────────── */

/* Must be called once after a valid GL context is current (GLFW/SDL). */
void aiml_gl_init(void);

/* ── Render state ──────────────────────────────────────────────────────────── */

void aiml_gl_viewport(int32_t x, int32_t y, int32_t w, int32_t h);
void aiml_gl_clear(float r, float g, float b, float a);
void aiml_gl_clear_depth(void);
void aiml_gl_clear_all(float r, float g, float b, float a);
void aiml_gl_enable_depth_test(void);
void aiml_gl_disable_depth_test(void);
void aiml_gl_enable_blend(void);
void aiml_gl_disable_blend(void);
void aiml_gl_enable_cull_face(void);
void aiml_gl_disable_cull_face(void);
void aiml_gl_cull_back(void);
void aiml_gl_cull_front(void);
void aiml_gl_wireframe(int8_t on);
void aiml_gl_polygon_offset(float factor, float units);
void aiml_gl_enable_polygon_offset_fill(void);
void aiml_gl_disable_polygon_offset_fill(void);

/* ── Buffer (VBO / IBO) ────────────────────────────────────────────────────── */

/* data_buf: AimlGltfBuffer* { i32 size(bytes), …, u8* data } — same layout as AIML buffer<T> */
void* aiml_gl_create_buffer        (void* data_buf);  /* GL_STATIC_DRAW  */
void* aiml_gl_create_buffer_dynamic(void* data_buf);  /* GL_DYNAMIC_DRAW */
void* aiml_gl_create_index_buffer  (void* data_buf);  /* IBO, GL_STATIC_DRAW */

void  aiml_gl_buffer_update      (void* buf, void* data_buf);
void  aiml_gl_buffer_delete      (void* buf);

/* ── VAO ───────────────────────────────────────────────────────────────────── */

void* aiml_gl_create_vao(void);
void  aiml_gl_vao_bind_vbo (void* vao, void* vbo);
void  aiml_gl_vao_bind_ibo (void* vao, void* ibo);
void  aiml_gl_vao_attrib   (void* vao, int32_t idx, int32_t components,
                             int32_t stride, int32_t offset);
void  aiml_gl_vao_attrib_i (void* vao, int32_t idx, int32_t components,
                             int32_t stride, int32_t offset);
void  aiml_gl_vao_delete   (void* vao);

/* ── Shader ────────────────────────────────────────────────────────────────── */

void* aiml_gl_compile_shader(const char* vert_src, const char* frag_src);
void* aiml_gl_load_shader   (const char* vert_path, const char* frag_path);
void  aiml_gl_shader_use    (void* shader);

void  aiml_gl_shader_uniform_f32       (void* shader, const char* name, float v);
void  aiml_gl_shader_uniform_i32       (void* shader, const char* name, int32_t v);
void  aiml_gl_shader_uniform_vec2      (void* shader, const char* name, float* v);
void  aiml_gl_shader_uniform_vec3      (void* shader, const char* name, float* v);
void  aiml_gl_shader_uniform_vec4      (void* shader, const char* name, float* v);
void  aiml_gl_shader_uniform_mat4      (void* shader, const char* name, float* m);
/* buf: AimlGltfBuffer* holding count*16 floats */
void  aiml_gl_shader_uniform_mat4_array(void* shader, const char* name,
                                        void* buf, int32_t count);
void  aiml_gl_shader_delete(void* shader);

/* ── Texture ───────────────────────────────────────────────────────────────── */

/* Load from file (stb_image: PNG/JPG/…) */
void* aiml_gl_load_texture   (const char* path);
/* Create from raw RGBA pixel buffer (w*h*4 bytes) */
void* aiml_gl_create_texture    (int32_t w, int32_t h, void* px_buf);
/* Create directly from a raw stbi_uc* RGBA pointer (no buffer wrapper needed) */
void* aiml_gl_create_texture_raw(int32_t w, int32_t h, const uint8_t* rgba);

void  aiml_gl_texture_set_wrap   (void* tex, int32_t wrap_s, int32_t wrap_t);
void  aiml_gl_texture_set_filter (void* tex, int32_t min_filter, int32_t mag_filter);
void  aiml_gl_texture_gen_mipmaps(void* tex);
void  aiml_gl_texture_delete     (void* tex);

/* Bind texture to a texture unit slot (for shader samplers) */
void aiml_gl_bind_texture(void* tex, int32_t slot);

/* ── gl_mesh — bundled VAO + VBOs + IBO ────────────────────────────────────── */

/*
 * gl.create_mesh(pos, uvs, normals, indices) — Standard layout:
 *   Attrib 0: position  (xyz)  3×f32, stride 12
 *   Attrib 1: uv        (uv)   2×f32, stride  8
 *   Attrib 2: normal    (xyz)  3×f32, stride 12  (pass NULL buf for no normals)
 *
 * pos / uvs / normals / indices are AimlGltfBuffer* (same layout as buffer<T>).
 * normals_buf may be NULL — attrib 2 is skipped in that case.
 */
void* aiml_gl_create_mesh(void* pos_buf, void* uvs_buf, void* normals_buf, void* idx_buf);
void  aiml_gl_mesh_set_tangents(void* mesh, void* tan_buf);
void  aiml_gl_mesh_set_skin(void* mesh, void* joint_buf, void* weight_buf);

void    aiml_gl_mesh_draw       (void* mesh);               /* GL_TRIANGLES, all indices */
void    aiml_gl_mesh_draw_count (void* mesh, int32_t count);/* GL_TRIANGLES, count indices */
int32_t aiml_gl_mesh_index_count(void* mesh);
void    aiml_gl_mesh_delete     (void* mesh);

/* ── Draw calls ────────────────────────────────────────────────────────────── */

void aiml_gl_draw_arrays      (void* vao, int32_t count);          /* GL_TRIANGLES */
void aiml_gl_draw_elements    (void* vao, int32_t count);          /* GL_TRIANGLES, GL_UNSIGNED_INT */
void aiml_gl_draw_lines       (void* vao, int32_t count);          /* GL_LINES,     GL_UNSIGNED_INT */
void aiml_gl_draw_arrays_mode (void* vao, int32_t mode, int32_t count);

/* ── Framebuffer ───────────────────────────────────────────────────────────── */

void* aiml_gl_create_framebuffer    (int32_t w, int32_t h);
void  aiml_gl_bind_framebuffer      (void* fbo);
void  aiml_gl_bind_default_framebuffer(void);
/* Returns AimlGLTexture* for the color attachment (do NOT delete separately) */
void* aiml_gl_framebuffer_color_texture(void* fbo);
void  aiml_gl_framebuffer_resize    (void* fbo, int32_t w, int32_t h);
void  aiml_gl_framebuffer_delete    (void* fbo);

#ifdef __cplusplus
}
#endif
