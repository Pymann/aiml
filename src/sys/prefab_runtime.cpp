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

#include <GL/glew.h>
#include <GL/gl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

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

/* ── External struct layouts (from gl_runtime / ui_runtime) ──────────────── */

struct AimlGLTexture { GLuint id; };

/* Must match ui_runtime.cpp AimlUIFont exactly */
struct AimlUIFont {
    stbtt_fontinfo info;
    unsigned char* ttf_data;
    GLuint  atlas_tex;
    stbtt_bakedchar cdata[96]; /* ASCII 32..127 */
    float   line_height;
    float   ascent_px;
    float   scale;
    int     atlas_w, atlas_h;
};

/* ── Child types ─────────────────────────────────────────────────────────── */

enum PrefabChildType { CHILD_SPRITE = 0, CHILD_LABEL = 1, CHILD_RECT = 2 };

/* ── Prefab definition (from YAML compile-time) ──────────────────────────── */

struct PrefabChildDef {
    std::string name;
    PrefabChildType type;
    float x, y, width, height;
    float font_size;
    uint32_t color;
    std::string texture_path;
    std::string anchor;  /* "left", "center", "right" */
};

struct PrefabDef {
    std::string name;
    float width, height;
    std::string texture_path;
    bool draggable;
    std::vector<PrefabChildDef> children;
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

    /* shared rendering resources */
    GLuint quad_vao, quad_vbo;
    GLuint shader;
    GLuint white_tex;          /* 1×1 white pixel for rects */
    float  proj[16];           /* orthographic projection matrix */
    int32_t screen_w, screen_h;
    int32_t design_w, design_h;   /* original design resolution (from scene_init) */

    AimlUIFont* font;          /* default font for labels */

    /* prefab registry */
    std::unordered_map<std::string, PrefabDef*> defs;
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

/* ── Shader ──────────────────────────────────────────────────────────────── */

static const char* PREFAB_VERT = R"(
#version 330 core
layout(location=0) in vec2 a_pos;
layout(location=1) in vec2 a_uv;
layout(location=2) in vec4 a_color;
uniform mat4 u_proj;
out vec2 v_uv;
out vec4 v_color;
void main(){
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
    v_uv    = a_uv;
    v_color = a_color;
}
)";

static const char* PREFAB_FRAG = R"(
#version 330 core
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_tex;
uniform int u_mode;
out vec4 out_color;
void main(){
    if (u_mode == 1) {
        /* RGBA texture (sprite) */
        out_color = texture(u_tex, v_uv) * v_color;
    } else if (u_mode == 2) {
        /* Font atlas (GL_RED channel = alpha) */
        float a = texture(u_tex, v_uv).r;
        out_color = vec4(v_color.rgb, v_color.a * a);
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

/* ── Vertex format: pos(2) + uv(2) + color(4 bytes packed as float) ────── */

struct PrefabVertex {
    float x, y;
    float u, v;
    uint32_t rgba;
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
    int mode;              /* 0=solid, 1=rgba_texture, 2=font_atlas */
    std::vector<PrefabVertex> verts;
};

static void push_quad(DrawBatch& batch, float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1, uint32_t rgba)
{
    PrefabVertex tl = {x,     y,     u0, v0, rgba};
    PrefabVertex tr = {x + w, y,     u1, v0, rgba};
    PrefabVertex bl = {x,     y + h, u0, v1, rgba};
    PrefabVertex br = {x + w, y + h, u1, v1, rgba};
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

    glUseProgram(s->shader);
    glUniformMatrix4fv(glGetUniformLocation(s->shader, "u_proj"), 1, GL_FALSE, s->proj);
    glUniform1i(glGetUniformLocation(s->shader, "u_mode"), batch.mode);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, batch.tex);
    glUniform1i(glGetUniformLocation(s->shader, "u_tex"), 0);

    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)batch.verts.size());
    glBindVertexArray(0);
}

/* ── Text rendering into a batch ─────────────────────────────────────────── */

