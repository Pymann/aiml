/*
 * prefab_runtime.cpp — AIML prefab system (scene graph + sprite rendering)
 *
 * Provides reusable visual entity templates (prefabs) with:
 *   - Hierarchical children (sprite, label, rect)
 *   - Scene graph with z_order sorted rendering
 *   - AABB hit-testing (reverse z_order)
 *   - Built-in drag-and-drop
 *   - Text rendering via stb_truetype (reuses AimlUIFont from ui_runtime)
 *
 * Dependencies: GL / GLEW, stb_truetype (via ui_runtime), stb_image (via gl_runtime)
 */

#ifdef __EMSCRIPTEN__
#  include <GLES3/gl3.h>
#  include <GLES2/gl2ext.h>
#else
#  include <GL/glew.h>
#  include <GL/gl.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include <stb/stb_truetype.h>  /* header-only, no IMPLEMENTATION (ui_runtime owns it) */

#include "prefab_runtime.h"
#include "ui_runtime.h"
#include "gl_runtime.h"

/* ── Forward declarations from other runtimes ────────────────────────────── */
extern "C" int32_t aiml_input_mouse_pressed(int32_t btn);

/* Forward declarations for material helpers used by flush_batch and the
 * instantiate path. Definitions live further down in this translation
 * unit, after parse_json_* helpers. */
struct Material;
static GLuint    material_get_or_compile(const char* shader_path);
static void      material_apply_uniforms(Material* m, GLuint prog);
static Material* material_find(const char* name);
static void      register_material_def(const char* name, const char* shader_path,
                                       const char* uniforms_json);
extern "C" int32_t aiml_input_mouse_released(int32_t btn);
extern "C" int32_t aiml_input_mouse_button(int32_t btn);
extern "C" int32_t aiml_input_has_event(void);
extern "C" void*   aiml_input_next_event(void);
extern "C" const char* aiml_input_event_type(void* ev);
extern "C" const char* aiml_input_event_key(void* ev);
extern "C" const char* aiml_input_event_char_(void* ev);

/* ── External struct layouts (from gl_runtime / ui_runtime) ──────────────── */

struct AimlGLTexture { GLuint id; };

/* Must match ui_runtime.cpp AimlUIFont exactly */
struct AimlUIFont {
    stbtt_fontinfo info;
    unsigned char* ttf_data;   /* malloc'd copy */
    int     ttf_size;          /* byte length of ttf_data */
    GLuint  atlas_tex;
    stbtt_bakedchar cdata[96]; /* ASCII 32..127 */
    float   line_height;
    float   ascent_px;
    float   scale;
    int     atlas_w, atlas_h;
    float   baked_size_px;     /* size passed to stbtt_BakeFontBitmap */
};

/* ── Child types ─────────────────────────────────────────────────────────── */

enum PrefabChildType {
    CHILD_SPRITE     = 0,
    CHILD_LABEL      = 1,
    CHILD_RECT       = 2,
    CHILD_BUTTON     = 3,
    CHILD_TEXT_INPUT = 4,
    CHILD_CHECKBOX   = 5,
};

/* ── Prefab definition (from YAML compile-time) ──────────────────────────── */

struct PrefabChildDef {
    std::string name;
    PrefabChildType type;
    float x, y, width, height;
    float font_size;
    uint32_t color;                  /* legacy: sprite tint or rect solid fill */
    std::string texture_path;
    std::string anchor;              /* "left", "center", "right" */

    /* ── UI-widget style fields (button, text_input, rect with frame) ───── */
    uint32_t bg_color      = 0;      /* 0xRRGGBBAA */
    uint32_t border_color  = 0;
    float    border_width  = 0.0f;
    float    corner_radius = 0.0f;
    uint32_t hover_color   = 0;      /* button hover */
    uint32_t active_color  = 0;      /* button pressed / input focused */
    std::string label_text;          /* button label / static label */
    bool     is_secret     = false;  /* text_input: render '*' instead of chars */

    /* ── Callback function names (resolved at render time) ──────────────── */
    std::string on_click_fn;         /* button */
    std::string on_change_fn;        /* text_input value changed */

    /* ── Material name (resolved at instantiate time to Material*) ──────── */
    std::string material_name;
};

struct PrefabDef {
    std::string name;
    float width, height;
    std::string texture_path;
    bool draggable;
    std::vector<PrefabChildDef> children;
};

/* ── Material system ─────────────────────────────────────────────────────────
 * A material bundles a fragment shader (loaded from disk) with a set of
 * uniform-value bindings. Materials are registered by name and referenced
 * from prefab children via the `material:` YAML field. The same material
 * can be reused across many widgets, and a child carries only a pointer.
 *
 * Built-in uniforms always available to the shader (no need to declare in
 * the material's uniform map):
 *   uniform mat4      u_proj;
 *   uniform sampler2D u_tex;       // white-tex slot, always slot 0
 *   uniform int       u_mode;      // forwarded from DrawBatch.mode
 *   uniform float     u_time;      // seconds since process start
 * ─────────────────────────────────────────────────────────────────────────── */

enum MaterialUniformKind {
    MU_FLOAT, MU_VEC2, MU_VEC3, MU_VEC4, MU_INT, MU_TEXTURE,
};

struct MaterialUniform {
    MaterialUniformKind kind;
    float    floats[4]  = {0,0,0,0};   /* float/vec2/vec3/vec4 storage */
    int32_t  integer    = 0;            /* int storage */
    GLuint   texture    = 0;            /* sampler2D: resolved texture id */
    std::string tex_path;               /* sampler2D: source path */
};

struct Material {
    std::string name;
    std::string shader_path;
    GLuint program = 0;                 /* lazily compiled on first use */
    bool   fallback_warned = false;     /* one-shot stderr when prog stays 0 */
    /* Ordered list so iteration order matches user intent; name->index map for lookup. */
    std::vector<std::pair<std::string, MaterialUniform>> uniforms;
};

/* ── Runtime instances ───────────────────────────────────────────────────── */

struct PrefabChild {
    std::string name;
    PrefabChildType type;
    float x, y, width, height;
    float font_size;
    uint32_t color;        /* RGBA packed i32 */
    GLuint texture;        /* 0 = no texture */
    std::string text;      /* label text */
    bool visible;

    /* ── UI-widget style (copied from def at instantiate time) ──────────── */
    uint32_t bg_color      = 0;
    uint32_t border_color  = 0;
    float    border_width  = 0.0f;
    float    corner_radius = 0.0f;
    uint32_t hover_color   = 0;
    uint32_t active_color  = 0;
    std::string label_text;
    bool     is_secret     = false;
    std::string on_click_fn;
    std::string on_change_fn;

    /* ── Material (custom shader + uniforms). Null = use default pipeline. */
    Material* material = nullptr;

    /* ── Runtime widget state (mouse interaction, focus, cursor) ────────── */
    bool     hot           = false; /* mouse over this child this frame */
    bool     pressed_now   = false; /* mouse down + initially pressed inside */
    bool     clicked_frame = false; /* set on mouse_released over a pressed button (cleared next frame) */
    bool     focused       = false; /* text_input: keyboard focus */
    int32_t  cursor_pos    = 0;     /* text_input: cursor index into text */
    bool     checked       = false; /* checkbox: toggle state */
};

struct AimlPrefabInstance {
    PrefabDef* def;
    float x, y;
    float scale_x, scale_y;
    float rotation;
    int32_t z_order;
    float alpha;
    bool visible;
    bool draggable;
    GLuint base_texture;
    std::vector<PrefabChild> children;
    std::unordered_map<std::string, int> child_index;

    /* drag state */
    bool is_dragging;
    float drag_offset_x, drag_offset_y;
    float drag_origin_x, drag_origin_y;  /* position at drag start — for cancel_drag */

    /* relative positioning: x,y stored as 0.0-1.0 ratios of screen */
    bool is_relative;

    /* hover state (set by update_hover each frame) */
    bool hovered;
};

/* ── Scene state ─────────────────────────────────────────────────────────── */

struct PrefabScene {
    std::vector<AimlPrefabInstance*> instances;
    AimlPrefabInstance* dragged;
    AimlPrefabInstance* hovered;
    /* Global keyboard-focus target for text_input children (one at a time). */
    AimlPrefabInstance* focused_input_inst = nullptr;
    int32_t             focused_input_child_idx = -1;

    /* shared rendering resources */
    GLuint quad_vao, quad_vbo;
    GLuint shader;
    GLuint white_tex;          /* 1×1 white pixel for rects */
    float  proj[16];           /* orthographic projection matrix */
    int32_t screen_w, screen_h;
    int32_t design_w, design_h;   /* original design resolution (from scene_init) */

    AimlUIFont* font;          /* base font (set via prefab_set_font) */
    std::unordered_map<int, AimlUIFont*> font_cache; /* key = round(font_size*10) */

    /* prefab registry */
    std::unordered_map<std::string, PrefabDef*> defs;

    /* material registry (owned). Looked up by name when a child references one. */
    std::unordered_map<std::string, Material*> materials;
    /* Shader-program cache, keyed by fragment-shader file path. Materials that
     * share a shader file share the program. */
    std::unordered_map<std::string, GLuint> shader_programs;
    /* Texture cache for material-sampler2D uniforms, keyed by file path. */
    std::unordered_map<std::string, GLuint> material_textures;

    /* Time uniform (seconds since first frame). Updated each draw_scene. */
    float u_time = 0.0f;
    double t_start_sec = 0.0;
};

static PrefabScene* g_scene = nullptr;

/* Helper: get actual pixel position from (possibly relative) coords */
static inline void get_actual_pos(AimlPrefabInstance* inst,
                                   float& ax, float& ay) {
    if (inst->is_relative && g_scene) {
        ax = inst->x * g_scene->screen_w;
        ay = inst->y * g_scene->screen_h;
    } else {
        ax = inst->x;
        ay = inst->y;
    }
}

/* Helper: get scale factor for relative instances (screen / design resolution) */
static inline void get_rel_scale(float& rsx, float& rsy) {
    if (g_scene && g_scene->design_w > 0 && g_scene->design_h > 0) {
        rsx = (float)g_scene->screen_w / (float)g_scene->design_w;
        rsy = (float)g_scene->screen_h / (float)g_scene->design_h;
    } else {
        rsx = 1.0f;
        rsy = 1.0f;
    }
}

/* ── Orthographic projection (column-major, matches GL convention) ───────── */

