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
#include <set>
#include <functional>
#include <unordered_map>
#include <algorithm>
#include <stdexcept>

#include <stb/stb_truetype.h>  /* header-only, no IMPLEMENTATION (ui_runtime owns it) */

#include "prefab_runtime.h"
#include "ui_runtime.h"
#include "gl_runtime.h"
#include "asset_runtime.h"
#include "log_runtime.h"   // F1-Debug: Drag-Pick-Diagnose

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
extern "C" int32_t aiml_input_mouse_cancelled(int32_t btn);
extern "C" int32_t aiml_input_drag_started  (int32_t btn);
extern "C" int32_t aiml_input_drag_ended    (int32_t btn);
extern "C" int32_t aiml_input_drag_cancelled(int32_t btn);
extern "C" float   aiml_input_drag_origin_x (int32_t btn);
extern "C" float   aiml_input_drag_start_x  (int32_t btn);
extern "C" float   aiml_input_drag_start_y  (int32_t btn);
extern "C" float   aiml_input_drag_origin_y (int32_t btn);
extern "C" int32_t aiml_input_mouse_button  (int32_t btn);
extern "C" int32_t aiml_input_mouse_button(int32_t btn);
extern "C" int32_t aiml_input_has_event(void);
extern "C" void*   aiml_input_next_event(void);
extern "C" const char* aiml_input_event_type(void* ev);
extern "C" const char* aiml_input_event_key(void* ev);
extern "C" const char* aiml_input_event_char_(void* ev);
extern "C" float       aiml_input_mouse_pos_x(void);
extern "C" float       aiml_input_mouse_pos_y(void);
extern "C" float       aiml_input_mouse_scroll_y(void);
extern "C" int32_t     aiml_input_drag_active(int32_t btn);
extern "C" float       aiml_input_drag_origin_x(int32_t btn);
extern "C" float       aiml_input_drag_origin_y(int32_t btn);
extern "C" float       aiml_input_drag_delta_y(int32_t btn);
extern "C" int32_t     aiml_input_key_pressed(const char* k);
extern "C" int32_t     aiml_input_key_down(const char* k);
extern "C" int32_t     aiml_input_mouse_button(int32_t b);
extern "C" const char* aiml_input_clipboard_get(void);
extern "C" void        aiml_input_clipboard_set(const char* s);

/* ── UTF-8 caret navigation (text_input) — move by whole codepoint so umlauts
 *    (ä/ö/ü) and other multi-byte chars are never split. ───────────────── */
static int32_t utf8_prev(const std::string& t, int32_t pos) {
    if (pos <= 0) return 0;
    --pos;
    while (pos > 0 && ((unsigned char)t[pos] & 0xC0) == 0x80) --pos;  /* skip continuation bytes */
    return pos;
}
static int32_t utf8_next(const std::string& t, int32_t pos) {
    int32_t n = (int32_t)t.size();
    if (pos >= n) return n;
    ++pos;
    while (pos < n && ((unsigned char)t[pos] & 0xC0) == 0x80) ++pos;
    return pos;
}

static void render_dropdown_popups(void);

/* ── External struct layouts (from gl_runtime / ui_runtime) ──────────────── */

struct AimlGLTexture { GLuint id; };

/* Must match ui_runtime.cpp AimlUIFont exactly */
struct AimlUIFont {
    stbtt_fontinfo info;
    unsigned char* ttf_data;   /* malloc'd copy */
    int     ttf_size;          /* byte length of ttf_data */
    GLuint  atlas_tex;
    stbtt_bakedchar cdata[224]; /* codepoints 32..255 (ASCII + Latin-1 Supplement) */
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
    CHILD_BAR        = 6,
    CHILD_SLIDER     = 7,
    CHILD_RADIO      = 8,   /* exklusive Auswahl innerhalb `group` */
    CHILD_DROPDOWN   = 9,   /* Klick cycled durch `options` (pipe-getrennt) */
    CHILD_CONTAINER  = 10,  /* Phase 6: bind_list-driven dynamic spawner       */
    CHILD_CASCADE    = 11,  /* Phase 6: cascade root-dropdown + payload sub-UI */
    CHILD_TEXTBOX    = 12,  /* multi-line scrollable text area (chat/log): wrap + window + scrollbar */
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
    std::string valign;              /* "top", "center" (default), "bottom" */
    std::string font_name;           /* "" = scene font; else a name from prefab_add_font */
    bool gutter = false;             /* line-number column (spec/44_6 §5g) */

    /* ── UI-widget style fields (button, text_input, rect with frame) ───── */
    uint32_t bg_color      = 0;      /* 0xRRGGBBAA */
    uint32_t border_color  = 0;
    float    border_width  = 0.0f;
    float    corner_radius = 0.0f;
    uint32_t hover_color   = 0;      /* button hover */
    uint32_t active_color  = 0;      /* button pressed / input focused */
    /* Label outline (see PrefabChild::outline_color). */
    uint32_t outline_color = 0;
    float    outline_px    = 0.0f;
    /* Tileable background texture for `rect` — stem resolved to a GL id
     * at instantiate time.  Tile repeats every `tile_size` logical pixels
     * regardless of panel dimensions. */
    std::string bg_texture_path;
    float       tile_size  = 128.0f;
    /* Slider skin: optional textured frame around the track and knob.
     * When set, the corresponding SDF pass is skipped and the sprite is
     * drawn stretched to the widget/knob bounds so the artwork keeps its
     * proportions.  handle_gem_texture (optional) is composited on top of
     * handle_texture and tinted with fill_color — meant for splittable
     * handles where the metal grip stays neutral and the inset gem takes
     * the mana color and glows during drag. */
    std::string track_texture_path;
    std::string handle_texture_path;
    std::string handle_gem_texture_path;
    /* Tileable border piece — same asset tiles along all 4 button edges.
     * `frame_border_size` is the on-screen thickness of the edge strips
     * (also used as tile pitch) so the pattern stays consistent no matter
     * how wide the button is.  Content sits INSIDE the border area. */
    std::string frame_border_texture_path;
    float       frame_border_size = 6.0f;
    /* Mirror the sprite horizontally at draw time — handy for reusing the
     * same asset for a "left" and "right" arrow button. */
    bool flip_h = false;
    /* Slider snap: when > 0, the slider's value quantizes to multiples of
     * `snap` while the user drags.  Zero means continuous. */
    float slider_snap = 0.0f;
    std::string label_text;          /* button label / static label */
    bool     is_secret     = false;  /* text_input: render '*' instead of chars */

    /* ── Bar/gauge (type "bar") + slider (type "slider") ────────────────────
     *   bar:    fill sized value/max of width (read-only progress widget)
     *   slider: same value/max + slider_min for the lower bound; mouse
     *           interaction maps cursor-x → value in [min, max]. A handle
     *           is rendered at the current fraction. */
    float    bar_value     = 0.0f;
    float    bar_max       = 1.0f;
    float    slider_min    = 0.0f;
    uint32_t fill_color    = 0;

    /* ── Callback function names (resolved at render time) ──────────────── */
    std::string on_click_fn;         /* button */
    std::string on_change_fn;        /* text_input value changed; slider value changed */

    /* ── Radio group (exclusive selection across instances with the same group) */
    std::string group;

    /* ── Dropdown: Pipe-separierte Optionen ("A|B|C") */
    std::string options;

    /* ── Material name (resolved at instantiate time to Material*) ──────── */
    std::string material_name;

    /* ── Phase 6: model-binding + container attributes ─────────────────────
     *   bind:      scalar field on the enclosing prefab's bound Model
     *              (two-way sync with widget value at render time)
     *   bind_list: list<T> field; container spawns one Prefab instance per
     *              element and keeps the count synchronized with .len
     *   spawn:     Prefab name to instantiate per element (used with bind_list)
     *   layout:    "vflow" / "hflow" — how spawned children are placed
     *   gap:       spacing between spawned children (pixels)                */
    std::string bind;
    /* Widget↔model binding (spec 44.6 §5d): field byte-offset + type tag
     * (0=i32, 1=f32, 2=string, 3=enum) baked by the compiler when the
     * enclosing prefab declares binds_model.  -1 = this child is not bound. */
    int         bind_offset = -1;
    int         bind_tag    = -1;
    std::string bind_list;
    std::string spawn;
    std::string layout;
    float       gap = 0.0f;
    /* Phase 6f — cascade widget metadata reference */
    std::string cascade_type;
    std::string payload_spawn;
    /* "horizontal" (default) or "vertical" — controls how the cascade's
     * spawned payload prefabs are laid out relative to the dropdown.       */
    std::string cascade_flow;
    bool        payload_scroll = false;  /* spec/44_6 §5h */
    /* `enum: TypeName` on a dropdown/radio — options come from the enum's
     * labels and the value accessor returns the true member value. */
    std::string enum_type;
    /* container `compact: true` — hidden spawned rows take NO layout space;
     * visible rows re-pack contiguously from the container origin each frame.
     * Lets AIML filter a list purely via set_visible without leaving gaps. */
    bool        compact = false;
    /* spec/44_6: a child may be declared hidden and revealed later via
     * set_visible.  Declaring the start state beats instantiating visible and
     * hiding it in the first frame — that shows one frame of the wrong thing. */
    bool        visible = true;
    bool        scrollable = false;  /* container: engine-managed row window + visible scrollbar (track+thumb, wheel+drag) */
    bool        tail = false;         /* textbox: stick to bottom as content grows (chat) */
    /* spec/44_6 §5f: `wrap: false` keeps a line a line, however
     * long it is — the box then scrolls SIDEWAYS.  A build log or a YAML
     * needs that: wrapping turns 200-character lines into mush
     * and takes away from the eye the structure the author wrote into it. */
    bool        wrap = true;
    int32_t     tb_prev_max = -1;     /* textbox: last max scroll offset (for tail-follow detection) */
    int32_t     tb_total = 0;         /* textbox render cache: wrapped line count (read by input) */
    int32_t     tb_visible = 0;       /* textbox render cache: visible line count (read by input) */
};

struct PrefabDef {
    std::string name;
    float width, height;
    std::string texture_path;
    bool draggable;
    bool input_transparent = false;   /* spec/44_6: applies to every instance */
    /* Phase 6: which Model/cascade type this prefab is bound to.  Runtime
     * uses it to route per-child `bind:` lookups to the right Model instance
     * held by this prefab-instance.                                          */
    std::string binds;
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

/* Forward — PrefabChild::spawned holds pointers to instances of THIS type. */
struct AimlPrefabInstance;

/* spec/44_6: hit trace, off by default (prefab.trace_hits). */
extern "C" void aiml_log_info(const char* msg, const char* loc);
static bool g_trace_hits = false;
/* Last resolved hits, "|"-separated.  "" = the press hit nothing,
 * "?" = not collected (tracing off).  Collection only runs with active
 * tracing, so that it costs other apps nothing; "?" prevents a
 * check for "nothing hit" from then passing for the wrong reason. */
static std::string g_last_hits;

struct PrefabChild {
    std::string name;
    PrefabChildType type;
    float x, y, width, height;
    float font_size;
    uint32_t color;        /* RGBA packed i32 */
    GLuint texture;        /* 0 = no texture */
    std::string text;      /* label text */
    std::string font_name; /* "" = scene font (spec/44_6: per-child font) */
    bool gutter = false;   /* line-number column for a multi-line field */
    /* Colour runs: flat triples (start, len, rgba) over the field's TEXT bytes
     * — the same offsets caret/selection and yaml.tokens use (spec/44_6 §5g). */
    std::vector<int32_t> color_runs;
    bool visible;

    /* ── UI-widget style (copied from def at instantiate time) ──────────── */
    uint32_t bg_color      = 0;
    uint32_t border_color  = 0;
    float    border_width  = 0.0f;
    float    corner_radius = 0.0f;
    uint32_t hover_color   = 0;
    uint32_t active_color  = 0;
    /* Text outline (labels only for now): if outline_px > 0 and outline_color
     * has non-zero alpha, the text is drawn 8 times at ±outline_px offsets in
     * outline_color, then once at the anchor in the main color — cheap
     * poor-man's stroke that stays readable over any icon background. */
    uint32_t outline_color = 0;
    float    outline_px    = 0.0f;
    /* Tileable background texture for `rect` (loaded from Def at
     * instantiate time).  0 → no bg texture; fill falls back to bg_color. */
    GLuint   bg_texture    = 0;
    float    tile_size     = 128.0f;
    /* Slider skin — resolved at instantiate time.  See PrefabChildDef. */
    GLuint   track_texture       = 0;
    GLuint   handle_texture      = 0;
    GLuint   handle_gem_texture  = 0;
    GLuint   frame_border_texture = 0;
    float    frame_border_size    = 6.0f;
    bool     flip_h              = false;
    float    slider_snap         = 0.0f;
    std::string label_text;
    bool     is_secret     = false;
    std::string on_click_fn;
    std::string on_change_fn;
    std::string group;   /* radio */
    std::string options; /* dropdown: pipe-separiert */
    /* Per-child buffer for aiml_prefab_dropdown_value: there used to be a
     * single thread_local buffer for all dropdowns — with two dropdown_value
     * calls in one expression the second call overwrote the buffer,
     * and the pointer from the first call then pointed to the second value. */
    mutable std::string dd_value_cache;
    int32_t     selected_index = 0;
    bool        dropdown_open  = false;
    int32_t     scroll_offset  = 0;   /* Popup-Scroll (# skipped rows) */
    bool        sb_dragging    = false; /* Popup-Scrollbar-Drag */
    bool        tb_drag        = false; /* textbox: body drag-to-scroll active (touch/maus) */
    int32_t     tb_drag_off    = 0;     /* textbox: scroll_offset bei Drag-Beginn */
    bool        sb_dragging_x  = false;  /* textbox: the LOWER bar is being dragged */

    /* ── Bar/gauge runtime state + slider state ─────────────────────────── */
    float    bar_value     = 0.0f;
    float    bar_max       = 1.0f;
    float    slider_min    = 0.0f;
    uint32_t fill_color    = 0;
    bool     slider_dragging = false;  /* slider: mouse held down on track */

    /* ── Material (custom shader + uniforms). Null = use default pipeline. */
    Material* material = nullptr;

    /* ── Runtime widget state (mouse interaction, focus, cursor) ────────── */
    bool     hot           = false; /* mouse over this child this frame */
    bool     pressed_now   = false; /* mouse down + initially pressed inside */
    bool     clicked_frame = false; /* set on mouse_released over a pressed button (cleared next frame) */
    bool     focused       = false; /* text_input: keyboard focus */
    int32_t  cursor_pos    = 0;     /* text_input: cursor BYTE index into text */
    int32_t  sel_anchor    = 0;     /* text_input: selection anchor (byte idx); selection = [min(anchor,cursor), max] */
    bool     sel_dragging  = false; /* text_input: mouse drag-select in progress */
    float    text_scroll_x = 0.0f;  /* text_input single-line: horizontal scroll offset (px) — caret-follow */
    float    text_scroll_y = 0.0f;  /* text_input multi-line: vertical scroll offset (line index) — caret-follow */
    std::vector<int32_t> caret_byte;/* text_input render cache: byte offset per caret stop (codepoint boundaries) */
    std::vector<float>   caret_x;   /* text_input render cache: screen-x per caret stop (parallel to caret_byte, already scroll-shifted) */
    bool     checked       = false; /* checkbox: toggle state */

    /* ── Phase 6: container / cascade widget state ────────────────────────
     * `spawned` are child prefab instances that this container widget
     * created based on the bound_list length.  Their lifecycle is managed
     * by the container in the sync-pass at render-time. */
    std::vector<AimlPrefabInstance*> spawned;
    /* copies of the compile-time binding attributes from the ChildDef so
     * we don't need to chase back to the def per frame. */
    std::string bind;
    /* Widget↔model binding (spec 44.6 §5d): field byte-offset + type tag
     * (0=i32, 1=f32, 2=string, 3=enum) baked by the compiler when the
     * enclosing prefab declares binds_model.  -1 = this child is not bound. */
    int         bind_offset = -1;
    int         bind_tag    = -1;
    std::string bind_list;
    std::string spawn;
    std::string layout;
    float       gap = 0.0f;
    std::string cascade_type;
    std::string payload_spawn;
    std::string cascade_flow;  /* "horizontal" (default) or "vertical" */
    /* spec/44_6 §5h: horizontal scrolling of the payload chain.  WINDOWING IS DONE
     * VIA VISIBILITY (pi->visible), just as the scrollable container windows its
     * rows — NOT via the GL scissor clip from §6c.  The first
     * attempt hung the clip on the row and made the WASM client crash in
     * render_instance ("index out of bounds", Firefox = invalid
     * table access); windowing via visibility, on the other hand, demonstrably runs in the client
     * (player/duel lists).  The offset snaps to the
     * payload pitch — with whole fields, windowing == clipping. */
    bool        payload_scroll   = false;
    bool        payload_sb_drag  = false;
    float       payload_scroll_x = 0.0f;
    float       payload_span     = 0.0f;  /* total width of the chain */
    float       payload_start    = 0.0f;  /* left edge of the payload zone (local) */
    float       payload_pitch    = 0.0f;  /* width of one payload + gap */
    /* `enum: TypeName` — dropdown/radio bound to an enum; drives options and
     * lets aiml_prefab_dropdown_enum_value return the true member value. */
    std::string enum_type;
    /* container compaction (copied from PrefabChildDef; may also be toggled at
     * runtime via aiml_prefab_container_set_compact).  See PrefabChildDef. */
    bool        compact = false;
    bool        scrollable = false;  /* container: engine-managed row window + visible scrollbar (track+thumb, wheel+drag) */
    bool        tail = false;         /* textbox: stick to bottom as content grows (chat) */
    bool        wrap = true;          /* spec/44_6 §5f: false = seitwaerts scrollen statt umbrechen */
    float       tb_off_x = 0.0f;      /* horizontal offset in pixels (only without wrapping) */
    float       tb_content_w = 0.0f;  /* width of the longest line (render intermediate) */
    float       tb_inner_w = 0.0f;    /* usable width — the input pass computes with it */
    AimlUIFont* tb_font = nullptr;    /* the same font as when drawing */
    int32_t     tb_prev_max = -1;     /* textbox: last max scroll offset (for tail-follow detection) */
    /* text_input: multi-line?  This is decided when DRAWING (height against
     * line height, spec/44_6 §5g) — the input pass reads the same result
     * instead of making the decision a second time. */
    bool        ti_multiline = false;
    /* The reader has scrolled themselves — then do NOT follow the cursor any more until it
     * really moves (spec/44_6 §5f).  Without this every frame pulled the
     * view back to the cursor, and scrolling was impossible. */
    bool        ti_free_scroll = false;
    int32_t     ti_caret_seen  = -1;
    int32_t     tb_total = 0;         /* textbox render cache: wrapped line count (read by input) */
    int32_t     tb_visible = 0;       /* textbox render cache: visible line count (read by input) */
    /* textbox selection (read-only text, spec/44_6 §5f).  Caret stops are
     * cached per VISIBLE line during render, exactly like text_input's
     * caret_x/caret_byte, but 2D: tb_row_start[r] indexes into tb_caret_* for
     * visible row r, and tb_row_y[r] is that row's top screen-y.  Byte values
     * are offsets into ch.text (the SOURCE), so Ctrl+C copies verbatim. */
    std::vector<float>   tb_caret_x;
    std::vector<int32_t> tb_caret_byte;
    std::vector<int32_t> tb_row_start;   /* size = visible rows + 1 (last = end sentinel) */
    std::vector<float>   tb_row_y;       /* size = visible rows */
    float       tb_row_h = 0.0f;         /* screen line height (for row hit-test) */
    bool        tb_sel_dragging = false; /* mouse drag-select in progress */
    /* Set by a NESTED cascade_payload_sync when one of our spawned_payload
     * instances respawned (its width changed).  Cleared after we re-layout
     * our own siblings.  Guards the whole cascade_payload_sync body so it
     * runs only on actual change (no per-frame layout).                     */
    bool needs_relayout = false;

    /* Phase 6f-w: per-widget bound_list.  A single prefab-instance can host
     * multiple containers each bound to a DIFFERENT list<list<i32>> — the
     * Ability page holds one container for triggers, one for effects, one
     * for costs.  When this pointer is non-null, container_sync prefers it
     * over the enclosing instance's bound_list. */
    void*   bound_list_widget = nullptr;
    /* Tracks the last outer-list pointer the container_sync saw for THIS
     * child.  When the caller swaps the bind_list (e.g. Ability Pager page
     * switch), we need to nuke every previously-spawned row so its widget
     * state (dropdown selected_index, text_input contents, popup-open, …)
     * doesn't leak into the new backing data on the very next persist pass. */
    void*   last_bound_list_seen = (void*)-1;   /* sentinel: never-seen */

    /* Phase 6f runtime state — reactive cascade widget:
     *   • `last_node_id`     — last observed tag; when the widget's dropdown
     *                          selection or the bound row's slot 0 changes,
     *                          the widget rebuilds its payload.
     *   • `spawned_payload`  — sub-widget prefab-instances currently mounted
     *                          below this cascade widget, one per field of
     *                          the current node.  Destroyed on node change. */
    int32_t                          last_node_id = -1;
    std::vector<AimlPrefabInstance*> spawned_payload;
};

struct AimlPrefabInstance {
    PrefabDef* def;
    /* Widget↔model binding (spec 44.6 §5d): the model instance attached via
     * bind_model(); borrowed (caller-owned).  sync_from/to_model read/write
     * each bound child's field at bound_model + child.bind_offset. */
    void*  bound_model = nullptr;
    float x, y;
    /* Per-instance width override — 0 means use def->width.  Cascade-payload
     * sync sets this on numeric text_input payloads so a "Value" chip can
     * be narrower than the generic AAFieldChip prefab (which also serves
     * wide enum dropdowns).                                                */
    float width_override = 0.0f;
    float scale_x, scale_y;
    float rotation;
    int32_t z_order;
    float alpha;
    bool visible;
    bool draggable;
    /* spec/44_6: true = SKIPPED by hit_test/update_hover.  Separates
     * "is drawn" from "takes input": a decorative surface (large
     * preview, banner, frame) otherwise makes everything beneath it unreachable, because
     * hit_test returns the topmost VISIBLE instance and `hovered` only gets this
     * one result. */
    bool input_transparent = false;
    /* spec/44_6: true = this instance BLOCKS input for everything with a
     * smaller z.  The counterpart of input_transparent: hit_test already arbitrates
     * by z, but the widget pass does NOT — it deliberately runs over
     * all instances, so that a transparent panel lets its widgets through.
     * The consequence was that a full-screen overlay correctly blocked the hover, but the
     * buttons beneath it stayed clickable. */
    bool input_blocking = false;
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

    /* Nested instances: a prefab instance can host other instances as
     * children. They inherit the parent's world transform (position + scale)
     * and are rendered right after the parent. Parented instances are skipped
     * in the top-level draw + hit-test loops (rendered/handled via the parent). */
    AimlPrefabInstance* parent = nullptr;
    std::vector<AimlPrefabInstance*> child_instances;

    /* Scroll-viewport: when clip_children is set, nested children are clipped
     * to this instance's world bounds (GL scissor) and shifted by scroll_x/y
     * (in local px). Turns a parent + nested content into a scroll container. */
    bool  clip_children = false;
    float scroll_x = 0.0f, scroll_y = 0.0f;

    /* Connector anchor mode for lines attached to this instance:
     *   0 = center      (all lines meet the instance centre)
     *   1 = edge        (all lines meet the centre of the edge facing the partner)
     *   2 = distributed (lines spread evenly along the facing edge) */
    int32_t connector_anchor = 0;

    /* Phase 6: model binding.
     *   bound_list  — pointer to a list<list<i32>> struct (aa_data / cascade
     *                 rows).  When a child has type=container + bind_list, the
     *                 container reads .len from this list and spawns/kills
     *                 sub-prefab-instances to match.
     *   bound_index — for prefab-instances spawned by a container:
     *                 the index into the parent's bound_list.  Their children
     *                 with bind: <field> read the list<i32> at this index. */
    void*   bound_list  = nullptr;
    int32_t bound_index = -1;

    /* Cascade payload seeding (load path).  A NESTED cascade payload instance
     * has no bound_list of its own; instead the enclosing cascade hands it a
     * pointer into the shared wire buffer so its cascade_payload_sync can
     * restore its sub-field values recursively (mirror of collect()'s
     * recursive save).  `seed_off` is the index of THIS cascade's node tag
     * within seed_wire.  One-shot: cleared after the first seed so later user
     * edits are not clobbered by a re-seed.  Top-level instances leave these
     * null and seed from bound_list instead. */
    int32_t* seed_wire = nullptr;
    int32_t  seed_len  = 0;
    int32_t  seed_off  = 0;

    /* Phase 6f-2 defer-delete.  container_sync / cascade_payload_sync can
     * mark an instance for deletion mid-frame, but the actual delete has to
     * wait until draw_scene's outer render loop has finished — otherwise a
     * still-cached pointer in that loop would dereference freed memory.
     * Marked instances are skipped in all rendering / hit-testing passes
     * and swept + freed at the end of draw_scene. */
    bool pending_delete = false;
};

/* ── Connector (cable between two instances) ─────────────────────────────────
 * Endpoints are resolved every frame from the live position/size of `from`
 * and `to`, so a cable follows its prefabs as they move.  Rendered as a
 * tessellated ribbon with a cylindrical-shading fragment mode (looks like a
 * round cable seen from above). */
struct AimlConnector {
    AimlPrefabInstance* from = nullptr;
    AimlPrefabInstance* to   = nullptr;
    uint32_t color0    = 0x44CCFFFFu;  /* gradient colour at the `from` end */
    uint32_t color1    = 0x2266AAFFu;  /* gradient colour at the `to` end   */
    uint32_t highlight = 0xFFFFFFFFu;  /* specular streak colour            */
    float    width     = 6.0f;         /* cable thickness in px             */
    float    bow        = 0.0f;        /* perpendicular curve amount (0 = straight) */
    int32_t  z_order   = -1;           /* default: behind all prefabs (z≥0) */
    bool     visible   = true;
    GLuint   port_tex  = 0;            /* optional endpoint sprite (0 = none) */
    float    port_scale = 1.6f;        /* port sprite size = width * port_scale */
};

/* ── Scene state ─────────────────────────────────────────────────────────── */

struct PrefabScene {
    std::vector<AimlPrefabInstance*> instances;
    std::vector<AimlConnector*>      connectors;   /* cables between instances */
    AimlPrefabInstance* dragged;
    AimlPrefabInstance* drag_source = nullptr;  /* who this gesture picked up (survives the drop frame) */
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
    /* Named extra fonts (prefab_add_font) + their size caches.  A code box
     * needs a monospace face while the rest of the UI stays proportional
     * (cosmos spec/98 F98-15), and one global font cannot serve both. */
    std::unordered_map<std::string, AimlUIFont*> named_fonts;
    std::unordered_map<std::string, std::unordered_map<int, AimlUIFont*>> named_cache;

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