static float text_width_font(AimlUIFont* font, const char* text) {
    if (!font || !text) return 0;
    float w = 0;
    while (*text) {
        unsigned char c = (unsigned char)*text++;
        if (c < 32 || c >= 128) continue;
        w += font->cdata[c - 32].xadvance;
    }
    return w;
}

static void push_text_batch(DrawBatch& batch, AimlUIFont* font,
                            const char* text, float cx, float cy,
                            float max_w, uint32_t rgba, const char* anchor)
{
    if (!font || !text || !*text) return;

    /* Measure for alignment */
    float tw = text_width_font(font, text);

    float x;
    if (anchor && strcmp(anchor, "center") == 0) {
        x = cx - tw * 0.5f;
    } else if (anchor && strcmp(anchor, "right") == 0) {
        x = cx - tw;
    } else {
        x = cx; /* left */
    }
    float y = cy + font->ascent_px;
    float limit = x + max_w;

    const char* p = text;
    while (*p) {
        unsigned char c = (unsigned char)*p++;
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
    if (strcmp(s, "label") == 0) return CHILD_LABEL;
    if (strcmp(s, "rect") == 0) return CHILD_RECT;
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

/* ── Render one instance ─────────────────────────────────────────────────── */

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

    /* 1) Base texture (card frame etc.) */
    if (inst->base_texture) {
        DrawBatch batch;
        batch.tex = inst->base_texture;
        batch.mode = 1; /* rgba texture */
        uint32_t col = color_with_alpha(0xFFFFFF, alpha);
        push_quad(batch, ox, oy,
                  inst->def->width * sx, inst->def->height * sy,
                  0, 1, 1, 0, col);
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
            flush_batch(batch);
            if (sprite_log_count < 3) {
                GLenum err = glGetError();
                GLboolean is_tex = glIsTexture(ch.texture);
                fprintf(stderr, "[prefab] RENDER sprite '%s' tex=%u pos=(%.0f,%.0f) sz=(%.0f,%.0f) col=0x%08X glErr=%u isTex=%d\n",
                        ch.name.c_str(), ch.texture, cx, cy, cw, ch_h, col, (unsigned)err, (int)is_tex);
                sprite_log_count++;
            }
            break;
        }
        case CHILD_RECT: {
            DrawBatch batch;
            batch.tex = s->white_tex;
            batch.mode = 0;
            uint32_t col = color_with_alpha(ch.color, alpha);
            push_quad(batch, cx, cy, cw, ch_h, 0, 0, 1, 1, col);
            flush_batch(batch);
            break;
        }
        case CHILD_LABEL: {
            if (ch.text.empty() || !s->font) break;
            DrawBatch batch;
            batch.tex = s->font->atlas_tex;
            batch.mode = 2; /* font atlas */
            uint32_t col = color_with_alpha(ch.color, alpha);
            /* anchor handling: cx is the x reference point */
            const char* anchor = "left";
            /* Find the child def for anchor info */
            auto it = inst->child_index.find(ch.name);
            if (it != inst->child_index.end() && inst->def) {
                int idx = it->second;
                if (idx >= 0 && idx < (int)inst->def->children.size()) {
                    anchor = inst->def->children[idx].anchor.c_str();
                }
            }
            push_text_batch(batch, s->font, ch.text.c_str(),
                           cx, cy, inst->def->width * sx, col, anchor);
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
    fprintf(stderr, "[prefab] set_texture('%s', tex=%p)\n", child_name ? child_name : "null", tex);
    PrefabChild* ch = find_child(inst, child_name);
    if (!ch) { fprintf(stderr, "[prefab]   child NOT found!\n"); return; }
    if (!tex) { fprintf(stderr, "[prefab]   tex is NULL!\n"); return; }
    GLuint id = ((AimlGLTexture*)tex)->id;
    fprintf(stderr, "[prefab]   child found, type=%d, setting GL tex id=%u\n", ch->type, id);
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

void aiml_prefab_set_font(void* font) {
    if (!g_scene) return;
    g_scene->font = (AimlUIFont*)font;
}