static void ortho(float* m, float l, float r, float b, float t) {
    memset(m, 0, 16 * sizeof(float));
    m[0]  = 2.0f / (r - l);
    m[5]  = 2.0f / (t - b);
    m[10] = -1.0f;
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[15] = 1.0f;
}

/* ── RGBA helpers ────────────────────────────────────────────────────────── */

static uint32_t rgba_to_abgr(uint32_t rgba) {
    uint8_t r = (rgba >> 24) & 0xFF;
    uint8_t g = (rgba >> 16) & 0xFF;
    uint8_t b = (rgba >>  8) & 0xFF;
    uint8_t a = (rgba      ) & 0xFF;
    return (uint32_t)a << 24 | (uint32_t)b << 16 | (uint32_t)g << 8 | r;
}

static uint32_t color_with_alpha(uint32_t color_i32, float alpha) {
    /* color_i32 is 0xRRGGBB (no alpha channel in the input) */
    uint8_t r = (color_i32 >> 16) & 0xFF;
    uint8_t g = (color_i32 >>  8) & 0xFF;
    uint8_t b = (color_i32      ) & 0xFF;
    uint8_t a = (uint8_t)(alpha * 255.0f);
    /* Return in ABGR for OpenGL */
    return (uint32_t)a << 24 | (uint32_t)b << 16 | (uint32_t)g << 8 | r;
}

/* 0xRRGGBBAA → GL-bytes (ABGR low→high), with instance alpha multiplied in. */
static uint32_t rgba32_to_gl(uint32_t rgba, float alpha_mul) {
    uint8_t r = (rgba >> 24) & 0xFF;
    uint8_t g = (rgba >> 16) & 0xFF;
    uint8_t b = (rgba >>  8) & 0xFF;
    uint8_t a = (uint8_t)(((rgba & 0xFF) / 255.0f) * alpha_mul * 255.0f);
    return (uint32_t)a << 24 | (uint32_t)b << 16 | (uint32_t)g << 8 | r;
}

/* ── Shader ──────────────────────────────────────────────────────────────── */

#ifdef __EMSCRIPTEN__
#  define PREFAB_GLSL_HEADER "#version 300 es\nprecision highp float;\n"
#else
#  define PREFAB_GLSL_HEADER "#version 330 core\n"
#endif

static const char* PREFAB_VERT = PREFAB_GLSL_HEADER R"(
layout(location=0) in vec2 a_pos;
layout(location=1) in vec2 a_uv;
layout(location=2) in vec4 a_color;
layout(location=3) in vec2 a_quad;     /* qw, qh (SDF only) */
layout(location=4) in vec2 a_style;    /* radius, border_w (SDF only) */
layout(location=5) in vec4 a_bcolor;   /* border color (SDF only) */
uniform mat4 u_proj;
out vec2 v_uv;
out vec4 v_color;
out vec2 v_quad;
out vec2 v_style;
out vec4 v_bcolor;
void main(){
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
    v_uv     = a_uv;
    v_color  = a_color;
    v_quad   = a_quad;
    v_style  = a_style;
    v_bcolor = a_bcolor;
}
)";

static const char* PREFAB_FRAG = PREFAB_GLSL_HEADER R"(
in vec2 v_uv;
in vec4 v_color;
in vec2 v_quad;
in vec2 v_style;
in vec4 v_bcolor;
uniform sampler2D u_tex;
uniform int u_mode;
out vec4 out_color;

float sdf_rrect(vec2 p, vec2 half_size, float r) {
    vec2 q = abs(p) - half_size + r;
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

void main(){
    if (u_mode == 1) {
        /* RGBA texture (sprite) */
        out_color = texture(u_tex, v_uv) * v_color;
    } else if (u_mode == 2) {
        /* Font atlas (GL_RED channel = alpha) */
        float a = texture(u_tex, v_uv).r;
        out_color = vec4(v_color.rgb, v_color.a * a);
    } else if (u_mode == 3) {
        /* SDF rounded rect with optional border. v_uv ∈ [0,1] over the
         * quad; map to centered local coords using v_quad (px size). */
        vec2  hs   = v_quad * 0.5;
        vec2  p    = v_uv * v_quad - hs;
        float r    = min(v_style.x, min(hs.x, hs.y));
        float bw   = v_style.y;
        float aa   = max(fwidth(p.x), fwidth(p.y));
        float d_out = sdf_rrect(p, hs, r);
        float outer = 1.0 - smoothstep(-aa, aa, d_out);
        vec4 final_col = v_color;
        if (bw > 0.5) {
            float d_in = sdf_rrect(p, hs - vec2(bw), max(r - bw, 0.0));
            /* in_mask = 1 deep inside the inner (fill) rect, 0 in border ring.
             * mix(border, fill, in_mask) → fill inside, border outside. */
            float in_mask = 1.0 - smoothstep(-aa, aa, d_in);
            final_col = mix(v_bcolor, v_color, in_mask);
        }
        final_col.a *= outer;
        out_color = final_col;
    } else {
        /* Solid color (rect, no texture) */
        out_color = v_color;
    }
}
)";

static GLuint compile_shader_src(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetShaderInfoLog(s, 512, nullptr, buf);
        fprintf(stderr, "[prefab] shader error: %s\n", buf);
    }
    return s;
}

static GLuint create_prefab_shader() {
    GLuint v = compile_shader_src(GL_VERTEX_SHADER, PREFAB_VERT);
    GLuint f = compile_shader_src(GL_FRAGMENT_SHADER, PREFAB_FRAG);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetProgramInfoLog(p, 512, nullptr, buf);
        fprintf(stderr, "[prefab] link error: %s\n", buf);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

/* ── Vertex format ───────────────────────────────────────────────────────
 *   pos(2) + uv(2) + rgba(4 bytes) — used by sprite/rect/font modes.
 *   The SDF UI mode also reads qw/qh/radius/border_w/border_rgba; for
 *   non-SDF modes those fields are written as 0 and ignored by the shader.
 * ─────────────────────────────────────────────────────────────────────── */

struct PrefabVertex {
    float    x, y;
    float    u, v;
    uint32_t rgba;
    float    qw, qh;          /* quad width/height in pixels (SDF) */
    float    radius;          /* corner radius (SDF) */
    float    border_w;        /* border thickness (SDF) */
    uint32_t border_rgba;     /* border color (SDF) */
};

static void setup_quad_vao(PrefabScene* s) {
    glGenVertexArrays(1, &s->quad_vao);
    glGenBuffers(1, &s->quad_vbo);
    glBindVertexArray(s->quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s->quad_vbo);
    /* Will be filled dynamically; allocate enough for many quads */
    glBufferData(GL_ARRAY_BUFFER, sizeof(PrefabVertex) * 6 * 4096, nullptr, GL_DYNAMIC_DRAW);

    /* a_pos = location 0 */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(PrefabVertex),
                          (void*)offsetof(PrefabVertex, x));
    /* a_uv = location 1 */
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(PrefabVertex),
                          (void*)offsetof(PrefabVertex, u));
    /* a_color = location 2 (packed RGBA as 4 unsigned bytes) */
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PrefabVertex),
                          (void*)offsetof(PrefabVertex, rgba));
    /* a_quad = location 3 (quad dimensions in px, SDF-mode only) */
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(PrefabVertex),
                          (void*)offsetof(PrefabVertex, qw));
    /* a_style = location 4 (radius, border_w) */
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(PrefabVertex),
                          (void*)offsetof(PrefabVertex, radius));
    /* a_bcolor = location 5 (border color, SDF-mode only) */
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PrefabVertex),
                          (void*)offsetof(PrefabVertex, border_rgba));

    glBindVertexArray(0);
}

static GLuint create_white_texture() {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    uint8_t white[4] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

/* ── Batch rendering helpers ─────────────────────────────────────────────── */

struct DrawBatch {
    GLuint tex;
    int mode;              /* 0=solid, 1=rgba_texture, 2=font_atlas, 3=sdf-rounded-rect */
    std::vector<PrefabVertex> verts;
    /* If non-null, use this material's program + uniforms instead of the
     * default shader. The built-in uniforms (u_proj, u_tex, u_time, u_mode)
     * are still set automatically. */
    Material* material = nullptr;
};

static void push_quad(DrawBatch& batch, float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1, uint32_t rgba)
{
    PrefabVertex tl = {x,     y,     u0, v0, rgba, 0,0, 0,0, 0};
    PrefabVertex tr = {x + w, y,     u1, v0, rgba, 0,0, 0,0, 0};
    PrefabVertex bl = {x,     y + h, u0, v1, rgba, 0,0, 0,0, 0};
    PrefabVertex br = {x + w, y + h, u1, v1, rgba, 0,0, 0,0, 0};
    batch.verts.push_back(tl); batch.verts.push_back(tr); batch.verts.push_back(bl);
    batch.verts.push_back(tr); batch.verts.push_back(br); batch.verts.push_back(bl);
}

/* SDF-mode push: emits a UV-mapped quad with size/radius/border data so the
 * fragment shader can compute a rounded rect with anti-aliased edges. */
static void push_quad_sdf(DrawBatch& batch, float x, float y, float w, float h,
                          uint32_t rgba, uint32_t bcolor,
                          float radius, float border_w)
{
    PrefabVertex tl = {x,     y,     0, 0, rgba, w, h, radius, border_w, bcolor};
    PrefabVertex tr = {x + w, y,     1, 0, rgba, w, h, radius, border_w, bcolor};
    PrefabVertex bl = {x,     y + h, 0, 1, rgba, w, h, radius, border_w, bcolor};
    PrefabVertex br = {x + w, y + h, 1, 1, rgba, w, h, radius, border_w, bcolor};
    batch.verts.push_back(tl); batch.verts.push_back(tr); batch.verts.push_back(bl);
    batch.verts.push_back(tr); batch.verts.push_back(br); batch.verts.push_back(bl);
}

/* Flush a batch to GL */
static void flush_batch(DrawBatch& batch) {
    if (batch.verts.empty()) return;
    PrefabScene* s = g_scene;

    glBindVertexArray(s->quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s->quad_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    batch.verts.size() * sizeof(PrefabVertex),
                    batch.verts.data());

    /* Resolve which program to use: material's custom program if attached
     * and compilable, otherwise the default prefab shader. */
    GLuint prog = s->shader;
    if (batch.material) {
        if (batch.material->program == 0)
            batch.material->program = material_get_or_compile(batch.material->shader_path.c_str());
        if (batch.material->program != 0) {
            prog = batch.material->program;
        } else {
            /* Once-per-material warn so a missing/broken shader file does not
             * silently render the default (which would look like the material
             * “did nothing”). */
            if (!batch.material->fallback_warned) {
                fprintf(stderr,
                    "[material] '%s' has no program → falling back to default shader\n",
                    batch.material->shader_path.c_str());
                batch.material->fallback_warned = true;
            }
        }
    }

    glUseProgram(prog);
    glUniformMatrix4fv(glGetUniformLocation(prog, "u_proj"), 1, GL_FALSE, s->proj);
    glUniform1i(glGetUniformLocation(prog, "u_mode"), batch.mode);
    GLint loc_time = glGetUniformLocation(prog, "u_time");
    if (loc_time >= 0) glUniform1f(loc_time, s->u_time);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, batch.tex);
    glUniform1i(glGetUniformLocation(prog, "u_tex"), 0);

    /* Custom material uniforms (after built-ins so a user can't accidentally
     * stomp on u_proj/u_tex/u_mode/u_time — they're set just above). */
    if (batch.material && prog == batch.material->program)
        material_apply_uniforms(batch.material, prog);

    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)batch.verts.size());
    glBindVertexArray(0);
}

