/*
 * ui_runtime.cpp — AIML ui namespace runtime (44.3)
 *
 * Immediate-Mode UI: Grid-Layout + 2D Batch-Renderer + stb_truetype Font-Rendering.
 * Renders directly via OpenGL 3.3 Core Profile.
 *
 * Dependencies (all already in exodus.json / libstb-dev):
 *   stb_truetype  — font atlas baking
 *   GL / GLEW     — rendering
 *   input_runtime — mouse/key polling
 *   engine_runtime — window size + DPI
 */

#include <GL/glew.h>
#include <GL/gl.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include "ui_runtime.h"
#include "input_runtime.h"
#include "engine_runtime.h"

/* ── AimlUIStyle C struct ─────────────────────────────────────────────────────
 * LLVM struct {i32×5, f32×6, f32(padding_x), i8*, f32×3, f32(_pad2)} — 17 fields, 72 bytes.
 * Field order matches GEP indices in builtin_registry.cpp.
 * ─────────────────────────────────────────────────────────────────────────── */
struct AimlUIStyle {
    int32_t color;          /* 0  — foreground (text/icons)   RGBA i32 */
    int32_t bg_color;       /* 1  — background                        */
    int32_t hover_color;    /* 2  — hover background                  */
    int32_t active_color;   /* 3  — active/click background           */
    int32_t border_color;   /* 4  — border                            */
    float   font_size;      /* 5  — logical pixels                    */
    float   border_width;   /* 6  — logical pixels                    */
    float   corner_radius;  /* 7  — logical pixels                    */
    float   padding_h;      /* 8  — horizontal padding (logical px)   */
    float   padding_v;      /* 9  — vertical padding   (logical px)   */
    float   opacity;        /* 10 — 0.0=invisible, 1.0=opaque         */
    float   padding_x;      /* 11 — was _pad; horiz outside margin as fraction of cell width */
    void*   font;           /* 12 — AimlUIFont* (i8*)                 */
    float   padding_y;      /* 13 — vert outside margin as fraction of cell height */
    float   size_w;         /* 14 — absolute widget width in logical px; -1.0 = fill */
    float   size_h;         /* 15 — absolute widget height in logical px; -1.0 = auto */
    float   _pad2;          /* 16 — alignment reserve                 */
};
static_assert(sizeof(AimlUIStyle) == 72, "AimlUIStyle must be 72 bytes");
static_assert(offsetof(AimlUIStyle, font) == 48, "font must be at offset 48");

/* ── Internal structs ────────────────────────────────────────────────────── */

struct AimlUIFont {
    stbtt_fontinfo info;
    unsigned char* ttf_data;   /* malloc'd copy */
    GLuint  atlas_tex;
    stbtt_bakedchar cdata[96]; /* ASCII 32..127 */
    float   line_height;
    float   ascent_px;  /* pixels from top-of-line-box to baseline */
    float   scale;
    int     atlas_w, atlas_h;
};

struct UiVertex {
    float    x, y;        /* screen position */
    float    u, v;        /* UV 0..1 (or atlas coords for text) */
    float    qw, qh;      /* quad size in px — 0 = skip SDF (text mode) */
    float    radius;      /* corner radius in px */
    float    border_w;    /* border width in px */
    uint32_t rgba;        /* fill  color (ABGR byte order for GL) */
    uint32_t border_rgba; /* border color */
};

struct UiDrawCmd {
    GLuint tex;
    int    offset;
    int    count;
    int    image_mode; /* 1 = full RGBA sample (images), 0 = .r only (font/solid) */
    int    scissor[4]; /* x, y, w, h in physical pixels (y=0 at BOTTOM for GL); all 0 = disabled */
};

/* Raw buffer layout: {i32 size, i32 capacity, i32 read_idx, i32 write_idx, u8* data} */
struct AimlGLRawBuf {
    int32_t  size;
    int32_t  capacity;
    int32_t  read_idx;
    int32_t  write_idx;
    uint8_t* data;
};

/* AimlGLTexture layout (from gl_runtime.cpp) */
struct AimlGLTexture {
    GLuint id;
    int    width, height;
    int    channels;
};

/* ── Widget style presets ─────────────────────────────────────────────────── *
 * Each carries a full AimlUIStyle snapshot + widget-specific texture slots.
 * Created via ui.XXX_style_new() — captures current style stack top.
 * Pass as last arg to widget functions; null = use global style unchanged.
 * ─────────────────────────────────────────────────────────────────────────── */
struct AimlUISliderStyle {
    AimlUIStyle base;
    GLuint thumb_tex; int thumb_w, thumb_h;
    GLuint track_tex; int track_w, track_h;
};
struct AimlUIButtonStyle {
    AimlUIStyle base;
    GLuint bg_tex;     int bg_w,     bg_h;
    GLuint hover_tex;  int hover_w,  hover_h;
    GLuint active_tex; int active_w, active_h;
};
struct AimlUILabelStyle {
    AimlUIStyle base;  /* font is in base.font; all appearance via base */
};
struct AimlUIProgressStyle {
    AimlUIStyle base;
    GLuint fill_tex; int fill_w, fill_h;
    GLuint bg_tex;   int bg_w,   bg_h;
};
struct AimlUIPanelStyle {
    AimlUIStyle base;
    GLuint bg_tex; int bg_w, bg_h;
};

struct UiGridState {
    float x, y, w, h;
    float col_offsets[64];
    float row_offsets[64];
    float col_sizes[64];
    float row_sizes[64];
    int   num_cols;
    int   num_rows;
    float gap_h, gap_v;
};

struct UiCellState {
    float x, y, w, h;
    float cursor_y;
    float prev_item_h;
    bool  same_line;
    char  align_h[16];
    char  align_v[16];
};

struct WidgetState {
    bool hovered;
    bool active;
    bool focused;
};

struct AimlUIState {
    /* Renderer */
    GLuint vao, vbo, prog;
    GLuint white_tex;
    GLint  u_proj, u_tex, u_image_mode;

    /* Window */
    int   win_w, win_h;
    float dpi;

    /* Draw list */
    std::vector<UiVertex>  verts;
    std::vector<UiDrawCmd> cmds;

    /* Layout stack */
    std::vector<UiGridState> grid_stack;
    std::vector<UiCellState> cell_stack;

    /* Style stack */
    std::vector<AimlUIStyle> style_stack;
    AimlUIFont* default_font;

    /* Pending gap for next begin_grid */
    float next_gap_h, next_gap_v;

    /* Pending alignment for next begin_cell */
    char next_align_h[16];
    char next_align_v[16];

    /* Widget state */
    std::unordered_map<uint32_t, WidgetState> widget_states;
    uint32_t hot;
    uint32_t active_w;
    uint32_t focused;

    /* Input snapshot */
    float mouse_x, mouse_y;
    bool  mouse_pressed;
    bool  mouse_released;
    bool  mouse_down;

    /* Text input buffers */
    std::unordered_map<uint32_t, std::string> text_bufs;

    bool initialized;
};

static AimlUIState* g_ui = nullptr;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static uint32_t fnv1a(const char* s) {
    uint32_t h = 0x811c9dc5u;
    while (*s) { h ^= (uint8_t)*s++; h *= 0x01000193u; }
    return h;
}