    /* spec/44_6 §8.1 — partial draw.  Everything with z_order < draw_z_floor
     * has already been rendered THIS frame; INT32_MIN means "nothing drawn
     * yet", which is also the state draw_scene restores when it ends the
     * frame.  Caller-visible effect: draw_scene_upto(z) renders the window
     * [floor, z) and raises the floor, draw_scene renders [floor, +inf). */
    int32_t draw_z_floor = INT32_MIN;
};

static PrefabScene* g_scene = nullptr;

/* ── Phase 6f: cascade metadata registry ───────────────────────────────
 *  Populated at program init via aiml_prefab_register_cascade().  Widget
 *  code (CHILD_CASCADE) consults it to build the dropdown, look up field
 *  types, and manage payload sub-widget lifecycle. */
struct CascadeFieldMeta {
    std::string name;
    std::string type;
    std::string default_value;
};
struct CascadeNodeMeta {
    std::string              name;
    int                      id = -1;
    std::string              label;
    std::vector<CascadeFieldMeta> fields;
};
struct CascadeMeta {
    std::string                    name;
    std::string                    opcode_enum;    /* Enum-Name des Opcodes (z.B. "EffectCode") */
    std::vector<CascadeNodeMeta>   nodes;
    std::string                    options_cache;  /* pipe-joined labels */
    std::unordered_map<int,int>    id_to_opt_idx;  /* node.id → nodes[idx] */
};
static std::unordered_map<std::string, CascadeMeta> g_cascades;
/* Enum registry: name → pipe-separated labels ("Player|Opponent|…"). */
static std::unordered_map<std::string, std::string> g_enums;
/* Parallel enum values: name → member i32 values, index-aligned with labels.
 * Lets an enum-bound widget return the true member value (not selected_index+1)
 * for sparse / non-1-based enums (e.g. GameZone 1,2,10,11,…). */
static std::unordered_map<std::string, std::vector<int>> g_enum_values;

/* Auto-derive a UI label from a snake_case identifier: "draw_card" → "Draw Card",
 * "counter_trap" → "Counter Trap".  Used when the cascade decl doesn't set one. */
static std::string derive_label_from_name(const std::string& n) {
    std::string out;
    out.reserve(n.size());
    bool cap = true;
    for (char c : n) {
        if (c == '_') { out += ' '; cap = true; }
        else if (cap) { out += (char)toupper((unsigned char)c); cap = false; }
        else          { out += c; }
    }
    return out;
}

/* Helper: get scale factor screen/design resolution (defined before get_actual_pos
 * because it now uses it too). */
static inline void get_rel_scale(float& rsx, float& rsy) {
    if (g_scene && g_scene->design_w > 0 && g_scene->design_h > 0) {
        rsx = (float)g_scene->screen_w / (float)g_scene->design_w;
        rsy = (float)g_scene->screen_h / (float)g_scene->design_h;
    } else {
        rsx = 1.0f;
        rsy = 1.0f;
    }
}

/* Helper: get actual pixel position from coords.
 * Spec/44_6 §Coordinates: all pos/size values are in design space (1280×720,
 * or whatever scene_init was called with).  Rendering scales by
 * screen_w/design_w  so that the UI grows along on window resize/maximize.
 * is_relative=true: x,y as 0..1 ratios (legacy). */
static inline void get_actual_pos(AimlPrefabInstance* inst,
                                   float& ax, float& ay) {
    if (!g_scene) { ax = inst->x; ay = inst->y; return; }
    if (inst->is_relative) {
        ax = inst->x * g_scene->screen_w;
        ay = inst->y * g_scene->screen_h;
    } else if (inst->parent) {
        /* Nested: local design coords.  The parent's render_instance already
         * folded rel_scale into g_render_scale_{x,y}; applying it again here
         * would double-scale and push the child far past its intended
         * position.  Keep raw and let the outer multiply do the work. */
        ax = inst->x;
        ay = inst->y;
    } else {
        float rsx, rsy; get_rel_scale(rsx, rsy);
        ax = inst->x * rsx;
        ay = inst->y * rsy;
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
    } else if (u_mode == 4) {
        /* Connector cable. v_uv.x = length param [0,1] (gradient already baked
         * into v_color per-vertex), v_uv.y = cross param [0,1] across the
         * ribbon width.  Shade as a cylinder seen from the side: bright spine,
         * darker toward the rim, plus a narrow specular highlight.  The border
         * color (v_bcolor) carries the highlight tint; v_style.x carries the
         * edge anti-alias half-width in cross-units. */
        float cross = v_uv.y * 2.0 - 1.0;            /* -1 .. +1 across width */
        float c2    = clamp(1.0 - cross * cross, 0.0, 1.0);
        float round = sqrt(c2);                      /* 1 at spine, 0 at rim  */
        /* Cylindrical body shading: rim ~0.70 (still well-saturated), spine 1.0 */
        vec3 body = v_color.rgb * (0.70 + 0.30 * round);
        /* Specular streak slightly above the spine (mild — avoid washing out) */
        float spec = pow(clamp(1.0 - abs(cross + 0.25) * 2.4, 0.0, 1.0), 3.0);
        body += v_bcolor.rgb * spec * 0.35;
        /* Anti-aliased edge: fade alpha only over the outermost rim pixels */
        float aa = max(v_style.x, 1e-4);
        float edge = smoothstep(0.0, aa, c2);
        out_color = vec4(body, v_color.a * edge);
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

/* Forward-decl: texture cache lives further down but the text renderer needs
 * it for inline `{icon:stem}` tokens. */
static GLuint load_texture_cached(const char* path);

/* If `p` points to a `{icon:stem}` token, fill *stem_out (nullable) with the
 * stem, advance *end_out past the closing brace, and return true.  Otherwise
 * false and no state is touched.  Used by both wrap/width and render passes so
 * they agree on token boundaries. */
static bool try_parse_icon_tok(const char* p, std::string* stem_out, const char** end_out) {
    if (!p || strncmp(p, "{icon:", 6) != 0) return false;
    const char* start = p + 6;
    const char* end = strchr(start, '}');
    if (!end) return false;
    if (stem_out) stem_out->assign(start, (size_t)(end - start));
    if (end_out)  *end_out = end + 1;
    return true;
}

/* Decode one UTF-8 code point from p, advance p past it, and return the
 * codepoint.  Only handles Latin-1 Supplement (2-byte sequences) — enough
 * for German/French; for 3+ byte sequences the function returns 0 so the
 * caller drops the char.  Invariant: p is advanced by at least 1 byte on
 * every call to prevent infinite loops on malformed input. */
static uint32_t decode_utf8_char(const char*& p) {
    unsigned char b0 = (unsigned char)*p++;
    if (b0 < 0x80) return b0;                        /* ASCII */
    if ((b0 & 0xE0) == 0xC0) {                        /* 2-byte */
        unsigned char b1 = (unsigned char)*p;
        if ((b1 & 0xC0) != 0x80) return 0;
        ++p;
        return ((uint32_t)(b0 & 0x1F) << 6) | (b1 & 0x3F);
    }
    /* 3-/4-byte sequences: consume + drop.  Not covered by the current
     * 32..255 atlas anyway. */
    if ((b0 & 0xF0) == 0xE0) { if (*p) ++p; if (*p) ++p; }
    else if ((b0 & 0xF8) == 0xF0) { if (*p) ++p; if (*p) ++p; if (*p) ++p; }
    return 0;
}

static float text_width_font(AimlUIFont* font, const char* text) {
    if (!font || !text) return 0;
    float w = 0;
    const char* p = text;
    while (*p) {
        /* Inline icon token — width = ascent_px (icon is a square glyph
         * aligned with cap-height; keep the width formula in lockstep
         * with push_text_batch or centering / wrapping desyncs). */
        const char* tok_end;
        if (try_parse_icon_tok(p, nullptr, &tok_end)) {
            w += font->ascent_px;
            p = tok_end;
            continue;
        }
        if (*p == '\n') break;
        uint32_t cp = decode_utf8_char(p);
        if (cp < 32 || cp >= 256) continue;
        w += font->cdata[cp - 32].xadvance;
    }
    return w;
}

/* Word-wrap `text` into rendered lines.
 *
 * `spans` (optional) receives, per emitted line, the SOURCE byte range
 * [begin, end) that the line covers.  Rendered lines are not plain slices of
 * the source (runs of spaces collapse to one, `\n`/`|` separators vanish), so
 * selection needs this mapping to turn a click into a source byte index —
 * see tb_build_caret_stops.  spans->size() always equals lines.size().
 *
 * Whitespace is kept VERBATIM — leading indentation and runs between words
 * alike.  Collapsing runs is a prose habit and it destroys the two things this
 * widget is actually used for: indented source and column-aligned logs
 * (spec/44_6, cosmos spec/98 F98-14).  The only whitespace that disappears is
 * the run at which a line was wrapped, because a continuation line starts at
 * the left edge. */
static std::vector<std::string> wrap_text_lines(AimlUIFont* font, const char* text, float max_w,
                                                std::vector<std::pair<int32_t,int32_t>>* spans = nullptr) {
    std::vector<std::string> lines;
    if (spans) spans->clear();
    if (!font || !text || !*text) return lines;

    std::string current_line;
    std::string current_word;
    std::string pending_ws;      /* whitespace seen before the next word, kept verbatim */
    int32_t line_begin = 0, line_end = 0, word_begin = 0;

    auto flush_line = [&]() {
        lines.push_back(current_line);
        if (spans) spans->push_back({line_begin, line_end});
        current_line.clear();
    };
    auto append_word = [&](int32_t word_end) {
        if (current_word.empty()) return;
        std::string candidate = current_line + pending_ws + current_word;
        if (max_w > 0.0f && !current_line.empty() &&
            text_width_font(font, candidate.c_str()) > max_w) {
            flush_line();
            current_line = current_word;   /* the break whitespace is dropped */
            line_begin   = word_begin;
        } else {
            if (current_line.empty() && pending_ws.empty()) line_begin = word_begin;
            current_line = candidate;
        }
        pending_ws.clear();
        line_end = word_end;
        current_word.clear();
    };

    const char* p = text;
    int32_t pos = 0;
    while (*p) {
        char c = *p++;
        int32_t cpos = pos++;
        if (c == '\n' || c == '|') {
            append_word(cpos);
            flush_line();
            pending_ws.clear();
            line_begin = pos; line_end = pos;
            continue;
        }
        if (c == ' ') {
            append_word(cpos);
            pending_ws += ' ';
            line_end = pos;
            continue;
        }
        if (current_word.empty()) word_begin = cpos;
        current_word += c;
        if (max_w > 0.0f && current_line.empty() && text_width_font(font, current_word.c_str()) > max_w) {
            lines.push_back(current_word);
            if (spans) spans->push_back({word_begin, pos});
            current_word.clear();
            line_begin = pos; line_end = pos;
            pending_ws.clear();
        }
    }
    append_word(pos);
    if (!current_line.empty()) flush_line();
    if (lines.empty()) {
        lines.push_back("");
        if (spans) spans->push_back({0, 0});
    }
    return lines;
}

/* Caret stops for ONE wrapped textbox line (spec/44_6 §5f): for every codepoint
 * boundary of the RENDERED line, its screen-x and the matching SOURCE byte
 * offset.  Since wrap_text_lines keeps whitespace verbatim, a rendered line is
 * now a plain slice of the source and the walk is 1:1 — with one exception: a
 * continuation line produced by wrapping starts AFTER the whitespace at which
 * the break happened, so that run is skipped once at the start.  Byte offsets
 * stay monotonic and clamped to the line's span, so a click never lands
 * outside its own line. */
static void tb_build_caret_stops(AimlUIFont* font, const std::string& src,
                                 int32_t span_b, int32_t span_e,
                                 const std::string& line, float x0,
                                 std::vector<float>& out_x,
                                 std::vector<int32_t>& out_byte)
{
    auto src_is_ws = [&](int32_t i) {
        return i < span_e && (src[(size_t)i] == ' ' || src[(size_t)i] == '\n' || src[(size_t)i] == '|');
    };
    int32_t si = span_b;
    /* Dropped break whitespace: only when the rendered line does NOT itself
     * start with a space — otherwise that space is real content. */
    if (line.empty() || line[0] != ' ') {
        while (src_is_ws(si)) ++si;
    }
    out_x.push_back(x0);
    out_byte.push_back(si);
    size_t ri = 0;
    while (ri < line.size()) {
        size_t rn = ri + 1;
        while (rn < line.size() && ((unsigned char)line[rn] & 0xC0) == 0x80) ++rn;
        si += (int32_t)(rn - ri);
        if (si > span_e) si = span_e;
        out_x.push_back(x0 + text_width_font(font, line.substr(0, rn).c_str()));
        out_byte.push_back(si);
        ri = rn;
    }
}

/* Forward-decl — vocab runtime lives in vocab_runtime.cpp. */
extern "C" const char* aiml_vocab_lookup(const char* source);

static void push_text_batch(DrawBatch& batch, AimlUIFont* font,
                            const char* text, float cx, float cy,
                            float max_w, uint32_t rgba, const char* anchor)
{
    if (!font || !text || !*text) return;
    /* Universal i18n hook: swap the source string for its translation
     * before any layout/glyph work.  Falls back to `text` unchanged if
     * no vocab is loaded or the source has no entry — nothing to do
     * for callers that use English-canonical labels. */
    text = aiml_vocab_lookup(text);
    if (!text || !*text) return;

    std::vector<std::string> lines = wrap_text_lines(font, text, max_w);
    float line_height = font->line_height;
    float base_y = cy + font->ascent_px;
    /* White sprite tint — icons render at their native RGB, no color mult. */
    const uint32_t kIconTint = 0xFFFFFFFFu;

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

        const char* p = line.c_str();
        while (*p) {
            /* Inline icon: {icon:stem} — render as a square sprite the
             * height of the line, sitting on the text baseline (top-left
             * = baseline - line_height). Flushes the pending text batch,
             * draws the icon in its own sprite batch, then continues
             * appending to the same text batch. */
            std::string icon_stem;
            const char* tok_end;
            if (try_parse_icon_tok(p, &icon_stem, &tok_end)) {
                /* Icon sits on the text baseline aligned with the top of
                 * the caps: top = baseline - ascent_px, size = ascent_px
                 * (square).  Using line_height for the size made icons
                 * hang above the visible x-height by the line_gap, so
                 * they looked lifted vs. the surrounding glyphs. */
                float icon_size = font->ascent_px;
                float x1 = x + icon_size;
                if (max_w > 0 && x1 > limit) break;
                /* Flush accumulated glyphs so the icon draws on top of
                 * anything before it and doesn't get lumped into the
                 * font-atlas draw call. */
                flush_batch(batch);
                batch.verts.clear();
                GLuint tex = load_texture_cached(icon_stem.c_str());
                if (tex) {
                    DrawBatch ib;
                    ib.tex  = tex;
                    ib.mode = 1;   /* rgba texture */
                    push_quad(ib, x, y - font->ascent_px, icon_size, icon_size,
                              0, 0, 1, 1, kIconTint);
                    flush_batch(ib);
                }
                x = x1;
                p = tok_end;
                continue;
            }
            uint32_t cp = decode_utf8_char(p);
            if (cp < 32 || cp >= 256) continue;
            stbtt_bakedchar* bc = &font->cdata[cp - 32];
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

    /* Asset-registry-first: when the prefab gives a stem (no slash, no
     * extension), resolve via the AssetScanner.  Falls back to raw-path
     * loading for legacy callers that pass a real path. */
    void* t = nullptr;
    bool looks_like_stem = !strchr(path, '/') && !strchr(path, '.');
    if (looks_like_stem) {
        t = aiml_asset_get_texture(path);
    }
    if (!t) {
        t = aiml_gl_load_texture(path);
    }
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
    if (strcmp(s, "radio") == 0)      return CHILD_RADIO;
    if (strcmp(s, "dropdown") == 0)   return CHILD_DROPDOWN;
    if (strcmp(s, "bar") == 0)        return CHILD_BAR;
    if (strcmp(s, "slider") == 0)     return CHILD_SLIDER;
    if (strcmp(s, "container") == 0)  return CHILD_CONTAINER;
    if (strcmp(s, "textbox") == 0)    return CHILD_TEXTBOX;
    if (strcmp(s, "cascade") == 0)    return CHILD_CASCADE;
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
            else if (key == "valign") cd.valign = parse_json_string(p);
            // `x`/`y` are legacy; the AIML naming-lint forces ≥3-char identifiers,
            // so YAML prefab children naturally write `pos_x`/`pos_y` to stay
            // consistent with the rest of the source.  Accept both.
            else if (key == "x" || key == "pos_x") cd.x = (float)parse_json_number(p);
            else if (key == "y" || key == "pos_y") cd.y = (float)parse_json_number(p);
            else if (key == "width") cd.width = (float)parse_json_number(p);
            else if (key == "height") cd.height = (float)parse_json_number(p);
            else if (key == "font_size") cd.font_size = (float)parse_json_number(p);
            else if (key == "font")      cd.font_name = parse_json_string(p);
            else if (key == "gutter")    cd.gutter = parse_json_number(p) != 0;
            else if (key == "color") cd.color = (uint32_t)parse_json_number(p);
            /* UI-widget style fields */
            else if (key == "bg_color")      cd.bg_color = (uint32_t)parse_json_number(p);
            else if (key == "border_color")  cd.border_color = (uint32_t)parse_json_number(p);
            else if (key == "border_width")  cd.border_width = (float)parse_json_number(p);
            else if (key == "corner_radius") cd.corner_radius = (float)parse_json_number(p);
            else if (key == "hover_color")   cd.hover_color = (uint32_t)parse_json_number(p);
            else if (key == "active_color")  cd.active_color = (uint32_t)parse_json_number(p);
            else if (key == "outline_color") cd.outline_color = (uint32_t)parse_json_number(p);
            else if (key == "outline_px")    cd.outline_px = (float)parse_json_number(p);
            else if (key == "bg_texture")    cd.bg_texture_path = parse_json_string(p);
            else if (key == "tile_size")     cd.tile_size = (float)parse_json_number(p);
            else if (key == "track_texture")      cd.track_texture_path      = parse_json_string(p);
            else if (key == "handle_texture")     cd.handle_texture_path     = parse_json_string(p);
            else if (key == "handle_gem_texture") cd.handle_gem_texture_path = parse_json_string(p);
            else if (key == "frame_border_texture") cd.frame_border_texture_path = parse_json_string(p);
            else if (key == "frame_border_size")    cd.frame_border_size = (float)parse_json_number(p);
            else if (key == "flip_h")             cd.flip_h = (parse_json_number(p) != 0);
            else if (key == "snap")               cd.slider_snap = (float)parse_json_number(p);
            else if (key == "label")         cd.label_text = parse_json_string(p);
            else if (key == "secret")        cd.is_secret = (parse_json_number(p) != 0);
            else if (key == "value")         cd.bar_value = (float)parse_json_number(p);
            else if (key == "max")           cd.bar_max = (float)parse_json_number(p);
            else if (key == "min")           cd.slider_min = (float)parse_json_number(p);
            else if (key == "fill_color")    cd.fill_color = (uint32_t)parse_json_number(p);
            else if (key == "on_click")      cd.on_click_fn = parse_json_string(p);
            else if (key == "group")         cd.group       = parse_json_string(p);
            else if (key == "options")       cd.options     = parse_json_string(p);
            else if (key == "enum")          cd.enum_type   = parse_json_string(p);
            else if (key == "on_change")     cd.on_change_fn = parse_json_string(p);
            else if (key == "material")      cd.material_name = parse_json_string(p);
            /* Phase 6: model binding + container attributes */
            else if (key == "bind")          cd.bind = parse_json_string(p);
            else if (key == "bind_offset")   cd.bind_offset = (int)parse_json_number(p);
            else if (key == "bind_tag")      cd.bind_tag = (int)parse_json_number(p);
            else if (key == "bind_list")     cd.bind_list = parse_json_string(p);
            else if (key == "spawn")         cd.spawn = parse_json_string(p);
            else if (key == "layout")        cd.layout = parse_json_string(p);
            else if (key == "gap")           cd.gap = (float)parse_json_number(p);
            else if (key == "compact")       cd.compact = (parse_json_number(p) != 0);
            else if (key == "visible")       cd.visible = (parse_json_number(p) != 0);
            else if (key == "scrollable")    cd.scrollable = (parse_json_number(p) != 0);
            else if (key == "tail")          cd.tail = (parse_json_number(p) != 0);
            else if (key == "wrap")          cd.wrap = (parse_json_number(p) != 0);
            else if (key == "cascade_type")  cd.cascade_type = parse_json_string(p);
            else if (key == "payload_spawn") cd.payload_spawn = parse_json_string(p);
            else if (key == "cascade_flow")  cd.cascade_flow = parse_json_string(p);
            else if (key == "payload_scroll") cd.payload_scroll = (parse_json_number(p) != 0);
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
/* Named per-child font (spec/44_6; cosmos spec/98 F98-15).  Falls back to the
 * scene font when the name is unknown, so a missing asset degrades to the
 * default face instead of drawing nothing. */
static AimlUIFont* font_for_size_named(PrefabScene* s, const std::string& name, float font_size);

/* Parent-transform applied to the instance currently being rendered. Set while
 * recursing into nested child instances; identity for top-level instances. */
static float g_render_off_x = 0.0f, g_render_off_y = 0.0f;
static float g_render_scale_x = 1.0f, g_render_scale_y = 1.0f;

/* ── Phase 6d: container sync ────────────────────────────────────────────
 * For every CHILD_CONTAINER widget with a `spawn:` and the enclosing
 * instance's `bound_list` set: read the outer list.size and adjust the
 * container's `spawned` vector so we render exactly one child prefab per
 * list element.  The spawned instance's `bound_list` is set to the
 * corresponding inner list<i32> (the cascade row) so its own children
 * can bind scalar fields to slots of that row. */
// spec/61 §5.7 (memV3): a list<list<i32>> / list<cascade<X>> stores its rows as HANDLE pointers
// (pointer stride), not inline {size,cap,data} structs — same rule as aiml_cascade_persist_row.
static void* cascade_row_handle(void* outer_data, int32_t idx) {
    return outer_data ? *(void**)((char*)outer_data + (size_t)idx * sizeof(void*)) : nullptr;
}

static void container_sync(AimlPrefabInstance* inst) {
    if (!inst) return;
    /* AIML list<T> layout: { i32 size, i32 capacity, ptr data }.
     * For list<list<i32>>, `data` is an array of the same 16-byte struct. */
    struct ListHdr { int32_t size; int32_t capacity; void* data; };

    for (auto& ch : inst->children) {
        if (ch.type != CHILD_CONTAINER)  continue;
        if (ch.spawn.empty())            continue;

        /* Per-widget bound_list wins over instance-level fallback.  Lets a
         * single AbilityPage host separate Trigger / Effects / Costs
         * containers each bound to its own list<list<i32>>.               */
        ListHdr* outer = (ListHdr*)(ch.bound_list_widget ? ch.bound_list_widget
                                                        : inst->bound_list);
        if (!outer) continue;

        /* Detect a bind_list SWAP (page-switch on the Ability Pager) and
         * nuke every existing spawned row.  Without this the surviving
         * widgets carry their old selected_index / text into the next
         * frame's persist pass and stomp the new ability's data. */
        if (ch.last_bound_list_seen != (void*)outer) {
            for (AimlPrefabInstance* dead : ch.spawned) {
                if (dead) dead->pending_delete = true;
            }
            ch.spawned.clear();
            ch.last_bound_list_seen = (void*)outer;
        }

        int desired = outer->size;
        if (desired < 0) desired = 0;
        /* Defensive cap: `outer` is a NON-OWNING raw pointer into an AIML list handle
         * (aiml_prefab_bind_list stores it as-is). If the bound list is freed/reassigned
         * without a rebind (e.g. a screen's begin_load resets its list fields mid-load),
         * this reads a DANGLING block → a wild `size` → an unbounded spawn loop that
         * grows the (wasm) heap until OOM/abort (observed: 9rooms deck editor black
         * screen, std::vector<PrefabChild> __throw_bad_alloc). A legitimate bound list is
         * far below this cap; a value above it means a stale/garbage pointer, so render 0
         * rows and log once rather than crash the whole client. */
        static const int kPrefabMaxSpawn = 100000;
        if (desired > kPrefabMaxSpawn) {
            fprintf(stderr, "[prefab] ERROR: container '%s' bind_list size=%d exceeds cap %d "
                    "(dangling/garbage bound_list — freed without rebind?) — spawning 0 rows\n",
                    ch.name.c_str(), desired, kPrefabMaxSpawn);
            desired = 0;
        }

        /* Shrink first — mark for deferred deletion (see the pending_delete
         * doc-comment on AimlPrefabInstance). */
        while ((int)ch.spawned.size() > desired) {
            AimlPrefabInstance* dead = ch.spawned.back();
            ch.spawned.pop_back();
            if (dead) dead->pending_delete = true;
        }
        /* Grow */
        bool hflow = (ch.layout == "hflow");
        while ((int)ch.spawned.size() < desired) {
            void* raw = aiml_prefab_instantiate(ch.spawn.c_str());
            if (!raw) break;  /* unknown prefab — bail rather than loop forever */
            auto* row = (AimlPrefabInstance*)raw;
            row->parent = inst;
            row->bound_index = (int)ch.spawned.size();
            /* Layout: vflow (default) stacks rows top-down.  hflow lays them
             * side-by-side left-to-right — useful for card-library rows.    */
            if (hflow) {
                row->x = ch.x + row->bound_index * (row->def->width + ch.gap);
                row->y = ch.y;
            } else {
                row->x = ch.x;
                row->y = ch.y + row->bound_index * (row->def->height + ch.gap);
            }
            inst->child_instances.push_back(row);
            ch.spawned.push_back(row);
        }
        /* Refresh each row's outer-list pointer + index.  Storing the outer
         * struct pointer (NOT outer->data) is safe: outer itself lives on
         * the caller's model and isn't reallocated when list.push grows the
         * inner data buffer.  Consumers (cascade_payload_sync) dereference
         * outer->data at ACCESS time so a mid-frame realloc is invisible.  */
        for (int i = 0; i < (int)ch.spawned.size(); ++i) {
            if (!ch.spawned[i]) continue;
            ch.spawned[i]->bound_index = i;
            ch.spawned[i]->bound_list  = outer;
        }

        /* For each freshly-spawned chip, PULL the cascade dropdown's
         * selected_index from the bound row's tag ONCE — right after
         * spawn, before cascade_payload_sync gets to react.  We detect
         * "freshly spawned" by `last_node_id == -1` (its struct-init
         * value; cascade_payload_sync stamps a real id once it processes
         * the widget).  If we ran this every frame, we'd overwrite the
         * user's live click before the persist pass sees it. */
        for (int i = 0; i < (int)ch.spawned.size(); ++i) {
            AimlPrefabInstance* row = ch.spawned[i];
            if (!row) continue;
            for (auto& sub : row->children) {
                if (sub.cascade_type.empty())                                continue;
                if (sub.type != CHILD_DROPDOWN && sub.type != CHILD_CASCADE) continue;
                if (sub.last_node_id != -1)                                  break;
                if (i >= outer->size)                                        break;
                ListHdr* inner = (ListHdr*)cascade_row_handle(outer->data, i);
                if (!inner || inner->size < 1 || !inner->data)               break;
                int32_t stored_id = ((int32_t*)inner->data)[0];
                int32_t opt = aiml_prefab_cascade_opt_idx_for(
                                  sub.cascade_type.c_str(), stored_id);
                if (opt >= 0) sub.selected_index = opt;
                break;
            }
        }

        /* ── Compacting layout ────────────────────────────────────────────
         * When `compact` is set, hidden rows (set_visible(0)) take NO layout
         * space: we re-pack the VISIBLE spawned rows contiguously from the
         * container origin every frame.  This lets AIML filter a bound list
         * purely by toggling row visibility — no gaps, always left-aligned.
         * Cheap (arithmetic over the spawned rows); safe to run per frame. */
        /* ── Scrollable window ────────────────────────────────────────────
         * `scrollable: true` → the ENGINE windows the spawned rows by
         * ch.scroll_offset (row index): only [offset, offset+visible) are shown
         * and then packed like `compact`.  visible = floor(extent / row_pitch).
         * (Overrides AIML row-visibility — for filtered lists keep AIML
         * pagination instead.) */
        if (ch.scrollable && !ch.spawned.empty()) {
            bool sflow_h = (ch.layout == "hflow");
            AimlPrefabInstance* r0 = ch.spawned[0];
            float pitch  = sflow_h ? (r0->def->width + ch.gap) : (r0->def->height + ch.gap);
            float extent = sflow_h ? ch.width : ch.height;
            int   visible = (pitch > 0.0f) ? (int)(extent / pitch) : (int)ch.spawned.size();
            if (visible < 1) visible = 1;
            int total   = (int)ch.spawned.size();
            int max_off = total > visible ? total - visible : 0;
            if (ch.scroll_offset > max_off) ch.scroll_offset = max_off;
            if (ch.scroll_offset < 0)       ch.scroll_offset = 0;
            for (int k = 0; k < total; ++k) {
                if (ch.spawned[k])
                    ch.spawned[k]->visible = (k >= ch.scroll_offset && k < ch.scroll_offset + visible);
            }
        }
        if (ch.compact || ch.scrollable) {
            bool cflow_h = (ch.layout == "hflow");
            int slot = 0;
            for (AimlPrefabInstance* row : ch.spawned) {
                if (!row || !row->visible) continue;
                /* Don't fight an active drag: the row being dragged keeps its
                 * cursor-following position (set by the drag handler).  We still
                 * reserve its slot so the remaining rows don't shift underneath
                 * it.  Without this the compact re-pack overwrites row->x/y every
                 * frame → the dragged card snaps back into the grid and never
                 * follows the pointer. */
                if (g_scene && g_scene->dragged == row) { ++slot; continue; }
                if (cflow_h) {
                    row->x = ch.x + slot * (row->def->width  + ch.gap);
                    row->y = ch.y;
                } else {
                    row->x = ch.x;
                    row->y = ch.y + slot * (row->def->height + ch.gap);
                }
                ++slot;
            }
        }
    }
}

/* ── Phase 6f-2: cascade widget reactive payload sync ────────────────────
 * For every dropdown widget with a `cascade_type` + `payload_spawn`, watch
 * the selection.  When it changes:
 *   1) Destroy every currently mounted `spawned_payload` prefab-instance
 *      (removed from BOTH parent.child_instances AND g_scene->instances).
 *      Any recursive spawns underneath fall away with them because a
 *      destroyed instance's own container/cascade state is gone.
 *   2) Spawn one `payload_spawn` instance per field of the newly-selected
 *      node.  Each spawned row is positioned right of / below the
 *      dropdown box, marked as a nested child (parent = inst).
 * This is what makes the user-visible cascade "collapse" and "re-expand"
 * as a node higher up in the chain gets changed. */
/* Fwd-decl: enum member value → option index (defined below, near enum_value_at).
 * Used by the payload-seed load path to stay symmetric with collect()'s save. */
static int enum_index_of_value(const std::string& enum_type, int value);

/* Advance `pos` past exactly ONE value of `type` in the flat wire buffer.
 * Mirrors the recursive consumption in aiml_casc_collect_field_from / the
 * recursive save collect(): a cascade-typed value consumes its node tag plus,
 * recursively, one value per field of that node; every other type (enum /
 * alias / primitive) consumes a single slot.  Returns the next position, or
 * -1 if the wire is truncated / the sub-node id is unknown.  This is what lets
 * the load path compute each field's TRUE wire offset instead of the old flat
 * `1 + f` (which drifted after any multi-slot field and never restored nested
 * Value/Duration sub-values). */
static int casc_advance_value(const std::string& type,
                              const int32_t* wire, int len, int pos) {
    auto it = g_cascades.find(type);
    if (it == g_cascades.end()) {           /* enum / alias / primitive → 1 slot */
        return (pos >= 0 && pos < len) ? pos + 1 : -1;
    }
    if (pos < 0 || pos >= len) return -1;
    int32_t node_id = wire[pos++];          /* consume the sub-node tag */
    auto idIt = it->second.id_to_opt_idx.find(node_id);
    if (idIt == it->second.id_to_opt_idx.end()) return -1;
    const CascadeNodeMeta& node = it->second.nodes[idIt->second];
    for (const auto& fld : node.fields) {
        pos = casc_advance_value(fld.type, wire, len, pos);
        if (pos < 0) return -1;
    }
    return pos;
}

static void cascade_payload_sync(AimlPrefabInstance* inst) {
    if (!inst || !g_scene) return;
    for (auto& ch : inst->children) {
        if (ch.type != CHILD_DROPDOWN && ch.type != CHILD_CASCADE) continue;
        if (ch.cascade_type.empty() || ch.payload_spawn.empty())   continue;

        /* Read current selection.  For MVP we drive off the widget's
         * selected_index (user click).  Later this widens to reading the
         * bound cascade row's slot 0 so external mutation also triggers
         * a rebuild. */
        int opt_idx = ch.selected_index;
        int32_t node_id = aiml_prefab_cascade_node_id_at(ch.cascade_type.c_str(), opt_idx);

        bool respawn = (node_id != ch.last_node_id);
        /* needs_relayout: a NESTED cascade in one of our spawned_payload
         * instances changed (its expansion width shifted), so our own
         * siblings need to slide.  Propagated bottom-up from the inner
         * cascade_payload_sync that did the respawn — see the walk-up
         * loop below.  Guards spawn + layout so those only run on actual
         * change; the persist pass further down still runs every frame so
         * text-input edits get pushed through to the wire.                  */
        bool do_spawn_or_layout = (respawn || ch.needs_relayout);

        /* Persist the user's node choice into the bound cascade row.  The
         * enclosing container refreshed row->bound_list to the current outer
         * list<list<i32>> pointer; combined with bound_index we can reach
         * the row's inner list<i32> and stamp slot 0 = node_id.            */
        AimlPrefabInstance* row = inst;
        if (respawn && row->bound_list && row->bound_index >= 0) {
            struct ListHdr { int32_t size; int32_t capacity; void* data; };
            ListHdr* outer = (ListHdr*)row->bound_list;
            if (outer && row->bound_index < outer->size) {
                ListHdr* inner = (ListHdr*)cascade_row_handle(outer->data, row->bound_index);
                if (inner && inner->data && inner->size >= 1) {
                    ((int32_t*)inner->data)[0] = node_id;
                }
            }
            /* Dump the whole outer list to stderr so the user can see how
             * their UI clicks translate to the wire bytecode.  Cheap because
             * this only fires ON CHANGE, not every frame.                   */
            ListHdr* outer2 = (ListHdr*)row->bound_list;
            if (outer2) {
                fprintf(stderr, "[cascade] model=[");
                for (int32_t r = 0; r < outer2->size; ++r) {
                    ListHdr* inner = (ListHdr*)cascade_row_handle(outer2->data, r);
                    fprintf(stderr, r ? ", [" : "[");
                    for (int32_t k = 0; inner && k < inner->size; ++k) {
                        fprintf(stderr, k ? ",%d" : "%d", ((int32_t*)inner->data)[k]);
                    }
                    fprintf(stderr, "]");
                }
                fprintf(stderr, "]\n");
            }
        }

        /* 1) On change: mark previously mounted payload for delete.  Actual
         *    delete + detach happens in the end-of-frame sweep, so any cached
         *    ptr in the outer render loop reads a pending_delete=true and
         *    safely skips the instance. */
        if (respawn) {
            for (AimlPrefabInstance* dead : ch.spawned_payload) {
                if (dead) dead->pending_delete = true;
            }
            ch.spawned_payload.clear();
            ch.last_node_id = node_id;
        }

        if (node_id < 0) continue;  /* nothing to render for "---" / invalid */

        /* 2) Spawn one payload prefab per field of the current node (only on
         *    change — otherwise we just re-layout the existing instances). */
        int n_fields = aiml_prefab_cascade_field_count(ch.cascade_type.c_str(), node_id);
        if (n_fields <= 0) continue;
        /* Reach the bound row now — we may need to seed newly-spawned
         * payload widgets with their previously-stored values so a bind-
         * swap (Ability pager) doesn't reset their selections. */
        struct ListHdrIn { int32_t size; int32_t capacity; void* data; };
        int32_t* stored_wire = nullptr;
        int32_t  stored_size = 0;
        int32_t  wire_base   = 0;   /* index of THIS node's tag within stored_wire */
        if (inst->seed_wire) {
            /* NESTED cascade payload: seed from the parent-supplied wire slice.
             * Our node tag sits at seed_off; fields follow it. */
            stored_wire = inst->seed_wire;
            stored_size = inst->seed_len;
            wire_base   = inst->seed_off;
        } else {
            ListHdrIn* outer_r = (ListHdrIn*)inst->bound_list;
            if (outer_r && inst->bound_index >= 0 &&
                inst->bound_index < outer_r->size) {
                ListHdrIn* inner_r = (ListHdrIn*)cascade_row_handle(outer_r->data, inst->bound_index);
                if (inner_r) {
                    stored_wire = (int32_t*)inner_r->data;
                    stored_size = inner_r->size;
                }
            }
        }
        /* Running wire offset of the field currently being spawned.  Starts one
         * past this node's tag; advances by each field's TRUE (recursive) span
         * so nested cascade fields don't drift the offsets of later siblings. */
        int wire_fpos = (stored_wire) ? wire_base + 1 : -1;
        /* SPAWN phase (only on node change): instantiate one payload prefab
         * per field of the newly-selected node, mutate its first child to the
         * appropriate widget kind, seed from the stored wire.  Positions are
         * placeholder — real placement happens in the LAYOUT phase below,
         * which runs every frame so nested-cascade expansion in siblings can
         * push subsequent siblings further right without an extra dirty flag. */
        if (respawn) {
        for (int f = 0; f < n_fields; ++f) {
            void* raw = aiml_prefab_instantiate(ch.payload_spawn.c_str());
            if (!raw) break;
            auto* pi = (AimlPrefabInstance*)raw;
            pi->parent      = inst;
            pi->bound_index = f;
            pi->x = 0.0f;     /* placeholder — LAYOUT phase overwrites */
            pi->y = 0.0f;
            /* Mutate the payload prefab's first child to the right widget
             * kind for the field's type:
             *   • registered enum      → dropdown (options = enum labels)
             *   • registered cascade   → dropdown (options = node labels)
             *   • primitive / alias    → label showing "name: type"
             *                            (later: numeric text_input)
             * The payload prefab (`FieldChip`) is designed with a single
             * child so mutation is cheap and the shape stays predictable. */
            const char* flabel = aiml_prefab_cascade_field_label(ch.cascade_type.c_str(), node_id, f);
            const char* ftype  = aiml_prefab_cascade_field_type (ch.cascade_type.c_str(), node_id, f);
            if (!pi->children.empty()) {
                auto& w = pi->children[0];
                const char* enum_labels    = aiml_prefab_enum_labels(ftype);
                const char* cascade_labels = aiml_prefab_cascade_options(ftype);
                if (enum_labels && *enum_labels) {
                    w.type          = CHILD_DROPDOWN;
                    w.options       = enum_labels;
                    w.enum_type     = ftype;   /* → real member value in collect() */
                    w.selected_index = 0;
                    w.dropdown_open = false;
                    w.text.clear();
                    w.label_text.clear();
                } else if (cascade_labels && *cascade_labels) {
                    /* Nested cascade — propagate payload_spawn so the widget's
                     * OWN selection change spawns another payload row below.
                     * Recursion falls out of the same cascade_payload_sync
                     * pass on the next render frame.                          */
                    w.type          = CHILD_DROPDOWN;
                    w.options       = cascade_labels;
                    w.selected_index = 0;
                    w.dropdown_open = false;
                    w.cascade_type  = ftype;
                    w.payload_spawn = ch.payload_spawn;
                    w.last_node_id  = -1;      /* force fresh spawn on first frame */
                    w.spawned_payload.clear();
                    w.text.clear();
                    w.label_text.clear();
                } else {
                    /* Primitive or role-alias (VALUE / COUNT / SIDES etc.) —
                     * render a numeric text_input.  Defaults to "1" because
                     * "0" reads as "no effect" in most bytecodes (0 damage,
                     * 0 turns) which is almost never the desired starting
                     * value for a fresh field.                              */
                    w.type          = CHILD_TEXT_INPUT;
                    w.text          = "1";
                    w.label_text    = flabel;
                    w.options.clear();
                    w.selected_index = 0;
                    w.dropdown_open = false;
                    /* Numeric inputs (values, counts) rarely exceed 3 digits.
                     * Shrink the widget + instance width so the sibling
                     * layout can pack cost/duration chips tightly right after
                     * the numeric.  Kept at 40px = ~3 digits at font_size 10.  */
                    w.width      = 40.0f;
                    pi->width_override = 40.0f;
                }
            }
            /* Seed the widget from the stored wire.  `wire_fpos` is this
             * field's TRUE offset (computed with the recursive span walk, not
             * the old flat 1+f which drifted after any multi-slot field).  We
             * read the slot, seed the widget, then advance wire_fpos past this
             * field's full (recursive) span so the next sibling reads the right
             * slot.  For a NESTED cascade we also hand the payload instance a
             * seed slice (seed_wire + seed_off) so its own cascade_payload_sync
             * restores its sub-field values recursively — mirror of collect(). */
            int this_off = wire_fpos;   /* -1 when there is no stored wire */
            if (stored_wire && wire_fpos >= 0) {
                wire_fpos = casc_advance_value(ftype ? ftype : "",
                                               stored_wire, stored_size, wire_fpos);
            }
            if (!pi->children.empty() && stored_wire &&
                this_off >= 0 && this_off < stored_size) {
                auto& w2 = pi->children[0];
                int32_t sv = stored_wire[this_off];
                if (w2.type == CHILD_DROPDOWN || w2.type == CHILD_CASCADE) {
                    if (!w2.enum_type.empty()) {
                        /* Enum dropdown: the stored wire value is the REAL enum member
                         * value → map it to its option index (symmetric with collect()'s
                         * aiml_prefab_enum_value_at).  The old `sv - 1` assumed a
                         * contiguous 1-based layout and corrupted any enum whose value
                         * != index+1 (subset/non-0-based) — e.g. CardState on load. */
                        int ix = enum_index_of_value(w2.enum_type, (int)sv);
                        if (ix >= 0) w2.selected_index = ix;
                    } else if (!w2.cascade_type.empty()) {
                        /* Nested cascade: the stored slot is the sub-node id (NOT
                         * index+1 — node ids are opcode enum values that may be
                         * sparse/non-0-based, the same corruption class as enums).
                         * Map id → option index via the cascade registry, then
                         * propagate the wire slice so the nested sync restores
                         * ITS sub-fields on the next frame. */
                        auto cit = g_cascades.find(w2.cascade_type);
                        if (cit != g_cascades.end()) {
                            auto oit = cit->second.id_to_opt_idx.find((int)sv);
                            if (oit != cit->second.id_to_opt_idx.end())
                                w2.selected_index = oit->second;
                        }
                        pi->seed_wire = stored_wire;
                        pi->seed_len  = stored_size;
                        pi->seed_off  = this_off;   /* nested node tag sits here */
                    } else if (sv >= 1) {
                        /* Legacy cascade-node dropdown with no cascade_type. */
                        w2.selected_index = sv - 1;
                    }
                } else if (w2.type == CHILD_TEXT_INPUT) {
                    char buf[16]; snprintf(buf, sizeof(buf), "%d", (int)sv);
                    w2.text = buf;
                }
            }
            inst->child_instances.push_back(pi);
            ch.spawned_payload.push_back(pi);
        }
        /* One-shot: the seed slice has been consumed (and propagated to any
         * nested payload instances above).  Clear it so a later user-driven
         * respawn on this instance starts nested sub-nodes at their defaults
         * instead of re-applying the now-stale saved values. */
        inst->seed_wire = nullptr;
        inst->seed_len  = 0;
        inst->seed_off  = 0;
        }  /* end if (respawn) */

        /* LAYOUT phase — runs on respawn or when a nested cascade in one of
         * our spawned_payload widgets signalled needs_relayout.  Positions
         * each pi based on the ACTUAL rendered extent of the previous
         * sibling (own width plus recursive nested-payload expansion), so
         * a subsequent field never lands on top of a preceding field's
         * expanded payload.                                                */
        if (do_spawn_or_layout) {
            bool vertical = (ch.cascade_flow == "vertical");
            float gap = 4.0f;
            float pos_x = vertical ? ch.x                    : ch.x + ch.width + 6.0f;
            float pos_y = vertical ? ch.y + ch.height + gap  : ch.y;
            /* Local right/bottom edge in a pi's own coord space, walking
             * its widgets' spawned_payload chains recursively.             */
            std::function<float(AimlPrefabInstance*)> right_edge;
            right_edge = [&right_edge](AimlPrefabInstance* pi) -> float {
                float r = pi->width_override > 0.0f
                              ? pi->width_override
                              : (pi->def ? pi->def->width : 0.0f);
                for (auto& w : pi->children) {
                    for (auto* sp : w.spawned_payload) {
                        if (!sp || sp->pending_delete) continue;
                        float sr = sp->x + right_edge(sp);
                        if (sr > r) r = sr;
                    }
                }
                return r;
            };
            std::function<float(AimlPrefabInstance*)> bottom_edge;
            bottom_edge = [&bottom_edge](AimlPrefabInstance* pi) -> float {
                float b = pi->def ? pi->def->height : 0.0f;
                for (auto& w : pi->children) {
                    for (auto* sp : w.spawned_payload) {
                        if (!sp || sp->pending_delete) continue;
                        float sb = sp->y + bottom_edge(sp);
                        if (sb > b) b = sb;
                    }
                }
                return b;
            };
            /* spec/44_6 §5h: horizontal scrolling.  Clamping and snapping use
             * the values of the PREVIOUS frame (span/pitch are only fixed after
             * setting) — one frame of delay that nobody sees. */
            const float pay_start = pos_x;
            const float row_w     = inst->def ? inst->def->width : 0.0f;
            float viewport = row_w - pay_start;
            if (viewport < 0.0f) viewport = 0.0f;
            if (ch.payload_scroll && !vertical) {
                float max_off = ch.payload_span - viewport;
                if (max_off < 0.0f) max_off = 0.0f;
                if (ch.payload_scroll_x > max_off) ch.payload_scroll_x = max_off;
                if (ch.payload_scroll_x < 0.0f)    ch.payload_scroll_x = 0.0f;
                if (ch.payload_pitch > 0.0f) {
                    float snapped = roundf(ch.payload_scroll_x / ch.payload_pitch) * ch.payload_pitch;
                    if (snapped > max_off) snapped = max_off;
                    if (snapped < 0.0f)    snapped = 0.0f;
                    ch.payload_scroll_x = snapped;
                }
                pos_x -= ch.payload_scroll_x;
            }
            float pay_pitch = 0.0f;
            for (auto* pi : ch.spawned_payload) {
                if (!pi || pi->pending_delete) continue;
                pi->x = pos_x;
                pi->y = pos_y;
                if (vertical) pos_y += bottom_edge(pi) + gap;
                else {
                    float step = right_edge(pi) + gap;
                    if (step > pay_pitch) pay_pitch = step;
                    pos_x += step;
                }
            }
            if (ch.payload_scroll && !vertical) {
                ch.payload_start = pay_start;
                ch.payload_pitch = pay_pitch;
                ch.payload_span  = (pos_x + ch.payload_scroll_x) - pay_start;
                /* Windowing via VISIBILITY (as the scrollable container does with its
                 * rows): a payload that does not fit entirely into the zone is not
                 * drawn.  No scissor, no intervention in render_instance. */
                const float left  = pay_start;
                const float right = pay_start + viewport;
                for (auto* pi : ch.spawned_payload) {
                    if (!pi || pi->pending_delete) continue;
                    float pw = pi->width_override > 0.0f
                                 ? pi->width_override
                                 : (pi->def ? pi->def->width : 0.0f);
                    pi->visible = (pi->x >= left - 0.5f && pi->x + pw <= right + 0.5f);
                }
            } else {
                /* Without scrolling (or vertical) the visibility is no longer ours
                 * — otherwise a payload windowed once would stay invisible. */
                for (auto* pi : ch.spawned_payload)
                    if (pi && !pi->pending_delete) pi->visible = true;
            }
        }
        ch.needs_relayout = false;

        /* Propagate: our respawn (or re-layout) changed our own extent, so
         * our enclosing outer cascade widget (if any) must re-layout ITS
         * siblings.  Walk up: for each ancestor instance, find the child
         * widget whose spawned_payload contains us, mark it dirty.  Terminate
         * at the first ancestor with no such owning widget (top-level
         * container — its own siblings aren't cascade-driven).               */
        if (do_spawn_or_layout) {
            AimlPrefabInstance* child_inst = inst;
            AimlPrefabInstance* parent_inst = child_inst->parent;
            while (parent_inst) {
                bool found = false;
                for (auto& pw : parent_inst->children) {
                    for (auto* sp : pw.spawned_payload) {
                        if (sp == child_inst) { pw.needs_relayout = true; found = true; break; }
                    }
                    if (found) break;
                }
                if (!found) break;
                child_inst = parent_inst;
                parent_inst = child_inst->parent;
            }
        }
    }  /* end for (auto& ch : inst->children) — cascade widget handling loop */

    /* ── Persist each payload widget's value back into the bound row every
     *    frame.  Gathers values from the spawned sub-widgets, then hands
     *    them to the pure `aiml_cascade_persist_row` helper (unit-tested
     *    separately).  Sub-widget kind → slot value:
     *        enum-dropdown     → selected_index + 1
     *        cascade-dropdown  → node_id + RECURSIVE inline expansion of
     *                            its own payload widgets (so nested Value
     *                            / Duration / etc. cascades get flattened
     *                            into the wire the bytecode consumers
     *                            expect — e.g. draw_card [1,DeckType,
     *                            ValueType,Value] instead of the truncated
     *                            [1,DeckType,ValueTag]).
     *        text_input        → atoi(text) — VALUE / COUNT / SIDES.       */
    std::function<void(const PrefabChild&, std::vector<int32_t>&)> collect;
    collect = [&collect](const PrefabChild& w, std::vector<int32_t>& out) {
        if (w.type == CHILD_TEXT_INPUT) {
            out.push_back(w.text.empty() ? 0 : atoi(w.text.c_str()));
            return;
        }
        if (w.type != CHILD_DROPDOWN && w.type != CHILD_CASCADE) {
            out.push_back(0);
            return;
        }
        if (w.cascade_type.empty()) {
            /* plain enum — one slot.  Use the true member value from the enum
             * registry; fall back to selected_index+1 only for legacy widgets
             * that carry no enum_type (gap-free 1-based assumption). */
            if (!w.enum_type.empty()) {
                int32_t ev = aiml_prefab_enum_value_at(w.enum_type.c_str(), w.selected_index);
                out.push_back(ev >= 0 ? ev : w.selected_index + 1);
            } else {
                out.push_back(w.selected_index + 1);
            }
            return;
        }
        /* nested cascade — resolve node_id (not selected_index+1, which
         * only matches for gap-free option lists) and recurse into its
         * payload widgets so their values get inlined right after. */
        int32_t sub_node_id = aiml_prefab_cascade_node_id_at(
                                  w.cascade_type.c_str(), w.selected_index);
        out.push_back(sub_node_id < 0 ? 0 : sub_node_id);
        for (auto* pi : w.spawned_payload) {
            if (!pi || pi->children.empty()) { out.push_back(0); continue; }
            collect(pi->children[0], out);
        }
    };
    for (auto& ch : inst->children) {
        if (ch.type != CHILD_DROPDOWN && ch.type != CHILD_CASCADE) continue;
        if (ch.cascade_type.empty())                                continue;
        if (!inst->bound_list || inst->bound_index < 0)              continue;
        int32_t node_id = aiml_prefab_cascade_node_id_at(
                              ch.cascade_type.c_str(), ch.selected_index);
        if (node_id < 0) continue;
        std::vector<int32_t> vals;
        for (auto* pi : ch.spawned_payload) {
            if (!pi || pi->children.empty()) { vals.push_back(0); continue; }
            collect(pi->children[0], vals);
        }
        aiml_cascade_persist_row(inst->bound_list, inst->bound_index,
                                 node_id,
                                 vals.empty() ? nullptr : vals.data(),
                                 (int32_t)vals.size());
    }
}

static void render_instance(AimlPrefabInstance* inst) {
    if (!inst || !inst->visible) return;
    if (inst->pending_delete) return;
    /* Phase 6d: sync container children with bound list length. */
    container_sync(inst);
    /* Phase 6f-2: reactive cascade payload (destroy + respawn on node change). */
    cascade_payload_sync(inst);
    PrefabScene* s = g_scene;

    float ox, oy;
    get_actual_pos(inst, ox, oy);
    float sx = inst->scale_x;
    float sy = inst->scale_y;
    /* Sizes: apply the design→screen ratio ONCE.  For nested instances the
     * parent's g_render_scale already carries rel_scale, so we skip it here
     * to avoid the same double-scale that hit position math. */
    if (!inst->parent) {
        float rsx, rsy;
        get_rel_scale(rsx, rsy);
        sx *= rsx;
        sy *= rsy;
    }
    /* Inherit parent transform (nested instances). Identity at top level. */
    ox = g_render_off_x + ox * g_render_scale_x;
    oy = g_render_off_y + oy * g_render_scale_y;
    sx *= g_render_scale_x;
    sy *= g_render_scale_y;
    /* World origin/scale of THIS instance — used to transform nested children. */
    const float wox = ox, woy = oy, wsx = sx, wsy = sy;
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
            if (ch.bg_texture != 0) {
                /* Tileable panel background — UV wraps every tile_size
                 * logical pixels.  Multiply the wrap count by the world-
                 * scale to keep the tile at a constant screen size across
                 * design→screen resizes.  V-flipped UV to match sprite
                 * orientation (stb_image top-down rows vs. GL bottom-up). */
                float wraps_x = (ch.tile_size > 0.0f)
                                    ? (cw / (ch.tile_size * sx))
                                    : 1.0f;
                float wraps_y = (ch.tile_size > 0.0f)
                                    ? (ch_h / (ch.tile_size * sy))
                                    : 1.0f;
                DrawBatch tile;
                tile.tex  = ch.bg_texture;
                tile.mode = 1;
                /* Always white tint — the tile pattern brings its own
                 * darkness; multiplying by the panel's bg_color
                 * (typically already dark for a dark theme) crushed the
                 * texture to near-black on dark backgrounds and made it
                 * look invisible. */
                uint32_t col = color_with_alpha(0xFFFFFFu, alpha);
                push_quad(tile, cx, cy, cw, ch_h,
                          0.0f, wraps_y, wraps_x, 0.0f, col);
                rotate_batch_verts(tile, 0, rot_cx, rot_cy, rot);
                flush_batch(tile);
                /* Optional SDF border/rounded corners on top of the
                 * tile: transparent bg (already drawn) + solid border
                 * stroke.  Runs only if the child has a visible border. */
                if (ch.border_width > 0.0f && ch.border_color != 0) {
                    batch.tex  = s->white_tex;
                    batch.mode = 3;
                    batch.material = ch.material;
                    uint32_t fg = 0;   /* transparent — bg already painted */
                    uint32_t bd = rgba32_to_gl(ch.border_color, alpha);
                    push_quad_sdf(batch, cx, cy, cw, ch_h, fg, bd,
                                  ch.corner_radius * sx,
                                  ch.border_width * sx);
                }
            } else if (ch.texture != 0) {
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
        case CHILD_BAR: {
            /* Track (background) — rounded rect from bg_color/border. */
            DrawBatch track;
            track.tex  = s->white_tex;
            track.mode = 3;
            uint32_t tg = rgba32_to_gl(ch.bg_color, alpha);
            uint32_t bd = rgba32_to_gl(ch.border_color, alpha);
            push_quad_sdf(track, cx, cy, cw, ch_h, tg, bd,
                          ch.corner_radius * sx, ch.border_width * sx);
            rotate_batch_verts(track, 0, rot_cx, rot_cy, rot);
            flush_batch(track);

            /* Fill — fraction value/max of the width, same rounding. */
            float frac = (ch.bar_max > 1e-6f) ? (ch.bar_value / ch.bar_max) : 0.0f;
            if (frac < 0.0f) frac = 0.0f; if (frac > 1.0f) frac = 1.0f;
            if (frac > 0.0f && ch.fill_color != 0) {
                DrawBatch fill;
                fill.tex  = s->white_tex;
                fill.mode = 3;
                uint32_t fg = rgba32_to_gl(ch.fill_color, alpha);
                float fw = cw * frac;
                push_quad_sdf(fill, cx, cy, fw, ch_h, fg, 0,
                              ch.corner_radius * sx, 0.0f);
                rotate_batch_verts(fill, 0, rot_cx, rot_cy, rot);
                flush_batch(fill);
            }
            break;
        }
        case CHILD_SLIDER: {
            /* Slider: track + fill (range min..max → value) + handle knob.
             * Skin variants:
             *   • plain (default): SDF rounded rect track / fill / knob.
             *   • track_texture:   colored fill first, then the ornate frame
             *                      sprite stretched over the full rect.
             *   • handle_texture:  knob rendered as a stretched sprite. */
            float range = ch.bar_max - ch.slider_min;
            float frac  = (range > 1e-6f) ? (ch.bar_value - ch.slider_min) / range : 0.0f;
            if (frac < 0.0f) frac = 0.0f; if (frac > 1.0f) frac = 1.0f;

            /* ── Track background: two-phase when a track_texture is set:
             *    1) fill (colored bar showing value/max) BEHIND the frame
             *    2) frame sprite (ornate rounded panel) ON TOP
             * Otherwise fall back to the SDF track + separate fill. */
            if (ch.track_texture != 0) {
                /* Inset the visible fill so it stays inside the frame's
                 * inner opening — the artwork has ~6% padding on each side
                 * horizontally and ~30% vertically before the transparent
                 * hole starts. */
                float fx_pad = cw * 0.06f;
                float fy_pad = ch_h * 0.30f;
                float track_x = cx + fx_pad;
                float track_y = cy + fy_pad;
                float track_w = cw - fx_pad * 2.0f;
                float track_h = ch_h - fy_pad * 2.0f;
                if (frac > 0.0f && ch.fill_color != 0) {
                    DrawBatch fill;
                    fill.tex  = s->white_tex;
                    fill.mode = 3;
                    uint32_t fg = rgba32_to_gl(ch.fill_color, alpha);
                    float fw = track_w * frac;
                    push_quad_sdf(fill, track_x, track_y, fw, track_h, fg, 0,
                                  ch.corner_radius * sx, 0.0f);
                    rotate_batch_verts(fill, 0, rot_cx, rot_cy, rot);
                    flush_batch(fill);
                }
                /* Ornate frame — sprite, v-flipped UVs (stb_image top-down). */
                DrawBatch frame;
                frame.tex  = ch.track_texture;
                frame.mode = 1;
                uint32_t tint = color_with_alpha(0xFFFFFFu, alpha);
                push_quad(frame, cx, cy, cw, ch_h, 0, 1, 1, 0, tint);
                rotate_batch_verts(frame, 0, rot_cx, rot_cy, rot);
                flush_batch(frame);
            } else {
                DrawBatch track;
                track.tex  = s->white_tex;
                track.mode = 3;
                uint32_t tg = rgba32_to_gl(ch.bg_color, alpha);
                uint32_t bd = rgba32_to_gl(ch.border_color, alpha);
                push_quad_sdf(track, cx, cy, cw, ch_h, tg, bd,
                              ch.corner_radius * sx, ch.border_width * sx);
                rotate_batch_verts(track, 0, rot_cx, rot_cy, rot);
                flush_batch(track);

                if (frac > 0.0f && ch.fill_color != 0) {
                    DrawBatch fill;
                    fill.tex  = s->white_tex;
                    fill.mode = 3;
                    uint32_t fg = rgba32_to_gl(ch.fill_color, alpha);
                    float fw = cw * frac;
                    push_quad_sdf(fill, cx, cy, fw, ch_h, fg, 0,
                                  ch.corner_radius * sx, 0.0f);
                    rotate_batch_verts(fill, 0, rot_cx, rot_cy, rot);
                    flush_batch(fill);
                }
            }

            /* ── Handle: sprite when handle_texture is set, else SDF knob.
             * When a track_texture is used, constrain the knob's travel to
             * the frame's inner opening so at value=min the knob sits at
             * the LEFT diamond marker and at value=max at the RIGHT one,
             * instead of half-hanging outside the ornate frame. */
            float knob_h = ch_h * 1.6f;
            float knob_w = ch_h * (ch.handle_texture ? 1.4f : 0.9f);
            float travel_x0 = cx;
            float travel_w  = cw;
            if (ch.track_texture != 0) {
                float fx_pad = cw * 0.06f;
                travel_x0 = cx + fx_pad;
                travel_w  = cw - fx_pad * 2.0f;
            }
            float knob_x = travel_x0 + travel_w * frac - knob_w * 0.5f;
            float knob_y = cy + (ch_h - knob_h) * 0.5f;
            if (ch.handle_texture != 0) {
                /* Metal grip — neutral tint, keeps its natural silver. */
                DrawBatch knob;
                knob.tex  = ch.handle_texture;
                knob.mode = 1;
                uint32_t tint = color_with_alpha(0xFFFFFFu, alpha);
                push_quad(knob, knob_x, knob_y, knob_w, knob_h, 0, 1, 1, 0, tint);
                rotate_batch_verts(knob, 0, rot_cx, rot_cy, rot);
                flush_batch(knob);

                /* Gem overlay — tinted with fill_color, brightens (and
                 * grows slightly) during drag/hover to simulate a glow. */
                if (ch.handle_gem_texture != 0) {
                    uint32_t gem_rgb =
                        ch.fill_color ? (ch.fill_color >> 8) : 0xFFFFFFu;
                    bool active = (ch.slider_dragging || ch.hot);
                    if (active) {
                        /* Mix 45 % toward white so the gem visibly lights
                         * up but keeps its color identity. */
                        uint32_t r = (gem_rgb >> 16) & 0xFF;
                        uint32_t g = (gem_rgb >>  8) & 0xFF;
                        uint32_t b = (gem_rgb      ) & 0xFF;
                        r = r + (255 - r) * 45 / 100;
                        g = g + (255 - g) * 45 / 100;
                        b = b + (255 - b) * 45 / 100;
                        gem_rgb = (r << 16) | (g << 8) | b;
                    }
                    uint32_t gem_tint = color_with_alpha(gem_rgb, alpha);
                    DrawBatch gem;
                    gem.tex  = ch.handle_gem_texture;
                    gem.mode = 1;
                    /* Grow the gem quad ~8 % when active — cheap bloom
                     * that reads as the gem "swelling" under the cursor. */
                    float grow = active ? 1.08f : 1.0f;
                    float gw = knob_w * grow;
                    float gh = knob_h * grow;
                    float gx = knob_x + (knob_w - gw) * 0.5f;
                    float gy = knob_y + (knob_h - gh) * 0.5f;
                    push_quad(gem, gx, gy, gw, gh, 0, 1, 1, 0, gem_tint);
                    rotate_batch_verts(gem, 0, rot_cx, rot_cy, rot);
                    flush_batch(gem);
                }
            } else {
                uint32_t knob_rgba = ch.slider_dragging && ch.active_color ? ch.active_color
                                  : ch.hot              && ch.hover_color  ? ch.hover_color
                                  : (ch.fill_color ? ch.fill_color : 0xFFFFFFFF);
                uint32_t kc = rgba32_to_gl(knob_rgba, alpha);
                uint32_t kb = rgba32_to_gl(ch.border_color ? ch.border_color : 0x00000080, alpha);
                DrawBatch knob;
                knob.tex  = s->white_tex;
                knob.mode = 3;
                push_quad_sdf(knob, knob_x, knob_y, knob_w, knob_h, kc, kb,
                              (knob_w * 0.5f) * sx, 1.5f * sx);
                rotate_batch_verts(knob, 0, rot_cx, rot_cy, rot);
                flush_batch(knob);
            }
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
                 * stb_image returns top-down rows while GL expects bottom-up.
                 * `flip_h` mirrors horizontally so a single "left arrow" asset
                 * can serve as both prev/next by only setting the flag. */
                float bu0 = ch.flip_h ? 1.0f : 0.0f;
                float bu1 = ch.flip_h ? 0.0f : 1.0f;
                push_quad(batch, cx, cy, cw, ch_h, bu0, 1, bu1, 0, tint);
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
            } else if (ch.bg_texture != 0) {
                /* Text-button with tileable stone bg (same field as rect):
                 * pattern repeats every tile_size logical pixels so a
                 * short button doesn't stretch the stone into blur.  An
                 * SDF border on top gives a clean rounded frame around
                 * the tile.  Hover/pressed tint the whole thing.
                 *
                 * If a frame_border_texture is also set, inset the bg by
                 * frame_border_size on every side so the stone doesn't
                 * peek past the rounded corners of the border tiling. */
                float bg_inset = (ch.frame_border_texture != 0)
                                     ? ch.frame_border_size * sx
                                     : 0.0f;
                float bg_x = cx + bg_inset;
                float bg_y = cy + bg_inset;
                float bg_w = cw   - 2.0f * bg_inset;
                float bg_h = ch_h - 2.0f * bg_inset;
                if (bg_w < 1.0f) bg_w = 1.0f;
                if (bg_h < 1.0f) bg_h = 1.0f;
                float wraps_x = (ch.tile_size > 0.0f)
                                    ? (bg_w / (ch.tile_size * sx))
                                    : 1.0f;
                float wraps_y = (ch.tile_size > 0.0f)
                                    ? (bg_h / (ch.tile_size * sy))
                                    : 1.0f;
                DrawBatch tile;
                tile.tex  = ch.bg_texture;
                tile.mode = 1;
                uint32_t tint_rgb =
                    ch.pressed_now && ch.active_color ? (ch.active_color >> 8)
                  : ch.hot         && ch.hover_color  ? (ch.hover_color  >> 8)
                                                       : 0xFFFFFFu;
                uint32_t tint = color_with_alpha(tint_rgb, alpha);
                push_quad(tile, bg_x, bg_y, bg_w, bg_h,
                          0.0f, wraps_y, wraps_x, 0.0f, tint);
                rotate_batch_verts(tile, 0, rot_cx, rot_cy, rot);
                flush_batch(tile);
                if (ch.border_width > 0.0f && ch.border_color != 0) {
                    DrawBatch fr;
                    fr.tex  = s->white_tex;
                    fr.mode = 3;
                    uint32_t bd = rgba32_to_gl(ch.border_color, alpha);
                    push_quad_sdf(fr, cx, cy, cw, ch_h, 0, bd,
                                  ch.corner_radius * sx,
                                  ch.border_width * sx);
                    rotate_batch_verts(fr, 0, rot_cx, rot_cy, rot);
                    flush_batch(fr);
                }
            } else if (ch.track_texture != 0) {
                /* Text-button with ornate frame — same slot as slider skins.
                 * The stretched frame carries the visual weight; hover /
                 * pressed states show as a tint on the sprite (fill_color
                 * fallback: warm gold if unset). */
                DrawBatch frame;
                frame.tex  = ch.track_texture;
                frame.mode = 1;
                uint32_t tint_rgb =
                    ch.pressed_now && ch.active_color ? (ch.active_color >> 8)
                  : ch.hot         && ch.hover_color  ? (ch.hover_color  >> 8)
                                                       : 0xFFFFFFu;
                uint32_t tint = color_with_alpha(tint_rgb, alpha);
                push_quad(frame, cx, cy, cw, ch_h, 0, 1, 1, 0, tint);
                rotate_batch_verts(frame, 0, rot_cx, rot_cy, rot);
                flush_batch(frame);
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
            /* Tileable frame border — one shared mini-tile placed around
             * the perimeter as discrete rotated instances, so the ornament
             * orientation follows the edge direction.  Corners get a
             * quarter-circle arc of tiles rotating from the "top-edge
             * orientation" to the next edge, so the ornament turns
             * smoothly instead of butting against a hard 90° join. */
            if (ch.frame_border_texture != 0 && ch.frame_border_size > 0.0f) {
                float fbs = ch.frame_border_size * sx;
                /* Corner radius = fbs by default → produces one full tile
                 * of arc, enough for a visible curved turn without eating
                 * too much of a short button.  Straight edges start
                 * inside the arc's tangent point. */
                float R  = fbs;
                float pi_f = (float)M_PI;
                uint32_t tint = color_with_alpha(0xFFFFFFu, alpha);
                DrawBatch b;
                b.tex  = ch.frame_border_texture;
                b.mode = 1;
                /* Helper (lambda): push one square tile centered at
                 * (px, py), rotated by `deg` degrees around its center. */
                auto tile = [&](float px, float py, float deg) {
                    size_t v0 = b.verts.size();
                    push_quad(b, px - fbs * 0.5f, py - fbs * 0.5f,
                              fbs, fbs, 0.0f, 1.0f, 1.0f, 0.0f, tint);
                    if (deg != 0.0f) rotate_batch_verts(b, v0, px, py, deg);
                };
                /* Straight-edge tile counts (excluding the corner arcs).
                 * ceil, not floor — that guarantees step_x/step_y ≤ fbs
                 * and the tiles overlap slightly instead of leaving gaps
                 * when the edge length does not divide evenly (e.g.
                 * straight_x=88 / fbs=6 → step=6.286 with floor → 0.29 px
                 * gap per edge; with ceil step=5.87 → 0.13 px overlap). */
                float straight_x = cw   - 2.0f * R;
                float straight_y = ch_h - 2.0f * R;
                int nx = straight_x > 0.0f ? (int)ceilf(straight_x / fbs) : 0;
                int ny = straight_y > 0.0f ? (int)ceilf(straight_y / fbs) : 0;
                if (nx < 1) nx = 1;
                if (ny < 1) ny = 1;
                float step_x = nx > 0 ? straight_x / (float)nx : fbs;
                float step_y = ny > 0 ? straight_y / (float)ny : fbs;
                /* Top edge — natural orientation. */
                for (int i = 0; i < nx; ++i) {
                    float tx = cx + R + (i + 0.5f) * step_x;
                    tile(tx, cy + fbs * 0.5f, 0.0f);
                }
                /* Bottom edge — rotated 180° so ornament faces outward. */
                for (int i = 0; i < nx; ++i) {
                    float tx = cx + R + (i + 0.5f) * step_x;
                    tile(tx, cy + ch_h - fbs * 0.5f, 180.0f);
                }
                /* Left edge — rotated 270° (-90°): ornament runs down the side. */
                for (int i = 0; i < ny; ++i) {
                    float ty = cy + R + (i + 0.5f) * step_y;
                    tile(cx + fbs * 0.5f, ty, 270.0f);
                }
                /* Right edge — rotated 90°. */
                for (int i = 0; i < ny; ++i) {
                    float ty = cy + R + (i + 0.5f) * step_y;
                    tile(cx + cw - fbs * 0.5f, ty, 90.0f);
                }
                /* Four corner arcs — N tiles each, rotating 90°/N per step.
                 * Tile CENTERS follow an arc of radius (R - fbs/2), not R,
                 * so they stay on the same fbs/2-inset locus as the
                 * straight-edge tile centers.  Using R itself put the
                 * centers on the button's outer corner, making half a
                 * tile stick out (see user screenshot). */
                float arc_r   = R - fbs * 0.5f;
                if (arc_r < 0.0f) arc_r = 0.0f;
                int   corner_n = (int)ceilf((pi_f * 0.5f * (arc_r + fbs * 0.5f)) / fbs);
                if (corner_n < 3) corner_n = 3;
                struct Corner { float ax, ay, base_theta, base_deg; };
                /* Order: top-left, top-right, bottom-right, bottom-left.
                 * base_theta = angle of the FIRST tile from arc center;
                 * base_deg   = tile rotation at that position (the tile at
                 * top-left starts as a "left-edge" tile, ends as a "top-edge"
                 * tile, so it rotates from 270° down to 0°). */
                Corner corners[4] = {
                    { cx + R,          cy + R,          pi_f,           270.0f },
                    { cx + cw - R,     cy + R,          -pi_f * 0.5f,     0.0f },
                    { cx + cw - R,     cy + ch_h - R,   0.0f,            90.0f },
                    { cx + R,          cy + ch_h - R,   pi_f * 0.5f,    180.0f },
                };
                for (const auto& c : corners) {
                    for (int k = 0; k < corner_n; ++k) {
                        float t   = (k + 0.5f) / (float)corner_n;   /* 0..1 */
                        float th  = c.base_theta + t * (pi_f * 0.5f);
                        float px  = c.ax + arc_r * cosf(th);
                        float py  = c.ay + arc_r * sinf(th);
                        float deg = c.base_deg + t * 90.0f;
                        tile(px, py, deg);
                    }
                }
                rotate_batch_verts(b, 0, rot_cx, rot_cy, rot);
                flush_batch(b);
            }
            /* Caption centered (skipped for icon-only buttons with empty text).
             * Reads ch.text so the AIML side can re-label a button via set_text. */
            if (!ch.text.empty() && s->font) {
                float target_size = ch.font_size > 0.0f ? ch.font_size * sx : 0.0f;
                AimlUIFont* lf = (target_size > 0.0f) ? font_for_size_named(s, ch.font_name, target_size) : s->font;
                if (lf) {
                    DrawBatch batch;
                    batch.tex  = lf->atlas_tex;
                    batch.mode = 2;
                    uint32_t col = rgba32_to_gl(ch.color ? ch.color : 0xFFFFFFFF, alpha);
                    float text_cx = cx + cw * 0.5f;
                    float text_cy = cy + (ch_h - lf->line_height) * 0.5f;
                    /* Optional outline halo — same 8-tap ring used for
                     * CHILD_LABEL, ported so text buttons can get a coloured
                     * glow (e.g. light blue) around the caption.  Only runs
                     * if outline_px > 0 AND outline_color has an alpha. */
                    if (ch.outline_px > 0.0f && (ch.outline_color & 0xFFu)) {
                        uint32_t ocol = rgba32_to_gl(ch.outline_color, alpha);
                        float d = ch.outline_px * sx;
                        const float offs[8][2] = {
                            { -d, -d }, {  0, -d }, {  d, -d },
                            { -d,  0 },              {  d,  0 },
                            { -d,  d }, {  0,  d }, {  d,  d },
                        };
                        for (int oi = 0; oi < 8; ++oi) {
                            push_text_batch(batch, lf, ch.text.c_str(),
                                            text_cx + offs[oi][0],
                                            text_cy + offs[oi][1],
                                            cw, ocol, "center");
                        }
                    }
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
            /* Displayed text: hide chars when secret.  Single line with
             * horizontal caret-follow scrolling — long text stays visible and
             * is clipped (GL scissor) to the field's inner box. */
            if (s->font) {
                std::string display = ch.is_secret
                                        ? std::string(ch.text.size(), '*')
                                        : ch.text;
                float target_size = ch.font_size > 0.0f ? ch.font_size * sx : 0.0f;
                AimlUIFont* lf = (target_size > 0.0f) ? font_for_size_named(s, ch.font_name, target_size) : s->font;
                if (lf) {
                    float padding    = 8.0f * sx;
                    float inner_w    = cw - 2.0f * padding;
                    float text_start = cx + padding;
                    float line_h     = lf->line_height;
                    int32_t dlen = (int32_t)display.size();
                    size_t cpos0 = (size_t)ch.cursor_pos <= display.size() ? (size_t)ch.cursor_pos : display.size();
                    uint32_t col = rgba32_to_gl(ch.color ? ch.color : 0xFFFFFFFF, alpha);
                    /* A field taller than ~1.8 lines is MULTI-LINE (wrap + vertical
                     * caret-follow scroll); otherwise SINGLE-LINE (horizontal
                     * caret-follow scroll).  Both clip to the inner box. */
                    bool multiline = ch_h >= 1.8f * line_h;
                    ch.ti_multiline = multiline;   /* vom Eingabe-Pass gelesen */

                    GLboolean prev_sc = glIsEnabled(GL_SCISSOR_TEST);
                    GLint prev_box[4] = {0,0,0,0};
                    glGetIntegerv(GL_SCISSOR_BOX, prev_box);
                    int sxp = (int)ceilf(text_start);
                    int syp = (int)ceilf((float)g_scene->screen_h - (cy + ch_h));
                    int swp = (int)floorf(inner_w);
                    int shp = (int)floorf(ch_h);
                    if (swp < 0) swp = 0;
                    if (shp < 0) shp = 0;
                    glEnable(GL_SCISSOR_TEST);
                    glScissor(sxp, syp, swp, shp);

                    if (!multiline) {
                        float caret_off = text_width_font(lf, display.substr(0, cpos0).c_str());
                        if (!ch.focused) {
                            ch.text_scroll_x = 0.0f;
                        } else {
                            if (caret_off - ch.text_scroll_x > inner_w) ch.text_scroll_x = caret_off - inner_w;
                            if (caret_off - ch.text_scroll_x < 0.0f)    ch.text_scroll_x = caret_off;
                            if (ch.text_scroll_x < 0.0f)                ch.text_scroll_x = 0.0f;
                        }
                        float tsx = ch.text_scroll_x;
                        ch.caret_byte.clear();
                        ch.caret_x.clear();
                        ch.caret_byte.push_back(0);
                        ch.caret_x.push_back(text_start - tsx);
                        int32_t bpos = 0;
                        while (bpos < (int32_t)ch.text.size()) {
                            bpos = utf8_next(ch.text, bpos);
                            int32_t db = bpos < dlen ? bpos : dlen;
                            float w = text_width_font(lf, display.substr(0, (size_t)db).c_str());
                            ch.caret_byte.push_back(bpos);
                            ch.caret_x.push_back(text_start + w - tsx);
                        }
                        if (ch.focused && ch.sel_anchor != ch.cursor_pos) {
                            int32_t lo = ch.sel_anchor < ch.cursor_pos ? ch.sel_anchor : ch.cursor_pos;
                            int32_t hi = ch.sel_anchor < ch.cursor_pos ? ch.cursor_pos : ch.sel_anchor;
                            if (lo < 0) lo = 0;
                            if (hi > dlen) hi = dlen;
                            if (lo > dlen) lo = dlen;
                            float xlo = text_start - tsx + text_width_font(lf, display.substr(0, (size_t)lo).c_str());
                            float xhi = text_start - tsx + text_width_font(lf, display.substr(0, (size_t)hi).c_str());
                            float sel_h = line_h;
                            float hy = cy + (ch_h - sel_h) * 0.5f;
                            DrawBatch hb; hb.tex = s->white_tex; hb.mode = 0;
                            uint32_t hc = rgba32_to_gl(0x3A5FA0FF, alpha);
                            push_quad(hb, xlo, hy, xhi - xlo, sel_h, 0,0,1,1, hc);
                            rotate_batch_verts(hb, 0, rot_cx, rot_cy, rot);
                            flush_batch(hb);
                        }
                        if (!display.empty()) {
                            DrawBatch batch; batch.tex = lf->atlas_tex; batch.mode = 2;
                            float text_cy = cy + (ch_h - line_h) * 0.5f;
                            push_text_batch(batch, lf, display.c_str(),
                                            text_start - tsx, text_cy, 1.0e9f, col, "left");
                            rotate_batch_verts(batch, 0, rot_cx, rot_cy, rot);
                            flush_batch(batch);
                        }
                        if (ch.focused) {
                            float bx = text_start - tsx + caret_off;
                            float by = cy + (ch_h - line_h) * 0.5f;
                            DrawBatch cbatch; cbatch.tex = s->white_tex; cbatch.mode = 0;
                            push_quad(cbatch, bx, by, 1.5f * sx, line_h, 0,0,1,1, col);
                            rotate_batch_verts(cbatch, 0, rot_cx, rot_cy, rot);
                            flush_batch(cbatch);
                        }
                    } else {
                        /* Multi-line: wrap at inner_w, top-aligned, vertical scroll
                         * follows the caret line.  The whole wrapped text is drawn
                         * shifted up by scroll_line*line_h; the scissor windows it. */
                        float vpad = 4.0f * sx;
                        int visible_lines = (int)floorf((ch_h - 2.0f * vpad) / line_h);
                        if (visible_lines < 1) visible_lines = 1;
                        /* ONE wrap computation for everything: drawing, cursor and
                         * click map.  Previously the PREFIX alone was wrapped
                         * for the cursor (`display.substr(0, cpos0)`) — a
                         * prefix wraps differently from the same text with the rest
                         * behind it, so the cursor stood on a different line
                         * and column from the character it belongs to.  Together
                         * with the missing gutter width (below) that produced the
                         * cursor "hopping wildly back and forth". */
                        std::vector<std::pair<int32_t,int32_t>> cspans;
                        std::vector<std::string> clines =
                            wrap_text_lines(lf, display.c_str(), ch.wrap ? inner_w : 0.0f, &cspans);
                        /* What the key pass needs to do the same computation
                         * (spec/44_6 §5f): font, width, window height. */
                        ch.tb_font    = lf;
                        ch.tb_inner_w = inner_w;
                        ch.tb_visible = visible_lines;
                        ch.tb_total   = (int32_t)clines.size();
                        /* Width of the number column.  It depends on the LINE COUNT
                         * ("9 " vs "16 ") and therefore changes while typing —
                         * whoever leaves it out at the cursor makes it jump. */
                        float cgut = 0.0f;
                        if (ch.gutter) {
                            int src_total = 1;
                            for (char dc : display) if (dc == '\n') ++src_total;
                            char wbuf[16];
                            snprintf(wbuf, sizeof(wbuf), "%d ", src_total);
                            cgut = text_width_font(lf, wbuf) + 8.0f * sx;
                        }
                        /* Cursor stops per line, RELATIVE to the start of the text — one
                         * source for the cursor position and the click mapping. */
                        std::vector<float>   all_cx;
                        std::vector<int32_t> all_cb;
                        std::vector<int32_t> all_rows;
                        const bool map_ok = (display == ch.text);
                        if (map_ok) {
                            for (size_t li = 0; li < clines.size(); ++li) {
                                all_rows.push_back((int32_t)all_cx.size());
                                int32_t sb = (li < cspans.size()) ? cspans[li].first  : 0;
                                int32_t se = (li < cspans.size()) ? cspans[li].second : 0;
                                tb_build_caret_stops(lf, ch.text, sb, se, clines[li],
                                                     0.0f, all_cx, all_cb);
                            }
                            all_rows.push_back((int32_t)all_cx.size());
                        }
                        int   caret_line = 0;
                        float caret_lx   = 0.0f;
                        if (map_ok && !all_cx.empty()) {
                            size_t best = 0;
                            for (size_t k = 0; k < all_cb.size(); ++k)
                                if (all_cb[k] <= cpos0) best = k;
                            caret_lx = all_cx[best];
                            for (size_t r = 0; r + 1 < all_rows.size(); ++r)
                                if ((int32_t)best >= all_rows[r]) caret_line = (int)r;
                        } else {
                            /* secret mode: the drawn dots say nothing
                             * about the bytes — then the old approximation. */
                            std::vector<std::string> pre_lines =
                                wrap_text_lines(lf, display.substr(0, cpos0).c_str(), inner_w);
                            caret_line = pre_lines.empty() ? 0 : (int)pre_lines.size() - 1;
                            caret_lx   = pre_lines.empty() ? 0.0f
                                         : text_width_font(lf, pre_lines.back().c_str());
                        }
                        int scroll_line = (int)ch.text_scroll_y;
                        /* Follow the cursor if it has MOVED (typing,
                         * arrow keys) — otherwise stay where the reader
                         * scrolled to.  Previously every frame pulled back to the
                         * cursor and an unfocused field
                         * jumped to line 0: both made the scrollbar
                         * ineffective (reported 2026-10-22). */
                        if (ch.cursor_pos != ch.ti_caret_seen) {
                            ch.ti_caret_seen = ch.cursor_pos;
                            ch.ti_free_scroll = false;
                        }
                        if (!ch.ti_free_scroll && ch.focused) {
                            if (caret_line < scroll_line) scroll_line = caret_line;
                            if (caret_line >= scroll_line + visible_lines) scroll_line = caret_line - visible_lines + 1;
                        }
                        int max_line = (int)clines.size() - visible_lines;
                        if (max_line < 0) max_line = 0;
                        if (scroll_line > max_line) scroll_line = max_line;
                        if (scroll_line < 0) scroll_line = 0;
                        ch.text_scroll_y = (float)scroll_line;
                        /* spec/44_6: click positioning in the MULTI-LINE field.
                         * Until 2026-10-15 there was exactly ONE stop here (byte 0): every
                         * click jumped to the start of the document.  Now the visible
                         * window of the same map the cursor comes from —
                         * with the gutter width in the starting point. */
                        ch.caret_byte.clear();
                        ch.caret_x.clear();
                        ch.caret_byte.push_back(0);
                        ch.caret_x.push_back(text_start);
                        ch.tb_caret_x.clear();
                        ch.tb_caret_byte.clear();
                        ch.tb_row_start.clear();
                        ch.tb_row_y.clear();
                        ch.tb_row_h = line_h;
                        if (map_ok && !all_rows.empty()) {
                            float crow_y = cy + vpad;
                            for (int li = scroll_line;
                                 li < scroll_line + visible_lines && li + 1 < (int)all_rows.size(); ++li) {
                                ch.tb_row_start.push_back((int32_t)ch.tb_caret_x.size());
                                ch.tb_row_y.push_back(crow_y);
                                for (int32_t k = all_rows[(size_t)li]; k < all_rows[(size_t)li + 1]; ++k) {
                                    ch.tb_caret_x.push_back(text_start + cgut + all_cx[(size_t)k]);
                                    ch.tb_caret_byte.push_back(all_cb[(size_t)k]);
                                }
                                crow_y += line_h;
                            }
                            ch.tb_row_start.push_back((int32_t)ch.tb_caret_x.size());
                        }
                        if (!display.empty()) {
                            float top_y = cy + vpad - (float)scroll_line * line_h;
                            if (!ch.gutter && ch.color_runs.empty()) {
                                DrawBatch batch; batch.tex = lf->atlas_tex; batch.mode = 2;
                                push_text_batch(batch, lf, display.c_str(),
                                                text_start, top_y, inner_w, col, "left");
                                rotate_batch_verts(batch, 0, rot_cx, rot_cy, rot);
                                flush_batch(batch);
                            } else {
                                /* spec/44_6 §5g: gutter and/or colour runs — line
                                 * by line, because both apply per line or per
                                 * byte section.  Because wrap_text_lines keeps
                                 * whitespace literally, a rendered
                                 * line is a clean excerpt of the source — the
                                 * mapping run→line is thus a pure
                                 * intersection, without a second way of counting. */
                                /* THE SAME wrap computation and gutter width as
                                 * above (cursor + click map).  There used to be a
                                 * second copy here — and exactly such copies drifted
                                 * apart as soon as one of them had to take
                                 * a little more into account. */
                                const std::vector<std::pair<int32_t,int32_t>>& spans = cspans;
                                const std::vector<std::string>& dl = clines;
                                const float gut_w = cgut;
                                float tx0 = text_start + gut_w;
                                int src_line = 1;
                                for (size_t li = 0; li < dl.size(); ++li) {
                                    float ly = top_y + (float)li * line_h;
                                    int32_t sb = spans[li].first;
                                    /* Numbers count SOURCE lines: a wrapped
                                     * line keeps its number (spec/44_6 §5g). */
                                    bool starts_src = (sb == 0) ||
                                        (sb > 0 && (size_t)sb <= display.size() &&
                                         display[(size_t)sb - 1] == '\n');
                                    if (starts_src && li > 0) ++src_line;
                                    if (ch.gutter && starts_src) {
                                        char nbuf[16];
                                        snprintf(nbuf, sizeof(nbuf), "%d", src_line);
                                        float nw = text_width_font(lf, nbuf);
                                        DrawBatch gb; gb.tex = lf->atlas_tex; gb.mode = 2;
                                        push_text_batch(gb, lf, nbuf,
                                                        text_start + gut_w - 8.0f * sx - nw,
                                                        ly, 1.0e9f, rgba32_to_gl(0x667788FF, alpha), "left");
                                        rotate_batch_verts(gb, 0, rot_cx, rot_cy, rot);
                                        flush_batch(gb);
                                    }
                                    const std::string& ln = dl[li];
                                    if (ln.empty()) continue;
                                    if (ch.color_runs.empty()) {
                                        DrawBatch tb; tb.tex = lf->atlas_tex; tb.mode = 2;
                                        push_text_batch(tb, lf, ln.c_str(), tx0, ly, 1.0e9f, col, "left");
                                        rotate_batch_verts(tb, 0, rot_cx, rot_cy, rot);
                                        flush_batch(tb);
                                        continue;
                                    }
                                    /* Determine the colour per byte (later runs
                                     * win), then draw same-coloured neighbours in one
                                     * go. */
                                    size_t n = ln.size();
                                    std::vector<uint32_t> cols(n, col);
                                    for (size_t r = 0; r + 2 < ch.color_runs.size(); r += 3) {
                                        int32_t rs = ch.color_runs[r], rl = ch.color_runs[r+1];
                                        uint32_t rc = (uint32_t)ch.color_runs[r+2];
                                        int32_t a = rs > sb ? rs : sb;
                                        int32_t b = (rs + rl) < (sb + (int32_t)n) ? (rs + rl) : (sb + (int32_t)n);
                                        for (int32_t q = a; q < b; ++q)
                                            cols[(size_t)(q - sb)] = rgba32_to_gl(rc, alpha);
                                    }
                                    size_t seg = 0;
                                    float px = tx0;
                                    while (seg < n) {
                                        size_t e = seg + 1;
                                        while (e < n && cols[e] == cols[seg]) ++e;
                                        std::string part = ln.substr(seg, e - seg);
                                        DrawBatch tb; tb.tex = lf->atlas_tex; tb.mode = 2;
                                        push_text_batch(tb, lf, part.c_str(), px, ly, 1.0e9f, cols[seg], "left");
                                        rotate_batch_verts(tb, 0, rot_cx, rot_cy, rot);
                                        flush_batch(tb);
                                        px += text_width_font(lf, part.c_str());
                                        seg = e;
                                    }
                                }
                            }
                        }
                        if (ch.focused) {
                            /* WITH the number column: the text is drawn shifted right by `cgut`,
                             * a cursor without this
                             * offset would stand next to the character it means. */
                            float bx = text_start + cgut + caret_lx;
                            float by = cy + vpad + (float)(caret_line - scroll_line) * line_h;
                            DrawBatch cbatch; cbatch.tex = s->white_tex; cbatch.mode = 0;
                            push_quad(cbatch, bx, by, 1.5f * sx, line_h, 0,0,1,1, col);
                            rotate_batch_verts(cbatch, 0, rot_cx, rot_cy, rot);
                            flush_batch(cbatch);
                        }
                    }

                    if (prev_sc) glScissor(prev_box[0], prev_box[1], prev_box[2], prev_box[3]);
                    else         glDisable(GL_SCISSOR_TEST);

                    /* Scrollbar here too (spec/44_6 §5f): a multi-line
                     * field without a bar hides that it continues — and
                     * one does not see WHERE in the text one is.  Vertical always,
                     * horizontal only without wrapping. */
                    if (multiline && ch.tb_total > ch.tb_visible && ch.tb_visible > 0) {
                        float sb_w    = 6.0f * sx;
                        float sb_x    = cx + cw - sb_w - 2.0f * sx;
                        float track_y = cy + 2.0f * sy;
                        float track_h = ch_h - 4.0f * sy;
                        { DrawBatch tk; tk.tex = s->white_tex; tk.mode = 0;
                          push_quad(tk, sb_x, track_y, sb_w, track_h, 0,0,1,1,
                                    rgba32_to_gl(0x101828A0, alpha));
                          rotate_batch_verts(tk, 0, rot_cx, rot_cy, rot); flush_batch(tk); }
                        int max_l = ch.tb_total - ch.tb_visible;
                        float thumb_h = track_h * (float)ch.tb_visible / (float)ch.tb_total;
                        if (thumb_h < 16.0f * sy) thumb_h = 16.0f * sy;
                        float frac = max_l > 0 ? ch.text_scroll_y / (float)max_l : 0.0f;
                        if (frac < 0.0f) frac = 0.0f;
                        if (frac > 1.0f) frac = 1.0f;
                        DrawBatch th; th.tex = s->white_tex; th.mode = 0;
                        push_quad(th, sb_x, track_y + (track_h - thumb_h) * frac, sb_w, thumb_h,
                                  0,0,1,1, rgba32_to_gl(0x3A5578F0, alpha));
                        rotate_batch_verts(th, 0, rot_cx, rot_cy, rot); flush_batch(th);
                    }
                }
            }
            break;
        }
        case CHILD_TEXTBOX: {
            /* Multi-line scrollable text area: wrap ch.text into lines, draw a
             * window [scroll_offset, +visible), and a scrollbar on the right.
             * `tail: true` keeps the view stuck to the bottom as content grows. */
            if (!s->font) break;
            if (ch.bg_color || ch.border_width > 0.0f) {
                DrawBatch bb; bb.tex = s->white_tex; bb.mode = 3; bb.material = ch.material;
                push_quad_sdf(bb, cx, cy, cw, ch_h, rgba32_to_gl(ch.bg_color, alpha),
                              rgba32_to_gl(ch.border_color, alpha),
                              ch.corner_radius * sx, ch.border_width * sx);
                rotate_batch_verts(bb, 0, rot_cx, rot_cy, rot);
                flush_batch(bb);
            }
            float tb_target = ch.font_size > 0.0f ? ch.font_size * sx : 0.0f;
            AimlUIFont* lf = (tb_target > 0.0f) ? font_for_size_named(s, ch.font_name, tb_target)
                                                : s->font;
            if (!lf) break;
            float pad     = 6.0f * sx;
            float inner_w = cw - 2.0f * pad - 8.0f * sx;     /* leave room for the scrollbar */
            std::vector<std::pair<int32_t,int32_t>> spans;
            /* spec/44_6 §5f: without wrapping a line stays a line — for that
             * a width of 0 is enough, then `wrap_text_lines` only breaks at
             * '\n'/'|'.  The box scrolls sideways instead. */
            std::vector<std::string> lines = wrap_text_lines(lf, ch.text.c_str(),
                                                             ch.wrap ? inner_w : 0.0f, &spans);
            ch.tb_inner_w = inner_w;
            ch.tb_font    = lf;
            if (!ch.wrap) {
                float widest = 0.0f;
                for (const std::string& ln : lines) {
                    float w = text_width_font(lf, ln.c_str());
                    if (w > widest) widest = w;
                }
                ch.tb_content_w = widest;
                float max_x = widest - inner_w;
                if (max_x < 0.0f) max_x = 0.0f;
                if (ch.tb_off_x > max_x) ch.tb_off_x = max_x;
                if (ch.tb_off_x < 0.0f)  ch.tb_off_x = 0.0f;
            } else {
                ch.tb_content_w = inner_w;
                ch.tb_off_x = 0.0f;
            }
            int   total   = (int)lines.size();
            float line_h  = lf->line_height;
            float bar_room = (!ch.wrap) ? (8.0f * sy) : 0.0f;   /* room for the lower bar */
            int   visible = (line_h > 0.0f) ? (int)((ch_h - 2.0f * pad - bar_room) / line_h) : total;
            if (visible < 1) visible = 1;
            int   max_off = total > visible ? total - visible : 0;
            ch.tb_total = total; ch.tb_visible = visible;    /* cache for the input pass */
            /* Tail-follow (chat): stick to bottom when we were already there. */
            if (ch.tail) {
                if (ch.tb_prev_max < 0 || ch.scroll_offset >= ch.tb_prev_max)
                    ch.scroll_offset = max_off;
                ch.tb_prev_max = max_off;
            }
            if (ch.scroll_offset > max_off) ch.scroll_offset = max_off;
            if (ch.scroll_offset < 0)       ch.scroll_offset = 0;
            /* Cache caret stops per visible line (like text_input, only 2D) —
             * the input pass turns them into click/drag → source byte index. */
            ch.tb_caret_x.clear();
            ch.tb_caret_byte.clear();
            ch.tb_row_start.clear();
            ch.tb_row_y.clear();
            ch.tb_row_h = line_h;
            {
                float ty0 = cy + pad;
                for (int li = ch.scroll_offset; li < ch.scroll_offset + visible && li < total; ++li) {
                    ch.tb_row_start.push_back((int32_t)ch.tb_caret_x.size());
                    ch.tb_row_y.push_back(ty0);
                    int32_t sb = (li < (int)spans.size()) ? spans[(size_t)li].first  : 0;
                    int32_t se = (li < (int)spans.size()) ? spans[(size_t)li].second : 0;
                    tb_build_caret_stops(lf, ch.text, sb, se, lines[(size_t)li],
                                         cx + pad - ch.tb_off_x, ch.tb_caret_x, ch.tb_caret_byte);
                    ty0 += line_h;
                }
                ch.tb_row_start.push_back((int32_t)ch.tb_caret_x.size());   /* Endmarke */
            }
            /* SCISSOR around the text area: without wrapping a long line
             * would otherwise stick out over the frame and overwrite what lies
             * next to it.  With wrapping it changes nothing (everything fits there). */
            GLboolean tb_prev_sc = glIsEnabled(GL_SCISSOR_TEST);
            GLint tb_prev_box[4] = {0,0,0,0};
            glGetIntegerv(GL_SCISSOR_BOX, tb_prev_box);
            {
                /* The INTERIOR, not the whole frame: otherwise the
                 * text runs right onto the frame line (reported 2026-10-22).
                 * On the right the vertical bar stays free, at the bottom the
                 * horizontal one, if there is one. */
                float bar_b = (!ch.wrap && ch.tb_content_w > inner_w) ? (8.0f * sy) : 0.0f;
                int sxp = (int)ceilf(cx + 1.0f * sx);
                int syp = (int)ceilf((float)g_scene->screen_h - (cy + ch_h - bar_b));
                int swp = (int)floorf(cw - 2.0f * sx - 8.0f * sx);
                int shp = (int)floorf(ch_h - 2.0f * sy - bar_b);
                if (swp < 0) swp = 0;
                if (shp < 0) shp = 0;
                glEnable(GL_SCISSOR_TEST);
                glScissor(sxp, syp, swp, shp);
            }
            /* Selection FIRST (behind the text), one rectangle per visible line. */
            if (ch.focused && ch.sel_anchor != ch.cursor_pos) {
                int32_t lo = ch.sel_anchor < ch.cursor_pos ? ch.sel_anchor : ch.cursor_pos;
                int32_t hi = ch.sel_anchor < ch.cursor_pos ? ch.cursor_pos : ch.sel_anchor;
                DrawBatch hb; hb.tex = s->white_tex; hb.mode = 0;
                uint32_t hc = rgba32_to_gl(0x3A5FA0FF, alpha);
                int rows = (int)ch.tb_row_y.size();
                for (int r = 0; r < rows; ++r) {
                    int32_t a = ch.tb_row_start[(size_t)r];
                    int32_t b = ch.tb_row_start[(size_t)r + 1];
                    if (b <= a) continue;
                    float xlo = -1.0f, xhi = -1.0f;
                    for (int32_t k = a; k < b; ++k) {
                        int32_t by = ch.tb_caret_byte[(size_t)k];
                        if (by >= lo && by <= hi) {
                            if (xlo < 0.0f) xlo = ch.tb_caret_x[(size_t)k];
                            xhi = ch.tb_caret_x[(size_t)k];
                        }
                    }
                    if (xlo < 0.0f || xhi <= xlo) continue;
                    push_quad(hb, xlo, ch.tb_row_y[(size_t)r], xhi - xlo, line_h,
                              0,0,1,1, hc);
                }
                rotate_batch_verts(hb, 0, rot_cx, rot_cy, rot);
                flush_batch(hb);
            }
            {
                uint32_t col = rgba32_to_gl(ch.color ? ch.color : 0xFFFFFFFF, alpha);
                float ty = cy + pad;
                if (ch.color_runs.empty()) {
                    DrawBatch tbx; tbx.tex = lf->atlas_tex; tbx.mode = 2;
                    for (int li = ch.scroll_offset; li < ch.scroll_offset + visible && li < total; ++li) {
                        /* Without wrapping the line may extend past the edge —
                         * the scissor below cuts it off. */
                        push_text_batch(tbx, lf, lines[li].c_str(), cx + pad - ch.tb_off_x, ty,
                                        ch.wrap ? inner_w : 1.0e9f, col, "left");
                        ty += line_h;
                    }
                    rotate_batch_verts(tbx, 0, rot_cx, rot_cy, rot);
                    flush_batch(tbx);
                } else {
                    /* spec/44_6 §5g: colour runs also apply to the DISPLAY,
                     * not only to the input field.  Colour is a
                     * property of text, not of editability — a
                     * status column (cosmos doc/spec/97 F97-17) needs it in
                     * a text box just as much.  Same logic as in the
                     * text_input path: determine the colour per byte, then
                     * draw same-coloured neighbours in one go. */
                    for (int li = ch.scroll_offset; li < ch.scroll_offset + visible && li < total; ++li) {
                        const std::string& ln = lines[(size_t)li];
                        int32_t sb = (li < (int)spans.size()) ? (int32_t)spans[(size_t)li].first : 0;
                        size_t n = ln.size();
                        if (n == 0) { ty += line_h; continue; }
                        std::vector<uint32_t> cols(n, col);
                        for (size_t r = 0; r + 2 < ch.color_runs.size(); r += 3) {
                            int32_t rs = ch.color_runs[r], rl = ch.color_runs[r+1];
                            uint32_t rc = (uint32_t)ch.color_runs[r+2];
                            int32_t a = rs > sb ? rs : sb;
                            int32_t b = (rs + rl) < (sb + (int32_t)n) ? (rs + rl) : (sb + (int32_t)n);
                            for (int32_t q = a; q < b; ++q)
                                cols[(size_t)(q - sb)] = rgba32_to_gl(rc, alpha);
                        }
                        size_t seg = 0;
                        float px = cx + pad;
                        while (seg < n) {
                            size_t e = seg + 1;
                            while (e < n && cols[e] == cols[seg]) ++e;
                            std::string part = ln.substr(seg, e - seg);
                            DrawBatch tb; tb.tex = lf->atlas_tex; tb.mode = 2;
                            push_text_batch(tb, lf, part.c_str(), px, ty, 1.0e9f, cols[seg], "left");
                            rotate_batch_verts(tb, 0, rot_cx, rot_cy, rot);
                            flush_batch(tb);
                            px += text_width_font(lf, part.c_str());
                            seg = e;
                        }
                        ty += line_h;
                    }
                }
            }
            if (tb_prev_sc) glScissor(tb_prev_box[0], tb_prev_box[1], tb_prev_box[2], tb_prev_box[3]);
            else            glDisable(GL_SCISSOR_TEST);
            if (total > visible) {
                float sb_w    = 6.0f * sx;
                float sb_x    = cx + cw - sb_w - 2.0f * sx;
                float track_y = cy + 2.0f * sy;
                float track_h = ch_h - 4.0f * sy;
                { DrawBatch tk; tk.tex = s->white_tex; tk.mode = 0;
                  push_quad(tk, sb_x, track_y, sb_w, track_h, 0,0,1,1, rgba32_to_gl(0x101828A0, alpha));
                  rotate_batch_verts(tk, 0, rot_cx, rot_cy, rot); flush_batch(tk); }
                float thumb_h = track_h * (float)visible / (float)total;
                if (thumb_h < 16.0f * sy) thumb_h = 16.0f * sy;
                float thumb_y = track_y + (track_h - thumb_h) * (float)ch.scroll_offset / (float)std::max(1, max_off);
                DrawBatch th; th.tex = s->white_tex; th.mode = 0;
                push_quad(th, sb_x, thumb_y, sb_w, thumb_h, 0,0,1,1, rgba32_to_gl(0x3A5578F0, alpha));
                rotate_batch_verts(th, 0, rot_cx, rot_cy, rot); flush_batch(th);
            }
            /* The SECOND bar, at the bottom (spec/44_6 §5f): only without wrapping, and
             * only if the content really is wider.  A bar that has nothing
             * to scroll is a claim. */
            if (!ch.wrap && ch.tb_content_w > inner_w) {
                float sb_h    = 6.0f * sy;
                float sb_y    = cy + ch_h - sb_h - 2.0f * sy;
                float track_x = cx + 2.0f * sx;
                float track_w = cw - 4.0f * sx - 8.0f * sx;   /* room for the vertical one */
                { DrawBatch tk; tk.tex = s->white_tex; tk.mode = 0;
                  push_quad(tk, track_x, sb_y, track_w, sb_h, 0,0,1,1, rgba32_to_gl(0x101828A0, alpha));
                  rotate_batch_verts(tk, 0, rot_cx, rot_cy, rot); flush_batch(tk); }
                float frac    = inner_w / ch.tb_content_w;
                float thumb_w = track_w * frac;
                if (thumb_w < 16.0f * sx) thumb_w = 16.0f * sx;
                float max_x   = ch.tb_content_w - inner_w;
                float thumb_x = track_x + (track_w - thumb_w) * (max_x > 0.0f ? ch.tb_off_x / max_x : 0.0f);
                DrawBatch th; th.tex = s->white_tex; th.mode = 0;
                push_quad(th, thumb_x, sb_y, thumb_w, sb_h, 0,0,1,1, rgba32_to_gl(0x3A5578F0, alpha));
                rotate_batch_verts(th, 0, rot_cx, rot_cy, rot); flush_batch(th);
            }
            break;
        }
        case CHILD_CONTAINER: {
            /* Native scrollbar for `scrollable` containers: a track + thumb on
             * the right edge, shown when more rows are spawned than fit. */
            if (ch.scrollable && !ch.spawned.empty()) {
                bool cflow_h = (ch.layout == "hflow");
                AimlPrefabInstance* r0 = ch.spawned[0];
                float pitch  = cflow_h ? (r0->def->width + ch.gap) : (r0->def->height + ch.gap);
                float extent = cflow_h ? ch.width : ch.height;
                int   visible = (pitch > 0.0f) ? (int)(extent / pitch) : (int)ch.spawned.size();
                if (visible < 1) visible = 1;
                int total = (int)ch.spawned.size();
                if (total > visible) {
                    float sb_w    = 6.0f * sx;
                    float sb_x    = cx + cw - sb_w - 2.0f * sx;
                    float track_y = cy + 2.0f * sy;
                    float track_h = ch_h - 4.0f * sy;
                    { DrawBatch tb; tb.tex = s->white_tex; tb.mode = 0;
                      push_quad(tb, sb_x, track_y, sb_w, track_h, 0,0,1,1,
                                rgba32_to_gl(0x101828A0, alpha));
                      rotate_batch_verts(tb, 0, rot_cx, rot_cy, rot);
                      flush_batch(tb); }
                    float thumb_h = track_h * (float)visible / (float)total;
                    if (thumb_h < 16.0f * sy) thumb_h = 16.0f * sy;
                    float thumb_y = track_y + (track_h - thumb_h)
                                  * (float)ch.scroll_offset / (float)std::max(1, total - visible);
                    DrawBatch sb; sb.tex = s->white_tex; sb.mode = 0;
                    push_quad(sb, sb_x, thumb_y, sb_w, thumb_h, 0,0,1,1,
                              rgba32_to_gl(0x3A5578F0, alpha));
                    rotate_batch_verts(sb, 0, rot_cx, rot_cy, rot);
                    flush_batch(sb);
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
                /* Default border: without an explicit border_color/border_width
                 * the box would be invisible (fg==bg often 0).  So that a checkbox
                 * is ALWAYS visible, it falls back to a discreet frame
                 * (overridable via border_color/border_width in the prefab). */
                uint32_t box_border = ch.border_color ? ch.border_color : 0x8899AACC;
                float    box_bw     = ch.border_width > 0.0f ? ch.border_width : 1.5f;
                uint32_t fg = rgba32_to_gl(bg_rgba,     alpha);
                uint32_t bd = rgba32_to_gl(box_border,  alpha);
                push_quad_sdf(batch, cx, cy, box_size, box_size, fg, bd,
                              ch.corner_radius * sx, box_bw * sx);
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
                AimlUIFont* lf = (target_size > 0.0f) ? font_for_size_named(s, ch.font_name, target_size) : s->font;
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
        case CHILD_DROPDOWN: {
            /* Button-like field with the current option label + "▼".  A click
             * cycles through the pipe-separated option list (see the input
             * loop).  Rendering: bg + label on the left, arrow on the right. */
            uint32_t bg_rgba = ch.pressed_now && ch.active_color ? ch.active_color
                            : ch.hot         && ch.hover_color  ? ch.hover_color
                            :                                     ch.bg_color;
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
            if (s->font) {
                /* Aktuelle Option ermitteln. */
                std::string current;
                if (!ch.options.empty()) {
                    int idx = 0;
                    size_t start = 0;
                    for (size_t p = 0; p <= ch.options.size(); ++p) {
                        if (p == ch.options.size() || ch.options[p] == '|') {
                            if (idx == ch.selected_index) {
                                current = ch.options.substr(start, p - start);
                                break;
                            }
                            idx++;
                            start = p + 1;
                        }
                    }
                }
                float target_size = ch.font_size > 0.0f ? ch.font_size * sx : 0.0f;
                AimlUIFont* lf = (target_size > 0.0f) ? font_for_size_named(s, ch.font_name, target_size) : s->font;
                if (lf) {
                    DrawBatch tb;
                    tb.tex  = lf->atlas_tex;
                    tb.mode = 2;
                    uint32_t lcol = rgba32_to_gl(ch.color ? ch.color : 0xFFFFFFFF, alpha);
                    float pad_l = 8.0f * sx;
                    float text_cx = cx + pad_l;
                    float text_cy = cy + (ch_h - lf->line_height) * 0.5f;
                    push_text_batch(tb, lf, current.c_str(),
                                    text_cx, text_cy, cw - 24.0f * sx, lcol, "left");
                    rotate_batch_verts(tb, 0, rot_cx, rot_cy, rot);
                    flush_batch(tb);
                    /* Arrow on the right — a small downward-pointing triangle
                     * instead of the letter "v".  3 vertices as a batch of their own
                     * (mode 0 = solid white texture).                        */
                    {
                        DrawBatch ab;
                        ab.tex  = s->white_tex;
                        ab.mode = 0;
                        float arrow_w = 8.0f * sx;
                        float arrow_h = 5.0f * sx;
                        float arrow_cx = cx + cw - 12.0f * sx;
                        float arrow_cy = cy + ch_h * 0.5f;
                        PrefabVertex vl{ arrow_cx - arrow_w * 0.5f, arrow_cy - arrow_h * 0.5f,
                                         0, 0, lcol, 0,0,0,0,0 };
                        PrefabVertex vr{ arrow_cx + arrow_w * 0.5f, arrow_cy - arrow_h * 0.5f,
                                         1, 0, lcol, 0,0,0,0,0 };
                        PrefabVertex vb{ arrow_cx,                  arrow_cy + arrow_h * 0.5f,
                                         0.5f, 1, lcol, 0,0,0,0,0 };
                        ab.verts.push_back(vl);
                        ab.verts.push_back(vr);
                        ab.verts.push_back(vb);
                        rotate_batch_verts(ab, 0, rot_cx, rot_cy, rot);
                        flush_batch(ab);
                    }
                }
            }
            /* spec/44_6 §5h: horizontal bar for the scrolled payload chain —
             * track + thumb at the bottom edge of the ROW above the payload zone.  Only
             * if there really is something to scroll (a bar without overhang is a
             * claim — the same rule as for the text box bar). */
            if (ch.payload_scroll && ch.payload_pitch > 0.0f && inst->def) {
                float viewport = inst->def->width - ch.payload_start;
                if (viewport > 0.0f && ch.payload_span > viewport + 0.5f) {
                    float sb_h    = 6.0f * sy;
                    float sb_y    = oy + (inst->def->height - 7.0f) * sy;
                    float track_x = ox + ch.payload_start * sx;
                    float track_w = viewport * sx;
                    { DrawBatch tk; tk.tex = s->white_tex; tk.mode = 0;
                      push_quad(tk, track_x, sb_y, track_w, sb_h, 0,0,1,1, rgba32_to_gl(0x101828A0, alpha));
                      rotate_batch_verts(tk, 0, rot_cx, rot_cy, rot); flush_batch(tk); }
                    float thumb_w = track_w * (viewport / ch.payload_span);
                    if (thumb_w < 16.0f * sx) thumb_w = 16.0f * sx;
                    float max_off = ch.payload_span - viewport;
                    float frac    = max_off > 0.0f ? ch.payload_scroll_x / max_off : 0.0f;
                    float thumb_x = track_x + (track_w - thumb_w) * frac;
                    DrawBatch th; th.tex = s->white_tex; th.mode = 0;
                    push_quad(th, thumb_x, sb_y, thumb_w, sb_h, 0,0,1,1, rgba32_to_gl(0x3A5578F0, alpha));
                    rotate_batch_verts(th, 0, rot_cx, rot_cy, rot); flush_batch(th);
                }
            }
            break;
        }
        case CHILD_RADIO: {
            /* Circular indicator on the left + label on the right (like CHILD_CHECKBOX
             * without the texture branch).  corner_radius is forced to half the box size
             * so that the look is always round, regardless of the YAML. */
            float box_size = ch_h < cw ? ch_h : cw;
            uint32_t bg_rgba = ch.checked && ch.active_color    ? ch.active_color
                            : ch.pressed_now && ch.active_color ? ch.active_color
                            : ch.hot         && ch.hover_color  ? ch.hover_color
                            :                                     ch.bg_color;
            float radius = box_size * 0.5f;
            {
                DrawBatch batch;
                batch.tex  = s->white_tex;
                batch.mode = 3;
                uint32_t fg = rgba32_to_gl(bg_rgba,         alpha);
                uint32_t bd = rgba32_to_gl(ch.border_color, alpha);
                push_quad_sdf(batch, cx, cy, box_size, box_size, fg, bd,
                              radius, ch.border_width * sx);
                rotate_batch_verts(batch, 0, rot_cx, rot_cy, rot);
                flush_batch(batch);
            }
            if (ch.checked) {
                float inset = box_size * 0.25f;
                DrawBatch dot;
                dot.tex  = s->white_tex;
                dot.mode = 3;
                uint32_t dot_fill = rgba32_to_gl(0xFFFFFFFF, alpha);
                push_quad_sdf(dot, cx + inset, cy + inset,
                              box_size - 2.0f * inset, box_size - 2.0f * inset,
                              dot_fill, 0, radius * 0.5f, 0);
                rotate_batch_verts(dot, 0, rot_cx, rot_cy, rot);
                flush_batch(dot);
            }
            if (!ch.text.empty() && s->font) {
                float target_size = ch.font_size > 0.0f ? ch.font_size * sx : 0.0f;
                AimlUIFont* lf = (target_size > 0.0f) ? font_for_size_named(s, ch.font_name, target_size) : s->font;
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
                             ? font_for_size_named(s, ch.font_name, target_size)
                             : s->font;
            if (!lf) break;
            /* Auto-fit: in a "short" box (title height ≈ one line) a
             * long text would wrap and run out of the frame.  Instead of wrapping,
             * shrink the font so that the (unwrapped) text fits into the
             * width.  Only for short boxes WITHOUT an explicit line break —
             * multi-line labels (ability text, valign:top, tall box) stay.
             *
             * IMPORTANT: shrink iteratively with a safety margin.  A one-off
             * fit_size = target*(fit_w/tw) only ESTIMATES the width — after
             * re-baking at the smaller size the (integer) glyph
             * width may round slightly above fit_w, so that the text DOES wrap onto 2 lines
             * and slides out of the title box.  Therefore: measure → shrink
             * (with a 4 % margin) → measure again, until it safely fits on ONE line. */
            if (target_size > 0.0f && ch.height > 0.0f &&
                ch.text.find('\n') == std::string::npos &&
                ch.text.find('|') == std::string::npos) {
                float fit_w = ch.width > 0.0f ? ch.width * sx
                                              : (inst->def ? inst->def->width * sx : 0.0f);
                float box_h = ch.height * sx;
                if (fit_w > 0.0f && box_h < 1.8f * lf->line_height) {
                    float min_size = target_size * 0.4f;
                    float cur_size = target_size;
                    AimlUIFont* fitf = lf;
                    for (int fit_it = 0; fit_it < 6; ++fit_it) {
                        float tw = text_width_font(fitf, ch.text.c_str());
                        if (tw <= fit_w) break;               /* fits on one line */
                        cur_size = cur_size * (fit_w / tw) * 0.96f;  /* Marge gegen Rundung */
                        if (cur_size < min_size) cur_size = min_size;
                        AimlUIFont* nf = font_for_size(s, cur_size);
                        if (!nf) break;
                        fitf = nf;
                        if (cur_size <= min_size) break;      /* Untergrenze erreicht */
                    }
                    lf = fitf;
                }
            }
            DrawBatch batch;
            batch.tex = lf->atlas_tex;
            batch.mode = 2; /* font atlas */
            /* Consistent with all other UI widgets: read .color as 0xRRGGBBAA
             * via rgba32_to_gl. The legacy 0xRRGGBB interpretation would
             * shift channels and disagree with button/checkbox labels. */
            uint32_t col = rgba32_to_gl(ch.color ? ch.color : 0xFFFFFFFF, alpha);
            /* anchor + valign from the def (PrefabChild is only a render snapshot;
             * layout hints live in PrefabChildDef). */
            const char* anchor = "left";
            const char* valign = "";
            auto it = inst->child_index.find(ch.name);
            if (it != inst->child_index.end() && inst->def) {
                int idx = it->second;
                if (idx >= 0 && idx < (int)inst->def->children.size()) {
                    anchor = inst->def->children[idx].anchor.c_str();
                    valign = inst->def->children[idx].valign.c_str();
                }
            }
            /* Vertical position: an explicit valign (top/center/bottom) overrides
             * the default.  Default = center if height>0, otherwise top
             * (baseline at cy + ascent via push_text_batch).
             * For multi-line "bottom" / "top" → take the text block height into account. */
            float text_cy;
            if (valign && strcmp(valign, "top") == 0) {
                text_cy = cy;
            } else if (valign && strcmp(valign, "bottom") == 0) {
                float box_h = (ch.height > 0.0f) ? ch.height * sx : (float)lf->line_height;
                auto lines = wrap_text_lines(lf, ch.text.c_str(),
                                             ch.width > 0.0f ? ch.width * sx : inst->def->width * sx);
                float block_h = (float)lines.size() * lf->line_height;
                text_cy = cy + (box_h - block_h);
            } else {
                /* Default centred (or top if height==0) — as before. */
                text_cy = (ch.height > 0.0f)
                              ? cy + (ch.height * sx - lf->line_height) * 0.5f
                              : cy;
            }
            /* With anchor="center"/"right", push_text_batch expects cx
             * to be the centre or right-edge reference point (see the x formula
             * there: cx - tw*0.5f / cx - tw).  For labels, however, we have the
             * LEFT edge as cx — shift according to the anchor. */
            float text_w = ch.width > 0.0f ? ch.width * sx : inst->def->width * sx;
            float text_cx = cx;
            if (anchor && strcmp(anchor, "center") == 0) {
                text_cx = cx + text_w * 0.5f;
            } else if (anchor && strcmp(anchor, "right") == 0) {
                text_cx = cx + text_w;
            }
            /* Optional outline pass — before the main text is drawn.
             * 8-sample ring at ±outline_px in outline_color, then the text on top.
             * Prevents white numbers from disappearing on light icons
             * (e.g. a count label on light mana). */
            if (ch.outline_px > 0.0f && (ch.outline_color & 0xFFu)) {
                uint32_t ocol = rgba32_to_gl(ch.outline_color, alpha);
                float d = ch.outline_px * sx;
                const float offs[8][2] = {
                    { -d, -d }, {  0, -d }, {  d, -d },
                    { -d,  0 },              {  d,  0 },
                    { -d,  d }, {  0,  d }, {  d,  d },
                };
                for (int oi = 0; oi < 8; ++oi) {
                    push_text_batch(batch, lf, ch.text.c_str(),
                                    text_cx + offs[oi][0],
                                    text_cy + offs[oi][1],
                                    text_w, ocol, anchor);
                }
            }
            push_text_batch(batch, lf, ch.text.c_str(),
                           text_cx, text_cy, text_w, col, anchor);
            rotate_batch_verts(batch, 0, rot_cx, rot_cy, rot);
            flush_batch(batch);
            break;
        }
        }
    }

    /* Nested child instances — inherit this instance's world transform.
     * If this instance is a clip/scroll container, scissor to its world
     * bounds and shift children by the scroll offset (local px). */
    if (!inst->child_instances.empty()) {
        float s_ox = g_render_off_x, s_oy = g_render_off_y;
        float s_sx = g_render_scale_x, s_sy = g_render_scale_y;

        bool clipping = inst->clip_children;
        GLboolean prev_scissor = GL_FALSE;
        GLint prev_box[4] = {0,0,0,0};
        if (clipping) {
            prev_scissor = glIsEnabled(GL_SCISSOR_TEST);
            glGetIntegerv(GL_SCISSOR_BOX, prev_box);
            float cw = inst->def->width * wsx;
            float ch = inst->def->height * wsy;
            /* GL scissor origin is bottom-left; our coords are top-left.
             * Round the box INWARD (ceil position, floor size) so sub-pixel
             * widths/heights never leave a one-pixel gap where content can
             * leak beyond the visible panel. */
            int sxp = (int)ceilf(wox);
            int syp = (int)ceilf((float)g_scene->screen_h - (woy + ch));
            int swp = (int)floorf(cw);
            int shp = (int)floorf(ch);
            if (swp < 0) swp = 0; if (shp < 0) shp = 0;
            glEnable(GL_SCISSOR_TEST);
            glScissor(sxp, syp, swp, shp);
            g_render_off_x = wox - inst->scroll_x * wsx;
            g_render_off_y = woy - inst->scroll_y * wsy;
        } else {
            g_render_off_x = wox; g_render_off_y = woy;
        }
        g_render_scale_x = wsx; g_render_scale_y = wsy;
        /* Parent's world bounds (used by the cull below). */
        const float pbx = wox, pby = woy;
        const float pbw = inst->def->width  * wsx;
        const float pbh = inst->def->height * wsy;
        for (auto* ci : inst->child_instances) {
            /* Software clip: when this instance is a clip container, render
             * each child only if its full bounding box fits inside the
             * parent's world bounds.  Strict cull (no partial-overlap render)
             * because GL_SCISSOR_TEST does not always clip reliably in our
             * WebGL2/emcc setup; this guarantees no edge leakage. The visual
             * trade-off: scrolling becomes discrete per row instead of smooth
             * (callers can mitigate by snapping scroll_y to the row pitch). */
            if (clipping) {
                float cw_ = ci->def->width  * wsx;
                float ch_ = ci->def->height * wsy;
                float clx = g_render_off_x + ci->x * wsx;
                float cly = g_render_off_y + ci->y * wsy;
                if (clx < pbx || clx + cw_ > pbx + pbw ||
                    cly < pby || cly + ch_ > pby + pbh) {
                    continue;
                }
            }
            render_instance(ci);
        }

        if (clipping) {
            if (prev_scissor) glScissor(prev_box[0], prev_box[1], prev_box[2], prev_box[3]);
            else              glDisable(GL_SCISSOR_TEST);
        }
        g_render_off_x = s_ox; g_render_off_y = s_oy;
        g_render_scale_x = s_sx; g_render_scale_y = s_sy;
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

struct PendingPrefab {
    std::string name;
    float width, height;
    std::string texture_path;
    int32_t draggable;
    std::string children_json;
    int32_t input_transparent;
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
                                const char* children_json, int32_t input_transparent);

/* Resize: only update screen dims + projection, without creating g_scene.
 * design_w/h stays from the initial aiml_prefab_scene_init.  Called from the
 * GLFW framebuffer-size callback so that the UI grows along. */
void aiml_prefab_scene_resize(int32_t screen_w, int32_t screen_h) {
    if (!g_scene) return;
    g_scene->screen_w = screen_w;
    g_scene->screen_h = screen_h;
    ortho(g_scene->proj, 0, (float)screen_w, (float)screen_h, 0);
}

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
                            p.children_json.c_str(), p.input_transparent);
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
                                const char* children_json, int32_t input_transparent)
{
    PrefabDef* def = new PrefabDef();
    def->name = name ? name : "";
    def->width = width;
    def->height = height;
    def->texture_path = texture_path ? texture_path : "";
    def->draggable = (draggable != 0);
    def->input_transparent = (input_transparent != 0);
    def->children = parse_children_json(children_json);

    g_scene->defs[def->name] = def;
    fprintf(stderr, "[prefab] registered '%s' (%gx%g, %zu children)\n",
            def->name.c_str(), width, height, def->children.size());
}

void aiml_prefab_register(const char* name, float width, float height,
                          const char* texture_path, int32_t draggable,
                          const char* children_json, int32_t input_transparent)
{
    if (!g_scene) {
        g_pending_prefabs.push_back({
            name ? name : "", width, height,
            texture_path ? texture_path : "", draggable,
            children_json ? children_json : "", input_transparent
        });
        return;
    }

    register_prefab_def(name, width, height, texture_path, draggable,
                        children_json, input_transparent);
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
    inst->input_transparent = def->input_transparent;
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
        child.font_name = cd.font_name;
        child.gutter    = cd.gutter;
        child.color = cd.color;
        child.visible = cd.visible;
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
        child.outline_color = cd.outline_color;
        child.outline_px    = cd.outline_px;
        /* Tileable rect bg: resolve stem to GL id once — same cache as
         * sprite textures, so `stone_texture_dark_cracked_tileable` gets
         * loaded from the asset registry with GL_REPEAT wrap already set
         * by aiml_gl_load_texture (see gl_runtime.cpp:124-125). */
        if (!cd.bg_texture_path.empty()) {
            child.bg_texture = load_texture_cached(cd.bg_texture_path.c_str());
        }
        child.tile_size     = cd.tile_size > 0.0f ? cd.tile_size : 128.0f;
        if (!cd.track_texture_path.empty()) {
            child.track_texture = load_texture_cached(cd.track_texture_path.c_str());
        }
        if (!cd.handle_texture_path.empty()) {
            child.handle_texture = load_texture_cached(cd.handle_texture_path.c_str());
        }
        if (!cd.handle_gem_texture_path.empty()) {
            child.handle_gem_texture =
                load_texture_cached(cd.handle_gem_texture_path.c_str());
        }
        if (!cd.frame_border_texture_path.empty()) {
            child.frame_border_texture =
                load_texture_cached(cd.frame_border_texture_path.c_str());
        }
        child.frame_border_size = cd.frame_border_size > 0.0f ? cd.frame_border_size : 6.0f;
        child.flip_h        = cd.flip_h;
        child.slider_snap   = cd.slider_snap;
        child.label_text    = cd.label_text;
        child.is_secret     = cd.is_secret;
        child.bar_value     = cd.bar_value;
        child.bar_max       = cd.bar_max;
        child.slider_min    = cd.slider_min;
        child.fill_color    = cd.fill_color;
        child.on_click_fn   = cd.on_click_fn;
        child.on_change_fn  = cd.on_change_fn;
        child.group         = cd.group;
        child.options       = cd.options;
        child.selected_index = 0;
        child.material      = material_find(cd.material_name.c_str());
        /* Phase 6 bindings — carry compile-time attributes to the runtime widget */
        child.bind          = cd.bind;
        child.bind_offset   = cd.bind_offset;
        child.bind_tag      = cd.bind_tag;
        child.bind_list     = cd.bind_list;
        child.spawn         = cd.spawn;
        child.layout        = cd.layout;
        child.gap           = cd.gap;
        child.compact       = cd.compact;
        child.scrollable    = cd.scrollable;
        child.tail          = cd.tail;
        child.wrap          = cd.wrap;
        child.cascade_type  = cd.cascade_type;
        child.payload_spawn = cd.payload_spawn;
        child.cascade_flow  = cd.cascade_flow;
        child.payload_scroll = cd.payload_scroll;
        child.enum_type     = cd.enum_type;
        /* `enum: TypeName` on a dropdown/radio — pull the display labels from
         * the enum registry (snake_case already derived to "Title Case" by the
         * compiler) so the author doesn't hard-code them.  The value accessor
         * (aiml_prefab_dropdown_enum_value) returns the true member value. */
        if (!cd.enum_type.empty() && child.options.empty()) {
            const char* enum_labels = aiml_prefab_enum_labels(cd.enum_type.c_str());
            if (enum_labels && *enum_labels) child.options = enum_labels;
        }
        /* Auto-populate dropdown options from the cascade metadata so the
         * prefab author doesn't have to hard-code node labels. */
        if (!cd.cascade_type.empty() &&
            (cd.type == CHILD_DROPDOWN || cd.type == CHILD_CASCADE)) {
            const char* opts = aiml_prefab_cascade_options(cd.cascade_type.c_str());
            if (opts && *opts) child.options = opts;
        }
        /* Seed text buffer: text_input uses label as initial value; static
         * labels use it as their displayed text; buttons use it as the
         * displayed caption (so set_text can re-label a button at runtime);
         * checkboxes use it as the label text rendered next to the box. */
        if (child.type == CHILD_TEXT_INPUT || child.type == CHILD_LABEL
            || child.type == CHILD_BUTTON  || child.type == CHILD_CHECKBOX
            || child.type == CHILD_RADIO   || child.type == CHILD_TEXTBOX)
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

/* Drop every scene-wide reference to a dying instance.  Must
 * be called from EVERY path that frees an instance — `destroy` already did
 * that, the end-of-frame sweep for `pending_delete` did not, and there above all
 * `focused_input_inst` dangled: it is STICKY (stays until the next
 * click elsewhere), so every further key press wrote into freed
 * memory. */
static void scene_forget_instance(AimlPrefabInstance* inst) {
    if (!inst || !g_scene) return;
    if (g_scene->dragged == inst) g_scene->dragged = nullptr;
    if (g_scene->drag_source == inst) g_scene->drag_source = nullptr;
    if (g_scene->hovered == inst) g_scene->hovered = nullptr;
    if (g_scene->focused_input_inst == inst) {
        g_scene->focused_input_inst = nullptr;
        g_scene->focused_input_child_idx = -1;
    }
}

void aiml_prefab_destroy(void* p) {
    AimlPrefabInstance* inst = (AimlPrefabInstance*)p;
    if (!inst || !g_scene) return;

    scene_forget_instance(inst);

    /* Detach from parent, then take dynamically-spawned children (bind_list rows /
     * spawn_child) DOWN with this instance.  They must not be left as orphans:
     * their x/y are parent-LOCAL, so an orphan renders at raw local coords near
     * the window origin (e.g. a stray ack-checkbox at 0,0 behind another widget).
     * We do NOT delete them inline — rows are tracked in BOTH `child_instances`
     * AND their container widget's `spawned`/`spawned_payload` lists, so an inline
     * recursive delete double-frees.  Instead mark them `pending_delete` + clear
     * their parent, exactly like the container-sync sweep in the render loop,
     * which frees them safely on the next frame. */
    if (inst->parent) {
        auto& sib = inst->parent->child_instances;
        sib.erase(std::remove(sib.begin(), sib.end(), inst), sib.end());
        inst->parent = nullptr;
    }
    for (auto& c : inst->children) {
        for (auto* s : c.spawned)         { if (s) { s->parent = nullptr; s->pending_delete = true; } }
        for (auto* s : c.spawned_payload) { if (s) { s->parent = nullptr; s->pending_delete = true; } }
    }
    for (auto* ci : inst->child_instances) { if (ci) { ci->parent = nullptr; ci->pending_delete = true; } }

    /* Drop any cables touching this instance. */
    {
        auto& cs = g_scene->connectors;
        for (auto* c : cs) if (c->from == inst || c->to == inst) c->visible = false;
        cs.erase(std::remove_if(cs.begin(), cs.end(),
            [&](AimlConnector* c){
                if (c->from == inst || c->to == inst) { delete c; return true; }
                return false;
            }), cs.end());
    }

    auto& v = g_scene->instances;
    v.erase(std::remove(v.begin(), v.end(), inst), v.end());
    delete inst;
}

/* Is a prefab instance still alive (present in the scene)?  Compares the
 * pointer VALUE against the live instance list — never dereferences `p`, so it
 * is safe to call with a stale/dangling handle.  Returns 1 if alive, 0 if it
 * was destroyed (or null / no scene).  Lets AIML code guard against use-after-
 * destroy instead of blindly re-destroying. */
int32_t aiml_prefab_is_alive(void* p) {
    if (!p || !g_scene) return 0;
    auto* inst = (AimlPrefabInstance*)p;
    return std::find(g_scene->instances.begin(), g_scene->instances.end(), inst)
               != g_scene->instances.end() ? 1 : 0;
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
void aiml_prefab_set_input_transparent(void* p, int32_t t) {
    auto* inst = (AimlPrefabInstance*)p; if (inst) inst->input_transparent = (t != 0);
}
void aiml_prefab_set_input_blocking(void* p, int32_t t) {
    auto* inst = (AimlPrefabInstance*)p; if (inst) inst->input_blocking = (t != 0);
}
int32_t aiml_prefab_get_input_blocking(void* p) {
    auto* inst = (AimlPrefabInstance*)p; return inst ? (inst->input_blocking ? 1 : 0) : 0;
}
int32_t aiml_prefab_get_input_transparent(void* p) {
    auto* inst = (AimlPrefabInstance*)p; return inst ? (inst->input_transparent ? 1 : 0) : 0;
}

// ────────────────────────────────────────────────────────────────────────
// Layout helpers (spec 44_6 §Layout) — auto-position a sibling list in a row / column
// / stack.  Saves the manual loop + spacing math
// that would otherwise be duplicated in every hand/zone render code.
//
// list_ptr is a `list<prefab_instance>` (i.e. AimlList* with i8* data).
// ────────────────────────────────────────────────────────────────────────

namespace {
    inline AimlPrefabInstance* layout_item(void** entries, int32_t count, int32_t idx) {
        if (!entries || idx < 0 || idx >= count) return nullptr;
        return static_cast<AimlPrefabInstance*>(entries[idx]);
    }
    inline float layout_w(AimlPrefabInstance* pi) {
        if (!pi || !pi->def) return 0.0f;
        return pi->def->width * pi->scale_x;
    }
    inline float layout_h(AimlPrefabInstance* pi) {
        if (!pi || !pi->def) return 0.0f;
        return pi->def->height * pi->scale_y;
    }
}

// Signature: data_ptr is data[] of the list<prefab_instance> (= i8**), count
// is the size of the list.  The caller (compiler glue) extracts it via
// CreateExtractValue on the AimlList struct.  Avoids a bitcast struct→ptr.
void aiml_engine_layout_hflow(void* data_ptr, int32_t count, float x, float y, float gap) {
    if (!data_ptr || count <= 0) return;
    void** entries = static_cast<void**>(data_ptr);
    float cur_x = x;
    for (int32_t i = 0; i < count; ++i) {
        AimlPrefabInstance* pi = static_cast<AimlPrefabInstance*>(entries[i]);
        if (!pi) continue;
        pi->x = cur_x;
        pi->y = y;
        pi->is_relative = false;
        cur_x += layout_w(pi) + gap;
    }
}

void aiml_engine_layout_vflow(void* data_ptr, int32_t count, float x, float y, float gap) {
    void** entries = static_cast<void**>(data_ptr);
    float cur_y = y;
    for (int32_t i = 0; i < count; ++i) {
        AimlPrefabInstance* pi = layout_item(entries, count, i);
        if (!pi) continue;
        pi->x = x;
        pi->y = cur_y;
        pi->is_relative = false;
        cur_y += layout_h(pi) + gap;
    }
}

void aiml_engine_layout_stack(void* data_ptr, int32_t count,
                              float x, float y, float dx, float dy, int32_t z_step) {
    void** entries = static_cast<void**>(data_ptr);
    for (int32_t i = 0; i < count; ++i) {
        AimlPrefabInstance* pi = layout_item(entries, count, i);
        if (!pi) continue;
        pi->x = x + dx * static_cast<float>(i);
        pi->y = y + dy * static_cast<float>(i);
        pi->is_relative = false;
        pi->z_order += z_step * i;
    }
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
    if (it != inst->child_index.end()) return &inst->children[it->second];
    // Unknown child name — checked centrally by ALL scene methods (set_child_visible/set_text/
    // dropdown_value/…).  Almost always a typo / outdated /
    // wrong widget name.  Formerly a no-op → bugs stayed hidden for a long time (e.g.
    // dropdown "pa0" instead of "pa_slot0" → passive neither rendered nor saved).
    // Now LOUD: a clear fatal message with the prefab name + all valid children.
    // Escape hatch: AIML_PREFAB_LENIENT=1 downgrades to a warning (return null).
    std::string avail;
    for (auto& kv : inst->child_index) { if (!avail.empty()) avail += ", "; avail += kv.first; }
    const char* pf = (inst->def && !inst->def->name.empty()) ? inst->def->name.c_str() : "<unknown>";
    static const bool lenient = (getenv("AIML_PREFAB_LENIENT") != nullptr);
    char msg[512];
    snprintf(msg, sizeof(msg),
             "prefab widget child '%s' not found in prefab '%s' — valid children: %s",
             child_name, pf, avail.c_str());
    fprintf(stderr, "[prefab-%s] %s\n", lenient ? "WARN" : "EXCEPTION", msg);
    if (lenient) return nullptr;
    // A real exception: propagates through the AIML C++ EH (personality functions);
    // catchable with AIML try/catch (catch-all), otherwise → terminate with a clear
    // "what()" message + crash-handler backtrace to the call site.
    throw std::runtime_error(msg);
}

void aiml_prefab_set_text(void* inst, const char* child_name, const char* text) {
    PrefabChild* ch = find_child(inst, child_name);
    if (!ch) return;
    ch->text = text ? text : "";
    /* An external text change (e.g. clearing a chat input after send) must keep
     * the text_input cursor/selection valid, else the next insert/erase/substr
     * runs past the end and throws std::out_of_range. */
    int32_t len = (int32_t)ch->text.size();
    if (ch->cursor_pos > len) ch->cursor_pos = len;
    if (ch->cursor_pos < 0)   ch->cursor_pos = 0;
    if (ch->sel_anchor > len) ch->sel_anchor = len;
    if (ch->sel_anchor < 0)   ch->sel_anchor = 0;
}

/* Programmatically give keyboard focus to a text_input child (e.g. re-focus the
 * chat input after the Send button stole focus). */
void aiml_prefab_set_focus(void* inst_p, const char* child_name) {
    auto* inst = (AimlPrefabInstance*)inst_p;
    if (!inst || !g_scene || !child_name) return;
    PrefabChild* ch = find_child(inst_p, child_name);
    if (!ch || ch->type != CHILD_TEXT_INPUT) return;
    if (g_scene->focused_input_inst && g_scene->focused_input_child_idx >= 0) {
        auto* old = g_scene->focused_input_inst;
        int32_t oi = g_scene->focused_input_child_idx;
        if (oi < (int32_t)old->children.size()) old->children[oi].focused = false;
    }
    for (size_t i = 0; i < inst->children.size(); ++i) {
        if (&inst->children[i] == ch) {
            g_scene->focused_input_inst = inst;
            g_scene->focused_input_child_idx = (int32_t)i;
            ch->focused = true;
            ch->sel_anchor = ch->cursor_pos;   /* collapsed cursor */
            break;
        }
    }
}

/* Pin a textbox / scrollable container to its bottom (newest line / last row).
 * Sets an out-of-range offset that render clamps to max, and re-arms textbox
 * tail-follow so subsequent growth keeps sticking to the bottom. */
void aiml_prefab_scroll_to_end(void* inst_p, const char* child_name) {
    PrefabChild* ch = find_child(inst_p, child_name);
    if (!ch) return;
    ch->scroll_offset = 1 << 30;   /* render clamps to max_off */
    ch->tb_prev_max   = -1;        /* textbox: re-arm tail-follow */
}

void aiml_prefab_set_texture(void* inst, const char* child_name, void* tex) {
    PrefabChild* ch = find_child(inst, child_name);
    if (!ch || !tex) return;
    GLuint id = ((AimlGLTexture*)tex)->id;
    ch->texture = id;
}

int32_t aiml_prefab_dropdown_index(void* inst, const char* child_name) {
    PrefabChild* ch = find_child(inst, child_name);
    return ch ? ch->selected_index : 0;
}

int32_t aiml_prefab_dropdown_enum_value(void* inst, const char* child_name) {
    PrefabChild* ch = find_child(inst, child_name);
    if (!ch || ch->enum_type.empty()) return -1;
    return aiml_prefab_enum_value_at(ch->enum_type.c_str(), ch->selected_index);
}

/* ── Widget↔model binding (spec 44.6 §5d) ─────────────────────────────────
 * A prefab declaring `binds_model: T` attaches one T instance via bind_model;
 * each child the compiler resolved carries (bind_offset, bind_tag).  The two
 * sync calls move each bound field to/from its widget's existing state. */

/* enum member value → option index (reverse of aiml_prefab_enum_value_at). */
static int enum_index_of_value(const std::string& enum_type, int value) {
    auto it = g_enum_values.find(enum_type);
    if (it == g_enum_values.end()) return -1;
    const std::vector<int>& vals = it->second;
    for (size_t i = 0; i < vals.size(); ++i)
        if (vals[i] == value) return (int)i;
    return -1;
}

void aiml_prefab_bind_model(void* inst_ptr, void* model_ptr) {
    auto* inst = static_cast<AimlPrefabInstance*>(inst_ptr);
    if (inst) inst->bound_model = model_ptr;
}

void aiml_prefab_sync_from_model(void* inst_ptr) {
    auto* inst = static_cast<AimlPrefabInstance*>(inst_ptr);
    if (!inst || !inst->bound_model) return;
    uint8_t* base = static_cast<uint8_t*>(inst->bound_model);
    for (auto& ch : inst->children) {
        if (ch.bind_tag < 0 || ch.bind_offset < 0) continue;
        uint8_t* fld = base + ch.bind_offset;
        if (ch.bind_tag == 0) {            /* i32 → slider/bar */
            float v = (float)(*reinterpret_cast<int32_t*>(fld));
            if (v < ch.slider_min) v = ch.slider_min;
            if (v > ch.bar_max)    v = ch.bar_max;
            ch.bar_value = v;
        } else if (ch.bind_tag == 1) {     /* f32 → slider/bar */
            float v = *reinterpret_cast<float*>(fld);
            if (v < ch.slider_min) v = ch.slider_min;
            if (v > ch.bar_max)    v = ch.bar_max;
            ch.bar_value = v;
        } else if (ch.bind_tag == 3) {     /* enum → dropdown */
            int idx = enum_index_of_value(ch.enum_type, *reinterpret_cast<int32_t*>(fld));
            ch.selected_index = idx >= 0 ? idx : 0;
        }
        /* bind_tag == 2 (string): Phase 2 */
    }
}

void aiml_prefab_sync_to_model(void* inst_ptr) {
    auto* inst = static_cast<AimlPrefabInstance*>(inst_ptr);
    if (!inst || !inst->bound_model) return;
    uint8_t* base = static_cast<uint8_t*>(inst->bound_model);
    for (auto& ch : inst->children) {
        if (ch.bind_tag < 0 || ch.bind_offset < 0) continue;
        uint8_t* fld = base + ch.bind_offset;
        if (ch.bind_tag == 0) {            /* slider/bar → i32 (round to nearest) */
            float v = ch.bar_value;
            *reinterpret_cast<int32_t*>(fld) = (int32_t)(v + (v >= 0.0f ? 0.5f : -0.5f));
        } else if (ch.bind_tag == 1) {     /* slider/bar → f32 */
            *reinterpret_cast<float*>(fld) = ch.bar_value;
        } else if (ch.bind_tag == 3) {     /* dropdown → enum */
            int ev = aiml_prefab_enum_value_at(ch.enum_type.c_str(), ch.selected_index);
            *reinterpret_cast<int32_t*>(fld) = ev >= 0 ? ev : 0;
        }
        /* bind_tag == 2 (string): Phase 2 */
    }
}

void aiml_prefab_set_dropdown_options(void* inst, const char* child_name, const char* options) {
    PrefabChild* ch = find_child(inst, child_name);
    if (!ch) return;
    std::string prev = ch->options;
    ch->options = options ? options : "";
    /* Options changed → reset selected_index + scroll_offset */
    if (prev != ch->options) {
        ch->selected_index = 0;
        ch->scroll_offset  = 0;
    }
}

void aiml_prefab_set_dropdown_index(void* inst, const char* child_name, int32_t idx) {
    PrefabChild* ch = find_child(inst, child_name);
    if (!ch) return;
    int n_opts = ch->options.empty() ? 0 : 1;
    for (char c : ch->options) if (c == '|') n_opts++;
    if (n_opts <= 0) { ch->selected_index = 0; return; }
    if (idx < 0) idx = 0;
    if (idx >= n_opts) idx = n_opts - 1;
    ch->selected_index = idx;
}

void aiml_prefab_set_dropdown_selected(void* inst, const char* child_name, const char* value) {
    PrefabChild* ch = find_child(inst, child_name);
    if (!ch || ch->options.empty() || !value) return;
    std::string target = value;
    int idx = 0;
    size_t start = 0;
    for (size_t p = 0; p <= ch->options.size(); ++p) {
        if (p == ch->options.size() || ch->options[p] == '|') {
            if (ch->options.compare(start, p - start, target) == 0) {
                ch->selected_index = idx;
                return;
            }
            idx++;
            start = p + 1;
        }
    }
    /* Not found — reset to 0 so a stale selection doesn't leak across
     * page-switches.  Previously we kept the current index, which meant
     * "load stored value 'X'" was a no-op when 'X' wasn't in the current
     * options — the widget kept showing whatever was there before. */
    ch->selected_index = 0;
}

extern "C" char* aiml_str_dup(const char* s);   // RC-managed dup (string_runtime); see below

const char* aiml_prefab_dropdown_value(void* inst, const char* child_name) {
    /* spec/61 — return an RC-managed copy (aiml_str_dup, NOT strdup): AIML strings carry a
     * refcount header, and the compiler now decref's the string local this fills (an owned-string
     * builtin). A raw strdup would have no header, so the decref would corrupt the heap. */
    PrefabChild* ch = find_child(inst, child_name);
    if (!ch || ch->options.empty()) return aiml_str_dup("");
    int idx = 0;
    size_t start = 0;
    for (size_t p = 0; p <= ch->options.size(); ++p) {
        if (p == ch->options.size() || ch->options[p] == '|') {
            if (idx == ch->selected_index) {
                std::string cur(ch->options, start, p - start);
                return aiml_str_dup(cur.c_str());
            }
            idx++;
            start = p + 1;
        }
    }
    return aiml_str_dup("");
}

void aiml_prefab_set_color(void* inst, const char* child_name, int32_t color) {
    PrefabChild* ch = find_child(inst, child_name);
    if (ch) ch->color = (uint32_t)color;
}

/* Generic child fill/border color setters (mirror set_color).  For a rect the
 * visible fill is bg_color (color is usually 0) — needed to flash a whole panel
 * (e.g. the Soulmage banner on hit/heal). */
/* spec/44_6 §5g: set colour runs — flat triples (start, len, rgba) over
 * the TEXT bytes of the field.  A run that extends past the end of the text
 * is clipped when drawing instead of rejected: a highlighter that lags
 * typing by one frame must not be an error. */
void aiml_prefab_set_color_runs(void* inst, const char* child_name,
                                const void* triples, int32_t count) {
    PrefabChild* ch = find_child(inst, child_name);
    if (!ch) return;
    ch->color_runs.clear();
    const int32_t* src = static_cast<const int32_t*>(triples);
    if (!src || count <= 0) return;
    int32_t n = (count / 3) * 3;
    ch->color_runs.assign(src, src + n);
}

void aiml_prefab_clear_color_runs(void* inst, const char* child_name) {
    PrefabChild* ch = find_child(inst, child_name);
    if (ch) ch->color_runs.clear();
}

void aiml_prefab_set_bg_color(void* inst, const char* child_name, int32_t color) {
    PrefabChild* ch = find_child(inst, child_name);
    if (ch) ch->bg_color = (uint32_t)color;
}

void aiml_prefab_set_border_color(void* inst, const char* child_name, int32_t color) {
    PrefabChild* ch = find_child(inst, child_name);
    if (ch) ch->border_color = (uint32_t)color;
}

void aiml_prefab_set_child_visible(void* inst, const char* child_name, int32_t v) {
    PrefabChild* ch = find_child(inst, child_name);
    if (ch) ch->visible = (v != 0);
}

void aiml_prefab_set_child_font_size(void* inst, const char* child_name, float size_px) {
    PrefabChild* ch = find_child(inst, child_name);
    if (ch) ch->font_size = size_px;
}

/* Toggle compacting layout on a container child at runtime.  See the
 * `compact` doc-comment on PrefabChildDef / container_sync.  Same effect as
 * `compact: true` in the prefab definition, but settable per instance. */
void aiml_prefab_container_set_compact(void* inst, const char* child_name, int32_t v) {
    PrefabChild* ch = find_child(inst, child_name);
    if (ch) ch->compact = (v != 0);
}

/* Reach into a container's spawned rows.  For a container widget bound via
 * bind_list to a list<Model> (or list<i32>), the runtime spawns one row per
 * element; these helpers let AIML code iterate them and call the standard
 * set_text / set_texture / set_dropdown_index / … on individual rows. */
int32_t aiml_prefab_container_row_count(void* inst, const char* child_name) {
    PrefabChild* ch = find_child(inst, child_name);
    if (!ch) return 0;
    return (int32_t)ch->spawned.size();
}
void* aiml_prefab_container_row(void* inst, const char* child_name, int32_t idx) {
    PrefabChild* ch = find_child(inst, child_name);
    if (!ch) return nullptr;
    if (idx < 0 || idx >= (int32_t)ch->spawned.size()) return nullptr;
    return (void*)ch->spawned[idx];
}

/* Bar/gauge: update the current value (fill = value/max of width). */
void aiml_prefab_set_bar_value(void* inst, const char* child_name, float value) {
    PrefabChild* ch = find_child(inst, child_name);
    if (ch) ch->bar_value = value;
}
void aiml_prefab_set_bar_max(void* inst, const char* child_name, float max_value) {
    PrefabChild* ch = find_child(inst, child_name);
    if (ch) ch->bar_max = max_value;
}
void aiml_prefab_set_bar_fill(void* inst, const char* child_name, int32_t color) {
    PrefabChild* ch = find_child(inst, child_name);
    if (ch) ch->fill_color = (uint32_t)color;
}

/* Slider: read/write current value and bounds. Same value/max as bar;
 * additionally a slider_min lower bound (bar always assumes min=0).
 * Setters clamp the resulting value into [min, max]. */
float aiml_prefab_get_slider_value(void* inst, const char* child_name) {
    PrefabChild* ch = find_child(inst, child_name);
    return ch ? ch->bar_value : 0.0f;
}
void aiml_prefab_set_slider_value(void* inst, const char* child_name, float value) {
    PrefabChild* ch = find_child(inst, child_name);
    if (!ch) return;
    if (value < ch->slider_min) value = ch->slider_min;
    if (value > ch->bar_max)    value = ch->bar_max;
    ch->bar_value = value;
}
void aiml_prefab_set_slider_min(void* inst, const char* child_name, float min_value) {
    PrefabChild* ch = find_child(inst, child_name);
    if (ch) ch->slider_min = min_value;
}
void aiml_prefab_set_slider_max(void* inst, const char* child_name, float max_value) {
    PrefabChild* ch = find_child(inst, child_name);
    if (ch) ch->bar_max = max_value;
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

/* Phase 6f: register a cascade type from its compile-time JSON blob.
 * See prefab_runtime.h for the expected shape. */
void aiml_cascade_persist_row(void* outer_ptr, int32_t bound_index,
                              int32_t tag,
                              const int32_t* values, int32_t n_values)
{
    if (!outer_ptr || bound_index < 0) return;
    if (n_values < 0) n_values = 0;
    if (n_values > 0 && !values) return;
    struct ListHdr { int32_t size; int32_t capacity; void* data; };
    ListHdr* outer = (ListHdr*)outer_ptr;
    if (bound_index >= outer->size) return;
    // spec/61 §5.7 (memV3, the default): a `list<list<i32>>` stores its inner lists as refcounted
    // HANDLE pointers (target-pointer stride), NOT inline {size,cap,data} structs — so index at
    // pointer stride and dereference to reach the inner list. (Reading it as an inline-struct array
    // used a 16-byte stride and no deref → wrong address → crash, test_cascade_persist_min.)
    ListHdr* inner = *(ListHdr**)((char*)outer->data + (size_t)bound_index * sizeof(void*));
    if (!inner) return;
    /* Grow the row buffer to fit tag + N payload slots. */
    int32_t need = 1 + n_values;
    if (need > inner->capacity) {
        int32_t newcap = need < 4 ? 4 : need;
        void* nd = realloc(inner->data, (size_t)newcap * sizeof(int32_t));
        if (!nd) return;
        inner->data     = nd;
        inner->capacity = newcap;
    }
    int32_t* wire = (int32_t*)inner->data;
    /* Zero any newly-exposed slots between old size and new size. */
    for (int32_t i = inner->size; i < need; ++i) wire[i] = 0;
    inner->size = need;
    /* Stamp tag + payload. */
    wire[0] = tag;
    for (int32_t f = 0; f < n_values; ++f) wire[1 + f] = values[f];
}

void aiml_prefab_register_cascade(const char* name, const char* nodes_json,
                                  const char* opcode_enum) {
    if (!name || !nodes_json) return;
    CascadeMeta m;
    m.name = name;
    m.opcode_enum = opcode_enum ? opcode_enum : "";

    const char* p = nodes_json;
    auto skip_ws = [&]() { while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++; };
    auto parse_str = [&]() -> std::string {
        skip_ws();
        if (*p != '"') return "";
        p++;
        std::string s;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) {
                char c = p[1];
                if      (c == 'n')  s += '\n';
                else if (c == 't')  s += '\t';
                else if (c == 'r')  s += '\r';
                else if (c == '"')  s += '"';
                else if (c == '\\') s += '\\';
                else                s += c;
                p += 2;
            } else s += *p++;
        }
        if (*p == '"') p++;
        return s;
    };
    auto parse_int = [&]() -> int {
        skip_ws();
        int sign = 1;
        if (*p == '-') { sign = -1; p++; }
        int v = 0; while (*p >= '0' && *p <= '9') { v = v*10 + (*p - '0'); p++; }
        return sign * v;
    };

    skip_ws();
    if (*p != '[') return;
    p++;
    while (*p) {
        skip_ws();
        if (*p == ']') { p++; break; }
        if (*p == ',') { p++; continue; }
        if (*p != '{') break;
        p++; /* node object */
        CascadeNodeMeta n;
        while (*p && *p != '}') {
            skip_ws();
            if (*p == ',') { p++; continue; }
            std::string key = parse_str();
            skip_ws();
            if (*p == ':') { p++; skip_ws(); }
            if      (key == "name")  n.name  = parse_str();
            else if (key == "label") n.label = parse_str();
            else if (key == "id")    n.id    = parse_int();
            else if (key == "fields") {
                if (*p == '[') {
                    p++;
                    while (*p && *p != ']') {
                        skip_ws();
                        if (*p == ',') { p++; continue; }
                        if (*p != '{') break;
                        p++;
                        CascadeFieldMeta f;
                        while (*p && *p != '}') {
                            skip_ws();
                            if (*p == ',') { p++; continue; }
                            std::string k2 = parse_str();
                            skip_ws();
                            if (*p == ':') { p++; skip_ws(); }
                            if      (k2 == "name")    f.name          = parse_str();
                            else if (k2 == "type")    f.type          = parse_str();
                            else if (k2 == "default") f.default_value = parse_str();
                            else                      parse_str();
                        }
                        if (*p == '}') p++;
                        n.fields.push_back(std::move(f));
                    }
                    if (*p == ']') p++;
                }
            } else {
                /* skip unknown value (string or number) */
                skip_ws();
                if (*p == '"') parse_str();
                else if (*p == '[' || *p == '{') {
                    /* skip balanced */
                    int depth = 0; char open = *p, close = (open == '[') ? ']' : '}';
                    do {
                        if (*p == open)  depth++;
                        if (*p == close) depth--;
                        p++;
                    } while (*p && depth > 0);
                } else parse_int();
            }
        }
        if (*p == '}') p++;
        if (n.label.empty()) n.label = derive_label_from_name(n.name);
        m.nodes.push_back(std::move(n));
    }

    /* Build derived caches */
    for (size_t i = 0; i < m.nodes.size(); ++i) {
        if (i) m.options_cache += "|";
        m.options_cache += m.nodes[i].label;
        if (m.nodes[i].id >= 0) m.id_to_opt_idx[m.nodes[i].id] = (int)i;
    }
    g_cascades[m.name] = std::move(m);
}

void aiml_prefab_register_enum(const char* name, const char* labels_pipe,
                               const char* values_pipe) {
    if (!name || !labels_pipe) return;
    g_enums[name] = labels_pipe;
    /* Parse the parallel "0|1|2" values into a vector so widgets bound to this
     * enum can map selected-index → true member value.  Missing/short
     * values_pipe leaves the vector empty (callers fall back gracefully). */
    std::vector<int>& vals = g_enum_values[name];
    vals.clear();
    if (values_pipe && *values_pipe) {
        const char* p = values_pipe;
        while (*p) {
            char* end = nullptr;
            long v = strtol(p, &end, 10);
            if (end == p) break;          /* no digits — stop */
            vals.push_back((int)v);
            p = end;
            if (*p == '|') ++p;           /* skip separator */
        }
    }
}
const char* aiml_prefab_enum_labels(const char* name) {
    static const char kEmpty[] = "";
    if (!name) return kEmpty;
    auto it = g_enums.find(name);
    if (it == g_enums.end()) return kEmpty;
    return it->second.c_str();
}
int32_t aiml_prefab_enum_value_at(const char* name, int32_t opt_idx) {
    if (!name || opt_idx < 0) return -1;
    auto it = g_enum_values.find(name);
    if (it == g_enum_values.end()) return -1;
    const std::vector<int>& vals = it->second;
    if (opt_idx >= (int32_t)vals.size()) return -1;
    return (int32_t)vals[opt_idx];
}

const char* aiml_prefab_cascade_options(const char* cascade_name) {
    static const char kEmpty[] = "";
    if (!cascade_name) return kEmpty;
    auto it = g_cascades.find(cascade_name);
    if (it == g_cascades.end()) return kEmpty;
    return it->second.options_cache.c_str();
}
int32_t aiml_prefab_cascade_node_id_at(const char* cascade_name, int32_t opt_idx) {
    if (!cascade_name) return -1;
    auto it = g_cascades.find(cascade_name);
    if (it == g_cascades.end()) return -1;
    if (opt_idx < 0 || opt_idx >= (int)it->second.nodes.size()) return -1;
    return it->second.nodes[opt_idx].id;
}
int32_t aiml_prefab_cascade_opt_idx_for(const char* cascade_name, int32_t node_id) {
    if (!cascade_name) return -1;
    auto it = g_cascades.find(cascade_name);
    if (it == g_cascades.end()) return -1;
    auto it2 = it->second.id_to_opt_idx.find(node_id);
    if (it2 == it->second.id_to_opt_idx.end()) return -1;
    return it2->second;
}
int32_t aiml_prefab_cascade_field_count(const char* cascade_name, int32_t node_id) {
    if (!cascade_name) return -1;
    auto it = g_cascades.find(cascade_name);
    if (it == g_cascades.end()) return -1;
    auto it2 = it->second.id_to_opt_idx.find(node_id);
    if (it2 == it->second.id_to_opt_idx.end()) return -1;
    return (int32_t)it->second.nodes[it2->second].fields.size();
}
const char* aiml_prefab_cascade_field_type(const char* cascade_name, int32_t node_id, int32_t field_idx) {
    static const char kEmpty[] = "";
    if (!cascade_name) return kEmpty;
    auto it = g_cascades.find(cascade_name);
    if (it == g_cascades.end()) return kEmpty;
    auto it2 = it->second.id_to_opt_idx.find(node_id);
    if (it2 == it->second.id_to_opt_idx.end()) return kEmpty;
    const auto& n = it->second.nodes[it2->second];
    if (field_idx < 0 || field_idx >= (int)n.fields.size()) return kEmpty;
    return n.fields[field_idx].type.c_str();
}
const char* aiml_prefab_cascade_field_label(const char* cascade_name, int32_t node_id, int32_t field_idx) {
    static const char kEmpty[] = "";
    if (!cascade_name) return kEmpty;
    auto it = g_cascades.find(cascade_name);
    if (it == g_cascades.end()) return kEmpty;
    auto it2 = it->second.id_to_opt_idx.find(node_id);
    if (it2 == it->second.id_to_opt_idx.end()) return kEmpty;
    const auto& n = it->second.nodes[it2->second];
    if (field_idx < 0 || field_idx >= (int)n.fields.size()) return kEmpty;
    const auto& f = n.fields[field_idx];
    /* Prefer explicit name, else derive from type. */
    return f.name.empty() ? f.type.c_str() : f.name.c_str();
}

/* ── Cascade validation (spec/44_7_cascade_validate.md) ────────────────────
 * Verify that a `list<i32>` row is a well-formed instance of a cascade: the
 * first int is a node id, followed by that node's typed payload fields.  A
 * field type may be another cascade (recurse), an enum (value must be a valid
 * member) or a numeric alias/i32 (any value).  Returns the next index on
 * success; on failure sets *bad to the offending index and returns -1, filling
 * *reason when non-null.  Uses g_cascades + g_enum_values (both populated at
 * program start for every declared cascade/enum → works headless). */
static int aiml_casc_validate_from(const std::string& casc,
                                   const int32_t* row, int len, int pos,
                                   int* bad, std::string* reason) {
    auto it = g_cascades.find(casc);
    if (it == g_cascades.end()) {
        *bad = pos; if (reason) *reason = "unknown cascade '" + casc + "'"; return -1;
    }
    if (pos >= len) {
        *bad = pos;
        if (reason) *reason = "pos " + std::to_string(pos) + ": expected a node id of '" + casc + "', row truncated";
        return -1;
    }
    int32_t node_id = row[pos];
    auto idIt = it->second.id_to_opt_idx.find(node_id);
    if (idIt == it->second.id_to_opt_idx.end()) {
        *bad = pos;
        if (reason) *reason = "pos " + std::to_string(pos) + ": " + std::to_string(node_id) +
                              " is not a node id of cascade '" + casc + "'";
        return -1;
    }
    const CascadeNodeMeta& node = it->second.nodes[idIt->second];
    pos++;  /* consumed the node id */
    for (const auto& fld : node.fields) {
        const std::string& ft = fld.type;
        std::string fname = fld.name.empty() ? ft : fld.name;
        if (g_cascades.count(ft)) {
            /* nested cascade → recurse in place */
            pos = aiml_casc_validate_from(ft, row, len, pos, bad, reason);
            if (pos < 0) return -1;
        } else if (g_enum_values.count(ft)) {
            if (pos >= len) {
                *bad = pos;
                if (reason) *reason = "pos " + std::to_string(pos) + ": node '" + node.name +
                                      "' field '" + fname + "' (" + ft + ") missing — row truncated";
                return -1;
            }
            int32_t v = row[pos];
            const std::vector<int>& vals = g_enum_values[ft];
            if (std::find(vals.begin(), vals.end(), (int)v) == vals.end()) {
                *bad = pos;
                if (reason) *reason = "pos " + std::to_string(pos) + ": node '" + node.name +
                                      "' field '" + fname + "' — value " + std::to_string(v) +
                                      " is not a valid " + ft;
                return -1;
            }
            pos++;
        } else {
            /* numeric alias / i32 → any value, but it must be present */
            if (pos >= len) {
                *bad = pos;
                if (reason) *reason = "pos " + std::to_string(pos) + ": node '" + node.name +
                                      "' field '" + fname + "' (" + ft + ") missing — row truncated";
                return -1;
            }
            pos++;
        }
    }
    return pos;
}

int32_t aiml_cascade_validate(const char* cascade_name, const void* row_data, int32_t len) {
    if (!cascade_name) return -2;
    std::string casc(cascade_name);
    if (!g_cascades.count(casc)) return -2;
    const int32_t* row = static_cast<const int32_t*>(row_data);
    int bad = -1;
    int end = aiml_casc_validate_from(casc, row, (int)len, 0, &bad, nullptr);
    if (end < 0) return bad;         /* structural error at `bad` */
    if (end != (int)len) return end; /* trailing garbage after a complete node */
    return -1;                       /* valid */
}

const char* aiml_cascade_explain(const char* cascade_name, const void* row_data, int32_t len) {
    static thread_local std::string buf;
    buf.clear();
    if (!cascade_name) { buf = "null cascade name"; return buf.c_str(); }
    std::string casc(cascade_name);
    if (!g_cascades.count(casc)) { buf = "unknown cascade '" + casc + "'"; return buf.c_str(); }
    const int32_t* row = static_cast<const int32_t*>(row_data);
    int bad = -1;
    int end = aiml_casc_validate_from(casc, row, (int)len, 0, &bad, &buf);
    if (end < 0) return buf.c_str();
    if (end != (int)len) {
        buf = "pos " + std::to_string(end) + ": trailing element(s) after a complete '" + casc + "' node";
        return buf.c_str();
    }
    buf.clear();  /* valid → empty reason */
    return buf.c_str();
}

/* ── Cascade field lookup (spec/44_7) ──────────────────────────────────────
 * Walk the node selected by row[pos], consuming its fields exactly like
 * validate (nested cascade → recurse; enum/alias → one slot), and collect the
 * row index of EVERY field whose declared type == `field_type`.  A type may
 * occur several times in one node (e.g. revive: [47, OwnerRef, OwnerRef, …]),
 * so all positions are returned, in declaration/row order.  Returns the next
 * index (advancing) or -1 if the row can't be walked (truncated / bad node id
 * / unknown nested cascade); matches collected so far are kept regardless. */
static int aiml_casc_collect_field_from(const std::string& casc,
                                        const int32_t* row, int len, int pos,
                                        const std::string& field_type,
                                        std::vector<int>& out) {
    auto it = g_cascades.find(casc);
    if (it == g_cascades.end()) return -1;
    if (pos >= len) return -1;
    int32_t node_id = row[pos];
    auto idIt = it->second.id_to_opt_idx.find(node_id);
    if (idIt == it->second.id_to_opt_idx.end()) return -1;
    const CascadeNodeMeta& node = it->second.nodes[idIt->second];
    pos++;  /* consumed the node id */
    for (const auto& fld : node.fields) {
        const std::string& ft = fld.type;
        if (ft == field_type) out.push_back(pos);
        if (g_cascades.count(ft)) {
            pos = aiml_casc_collect_field_from(ft, row, len, pos, field_type, out);
            if (pos < 0) return -1;
        } else {
            if (pos >= len) return -1;   /* payload slot missing — row truncated */
            pos++;
        }
    }
    return pos;
}

/* i32-list layout mirrors GenericList: {i32 size, i32 capacity, int32_t* data}.
 * (list<i32> stores elements inline as a contiguous int32 array, so `data`
 * reads as int32_t* — same buffer validate walks.) */
struct AimlI32ListRaw { int32_t size; int32_t capacity; int32_t* data; };
static void aiml_i32list_push(AimlI32ListRaw* l, int32_t v) {
    if (l->size >= l->capacity) {
        int newCap = l->capacity > 0 ? l->capacity * 2 : 4;
        int32_t* nd = static_cast<int32_t*>(std::realloc(l->data, (size_t)newCap * sizeof(int32_t)));
        if (!nd) return;
        l->data = nd; l->capacity = newCap;
    }
    l->data[l->size++] = v;
}

/* Fill `out_list` (a list<i32>) with the row indices of every `field_type`
 * field in the node `row` selects.  Returns the count (>=0), or -2 unknown
 * cascade.  An empty result means the field does not occur in that node. */
int32_t aiml_cascade_field_indices(const char* cascade_name, const void* row_data,
                                   int32_t len, const char* field_type, void* out_list) {
    if (!cascade_name || !field_type || !out_list) return -2;
    std::string casc(cascade_name);
    if (!g_cascades.count(casc)) return -2;
    const int32_t* row = static_cast<const int32_t*>(row_data);
    std::vector<int> found;
    aiml_casc_collect_field_from(casc, row, (int)len, 0, std::string(field_type), found);
    AimlI32ListRaw* out = static_cast<AimlI32ListRaw*>(out_list);
    for (int p : found) aiml_i32list_push(out, p);
    return (int32_t)found.size();
}

/* ── Cascade full enumeration (spec 44_7 — smoke-test corpus) ─────────────────
 * Enumerate EVERY possible row of a cascade: opcodes × enum-members × nested
 * sub-opcodes.  A value/alias field contributes exactly ONE representative
 * (its `default`, else 1) — numbers do not branch text/execution and would be
 * unbounded.  Lazy random access via mixed-radix ranking (no materialisation):
 *   aiml_cascade_row_count(name, start_op, end_op)         -> i64 total rows
 *   aiml_cascade_row_at   (name, k, start_op, end_op, out) -> 0 ok / -1 k-range
 *                                                             / -2 unknown
 * start_op/end_op bound which TOP-LEVEL opcode ids participate (kCascOpMin /
 * kCascOpMax = unbounded); nested cascades always enumerate all their nodes. */
static const int64_t kCascI64Max = 0x7fffffffffffffffLL;
static const int32_t kCascOpMin  = (-2147483647 - 1);
static const int32_t kCascOpMax  = 2147483647;

static int64_t casc_sat_mul(int64_t a, int64_t b) {
    if (a == 0 || b == 0) return 0;
    if (a > kCascI64Max / b) return kCascI64Max;   /* saturate, don't wrap */
    return a * b;
}
static int64_t casc_sat_add(int64_t a, int64_t b) {
    if (a > kCascI64Max - b) return kCascI64Max;
    return a + b;
}

/* Number of representative values a field of `type` yields:
 *   enum    -> member count ; cascade -> Σ over nodes of Π field-domains ;
 *   else (value/alias/i32) -> 1.  Memoised; `active` guards cascade cycles. */
static int64_t casc_type_domain(const std::string& type,
                                std::unordered_map<std::string,int64_t>& memo,
                                std::set<std::string>& active) {
    auto e = g_enum_values.find(type);
    if (e != g_enum_values.end() && !e->second.empty())
        return (int64_t)e->second.size();
    auto c = g_cascades.find(type);
    if (c != g_cascades.end()) {
        if (active.count(type)) return 1;                 /* cycle -> 1 repr */
        auto m = memo.find(type);
        if (m != memo.end()) return m->second;
        active.insert(type);
        int64_t sum = 0;
        for (const auto& n : c->second.nodes) {
            int64_t prod = 1;
            for (const auto& f : n.fields)
                prod = casc_sat_mul(prod, casc_type_domain(f.type, memo, active));
            sum = casc_sat_add(sum, prod);
        }
        active.erase(type);
        memo[type] = sum;
        return sum;
    }
    return 1;
}

static int32_t casc_field_repr(const CascadeFieldMeta& f) {
    if (!f.default_value.empty()) {
        char* end = nullptr;
        long v = strtol(f.default_value.c_str(), &end, 10);
        if (end != f.default_value.c_str()) return (int32_t)v;
    }
    return 1;
}

static void casc_decode_cascade(const std::string& name, int64_t k,
                                int32_t lo, int32_t hi,
                                std::unordered_map<std::string,int64_t>& memo,
                                AimlI32ListRaw* out);

/* Decode the k-th value of one field into `out` (recurses into cascades). */
static void casc_decode_field(const CascadeFieldMeta& f, int64_t k,
                              std::unordered_map<std::string,int64_t>& memo,
                              AimlI32ListRaw* out) {
    auto e = g_enum_values.find(f.type);
    if (e != g_enum_values.end() && !e->second.empty()) {
        int64_t idx = k % (int64_t)e->second.size();
        aiml_i32list_push(out, e->second[(size_t)idx]);
        return;
    }
    if (g_cascades.count(f.type)) {
        casc_decode_cascade(f.type, k, kCascOpMin, kCascOpMax, memo, out);
        return;
    }
    aiml_i32list_push(out, casc_field_repr(f));   /* value/alias representative */
}

/* Emit the k-th node-row of cascade `name`.  [lo,hi] filters top-level opcode
 * ids (nested calls pass the unbounded range). */
static void casc_decode_cascade(const std::string& name, int64_t k,
                                int32_t lo, int32_t hi,
                                std::unordered_map<std::string,int64_t>& memo,
                                AimlI32ListRaw* out) {
    auto c = g_cascades.find(name);
    if (c == g_cascades.end()) return;
    for (const auto& n : c->second.nodes) {
        if (n.id < lo || n.id > hi) continue;
        std::set<std::string> active;
        int64_t nodeSize = 1;
        for (const auto& f : n.fields) {
            active.clear();
            nodeSize = casc_sat_mul(nodeSize, casc_type_domain(f.type, memo, active));
        }
        if (k >= nodeSize) { k -= nodeSize; continue; }
        aiml_i32list_push(out, n.id);                    /* opcode */
        for (const auto& f : n.fields) {                 /* mixed-radix fields */
            active.clear();
            int64_t d = casc_type_domain(f.type, memo, active);
            int64_t sub = (d > 0) ? (k % d) : 0;
            k          = (d > 0) ? (k / d) : k;
            casc_decode_field(f, sub, memo, out);
        }
        return;
    }
}

extern "C" int64_t aiml_cascade_row_count(const char* cascade_name,
                                          int32_t start_op, int32_t end_op) {
    if (!cascade_name) return 0;
    auto c = g_cascades.find(cascade_name);
    if (c == g_cascades.end()) return 0;
    std::unordered_map<std::string,int64_t> memo;
    int64_t sum = 0;
    for (const auto& n : c->second.nodes) {
        if (n.id < start_op || n.id > end_op) continue;
        std::set<std::string> active;
        int64_t prod = 1;
        for (const auto& f : n.fields) {
            active.clear();
            prod = casc_sat_mul(prod, casc_type_domain(f.type, memo, active));
        }
        sum = casc_sat_add(sum, prod);
    }
    return sum;
}

extern "C" int32_t aiml_cascade_row_at(const char* cascade_name, int64_t k,
                                       int32_t start_op, int32_t end_op,
                                       void* out_list) {
    if (!cascade_name || !out_list) return -2;
    if (!g_cascades.count(cascade_name)) return -2;
    int64_t total = aiml_cascade_row_count(cascade_name, start_op, end_op);
    if (k < 0 || k >= total) return -1;
    std::unordered_map<std::string,int64_t> memo;
    casc_decode_cascade(cascade_name, k, start_op, end_op,
                        memo, static_cast<AimlI32ListRaw*>(out_list));
    return 0;
}

/* aiml_cascade_describe(name) -> string: schema description of a cascade —
 * per node (opcode) the field structure as `[values]TypeName`.  For enum fields
 * the member values, for nested cascade fields the sub-node IDs, for value/alias
 * empty.  One line per opcode.  Purely informative (review/documentation of the possible
 * data type combinations). */
static std::string casc_range_of(const std::string& type) {
    std::string s;
    auto e = g_enum_values.find(type);
    if (e != g_enum_values.end() && !e->second.empty()) {
        for (size_t i = 0; i < e->second.size(); ++i) { if (i) s += ","; s += std::to_string(e->second[i]); }
        return s;
    }
    auto c = g_cascades.find(type);
    if (c != g_cascades.end()) {
        bool first = true;
        for (const auto& n : c->second.nodes) { if (!first) s += ","; s += std::to_string(n.id); first = false; }
        return s;
    }
    return "";   /* value/alias/i32 → no enum range */
}
/* String-Listen-ABI (GenericList: {i32 size, i32 cap, void** data} mit
 * refcounted char*-Elementen; siehe string_runtime aiml_str_split_fill). */
extern "C" char* aiml_str_dup(const char* s);
struct AimlStrListRaw { int32_t size; int32_t capacity; void** data; };
static void casc_strlist_push(AimlStrListRaw* l, const std::string& s) {
    if (l->size >= l->capacity) {
        int nc = l->capacity > 0 ? l->capacity * 2 : 8;
        void** nd = static_cast<void**>(std::realloc(l->data, (size_t)nc * sizeof(void*)));
        if (!nd) return;
        l->data = nd; l->capacity = nc;
    }
    l->data[l->size++] = aiml_str_dup(s.c_str());
}

/* aiml_cascade_describe(cascade, opcodes, opc_len, out) → list<string>: the
 * possible data type combinations of the given opcodes (opc_len==0 → ALL).
 * Element 0 = opcode enum name (e.g. "EffectCode"); after that ONE line per opcode
 * "[id]node [range]Type [range]Type …" (enum→member values, nested cascade→sub-
 * node IDs).  Returns the number of elements. */
extern "C" int32_t aiml_cascade_describe(const char* cascade_name,
                                         const void* opcodes, int32_t opc_len,
                                         void* out_list) {
    if (!cascade_name || !out_list) return 0;
    AimlStrListRaw* out = static_cast<AimlStrListRaw*>(out_list);
    auto c = g_cascades.find(cascade_name);
    if (c == g_cascades.end()) return 0;
    const int32_t* ops = static_cast<const int32_t*>(opcodes);
    casc_strlist_push(out, c->second.opcode_enum);          /* Element 0: Opcode-Enum */
    for (const auto& n : c->second.nodes) {
        if (opc_len > 0 && ops) {                            /* filter on the selected opcodes */
            bool want = false;
            for (int i = 0; i < opc_len; ++i) if (ops[i] == n.id) { want = true; break; }
            if (!want) continue;
        }
        std::string line = "[" + std::to_string(n.id) + "]" + n.name;
        for (const auto& f : n.fields)
            line += " [" + casc_range_of(f.type) + "]" + f.type;
        casc_strlist_push(out, line);
    }
    return out->size;
}

/* aiml_cascade_type_range(type, out) → i32: the possible values of a field type
 * as a serialisable list<i32>.  enum → member values; nested cascade → sub-node
 * IDs; value/alias → empty.  Returns the count. */
extern "C" int32_t aiml_cascade_type_range(const char* type_name, void* out_list) {
    if (!type_name || !out_list) return 0;
    AimlI32ListRaw* out = static_cast<AimlI32ListRaw*>(out_list);
    std::string ty(type_name);
    auto e = g_enum_values.find(ty);
    if (e != g_enum_values.end()) {
        for (int v : e->second) aiml_i32list_push(out, v);
        return (int32_t)e->second.size();
    }
    auto c = g_cascades.find(ty);
    if (c != g_cascades.end()) {
        int32_t cnt = 0;
        for (const auto& n : c->second.nodes) { aiml_i32list_push(out, n.id); ++cnt; }
        return cnt;
    }
    return 0;
}

/* Phase 6: attach a bound list<list<i32>> to a prefab-instance.  Consumed
 * by CHILD_CONTAINER widgets whose `bind_list` names the list on the model. */
void aiml_prefab_bind_list(void* inst, void* list_ptr) {
    if (!inst) return;
    ((AimlPrefabInstance*)inst)->bound_list = list_ptr;
}
void aiml_prefab_child_bind_list(void* inst, const char* child_name, void* list_ptr) {
    PrefabChild* ch = find_child(inst, child_name);
    if (!ch) return;
    ch->bound_list_widget = list_ptr;
}
void* aiml_prefab_bound_list(void* inst) {
    if (!inst) return nullptr;
    return ((AimlPrefabInstance*)inst)->bound_list;
}
int32_t aiml_prefab_bound_index(void* inst) {
    if (!inst) return -1;
    return ((AimlPrefabInstance*)inst)->bound_index;
}

/* ── Connector (cable) rendering ─────────────────────────────────────────── */

/* Walk the parent chain and return the instance's WORLD position + total
 * accumulated scale.  Each parent contributes its own world position + scale,
 * and the child's local position is scaled (and scroll-shifted, for clip
 * containers) by the parent's accumulated scale. Mirrors the render-time
 * logic in render_instance() so cable endpoints (and bounds) match exactly
 * where nested children are drawn. */
static void inst_world_transform(AimlPrefabInstance* in,
                                 float& wx, float& wy,
                                 float& sx, float& sy) {
    float lx, ly; get_actual_pos(in, lx, ly);
    sx = in->scale_x;
    sy = in->scale_y;
    /* rel_scale only at top-level.  For nested instances the parent's psx
     * already carries it — folding it in again would double-scale, blowing
     * every child hitbox up to ~2× its drawn size and making dropdowns
     * appear to "hover" from far away.  Mirrors the render_instance /
     * get_actual_pos policy. */
    if (!in->parent) {
        float rsx, rsy; get_rel_scale(rsx, rsy); sx *= rsx; sy *= rsy;
    }
    if (in->parent) {
        float pwx, pwy, psx, psy;
        inst_world_transform(in->parent, pwx, pwy, psx, psy);
        wx = pwx + (lx - in->parent->scroll_x) * psx;
        wy = pwy + (ly - in->parent->scroll_y) * psy;
        sx *= psx;
        sy *= psy;
    } else {
        wx = lx;
        wy = ly;
    }
}

/* Resolve an instance's WORLD pixel bounds (top-left + size), accounting for
 * relative positioning, per-axis scale, and the full parent transform chain. */
static void inst_bounds(AimlPrefabInstance* in,
                        float& x, float& y, float& w, float& h) {
    float sx, sy;
    inst_world_transform(in, x, y, sx, sy);
    w = in->def->width * sx;
    h = in->def->height * sy;
}

/* Compute the anchor point where a cable meets `in` on its way to (tx,ty).
 * `idx`/`cnt` index this cable among all cables sharing the same edge (used by
 * the `distributed` anchor mode). */
static void connector_anchor_point(AimlPrefabInstance* in, float tx, float ty,
                                   int idx, int cnt, float& px, float& py) {
    float x, y, w, h; inst_bounds(in, x, y, w, h);
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    if (in->connector_anchor == 0) {           /* center */
        px = cx; py = cy; return;
    }
    /* Decide which edge faces the partner (dominant axis of the delta). */
    float dx = tx - cx, dy = ty - cy;
    bool horizontal = fabsf(dx) >= fabsf(dy);
    /* Position along the facing edge: 0.5 for `edge` mode, spread for `distributed`. */
    float t = 0.5f;
    if (in->connector_anchor == 2 && cnt > 1)
        t = (float)(idx + 1) / (float)(cnt + 1);
    if (horizontal) {
        px = (dx >= 0.0f) ? x + w : x;          /* right or left edge */
        py = y + h * t;
    } else {
        px = x + w * t;
        py = (dy >= 0.0f) ? y + h : y;          /* bottom or top edge */
    }
}

/* Per-instance counters for distributed anchoring (filled each frame). */
static void connector_count_for(AimlPrefabInstance* in, int& total, int& mine,
                                AimlConnector* self) {
    total = 0; mine = -1;
    if (in->connector_anchor != 2) return;      /* only distributed needs ordering */
    for (auto* c : g_scene->connectors) {
        if (!c->visible) continue;
        if (c->from == in || c->to == in) {
            if (c == self) mine = total;
            total++;
        }
    }
    if (mine < 0) mine = 0;
}

static inline uint32_t lerp_rgba(uint32_t a, uint32_t b, float t) {
    float ar = (a >> 24) & 0xFF, ag = (a >> 16) & 0xFF, ab = (a >> 8) & 0xFF, aa = a & 0xFF;
    float br = (b >> 24) & 0xFF, bg = (b >> 16) & 0xFF, bb = (b >> 8) & 0xFF, ba = b & 0xFF;
    uint32_t r = (uint32_t)(ar + (br - ar) * t);
    uint32_t g = (uint32_t)(ag + (bg - ag) * t);
    uint32_t bl= (uint32_t)(ab + (bb - ab) * t);
    uint32_t al= (uint32_t)(aa + (ba - aa) * t);
    return (r << 24) | (g << 16) | (bl << 8) | al;
}

/* Tessellate a quadratic Bézier (p0 → ctrl → p1) into a ribbon of width `w`,
 * pushing mode-4 verts (cross-UV in .y, length-UV in .x, per-vertex gradient
 * colour, highlight tint in border_rgba, edge-AA half-width in radius). */
static void push_cable(DrawBatch& batch, float x0, float y0, float x1, float y1,
                       float bow, float w, uint32_t c0, uint32_t c1, uint32_t hl)
{
    /* GL vertex colours expect ABGR byte order (little-endian); incoming
     * args are AIML's RRGGBBAA convention. Convert once before interpolation. */
    c0 = rgba32_to_gl(c0, 1.0f);
    c1 = rgba32_to_gl(c1, 1.0f);
    hl = rgba32_to_gl(hl, 1.0f);

    /* Overshoot both endpoints into their prefabs along the chord direction.
     * The ribbon end stands perpendicular to the curve tangent and would leave
     * a slanted gap against a horizontal/vertical prefab edge when the cable
     * meets it at an angle.  Extending past the anchor by ≥ half-width lets
     * the (higher-z) prefab cover that slanted edge cleanly. */
    {
        float dxe = x1 - x0, dye = y1 - y0;
        float dl  = sqrtf(dxe * dxe + dye * dye);
        if (dl > 1e-3f) {
            float ex = dxe / dl, ey = dye / dl;
            float overshoot = w * 0.5f + 2.0f;
            x0 -= ex * overshoot; y0 -= ey * overshoot;
            x1 += ex * overshoot; y1 += ey * overshoot;
        }
    }

    const int SEG = 24;
    float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
    /* Control point bowed perpendicular to the chord. */
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx*dx + dy*dy);
    float nx = 0.0f, ny = 0.0f;
    if (len > 1e-3f) { nx = -dy / len; ny = dx / len; }
    float cxp = mx + nx * bow * len;
    float cyp = my + ny * bow * len;
    float half = w * 0.5f;
    /* Edge anti-alias band in cross-units. cross goes -1..+1 over the full
     * width; the AA band corresponds to ~1 pixel along that axis. Slightly
     * narrower than 1/half to keep the cable solidly opaque except on the
     * outermost rim. */
    float aa = (half > 0.5f) ? (0.6f / half) : 0.3f;

    float prevx = 0, prevy = 0, ptanx = 0, ptany = 0;
    for (int i = 0; i <= SEG; ++i) {
        float t  = (float)i / (float)SEG;
        float omt = 1.0f - t;
        /* Quadratic Bézier point */
        float bxp = omt*omt*x0 + 2*omt*t*cxp + t*t*x1;
        float byp = omt*omt*y0 + 2*omt*t*cyp + t*t*y1;
        /* Tangent → normal */
        float tgx = 2*omt*(cxp - x0) + 2*t*(x1 - cxp);
        float tgy = 2*omt*(cyp - y0) + 2*t*(y1 - cyp);
        float tl = sqrtf(tgx*tgx + tgy*tgy);
        float n2x = 0, n2y = 0;
        if (tl > 1e-3f) { n2x = -tgy / tl; n2y = tgx / tl; }
        if (i > 0) {
            uint32_t cp = lerp_rgba(c0, c1, (float)(i-1) / (float)SEG);
            uint32_t cc = lerp_rgba(c0, c1, t);
            /* Quad between previous slice and this slice. */
            float pax = prevx + ptanx * half, pay = prevy + ptany * half; /* prev +n */
            float pbx = prevx - ptanx * half, pby = prevy - ptany * half; /* prev -n */
            float cax = bxp + n2x * half,    cay = byp + n2y * half;       /* cur +n */
            float cbx = bxp - n2x * half,    cby = byp - n2y * half;       /* cur -n */
            float up = (float)(i-1) / (float)SEG, uc = t;
            PrefabVertex pa = {pax, pay, up, 0.0f, cp, 0,0, aa, 0, hl};
            PrefabVertex pb = {pbx, pby, up, 1.0f, cp, 0,0, aa, 0, hl};
            PrefabVertex ca = {cax, cay, uc, 0.0f, cc, 0,0, aa, 0, hl};
            PrefabVertex cb = {cbx, cby, uc, 1.0f, cc, 0,0, aa, 0, hl};
            batch.verts.push_back(pa); batch.verts.push_back(pb); batch.verts.push_back(ca);
            batch.verts.push_back(pb); batch.verts.push_back(cb); batch.verts.push_back(ca);
        }
        prevx = bxp; prevy = byp; ptanx = n2x; ptany = n2y;
    }
}

/* Render a single cable + its optional endpoint ports at its current
 * endpoints (resolved live from both instances). */
static void render_one_connector(AimlConnector* c) {
    if (!c->visible || !c->from || !c->to) return;
    int tf=0, mf=0, tt=0, mt=0;
    connector_count_for(c->from, tf, mf, c);
    connector_count_for(c->to,   tt, mt, c);
    float fbx, fby, fbw, fbh; inst_bounds(c->from, fbx, fby, fbw, fbh);
    float tbx, tby, tbw, tbh; inst_bounds(c->to,   tbx, tby, tbw, tbh);
    float fcx = fbx + fbw*0.5f, fcy = fby + fbh*0.5f;
    float tcx = tbx + tbw*0.5f, tcy = tby + tbh*0.5f;
    float x0, y0, x1, y1;
    connector_anchor_point(c->from, tcx, tcy, mf, tf, x0, y0);
    connector_anchor_point(c->to,   fcx, fcy, mt, tt, x1, y1);

    DrawBatch cable; cable.tex = g_scene->white_tex; cable.mode = 4;
    push_cable(cable, x0, y0, x1, y1, c->bow, c->width, c->color0, c->color1, c->highlight);
    flush_batch(cable);

    if (c->port_tex != 0) {
        float ps = c->width * c->port_scale;
        DrawBatch pb; pb.tex = c->port_tex; pb.mode = 1;
        push_quad(pb, x0 - ps*0.5f, y0 - ps*0.5f, ps, ps, 0,0,1,1, 0xFFFFFFFFu);
        push_quad(pb, x1 - ps*0.5f, y1 - ps*0.5f, ps, ps, 0,0,1,1, 0xFFFFFFFFu);
        flush_batch(pb);
    }
}

/* ── Scene rendering ─────────────────────────────────────────────────────── */

/* spec/44_6 §8.1 — per-frame setup.  Runs on the FIRST draw call of a frame
 * (draw_scene or draw_scene_upto) so every partial draw of the same frame sees
 * ONE z-order and ONE u_time: a set_z between two partial draws must not
 * reshuffle what the earlier part already rendered. */
static void scene_frame_prologue(void) {
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
}

/* Render the instances and cables whose z_order lies in [zlo, zhi).
 *
 * spec/44_6 §8.1: the 2D GL state is re-asserted HERE, not once per frame.
 * Between two partial draws the caller may have drawn 3D — which means depth
 * test on and a depth buffer cleared.  Leaving that enabled would make the
 * remaining UI fail the depth test and vanish, so the engine takes the state
 * back for every window instead of trusting the caller to restore it. */
static void render_scene_window(int64_t zlo, int64_t zhi) {
    if (zhi <= zlo) return;

    /* Enable blending for alpha */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    /* Interleave cables with instances by z_order so each cable sits behind
     * the prefabs it visually belongs to (its z + 1) but in front of layers
     * below it.  Connectors default to z_order = 0; the AIML side picks a z
     * one less than the nodes it links. */
    std::vector<AimlConnector*> cs;
    cs.reserve(g_scene->connectors.size());
    for (auto* c : g_scene->connectors)
        if (c->visible && (int64_t)c->z_order >= zlo && (int64_t)c->z_order < zhi)
            cs.push_back(c);
    std::stable_sort(cs.begin(), cs.end(),
        [](AimlConnector* a, AimlConnector* b){ return a->z_order < b->z_order; });

    /* Snapshot the instance list — render_instance() may spawn new instances
     * (via container_sync / cascade_payload_sync) which push_back into
     * g_scene->instances and can invalidate the range-for iterator. */
    std::vector<AimlPrefabInstance*> render_targets;
    render_targets.reserve(g_scene->instances.size());
    for (auto* inst : g_scene->instances)
        if ((int64_t)inst->z_order >= zlo && (int64_t)inst->z_order < zhi)
            render_targets.push_back(inst);
    size_t ci = 0;
    for (auto* inst : render_targets) {
        if (inst->parent) continue;
        while (ci < cs.size() && cs[ci]->z_order < inst->z_order) {
            render_one_connector(cs[ci]); ++ci;
        }
        render_instance(inst);
    }
    while (ci < cs.size()) { render_one_connector(cs[ci]); ++ci; }
}

/* spec/44_6 §8.1 — draw only what lies BELOW z and leave the rest for the
 * closing draw_scene.  The hook for putting an own drawing (3D, raw GL) inside
 * the prefab z-order instead of under or over all of it. */
void aiml_prefab_draw_scene_upto(int32_t z) {
    if (!g_scene) return;
    if (g_scene->draw_z_floor == INT32_MIN) scene_frame_prologue();
    render_scene_window((int64_t)g_scene->draw_z_floor, (int64_t)z);
    /* Never lower the mark: a step backwards would redraw a layer, and with
     * alpha blending a layer drawn twice is visibly wrong, not merely slow. */
    if (z > g_scene->draw_z_floor) g_scene->draw_z_floor = z;
}

void aiml_prefab_draw_scene(void) {
    if (!g_scene) return;

    if (g_scene->draw_z_floor == INT32_MIN) scene_frame_prologue();
    render_scene_window((int64_t)g_scene->draw_z_floor, INT64_MAX);
    /* Frame is over: the mark drops back so the next frame draws everything. */
    g_scene->draw_z_floor = INT32_MIN;

    /* Second pass: render open dropdown popups above everything. */
    render_dropdown_popups();

    /* Phase 6f-2 sweep: retire any prefab-instance marked for delete by
     * container_sync / cascade_payload_sync earlier this frame.  Detach
     * from parent nesting, drop from the scene registry, then free.       */
    {
        std::vector<AimlPrefabInstance*> dead_list;
        for (auto* p : g_scene->instances)
            if (p && p->pending_delete) dead_list.push_back(p);
        for (auto* dead : dead_list) {
            if (dead->parent) {
                auto& sib = dead->parent->child_instances;
                sib.erase(std::remove(sib.begin(), sib.end(), dead), sib.end());
            }
            /* If this dead instance had its OWN spawned payload / rows,
             * mark them dead too so a follow-up frame cleans them up.
             * Critical: also clear their `parent` pointer BEFORE we free
             * this instance — otherwise next frame's sweep for `s` would
             * dereference `s->parent` which now points to freed memory. */
            for (auto& c : dead->children) {
                for (auto* s : c.spawned) {
                    if (!s) continue;
                    s->parent = nullptr;
                    s->pending_delete = true;
                }
                for (auto* s : c.spawned_payload) {
                    if (!s) continue;
                    s->parent = nullptr;
                    s->pending_delete = true;
                }
            }
            /* Same for any direct-nested children (add_child API) so the
             * chain doesn't dangle either. */
            for (auto* s : dead->child_instances) {
                if (!s) continue;
                s->parent = nullptr;
                s->pending_delete = true;
            }
            auto& all = g_scene->instances;
            all.erase(std::remove(all.begin(), all.end(), dead), all.end());
            scene_forget_instance(dead);
            delete dead;
        }
    }
}

static void child_screen_bbox(AimlPrefabInstance* inst, const PrefabChild& ch,
                              float& bx, float& by, float& bw, float& bh);

/* Compute the popup rect for a CHILD_DROPDOWN.  Anchored below the box by
 * default; flips above the box when the popup would overflow the bottom of
 * the screen (so dropdowns at the bottom edge open upward instead of being
 * clipped).  n_opts controls popup_h so the caller doesn't have to remember
 * the MAX_VIS / row_h math. */
static inline void compute_popup_rect(float bx, float by, float bw, float bh,
                                      int n_opts,
                                      float& popup_x, float& popup_y,
                                      float& popup_w, float& popup_h)
{
    const int MAX_VIS = 10;
    int visible = n_opts < MAX_VIS ? n_opts : MAX_VIS;
    if (visible < 1) visible = 1;
    float row_h = bh;
    popup_x = bx;
    popup_w = bw;
    popup_h = row_h * visible;
    float scr_h = g_scene ? (float)g_scene->screen_h : 0.0f;
    /* Below the box normally. Flip above only if it truly won't fit — and
     * only if flipping up actually gives more headroom. */
    if (scr_h > 0.0f && (by + bh + popup_h) > scr_h && by >= popup_h) {
        popup_y = by - popup_h;
    } else {
        popup_y = by + bh;
    }
}

static void render_dropdown_popups(void) {
    if (!g_scene) return;
    for (auto* inst : g_scene->instances) {
        if (!inst->visible) continue;
        for (auto& ch : inst->children) {
            if (ch.type != CHILD_DROPDOWN || !ch.dropdown_open) continue;
            if (!ch.visible) continue;
            float bx, by, bw, bh;
            child_screen_bbox(inst, ch, bx, by, bw, bh);
            int n_opts = ch.options.empty() ? 0 : 1;
            for (char c : ch.options) if (c == '|') n_opts++;
            if (n_opts <= 0) continue;
            const int MAX_VIS = 10;
            /* Clamp scroll_offset to the valid range, so that visible always
             * fills MIN(n_opts, MAX_VIS) rows (no empty rows at the end). */
            int max_scroll = n_opts - MAX_VIS; if (max_scroll < 0) max_scroll = 0;
            if (ch.scroll_offset > max_scroll) ch.scroll_offset = max_scroll;
            if (ch.scroll_offset < 0) ch.scroll_offset = 0;
            int visible = n_opts < MAX_VIS ? n_opts : MAX_VIS;
            float row_h = bh;
            float popup_x, popup_y, popup_w, popup_h;
            compute_popup_rect(bx, by, bw, bh, n_opts, popup_x, popup_y, popup_w, popup_h);
            float sx = 1.0f; { float rsx, rsy; get_rel_scale(rsx, rsy); sx *= rsx; }
            /* Popup-Hintergrund */
            {
                DrawBatch bg;
                bg.tex  = g_scene->white_tex;
                bg.mode = 3;
                uint32_t fg = rgba32_to_gl(0x0C1424FF, 1.0f);
                /* Border muted (formerly 0x88BBEE light blue) — now matches the
                 * dark popup and no longer draws attention away
                 * from the highlighted row. */
                uint32_t bd = rgba32_to_gl(0x4A6890FF, 1.0f);
                push_quad_sdf(bg, popup_x, popup_y, popup_w, popup_h, fg, bd,
                              ch.corner_radius * sx, 1.5f * sx);
                flush_batch(bg);
            }
            AimlUIFont* lf = nullptr;
            if (g_scene->font) {
                float target_size = ch.font_size > 0.0f ? ch.font_size * sx : 0.0f;
                lf = (target_size > 0.0f) ? font_for_size(g_scene, target_size) : g_scene->font;
            }
            std::vector<std::string> opts;
            size_t start = 0;
            for (size_t p = 0; p <= ch.options.size(); ++p) {
                if (p == ch.options.size() || ch.options[p] == '|') {
                    opts.push_back(ch.options.substr(start, p - start));
                    start = p + 1;
                }
            }
            float mx = aiml_input_mouse_pos_x();
            float my = aiml_input_mouse_pos_y();
            for (int r = 0; r < visible; ++r) {
                int idx = ch.scroll_offset + r;
                if (idx >= (int)opts.size()) break;
                float ry = popup_y + r * row_h;
                bool row_hot = (mx >= popup_x && mx < popup_x + popup_w
                             && my >= ry && my < ry + row_h);
                bool row_sel = (idx == ch.selected_index);
                uint32_t row_bg = row_hot ? 0x1E3554FF : (row_sel ? 0x162034FF : 0x0C1424FF);
                DrawBatch rb;
                rb.tex  = g_scene->white_tex;
                rb.mode = 0;
                /* row_bg is 0xRRGGBBAA — strip the alpha byte (shift 8),
                 * NOT mask the low 24 bits (which would drop RR and keep
                 * alpha as if it were B — the old code turned every row
                 * into knallblau because 0xXX__.__.FF became R,G,255). */
                uint32_t col = color_with_alpha(row_bg >> 8, 1.0f);
                /* Inset the first/last visible row by 1 px vertically so
                 * the outer popup border stays visible at top and bottom —
                 * otherwise the row fill overdraws the SDF stroke. */
                float row_y  = ry + (r == 0 ? 1.0f : 0.0f);
                float row_bh = row_h
                             - (r == 0 ? 1.0f : 0.0f)
                             - (r == visible - 1 ? 1.0f : 0.0f);
                push_quad(rb, popup_x + 1, row_y, popup_w - 2, row_bh, 0, 0, 1, 1, col);
                flush_batch(rb);
                if (lf) {
                    DrawBatch tb;
                    tb.tex  = lf->atlas_tex;
                    tb.mode = 2;
                    uint32_t lcol = rgba32_to_gl(0xFFFFFFFF, 1.0f);
                    float pad_l = 8.0f * sx;
                    float text_cy = ry + (row_h - lf->line_height) * 0.5f;
                    push_text_batch(tb, lf, opts[idx].c_str(),
                                    popup_x + pad_l, text_cy, popup_w - pad_l, lcol, "left");
                    flush_batch(tb);
                }
            }
            if (n_opts > visible) {
                float sb_w = 6.0f * sx;
                float sb_x = popup_x + popup_w - sb_w - 2.0f * sx;
                float track_y = popup_y + 2.0f * sx;
                float track_h = popup_h - 4.0f * sx;
                /* Track-Hintergrund */
                {
                    DrawBatch tb;
                    tb.tex = g_scene->white_tex; tb.mode = 0;
                    uint32_t tcol = color_with_alpha(0x101828, 0.6f);
                    push_quad(tb, sb_x, track_y, sb_w, track_h, 0, 0, 1, 1, tcol);
                    flush_batch(tb);
                }
                float thumb_h = track_h * (float)visible / (float)n_opts;
                if (thumb_h < 16.0f * sx) thumb_h = 16.0f * sx;
                float thumb_y = track_y
                              + (track_h - thumb_h) * (float)ch.scroll_offset
                                / (float)std::max(1, n_opts - visible);
                DrawBatch sb;
                sb.tex = g_scene->white_tex; sb.mode = 0;
                /* Thumb in the tone of the new dark palette (formerly 0x88BBEE
                 * light blue) — now sits with good contrast but calmly on the
                 * almost black popup. */
                uint32_t col = color_with_alpha(0x3A5578, 0.95f);
                push_quad(sb, sb_x, thumb_y, sb_w, thumb_h, 0, 0, 1, 1, col);
                flush_batch(sb);
            }
        }
    }
}

/* ── Hit testing ─────────────────────────────────────────────────────────── */

void* aiml_prefab_hit_test(float mx, float my) {
    if (!g_scene) return nullptr;

    /* Iterate the instance list backwards.  That is topmost-first ONLY because
     * prefab_draw_scene z-sorts this very list as a side effect — set_z does not
     * sort.  So a z change takes effect for hit-testing one frame late, and an
     * instance created after the last draw sits at the list end, i.e. on top,
     * whatever its z.  Specified in spec/44_6 (§Treffer-Reihenfolge). */
    for (int i = (int)g_scene->instances.size() - 1; i >= 0; i--) {
        auto* inst = g_scene->instances[i];
        if (!inst->visible) continue;
        if (inst->input_transparent) continue;   /* spec/44_6: click-through */
        if (inst->pending_delete) continue;      /* dies at the end of the frame: takes no more input */
        if (inst->parent) continue;  /* nested children: not hit-tested (local coords) */

        float ax, ay;
        get_actual_pos(inst, ax, ay);
        float sx = inst->scale_x, sy = inst->scale_y;
        { float rsx, rsy; get_rel_scale(rsx, rsy); sx *= rsx; sy *= rsy; }
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

/* The z level that counts for arbitration: that of the topmost
 * parent instance.  Nested instances (bind_list rows, spawn_child) have
 * a meaningless z_order of their own — they are drawn WITH the parent.
 * Without this the modal threshold would have locked out the rows of a modal
 * dialog: row z=0 < dialog z=460, so the card selection
 * in its own overlay would no longer have been operable. */
static int32_t effective_z(const AimlPrefabInstance* inst) {
    int guard = 0;
    while (inst && inst->parent && ++guard < 64) inst = inst->parent;
    return inst ? inst->z_order : 0;
}

/* spec/44_6 "Modal": z of the TOPMOST visible input_blocking instance whose
 * area contains (mx,my).  `found` says whether there is one at all.
 *
 * The widget pass deliberately does not arbitrate by z — it must see all
 * instances, so that a transparent panel lets its own widgets
 * through.  Exactly because of that everything stayed clickable under a full-screen overlay.
 * This function returns the threshold below which widgets
 * are skipped. */
static int32_t modal_block_z(float mx, float my, bool& found) {
    found = false;
    int32_t top = 0;
    if (!g_scene) return 0;
    for (auto* inst : g_scene->instances) {
        if (!inst || !inst->visible || inst->pending_delete) continue;
        if (!inst->input_blocking) continue;
        if (inst->parent) continue;   /* genestet: eigene lokale Koordinaten */
        float ax, ay;
        get_actual_pos(inst, ax, ay);
        float sx = inst->scale_x, sy = inst->scale_y;
        { float rsx, rsy; get_rel_scale(rsx, rsy); sx *= rsx; sy *= rsy; }
        float w = inst->def->width * sx;
        float h = inst->def->height * sy;
        if (mx < ax || mx > ax + w || my < ay || my > ay + h) continue;
        if (!found || inst->z_order > top) { top = inst->z_order; found = true; }  /* top-level: z_order == effective_z */
    }
    return top;
}

/* Compute a child's screen-space bbox (post-scale, post-instance-position). */
static void child_screen_bbox(AimlPrefabInstance* inst, const PrefabChild& ch,
                              float& bx, float& by, float& bw, float& bh)
{
    /* Walk the parent chain so nested / spawned instances (e.g. container
     * rows) get their true world-space bounds — otherwise a widget's
     * hit-box would be computed as if the row were at the top-left of the
     * window, and clicks on the visible dropdown would miss it. */
    float wx, wy, sx, sy;
    inst_world_transform(inst, wx, wy, sx, sy);
    bx = wx + ch.x * sx;
    by = wy + ch.y * sy;
    bw = ch.width  * sx;
    bh = ch.height * sy;
}

/* Forward declaration — callback dispatch (defined further below). */
static void prefab_invoke_callback(const std::string& fn_name, AimlPrefabInstance* inst);

#ifdef __EMSCRIPTEN__
/* Mobile soft-keyboard bridge (see input_runtime.cpp).  When the focused
 * text_input changes, raise/lower the hidden DOM <input> so the on-screen
 * keyboard appears/dismisses. */
extern "C" void aiml_web_kbd_open(int is_secret, int is_numeric, int is_email);
extern "C" void aiml_web_kbd_close(void);
static void prefab_web_kbd_sync() {
    static AimlPrefabInstance* prev_inst  = nullptr;
    static int32_t             prev_child = -1;
    AimlPrefabInstance* cur   = g_scene ? g_scene->focused_input_inst      : nullptr;
    int32_t             child = g_scene ? g_scene->focused_input_child_idx : -1;
    if (cur == prev_inst && child == prev_child) return;   /* no transition */
    prev_inst = cur; prev_child = child;
    if (cur && child >= 0 && child < (int32_t)cur->children.size() &&
        cur->children[child].type == CHILD_TEXT_INPUT) {
        aiml_web_kbd_open(cur->children[child].is_secret ? 1 : 0, 0, 0);
    } else {
        aiml_web_kbd_close();
    }
}
#endif

/* tb_follow_x: keep the horizontal window following the caret
 * (spec/44_6 §5f).  Without wrapping the caret can wander out of the window
 * to the left or right; then the window wanders along.  With wrapping there is nothing
 * to do — there every line is as wide as the box. */
static void tb_follow_x(PrefabChild& ch) {
    if (ch.wrap || !ch.tb_font || ch.tb_inner_w <= 0.0f) return;
    /* Find the start of the line and measure the width up to the caret. */
    int32_t b = ch.cursor_pos;
    if (b > (int32_t)ch.text.size()) b = (int32_t)ch.text.size();
    while (b > 0 && ch.text[(size_t)b - 1] != '\n') --b;
    std::string prefix = ch.text.substr((size_t)b, (size_t)(ch.cursor_pos - b));
    float caret = text_width_font(ch.tb_font, prefix.c_str());
    if (caret - ch.tb_off_x > ch.tb_inner_w) ch.tb_off_x = caret - ch.tb_inner_w;
    if (caret - ch.tb_off_x < 0.0f)          ch.tb_off_x = caret;
    if (ch.tb_off_x < 0.0f)                  ch.tb_off_x = 0.0f;
}

/* tb_move_rows: move the caret by lines (spec/44_6 §5f) — for
 * `textbox` AND for a multi-line `text_input`.  The line structure comes
 * from THE SAME function as for drawing; two derivations would be two
 * opportunities to let them drift apart.
 *
 * Column here means the byte distance from the start of the line — for a mono font
 * (log, source text) that is exactly the column, and both fields concerned
 * are mono. */
static void tb_move_rows(PrefabChild& ch, const char* key, bool shift_h, int32_t tlen) {
    if (!ch.tb_font) return;
    std::vector<std::pair<int32_t,int32_t>> ksp;
    std::vector<std::string> klines =
        wrap_text_lines(ch.tb_font, ch.text.c_str(), ch.wrap ? ch.tb_inner_w : 0.0f, &ksp);
    if (klines.empty()) return;
    int rows = (int)ksp.size();
    int cur_row = 0;
    for (int r = 0; r < rows; ++r)
        if (ch.cursor_pos >= ksp[(size_t)r].first) cur_row = r;
    int32_t col = ch.cursor_pos - ksp[(size_t)cur_row].first;
    if (col < 0) col = 0;
    int vis  = ch.tb_visible > 0 ? ch.tb_visible : 1;
    int step = (strcmp(key, "Up") == 0) ? -1
             : (strcmp(key, "Down") == 0) ? 1
             : (strcmp(key, "PageUp") == 0) ? -vis : vis;
    int want = cur_row + step;
    if (want < 0) want = 0;
    if (want > rows - 1) want = rows - 1;
    int32_t beg = ksp[(size_t)want].first;
    int32_t end = ksp[(size_t)want].second;
    int32_t tgt = beg + col;
    if (tgt > end) tgt = end;
    if (tgt > tlen) tgt = tlen;
    ch.cursor_pos = tgt;
    if (!shift_h) ch.sel_anchor = ch.cursor_pos;
    /* The view FOLLOWS the caret — a key that moves it out of the window
     * moves the window.  The text box keeps its window in `scroll_offset`,
     * the input field in `text_scroll_y`. */
    if (ch.type == CHILD_TEXTBOX) {
        if (want < ch.scroll_offset)        ch.scroll_offset = want;
        if (want >= ch.scroll_offset + vis) ch.scroll_offset = want - vis + 1;
        if (ch.scroll_offset < 0)           ch.scroll_offset = 0;
        if (ch.tail) ch.tb_prev_max = -1;   /* wer blaettert, will bleiben */
    } else {
        int sl = (int)ch.text_scroll_y;
        if (want < sl)        sl = want;
        if (want >= sl + vis) sl = want - vis + 1;
        if (sl < 0) sl = 0;
        ch.text_scroll_y = (float)sl;
    }
    tb_follow_x(ch);
}

/* Apply text/key events from the input event queue to the currently focused
 * text_input child. Mirrors ui_runtime's focus behaviour. */
static void apply_text_events_to_focus() {
    if (!g_scene || !g_scene->focused_input_inst) return;
    AimlPrefabInstance* inst = g_scene->focused_input_inst;
    int32_t idx = g_scene->focused_input_child_idx;
    if (idx < 0 || idx >= (int32_t)inst->children.size()) return;
    PrefabChild& ch = inst->children[idx];

    /* Textbox (spec/44_6 §5f): read-only — select and copy, never type.
     * A branch of its own, so that no editing path changes text by accident. */
    if (ch.type == CHILD_TEXTBOX) {
        int32_t tlen = (int32_t)ch.text.size();
        if (ch.cursor_pos > tlen) ch.cursor_pos = tlen;
        if (ch.cursor_pos < 0)    ch.cursor_pos = 0;
        if (ch.sel_anchor > tlen) ch.sel_anchor = tlen;
        if (ch.sel_anchor < 0)    ch.sel_anchor = 0;
        bool alt_h  = aiml_input_key_down("lalt") || aiml_input_key_down("ralt");
        bool ctrl_h = (aiml_input_key_down("lctrl") || aiml_input_key_down("rctrl")) && !alt_h;
        bool shift_h = aiml_input_key_down("lshift") || aiml_input_key_down("rshift");
        while (aiml_input_has_event()) {
            void* ev = aiml_input_next_event();
            const char* type = aiml_input_event_type(ev);
            if (!type || strcmp(type, "key_down") != 0) continue;   /* "text" verwerfen */
            const char* key = aiml_input_event_key(ev);
            if (!key) continue;
            int32_t lo = ch.sel_anchor < ch.cursor_pos ? ch.sel_anchor : ch.cursor_pos;
            int32_t hi = ch.sel_anchor < ch.cursor_pos ? ch.cursor_pos : ch.sel_anchor;
            if (ctrl_h && strcmp(key, "A") == 0) {
                ch.sel_anchor = 0; ch.cursor_pos = tlen;
            } else if (ctrl_h && strcmp(key, "C") == 0) {
                /* Without a selection copy the WHOLE content — for a log/
                 * output box that is practically always what is meant. */
                if (lo != hi) aiml_input_clipboard_set(ch.text.substr((size_t)lo, (size_t)(hi - lo)).c_str());
                else          aiml_input_clipboard_set(ch.text.c_str());
            } else if (strcmp(key, "Left") == 0) {
                if (!shift_h && lo != hi) ch.cursor_pos = lo;
                else                      ch.cursor_pos = utf8_prev(ch.text, ch.cursor_pos);
                if (!shift_h) ch.sel_anchor = ch.cursor_pos;
                tb_follow_x(ch);
            } else if (strcmp(key, "Right") == 0) {
                if (!shift_h && lo != hi) ch.cursor_pos = hi;
                else                      ch.cursor_pos = utf8_next(ch.text, ch.cursor_pos);
                if (!shift_h) ch.sel_anchor = ch.cursor_pos;
                tb_follow_x(ch);
            } else if (strcmp(key, "Up") == 0 || strcmp(key, "Down") == 0 ||
                       strcmp(key, "PageUp") == 0 || strcmp(key, "PageDown") == 0) {
                /* spec/44_6 §5f: all four directions.  The line structure
                 * comes from THE SAME function as for drawing — two
                 * derivations would be two opportunities to let them drift
                 * apart.  Column here means: byte distance from the
                 * start of the line; for a mono font that is the column. */
                tb_move_rows(ch, key, shift_h, tlen);
            } else if (strcmp(key, "Home") == 0) {
                /* Start of line; with Ctrl the start of the WHOLE text. */
                if (ctrl_h) { ch.cursor_pos = 0; }
                else {
                    int32_t b = ch.cursor_pos;
                    while (b > 0 && ch.text[(size_t)b - 1] != '\n') --b;
                    ch.cursor_pos = b;
                    ch.tb_off_x = 0.0f;
                }
                if (!shift_h) ch.sel_anchor = ch.cursor_pos;
            } else if (strcmp(key, "End") == 0) {
                if (ctrl_h) { ch.cursor_pos = tlen; }
                else {
                    int32_t e = ch.cursor_pos;
                    while (e < tlen && ch.text[(size_t)e] != '\n') ++e;
                    ch.cursor_pos = e;
                }
                if (!shift_h) ch.sel_anchor = ch.cursor_pos;
            } else if (strcmp(key, "Escape") == 0) {
                ch.focused = false;
                ch.sel_anchor = ch.cursor_pos;
                g_scene->focused_input_inst = nullptr;
                g_scene->focused_input_child_idx = -1;
            }
        }
        if (ch.cursor_pos < 0) ch.cursor_pos = 0; if (ch.cursor_pos > tlen) ch.cursor_pos = tlen;
        if (ch.sel_anchor < 0) ch.sel_anchor = 0; if (ch.sel_anchor > tlen) ch.sel_anchor = tlen;
        return;
    }

    if (ch.type != CHILD_TEXT_INPUT) return;

    /* Defensive: the text may have been shrunk externally (set_text) since the
     * cursor last moved — clamp before any insert/erase/substr below. */
    {
        int32_t len0 = (int32_t)ch.text.size();
        if (ch.cursor_pos > len0) ch.cursor_pos = len0;
        if (ch.cursor_pos < 0)    ch.cursor_pos = 0;
        if (ch.sel_anchor > len0) ch.sel_anchor = len0;
        if (ch.sel_anchor < 0)    ch.sel_anchor = 0;
    }

    /* Ctrl held (either side) → clipboard/select shortcuts instead of typing.
     * Exclude Alt: AltGr (needed for @/€/… on e.g. German layouts) reports as
     * Ctrl+Alt on some platforms — must still type its character, not shortcut. */
    bool alt_held = aiml_input_key_down("lalt") || aiml_input_key_down("ralt");
    bool ctrl  = (aiml_input_key_down("lctrl")  || aiml_input_key_down("rctrl"))  && !alt_held;
    bool shift =  aiml_input_key_down("lshift") || aiml_input_key_down("rshift");
    bool changed = false;

    auto has_sel = [&]() { return ch.sel_anchor != ch.cursor_pos; };
    auto sel_lo  = [&]() { return ch.sel_anchor < ch.cursor_pos ? ch.sel_anchor : ch.cursor_pos; };
    auto sel_hi  = [&]() { return ch.sel_anchor < ch.cursor_pos ? ch.cursor_pos : ch.sel_anchor; };
    auto del_sel = [&]() {
        int32_t a = sel_lo(), b = sel_hi();
        ch.text.erase((size_t)a, (size_t)(b - a));
        ch.cursor_pos = a; ch.sel_anchor = a; changed = true;
    };

    while (aiml_input_has_event()) {
        void* ev = aiml_input_next_event();
        const char* type = aiml_input_event_type(ev);
        if (!type) continue;
        if (strcmp(type, "text") == 0) {
            /* GLFW usually suppresses char events for Ctrl+letter, but guard
             * anyway so Ctrl+V/C/etc. never inserts the literal letter. */
            if (ctrl) continue;
            const char* s = aiml_input_event_char_(ev);
            if (!s) continue;
            if (has_sel()) del_sel();
            ch.text.insert((size_t)ch.cursor_pos, s);
            ch.cursor_pos += (int32_t)strlen(s);
            ch.sel_anchor = ch.cursor_pos;
            changed = true;
        } else if (strcmp(type, "key_down") == 0) {
            const char* key = aiml_input_event_key(ev);
            if (!key) continue;
            int32_t len = (int32_t)ch.text.size();
            if (ctrl && strcmp(key, "A") == 0) {
                ch.sel_anchor = 0; ch.cursor_pos = len;               /* select all */
            } else if (ctrl && strcmp(key, "C") == 0) {
                if (has_sel()) aiml_input_clipboard_set(ch.text.substr((size_t)sel_lo(), (size_t)(sel_hi() - sel_lo())).c_str());
            } else if (ctrl && strcmp(key, "X") == 0) {
                if (has_sel()) { aiml_input_clipboard_set(ch.text.substr((size_t)sel_lo(), (size_t)(sel_hi() - sel_lo())).c_str()); del_sel(); }
            } else if (ctrl && strcmp(key, "V") == 0) {
                const char* clip = aiml_input_clipboard_get();
                if (clip && *clip) {
                    if (has_sel()) del_sel();
                    ch.text.insert((size_t)ch.cursor_pos, clip);
                    ch.cursor_pos += (int32_t)strlen(clip);
                    ch.sel_anchor = ch.cursor_pos; changed = true;
                }
            } else if (strcmp(key, "Left") == 0) {
                if (!shift && has_sel()) ch.cursor_pos = sel_lo();
                else                     ch.cursor_pos = utf8_prev(ch.text, ch.cursor_pos);
                if (!shift) ch.sel_anchor = ch.cursor_pos;
            } else if (strcmp(key, "Right") == 0) {
                if (!shift && has_sel()) ch.cursor_pos = sel_hi();
                else                     ch.cursor_pos = utf8_next(ch.text, ch.cursor_pos);
                if (!shift) ch.sel_anchor = ch.cursor_pos;
            } else if (ch.ti_multiline &&
                       (strcmp(key, "Up") == 0 || strcmp(key, "Down") == 0 ||
                        strcmp(key, "PageUp") == 0 || strcmp(key, "PageDown") == 0)) {
                /* spec/44_6 §5f: all four directions in the multi-line field
                 * too.  Previously there was no up/down at all — the caret
                 * could not leave line one by keyboard. */
                tb_move_rows(ch, key, shift, len);
            } else if (strcmp(key, "Home") == 0) {
                /* Start of line; with Ctrl the start of the WHOLE text. */
                if (ctrl || !ch.ti_multiline) { ch.cursor_pos = 0; }
                else {
                    int32_t b = ch.cursor_pos;
                    while (b > 0 && ch.text[(size_t)b - 1] != '\n') --b;
                    ch.cursor_pos = b;
                }
                if (!shift) ch.sel_anchor = ch.cursor_pos;
            } else if (strcmp(key, "End") == 0) {
                if (ctrl || !ch.ti_multiline) { ch.cursor_pos = len; }
                else {
                    int32_t e = ch.cursor_pos;
                    while (e < len && ch.text[(size_t)e] != '\n') ++e;
                    ch.cursor_pos = e;
                }
                if (!shift) ch.sel_anchor = ch.cursor_pos;
            } else if (strcmp(key, "Backspace") == 0) {
                if (has_sel()) del_sel();
                else if (ch.cursor_pos > 0) {
                    int32_t p = utf8_prev(ch.text, ch.cursor_pos);
                    ch.text.erase((size_t)p, (size_t)(ch.cursor_pos - p));
                    ch.cursor_pos = p; ch.sel_anchor = p; changed = true;
                }
            } else if (strcmp(key, "Delete") == 0) {
                if (has_sel()) del_sel();
                else if (ch.cursor_pos < len) {
                    int32_t q = utf8_next(ch.text, ch.cursor_pos);
                    ch.text.erase((size_t)ch.cursor_pos, (size_t)(q - ch.cursor_pos));
                    ch.sel_anchor = ch.cursor_pos; changed = true;
                }
            } else if (strcmp(key, "Enter") == 0) {
                /* A multi-line field is an EDITOR: the Enter key makes
                 * a new line there.  Until 2026-10-18 it did nothing of the sort
                 * — a code field in which one cannot start a line, although
                 * spec/44_6 §5g promises multi-line editing.  In the single-line
                 * field it stays the submit. */
                if (ch.ti_multiline) {
                    if (has_sel()) del_sel();
                    ch.text.insert((size_t)ch.cursor_pos, "\n");
                    ch.cursor_pos += 1;
                    ch.sel_anchor = ch.cursor_pos;
                    changed = true;
                } else if (!ch.on_change_fn.empty()) {
                    prefab_invoke_callback(ch.on_change_fn, inst);
                }
            } else if (strcmp(key, "Escape") == 0) {
                ch.focused = false;
                g_scene->focused_input_inst = nullptr;
                g_scene->focused_input_child_idx = -1;
            }
        }
    }
    /* Clamp cursor+anchor into range (paranoia against desync). */
    int32_t len = (int32_t)ch.text.size();
    if (ch.cursor_pos < 0) ch.cursor_pos = 0; if (ch.cursor_pos > len) ch.cursor_pos = len;
    if (ch.sel_anchor < 0) ch.sel_anchor = 0; if (ch.sel_anchor > len) ch.sel_anchor = len;
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
    /* spec/44_6 "Modal": if a blocking instance lies under the cursor,
     * widgets with a smaller z get no input. */
    bool    modal_found = false;
    int32_t modal_z     = modal_block_z(mx, my, modal_found);
    /* spec/44_6: a click = press + release inside the widget rect, independent of the instance —
     * a rebuild between press and release (board reload) must not swallow it. */
    static bool  s_press_valid = false;
    static float s_press_x = 0.f, s_press_y = 0.f;
    if (mb_pressed) { s_press_valid = true; s_press_x = (float)mx; s_press_y = (float)my; }

    /* spec/44_6: hit trace.  Names EVERY widget whose area contains the press point
     * — not a single "target": the widget pass below deliberately does not gate
     * on which instance owns the cursor, so
     * overlapping widgets can both fire.  The trace shows that instead of
     * hiding it.  "miss" is an outcome of its own that can be checked.  Without these lines
     * "button does nothing" cannot be told apart from "click never arrived". */
    if (mb_pressed && g_scene && !g_trace_hits) {
        /* Tracing off (the normal case of every app): collect nothing.  "?" means
         * "not collected" and can be told apart from "" (real miss) —
         * otherwise a check for "nothing hit" would pass with
         * tracing switched off for the wrong reason. */
        g_last_hits = "?";
    } else if (mb_pressed && g_scene) {
        g_last_hits.clear();
        for (size_t ii = 0; ii < g_scene->instances.size(); ii++) {
            auto* inst = g_scene->instances[ii];
            if (!inst || !inst->visible) continue;
            if (inst->input_transparent) continue;   /* spec/44_6: click-through */
            if (modal_found && effective_z(inst) < modal_z) continue;  /* spec/44_6: modal */
            for (auto& ch : inst->children) {
                if (!ch.visible) continue;
                float bx, by, bw, bh;
                child_screen_bbox(inst, ch, bx, by, bw, bh);
                if (s_press_x < bx || s_press_x >= bx + bw
                 || s_press_y < by || s_press_y >= by + bh) continue;
                char addr[224];
                snprintf(addr, sizeof addr, "%s[%zu]/%s",
                    inst->def ? inst->def->name.c_str() : "?", ii, ch.name.c_str());
                if (!g_last_hits.empty()) g_last_hits += "|";
                g_last_hits += addr;
                if (g_trace_hits) {
                    char tbuf[320];
                    snprintf(tbuf, sizeof tbuf, "[prefab] hit %s at (%.1f,%.1f) kind=%d",
                             addr, s_press_x, s_press_y, (int)ch.type);
                    aiml_log_info(tbuf, "prefab_runtime.cpp");
                }
            }
        }
        if (g_trace_hits && g_last_hits.empty()) {
            char tbuf[128];
            snprintf(tbuf, sizeof tbuf, "[prefab] miss at (%.1f,%.1f)",
                     s_press_x, s_press_y);
            aiml_log_info(tbuf, "prefab_runtime.cpp");
        }
    }

    auto press_origin_in = [&](float rx, float ry, float rw, float rh) {
        return s_press_valid && s_press_x >= rx && s_press_x < rx + rw && s_press_y >= ry && s_press_y < ry + rh;
    };

    /* Click-resolution: if the mouse is being pressed, we'll set focus to
     * the topmost text_input under the cursor (else clear focus). */
    AimlPrefabInstance* new_focus_inst = nullptr;
    int32_t              new_focus_idx  = -1;

    /* First run over ALL widget children (hidden instances too) and
     * reset clicked_frame — otherwise the flag stays stale for prefabs that were
     * invisible in the meantime (overlays open→close→open crash). */
    for (auto* inst : g_scene->instances) {
        for (auto& ch : inst->children) {
            if (ch.type == CHILD_BUTTON
             || ch.type == CHILD_TEXT_INPUT
             || ch.type == CHILD_CHECKBOX
             || ch.type == CHILD_RADIO
             || ch.type == CHILD_DROPDOWN
             || ch.type == CHILD_SLIDER) {
                ch.clicked_frame = false;
            }
        }
    }

    /* If any dropdown popup is open AND the mouse is inside its popup rect,
     * click events must go to that popup only — otherwise a click on a
     * popup row would also fire on any widget rendered underneath it
     * (spawned rows in a container, sibling dropdowns, etc.). */
    bool click_absorbed_by_popup = false;
    for (auto* inst : g_scene->instances) {
        if (!inst->visible) continue;
        for (auto& ch : inst->children) {
            if (ch.type != CHILD_DROPDOWN || !ch.dropdown_open || !ch.visible) continue;
            float bx, by, bw, bh;
            child_screen_bbox(inst, ch, bx, by, bw, bh);
            int n_opts = ch.options.empty() ? 0 : 1;
            for (char c : ch.options) if (c == '|') n_opts++;
            if (n_opts <= 0) continue;
            float popup_x, popup_y, popup_w, popup_h;
            compute_popup_rect(bx, by, bw, bh, n_opts, popup_x, popup_y, popup_w, popup_h);
            if (mx >= popup_x && mx < popup_x + popup_w
             && my >= popup_y && my < popup_y + popup_h) {
                click_absorbed_by_popup = true;
                break;
            }
        }
        if (click_absorbed_by_popup) break;
    }

    /* BY INDEX, not by iterator: an on_click callback may
     * CREATE prefabs -- the travel map builds eight instances in its
     * click handler.  Every instantiate appends to g_scene->instances;
     * if the vector grows beyond its capacity in the process, a running
     * range-for iterator is INVALID and the next step reads
     * freed memory.  In the browser this ended as
     * "RuntimeError: index out of bounds" in the middle of aiml_prefab_update_hover
     * as soon as the travel map was opened for the first time.
     *
     * The upper bound is the state BEFORE the pass: what only comes into being through this
     * click must not get the same click once more --
     * otherwise a freshly appeared overlay takes the press that has just
     * opened it (spec/44_6 "one press, one taker"). */
    const size_t inst_count_at_entry = g_scene->instances.size();
    for (size_t ii_w = 0; ii_w < inst_count_at_entry; ii_w++) {
        if (ii_w >= g_scene->instances.size()) break;   /* a callback has deleted */
        auto* inst = g_scene->instances[ii_w];
        if (!inst) continue;
        if (!inst->visible) continue;
        /* spec/44_6: whoever dies at the end of the frame takes no more input — especially
         * important here, because otherwise an orphan could still grab the keyboard focus
         * in its one frame, and that STICKS until the next
         * click elsewhere. */
        if (inst->pending_delete) continue;
        /* spec/44_6 "Modal": under a blocking instance nothing takes
         * input any more — otherwise an overlay is only visually modal and a click
         * additionally triggers whatever lies beneath. */
        if (modal_found && effective_z(inst) < modal_z) continue;
        for (size_t i = 0; i < inst->children.size(); i++) {
            PrefabChild& ch = inst->children[i];
            if (!ch.visible) continue;
            if (ch.type != CHILD_BUTTON
                && ch.type != CHILD_TEXT_INPUT
                && ch.type != CHILD_CHECKBOX
                && ch.type != CHILD_RADIO
                && ch.type != CHILD_DROPDOWN
                && ch.type != CHILD_SLIDER
                && ch.type != CHILD_CONTAINER
                && ch.type != CHILD_TEXTBOX) continue;
            /* Popup absorbs the click — but only for CLOSED dropdowns and
             * other widgets.  The dropdown that OWNS the open popup still
             * needs to process its own release-outside-popup to close. */
            if (click_absorbed_by_popup && !(ch.type == CHILD_DROPDOWN && ch.dropdown_open))
                continue;

            float bx, by, bw, bh;
            child_screen_bbox(inst, ch, bx, by, bw, bh);
            float hx = bx, hy = by, hw = bw, hh = bh;
#ifdef __EMSCRIPTEN__
            /* Touch hit-slop: a 20px design checkbox shrinks to a few screen
             * pixels on a phone (the 1280x720 board is scaled to fit) and is
             * essentially untappable with a finger.  Grow the HIT rect (not the
             * drawn rect) of small tappable widgets to a minimum finger size,
             * centered, so ack-checkboxes / small buttons can be tapped.  Large
             * widgets are unchanged. */
            if (ch.type == CHILD_CHECKBOX || ch.type == CHILD_RADIO
                || ch.type == CHILD_BUTTON || ch.type == CHILD_DROPDOWN) {
                const float MIN_TOUCH = 30.0f;
                if (hw < MIN_TOUCH) { hx -= (MIN_TOUCH - hw) * 0.5f; hw = MIN_TOUCH; }
                if (hh < MIN_TOUCH) { hy -= (MIN_TOUCH - hh) * 0.5f; hh = MIN_TOUCH; }
            }
#endif
            bool over = (mx >= hx && mx < hx + hw && my >= hy && my < hy + hh);
            ch.hot = over;

            if (ch.type == CHILD_BUTTON) {
                if (mb_pressed && over) ch.pressed_now = true;
                if (mb_released) {
                    if ((ch.pressed_now || press_origin_in(hx, hy, hw, hh)) && over) {
                        ch.clicked_frame = true;
                        if (!ch.on_click_fn.empty())
                            prefab_invoke_callback(ch.on_click_fn, inst);
                    }
                    ch.pressed_now = false;
                }
            } else if (ch.type == CHILD_CHECKBOX) {
                if (mb_pressed && over) ch.pressed_now = true;
                if (mb_released) {
                    if ((ch.pressed_now || press_origin_in(hx, hy, hw, hh)) && over) {
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
            } else if (ch.type == CHILD_RADIO) {
                /* Radio: on release, set THIS checked, uncheck all others with
                 * the same group name across the scene. */
                if (mb_pressed && over) ch.pressed_now = true;
                if (mb_released) {
                    if ((ch.pressed_now || press_origin_in(hx, hy, hw, hh)) && over) {
                        ch.clicked_frame = true;
                        if (!ch.checked || !ch.group.empty()) {
                            /* Deselect siblings first. */
                            if (!ch.group.empty()) {
                                for (auto* other_inst : g_scene->instances) {
                                    for (auto& other_ch : other_inst->children) {
                                        if (other_ch.type == CHILD_RADIO
                                            && other_ch.group == ch.group) {
                                            other_ch.checked = false;
                                        }
                                    }
                                }
                            }
                            ch.checked = true;
                        }
                        if (!ch.on_click_fn.empty())
                            prefab_invoke_callback(ch.on_click_fn, inst);
                        if (!ch.on_change_fn.empty())
                            prefab_invoke_callback(ch.on_change_fn, inst);
                    }
                    ch.pressed_now = false;
                }
            } else if (ch.type == CHILD_DROPDOWN) {
                /* spec/44_6 §5h: operate the payload bar.  It lies at the bottom edge
                 * of the ROW above the payload zone, i.e. outside the dropdown box
                 * — a drag on it does not toggle the popup.  The wheel scrolls field by field
                 * (one pitch per notch): windowing happens in whole payloads, a
                 * partial offset would make a field disappear instead of showing it half. */
                if (ch.payload_scroll && ch.payload_pitch > 0.0f && inst->def
                    && ch.width > 0.0f && ch.height > 0.0f) {
                    float scx2 = bw / ch.width;
                    float scy2 = bh / ch.height;
                    float viewport = inst->def->width - ch.payload_start;
                    float max_off  = ch.payload_span - viewport;
                    if (viewport > 0.0f && max_off > 0.5f) {
                        float zone_x = bx + (ch.payload_start - ch.x) * scx2;
                        float zone_w = viewport * scx2;
                        float row_y0 = by - ch.y * scy2;
                        float bar_y  = row_y0 + (inst->def->height - 7.0f) * scy2;
                        bool on_zone = (mx >= zone_x && mx < zone_x + zone_w
                                     && my >= row_y0 && my < row_y0 + inst->def->height * scy2);
                        bool on_bar  = (mx >= zone_x && mx < zone_x + zone_w
                                     && my >= bar_y - 2.0f * scy2
                                     && my <= bar_y + 8.0f * scy2);
                        if (mb_pressed && on_bar) ch.payload_sb_drag = true;
                        if (ch.payload_sb_drag && aiml_input_mouse_button(0) && zone_w > 0.0f) {
                            float f = (mx - zone_x) / zone_w;
                            if (f < 0.0f) f = 0.0f;
                            if (f > 1.0f) f = 1.0f;
                            ch.payload_scroll_x = f * max_off;
                            ch.needs_relayout = true;
                        }
                        if (on_zone && !ch.payload_sb_drag) {
                            float wy = aiml_input_mouse_scroll_y();
                            if (wy != 0.0f) {
                                ch.payload_scroll_x -= wy * ch.payload_pitch;
                                ch.needs_relayout = true;
                            }
                        }
                    }
                }
                if (mb_released) ch.payload_sb_drag = false;
                /* Dropdown: click on box toggles popup (dropdown_open).  Popup
                 * handling (row click, scroll, click outside) runs in a
                 * separate loop after this one (see below). */
                if (mb_pressed && over && !ch.dropdown_open) ch.pressed_now = true;
                if (mb_released) {
                    if (ch.pressed_now && over) {
                        ch.dropdown_open = !ch.dropdown_open;
                        if (ch.dropdown_open) {
                            /* Scroll auf selected zentrieren. */
                            ch.scroll_offset = std::max(0, ch.selected_index - 3);
                        }
                    }
                    ch.pressed_now = false;
                }
            } else if (ch.type == CHILD_SLIDER) {
                /* Slider: press inside the track starts dragging; while
                 * dragging, follow the cursor and continuously map x → value
                 * within [slider_min, bar_max].  Fires on_change when value
                 * actually changes.  Release ends the drag.
                 * When the slider has a track_texture skin, the interactive
                 * range is limited to the frame's inner opening so mouse
                 * position maps to knob position 1:1 (matches the render
                 * pass in CHILD_SLIDER). */
                if (mb_pressed && over) {
                    ch.slider_dragging = true;
                }
                bool held = (aiml_input_mouse_button(0) != 0);
                float drag_x0 = bx;
                float drag_w  = bw;
                if (ch.track_texture != 0) {
                    float fx_pad = bw * 0.06f;
                    drag_x0 = bx + fx_pad;
                    drag_w  = bw - fx_pad * 2.0f;
                }
                if (ch.slider_dragging && held && drag_w > 0.0f) {
                    float rel = (mx - drag_x0) / drag_w;
                    if (rel < 0.0f) rel = 0.0f; if (rel > 1.0f) rel = 1.0f;
                    float range  = ch.bar_max - ch.slider_min;
                    float new_v  = ch.slider_min + rel * range;
                    /* Snap-to-step: quantize to nearest multiple of
                     * `slider_snap`, offset from slider_min so the min
                     * itself is always a valid rest state. */
                    if (ch.slider_snap > 0.0f) {
                        float step = ch.slider_snap;
                        float off  = new_v - ch.slider_min;
                        new_v = ch.slider_min + roundf(off / step) * step;
                        if (new_v > ch.bar_max)    new_v = ch.bar_max;
                        if (new_v < ch.slider_min) new_v = ch.slider_min;
                    }
                    if (new_v != ch.bar_value) {
                        ch.bar_value = new_v;
                        if (!ch.on_change_fn.empty())
                            prefab_invoke_callback(ch.on_change_fn, inst);
                    }
                }
                if (mb_released) {
                    ch.slider_dragging = false;
                }
            } else if (ch.type == CHILD_TEXT_INPUT) {
                /* Map a screen-x to the nearest caret stop (byte index), using the
                 * per-char x positions cached during the last render. */
                auto idx_from_x = [&](float x) -> int32_t {
                    if (ch.caret_x.empty()) return 0;
                    int32_t best = ch.caret_byte[0];
                    float   bd   = fabsf(ch.caret_x[0] - x);
                    for (size_t k = 1; k < ch.caret_x.size(); ++k) {
                        float d = fabsf(ch.caret_x[k] - x);
                        if (d < bd) { bd = d; best = ch.caret_byte[k]; }
                    }
                    return best;
                };
                /* Multi-line: first the LINE from y, then the column from x — a
                 * pure x search would take the nearest stop across all lines
                 * and thereby almost always land in the wrong line. */
                auto idx_from_xy = [&](float x, float y) -> int32_t {
                    int rows = (int)ch.tb_row_y.size();
                    if (rows <= 0 || ch.tb_row_start.size() < (size_t)rows + 1)
                        return idx_from_x(x);
                    int r = 0;
                    for (int k = 0; k < rows; ++k)
                        if (y >= ch.tb_row_y[(size_t)k]) r = k;
                    if (y < ch.tb_row_y[0]) r = 0;
                    int32_t a = ch.tb_row_start[(size_t)r];
                    int32_t b = ch.tb_row_start[(size_t)r + 1];
                    if (b <= a) return idx_from_x(x);
                    int32_t best = ch.tb_caret_byte[(size_t)a];
                    float   bd   = fabsf(ch.tb_caret_x[(size_t)a] - x);
                    for (int32_t k = a + 1; k < b; ++k) {
                        float d = fabsf(ch.tb_caret_x[(size_t)k] - x);
                        if (d < bd) { bd = d; best = ch.tb_caret_byte[(size_t)k]; }
                    }
                    return best;
                };
                /* Multi-line: wheel and scrollbar as in the text box
                 * (spec/44_6 §5f).  Without this the bar was a PICTURE:
                 * drawn, but nothing moved it, and the field jumped back to the cursor
                 * in every frame (reported 2026-10-22). */
                bool on_bar = false;
                if (ch.ti_multiline) {
                    float scx = (ch.width  > 0.0f) ? bw / ch.width  : 1.0f;
                    float scy = (ch.height > 0.0f) ? bh / ch.height : 1.0f;
                    int   max_l = ch.tb_total > ch.tb_visible ? ch.tb_total - ch.tb_visible : 0;
                    if (max_l > 0) {
                        float sb_w    = 6.0f * scx;
                        float sb_x    = bx + bw - sb_w - 2.0f * scx;
                        float track_y = by + 2.0f * scy;
                        float track_h = bh - 4.0f * scy;
                        float thumb_h = track_h * (float)ch.tb_visible / (float)ch.tb_total;
                        if (thumb_h < 16.0f * scy) thumb_h = 16.0f * scy;
                        float denom   = track_h - thumb_h;
                        float frac    = ch.text_scroll_y / (float)max_l;
                        if (frac < 0.0f) frac = 0.0f;
                        if (frac > 1.0f) frac = 1.0f;
                        float thumb_y = track_y + denom * frac;
                        if (mx >= sb_x - 2.0f * scx && mx <= sb_x + sb_w + 2.0f * scx
                            && my >= track_y && my <= track_y + track_h) on_bar = true;
                        bool over_thumb = (on_bar && my >= thumb_y && my <= thumb_y + thumb_h);
                        if (mb_pressed && (over_thumb || on_bar)) ch.sb_dragging = true;
                        if (ch.sb_dragging && aiml_input_mouse_button(0) && denom > 0.0f) {
                            float f2 = (my - track_y - thumb_h * 0.5f) / denom;
                            if (f2 < 0.0f) f2 = 0.0f;
                            if (f2 > 1.0f) f2 = 1.0f;
                            ch.text_scroll_y = f2 * (float)max_l;
                            ch.ti_free_scroll = true;
                        }
                        if (over && !ch.sb_dragging) {
                            float wy = aiml_input_mouse_scroll_y();
                            if (wy != 0.0f) {
                                /* Shift+wheel scrolls horizontally — the same gesture
                                 * as in the text box. */
                                if (aiml_input_key_down("lshift") || aiml_input_key_down("rshift")) {
                                    ch.tb_off_x -= wy * 48.0f * scx;
                                    if (ch.tb_off_x < 0.0f) ch.tb_off_x = 0.0f;
                                } else {
                                    ch.text_scroll_y -= wy * 3.0f;
                                    if (ch.text_scroll_y < 0.0f) ch.text_scroll_y = 0.0f;
                                    if (ch.text_scroll_y > (float)max_l) ch.text_scroll_y = (float)max_l;
                                }
                                ch.ti_free_scroll = true;
                            }
                        }
                    }
                }
                if (mb_released) ch.sb_dragging = false;
                /* A click ON the bar sets no cursor and selects
                 * nothing — otherwise every scroll drags text along (F44_6-…). */
                if (mb_pressed && over && !on_bar && !ch.sb_dragging) {
                    new_focus_inst = inst;
                    new_focus_idx  = (int32_t)i;
                    int32_t ci = idx_from_xy(mx, my);
                    ch.cursor_pos = ci;
                    ch.sel_anchor = ci;          /* click → collapsed cursor */
                    ch.sel_dragging = true;
                } else if (ch.sel_dragging && ch.focused && aiml_input_mouse_button(0)) {
                    ch.cursor_pos = idx_from_xy(mx, my);   /* drag → extend selection (anchor stays) */
                }
                if (mb_released) ch.sel_dragging = false;
            } else if (ch.type == CHILD_CONTAINER) {
                /* Scrollable container: mouse wheel over it + drag the thumb. */
                if (ch.scrollable && !ch.spawned.empty()) {
                    bool cflow_h = (ch.layout == "hflow");
                    AimlPrefabInstance* r0 = ch.spawned[0];
                    float pitch  = cflow_h ? (r0->def->width + ch.gap) : (r0->def->height + ch.gap);
                    float extent = cflow_h ? ch.width : ch.height;
                    int   visible = (pitch > 0.0f) ? (int)(extent / pitch) : (int)ch.spawned.size();
                    if (visible < 1) visible = 1;
                    int total   = (int)ch.spawned.size();
                    int max_off = total > visible ? total - visible : 0;
                    if (total > visible) {
                        float scx = (ch.width  > 0.0f) ? bw / ch.width  : 1.0f;
                        float scy = (ch.height > 0.0f) ? bh / ch.height : 1.0f;
                        if (over) {
                            float wy = aiml_input_mouse_scroll_y();
                            if      (wy > 0.0f && ch.scroll_offset > 0)       ch.scroll_offset--;
                            else if (wy < 0.0f && ch.scroll_offset < max_off) ch.scroll_offset++;
                        }
                        float sb_w    = 6.0f * scx;
                        float sb_x    = bx + bw - sb_w - 2.0f * scx;
                        float track_y = by + 2.0f * scy;
                        float track_h = bh - 4.0f * scy;
                        float thumb_h = track_h * (float)visible / (float)total;
                        if (thumb_h < 16.0f * scy) thumb_h = 16.0f * scy;
                        float denom = track_h - thumb_h;
                        float thumb_y = track_y + (denom > 0.0f ? denom * (float)ch.scroll_offset / (float)std::max(1, max_off) : 0.0f);
                        bool over_thumb = (mx >= sb_x && mx <= sb_x + sb_w && my >= thumb_y && my <= thumb_y + thumb_h);
                        if (mb_pressed && over_thumb) ch.sb_dragging = true;
                        if (ch.sb_dragging && aiml_input_mouse_button(0) && denom > 0.0f) {
                            float frac = (my - track_y - thumb_h * 0.5f) / denom;
                            if (frac < 0.0f) frac = 0.0f;
                            if (frac > 1.0f) frac = 1.0f;
                            ch.scroll_offset = (int)(frac * (float)max_off + 0.5f);
                        }
                    }
                }
                if (mb_released) ch.sb_dragging = false;
            } else if (ch.type == CHILD_TEXTBOX) {
                /* Selection (spec/44_6 §5f): a click sets focus + cursor, dragging
                 * extends, Ctrl+C copies (apply_text_events_to_focus).
                 * Caret stops come from the render pass.  The text box is
                 * NOT editable — only select and copy. */
                auto tb_idx_from_xy = [&](float x, float y) -> int32_t {
                    int rows = (int)ch.tb_row_y.size();
                    if (rows <= 0 || ch.tb_row_start.size() < (size_t)rows + 1) return ch.cursor_pos;
                    int r = 0;
                    if (ch.tb_row_h > 0.0f) {
                        for (int k = 0; k < rows; ++k) {
                            if (y >= ch.tb_row_y[(size_t)k]) r = k;
                        }
                        if (y < ch.tb_row_y[0]) r = 0;
                    }
                    int32_t a = ch.tb_row_start[(size_t)r];
                    int32_t b = ch.tb_row_start[(size_t)r + 1];
                    if (b <= a) return ch.cursor_pos;
                    int32_t best = ch.tb_caret_byte[(size_t)a];
                    float   bd   = fabsf(ch.tb_caret_x[(size_t)a] - x);
                    for (int32_t k = a + 1; k < b; ++k) {
                        float d = fabsf(ch.tb_caret_x[(size_t)k] - x);
                        if (d < bd) { bd = d; best = ch.tb_caret_byte[(size_t)k]; }
                    }
                    return best;
                };
                bool on_scrollbar = false;
                {   /* Clicks on a scrollbar stay scrolling, not selection —
                     * on BOTH.  The lower one was missing: a drag on the horizontal
                     * handle selected text instead of scrolling (reported
                     * 2026-10-22). */
                    float scx2 = (ch.width  > 0.0f) ? bw / ch.width  : 1.0f;
                    float scy2 = (ch.height > 0.0f) ? bh / ch.height : 1.0f;
                    float sb_w = 6.0f * scx2 + 4.0f * scx2;
                    if (mx >= bx + bw - sb_w) on_scrollbar = true;
                    if (!ch.wrap && ch.tb_content_w > ch.tb_inner_w) {
                        float sb_h = 6.0f * scy2 + 4.0f * scy2;
                        if (my >= by + bh - sb_h) on_scrollbar = true;
                    }
                }
                if (mb_pressed && over && !on_scrollbar) {
                    new_focus_inst = inst;
                    new_focus_idx  = (int32_t)i;
                    int32_t ci = tb_idx_from_xy(mx, my);
                    ch.cursor_pos = ci;
                    ch.sel_anchor = ci;
                    ch.tb_sel_dragging = true;
                } else if (ch.tb_sel_dragging && ch.focused && aiml_input_mouse_button(0) &&
                           !ch.sb_dragging && !ch.sb_dragging_x) {
                    ch.cursor_pos = tb_idx_from_xy(mx, my);
                    /* Dragging beyond the edge scrolls along. */
                    int max_off_sel = ch.tb_total > ch.tb_visible ? ch.tb_total - ch.tb_visible : 0;
                    if (my < by && ch.scroll_offset > 0)                    ch.scroll_offset--;
                    else if (my > by + bh && ch.scroll_offset < max_off_sel) ch.scroll_offset++;
                }
                if (mb_released) ch.tb_sel_dragging = false;

                /* Scrollable text area: wheel over it + drag the thumb.  Line
                 * counts come from the render pass (tb_total / tb_visible). */
                int total = ch.tb_total, visible = ch.tb_visible;
                int max_off = total > visible ? total - visible : 0;
                if (max_off > 0) {
                    float scx = (ch.width  > 0.0f) ? bw / ch.width  : 1.0f;
                    float scy = (ch.height > 0.0f) ? bh / ch.height : 1.0f;
                    if (over) {
                        float wy = aiml_input_mouse_scroll_y();
                        if      (wy > 0.0f && ch.scroll_offset > 0)       ch.scroll_offset--;
                        else if (wy < 0.0f && ch.scroll_offset < max_off) ch.scroll_offset++;
                    }
                    float sb_w    = 6.0f * scx;
                    float sb_x    = bx + bw - sb_w - 2.0f * scx;
                    float track_y = by + 2.0f * scy;
                    float track_h = bh - 4.0f * scy;
                    float thumb_h = track_h * (float)visible / (float)std::max(1, total);
                    if (thumb_h < 16.0f * scy) thumb_h = 16.0f * scy;
                    float denom = track_h - thumb_h;
                    float thumb_y = track_y + (denom > 0.0f ? denom * (float)ch.scroll_offset / (float)std::max(1, max_off) : 0.0f);
                    bool over_thumb = (mx >= sb_x && mx <= sb_x + sb_w && my >= thumb_y && my <= thumb_y + thumb_h);
                    if (mb_pressed && over_thumb) ch.sb_dragging = true;
                    if (ch.sb_dragging && aiml_input_mouse_button(0) && denom > 0.0f) {
                        float frac = (my - track_y - thumb_h * 0.5f) / denom;
                        if (frac < 0.0f) frac = 0.0f;
                        if (frac > 1.0f) frac = 1.0f;
                        ch.scroll_offset = (int)(frac * (float)max_off + 0.5f);
                    }
                    /* Body drag scroll (touch/mouse): a button-0 drag whose
                     * ORIGIN lies in this text box scrolls it — that way the
                     * text box can be scrolled by finger on a phone (no wheel/thumb needed).
                     * Only if the thumb is NOT being dragged.  Text boxes have NO
                     * draggable children → no conflict with card drag; costs only arise
                     * while a drag is active. */
                    if (!ch.sb_dragging && aiml_input_drag_active(0)) {
                        float ox = aiml_input_drag_origin_x(0);
                        float oy = aiml_input_drag_origin_y(0);
                        if (ox >= bx && ox < bx + bw && oy >= by && oy < by + bh) {
                            if (!ch.tb_drag) { ch.tb_drag = true; ch.tb_drag_off = ch.scroll_offset; }
                            float line_h_screen = bh / (float)std::max(1, visible);
                            if (line_h_screen < 1.0f) line_h_screen = 1.0f;
                            int delta_lines = (int)(aiml_input_drag_delta_y(0) / line_h_screen);
                            int no = ch.tb_drag_off - delta_lines;
                            if (no < 0) no = 0;
                            if (no > max_off) no = max_off;
                            ch.scroll_offset = no;
                        }
                    } else {
                        ch.tb_drag = false;
                    }
                }
                /* Horizontal (spec/44_6 §5f): Shift+wheel moves the window,
                 * and the LOWER bar can be dragged.  Without that it would be
                 * a sign without a handle — one sees that it continues, and
                 * cannot get there. */
                if (!ch.wrap && ch.tb_content_w > ch.tb_inner_w && ch.tb_inner_w > 0.0f) {
                    float scx = (ch.width  > 0.0f) ? bw / ch.width  : 1.0f;
                    float scy = (ch.height > 0.0f) ? bh / ch.height : 1.0f;
                    float max_x = ch.tb_content_w - ch.tb_inner_w;
                    bool shift_held = aiml_input_key_down("lshift") || aiml_input_key_down("rshift");
                    if (over && shift_held) {
                        float wy = aiml_input_mouse_scroll_y();
                        if (wy != 0.0f) {
                            ch.tb_off_x -= wy * 48.0f * scx;
                            if (ch.tb_off_x < 0.0f)  ch.tb_off_x = 0.0f;
                            if (ch.tb_off_x > max_x) ch.tb_off_x = max_x;
                        }
                    }
                    float sb_h    = 6.0f * scy;
                    float sb_y    = by + bh - sb_h - 2.0f * scy;
                    float track_x = bx + 2.0f * scx;
                    float track_w = bw - 4.0f * scx - 8.0f * scx;
                    float thumb_w = track_w * (ch.tb_inner_w / ch.tb_content_w);
                    if (thumb_w < 16.0f * scx) thumb_w = 16.0f * scx;
                    float denom_x = track_w - thumb_w;
                    float thumb_x = track_x + (denom_x > 0.0f && max_x > 0.0f
                                               ? denom_x * (ch.tb_off_x / max_x) : 0.0f);
                    bool over_tx = (my >= sb_y && my <= sb_y + sb_h &&
                                    mx >= thumb_x && mx <= thumb_x + thumb_w);
                    /* A click NEXT TO the handle grabs too: it jumps there
                     * and keeps dragging — otherwise one has to hit the narrow handle,
                     * and exactly that is the reason why a bar
                     * "won't move". */
                    bool over_track = (my >= sb_y - 2.0f * scy && my <= sb_y + sb_h + 2.0f * scy &&
                                       mx >= track_x && mx <= track_x + track_w);
                    if (mb_pressed && (over_tx || over_track)) ch.sb_dragging_x = true;
                    if (ch.sb_dragging_x && aiml_input_mouse_button(0) && denom_x > 0.0f) {
                        float frac = (mx - track_x - thumb_w * 0.5f) / denom_x;
                        if (frac < 0.0f) frac = 0.0f;
                        if (frac > 1.0f) frac = 1.0f;
                        ch.tb_off_x = frac * max_x;
                    }
                }
                if (mb_released) { ch.sb_dragging = false; ch.sb_dragging_x = false; }
            }
        }
    }

    if (mb_released) s_press_valid = false;

    /* Apply focus changes on mouse-press only. */
    if (mb_pressed) {
        /* Clear previous focus */
        if (g_scene->focused_input_inst && g_scene->focused_input_child_idx >= 0) {
            auto* old = g_scene->focused_input_inst;
            int32_t oi = g_scene->focused_input_child_idx;
            if (oi < (int32_t)old->children.size()) {
                PrefabChild& oc = old->children[oi];
                oc.focused = false;
                oc.sel_anchor = oc.cursor_pos;   /* collapse selection on blur */
                oc.sel_dragging = false;
            }
        }
        g_scene->focused_input_inst = new_focus_inst;
        g_scene->focused_input_child_idx = new_focus_idx;
        if (new_focus_inst && new_focus_idx >= 0) {
            /* cursor/anchor were set by the click branch above; just take focus. */
            new_focus_inst->children[new_focus_idx].focused = true;
        }
    }

    /* Popup dropdown interaction after the normal widget loop.  Iterates over
     * all open dropdowns, resolves row click / scroll / click outside. */
    float scroll_dy = aiml_input_mouse_scroll_y();
    int32_t mb_held = aiml_input_mouse_button(0);
    int32_t key_up_p    = aiml_input_key_pressed("Up");
    int32_t key_down_p  = aiml_input_key_pressed("Down");
    int32_t key_enter_p = aiml_input_key_pressed("Enter");
    int32_t key_esc_p   = aiml_input_key_pressed("Escape");
    /* Index instead of iterator, the same danger as in the widget pass: the
     * row callbacks of an open dropdown list may create prefabs. */
    const size_t inst_count_dd = g_scene->instances.size();
    for (size_t ii_d = 0; ii_d < inst_count_dd; ii_d++) {
        if (ii_d >= g_scene->instances.size()) break;
        auto* inst = g_scene->instances[ii_d];
        if (!inst) continue;
        if (!inst->visible) continue;
        for (auto& ch : inst->children) {
            if (ch.type != CHILD_DROPDOWN || !ch.dropdown_open) continue;
            float bx, by, bw, bh;
            child_screen_bbox(inst, ch, bx, by, bw, bh);
            /* count the options */
            int n_opts = ch.options.empty() ? 0 : 1;
            for (char c : ch.options) if (c == '|') n_opts++;
            if (n_opts <= 0) { ch.dropdown_open = false; continue; }
            const int MAX_VIS = 10;
            int visible = n_opts < MAX_VIS ? n_opts : MAX_VIS;
            /* row height: bound to the font, approx. box height */
            float row_h = bh;
            float popup_x, popup_y, popup_w, popup_h;
            compute_popup_rect(bx, by, bw, bh, n_opts, popup_x, popup_y, popup_w, popup_h);
            bool over_popup = (mx >= popup_x && mx < popup_x + popup_w
                            && my >= popup_y && my < popup_y + popup_h);
            bool over_box   = (mx >= bx      && mx < bx + bw
                            && my >= by      && my < by + bh);
            /* Scroll — as long as the popup is open (even if the mouse is over the box). */
            if (n_opts > visible && scroll_dy != 0.0f) {
                int step = 0;
                if (scroll_dy > 0.0f) step =  (int)ceilf(scroll_dy);
                else                  step = -(int)ceilf(-scroll_dy);
                ch.scroll_offset -= step;
                if (ch.scroll_offset < 0) ch.scroll_offset = 0;
                if (ch.scroll_offset > n_opts - visible) ch.scroll_offset = n_opts - visible;
            }
            /* Keyboard-Navigation solange popup offen. */
            if (key_up_p) {
                if (ch.selected_index > 0) ch.selected_index--;
                if (ch.selected_index < ch.scroll_offset) ch.scroll_offset = ch.selected_index;
            }
            if (key_down_p) {
                if (ch.selected_index < n_opts - 1) ch.selected_index++;
                if (ch.selected_index >= ch.scroll_offset + visible)
                    ch.scroll_offset = ch.selected_index - visible + 1;
            }
            if (key_enter_p) {
                ch.clicked_frame = true;
                ch.dropdown_open = false;
                if (!ch.on_click_fn.empty())
                    prefab_invoke_callback(ch.on_click_fn, inst);
                if (!ch.on_change_fn.empty())
                    prefab_invoke_callback(ch.on_change_fn, inst);
                continue;
            }
            if (key_esc_p) {
                ch.dropdown_open = false;
                continue;
            }
            /* Scrollbar-Drag: bbox rechts im Popup.  Start on press,
             * follow while held, release stops. */
            float sx2 = 1.0f; { float rsx, rsy; get_rel_scale(rsx, rsy); sx2 *= rsx; }
            float sb_w = 6.0f * sx2;
            float sb_x = popup_x + popup_w - sb_w - 2.0f * sx2;
            float sb_track_y = popup_y + 2.0f * sx2;
            float sb_track_h = popup_h - 4.0f * sx2;
            bool over_sb = (n_opts > visible)
                        && (mx >= sb_x && mx < sb_x + sb_w
                            && my >= sb_track_y && my < sb_track_y + sb_track_h);
            if (mb_pressed && over_sb) {
                ch.sb_dragging = true;
            }
            if (!mb_held) ch.sb_dragging = false;
            if (ch.sb_dragging && sb_track_h > 0.0f) {
                float rel = (my - sb_track_y) / sb_track_h;
                if (rel < 0.0f) rel = 0.0f; if (rel > 1.0f) rel = 1.0f;
                ch.scroll_offset = (int)(rel * (float)(n_opts - visible) + 0.5f);
                if (ch.scroll_offset < 0) ch.scroll_offset = 0;
                if (ch.scroll_offset > n_opts - visible) ch.scroll_offset = n_opts - visible;
            }
            /* Row click (not during a scrollbar drag). */
            if (mb_released && over_popup && !ch.sb_dragging && !over_sb) {
                int row = (int)((my - popup_y) / row_h) + ch.scroll_offset;
                if (row >= 0 && row < n_opts) {
                    ch.selected_index = row;
                    ch.clicked_frame  = true;
                    if (!ch.on_click_fn.empty())
                        prefab_invoke_callback(ch.on_click_fn, inst);
                    if (!ch.on_change_fn.empty())
                        prefab_invoke_callback(ch.on_change_fn, inst);
                }
                ch.dropdown_open = false;
            } else if (mb_released && !over_popup && !over_box && !ch.sb_dragging) {
                ch.dropdown_open = false;
            }
        }
    }

    /* Feed keyboard events into focused text_input. */
    apply_text_events_to_focus();
#ifdef __EMSCRIPTEN__
    /* Raise/lower the mobile soft keyboard on focus changes. */
    prefab_web_kbd_sync();
#endif
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

/* Resolve a shader STEM → on-disk path for the current target (asset_runtime).
 * Declared here to avoid pulling in the whole asset_runtime.h. */
extern "C" const char* aiml_asset_shader_path(const char* stem);

static GLuint material_get_or_compile(const char* shader_path) {
    if (!g_scene || !shader_path || !*shader_path) return 0;
    auto it = g_scene->shader_programs.find(shader_path);
    if (it != g_scene->shader_programs.end()) return it->second;

    /* A bare stem (no '/' and no '.') is resolved via the asset registry so it
     * works on both native (abs path) and wasm (preload path), like gl.tex /
     * gl.font.  A raw path (contains '/' or '.') is opened as-is (legacy). */
    std::string path_str = shader_path;
    if (path_str.find('/') == std::string::npos &&
        path_str.find('.') == std::string::npos) {
        const char* resolved = aiml_asset_shader_path(shader_path);
        if (resolved && *resolved) path_str = resolved;
    }

    std::string body = read_text_file(path_str.c_str());
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
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) {
        char buf[512]; glGetProgramInfoLog(p, 512, nullptr, buf);
        fprintf(stderr, "[material] link error for '%s': %s\n", shader_path, buf);
        /* A linked-but-failed program must NOT be used — glUseProgram on it
         * renders garbage/nothing and can wreck an unrelated widget that shares
         * the batch.  Drop it and cache the failure (0) so the batch falls back
         * cleanly to the default shader instead. */
        glDeleteProgram(p);
        g_scene->shader_programs[shader_path] = 0;
        return 0;
    }

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
    /* spec/61 — return an RC-managed copy (aiml_str_dup, NOT strdup): the returned string carries
     * the AIML refcount header, so the compiler-emitted decref on the receiving local frees it
     * correctly (a raw strdup has no header → decref would corrupt the heap). Handing back
     * ch->text.c_str() would dangle once the widget's text is reassigned; the consumer owns the
     * returned buffer (balanced by its scope-drop). */
    auto* ch = find_child(inst, child_name);
    return aiml_str_dup(ch ? ch->text.c_str() : "");
}

int32_t aiml_prefab_child_clicked(void* inst, const char* child_name) {
    auto* ch = find_child(inst, child_name);
    return (ch && ch->clicked_frame) ? 1 : 0;
}

/* spec/44_6 §5f — selection of a textbox/text_input, programmatically.
 * select_all gives the widget focus too, so a following Ctrl+C in the widget
 * copies it; `selection` hands back the marked text ("" when nothing is
 * marked), which an app can pass to input.clipboard_set from a button. */
void aiml_prefab_select_all(void* inst, const char* child_name) {
    auto* ch = find_child(inst, child_name);
    if (!ch) return;
    if (ch->type != CHILD_TEXTBOX && ch->type != CHILD_TEXT_INPUT) return;
    ch->sel_anchor = 0;
    ch->cursor_pos = (int32_t)ch->text.size();
    ch->focused    = true;
    if (g_scene) {
        auto* pi = (AimlPrefabInstance*)inst;
        auto it = pi->child_index.find(child_name ? child_name : "");
        if (it != pi->child_index.end()) {
            /* Collect the previous focus, so that two widgets are not focused. */
            if (g_scene->focused_input_inst && g_scene->focused_input_child_idx >= 0) {
                auto* old = g_scene->focused_input_inst;
                int32_t oi = g_scene->focused_input_child_idx;
                if (oi < (int32_t)old->children.size() && (old != pi || oi != it->second)) {
                    old->children[oi].focused    = false;
                    old->children[oi].sel_anchor = old->children[oi].cursor_pos;
                }
            }
            g_scene->focused_input_inst      = pi;
            g_scene->focused_input_child_idx = it->second;
        }
    }
}

/* spec/44_0 §2.5 — what a Ctrl+C in the currently focused widget would copy:
 * the selection, for a textbox without a selection the whole content.  The browser
 * must know this WITHIN the keydown (writing to the clipboard requires an
 * ongoing user interaction), while the aiml frame loop only runs afterwards
 * — hence this synchronous hook for input_runtime. */
extern "C" const char* aiml_focused_copy_text(void) {
    if (!g_scene || !g_scene->focused_input_inst) return "";
    AimlPrefabInstance* inst = g_scene->focused_input_inst;
    int32_t idx = g_scene->focused_input_child_idx;
    if (idx < 0 || idx >= (int32_t)inst->children.size()) return "";
    PrefabChild& ch = inst->children[idx];
    if (ch.type != CHILD_TEXTBOX && ch.type != CHILD_TEXT_INPUT) return "";
    int32_t len = (int32_t)ch.text.size();
    int32_t lo = ch.sel_anchor < ch.cursor_pos ? ch.sel_anchor : ch.cursor_pos;
    int32_t hi = ch.sel_anchor < ch.cursor_pos ? ch.cursor_pos : ch.sel_anchor;
    if (lo < 0) lo = 0;
    if (hi > len) hi = len;
    static std::string out;
    if (hi > lo)                       out = ch.text.substr((size_t)lo, (size_t)(hi - lo));
    else if (ch.type == CHILD_TEXTBOX) out = ch.text;    /* without a selection: the whole box */
    else                               out.clear();
    return out.c_str();
}

const char* aiml_prefab_selection(void* inst, const char* child_name) {
    auto* ch = find_child(inst, child_name);
    if (!ch) return aiml_str_dup("");
    int32_t len = (int32_t)ch->text.size();
    int32_t lo = ch->sel_anchor < ch->cursor_pos ? ch->sel_anchor : ch->cursor_pos;
    int32_t hi = ch->sel_anchor < ch->cursor_pos ? ch->cursor_pos : ch->sel_anchor;
    if (lo < 0) lo = 0;
    if (hi > len) hi = len;
    if (hi <= lo) return aiml_str_dup("");
    return aiml_str_dup(ch->text.substr((size_t)lo, (size_t)(hi - lo)).c_str());
}

/* Point-in-child test in SCREEN coords — works for ANY child type (incl.
 * non-interactive `rect`s like the deck-set slots), unlike child_clicked which
 * only tracks interactive widgets.  Uses the same screen-bbox as click hit-
 * detection, so window scaling is handled.  Used for drop-target detection. */
int32_t aiml_prefab_child_contains(void* inst_p, const char* child_name,
                                   float sx, float sy) {
    auto* inst = (AimlPrefabInstance*)inst_p;
    if (!inst || !child_name) return 0;
    auto it = inst->child_index.find(child_name);
    if (it == inst->child_index.end()) return 0;
    float bx, by, bw, bh;
    child_screen_bbox(inst, inst->children[it->second], bx, by, bw, bh);
    return (sx >= bx && sx < bx + bw && sy >= by && sy < by + bh) ? 1 : 0;
}

/* spec/44_6: hit area of a child in screen coordinates — the inverse
 * of child_contains, which only checks a point one already has.  Needed so that
 * a test driver can resolve "button X" into a cursor position (spec/44_9).
 * -1 means "child does not exist"; it is not a valid coordinate and thereby
 * distinguishable from a real area. */
static int32_t child_bbox_or_fail(void* inst_p, const char* child_name,
                                  float& bx, float& by, float& bw, float& bh) {
    auto* inst = (AimlPrefabInstance*)inst_p;
    if (!inst || !child_name) return 0;
    auto it = inst->child_index.find(child_name);
    if (it == inst->child_index.end()) return 0;
    child_screen_bbox(inst, inst->children[it->second], bx, by, bw, bh);
    return 1;
}
#define PREFAB_RECT_GETTER(fn, field)                                          \
    extern "C" float fn(void* inst_p, const char* child_name) {                \
        float bx, by, bw, bh;                                                  \
        if (!child_bbox_or_fail(inst_p, child_name, bx, by, bw, bh)) return -1.f; \
        return field;                                                          \
    }
PREFAB_RECT_GETTER(aiml_prefab_child_rect_x, bx)
PREFAB_RECT_GETTER(aiml_prefab_child_rect_y, by)
PREFAB_RECT_GETTER(aiml_prefab_child_rect_w, bw)
PREFAB_RECT_GETTER(aiml_prefab_child_rect_h, bh)
#undef PREFAB_RECT_GETTER

/* child_design_x/y/w/h — the same rectangle as child_rect_*, but in DESIGN
 * coordinates (1280x720), i.e. divided by the window scaling.
 *
 * Why both: child_rect_* returns SCREEN pixels (for hit tests and
 * cursor positions), but set_pos/x/y of a prefab expect DESIGN pixels.
 * Whoever wanted to place an instance next to a child (tooltip, speech bubble) inevitably
 * mixed two spaces until now: with a window != 1280x720 the
 * instance landed off target, shifted by the scaling factor.  That is exactly how
 * the tour speech bubble in 9rooms was noticed (2026-10-25).
 * -1 means, as with child_rect_*, "child does not exist". */
#define PREFAB_DESIGN_GETTER(fn, field)                                        \
    extern "C" float fn(void* inst_p, const char* child_name) {                \
        float bx, by, bw, bh;                                                  \
        if (!child_bbox_or_fail(inst_p, child_name, bx, by, bw, bh)) return -1.f; \
        float rsx, rsy; get_rel_scale(rsx, rsy);                               \
        if (rsx <= 0.0f) rsx = 1.0f;                                           \
        if (rsy <= 0.0f) rsy = 1.0f;                                           \
        return field;                                                          \
    }
PREFAB_DESIGN_GETTER(aiml_prefab_child_design_x, bx / rsx)
PREFAB_DESIGN_GETTER(aiml_prefab_child_design_y, by / rsy)
PREFAB_DESIGN_GETTER(aiml_prefab_child_design_w, bw / rsx)
PREFAB_DESIGN_GETTER(aiml_prefab_child_design_h, bh / rsy)
#undef PREFAB_DESIGN_GETTER

extern "C" void aiml_prefab_trace_hits(int32_t on) { g_trace_hits = (on != 0); }
/* spec/44_6: the resolved hits of the last press, "|"-separated.
 * "" = the press hit nothing.  That makes `expect_hit` (spec/44_9) checkable without
 * log parsing — the log is for humans, this is for tests. */
extern "C" const char* aiml_prefab_last_hit(void) { return g_last_hits.c_str(); }

int32_t aiml_prefab_child_focused(void* inst, const char* child_name) {
    auto* ch = find_child(inst, child_name);
    return (ch && ch->focused) ? 1 : 0;
}

int32_t aiml_prefab_child_checked(void* inst, const char* child_name) {
    auto* ch = find_child(inst, child_name);
    return (ch && ch->checked) ? 1 : 0;
}

void aiml_prefab_set_checked(void* inst, const char* child_name, int32_t checked) {
    auto* ch = find_child(inst, child_name);
    if (ch) ch->checked = (checked != 0);
}

/* ── Drag-and-drop ───────────────────────────────────────────────────────── */

/* Pick the topmost draggable instance containing (px,py) and begin dragging it.
 * drag_offset is stored in WORLD pixels relative to the PICK point, drag_origin
 * stays local for snap-back.  Shared by update_drag (picks at the cursor) and
 * update_drag_gesture (picks at the press point). */
static bool prefab_drag_pick_at(float px, float py) {
    /* spec/44_6 "Modal": no drag begins under a blocking instance.
     * Without this "blocks input beneath" would only mean "blocks clicks" —
     * the selection here skips non-draggables and would happily reach through
     * a dialog for the card beneath. */
    bool mod_found = false;
    int32_t mod_z = modal_block_z(px, py, mod_found);
    for (int i = (int)g_scene->instances.size() - 1; i >= 0; i--) {
        auto* inst = g_scene->instances[i];
        if (!inst->visible || !inst->draggable) continue;
        if (inst->pending_delete) continue;
        if (mod_found && effective_z(inst) < mod_z) continue;
        float ax, ay, w, h;
        inst_bounds(inst, ax, ay, w, h);
        if (px >= ax && px <= ax + w && py >= ay && py <= ay + h) {
            inst->is_dragging = true;
            inst->drag_offset_x = px - ax;
            inst->drag_offset_y = py - ay;
            inst->drag_origin_x = inst->x;
            inst->drag_origin_y = inst->y;
            g_scene->dragged = inst;
            g_scene->drag_source = inst;
            return true;
        }
    }
    return false;
}

/* Keep the dragged item under the cursor.  See the long note in update_drag for
 * the coordinate-space conversions. */
static void prefab_drag_follow(float mx, float my) {
    auto* d = g_scene->dragged;
    if (!d || !d->is_dragging) return;
    float tgt_wx = mx - d->drag_offset_x;
    float tgt_wy = my - d->drag_offset_y;
    if (d->is_relative && g_scene->screen_w > 0 && g_scene->screen_h > 0) {
        d->x = tgt_wx / g_scene->screen_w;
        d->y = tgt_wy / g_scene->screen_h;
    } else if (d->parent) {
        float pwx, pwy, psx, psy;
        inst_world_transform(d->parent, pwx, pwy, psx, psy);
        if (psx <= 0.0f) psx = 1.0f;
        if (psy <= 0.0f) psy = 1.0f;
        d->x = (tgt_wx - pwx) / psx + d->parent->scroll_x;
        d->y = (tgt_wy - pwy) / psy + d->parent->scroll_y;
    } else {
        float rsx, rsy; get_rel_scale(rsx, rsy);
        if (rsx <= 0.0f) rsx = 1.0f;
        if (rsy <= 0.0f) rsy = 1.0f;
        d->x = tgt_wx / rsx;
        d->y = tgt_wy / rsy;
    }
}

/* End the gesture: snap the item back to where it was picked up (a drag is a
 * GESTURE; the drop handler repositions it if it wants to) and clear state. */
static void prefab_drag_release(void) {
    if (!g_scene->dragged) return;
    g_scene->dragged->x = g_scene->dragged->drag_origin_x;
    g_scene->dragged->y = g_scene->dragged->drag_origin_y;
    g_scene->dragged->is_dragging = false;
    g_scene->dragged = nullptr;
}

/* spec/44_6: gesture-driven drag.  Picks at the PRESS POINT once the movement
 * threshold is crossed (input.drag_started), instead of at this frame's cursor
 * the moment the button is found down — so a moving cursor cannot shift the
 * pick to a different card, and a tap never becomes a drag.
 * 0 none, 1 candidate, 2 dragging, 3 dropped this frame, 4 cancelled this frame. */
int32_t aiml_prefab_update_drag_gesture(float mx, float my) {
    if (!g_scene) return 0;

    if (aiml_input_mouse_cancelled(0) || aiml_input_drag_cancelled(0)) {
        prefab_drag_release();
        return 4;
    }
    if (aiml_input_drag_started(0) && !g_scene->dragged)
        /* LATCHED start point, not the live press point: `drag_origin_*`
         * is overwritten immediately by every new press, but the started edge
         * is only reported in the next frame.  With "press A -> drag ->
         * release -> press B" in one frame gap the pick-up would otherwise
         * grab at B (spec/44_0 §2.3). */
        prefab_drag_pick_at(aiml_input_drag_start_x(0), aiml_input_drag_start_y(0));

    prefab_drag_follow(mx, my);

    if (aiml_input_drag_ended(0)) {
        if (g_scene->dragged) { prefab_drag_release(); return 3; }
        return 0;
    }
    if (g_scene->dragged) return 2;
    if (aiml_input_mouse_button(0)) return 1;
    return 0;
}

/* spec/44_6: width of `text` in the prefab font, including the
 * inline marker rules ({icon:…} counts ascent_px).  Makes
 * centring, wrapping AND the markers checkable — without this "the marker is
 * consumed instead of drawn" cannot be observed from outside. */
float aiml_prefab_text_width(const char* text) {
    if (!g_scene || !g_scene->font || !text) return 0.0f;
    return text_width_font(g_scene->font, text);
}

void* aiml_prefab_get_drag_source(void) {
    return g_scene ? g_scene->drag_source : nullptr;
}

void aiml_prefab_update_drag(float mx, float my, int32_t btn_down, int32_t btn_released) {
    if (!g_scene) return;

    /* spec/44_0 §2.2 + spec/44_6: an ABORTED press (touchcancel, second finger,
     * focus loss) ends the drag without a drop.  It must not look like a release:
     * callers detect a drop as "was dragging, now not", so a release here would
     * commit whatever the drop handler does — in a game, a move nobody made.
     * Checked before the start block so the same frame cannot re-grab. */
    if (aiml_input_mouse_cancelled(0) && g_scene->dragged) {
        g_scene->dragged->x = g_scene->dragged->drag_origin_x;
        g_scene->dragged->y = g_scene->dragged->drag_origin_y;
        g_scene->dragged->is_dragging = false;
        g_scene->dragged = nullptr;
        return;
    }

    if (btn_down && !g_scene->dragged) {
        /* Start drag: find topmost draggable instance under cursor.
         * Use the full world-transform bounds (inst_bounds) — the SAME transform
         * that rendering + widget hit-testing use — so NESTED (container-row)
         * draggables are picked at their actual drawn location, not at raw local
         * coords.  drag_offset is stored in WORLD pixels; drag_origin stays local
         * for snap-back. */
        for (int i = (int)g_scene->instances.size() - 1; i >= 0; i--) {
            auto* inst = g_scene->instances[i];
            if (!inst->visible || !inst->draggable) continue;

            float ax, ay, w, h;
            inst_bounds(inst, ax, ay, w, h);
            bool inside = (mx >= ax && mx <= ax + w && my >= ay && my <= ay + h);
            if (inside) {
                inst->is_dragging = true;
                inst->drag_offset_x = mx - ax;
                inst->drag_offset_y = my - ay;
                inst->drag_origin_x = inst->x;
                inst->drag_origin_y = inst->y;
                g_scene->dragged = inst;
                g_scene->drag_source = inst;
                break;
            }
        }
    }

    if (g_scene->dragged && g_scene->dragged->is_dragging) {
        /* Update position.  drag_offset is in WORLD pixels, so the desired
         * world top-left is (mx,my) - drag_offset.  Convert that back into the
         * coordinate space of d->x/d->y:
         *   - relative:  0..1 window ratio,
         *   - nested:    parent-local design coords (undo parent world+scale+scroll),
         *   - top-level: design coords (undo rel_scale).
         * so the item sticks to the cursor at any scale/nesting. */
        auto* d = g_scene->dragged;
        float tgt_wx = mx - d->drag_offset_x;
        float tgt_wy = my - d->drag_offset_y;
        if (d->is_relative && g_scene->screen_w > 0 && g_scene->screen_h > 0) {
            d->x = tgt_wx / g_scene->screen_w;
            d->y = tgt_wy / g_scene->screen_h;
        } else if (d->parent) {
            float pwx, pwy, psx, psy;
            inst_world_transform(d->parent, pwx, pwy, psx, psy);
            if (psx <= 0.0f) psx = 1.0f;
            if (psy <= 0.0f) psy = 1.0f;
            d->x = (tgt_wx - pwx) / psx + d->parent->scroll_x;
            d->y = (tgt_wy - pwy) / psy + d->parent->scroll_y;
        } else {
            float rsx, rsy; get_rel_scale(rsx, rsy);
            if (rsx <= 0.0f) rsx = 1.0f;
            if (rsy <= 0.0f) rsy = 1.0f;
            d->x = tgt_wx / rsx;
            d->y = tgt_wy / rsy;
        }
    }

    if (btn_released && g_scene->dragged) {
        /* End drag — SNAP the item back to where it was picked up.  A drag is a
         * GESTURE: the visual item returns to its origin unless the drop handler
         * explicitly repositions it (e.g. board zones move the card, the deck
         * editor only appends a card_id).  Without this, a dragged library card
         * is left "placed" wherever it was dropped instead of staying in the
         * browser.  drag_origin_{x,y} were recorded at drag start. */
        g_scene->dragged->x = g_scene->dragged->drag_origin_x;
        g_scene->dragged->y = g_scene->dragged->drag_origin_y;
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

/* Cursor position in DESIGN coordinates (= raw pixel / rel_scale).  set_pos and
 * friends work in design coords, but input.mouse_pos_* returns world/pixel — this
 * exposes the same conversion update_drag/hit_test use internally, so AIML can
 * place things at the cursor (drag-centering, tooltips, context menus …). */
float aiml_prefab_mouse_design_x(void) {
    float rsx, rsy; get_rel_scale(rsx, rsy);
    if (rsx <= 0.0f) rsx = 1.0f;
    return aiml_input_mouse_pos_x() / rsx;
}
float aiml_prefab_mouse_design_y(void) {
    float rsx, rsy; get_rel_scale(rsx, rsy);
    if (rsy <= 0.0f) rsy = 1.0f;
    return aiml_input_mouse_pos_y() / rsy;
}

/* ── Nested instances ────────────────────────────────────────────────────── */

/* Attach `child` under `parent`. The child's position/scale become local to
 * the parent and it renders with the parent's world transform. The child stays
 * in the scene's instance list but is skipped by the top-level draw/hit loops. */
void aiml_prefab_add_child(void* parent, void* child) {
    auto* p = (AimlPrefabInstance*)parent;
    auto* c = (AimlPrefabInstance*)child;
    if (!p || !c || p == c) return;
    /* Detach from a previous parent first. */
    if (c->parent) {
        auto& sib = c->parent->child_instances;
        sib.erase(std::remove(sib.begin(), sib.end(), c), sib.end());
    }
    c->parent = p;
    if (std::find(p->child_instances.begin(), p->child_instances.end(), c)
        == p->child_instances.end())
        p->child_instances.push_back(c);
}

/* Detach `child` from its parent (becomes a top-level instance again). */
void aiml_prefab_remove_child(void* parent, void* child) {
    auto* p = (AimlPrefabInstance*)parent;
    auto* c = (AimlPrefabInstance*)child;
    if (!c) return;
    if (p) {
        auto& sib = p->child_instances;
        sib.erase(std::remove(sib.begin(), sib.end(), c), sib.end());
    }
    c->parent = nullptr;
}

/* ── Scroll viewport / clipping ──────────────────────────────────────────── */

/* Enable/disable clipping of nested children to this instance's world bounds. */
void aiml_prefab_set_clip(void* p, int32_t enabled) {
    auto* inst = (AimlPrefabInstance*)p;
    if (inst) inst->clip_children = (enabled != 0);
}
/* Set the scroll offset (local px) applied to nested children. */
void aiml_prefab_set_scroll(void* p, float sx, float sy) {
    auto* inst = (AimlPrefabInstance*)p;
    if (inst) { inst->scroll_x = sx; inst->scroll_y = sy; }
}
float aiml_prefab_get_scroll_x(void* p) {
    auto* inst = (AimlPrefabInstance*)p; return inst ? inst->scroll_x : 0.0f;
}
float aiml_prefab_get_scroll_y(void* p) {
    auto* inst = (AimlPrefabInstance*)p; return inst ? inst->scroll_y : 0.0f;
}

/* ── Connectors (cables between instances) ───────────────────────────────── */

void* aiml_prefab_connect(void* from, void* to) {
    if (!g_scene || !from || !to) return nullptr;
    auto* c = new AimlConnector();
    c->from = (AimlPrefabInstance*)from;
    c->to   = (AimlPrefabInstance*)to;
    g_scene->connectors.push_back(c);
    return c;
}

void aiml_connector_set_colors(void* p, int32_t c0, int32_t c1) {
    auto* c = (AimlConnector*)p; if (c) { c->color0 = (uint32_t)c0; c->color1 = (uint32_t)c1; }
}
void aiml_connector_set_highlight(void* p, int32_t hl) {
    auto* c = (AimlConnector*)p; if (c) c->highlight = (uint32_t)hl;
}
void aiml_connector_set_width(void* p, float w) {
    auto* c = (AimlConnector*)p; if (c) c->width = w;
}
void aiml_connector_set_bow(void* p, float bow) {
    auto* c = (AimlConnector*)p; if (c) c->bow = bow;
}
void aiml_connector_set_visible(void* p, int32_t v) {
    auto* c = (AimlConnector*)p; if (c) c->visible = (v != 0);
}
void aiml_connector_set_z(void* p, int32_t z) {
    auto* c = (AimlConnector*)p; if (c) c->z_order = z;
}
void aiml_connector_set_port(void* p, void* tex, float scale) {
    auto* c = (AimlConnector*)p; if (!c) return;
    c->port_tex = tex ? ((AimlGLTexture*)tex)->id : 0;
    if (scale > 0.0f) c->port_scale = scale;
}
void aiml_connector_destroy(void* p) {
    auto* c = (AimlConnector*)p; if (!c || !g_scene) return;
    auto& v = g_scene->connectors;
    v.erase(std::remove(v.begin(), v.end(), c), v.end());
    delete c;
}

/* Set how cables attach to an instance: 0=center, 1=edge, 2=distributed. */
void aiml_prefab_set_connector_anchor(void* p, int32_t mode) {
    auto* inst = (AimlPrefabInstance*)p;
    if (inst) inst->connector_anchor = mode;
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

static AimlUIFont* font_for_size_named(PrefabScene* s, const std::string& name, float font_size) {
    if (name.empty()) return font_for_size(s, font_size);
    auto base = s->named_fonts.find(name);
    if (base == s->named_fonts.end() || !base->second) return font_for_size(s, font_size);
    int key = (int)roundf(font_size * 10.0f);
    auto& cache = s->named_cache[name];
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    AimlUIFont* f = (AimlUIFont*)aiml_ui_bake_font_for_size(base->second, font_size);
    if (f) cache[key] = f;
    return f;
}

void aiml_prefab_set_font(void* font) {
    if (!g_scene) return;
    clear_font_cache(g_scene);
    g_scene->font = (AimlUIFont*)font;
}

/* Register an additional named font for `font:` on a child (spec/44_6). */
void aiml_prefab_add_font(const char* name, void* font) {
    if (!g_scene || !name || !*name) return;
    auto& cache = g_scene->named_cache[name];
    for (auto& kv : cache) aiml_ui_free_font(kv.second);
    cache.clear();
    g_scene->named_fonts[name] = (AimlUIFont*)font;
}