/* ── Text rendering into a batch ─────────────────────────────────────────── */

static float text_width_font(AimlUIFont* font, const char* text) {
    if (!font || !text) return 0;
    float w = 0;
    while (*text) {
        unsigned char c = (unsigned char)*text++;
        if (c == '\n') break;
        if (c < 32 || c >= 128) continue;
        w += font->cdata[c - 32].xadvance;
    }
    return w;
}

static std::vector<std::string> wrap_text_lines(AimlUIFont* font, const char* text, float max_w) {
    std::vector<std::string> lines;
    if (!font || !text || !*text) return lines;

    std::string current_line;
    std::string current_word;

    auto flush_line = [&]() {
        lines.push_back(current_line);
        current_line.clear();
    };
    auto append_word = [&]() {
        if (current_word.empty()) return;
        std::string candidate = current_line.empty() ? current_word : current_line + " " + current_word;
        if (max_w > 0.0f && !current_line.empty() && text_width_font(font, candidate.c_str()) > max_w) {
            flush_line();
            current_line = current_word;
        } else {
            current_line = candidate;
        }
        current_word.clear();
    };

    const char* p = text;
    while (*p) {
        char c = *p++;
        if (c == '\n' || c == '|') {
            append_word();
            flush_line();
            continue;
        }
        if (c == ' ') {
            append_word();
            continue;
        }
        current_word += c;
        if (max_w > 0.0f && current_line.empty() && text_width_font(font, current_word.c_str()) > max_w) {
            lines.push_back(current_word);
            current_word.clear();
        }
    }
    append_word();
    if (!current_line.empty()) lines.push_back(current_line);
    if (lines.empty()) lines.push_back("");
    return lines;
}

static void push_text_batch(DrawBatch& batch, AimlUIFont* font,
                            const char* text, float cx, float cy,
                            float max_w, uint32_t rgba, const char* anchor)
{
    if (!font || !text || !*text) return;

    std::vector<std::string> lines = wrap_text_lines(font, text, max_w);
    float line_height = font->line_height;
    float base_y = cy + font->ascent_px;

    for (size_t li = 0; li < lines.size(); ++li) {
        const std::string& line = lines[li];
        float tw = text_width_font(font, line.c_str());
        float x;
        if (anchor && strcmp(anchor, "center") == 0) {
            x = cx - tw * 0.5f;
        } else if (anchor && strcmp(anchor, "right") == 0) {
            x = cx - tw;
        } else {
            x = cx;
        }
        float y = base_y + (float)li * line_height;
        float limit = x + max_w;

        for (char raw_c : line) {
            unsigned char c = (unsigned char)raw_c;
            if (c < 32 || c >= 128) continue;
            stbtt_bakedchar* bc = &font->cdata[c - 32];
            float x0 = x + bc->xoff;
            float y0 = y + bc->yoff;
            float gw = (float)(bc->x1 - bc->x0);
            float gh = (float)(bc->y1 - bc->y0);
            float x1 = x0 + gw;
            if (max_w > 0 && x1 > limit) break;
            float u0 = (float)bc->x0 / font->atlas_w;
            float v0 = (float)bc->y0 / font->atlas_h;
            float u1 = (float)bc->x1 / font->atlas_w;
            float v1 = (float)bc->y1 / font->atlas_h;
            push_quad(batch, x0, y0, gw, gh, u0, v0, u1, v1, rgba);
            x += bc->xadvance;
        }
    }
}

/* ── Texture loading (reuse gl_runtime pattern) ──────────────────────────── */

static std::unordered_map<std::string, GLuint> s_texture_cache;

static GLuint load_texture_cached(const char* path) {
    if (!path || !*path) return 0;
    auto it = s_texture_cache.find(path);
    if (it != s_texture_cache.end()) return it->second;

    /* Use gl_runtime's loader which returns AimlGLTexture* */
    void* t = aiml_gl_load_texture(path);
    if (!t) {
        fprintf(stderr, "[prefab] failed to load texture: %s\n", path);
        s_texture_cache[path] = 0;
        return 0;
    }
    GLuint id = ((AimlGLTexture*)t)->id;
    s_texture_cache[path] = id;
    return id;
}

/* ── JSON child parser (minimal, for children_json from register) ────────── */

/* Parse a JSON array of child definitions.
 * Format: [{"name":"x","type":"sprite","x":10,...}, ...]
 * This is intentionally minimal — only handles the fields we need. */

static PrefabChildType parse_child_type(const char* s) {
    if (strcmp(s, "label") == 0)      return CHILD_LABEL;
    if (strcmp(s, "rect") == 0)       return CHILD_RECT;
    if (strcmp(s, "button") == 0)     return CHILD_BUTTON;
    if (strcmp(s, "text_input") == 0) return CHILD_TEXT_INPUT;
    if (strcmp(s, "checkbox") == 0)   return CHILD_CHECKBOX;
    return CHILD_SPRITE;
}

/* Tiny JSON scanner — just enough for our flat object arrays */
static void skip_ws(const char*& p) { while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++; }

static std::string parse_json_string(const char*& p) {
    skip_ws(p);
    if (*p != '"') return "";
    p++;
    std::string s;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p+1)) { s += *(p+1); p += 2; }
        else { s += *p; p++; }
    }
    if (*p == '"') p++;
    return s;
}

static double parse_json_number(const char*& p) {
    skip_ws(p);
    char* end;
    double v = strtod(p, &end);
    p = end;
    return v;
}

static std::vector<PrefabChildDef> parse_children_json(const char* json) {
    std::vector<PrefabChildDef> result;
    if (!json || !*json) return result;

    const char* p = json;
    skip_ws(p);
    if (*p != '[') return result;
    p++;

    while (*p) {
        skip_ws(p);
        if (*p == ']') break;
        if (*p == ',') { p++; continue; }
        if (*p != '{') break;
        p++; /* skip '{' */

        PrefabChildDef cd;
        cd.type = CHILD_SPRITE;
        cd.x = cd.y = cd.width = cd.height = 0;
        cd.font_size = 12;
        cd.color = 0xFFFFFF;
        cd.anchor = "left";

        /* Parse key-value pairs */
        while (*p && *p != '}') {
            skip_ws(p);
            if (*p == ',') { p++; continue; }
            if (*p == '}') break;

            std::string key = parse_json_string(p);
            skip_ws(p);
            if (*p == ':') p++;
            skip_ws(p);

            if (key == "name") cd.name = parse_json_string(p);
            else if (key == "type") {
                std::string t = parse_json_string(p);
                cd.type = parse_child_type(t.c_str());
            }
            else if (key == "texture") cd.texture_path = parse_json_string(p);
            else if (key == "anchor") cd.anchor = parse_json_string(p);
            else if (key == "x") cd.x = (float)parse_json_number(p);
            else if (key == "y") cd.y = (float)parse_json_number(p);
            else if (key == "width") cd.width = (float)parse_json_number(p);
            else if (key == "height") cd.height = (float)parse_json_number(p);
            else if (key == "font_size") cd.font_size = (float)parse_json_number(p);
            else if (key == "color") cd.color = (uint32_t)parse_json_number(p);
            /* UI-widget style fields */
            else if (key == "bg_color")      cd.bg_color = (uint32_t)parse_json_number(p);
            else if (key == "border_color")  cd.border_color = (uint32_t)parse_json_number(p);
            else if (key == "border_width")  cd.border_width = (float)parse_json_number(p);
            else if (key == "corner_radius") cd.corner_radius = (float)parse_json_number(p);
            else if (key == "hover_color")   cd.hover_color = (uint32_t)parse_json_number(p);
            else if (key == "active_color")  cd.active_color = (uint32_t)parse_json_number(p);
            else if (key == "label")         cd.label_text = parse_json_string(p);
            else if (key == "secret")        cd.is_secret = (parse_json_number(p) != 0);
            else if (key == "on_click")      cd.on_click_fn = parse_json_string(p);
            else if (key == "on_change")     cd.on_change_fn = parse_json_string(p);
            else if (key == "material")      cd.material_name = parse_json_string(p);
            else {
                /* skip unknown value */
                if (*p == '"') parse_json_string(p);
                else parse_json_number(p);
            }
        }
        if (*p == '}') p++;
        result.push_back(std::move(cd));
    }
    return result;
}

/* ── Rotation helper ─────────────────────────────────────────────────────── */

static void rotate_batch_verts(DrawBatch& batch, size_t start_idx,
                               float cx, float cy, float angle_deg)
{
    if (angle_deg == 0.0f) return;
    float rad = angle_deg * (float)(M_PI / 180.0);
    float cs = cosf(rad);
    float sn = sinf(rad);
    for (size_t i = start_idx; i < batch.verts.size(); i++) {
        float dx = batch.verts[i].x - cx;
        float dy = batch.verts[i].y - cy;
        batch.verts[i].x = cx + dx * cs - dy * sn;
        batch.verts[i].y = cy + dx * sn + dy * cs;
    }
}

/* ── Render one instance ─────────────────────────────────────────────────── */

static AimlUIFont* font_for_size(PrefabScene* s, float font_size);