static uint32_t rgba_to_gl(int32_t rgba) {
    /* Input: 0xRRGGBBAA, output: same (little-endian RGBA bytes) */
    uint8_t r = (rgba >> 24) & 0xFF;
    uint8_t g = (rgba >> 16) & 0xFF;
    uint8_t b = (rgba >>  8) & 0xFF;
    uint8_t a = (rgba      ) & 0xFF;
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

static AimlUIStyle default_style() {
    AimlUIStyle s{};
    s.color        = (int32_t)0xEEEEEEFF;   /* light gray text */
    s.bg_color     = (int32_t)0x2A2A3EFF;   /* dark blue-gray background */
    s.hover_color  = (int32_t)0x3D3D6AFF;   /* brighter on hover */
    s.active_color = (int32_t)0x5566CCFF;   /* blue on click */
    s.border_color = (int32_t)0x6666AAFF;   /* visible border */
    s.font_size    = 16.0f;
    s.border_width = 1.0f;
    s.corner_radius= 3.0f;
    s.padding_h    = 8.0f;
    s.padding_v    = 4.0f;
    s.opacity      = 1.0f;
    s.padding_x    = 0.0f;
    s.font         = nullptr;
    s.padding_y    = 0.0f;
    s.size_w       = -1.0f;
    s.size_h       = -1.0f;
    s._pad2        = 0.0f;
    return s;
}

static AimlUIStyle& cur_style() {
    return g_ui->style_stack.back();
}

/* ── Batch renderer ──────────────────────────────────────────────────────── */

static void ensure_cmd(GLuint tex, int image_mode) {
    if (g_ui->cmds.empty()
        || g_ui->cmds.back().tex != tex
        || g_ui->cmds.back().image_mode != image_mode) {
        UiDrawCmd c;
        c.tex        = tex;
        c.offset     = (int)g_ui->verts.size();
        c.count      = 0;
        c.image_mode = image_mode;
        c.scissor[0] = c.scissor[1] = c.scissor[2] = c.scissor[3] = 0;
        g_ui->cmds.push_back(c);
    }
}

/* use_sdf=false  → qw=0,qh=0  (text glyphs: no SDF clipping)
 * use_sdf=true   → qw=w,qh=h  (widgets: SDF rounded-rect) */
static void push_quad(float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1,
                      uint32_t rgba, GLuint tex, int image_mode = 0,
                      float radius = 0.0f, float border_w = 0.0f,
                      uint32_t border_rgba = 0, bool use_sdf = true)
{
    ensure_cmd(tex, image_mode);
    float qw = use_sdf ? w : 0.0f;
    float qh = use_sdf ? h : 0.0f;
    UiVertex tl = {x,   y,   u0, v0, qw, qh, radius, border_w, rgba, border_rgba};
    UiVertex tr = {x+w, y,   u1, v0, qw, qh, radius, border_w, rgba, border_rgba};
    UiVertex bl = {x,   y+h, u0, v1, qw, qh, radius, border_w, rgba, border_rgba};
    UiVertex br = {x+w, y+h, u1, v1, qw, qh, radius, border_w, rgba, border_rgba};
    g_ui->verts.push_back(tl); g_ui->verts.push_back(tr); g_ui->verts.push_back(bl);
    g_ui->verts.push_back(tr); g_ui->verts.push_back(br); g_ui->verts.push_back(bl);
    g_ui->cmds.back().count += 6;
}

static void push_text(AimlUIFont* font, const char* text,
                      float cx, float cy, float max_w, uint32_t rgba)
{
    if (!font || !text) return;
    float x = cx;
    float y = cy + font->ascent_px;  /* cy = top of line box; shift to baseline */
    float limit = cx + max_w;
    while (*text) {
        unsigned char c = (unsigned char)*text++;
        if (c < 32 || c >= 128) continue;
        stbtt_bakedchar* bc = &font->cdata[c - 32];
        float x0 = x + bc->xoff;
        float y0 = y + bc->yoff;
        float x1 = x0 + (bc->x1 - bc->x0);
        float y1 = y0 + (bc->y1 - bc->y0);
        if (x1 > limit) break; /* soft clip */
        float u0 = (float)bc->x0 / font->atlas_w;
        float v0 = (float)bc->y0 / font->atlas_h;
        float u1 = (float)bc->x1 / font->atlas_w;
        float v1 = (float)bc->y1 / font->atlas_h;
        push_quad(x0, y0, x1-x0, y1-y0, u0, v0, u1, v1, rgba, font->atlas_tex,
                  0, 0.0f, 0.0f, 0, false);  /* use_sdf=false: no SDF for text glyphs */
        x += bc->xadvance;
    }
}

static float text_width(AimlUIFont* font, const char* text) {
    if (!font || !text) return 0;
    float w = 0;
    while (*text) {
        unsigned char c = (unsigned char)*text++;
        if (c < 32 || c >= 128) continue;
        w += font->cdata[c - 32].xadvance;
    }
    return w;
}

/* ── Widget placement within current cell ────────────────────────────────── */

static UiCellState& cur_cell() {
    return g_ui->cell_stack.back();
}

static float cur_dpi() { return g_ui->dpi > 0.1f ? g_ui->dpi : 1.0f; }

/* logical pixels → physical pixels */
static float lp(float x) { return x * cur_dpi(); }

/* Get item start position within cell, advance cursor */
static void cell_advance(float item_w, float item_h, float* out_x, float* out_y) {
    UiCellState& cell = cur_cell();
    float x, y;
    if (cell.same_line) {
        /* Place to the right of previous item */
        x = cell.x + cell.cursor_y;  /* cursor_y used as x when same_line */
        y = cell.y + cell.prev_item_h;
        cell.same_line = false;
    } else {
        /* Alignment */
        if (strcmp(cell.align_h, "center") == 0)      x = cell.x + (cell.w - item_w) * 0.5f;
        else if (strcmp(cell.align_h, "right") == 0)  x = cell.x + cell.w - item_w;
        else                                           x = cell.x;
        if (strcmp(cell.align_v, "middle") == 0)      y = cell.y + (cell.h - item_h) * 0.5f;
        else if (strcmp(cell.align_v, "bottom") == 0) y = cell.y + cell.h - item_h;
        else                                           y = cell.y + cell.cursor_y;
        cell.cursor_y += item_h + lp(2.0f);
    }
    cell.prev_item_h = item_h;
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}

/* ── Scissor helper ──────────────────────────────────────────────────────── */

/* Push a new draw cmd that inherits tex/image_mode from the previous one but
 * sets a new scissor rect.  scissor[2]==0 means "disable scissor". */
static void push_scissor_cmd(int scissor[4]) {
    if (g_ui->cmds.empty()) return;
    UiDrawCmd c = g_ui->cmds.back();
    c.offset = (int)g_ui->verts.size();
    c.count  = 0;
    memcpy(c.scissor, scissor, sizeof(c.scissor));
    g_ui->cmds.push_back(c);
}

/* ── Widget layout helper ────────────────────────────────────────────────── */

/* Returned by apply_widget_layout */
struct WidgetRect { float x, y, w, h; };

/* Compute widget x/y/w/h honouring padding_x/y and size_w/h from cur_style().
 * cell_x/y/w = base cell position and width.
 * cell_h_avail = available height (pass 0 when unknown, skips padding_y on height).
 * auto_h       = content-driven height (used when size_h == -1).
 * out_scissor[4] is set to the padded cell rect in physical pixels (GL y=0 at bottom)
 * if clipping is needed, or all-zeros if no clipping required.
 * Returns the final widget rect {x, y, w, h}. */
static WidgetRect apply_widget_layout(float cell_x, float cell_y,
                                       float cell_w, float cell_h_avail,
                                       float auto_h,
                                       int out_scissor[4]) {
    const AimlUIStyle& st = cur_style();
    float dpi = cur_dpi();

    /* Padded available area */
    float px = st.padding_x, py = st.padding_y;
    float avail_x = cell_x + cell_w * px;
    float avail_w = cell_w * (1.0f - 2.0f * px);
    float avail_y = cell_y + (cell_h_avail > 0 ? cell_h_avail * py : 0.0f);
    float avail_h = (cell_h_avail > 0) ? cell_h_avail * (1.0f - 2.0f * py) : auto_h;

    /* Widget size */
    float ww = (st.size_w > 0.0f) ? st.size_w * dpi : avail_w;
    float wh = (st.size_h > 0.0f) ? st.size_h * dpi : auto_h;

    /* Center widget within padded area */
    float wx = avail_x + (avail_w - fminf(ww, avail_w)) * 0.5f;
    float wy = avail_y;

    /* Scissor: needed only when widget exceeds available area */
    bool needs_scissor = (ww > avail_w + 0.5f || wh > avail_h + 0.5f);
    if (out_scissor) {
        if (needs_scissor) {
            int win_h = g_ui->win_h;
            int sx = (int)(avail_x);
            int sy = (int)(avail_y);
            int sw = (int)(avail_w + 0.5f);
            int sh = (int)(avail_h + 0.5f);
            out_scissor[0] = sx;
            out_scissor[1] = win_h - (sy + sh);   /* flip Y for GL */
            out_scissor[2] = sw;
            out_scissor[3] = sh;
        } else {
            out_scissor[0] = out_scissor[1] = out_scissor[2] = out_scissor[3] = 0;
        }
    }

    return {wx, wy, ww, wh};
}

/* ── Hit testing ─────────────────────────────────────────────────────────── */

static bool point_in_rect(float px, float py, float rx, float ry, float rw, float rh) {
    return px >= rx && px < rx+rw && py >= ry && py < ry+rh;
}

static bool widget_hot(uint32_t id, float x, float y, float w, float h) {
    return point_in_rect(g_ui->mouse_x, g_ui->mouse_y, x, y, w, h);
}

/* ── Layout computation ───────────────────────────────────────────────────── */

/* Compute cell sizes from a buffer<f32>.
 * fixed_fraction < 1.0; flexible == 1.0 shares the remainder equally. */
static void compute_sizes(AimlGLRawBuf* buf, float total, float gap,
                          int* n_out, float* offsets, float* sizes) {
    if (!buf || !buf->data || buf->size <= 0) {
        *n_out = 1;
        offsets[0] = 0; sizes[0] = total;
        return;
    }
    int n = buf->size / (int)sizeof(float);
    if (n > 63) n = 63;
    float* vals = (float*)buf->data;

    /* Count flexible slots */
    float fixed_sum = 0;
    int   flex_count = 0;
    for (int i = 0; i < n; i++) {
        if (vals[i] < 1.0f - 1e-4f) fixed_sum += vals[i];
        else                          flex_count++;
    }
    float gap_total = gap * (float)(n - 1);
    float avail     = total - gap_total;
    float fixed_px  = fixed_sum * avail;
    float flex_px   = (flex_count > 0) ? (avail - fixed_px) / (float)flex_count : 0;

    float offset = 0;
    for (int i = 0; i < n; i++) {
        float sz = (vals[i] < 1.0f - 1e-4f) ? vals[i] * avail : flex_px;
        offsets[i] = offset;
        sizes[i]   = sz;
        offset += sz + gap;
    }
    *n_out = n;
}

/* ── Shader ──────────────────────────────────────────────────────────────── */

static const char* VERT_SRC = R"(
#version 330 core
layout(location=0) in vec2 a_pos;
layout(location=1) in vec2 a_uv;
layout(location=2) in vec2 a_quad;    /* qw, qh */
layout(location=3) in vec2 a_style;   /* radius, border_w */
layout(location=4) in vec4 a_color;
layout(location=5) in vec4 a_bcolor;
uniform mat4 u_proj;
out vec2 v_uv;
out vec2 v_quad;
out vec2 v_style;
out vec4 v_color;
out vec4 v_bcolor;
void main(){
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
    v_uv    = a_uv;
    v_quad  = a_quad;
    v_style = a_style;
    v_color = a_color;
    v_bcolor = a_bcolor;
}
)";