static void render_instance(AimlPrefabInstance* inst) {
    if (!inst || !inst->visible) return;
    PrefabScene* s = g_scene;

    float ox, oy;
    get_actual_pos(inst, ox, oy);
    float sx = inst->scale_x;
    float sy = inst->scale_y;
    /* For relative-mode instances, also scale size by screen/design ratio */
    if (inst->is_relative) {
        float rsx, rsy;
        get_rel_scale(rsx, rsy);
        sx *= rsx;
        sy *= rsy;
    }
    float alpha = inst->alpha;
    float rot = inst->rotation;
    /* Rotation center = center of the instance bounding box */
    float rot_cx = ox + inst->def->width  * sx * 0.5f;
    float rot_cy = oy + inst->def->height * sy * 0.5f;

    /* 1) Base texture (card frame etc.) */
    if (inst->base_texture) {
        DrawBatch batch;
        batch.tex = inst->base_texture;
        batch.mode = 1; /* rgba texture */
        uint32_t col = color_with_alpha(0xFFFFFF, alpha);
        push_quad(batch, ox, oy,
                  inst->def->width * sx, inst->def->height * sy,
                  0, 1, 1, 0, col);
        rotate_batch_verts(batch, 0, rot_cx, rot_cy, rot);
        flush_batch(batch);
    }

    /* 2) Children */
    for (auto& ch : inst->children) {
        if (!ch.visible) continue;

        float cx = ox + ch.x * sx;
        float cy = oy + ch.y * sy;
        float cw = ch.width * sx;
        float ch_h = ch.height * sy;

        switch (ch.type) {
        case CHILD_SPRITE: {
            if (ch.texture == 0) break;
            static int sprite_log_count = 0;
            DrawBatch batch;
            batch.tex = ch.texture;
            batch.mode = 1;
            uint32_t col = color_with_alpha(0xFFFFFF, alpha);
            push_quad(batch, cx, cy, cw, ch_h, 0, 1, 1, 0, col);
            rotate_batch_verts(batch, 0, rot_cx, rot_cy, rot);
            flush_batch(batch);
            (void)0;
            break;
        }
        case CHILD_RECT: {
            DrawBatch batch;
            if (ch.texture != 0) {
                /* Textured panel/frame — alpha from texture (sprite mode).
                 * V-flipped UV (v0=1, v1=0) — matches CHILD_SPRITE so that
                 * asymmetric textures (creature images, icons) render with
                 * the correct vertical orientation. stb_image returns top-down
                 * rows; GL needs bottom-up. */
                batch.tex  = ch.texture;
                batch.mode = 1;
                uint32_t col = color_with_alpha(0xFFFFFF, alpha);
                push_quad(batch, cx, cy, cw, ch_h, 0, 1, 1, 0, col);
            } else if (ch.corner_radius > 0.0f || ch.border_width > 0.0f
                       || ch.bg_color != 0 || ch.border_color != 0
                       || ch.material) {
                /* Rounded/bordered rect — SDF, optionally via a material's
                 * custom fragment shader. */
                batch.tex  = s->white_tex;
                batch.mode = 3;
                batch.material = ch.material;
                uint32_t fg = rgba32_to_gl(ch.bg_color, alpha);
                uint32_t bd = rgba32_to_gl(ch.border_color, alpha);
                push_quad_sdf(batch, cx, cy, cw, ch_h, fg, bd,
                              ch.corner_radius * sx, ch.border_width * sx);
            } else {
                /* Legacy solid fill from .color (0xRRGGBB, no alpha). */
                batch.tex  = s->white_tex;
                batch.mode = 0;
                uint32_t col = color_with_alpha(ch.color, alpha);
                push_quad(batch, cx, cy, cw, ch_h, 0, 0, 1, 1, col);
            }
            rotate_batch_verts(batch, 0, rot_cx, rot_cy, rot);
            flush_batch(batch);
            break;
        }
        case CHILD_BUTTON: {
            /* Pick bg based on widget state. */
            uint32_t bg_rgba = ch.pressed_now && ch.active_color ? ch.active_color
                            : ch.hot         && ch.hover_color  ? ch.hover_color
                            :                                     ch.bg_color;
            if (ch.texture != 0) {
                /* Icon-button: textured sprite. The bg_rgba modulates the
                 * texture as a tint (white = unmodified, hover/active gives
                 * the press feedback without needing a second pass). */
                DrawBatch batch;
                batch.tex  = ch.texture;
                batch.mode = 1;
                uint32_t tint = rgba32_to_gl(bg_rgba ? bg_rgba : 0xFFFFFFFF, alpha);
                /* V-flipped UV (v0=1, v1=0) — same as CHILD_SPRITE.
                 * Without the flip, asymmetric icons render upside-down because
                 * stb_image returns top-down rows while GL expects bottom-up. */
                push_quad(batch, cx, cy, cw, ch_h, 0, 1, 1, 0, tint);
                rotate_batch_verts(batch, 0, rot_cx, rot_cy, rot);
                flush_batch(batch);
                /* Optional border around the icon. */
                if (ch.border_color != 0 && ch.border_width > 0.0f) {
                    DrawBatch frame;
                    frame.tex  = s->white_tex;
                    frame.mode = 3;
                    uint32_t bd = rgba32_to_gl(ch.border_color, alpha);
                    push_quad_sdf(frame, cx, cy, cw, ch_h, 0, bd,
                                  ch.corner_radius * sx, ch.border_width * sx);
                    rotate_batch_verts(frame, 0, rot_cx, rot_cy, rot);
                    flush_batch(frame);
                }
            } else {
                /* Text-button: SDF rounded rect, label drawn below. */
                DrawBatch batch;
                batch.tex  = s->white_tex;
                batch.mode = 3;
                batch.material = ch.material;
                uint32_t fg = rgba32_to_gl(bg_rgba,         alpha);
                uint32_t bd = rgba32_to_gl(ch.border_color, alpha);
                push_quad_sdf(batch, cx, cy, cw, ch_h, fg, bd,
                              ch.corner_radius * sx, ch.border_width * sx);
                rotate_batch_verts(batch, 0, rot_cx, rot_cy, rot);
                flush_batch(batch);
            }
            /* Caption centered (skipped for icon-only buttons with empty text).
             * Reads ch.text so the AIML side can re-label a button via set_text. */
            if (!ch.text.empty() && s->font) {
                float target_size = ch.font_size > 0.0f ? ch.font_size * sx : 0.0f;
                AimlUIFont* lf = (target_size > 0.0f) ? font_for_size(s, target_size) : s->font;
                if (lf) {
                    DrawBatch batch;
                    batch.tex  = lf->atlas_tex;
                    batch.mode = 2;
                    uint32_t col = rgba32_to_gl(ch.color ? ch.color : 0xFFFFFFFF, alpha);
                    float text_cx = cx + cw * 0.5f;
                    float text_cy = cy + (ch_h - lf->line_height) * 0.5f;
                    push_text_batch(batch, lf, ch.text.c_str(),
                                    text_cx, text_cy, cw, col, "center");
                    rotate_batch_verts(batch, 0, rot_cx, rot_cy, rot);
                    flush_batch(batch);
                }
            }
            break;
        }
        case CHILD_TEXT_INPUT: {
            /* Background uses active_color when focused (otherwise bg_color). */
            uint32_t bg_rgba = ch.focused && ch.active_color ? ch.active_color : ch.bg_color;
            {
                DrawBatch batch;
                batch.tex  = s->white_tex;
                batch.mode = 3;
                batch.material = ch.material;
                uint32_t fg = rgba32_to_gl(bg_rgba,         alpha);
                uint32_t bd = rgba32_to_gl(ch.border_color, alpha);
                push_quad_sdf(batch, cx, cy, cw, ch_h, fg, bd,
                              ch.corner_radius * sx, ch.border_width * sx);
                rotate_batch_verts(batch, 0, rot_cx, rot_cy, rot);
                flush_batch(batch);
            }
            /* Displayed text: hide chars when secret. */
            if (s->font) {
                std::string display = ch.is_secret
                                        ? std::string(ch.text.size(), '*')
                                        : ch.text;
                float target_size = ch.font_size > 0.0f ? ch.font_size * sx : 0.0f;
                AimlUIFont* lf = (target_size > 0.0f) ? font_for_size(s, target_size) : s->font;
                if (lf && !display.empty()) {
                    DrawBatch batch;
                    batch.tex  = lf->atlas_tex;
                    batch.mode = 2;
                    uint32_t col = rgba32_to_gl(ch.color ? ch.color : 0xFFFFFFFF, alpha);
                    float padding = 8.0f * sx;
                    float text_cx = cx + padding;
                    float text_cy = cy + (ch_h - lf->line_height) * 0.5f;
                    push_text_batch(batch, lf, display.c_str(),
                                    text_cx, text_cy, cw - 2 * padding, col, "left");
                    rotate_batch_verts(batch, 0, rot_cx, rot_cy, rot);
                    flush_batch(batch);
                }
                /* Cursor: a thin vertical bar after the text. */
                if (ch.focused && lf) {
                    float caret_w = 1.5f * sx;
                    float caret_h = lf->line_height;
                    std::string before = display.substr(0, (size_t)ch.cursor_pos);
                    float tw = text_width_font(lf, before.c_str());
                    float padding = 8.0f * sx;
                    float bx = cx + padding + tw;
                    float by = cy + (ch_h - caret_h) * 0.5f;
                    DrawBatch cbatch;
                    cbatch.tex  = s->white_tex;
                    cbatch.mode = 0;
                    uint32_t col = rgba32_to_gl(ch.color ? ch.color : 0xFFFFFFFF, alpha);
                    push_quad(cbatch, bx, by, caret_w, caret_h, 0,0,1,1, col);
                    rotate_batch_verts(cbatch, 0, rot_cx, rot_cy, rot);
                    flush_batch(cbatch);
                }
            }
            break;
        }
        case CHILD_CHECKBOX: {
            /* Layout: square box on the left (size = min(child.h, child.w)),
             * label to the right. When a texture is set, the entire bbox is
             * treated as the toggle target (no label cell), the texture fills
             * it as the "icon", and a tick overlay in the top-right corner
             * indicates the checked state. Without texture: original
             * box-on-left + label-on-right layout via SDF rendering. */
            if (ch.texture != 0) {
                /* Icon-checkbox mode: texture fills the whole bbox. */
                DrawBatch tex;
                tex.tex  = ch.texture;
                tex.mode = 1;
                uint32_t tint = rgba32_to_gl(0xFFFFFFFF, alpha);
                push_quad(tex, cx, cy, cw, ch_h, 0, 1, 1, 0, tint);
                rotate_batch_verts(tex, 0, rot_cx, rot_cy, rot);
                flush_batch(tex);
                /* Optional border (highlight when checked OR hovered). */
                int32_t edge = ch.checked      ? (ch.active_color ? ch.active_color : ch.border_color)
                            : ch.hot && ch.hover_color ? ch.hover_color
                            :                            ch.border_color;
                float bw = ch.border_width > 0.0f
                             ? ch.border_width * sx
                             : (ch.checked ? 3.0f * sx : 0.0f);
                if (edge != 0 && bw > 0.0f) {
                    DrawBatch frame;
                    frame.tex  = s->white_tex;
                    frame.mode = 3;
                    uint32_t bd = rgba32_to_gl(edge, alpha);
                    push_quad_sdf(frame, cx, cy, cw, ch_h, 0, bd,
                                  ch.corner_radius * sx, bw);
                    rotate_batch_verts(frame, 0, rot_cx, rot_cy, rot);
                    flush_batch(frame);
                }
                /* Tick overlay in top-right corner when checked. */
                if (ch.checked) {
                    float tick_size = (cw < ch_h ? cw : ch_h) * 0.30f;
                    float tx = cx + cw - tick_size - 4.0f * sx;
                    float ty = cy + 4.0f * sy;
                    DrawBatch tick;
                    tick.tex  = s->white_tex;
                    tick.mode = 3;
                    uint32_t tfill = rgba32_to_gl(0xFFFFFFFF, alpha);
                    push_quad_sdf(tick, tx, ty, tick_size, tick_size, tfill, 0,
                                  tick_size * 0.4f, 0);
                    rotate_batch_verts(tick, 0, rot_cx, rot_cy, rot);
                    flush_batch(tick);
                }
                break;
            }
            float box_size = ch_h < cw ? ch_h : cw;
            uint32_t bg_rgba = ch.checked && ch.active_color    ? ch.active_color
                            : ch.pressed_now && ch.active_color ? ch.active_color
                            : ch.hot         && ch.hover_color  ? ch.hover_color
                            :                                     ch.bg_color;
            {
                DrawBatch batch;
                batch.tex  = s->white_tex;
                batch.mode = 3;
                batch.material = ch.material;
                uint32_t fg = rgba32_to_gl(bg_rgba,         alpha);
                uint32_t bd = rgba32_to_gl(ch.border_color, alpha);
                push_quad_sdf(batch, cx, cy, box_size, box_size, fg, bd,
                              ch.corner_radius * sx, ch.border_width * sx);
                rotate_batch_verts(batch, 0, rot_cx, rot_cy, rot);
                flush_batch(batch);
            }
            if (ch.checked) {
                /* Bigger tick (15% inset = 70% of box width) for an obvious
                 * visual that survives small box sizes. Always white-ish so
                 * it contrasts against the active_color bg. */
                float inset = box_size * 0.15f;
                DrawBatch tick;
                tick.tex  = s->white_tex;
                tick.mode = 3;
                uint32_t tick_fill = rgba32_to_gl(0xFFFFFFFF, alpha);
                push_quad_sdf(tick, cx + inset, cy + inset,
                              box_size - 2.0f * inset, box_size - 2.0f * inset,
                              tick_fill, 0,
                              ch.corner_radius * sx * 0.5f, 0);
                rotate_batch_verts(tick, 0, rot_cx, rot_cy, rot);
                flush_batch(tick);
            }
            /* Label to the right of the box, vertically centered. */
            if (!ch.text.empty() && s->font) {
                float target_size = ch.font_size > 0.0f ? ch.font_size * sx : 0.0f;
                AimlUIFont* lf = (target_size > 0.0f) ? font_for_size(s, target_size) : s->font;
                if (lf) {
                    DrawBatch tb;
                    tb.tex  = lf->atlas_tex;
                    tb.mode = 2;
                    uint32_t lcol = rgba32_to_gl(ch.color ? ch.color : 0xFFFFFFFF, alpha);
                    float gap = 8.0f * sx;
                    float text_cx = cx + box_size + gap;
                    float text_cy = cy + (ch_h - lf->line_height) * 0.5f;
                    push_text_batch(tb, lf, ch.text.c_str(),
                                    text_cx, text_cy, cw - box_size - gap, lcol, "left");
                    rotate_batch_verts(tb, 0, rot_cx, rot_cy, rot);
                    flush_batch(tb);
                }
            }
            break;
        }
        case CHILD_LABEL: {
            if (ch.text.empty() || !s->font) break;
            /* Select font for this label's font_size (cached per size). */
            float target_size = ch.font_size > 0.0f ? ch.font_size * sx : 0.0f;
            AimlUIFont* lf = (target_size > 0.0f)
                             ? font_for_size(s, target_size)
                             : s->font;
            if (!lf) break;
            DrawBatch batch;
            batch.tex = lf->atlas_tex;
            batch.mode = 2; /* font atlas */
            /* Consistent with all other UI widgets: read .color as 0xRRGGBBAA
             * via rgba32_to_gl. The legacy 0xRRGGBB interpretation would
             * shift channels and disagree with button/checkbox labels. */
            uint32_t col = rgba32_to_gl(ch.color ? ch.color : 0xFFFFFFFF, alpha);
            /* anchor handling: cx is the x reference point */
            const char* anchor = "left";
            auto it = inst->child_index.find(ch.name);
            if (it != inst->child_index.end() && inst->def) {
                int idx = it->second;
                if (idx >= 0 && idx < (int)inst->def->children.size()) {
                    anchor = inst->def->children[idx].anchor.c_str();
                }
            }
            push_text_batch(batch, lf, ch.text.c_str(),
                           cx, cy, ch.width > 0.0f ? ch.width * sx : inst->def->width * sx, col, anchor);
            rotate_batch_verts(batch, 0, rot_cx, rot_cy, rot);
            flush_batch(batch);
            break;
        }
        }
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

struct PendingPrefab {
    std::string name;
    float width, height;
    std::string texture_path;
    int32_t draggable;
    std::string children_json;
};
static std::vector<PendingPrefab> g_pending_prefabs;

/* Materials registered before aiml_prefab_scene_init() arrives are queued
 * here and flushed once the GL context + scene exist. (Definition lives
 * here so aiml_prefab_scene_init can iterate them; the register helper is
 * defined later, after the JSON-parsing utilities.) */
struct PendingMaterial { std::string name, shader_path, uniforms_json; };
static std::vector<PendingMaterial> g_pending_materials;

static void register_prefab_def(const char* name, float width, float height,
                                const char* texture_path, int32_t draggable,
                                const char* children_json);

void aiml_prefab_scene_init(int32_t screen_w, int32_t screen_h) {
    if (g_scene) {
        /* Re-init: just update projection */
        g_scene->screen_w = screen_w;
        g_scene->screen_h = screen_h;
        ortho(g_scene->proj, 0, (float)screen_w, (float)screen_h, 0);
        return;
    }

    g_scene = new PrefabScene();
    g_scene->screen_w = screen_w;
    g_scene->screen_h = screen_h;
    g_scene->design_w = screen_w;
    g_scene->design_h = screen_h;
    g_scene->dragged = nullptr;
    g_scene->font = nullptr;

    ortho(g_scene->proj, 0, (float)screen_w, (float)screen_h, 0);
    g_scene->shader = create_prefab_shader();
    setup_quad_vao(g_scene);
    g_scene->white_tex = create_white_texture();

    fprintf(stderr, "[prefab] scene init %dx%d\n", screen_w, screen_h);

    // Flush any prefabs that were registered before scene_init
    for (auto& p : g_pending_prefabs) {
        register_prefab_def(p.name.c_str(), p.width, p.height,
                            p.texture_path.c_str(), p.draggable,
                            p.children_json.c_str());
    }
    g_pending_prefabs.clear();

    // Flush deferred materials (registered before scene_init).
    for (auto& mp : g_pending_materials) {
        register_material_def(mp.name.c_str(), mp.shader_path.c_str(),
                              mp.uniforms_json.c_str());
    }
    g_pending_materials.clear();
}

static void register_prefab_def(const char* name, float width, float height,
                                const char* texture_path, int32_t draggable,
                                const char* children_json)
{
    PrefabDef* def = new PrefabDef();
    def->name = name ? name : "";
    def->width = width;
    def->height = height;
    def->texture_path = texture_path ? texture_path : "";
    def->draggable = (draggable != 0);
    def->children = parse_children_json(children_json);

    g_scene->defs[def->name] = def;
    fprintf(stderr, "[prefab] registered '%s' (%gx%g, %zu children)\n",
            def->name.c_str(), width, height, def->children.size());
}

void aiml_prefab_register(const char* name, float width, float height,
                          const char* texture_path, int32_t draggable,
                          const char* children_json)
{
    if (!g_scene) {
        g_pending_prefabs.push_back({
            name ? name : "", width, height,
            texture_path ? texture_path : "", draggable,
            children_json ? children_json : ""
        });
        return;
    }

    register_prefab_def(name, width, height, texture_path, draggable, children_json);
}

void* aiml_prefab_instantiate(const char* prefab_name) {
    if (!g_scene || !prefab_name) return nullptr;

    auto it = g_scene->defs.find(prefab_name);
    if (it == g_scene->defs.end()) {
        fprintf(stderr, "[prefab] ERROR: unknown prefab '%s'\n", prefab_name);
        return nullptr;
    }

    PrefabDef* def = it->second;
    AimlPrefabInstance* inst = new AimlPrefabInstance();
    inst->def = def;
    inst->x = 0;
    inst->y = 0;
    inst->scale_x = 1.0f;
    inst->scale_y = 1.0f;
    inst->rotation = 0;
    inst->z_order = 0;
    inst->alpha = 1.0f;
    inst->visible = true;
    inst->draggable = def->draggable;
    inst->is_dragging = false;
    inst->drag_offset_x = 0;
    inst->drag_offset_y = 0;
    inst->is_relative = false;

    /* Load base texture */
    inst->base_texture = 0;
    if (!def->texture_path.empty()) {
        inst->base_texture = load_texture_cached(def->texture_path.c_str());
    }

    /* Create children from def */
    for (int i = 0; i < (int)def->children.size(); i++) {
        auto& cd = def->children[i];
        PrefabChild child;
        child.name = cd.name;
        child.type = cd.type;
        child.x = cd.x;
        child.y = cd.y;
        child.width = cd.width;
        child.height = cd.height;
        child.font_size = cd.font_size;
        child.color = cd.color;
        child.visible = true;
        child.texture = 0;

        if (!cd.texture_path.empty()) {
            child.texture = load_texture_cached(cd.texture_path.c_str());
        }

        /* UI-widget style copy */
        child.bg_color      = cd.bg_color;
        child.border_color  = cd.border_color;
        child.border_width  = cd.border_width;
        child.corner_radius = cd.corner_radius;
        child.hover_color   = cd.hover_color;
        child.active_color  = cd.active_color;
        child.label_text    = cd.label_text;
        child.is_secret     = cd.is_secret;
        child.on_click_fn   = cd.on_click_fn;
        child.on_change_fn  = cd.on_change_fn;
        child.material      = material_find(cd.material_name.c_str());
        /* Seed text buffer: text_input uses label as initial value; static
         * labels use it as their displayed text; buttons use it as the
         * displayed caption (so set_text can re-label a button at runtime);
         * checkboxes use it as the label text rendered next to the box. */
        if (child.type == CHILD_TEXT_INPUT || child.type == CHILD_LABEL
            || child.type == CHILD_BUTTON  || child.type == CHILD_CHECKBOX)
            child.text = cd.label_text;

        /* Resolve material name → Material* (null if unknown — falls back
         * to default shader silently). Looked up once at instantiate time. */
        if (!cd.material_name.empty())
            child.material = material_find(cd.material_name.c_str());

        inst->child_index[child.name] = (int)inst->children.size();
        inst->children.push_back(std::move(child));
    }

    g_scene->instances.push_back(inst);
    return inst;
}

void aiml_prefab_destroy(void* p) {
    AimlPrefabInstance* inst = (AimlPrefabInstance*)p;
    if (!inst || !g_scene) return;

    if (g_scene->dragged == inst) g_scene->dragged = nullptr;
    if (g_scene->hovered == inst) g_scene->hovered = nullptr;
    if (g_scene->focused_input_inst == inst) {
        g_scene->focused_input_inst = nullptr;
        g_scene->focused_input_child_idx = -1;
    }

    auto& v = g_scene->instances;
    v.erase(std::remove(v.begin(), v.end(), inst), v.end());
    delete inst;
}

/* ── Transform ───────────────────────────────────────────────────────────── */

void aiml_prefab_set_pos(void* p, float x, float y) {
    auto* inst = (AimlPrefabInstance*)p; if (!inst) return;
    inst->x = x; inst->y = y;
    inst->is_relative = false;
}
void aiml_prefab_set_pos_rel(void* p, float rx, float ry) {
    auto* inst = (AimlPrefabInstance*)p; if (!inst) return;
    inst->x = rx; inst->y = ry;
    inst->is_relative = true;
}
float aiml_prefab_get_x(void* p) {
    auto* inst = (AimlPrefabInstance*)p; return inst ? inst->x : 0;
}
float aiml_prefab_get_y(void* p) {
    auto* inst = (AimlPrefabInstance*)p; return inst ? inst->y : 0;
}
void aiml_prefab_set_scale(void* p, float s) {
    auto* inst = (AimlPrefabInstance*)p; if (inst) { inst->scale_x = s; inst->scale_y = s; }
}
float aiml_prefab_get_scale(void* p) {
    auto* inst = (AimlPrefabInstance*)p; return inst ? inst->scale_x : 1;
}
void aiml_prefab_set_rotation(void* p, float r) {
    auto* inst = (AimlPrefabInstance*)p; if (inst) inst->rotation = r;
}
float aiml_prefab_get_rotation(void* p) {
    auto* inst = (AimlPrefabInstance*)p; return inst ? inst->rotation : 0;
}
void aiml_prefab_set_z(void* p, int32_t z) {
    auto* inst = (AimlPrefabInstance*)p; if (inst) inst->z_order = z;
}
int32_t aiml_prefab_get_z(void* p) {
    auto* inst = (AimlPrefabInstance*)p; return inst ? inst->z_order : 0;
}
void aiml_prefab_set_visible(void* p, int32_t v) {
    auto* inst = (AimlPrefabInstance*)p; if (inst) inst->visible = (v != 0);
}
int32_t aiml_prefab_get_visible(void* p) {
    auto* inst = (AimlPrefabInstance*)p; return inst ? (inst->visible ? 1 : 0) : 0;
}
void aiml_prefab_set_alpha(void* p, float a) {
    auto* inst = (AimlPrefabInstance*)p; if (inst) inst->alpha = a;
}
float aiml_prefab_get_alpha(void* p) {
    auto* inst = (AimlPrefabInstance*)p; return inst ? inst->alpha : 1;
}
void aiml_prefab_set_draggable(void* p, int32_t d) {
    auto* inst = (AimlPrefabInstance*)p; if (inst) inst->draggable = (d != 0);
}
float aiml_prefab_get_width(void* p) {
    auto* inst = (AimlPrefabInstance*)p;
    return inst ? inst->def->width * inst->scale_x : 0;
}
float aiml_prefab_get_height(void* p) {
    auto* inst = (AimlPrefabInstance*)p;
    return inst ? inst->def->height * inst->scale_y : 0;
}

/* ── Child property access ───────────────────────────────────────────────── */

static PrefabChild* find_child(void* p, const char* child_name) {
    auto* inst = (AimlPrefabInstance*)p;
    if (!inst || !child_name) return nullptr;
    auto it = inst->child_index.find(child_name);
    if (it == inst->child_index.end()) return nullptr;
    return &inst->children[it->second];
}

void aiml_prefab_set_text(void* inst, const char* child_name, const char* text) {
    PrefabChild* ch = find_child(inst, child_name);
    if (ch) ch->text = text ? text : "";
}

void aiml_prefab_set_texture(void* inst, const char* child_name, void* tex) {
    PrefabChild* ch = find_child(inst, child_name);
    if (!ch || !tex) return;
    GLuint id = ((AimlGLTexture*)tex)->id;
    ch->texture = id;
}

void aiml_prefab_set_color(void* inst, const char* child_name, int32_t color) {
    PrefabChild* ch = find_child(inst, child_name);
    if (ch) ch->color = (uint32_t)color;
}

void aiml_prefab_set_child_visible(void* inst, const char* child_name, int32_t v) {
    PrefabChild* ch = find_child(inst, child_name);
    if (ch) ch->visible = (v != 0);
}

void* aiml_prefab_child(void* p, const char* child_name) {
    /* Return a child as if it were its own prefab_instance.
     * This is a simplified version — we create a lightweight wrapper instance. */
    auto* inst = (AimlPrefabInstance*)p;
    if (!inst || !child_name) return nullptr;
    auto it = inst->child_index.find(child_name);
    if (it == inst->child_index.end()) return nullptr;

    /* For now, return parent instance — child access is via set_text/set_color etc.
     * A full child-as-instance would need a wrapper struct. */
    return p;
}

/* ── Scene rendering ─────────────────────────────────────────────────────── */

void aiml_prefab_draw_scene(void) {
    if (!g_scene) return;

    /* Update projection to match current GL viewport (handles window resize) */
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] != g_scene->screen_w || vp[3] != g_scene->screen_h) {
        g_scene->screen_w = vp[2];
        g_scene->screen_h = vp[3];
        ortho(g_scene->proj, 0, (float)vp[2], (float)vp[3], 0);
    }

    /* Update u_time uniform source — seconds since first frame. Material
     * shaders read this via `uniform float u_time;`. */
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        double now = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
        if (g_scene->t_start_sec == 0.0) g_scene->t_start_sec = now;
        g_scene->u_time = (float)(now - g_scene->t_start_sec);
    }

    /* Sort instances by z_order (lower z = drawn first = behind) */
    std::stable_sort(g_scene->instances.begin(), g_scene->instances.end(),
              [](AimlPrefabInstance* a, AimlPrefabInstance* b) {
                  return a->z_order < b->z_order;
              });

    /* Enable blending for alpha */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    for (auto* inst : g_scene->instances) {
        render_instance(inst);
    }
}

/* ── Hit testing ─────────────────────────────────────────────────────────── */

void* aiml_prefab_hit_test(float mx, float my) {
    if (!g_scene) return nullptr;

    /* Iterate in reverse z_order (topmost first) */
    for (int i = (int)g_scene->instances.size() - 1; i >= 0; i--) {
        auto* inst = g_scene->instances[i];
        if (!inst->visible) continue;

        float ax, ay;
        get_actual_pos(inst, ax, ay);
        float sx = inst->scale_x, sy = inst->scale_y;
        if (inst->is_relative) { float rsx, rsy; get_rel_scale(rsx, rsy); sx *= rsx; sy *= rsy; }
        float w = inst->def->width * sx;
        float h = inst->def->height * sy;
        if (mx >= ax && mx <= ax + w &&
            my >= ay && my <= ay + h) {
            return inst;
        }
    }
    return nullptr;
}

/* ── Hover tracking ──────────────────────────────────────────────────────── */

static int32_t s_mouse_btn_pressed[3] = {0, 0, 0};

/* Compute a child's screen-space bbox (post-scale, post-instance-position). */
static void child_screen_bbox(AimlPrefabInstance* inst, const PrefabChild& ch,
                              float& bx, float& by, float& bw, float& bh)
{
    float ox, oy;
    get_actual_pos(inst, ox, oy);
    float sx = inst->scale_x, sy = inst->scale_y;
    if (inst->is_relative) {
        float rsx, rsy; get_rel_scale(rsx, rsy);
        sx *= rsx; sy *= rsy;
    }
    bx = ox + ch.x * sx;
    by = oy + ch.y * sy;
    bw = ch.width  * sx;
    bh = ch.height * sy;
}