static const char* FRAG_SRC = R"(
#version 330 core
in vec2 v_uv;
in vec2 v_quad;    /* qw, qh; 0 = skip SDF (text mode) */
in vec2 v_style;   /* radius, border_w */
in vec4 v_color;
in vec4 v_bcolor;
uniform sampler2D u_tex;
uniform int       u_image_mode;
out vec4 out_color;

float sdf_rrect(vec2 p, vec2 half_size, float r) {
    vec2 q = abs(p) - half_size + r;
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

void main(){
    /* Texture sampling */
    vec4 tex_col;
    if (u_image_mode == 1)
        tex_col = texture(u_tex, v_uv);
    else
        tex_col = vec4(1.0, 1.0, 1.0, texture(u_tex, v_uv).r);
    vec4 col = v_color * tex_col;

    /* SDF rounded-rect clipping (skipped for text when qw == 0) */
    if (v_quad.x > 0.5) {
        vec2  hs   = v_quad * 0.5;          /* half-size of widget quad */
        vec2  p    = v_uv * v_quad - hs;
        float r    = min(v_style.x, min(hs.x, hs.y));
        float bw   = v_style.y;
        float aa   = max(fwidth(p.x), fwidth(p.y));

        /* Outer shape */
        float d_out = sdf_rrect(p, hs, r);
        float outer = 1.0 - smoothstep(-aa, aa, d_out);

        /* Border: blend border color inside the outer ring */
        vec4 final_col = col;
        if (bw > 0.5) {
            float d_in    = sdf_rrect(p, hs - vec2(bw), max(r - bw, 0.0));
            float in_mask = 1.0 - smoothstep(-aa, aa, d_in);
            final_col = mix(col, v_bcolor, in_mask);
        }
        final_col.a *= outer;
        out_color = final_col;
    } else {
        out_color = col;
    }
}
)";

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetShaderInfoLog(s, 512, nullptr, buf);
        fprintf(stderr, "[ui] shader error: %s\n", buf);
    }
    return s;
}

/* ── Font loading ─────────────────────────────────────────────────────────── */

static AimlUIFont* bake_font(unsigned char* ttf_data, int ttf_size, float size_px) {
    AimlUIFont* f = new AimlUIFont();
    f->ttf_data   = (unsigned char*)malloc(ttf_size);
    memcpy(f->ttf_data, ttf_data, ttf_size);

    /* Bake at requested size into atlas */
    f->atlas_w = 512; f->atlas_h = 512;
    unsigned char* bitmap = (unsigned char*)calloc(f->atlas_w * f->atlas_h, 1);
    while (true) {
        int r = stbtt_BakeFontBitmap(f->ttf_data, 0, size_px,
                                     bitmap, f->atlas_w, f->atlas_h,
                                     32, 96, f->cdata);
        if (r > 0 || f->atlas_h >= 2048) break;
        f->atlas_h *= 2;
        bitmap = (unsigned char*)realloc(bitmap, f->atlas_w * f->atlas_h);
        memset(bitmap, 0, f->atlas_w * f->atlas_h);
    }

    glGenTextures(1, &f->atlas_tex);
    glBindTexture(GL_TEXTURE_2D, f->atlas_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, f->atlas_w, f->atlas_h,
                 0, GL_RED, GL_UNSIGNED_BYTE, bitmap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    free(bitmap);

    stbtt_InitFont(&f->info, f->ttf_data,
                   stbtt_GetFontOffsetForIndex(f->ttf_data, 0));
    f->scale = stbtt_ScaleForPixelHeight(&f->info, size_px);

    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&f->info, &ascent, &descent, &line_gap);
    f->line_height = (float)(ascent - descent + line_gap) * f->scale;
    f->ascent_px   = (float)ascent * f->scale;  /* baseline offset from top of line box */

    return f;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

extern "C" {

void aiml_ui_init(void) {
    if (g_ui && g_ui->initialized) return;
    if (!g_ui) g_ui = new AimlUIState();

    /* Compile UI shader */
    GLuint vs = compile_shader(GL_VERTEX_SHADER,   VERT_SRC);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, FRAG_SRC);
    g_ui->prog = glCreateProgram();
    glAttachShader(g_ui->prog, vs);
    glAttachShader(g_ui->prog, fs);
    glLinkProgram(g_ui->prog);
    glDeleteShader(vs); glDeleteShader(fs);

    g_ui->u_proj       = glGetUniformLocation(g_ui->prog, "u_proj");
    g_ui->u_tex        = glGetUniformLocation(g_ui->prog, "u_tex");
    g_ui->u_image_mode = glGetUniformLocation(g_ui->prog, "u_image_mode");

    /* VAO / VBO */
    glGenVertexArrays(1, &g_ui->vao);
    glGenBuffers(1, &g_ui->vbo);
    glBindVertexArray(g_ui->vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_ui->vbo);
    for (int i = 0; i < 6; i++) glEnableVertexAttribArray(i);
    /* 0: pos (vec2 float) */
    glVertexAttribPointer(0, 2, GL_FLOAT,         GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, x));
    /* 1: uv  (vec2 float) */
    glVertexAttribPointer(1, 2, GL_FLOAT,         GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, u));
    /* 2: quad size (vec2 float: qw, qh) */
    glVertexAttribPointer(2, 2, GL_FLOAT,         GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, qw));
    /* 3: style (vec2 float: radius, border_w) */
    glVertexAttribPointer(3, 2, GL_FLOAT,         GL_FALSE, sizeof(UiVertex), (void*)offsetof(UiVertex, radius));
    /* 4: fill color (4×u8 normalized → vec4) */
    glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(UiVertex), (void*)offsetof(UiVertex, rgba));
    /* 5: border color (4×u8 normalized → vec4) */
    glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(UiVertex), (void*)offsetof(UiVertex, border_rgba));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    /* 1×1 white texture (solid color quads) */
    uint32_t white = 0xFFFFFFFF;
    glGenTextures(1, &g_ui->white_tex);
    glBindTexture(GL_TEXTURE_2D, g_ui->white_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Default style */
    g_ui->style_stack.push_back(default_style());
    g_ui->next_gap_h = 4.0f;
    g_ui->next_gap_v = 4.0f;
    strcpy(g_ui->next_align_h, "left");
    strcpy(g_ui->next_align_v, "top");
    g_ui->hot = g_ui->active_w = g_ui->focused = 0;
    g_ui->default_font = nullptr;
    g_ui->initialized  = true;
}

void aiml_ui_begin(void* win) {
    if (!g_ui) aiml_ui_init();

    /* Read window size */
    g_ui->win_w = aiml_engine_window_size_x(win);
    g_ui->win_h = aiml_engine_window_size_y(win);
    g_ui->dpi   = aiml_engine_dpi();

    /* Snapshot input state */
    g_ui->mouse_x         = aiml_input_mouse_pos_x();
    g_ui->mouse_y         = aiml_input_mouse_pos_y();
    g_ui->mouse_pressed   = aiml_input_mouse_pressed(0)  != 0;
    g_ui->mouse_released  = aiml_input_mouse_released(0) != 0;
    g_ui->mouse_down      = aiml_input_mouse_button(0)   != 0;

    /* Reset draw list */
    g_ui->verts.clear();
    g_ui->cmds.clear();
    g_ui->grid_stack.clear();
    g_ui->cell_stack.clear();

    /* Reset hot (re-evaluated each frame) */
    g_ui->hot = 0;

    /* Consume text events for focused widget */
    if (g_ui->focused != 0) {
        while (aiml_input_has_event()) {
            void* ev = aiml_input_next_event();
            const char* type = aiml_input_event_type(ev);
            if (!type) continue;
            if (strcmp(type, "text") == 0) {
                const char* ch = aiml_input_event_char_(ev);
                if (ch) g_ui->text_bufs[g_ui->focused] += ch;
            } else if (strcmp(type, "key_down") == 0) {
                const char* key = aiml_input_event_key(ev);
                if (key && strcmp(key, "Backspace") == 0) {
                    auto it = g_ui->text_bufs.find(g_ui->focused);
                    if (it != g_ui->text_bufs.end() && !it->second.empty())
                        it->second.pop_back();
                }
            }
        }
    }
}