/* Forward declaration — callback dispatch (defined further below). */
static void prefab_invoke_callback(const std::string& fn_name, AimlPrefabInstance* inst);

/* Apply text/key events from the input event queue to the currently focused
 * text_input child. Mirrors ui_runtime's focus behaviour. */
static void apply_text_events_to_focus() {
    if (!g_scene || !g_scene->focused_input_inst) return;
    AimlPrefabInstance* inst = g_scene->focused_input_inst;
    int32_t idx = g_scene->focused_input_child_idx;
    if (idx < 0 || idx >= (int32_t)inst->children.size()) return;
    PrefabChild& ch = inst->children[idx];
    if (ch.type != CHILD_TEXT_INPUT) return;

    bool changed = false;
    while (aiml_input_has_event()) {
        void* ev = aiml_input_next_event();
        const char* type = aiml_input_event_type(ev);
        if (!type) continue;
        if (strcmp(type, "text") == 0) {
            const char* s = aiml_input_event_char_(ev);
            if (!s) continue;
            ch.text += s;
            ch.cursor_pos = (int32_t)ch.text.size();
            changed = true;
        } else if (strcmp(type, "key_down") == 0) {
            const char* key = aiml_input_event_key(ev);
            if (!key) continue;
            if (strcmp(key, "Backspace") == 0 && !ch.text.empty()) {
                ch.text.pop_back();
                ch.cursor_pos = (int32_t)ch.text.size();
                changed = true;
            } else if (strcmp(key, "Enter") == 0) {
                if (!ch.on_change_fn.empty())
                    prefab_invoke_callback(ch.on_change_fn, inst);
            } else if (strcmp(key, "Escape") == 0) {
                ch.focused = false;
                g_scene->focused_input_inst = nullptr;
                g_scene->focused_input_child_idx = -1;
            }
        }
    }
    if (changed && !ch.on_change_fn.empty()) {
        prefab_invoke_callback(ch.on_change_fn, inst);
    }
}

void aiml_prefab_update_hover(float mx, float my) {
    if (!g_scene) return;

    auto* hit = (AimlPrefabInstance*)aiml_prefab_hit_test(mx, my);

    /* Clear previous hover */
    if (g_scene->hovered && g_scene->hovered != hit) {
        g_scene->hovered->hovered = false;
    }

    /* Set new hover */
    if (hit) {
        hit->hovered = true;
    }
    g_scene->hovered = hit;

    /* Snapshot mouse button pressed state (rising edge) for was_clicked */
    for (int i = 0; i < 3; i++) {
        s_mouse_btn_pressed[i] = aiml_input_mouse_pressed(i);
    }

    /* ── Child-widget input ─────────────────────────────────────────────
     * Walks every UI-widget child across all instances, updates hot/
     * pressed/focused state and dispatches click + change callbacks.
     * This deliberately doesn't gate on which instance "owns" the cursor —
     * a transparent panel must still let widgets through their own bbox. */
    int32_t mb_pressed  = aiml_input_mouse_pressed(0);
    int32_t mb_released = aiml_input_mouse_released(0);

    /* Click-resolution: if the mouse is being pressed, we'll set focus to
     * the topmost text_input under the cursor (else clear focus). */
    AimlPrefabInstance* new_focus_inst = nullptr;
    int32_t              new_focus_idx  = -1;

    for (auto* inst : g_scene->instances) {
        if (!inst->visible) continue;
        for (size_t i = 0; i < inst->children.size(); i++) {
            PrefabChild& ch = inst->children[i];
            if (!ch.visible) continue;
            if (ch.type != CHILD_BUTTON
                && ch.type != CHILD_TEXT_INPUT
                && ch.type != CHILD_CHECKBOX) continue;

            float bx, by, bw, bh;
            child_screen_bbox(inst, ch, bx, by, bw, bh);
            bool over = (mx >= bx && mx < bx + bw && my >= by && my < by + bh);
            ch.hot = over;
            ch.clicked_frame = false;  /* clear from last frame */

            if (ch.type == CHILD_BUTTON) {
                if (mb_pressed && over) ch.pressed_now = true;
                if (mb_released) {
                    if (ch.pressed_now && over) {
                        ch.clicked_frame = true;
                        if (!ch.on_click_fn.empty())
                            prefab_invoke_callback(ch.on_click_fn, inst);
                    }
                    ch.pressed_now = false;
                }
            } else if (ch.type == CHILD_CHECKBOX) {
                if (mb_pressed && over) ch.pressed_now = true;
                if (mb_released) {
                    if (ch.pressed_now && over) {
                        ch.checked = !ch.checked;
                        ch.clicked_frame = true;
                        /* on_click fires on any toggle; on_change too if set. */
                        if (!ch.on_click_fn.empty())
                            prefab_invoke_callback(ch.on_click_fn, inst);
                        if (!ch.on_change_fn.empty())
                            prefab_invoke_callback(ch.on_change_fn, inst);
                    }
                    ch.pressed_now = false;
                }
            } else if (ch.type == CHILD_TEXT_INPUT) {
                if (mb_pressed && over) {
                    new_focus_inst = inst;
                    new_focus_idx  = (int32_t)i;
                }
            }
        }
    }

    /* Apply focus changes on mouse-press only. */
    if (mb_pressed) {
        /* Clear previous focus */
        if (g_scene->focused_input_inst && g_scene->focused_input_child_idx >= 0) {
            auto* old = g_scene->focused_input_inst;
            int32_t oi = g_scene->focused_input_child_idx;
            if (oi < (int32_t)old->children.size())
                old->children[oi].focused = false;
        }
        g_scene->focused_input_inst = new_focus_inst;
        g_scene->focused_input_child_idx = new_focus_idx;
        if (new_focus_inst && new_focus_idx >= 0) {
            new_focus_inst->children[new_focus_idx].focused = true;
        }
    }

    /* Feed keyboard events into focused text_input. */
    apply_text_events_to_focus();
}

void* aiml_prefab_get_hovered(void) {
    return g_scene ? g_scene->hovered : nullptr;
}

int32_t aiml_prefab_is_hovered(void* inst) {
    auto* p = (AimlPrefabInstance*)inst;
    return (p && p->hovered) ? 1 : 0;
}

int32_t aiml_prefab_was_clicked(void* inst, int32_t btn) {
    auto* p = (AimlPrefabInstance*)inst;
    if (!p || !p->hovered || btn < 0 || btn > 2) return 0;
    return s_mouse_btn_pressed[btn];
}

/* ── Material: shader loader + uniform parser ───────────────────────────────
 *
 * Shader files are plain GLSL bodies — no `#version` header. The runtime
 * prepends the platform-specific header (WebGL2 vs desktop), then compiles
 * the fragment shader and links it with the shared PREFAB_VERT vertex
 * shader. Programs are cached per file path so multiple materials that
 * share a shader incur only one compile. */

extern "C" void* aiml_gl_load_texture(const char* path);

static std::string read_text_file(const char* path) {
    if (!path) return "";
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[material] cannot open shader file '%s'\n", path);
        return "";
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string s; s.resize((size_t)n);
    if (n > 0) (void)fread(&s[0], 1, (size_t)n, f);
    fclose(f);
    return s;
}

/* Preamble injected at the top of every user material shader. Brings the
 * standard varyings + built-in uniforms + `out vec4 out_color;` into scope
 * so the user shader only needs to declare custom uniforms + main(). */
static const char* MATERIAL_FRAG_PREAMBLE =
    "in vec2 v_uv;\n"
    "in vec4 v_color;\n"
    "in vec2 v_quad;\n"
    "in vec2 v_style;\n"
    "in vec4 v_bcolor;\n"
    "uniform sampler2D u_tex;\n"
    "uniform int       u_mode;\n"
    "uniform float     u_time;\n"
    "out vec4 out_color;\n";

static GLuint material_get_or_compile(const char* shader_path) {
    if (!g_scene || !shader_path || !*shader_path) return 0;
    auto it = g_scene->shader_programs.find(shader_path);
    if (it != g_scene->shader_programs.end()) return it->second;

    std::string body = read_text_file(shader_path);
    if (body.empty()) return 0;

    /* Prepend platform header + standard preamble so user shaders are
     * minimal (only need to declare custom uniforms + main()). */
    std::string full = std::string(PREFAB_GLSL_HEADER) + MATERIAL_FRAG_PREAMBLE + body;

    GLuint v = compile_shader_src(GL_VERTEX_SHADER, PREFAB_VERT);
    GLuint f = compile_shader_src(GL_FRAGMENT_SHADER, full.c_str());
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetProgramInfoLog(p, 512, nullptr, buf);
        fprintf(stderr, "[material] link error for '%s': %s\n", shader_path, buf);
    }
    glDeleteShader(v);
    glDeleteShader(f);

    g_scene->shader_programs[shader_path] = p;
    return p;
}

static GLuint material_get_or_load_texture(const char* path) {
    if (!g_scene || !path || !*path) return 0;
    auto it = g_scene->material_textures.find(path);
    if (it != g_scene->material_textures.end()) return it->second;
    /* Delegate to gl_runtime — returns AimlGLTexture* or null. */
    void* tex = aiml_gl_load_texture(path);
    if (!tex) return 0;
    GLuint id = ((AimlGLTexture*)tex)->id;
    g_scene->material_textures[path] = id;
    return id;
}

/* JSON parser — light-weight, expects the format described in the header. */
static void material_parse_uniforms_json(Material* m, const char* json) {
    if (!m || !json) return;
    const char* p = json;
    skip_ws(p);
    if (*p != '{') return;
    p++;
    while (*p) {
        skip_ws(p);
        if (*p == '}') { p++; break; }
        if (*p == ',') { p++; continue; }
        if (*p != '"') break;
        std::string key = parse_json_string(p);
        skip_ws(p);
        if (*p == ':') p++;
        skip_ws(p);
        if (*p != '{') {
            /* Malformed entry — skip a token. */
            if (*p == '"') parse_json_string(p);
            else parse_json_number(p);
            continue;
        }
        p++; /* { */
        MaterialUniform u;
        std::string k_tag;
        while (*p && *p != '}') {
            skip_ws(p);
            if (*p == ',') { p++; continue; }
            if (*p == '}') break;
            if (*p != '"') break;
            std::string inner_key = parse_json_string(p);
            skip_ws(p); if (*p == ':') p++; skip_ws(p);
            if (inner_key == "k") {
                k_tag = parse_json_string(p);
            } else if (inner_key == "v") {
                if (k_tag == "f") {
                    u.kind = MU_FLOAT;
                    u.floats[0] = (float)parse_json_number(p);
                } else if (k_tag == "i") {
                    u.kind = MU_INT;
                    u.integer = (int32_t)parse_json_number(p);
                } else if (k_tag == "v2" || k_tag == "v3" || k_tag == "v4") {
                    u.kind = (k_tag == "v2") ? MU_VEC2
                           : (k_tag == "v3") ? MU_VEC3 : MU_VEC4;
                    /* Expect "[n,n,...]" */
                    if (*p == '[') {
                        p++;
                        int idx = 0;
                        while (*p && *p != ']' && idx < 4) {
                            skip_ws(p);
                            if (*p == ',') { p++; continue; }
                            u.floats[idx++] = (float)parse_json_number(p);
                            skip_ws(p);
                        }
                        if (*p == ']') p++;
                    }
                } else if (k_tag == "t") {
                    u.kind = MU_TEXTURE;
                    u.tex_path = parse_json_string(p);
                    /* texture id resolved lazily on first use */
                } else {
                    /* Unknown tag — skip the value. */
                    if (*p == '"') parse_json_string(p);
                    else if (*p == '[') {
                        int depth = 1; p++;
                        while (*p && depth) { if (*p=='[') depth++; else if (*p==']') depth--; p++; }
                    }
                    else parse_json_number(p);
                }
            } else {
                /* Unknown inner key — skip value */
                if (*p == '"') parse_json_string(p);
                else parse_json_number(p);
            }
        }
        if (*p == '}') p++;
        m->uniforms.emplace_back(std::move(key), std::move(u));
    }
}