void aiml_ui_end(void) {
    if (!g_ui || !g_ui->initialized) return;
    if (g_ui->verts.empty()) return;

    /* Upload vertices */
    glBindBuffer(GL_ARRAY_BUFFER, g_ui->vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(g_ui->verts.size() * sizeof(UiVertex)),
                 g_ui->verts.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* Ensure viewport covers the full framebuffer (handles window resize) */
    glViewport(0, 0, g_ui->win_w, g_ui->win_h);

    /* Ortho projection: y increases downward */
    float W = (float)g_ui->win_w, H = (float)g_ui->win_h;
    float proj[16] = {
        2.0f/W, 0,       0, 0,
        0,     -2.0f/H,  0, 0,
        0,      0,      -1, 0,
       -1.0f,   1.0f,    0, 1
    };

    /* GL state for 2D UI */
    GLboolean prev_depth; glGetBooleanv(GL_DEPTH_TEST, &prev_depth);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(g_ui->prog);
    glUniformMatrix4fv(g_ui->u_proj, 1, GL_FALSE, proj);
    glUniform1i(g_ui->u_tex, 0);

    glBindVertexArray(g_ui->vao);
    glActiveTexture(GL_TEXTURE0);

    for (auto& cmd : g_ui->cmds) {
        if (cmd.count == 0) continue;
        if (cmd.scissor[2] > 0 && cmd.scissor[3] > 0) {
            glEnable(GL_SCISSOR_TEST);
            glScissor(cmd.scissor[0], cmd.scissor[1], cmd.scissor[2], cmd.scissor[3]);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
        glBindTexture(GL_TEXTURE_2D, cmd.tex);
        glUniform1i(g_ui->u_image_mode, cmd.image_mode);
        glDrawArrays(GL_TRIANGLES, cmd.offset, cmd.count);
    }
    glDisable(GL_SCISSOR_TEST);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    if (prev_depth) glEnable(GL_DEPTH_TEST);

    /* Active widget cleared if mouse released */
    if (g_ui->mouse_released) g_ui->active_w = 0;
}

/* ── Forward declarations for draw helpers used in begin_cell ────────────── */
static void draw_filled_rect(float x, float y, float w, float h, int32_t color);
static void draw_border(float x, float y, float w, float h, int32_t color, float bw);
static void draw_widget_rect(float x, float y, float w, float h,
                              int32_t fill, int32_t border_col,
                              float border_w, float radius);

/* ── Grid ─────────────────────────────────────────────────────────────────── */

void aiml_ui_begin_grid(void* cols_buf, void* rows_buf) {
    if (!g_ui) aiml_ui_init();

    UiGridState gs{};
    /* Root rect or current cell rect */
    if (!g_ui->cell_stack.empty()) {
        UiCellState& cell = g_ui->cell_stack.back();
        gs.x = cell.x; gs.y = cell.y; gs.w = cell.w; gs.h = cell.h;
    } else {
        gs.x = 0; gs.y = 0;
        gs.w = (float)g_ui->win_w; gs.h = (float)g_ui->win_h;
    }
    gs.gap_h = lp(g_ui->next_gap_h);
    gs.gap_v = lp(g_ui->next_gap_v);

    compute_sizes((AimlGLRawBuf*)cols_buf, gs.w, gs.gap_h,
                  &gs.num_cols, gs.col_offsets, gs.col_sizes);
    compute_sizes((AimlGLRawBuf*)rows_buf, gs.h, gs.gap_v,
                  &gs.num_rows, gs.row_offsets, gs.row_sizes);

    /* Offset by grid origin */
    for (int i = 0; i < gs.num_cols; i++) gs.col_offsets[i] += gs.x;
    for (int i = 0; i < gs.num_rows; i++) gs.row_offsets[i] += gs.y;

    g_ui->grid_stack.push_back(gs);
}

void aiml_ui_end_grid(void) {
    if (g_ui && !g_ui->grid_stack.empty())
        g_ui->grid_stack.pop_back();
}

void aiml_ui_set_gap(float h, float v) {
    if (!g_ui) aiml_ui_init();
    g_ui->next_gap_h = h;
    g_ui->next_gap_v = v;
}

void aiml_ui_begin_cell(int32_t col, int32_t row, int32_t colspan, int32_t rowspan) {
    if (!g_ui || g_ui->grid_stack.empty()) return;
    UiGridState& gs = g_ui->grid_stack.back();

    int c0 = col, c1 = col + colspan - 1;
    int r0 = row, r1 = row + rowspan - 1;
    if (c1 >= gs.num_cols) c1 = gs.num_cols - 1;
    if (r1 >= gs.num_rows) r1 = gs.num_rows - 1;

    float x = gs.col_offsets[c0];
    float y = gs.row_offsets[r0];
    float w = gs.col_offsets[c1] + gs.col_sizes[c1] - x;
    float h = gs.row_offsets[r1] + gs.row_sizes[r1] - y;

    UiCellState cs{};
    cs.x = x; cs.y = y; cs.w = w; cs.h = h;
    cs.cursor_y   = lp(6.0f);  /* initial top padding within cell */
    cs.prev_item_h= 0;
    cs.same_line  = false;
    strncpy(cs.align_h, g_ui->next_align_h, 15);
    strncpy(cs.align_v, g_ui->next_align_v, 15);

    /* Reset alignment for next cell */
    strcpy(g_ui->next_align_h, "left");
    strcpy(g_ui->next_align_v, "top");

    g_ui->cell_stack.push_back(cs);

    /* Cell background + border as SDF rounded rect */
    draw_widget_rect(cs.x, cs.y, cs.w, cs.h,
                     (int32_t)0x12121EFF, (int32_t)0x33334AFF, lp(1.0f), 0.0f);
}

void aiml_ui_end_cell(void) {
    if (g_ui && !g_ui->cell_stack.empty())
        g_ui->cell_stack.pop_back();
}

void aiml_ui_set_align(const char* h, const char* v) {
    if (!g_ui) aiml_ui_init();
    if (h) strncpy(g_ui->next_align_h, h, 15);
    if (v) strncpy(g_ui->next_align_v, v, 15);
}

/* ── Internal widget rendering helpers ───────────────────────────────────── */

static AimlUIFont* active_font() {
    AimlUIStyle& s = cur_style();
    if (s.font) return (AimlUIFont*)s.font;
    return g_ui->default_font;
}

static uint32_t style_rgba(int32_t c, float opacity) {
    uint32_t r = rgba_to_gl(c);
    /* Apply opacity to alpha byte */
    uint8_t a = (uint8_t)(((r >> 24) & 0xFF) * opacity);
    return (r & 0x00FFFFFF) | ((uint32_t)a << 24);
}

/* Flat solid fill — radius=0, no border, no SDF corner clipping */
static void draw_filled_rect(float x, float y, float w, float h, int32_t color) {
    if (w <= 0 || h <= 0) return;
    uint32_t rgba = style_rgba(color, cur_style().opacity);
    push_quad(x, y, w, h, 0, 0, 1, 1, rgba, g_ui->white_tex, 1);
}

/* Fallback rectangular border (4 quads) — used for non-rounded elements */
static void draw_border(float x, float y, float w, float h,
                        int32_t color, float bw) {
    if (bw <= 0) return;
    draw_filled_rect(x,       y,       w,  bw, color);
    draw_filled_rect(x,       y+h-bw,  w,  bw, color);
    draw_filled_rect(x,       y,       bw, h,  color);
    draw_filled_rect(x+w-bw,  y,       bw, h,  color);
}

/* SDF rounded rect: fill + border in one draw call */
static void draw_widget_rect(float x, float y, float w, float h,
                              int32_t fill, int32_t border_col,
                              float border_w, float radius) {
    if (w <= 0 || h <= 0) return;
    float    op  = cur_style().opacity;
    uint32_t fg  = style_rgba(fill,       op);
    uint32_t bg  = style_rgba(border_col, op);
    push_quad(x, y, w, h, 0, 0, 1, 1, fg, g_ui->white_tex, 1,
              radius, border_w, bg, true);
}

/* Query GL texture dimensions from GL state (AimlGLTexture only stores id) */
static void style_set_tex(GLuint* tex, int* w, int* h, void* gl_tex) {
    if (!gl_tex) { *tex = 0; *w = *h = 0; return; }
    GLuint id = ((AimlGLTexture*)gl_tex)->id;
    *tex = id;
    GLint tw = 1, th = 1;
    glBindTexture(GL_TEXTURE_2D, id);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,  &tw);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
    glBindTexture(GL_TEXTURE_2D, 0);
    *w = (int)tw; *h = (int)th;
}

static void draw_label_in_rect(const char* text, float x, float y, float w, float h,
                                int32_t color) {
    AimlUIFont* f = active_font();
    if (!f) return;
    /* Vertically and horizontally center text within rect */
    float ty = y + (h - f->line_height) * 0.5f;
    float tw = text_width(f, text);
    float tx = x + (w - tw) * 0.5f;
    if (tx < x + lp(4.0f)) tx = x + lp(4.0f);  /* minimum left indent */
    uint32_t rgba = style_rgba(color, cur_style().opacity);
    push_text(f, text, tx, ty, w - lp(4.0f), rgba);
}

/* ── Text widgets ─────────────────────────────────────────────────────────── */

void aiml_ui_label(const char* text, void* preset) {
    if (!text || g_ui->cell_stack.empty()) return;
    AimlUILabelStyle* st = (AimlUILabelStyle*)preset;
    if (st) g_ui->style_stack.push_back(st->base);
    AimlUIFont* f = active_font();
    float fh = f ? f->line_height : lp(cur_style().font_size);
    float fw = f ? text_width(f, text) : 100.0f;
    UiCellState& cell = cur_cell();

    int scissor[4];
    WidgetRect wr = apply_widget_layout(cell.x, cell.y + cell.cursor_y,
                                         cell.w, 0, fh, scissor);
    float ix, iy;
    cell_advance(fw, fh, &ix, &iy);

    uint32_t rgba = style_rgba(cur_style().color, cur_style().opacity);
    float pad = lp(4.0f);

    if (scissor[2] > 0) push_scissor_cmd(scissor);
    push_text(f, text, wr.x + pad, wr.y, wr.w - pad, rgba);
    if (scissor[2] > 0) {
        int clear[4] = {0,0,0,0};
        push_scissor_cmd(clear);
    }

    if (st) g_ui->style_stack.pop_back();
}

void aiml_ui_label_colored(const char* text, int32_t color, void* preset) {
    if (!text || g_ui->cell_stack.empty()) return;
    AimlUILabelStyle* st = (AimlUILabelStyle*)preset;
    if (st) g_ui->style_stack.push_back(st->base);
    AimlUIFont* f = active_font();
    float fh = f ? f->line_height : lp(cur_style().font_size);
    float fw = f ? text_width(f, text) : 100.0f;
    UiCellState& cell = cur_cell();

    int scissor[4];
    WidgetRect wr = apply_widget_layout(cell.x, cell.y + cell.cursor_y,
                                         cell.w, 0, fh, scissor);
    float ix, iy;
    cell_advance(fw, fh, &ix, &iy);

    uint32_t rgba = style_rgba(color, cur_style().opacity);
    float pad = lp(4.0f);

    if (scissor[2] > 0) push_scissor_cmd(scissor);
    push_text(f, text, wr.x + pad, wr.y, wr.w - pad, rgba);
    if (scissor[2] > 0) {
        int clear[4] = {0,0,0,0};
        push_scissor_cmd(clear);
    }

    if (st) g_ui->style_stack.pop_back();
}

const char* aiml_ui_text_input(const char* id, const char* buf, void* preset) {
    if (!id || g_ui->cell_stack.empty()) return buf ? buf : "";
    AimlUILabelStyle* st = (AimlUILabelStyle*)preset;
    if (st) g_ui->style_stack.push_back(st->base);
    uint32_t wid = fnv1a(id);
    /* Init buffer if not tracked */
    auto it = g_ui->text_bufs.find(wid);
    if (it == g_ui->text_bufs.end()) {
        g_ui->text_bufs[wid] = buf ? buf : "";
        it = g_ui->text_bufs.find(wid);
    }
    UiCellState& cell = cur_cell();
    float h = lp(cur_style().font_size + cur_style().padding_v * 2);
    float x = cell.x, y = cell.y + cell.cursor_y, w = cell.w;
    cell_advance(w, h, nullptr, nullptr);

    bool hot = widget_hot(wid, x, y, w, h);
    if (hot) g_ui->hot = wid;
    if (hot && g_ui->mouse_pressed) g_ui->focused = wid;

    int32_t bg = (g_ui->focused == wid) ? cur_style().active_color : cur_style().bg_color;
    draw_filled_rect(x, y, w, h, bg);
    draw_border(x, y, w, h, cur_style().border_color, lp(cur_style().border_width));
    draw_label_in_rect(it->second.c_str(), x, y, w, h, cur_style().color);

    if (st) g_ui->style_stack.pop_back();
    return it->second.c_str();
}

const char* aiml_ui_text_area(const char* id, const char* buf, void* preset) {
    /* Simple single-line implementation for now */
    return aiml_ui_text_input(id, buf, preset);
}

/* ── Interactive widgets ──────────────────────────────────────────────────── */

int32_t aiml_ui_button(const char* label, void* preset) {
    if (!label || g_ui->cell_stack.empty()) return 0;
    AimlUIButtonStyle* st = (AimlUIButtonStyle*)preset;
    if (st) g_ui->style_stack.push_back(st->base);
    uint32_t wid = fnv1a(label);
    AimlUIFont* f = active_font();
    float fh = f ? f->line_height : lp(cur_style().font_size);
    float ph = lp(cur_style().padding_v);
    float auto_h = fh + ph * 2;
    UiCellState& cell = cur_cell();

    int scissor[4];
    WidgetRect wr = apply_widget_layout(cell.x, cell.y + cell.cursor_y,
                                         cell.w, 0, auto_h, scissor);
    float bw = wr.w;
    float bh = wr.h;
    float ix = wr.x, iy = wr.y;

    /* cursor advances by full cell width so layout is predictable */
    cell_advance(cell.w, bh, nullptr, nullptr);

    bool hot = widget_hot(wid, ix, iy, bw, bh);
    if (hot) g_ui->hot = wid;
    if (hot && g_ui->mouse_pressed) g_ui->active_w = wid;

    bool is_active = (g_ui->active_w == wid && hot);
    bool is_hover  = hot && !is_active;

    if (scissor[2] > 0) push_scissor_cmd(scissor);

    if (st) {
        GLuint use_tex = 0;
        if (is_active && st->active_tex) use_tex = st->active_tex;
        else if (is_hover && st->hover_tex) use_tex = st->hover_tex;
        else if (st->bg_tex) use_tex = st->bg_tex;
        if (use_tex) {
            push_quad(ix, iy, bw, bh, 0, 0, 1, 1, 0xFFFFFFFF, use_tex, 1, 0.0f, 0.0f, 0, false);
        } else {
            int32_t bg;
            if (is_active)    bg = cur_style().active_color;
            else if (is_hover) bg = cur_style().hover_color;
            else               bg = cur_style().bg_color;
            draw_widget_rect(ix, iy, bw, bh, bg, cur_style().border_color,
                             lp(cur_style().border_width), lp(cur_style().corner_radius));
        }
    } else {
        int32_t bg;
        if (is_active)    bg = cur_style().active_color;
        else if (is_hover) bg = cur_style().hover_color;
        else               bg = cur_style().bg_color;
        draw_widget_rect(ix, iy, bw, bh, bg, cur_style().border_color,
                         lp(cur_style().border_width), lp(cur_style().corner_radius));
    }
    draw_label_in_rect(label, ix, iy, bw, bh, cur_style().color);

    if (scissor[2] > 0) {
        int clear[4] = {0,0,0,0};
        push_scissor_cmd(clear);
    }

    if (st) g_ui->style_stack.pop_back();
    return (hot && g_ui->mouse_released) ? 1 : 0;
}

int32_t aiml_ui_button_icon(const char* label, void* tex) {
    /* For now, just render as regular button */
    return aiml_ui_button(label, nullptr);
}

int32_t aiml_ui_checkbox(const char* id, int32_t value, void* preset) {
    if (!id || g_ui->cell_stack.empty()) return value;
    AimlUIButtonStyle* st = (AimlUIButtonStyle*)preset;
    if (st) g_ui->style_stack.push_back(st->base);
    uint32_t wid = fnv1a(id);
    float sz = lp(cur_style().font_size);
    float ix, iy;
    cell_advance(sz * 3, sz, &ix, &iy);

    bool hot = widget_hot(wid, ix, iy, sz * 3, sz);
    if (hot) g_ui->hot = wid;
    int32_t new_val = value;
    if (hot && g_ui->mouse_released) new_val = value ? 0 : 1;

    if (st) {
        GLuint use_tex = (new_val && st->active_tex) ? st->active_tex :
                         (!new_val && st->bg_tex) ? st->bg_tex : 0;
        if (use_tex) {
            push_quad(ix, iy, sz, sz, 0, 0, 1, 1, 0xFFFFFFFF, use_tex, 1, 0.0f, 0.0f, 0, false);
        } else {
            int32_t bg = hot ? cur_style().hover_color : cur_style().bg_color;
            draw_filled_rect(ix, iy, sz, sz, bg);
            draw_border(ix, iy, sz, sz, cur_style().border_color, lp(1.0f));
            if (new_val) {
                float m = sz * 0.2f;
                draw_filled_rect(ix+m, iy+m, sz-m*2, sz-m*2, cur_style().color);
            }
        }
    } else {
        int32_t bg = hot ? cur_style().hover_color : cur_style().bg_color;
        draw_filled_rect(ix, iy, sz, sz, bg);
        draw_border(ix, iy, sz, sz, cur_style().border_color, lp(1.0f));
        if (new_val) {
            float m = sz * 0.2f;
            draw_filled_rect(ix+m, iy+m, sz-m*2, sz-m*2, cur_style().color);
        }
    }

    AimlUIFont* f = active_font();
    uint32_t rgba = style_rgba(cur_style().color, cur_style().opacity);
    push_text(f, id, ix + sz + lp(4.0f), iy, cur_cell().w, rgba);
    if (st) g_ui->style_stack.pop_back();
    return new_val;
}

int32_t aiml_ui_toggle(const char* id, int32_t value, void* preset) {
    return aiml_ui_checkbox(id, value, preset);
}

int32_t aiml_ui_radio(const char* id, int32_t option, int32_t current, void* preset) {
    if (!id || g_ui->cell_stack.empty()) return current;
    AimlUIButtonStyle* st = (AimlUIButtonStyle*)preset;
    if (st) g_ui->style_stack.push_back(st->base);
    char wid_str[64]; snprintf(wid_str, 64, "%s_%d", id, option);
    uint32_t wid = fnv1a(wid_str);
    float sz = lp(cur_style().font_size);
    float ix, iy;
    cell_advance(sz * 4, sz, &ix, &iy);

    bool hot = widget_hot(wid, ix, iy, sz * 4, sz);
    if (hot) g_ui->hot = wid;
    if (hot && g_ui->mouse_released) current = option;

    int32_t bg = hot ? cur_style().hover_color : cur_style().bg_color;
    draw_filled_rect(ix, iy, sz, sz, bg);
    draw_border(ix, iy, sz, sz, cur_style().border_color, lp(1.0f));
    if (current == option) {
        float m = sz * 0.25f;
        draw_filled_rect(ix+m, iy+m, sz-m*2, sz-m*2, cur_style().color);
    }
    AimlUIFont* f = active_font();
    uint32_t rgba = style_rgba(cur_style().color, cur_style().opacity);
    push_text(f, id, ix + sz + lp(4.0f), iy, cur_cell().w, rgba);
    if (st) g_ui->style_stack.pop_back();
    return current;
}

/* ── Value widgets ────────────────────────────────────────────────────────── */

float aiml_ui_slider(const char* id, float value, float mn, float mx, void* preset) {
    if (!id || g_ui->cell_stack.empty()) return value;
    AimlUISliderStyle* st = (AimlUISliderStyle*)preset;
    if (st) g_ui->style_stack.push_back(st->base);
    uint32_t wid = fnv1a(id);
    UiCellState& cell = cur_cell();
    float auto_h = lp(cur_style().font_size + cur_style().padding_v * 2);

    int scissor[4];
    WidgetRect wr = apply_widget_layout(cell.x, cell.y + cell.cursor_y,
                                         cell.w, auto_h, auto_h, scissor);
    float x = wr.x, y = wr.y, w = wr.w, h = wr.h;
    cell_advance(cell.w, h, nullptr, nullptr);

    if (scissor[2] > 0) push_scissor_cmd(scissor);

    bool hot = widget_hot(wid, x, y, w, h);
    if (hot) g_ui->hot = wid;
    if (hot && g_ui->mouse_pressed)  g_ui->active_w = wid;

    if (g_ui->active_w == wid && g_ui->mouse_down) {
        float t = (g_ui->mouse_x - x) / w;
        value = mn + t * (mx - mn);
        if (value < mn) value = mn;
        if (value > mx) value = mx;
    }

    /* Track — textured or pill-shaped SDF */
    GLuint track_tex = st ? st->track_tex : 0;
    int    track_tw  = st ? st->track_w   : 0;
    int    track_th  = st ? st->track_h   : 0;
    float track_h, track_y;
    if (track_tex && track_th > 0) {
        float aspect = (float)track_tw / track_th;
        track_h = h * 0.55f;
        float tw = track_h * aspect;
        (void)tw;
        track_y = y + (h - track_h) * 0.5f;
        push_quad(x, track_y, w, track_h, 0, 0, 1, 1,
                  0xFFFFFFFF, track_tex, 1,
                  0.0f, 0.0f, 0, false);
    } else {
        track_h = h * 0.2f;
        float track_r = track_h * 0.5f;
        track_y = y + h * 0.4f;
        draw_widget_rect(x, track_y, w, track_h,
                         cur_style().border_color, (int32_t)0x00000000, 0.0f, track_r);
    }
    /* Thumb — textured sprite or rounded rect */
    GLuint thumb_tex = st ? st->thumb_tex : 0;
    int    thumb_tw  = st ? st->thumb_w   : 0;
    int    thumb_th  = st ? st->thumb_h   : 0;
    float fval = (mx > mn) ? (value - mn) / (mx - mn) : 0.0f;
    float thumb_h = h * 0.85f;
    float thumb_w = (thumb_tex && thumb_th > 0)
                    ? thumb_h * ((float)thumb_tw / thumb_th)
                    : h * 0.7f;
    float tx = x + fval * (w - thumb_w);
    float ty = y + (h - thumb_h) * 0.5f;
    if (thumb_tex) {
        uint32_t tint = (hot || g_ui->active_w == wid) ? 0xFFFFFFFF : 0xCCCCCCFF;
        push_quad(tx, ty, thumb_w, thumb_h, 0, 0, 1, 1,
                  tint, thumb_tex, 1,
                  0.0f, 0.0f, 0, false);
    } else {
        int32_t thumb_col = (hot || g_ui->active_w == wid)
                            ? cur_style().hover_color : cur_style().active_color;
        draw_widget_rect(tx, ty, thumb_w, thumb_h,
                         thumb_col, cur_style().border_color,
                         lp(cur_style().border_width), lp(cur_style().corner_radius));
    }
    if (scissor[2] > 0) {
        int clear[4] = {0,0,0,0};
        push_scissor_cmd(clear);
    }
    if (st) g_ui->style_stack.pop_back();
    return value;
}

int32_t aiml_ui_slider_int(const char* id, int32_t value, int32_t mn, int32_t mx, void* preset) {
    return (int32_t)aiml_ui_slider(id, (float)value, (float)mn, (float)mx, preset);
}

float aiml_ui_drag_f32(const char* id, float value, float speed, void* preset) {
    if (!id || g_ui->cell_stack.empty()) return value;
    AimlUISliderStyle* st = (AimlUISliderStyle*)preset;
    if (st) g_ui->style_stack.push_back(st->base);
    uint32_t wid = fnv1a(id);
    UiCellState& cell = cur_cell();
    float auto_h = lp(cur_style().font_size + cur_style().padding_v * 2);

    int scissor[4];
    WidgetRect wr = apply_widget_layout(cell.x, cell.y + cell.cursor_y,
                                         cell.w, auto_h, auto_h, scissor);
    float x = wr.x, y = wr.y, w = wr.w, h = wr.h;
    cell_advance(cell.w, h, nullptr, nullptr);

    bool hot = widget_hot(wid, x, y, w, h);
    if (hot) g_ui->hot = wid;
    if (hot && g_ui->mouse_pressed) g_ui->active_w = wid;
    if (g_ui->active_w == wid && g_ui->mouse_down) {
        float dx = aiml_input_mouse_delta_x();
        value += dx * speed;
    }

    if (scissor[2] > 0) push_scissor_cmd(scissor);
    int32_t bg = (g_ui->active_w == wid) ? cur_style().active_color : cur_style().bg_color;
    draw_filled_rect(x, y, w, h, bg);
    char buf[64]; snprintf(buf, 64, "%.3f", value);
    draw_label_in_rect(buf, x, y, w, h, cur_style().color);
    if (scissor[2] > 0) {
        int clear[4] = {0,0,0,0};
        push_scissor_cmd(clear);
    }
    if (st) g_ui->style_stack.pop_back();
    return value;
}

/* ── Visualization ────────────────────────────────────────────────────────── */

void aiml_ui_progress_bar(float value, void* preset) {
    aiml_ui_progress_bar_colored(value, (int32_t)0x44BB44FF, (int32_t)0x223322FF, preset);
}

void aiml_ui_progress_bar_colored(float value, int32_t fg, int32_t bg, void* preset) {
    if (g_ui->cell_stack.empty()) return;
    AimlUIProgressStyle* st = (AimlUIProgressStyle*)preset;
    if (st) g_ui->style_stack.push_back(st->base);
    UiCellState& cell = cur_cell();
    float auto_h = lp(cur_style().font_size);

    int scissor[4];
    WidgetRect wr = apply_widget_layout(cell.x, cell.y + cell.cursor_y,
                                         cell.w, 0, auto_h, scissor);
    float pad = lp(4.0f);
    float x = wr.x + pad, y = wr.y, w = wr.w - pad * 2.0f, h = wr.h;
    cell_advance(cell.w, h, nullptr, nullptr);

    if (value < 0) value = 0; if (value > 1) value = 1;
    float bar_r = h * 0.5f;  /* pill-shaped background */

    if (scissor[2] > 0) push_scissor_cmd(scissor);

    /* Background */
    if (st && st->bg_tex) {
        push_quad(x, y, w, h, 0, 0, 1, 1, 0xFFFFFFFF, st->bg_tex, 1, 0.0f, 0.0f, 0, false);
    } else {
        draw_widget_rect(x, y, w, h, bg, cur_style().border_color,
                         lp(cur_style().border_width), bar_r);
    }
    /* Fill */
    if (value > 0.0f) {
        if (st && st->fill_tex) {
            push_quad(x, y, w * value, h, 0, 0, value, 1, 0xFFFFFFFF, st->fill_tex, 1, 0.0f, 0.0f, 0, false);
        } else {
            draw_filled_rect(x, y, w * value, h, fg);
        }
    }

    if (scissor[2] > 0) {
        int clear[4] = {0,0,0,0};
        push_scissor_cmd(clear);
    }
    if (st) g_ui->style_stack.pop_back();
}

void aiml_ui_image(void* tex) {
    if (!tex || g_ui->cell_stack.empty()) return;
    AimlGLTexture* t = (AimlGLTexture*)tex;
    UiCellState& cell = cur_cell();
    /* Aspect-ratio preserve */
    float cw = cell.w, ch = cell.h - cell.cursor_y;
    float aspect = (t->height > 0) ? (float)t->width / t->height : 1.0f;
    float iw, ih;
    if (cw / ch > aspect) { ih = ch; iw = ih * aspect; }
    else                  { iw = cw; ih = iw / aspect; }
    float ix = cell.x + (cw - iw) * 0.5f;
    float iy = cell.y + cell.cursor_y;
    cell_advance(iw, ih, nullptr, nullptr);
    push_quad(ix, iy, iw, ih, 0, 0, 1, 1, 0xFFFFFFFF, t->id, 1);
}

void aiml_ui_image_sized(void* tex, float w, float h) {
    if (!tex || g_ui->cell_stack.empty()) return;
    AimlGLTexture* t = (AimlGLTexture*)tex;
    float pw = lp(w), ph = lp(h);
    float ix, iy;
    cell_advance(pw, ph, &ix, &iy);
    push_quad(ix, iy, pw, ph, 0, 0, 1, 1, 0xFFFFFFFF, t->id, 1);
}

void aiml_ui_rect(int32_t color) {
    if (g_ui->cell_stack.empty()) return;
    UiCellState& cell = cur_cell();
    float h = cell.h - cell.cursor_y;
    draw_filled_rect(cell.x, cell.y + cell.cursor_y, cell.w, h, color);
    cell.cursor_y += h;
}

void aiml_ui_rect_rounded(int32_t color, float radius, void* preset) {
    if (g_ui->cell_stack.empty()) return;
    AimlUIPanelStyle* st = (AimlUIPanelStyle*)preset;
    if (st) g_ui->style_stack.push_back(st->base);
    UiCellState& cell = cur_cell();
    float cell_h_avail = cell.h - cell.cursor_y;

    int scissor[4];
    WidgetRect wr = apply_widget_layout(cell.x, cell.y + cell.cursor_y,
                                         cell.w, cell_h_avail, cell_h_avail, scissor);
    float r = lp(radius);

    if (scissor[2] > 0) push_scissor_cmd(scissor);

    if (st && st->bg_tex) {
        push_quad(wr.x, wr.y, wr.w, wr.h,
                  0, 0, 1, 1, 0xFFFFFFFF, st->bg_tex, 1, 0.0f, 0.0f, 0, false);
    } else {
        draw_widget_rect(wr.x, wr.y, wr.w, wr.h,
                         color, cur_style().border_color,
                         lp(cur_style().border_width), r);
    }

    if (scissor[2] > 0) {
        int clear[4] = {0,0,0,0};
        push_scissor_cmd(clear);
    }

    cell.cursor_y += cell_h_avail;
    if (st) g_ui->style_stack.pop_back();
}

void aiml_ui_separator(void) {
    if (g_ui->cell_stack.empty()) return;
    UiCellState& cell = cur_cell();
    float h = lp(1.0f);
    /* Use border_color if opaque enough, else fall back to a visible default */
    int32_t col = cur_style().border_color;
    if ((col & 0xFF) < 32) col = (int32_t)0x6666AAFF;
    draw_filled_rect(cell.x, cell.y + cell.cursor_y, cell.w, h, col);
    cell.cursor_y += h + lp(4.0f);
}

void aiml_ui_spacer(float size) {
    if (g_ui->cell_stack.empty()) return;
    cur_cell().cursor_y += lp(size);
}

void aiml_ui_same_line(void) {
    if (g_ui->cell_stack.empty()) return;
    UiCellState& cell = cur_cell();
    /* Save current x-cursor for next widget */
    cell.same_line  = true;
    /* Reuse cursor_y as x-offset for same-line placement */
    cell.cursor_y  -= (cell.prev_item_h + lp(2.0f)); /* undo last advance */
    /* Use cursor_y as right-edge x of previous item (approximate) */
}

void aiml_ui_newline(void) {
    if (!g_ui->cell_stack.empty())
        cur_cell().cursor_y += cur_style().font_size * cur_dpi();
}

/* ── Style preset factories ───────────────────────────────────────────────── */

static AimlUIStyle capture_style() {
    return g_ui ? cur_style() : AimlUIStyle{};
}

void* aiml_ui_slider_style_new()   { auto* s = new AimlUISliderStyle{};   s->base = capture_style(); return s; }
void* aiml_ui_button_style_new()   { auto* s = new AimlUIButtonStyle{};   s->base = capture_style(); return s; }
void* aiml_ui_label_style_new()    { auto* s = new AimlUILabelStyle{};    s->base = capture_style(); return s; }
void* aiml_ui_progress_style_new() { auto* s = new AimlUIProgressStyle{}; s->base = capture_style(); return s; }
void* aiml_ui_panel_style_new()    { auto* s = new AimlUIPanelStyle{};    s->base = capture_style(); return s; }

/* ── Slider style setters ─────────────────────────────────────────────────── */
void aiml_ui_slider_style_set_thumb(void* s, void* t)          { style_set_tex(&((AimlUISliderStyle*)s)->thumb_tex, &((AimlUISliderStyle*)s)->thumb_w, &((AimlUISliderStyle*)s)->thumb_h, t); }
void aiml_ui_slider_style_set_track(void* s, void* t)          { style_set_tex(&((AimlUISliderStyle*)s)->track_tex, &((AimlUISliderStyle*)s)->track_w, &((AimlUISliderStyle*)s)->track_h, t); }
void aiml_ui_slider_style_set_color(void* s, int32_t c)        { ((AimlUISliderStyle*)s)->base.color = c; }
void aiml_ui_slider_style_set_bg_color(void* s, int32_t c)     { ((AimlUISliderStyle*)s)->base.bg_color = c; }
void aiml_ui_slider_style_set_hover_color(void* s, int32_t c)  { ((AimlUISliderStyle*)s)->base.hover_color = c; }
void aiml_ui_slider_style_set_active_color(void* s, int32_t c) { ((AimlUISliderStyle*)s)->base.active_color = c; }
void aiml_ui_slider_style_set_border_color(void* s, int32_t c) { ((AimlUISliderStyle*)s)->base.border_color = c; }
void aiml_ui_slider_style_set_corner_radius(void* s, float r)  { ((AimlUISliderStyle*)s)->base.corner_radius = r; }
void aiml_ui_slider_style_set_border_width(void* s, float w)   { ((AimlUISliderStyle*)s)->base.border_width = w; }
void aiml_ui_slider_style_set_font(void* s, void* f)           { ((AimlUISliderStyle*)s)->base.font = f; }
void aiml_ui_slider_style_set_padding_x(void* s, float v)      { ((AimlUISliderStyle*)s)->base.padding_x = v; }
void aiml_ui_slider_style_set_padding_y(void* s, float v)      { ((AimlUISliderStyle*)s)->base.padding_y = v; }
void aiml_ui_slider_style_set_size_w(void* s, float v)         { ((AimlUISliderStyle*)s)->base.size_w = v; }
void aiml_ui_slider_style_set_size_h(void* s, float v)         { ((AimlUISliderStyle*)s)->base.size_h = v; }

/* ── Button style setters ─────────────────────────────────────────────────── */
void aiml_ui_button_style_set_bg(void* s, void* t)             { style_set_tex(&((AimlUIButtonStyle*)s)->bg_tex, &((AimlUIButtonStyle*)s)->bg_w, &((AimlUIButtonStyle*)s)->bg_h, t); }
void aiml_ui_button_style_set_hover(void* s, void* t)          { style_set_tex(&((AimlUIButtonStyle*)s)->hover_tex, &((AimlUIButtonStyle*)s)->hover_w, &((AimlUIButtonStyle*)s)->hover_h, t); }
void aiml_ui_button_style_set_active(void* s, void* t)         { style_set_tex(&((AimlUIButtonStyle*)s)->active_tex, &((AimlUIButtonStyle*)s)->active_w, &((AimlUIButtonStyle*)s)->active_h, t); }
void aiml_ui_button_style_set_color(void* s, int32_t c)        { ((AimlUIButtonStyle*)s)->base.color = c; }
void aiml_ui_button_style_set_bg_color(void* s, int32_t c)     { ((AimlUIButtonStyle*)s)->base.bg_color = c; }
void aiml_ui_button_style_set_hover_color(void* s, int32_t c)  { ((AimlUIButtonStyle*)s)->base.hover_color = c; }
void aiml_ui_button_style_set_active_color(void* s, int32_t c) { ((AimlUIButtonStyle*)s)->base.active_color = c; }
void aiml_ui_button_style_set_border_color(void* s, int32_t c) { ((AimlUIButtonStyle*)s)->base.border_color = c; }
void aiml_ui_button_style_set_corner_radius(void* s, float r)  { ((AimlUIButtonStyle*)s)->base.corner_radius = r; }
void aiml_ui_button_style_set_border_width(void* s, float w)   { ((AimlUIButtonStyle*)s)->base.border_width = w; }
void aiml_ui_button_style_set_font(void* s, void* f)           { ((AimlUIButtonStyle*)s)->base.font = f; }
void aiml_ui_button_style_set_padding_x(void* s, float v)      { ((AimlUIButtonStyle*)s)->base.padding_x = v; }
void aiml_ui_button_style_set_padding_y(void* s, float v)      { ((AimlUIButtonStyle*)s)->base.padding_y = v; }
void aiml_ui_button_style_set_size_w(void* s, float v)         { ((AimlUIButtonStyle*)s)->base.size_w = v; }
void aiml_ui_button_style_set_size_h(void* s, float v)         { ((AimlUIButtonStyle*)s)->base.size_h = v; }

/* ── Label style setters ──────────────────────────────────────────────────── */
void aiml_ui_label_style_set_color(void* s, int32_t c)        { ((AimlUILabelStyle*)s)->base.color = c; }
void aiml_ui_label_style_set_bg_color(void* s, int32_t c)     { ((AimlUILabelStyle*)s)->base.bg_color = c; }
void aiml_ui_label_style_set_hover_color(void* s, int32_t c)  { ((AimlUILabelStyle*)s)->base.hover_color = c; }
void aiml_ui_label_style_set_active_color(void* s, int32_t c) { ((AimlUILabelStyle*)s)->base.active_color = c; }
void aiml_ui_label_style_set_border_color(void* s, int32_t c) { ((AimlUILabelStyle*)s)->base.border_color = c; }
void aiml_ui_label_style_set_corner_radius(void* s, float r)  { ((AimlUILabelStyle*)s)->base.corner_radius = r; }
void aiml_ui_label_style_set_border_width(void* s, float w)   { ((AimlUILabelStyle*)s)->base.border_width = w; }
void aiml_ui_label_style_set_font(void* s, void* f)           { ((AimlUILabelStyle*)s)->base.font = f; }
void aiml_ui_label_style_set_padding_x(void* s, float v)      { ((AimlUILabelStyle*)s)->base.padding_x = v; }
void aiml_ui_label_style_set_padding_y(void* s, float v)      { ((AimlUILabelStyle*)s)->base.padding_y = v; }
void aiml_ui_label_style_set_size_w(void* s, float v)         { ((AimlUILabelStyle*)s)->base.size_w = v; }
void aiml_ui_label_style_set_size_h(void* s, float v)         { ((AimlUILabelStyle*)s)->base.size_h = v; }

/* ── Progress style setters ───────────────────────────────────────────────── */
void aiml_ui_progress_style_set_fill(void* s, void* t)          { style_set_tex(&((AimlUIProgressStyle*)s)->fill_tex, &((AimlUIProgressStyle*)s)->fill_w, &((AimlUIProgressStyle*)s)->fill_h, t); }
void aiml_ui_progress_style_set_bg_tex(void* s, void* t)        { style_set_tex(&((AimlUIProgressStyle*)s)->bg_tex, &((AimlUIProgressStyle*)s)->bg_w, &((AimlUIProgressStyle*)s)->bg_h, t); }
void aiml_ui_progress_style_set_color(void* s, int32_t c)       { ((AimlUIProgressStyle*)s)->base.color = c; }
void aiml_ui_progress_style_set_bg_color(void* s, int32_t c)    { ((AimlUIProgressStyle*)s)->base.bg_color = c; }
void aiml_ui_progress_style_set_hover_color(void* s, int32_t c) { ((AimlUIProgressStyle*)s)->base.hover_color = c; }
void aiml_ui_progress_style_set_active_color(void* s, int32_t c){ ((AimlUIProgressStyle*)s)->base.active_color = c; }
void aiml_ui_progress_style_set_border_color(void* s, int32_t c){ ((AimlUIProgressStyle*)s)->base.border_color = c; }
void aiml_ui_progress_style_set_corner_radius(void* s, float r) { ((AimlUIProgressStyle*)s)->base.corner_radius = r; }
void aiml_ui_progress_style_set_border_width(void* s, float w)  { ((AimlUIProgressStyle*)s)->base.border_width = w; }
void aiml_ui_progress_style_set_font(void* s, void* f)          { ((AimlUIProgressStyle*)s)->base.font = f; }
void aiml_ui_progress_style_set_padding_x(void* s, float v)     { ((AimlUIProgressStyle*)s)->base.padding_x = v; }
void aiml_ui_progress_style_set_padding_y(void* s, float v)     { ((AimlUIProgressStyle*)s)->base.padding_y = v; }
void aiml_ui_progress_style_set_size_w(void* s, float v)        { ((AimlUIProgressStyle*)s)->base.size_w = v; }
void aiml_ui_progress_style_set_size_h(void* s, float v)        { ((AimlUIProgressStyle*)s)->base.size_h = v; }

/* ── Panel style setters ──────────────────────────────────────────────────── */
void aiml_ui_panel_style_set_bg_tex(void* s, void* t)          { style_set_tex(&((AimlUIPanelStyle*)s)->bg_tex, &((AimlUIPanelStyle*)s)->bg_w, &((AimlUIPanelStyle*)s)->bg_h, t); }
void aiml_ui_panel_style_set_color(void* s, int32_t c)         { ((AimlUIPanelStyle*)s)->base.color = c; }
void aiml_ui_panel_style_set_bg_color(void* s, int32_t c)      { ((AimlUIPanelStyle*)s)->base.bg_color = c; }
void aiml_ui_panel_style_set_hover_color(void* s, int32_t c)   { ((AimlUIPanelStyle*)s)->base.hover_color = c; }
void aiml_ui_panel_style_set_active_color(void* s, int32_t c)  { ((AimlUIPanelStyle*)s)->base.active_color = c; }
void aiml_ui_panel_style_set_border_color(void* s, int32_t c)  { ((AimlUIPanelStyle*)s)->base.border_color = c; }
void aiml_ui_panel_style_set_corner_radius(void* s, float r)   { ((AimlUIPanelStyle*)s)->base.corner_radius = r; }
void aiml_ui_panel_style_set_border_width(void* s, float w)    { ((AimlUIPanelStyle*)s)->base.border_width = w; }
void aiml_ui_panel_style_set_font(void* s, void* f)            { ((AimlUIPanelStyle*)s)->base.font = f; }
void aiml_ui_panel_style_set_padding_x(void* s, float v)       { ((AimlUIPanelStyle*)s)->base.padding_x = v; }
void aiml_ui_panel_style_set_padding_y(void* s, float v)       { ((AimlUIPanelStyle*)s)->base.padding_y = v; }
void aiml_ui_panel_style_set_size_w(void* s, float v)          { ((AimlUIPanelStyle*)s)->base.size_w = v; }
void aiml_ui_panel_style_set_size_h(void* s, float v)          { ((AimlUIPanelStyle*)s)->base.size_h = v; }

/* ── Style ────────────────────────────────────────────────────────────────── */

void aiml_ui_push_style(void* style_ptr) {
    if (!style_ptr) return;
    if (!g_ui) aiml_ui_init();
    AimlUIStyle* s = (AimlUIStyle*)style_ptr;
    g_ui->style_stack.push_back(*s);
}

void aiml_ui_pop_style(void) {
    if (!g_ui) return;
    if (g_ui->style_stack.size() > 1) g_ui->style_stack.pop_back();
}

void aiml_ui_set_default_style(void* style_ptr) {
    if (!style_ptr || !g_ui) return;
    g_ui->style_stack[0] = *(AimlUIStyle*)style_ptr;
}

void aiml_ui_copy_current_style(void* out) {
    if (!out) return;
    if (!g_ui) aiml_ui_init();
    *(AimlUIStyle*)out = g_ui->style_stack.back();
}

/* ── Font ─────────────────────────────────────────────────────────────────── */

void* aiml_ui_load_font(void* path_ptr, float size) {
    if (!path_ptr) return nullptr;
    const char* path = (const char*)path_ptr;
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[ui] load_font: cannot open %s\n", path); return nullptr; }
    fseek(f, 0, SEEK_END); int sz = (int)ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* data = (unsigned char*)malloc(sz);
    if (!data) { fclose(f); return nullptr; }
    fread(data, 1, sz, f); fclose(f);
    AimlUIFont* font = bake_font(data, sz, size * cur_dpi());
    free(data);
    return font;
}

void* aiml_ui_load_font_default(float size) {
    /* Try common system TTF paths */
    const char* candidates[] = {
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/System/Library/Fonts/Monaco.ttf",
        "C:/Windows/Fonts/consola.ttf",
        nullptr
    };
    for (int i = 0; candidates[i]; i++) {
        FILE* f = fopen(candidates[i], "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END); int sz = (int)ftell(f); fseek(f, 0, SEEK_SET);
        unsigned char* data = (unsigned char*)malloc(sz);
        fread(data, 1, sz, f); fclose(f);
        AimlUIFont* font = bake_font(data, sz, size);
        free(data);
        if (font) return font;
    }
    fprintf(stderr, "[ui] load_font_default: no system font found\n");
    return nullptr;
}

void aiml_ui_set_default_font(void* font) {
    if (!g_ui) aiml_ui_init();
    g_ui->default_font = (AimlUIFont*)font;
    /* Also update style stack default */
    if (!g_ui->style_stack.empty()) g_ui->style_stack[0].font = font;
}

} /* extern "C" */