/* (PendingMaterial + g_pending_materials defined earlier near g_pending_prefabs.) */

static void register_material_def(const char* name, const char* shader_path,
                                  const char* uniforms_json)
{
    auto it = g_scene->materials.find(name);
    Material* m;
    if (it != g_scene->materials.end()) {
        m = it->second;
        m->uniforms.clear();
        m->program = 0;  /* re-compile on next use */
    } else {
        m = new Material();
        m->name = name;
        g_scene->materials[name] = m;
    }
    m->shader_path = shader_path ? shader_path : "";
    if (uniforms_json && *uniforms_json)
        material_parse_uniforms_json(m, uniforms_json);
}

void aiml_material_register(const char* name, const char* shader_path,
                            const char* uniforms_json)
{
    if (!name || !*name) return;
    if (!g_scene) {
        g_pending_materials.push_back({
            name,
            shader_path ? shader_path : "",
            uniforms_json ? uniforms_json : ""
        });
        return;
    }
    register_material_def(name, shader_path, uniforms_json);
}

static Material* material_find(const char* name) {
    if (!g_scene || !name || !*name) return nullptr;
    auto it = g_scene->materials.find(name);
    return it == g_scene->materials.end() ? nullptr : it->second;
}

/* Apply all uniforms of a material to a program. The program MUST already
 * be bound (glUseProgram) before calling this. Built-in uniforms (u_proj,
 * u_tex, u_time, u_mode) are set by the caller; this fn only handles the
 * material's custom uniforms. */
static void material_apply_uniforms(Material* m, GLuint prog) {
    if (!m || !prog) return;
    int tex_slot = 1; /* slot 0 reserved for u_tex */
    for (auto& kv : m->uniforms) {
        GLint loc = glGetUniformLocation(prog, kv.first.c_str());
        if (loc < 0) continue; /* uniform optimized out — skip */
        MaterialUniform& u = kv.second;
        switch (u.kind) {
            case MU_FLOAT: glUniform1f(loc, u.floats[0]); break;
            case MU_VEC2:  glUniform2fv(loc, 1, u.floats); break;
            case MU_VEC3:  glUniform3fv(loc, 1, u.floats); break;
            case MU_VEC4:  glUniform4fv(loc, 1, u.floats); break;
            case MU_INT:   glUniform1i(loc, u.integer); break;
            case MU_TEXTURE: {
                if (u.texture == 0 && !u.tex_path.empty())
                    u.texture = material_get_or_load_texture(u.tex_path.c_str());
                if (u.texture) {
                    glActiveTexture(GL_TEXTURE0 + tex_slot);
                    glBindTexture(GL_TEXTURE_2D, u.texture);
                    glUniform1i(loc, tex_slot);
                    tex_slot++;
                }
                break;
            }
        }
    }
    /* Restore active texture unit */
    glActiveTexture(GL_TEXTURE0);
}

/* ── Callback table & widget property accessors ──────────────────────────── */

static std::unordered_map<std::string, void*> g_callbacks;

void aiml_prefab_register_callback(const char* fn_name, void* fn_ptr) {
    if (!fn_name || !fn_ptr) return;
    g_callbacks[fn_name] = fn_ptr;
}

static void prefab_invoke_callback(const std::string& fn_name, AimlPrefabInstance* inst) {
    auto it = g_callbacks.find(fn_name);
    if (it == g_callbacks.end()) {
        fprintf(stderr, "[prefab] callback '%s' not registered\n", fn_name.c_str());
        return;
    }
    using cb_t = void(*)(void*);
    ((cb_t)it->second)((void*)inst);
}

static PrefabChild* find_child_mut(AimlPrefabInstance* inst, const char* name) {
    if (!inst || !name) return nullptr;
    auto it = inst->child_index.find(name);
    if (it == inst->child_index.end()) return nullptr;
    int idx = it->second;
    if (idx < 0 || idx >= (int)inst->children.size()) return nullptr;
    return &inst->children[idx];
}

const char* aiml_prefab_text_value(void* inst, const char* child_name) {
    auto* ch = find_child_mut((AimlPrefabInstance*)inst, child_name);
    if (!ch) return "";
    return ch->text.c_str();
}

int32_t aiml_prefab_child_clicked(void* inst, const char* child_name) {
    auto* ch = find_child_mut((AimlPrefabInstance*)inst, child_name);
    return (ch && ch->clicked_frame) ? 1 : 0;
}

int32_t aiml_prefab_child_focused(void* inst, const char* child_name) {
    auto* ch = find_child_mut((AimlPrefabInstance*)inst, child_name);
    return (ch && ch->focused) ? 1 : 0;
}

int32_t aiml_prefab_child_checked(void* inst, const char* child_name) {
    auto* ch = find_child_mut((AimlPrefabInstance*)inst, child_name);
    return (ch && ch->checked) ? 1 : 0;
}

void aiml_prefab_set_checked(void* inst, const char* child_name, int32_t checked) {
    auto* ch = find_child_mut((AimlPrefabInstance*)inst, child_name);
    if (ch) ch->checked = (checked != 0);
}

/* ── Drag-and-drop ───────────────────────────────────────────────────────── */

void aiml_prefab_update_drag(float mx, float my, int32_t btn_down, int32_t btn_released) {
    if (!g_scene) return;

    if (btn_down && !g_scene->dragged) {
        /* Start drag: find topmost draggable instance under cursor */
        for (int i = (int)g_scene->instances.size() - 1; i >= 0; i--) {
            auto* inst = g_scene->instances[i];
            if (!inst->visible || !inst->draggable) continue;

            float ax, ay;
            get_actual_pos(inst, ax, ay);
            float isx = inst->scale_x, isy = inst->scale_y;
            if (inst->is_relative) { float rsx, rsy; get_rel_scale(rsx, rsy); isx *= rsx; isy *= rsy; }
            float w = inst->def->width * isx;
            float h = inst->def->height * isy;
            if (mx >= ax && mx <= ax + w &&
                my >= ay && my <= ay + h) {
                inst->is_dragging = true;
                inst->drag_offset_x = mx - ax;
                inst->drag_offset_y = my - ay;
                inst->drag_origin_x = inst->x;
                inst->drag_origin_y = inst->y;
                g_scene->dragged = inst;
                break;
            }
        }
    }

    if (g_scene->dragged && g_scene->dragged->is_dragging) {
        /* Update position — convert back to relative if needed */
        auto* d = g_scene->dragged;
        if (d->is_relative && g_scene->screen_w > 0 && g_scene->screen_h > 0) {
            d->x = (mx - d->drag_offset_x) / g_scene->screen_w;
            d->y = (my - d->drag_offset_y) / g_scene->screen_h;
        } else {
            d->x = mx - d->drag_offset_x;
            d->y = my - d->drag_offset_y;
        }
    }

    if (btn_released && g_scene->dragged) {
        /* End drag */
        g_scene->dragged->is_dragging = false;
        g_scene->dragged = nullptr;
    }
}

void* aiml_prefab_get_dragged(void) {
    return g_scene ? g_scene->dragged : nullptr;
}

void aiml_prefab_cancel_drag(void* p) {
    auto* inst = (AimlPrefabInstance*)p;
    if (!inst) return;
    inst->x = inst->drag_origin_x;
    inst->y = inst->drag_origin_y;
    inst->is_dragging = false;
    if (g_scene && g_scene->dragged == inst)
        g_scene->dragged = nullptr;
}

/* ── Dock to UI grid cell ────────────────────────────────────────────────── */

void aiml_prefab_dock_x(void* p, int32_t col, int32_t colspan) {
    auto* inst = (AimlPrefabInstance*)p;
    if (!inst || inst->is_dragging) return;
    float cx, cy, cw, ch;
    if (aiml_ui_grid_cell_rect(col, 0, colspan, 1, &cx, &cy, &cw, &ch) != 0) return;
    inst->x = cx;
    if (inst->def->width > 0)
        inst->scale_x = cw / inst->def->width;
}

void aiml_prefab_dock_y(void* p, int32_t row, int32_t rowspan) {
    auto* inst = (AimlPrefabInstance*)p;
    if (!inst || inst->is_dragging) return;
    float cx, cy, cw, ch;
    if (aiml_ui_grid_cell_rect(0, row, 1, rowspan, &cx, &cy, &cw, &ch) != 0) return;
    inst->y = cy;
    if (inst->def->height > 0)
        inst->scale_y = ch / inst->def->height;
}

/* ── Font ────────────────────────────────────────────────────────────────── */

static void clear_font_cache(PrefabScene* s) {
    for (auto& kv : s->font_cache) aiml_ui_free_font(kv.second);
    s->font_cache.clear();
}

/* Return a font baked at the requested size, using the cache. */
static AimlUIFont* font_for_size(PrefabScene* s, float font_size) {
    if (!s->font) return nullptr;
    int key = (int)roundf(font_size * 10.0f);
    auto it = s->font_cache.find(key);
    if (it != s->font_cache.end()) return it->second;
    AimlUIFont* f = (AimlUIFont*)aiml_ui_bake_font_for_size(s->font, font_size);
    if (f) s->font_cache[key] = f;
    return f;
}

void aiml_prefab_set_font(void* font) {
    if (!g_scene) return;
    clear_font_cache(g_scene);
    g_scene->font = (AimlUIFont*)font;
}
