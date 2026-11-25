#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ui_runtime.h — AIML ui namespace (44.3)
 *
 * Immediate-Mode UI system:
 *   - Grid layout (normalized f32 column/row sizes, colspan/rowspan)
 *   - 2D batch renderer (quads + text, one GL draw call per texture group)
 *   - stb_truetype font rendering (GL_RED atlas)
 *   - Style push/pop stack
 *   - Input integration (via aiml_input_* polling)
 *
 * ui_font   = i8*  (opaque AimlUIFont*)
 * ui_style  = LLVM struct {i32×5, f32×6, f32(padding_x), i8*, f32×3, f32(_pad2)}
 *             72 bytes, 17 fields (see AimlUIStyle in .cpp)
 *   New fields (indices 11,13-15): padding_x, padding_y, size_w, size_h
 *
 * Widget style presets (all are heap-allocated opaque i8* handles):
 *   ui_slider_style, ui_button_style, ui_label_style,
 *   ui_progress_style, ui_panel_style
 *
 * AimlGLRawBuf (same layout as AIML buffer<T>):
 *   { i32 size, i32 capacity, i32 read_idx, i32 write_idx, u8* data }
 */

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

/* Call once before first ui.begin() — creates VAO/VBO/shader/white-pixel-tex. */
void aiml_ui_init (void);
/* Begin UI frame: reset draw-list, read window size, snapshot input state.
 * win = AimlWindow* (window_handle). */
void aiml_ui_begin(void* win);
/* Compute layout, run hit-tests, flush geometry to OpenGL. */
void aiml_ui_end  (void);

/* ── Scroll ───────────────────────────────────────────────────────────────── */

/* Set scroll axes for the NEXT begin_cell call.
 * axis = "x", "y", or "xy" — enables scrollbar(s) on that cell. */
void aiml_ui_set_scroll(const char* axis);

/* ── Grid ─────────────────────────────────────────────────────────────────── */

/* cols_buf / rows_buf = AimlGLRawBuf* pointing at buffer<f32> data.
 * Each f32: < 1.0 = fixed fraction; == 1.0 = flexible (shares remaining space). */
void aiml_ui_begin_grid(void* cols_buf, void* rows_buf);
void aiml_ui_end_grid  (void);
/* Gap between cells in logical pixels; must be set before begin_grid. */
void aiml_ui_set_gap   (float h, float v);
/* Enter a cell; colspan/rowspan ≥ 1. */
void aiml_ui_begin_cell(int32_t col, int32_t row, int32_t colspan, int32_t rowspan);
void aiml_ui_end_cell  (void);
/* Query current grid cell rect in pixels. Returns 0 on success, -1 if no grid. */
int32_t aiml_ui_grid_cell_rect(int32_t col, int32_t row, int32_t colspan, int32_t rowspan,
                                float* out_x, float* out_y, float* out_w, float* out_h);
/* Horizontal: "left"/"center"/"right"; Vertical: "top"/"middle"/"bottom".
 * Must be called before begin_cell. */
void aiml_ui_set_align (const char* h, const char* v);

/* ── Text widgets ─────────────────────────────────────────────────────────── */

void        aiml_ui_label         (const char* text, void* preset);
void        aiml_ui_label_colored (const char* text, int32_t color, void* preset);
/* Returns currently edited string (static buffer, valid until next text_input call). */
const char* aiml_ui_text_input    (const char* id, const char* buf, void* preset);
const char* aiml_ui_text_area     (const char* id, const char* buf, void* preset);
const char* aiml_ui_password_input(const char* id, const char* buf, void* preset);
const char* aiml_ui_password_mask (const char* text);

/* spec/44_3 §4.1 — metrics of the font ui.label would use.  preset may be NULL.
 * Window pixels, like widget rects.  0.0 when there is no font: a guessed size
 * would be indistinguishable from a measurement at the call site. */
float aiml_ui_measure_text (const char* text, void* preset);
float aiml_ui_text_height  (void* preset);

/* spec/44_3 §4.6 — free drawing.  Coordinates are window pixels (same space as
 * input.mouse_pos); width is logical pixels (DPI-scaled). */
void aiml_ui_draw_line     (float x0, float y0, float x1, float y1,
                            float width, int32_t color);
void aiml_ui_draw_polyline (void* pts_buf, float width, int32_t color);
/* Text at a caller-chosen position; x/y is the TOP-LEFT of the line box, like every
 * other draw_* coordinate.  preset may be NULL. */
void aiml_ui_draw_text     (float x, float y, const char* text, int32_t color,
                            void* preset);
/* A filled rect at a caller-chosen position.  ui.rect fills the CELL; this fills what
 * it is given, in window pixels -- no DPI scaling, unlike a line's width. */
void aiml_ui_draw_rect     (float x, float y, float w, float h, int32_t color);
/* A texture at a caller-chosen rect; x/y is its top-left, as for every draw_*. */
void aiml_ui_draw_image    (float x, float y, float w, float h, void* tex);

/* ── Interactive widgets ──────────────────────────────────────────────────── */

/* Returns 1 on the frame the button is clicked, 0 otherwise.
 * preset = AimlUIButtonStyle* (ui_button_style*) or null. */
int32_t aiml_ui_button      (const char* label, void* preset);
/* Button with icon texture (gl_texture* = AimlGLTexture*) left of text. */
int32_t aiml_ui_button_icon (const char* label, void* tex);
/* Returns new checked state (0/1). */
int32_t aiml_ui_checkbox    (const char* id, int32_t value, void* preset);
/* Returns new toggled state (0/1). */
int32_t aiml_ui_toggle      (const char* id, int32_t value, void* preset);
/* Returns selected option index. */
int32_t aiml_ui_radio       (const char* id, int32_t option, int32_t current, void* preset);
/* Dropdown / spinbox — opts_list = pointer to list<string> struct {i32 size, i32 cap, char** data}. */
int32_t aiml_ui_spinbox     (const char* id, void* opts_list, int32_t current, void* preset);
/* Returns 1 if the current ui.begin_cell(...) contains the mouse cursor. */
int32_t aiml_ui_cell_hovered (void);

/* ── Value widgets ────────────────────────────────────────────────────────── */

float   aiml_ui_slider     (const char* id, float value, float mn, float mx, void* preset);
int32_t aiml_ui_slider_int (const char* id, int32_t value, int32_t mn, int32_t mx, void* preset);
float   aiml_ui_drag_f32   (const char* id, float value, float speed, void* preset);

/* ── Visualization ────────────────────────────────────────────────────────── */

/* value in [0.0, 1.0] — fills cell width. */
void aiml_ui_progress_bar        (float value, void* preset);
void aiml_ui_progress_bar_colored(float value, int32_t fg, int32_t bg, void* preset);
/* tex = AimlGLTexture* (gl_texture). Image fills available cell space. */
void aiml_ui_image               (void* tex);
/* Image at explicit size in logical pixels. */
void aiml_ui_image_sized         (void* tex, float w, float h);
/* Solid filled rect (fills cell). color = RGBA i32. */
void aiml_ui_rect                (int32_t color);
/* Rounded rect via SDF in fragment shader. */
void aiml_ui_rect_rounded        (int32_t color, float radius, void* preset);
/* Horizontal separator line. */
void aiml_ui_separator           (void);
/* Vertical empty space in logical pixels. */
void aiml_ui_spacer              (float size);

/* ── Notebook / Tab widget ────────────────────────────────────────────────── */

/* begin_notebook renders a tab bar (from previous frame's tabs) with a built-in
 * "−" remove button, and returns the active tab index (0-based). */
int32_t aiml_ui_begin_notebook         (const char* id);
/* Register a tab in the current notebook.  Returns 1 if this tab is active. */
int32_t aiml_ui_notebook_tab           (const char* label);
/* End the notebook. */
void    aiml_ui_end_notebook           (void);
/* Returns 1 if the "−" button in the tab bar was clicked this frame. */
int32_t aiml_ui_notebook_remove_clicked(const char* id);

/* ── Intra-cell layout ────────────────────────────────────────────────────── */

/* Next widget on same line as previous. */
void aiml_ui_same_line(void);
/* Move cursor to new line. */
void aiml_ui_newline  (void);

/* ── Style ────────────────────────────────────────────────────────────────── */

/* style_ptr = AimlUIStyle* (ui_style value on stack, passed by pointer). */
void aiml_ui_push_style       (void* style_ptr);
void aiml_ui_pop_style        (void);
/* Set global base style (bottom of stack). */
void aiml_ui_set_default_style(void* style_ptr);
/* Copy current top-of-stack style into *out (for initializing a new ui_style decl). */
void aiml_ui_copy_current_style(void* out);

/* ── Font ─────────────────────────────────────────────────────────────────── */

/* Load TTF from path (AimlPath* = interned char*) at given logical pixel size. */
void* aiml_ui_load_font        (void* path_ptr, float size);
/* Load built-in default font (tries common system TTF paths). */
void* aiml_ui_load_font_default(float size);
/* Bake a new atlas at a different size using the same TTF data as base_font. */
void* aiml_ui_bake_font_for_size(void* base_font, float size_px);
/* Free a font created by load_font / bake_font_for_size. */
void  aiml_ui_free_font        (void* font);
/* Set default font for all subsequent ui_style declarations. */
void  aiml_ui_set_default_font (void* font);

/* ── Widget style preset factories ───────────────────────────────────────── */

void* aiml_ui_slider_style_new   (void);
void* aiml_ui_button_style_new   (void);
void* aiml_ui_label_style_new    (void);
void* aiml_ui_progress_style_new (void);
void* aiml_ui_panel_style_new    (void);

/* ── Slider style setters ─────────────────────────────────────────────────── */

void aiml_ui_slider_style_set_thumb        (void* s, void* t);
void aiml_ui_slider_style_set_track        (void* s, void* t);
void aiml_ui_slider_style_set_color        (void* s, int32_t c);
void aiml_ui_slider_style_set_bg_color     (void* s, int32_t c);
void aiml_ui_slider_style_set_hover_color  (void* s, int32_t c);
void aiml_ui_slider_style_set_active_color (void* s, int32_t c);
void aiml_ui_slider_style_set_border_color (void* s, int32_t c);
void aiml_ui_slider_style_set_corner_radius(void* s, float r);
void aiml_ui_slider_style_set_border_width (void* s, float w);
void aiml_ui_slider_style_set_font         (void* s, void* f);
void aiml_ui_slider_style_set_padding_x    (void* s, float v);
void aiml_ui_slider_style_set_padding_y    (void* s, float v);
void aiml_ui_slider_style_set_size_w       (void* s, float v);
void aiml_ui_slider_style_set_size_h       (void* s, float v);

/* ── Button style setters ─────────────────────────────────────────────────── */

void aiml_ui_button_style_set_bg           (void* s, void* t);
void aiml_ui_button_style_set_hover        (void* s, void* t);
void aiml_ui_button_style_set_active       (void* s, void* t);
void aiml_ui_button_style_set_color        (void* s, int32_t c);
void aiml_ui_button_style_set_bg_color     (void* s, int32_t c);
void aiml_ui_button_style_set_hover_color  (void* s, int32_t c);
void aiml_ui_button_style_set_active_color (void* s, int32_t c);
void aiml_ui_button_style_set_border_color (void* s, int32_t c);
void aiml_ui_button_style_set_corner_radius(void* s, float r);
void aiml_ui_button_style_set_border_width (void* s, float w);
void aiml_ui_button_style_set_font         (void* s, void* f);
void aiml_ui_button_style_set_padding_x    (void* s, float v);
void aiml_ui_button_style_set_padding_y    (void* s, float v);
void aiml_ui_button_style_set_size_w       (void* s, float v);
void aiml_ui_button_style_set_size_h       (void* s, float v);

/* ── Label style setters ──────────────────────────────────────────────────── */

void aiml_ui_label_style_set_color        (void* s, int32_t c);
void aiml_ui_label_style_set_bg_color     (void* s, int32_t c);
void aiml_ui_label_style_set_hover_color  (void* s, int32_t c);
void aiml_ui_label_style_set_active_color (void* s, int32_t c);
void aiml_ui_label_style_set_border_color (void* s, int32_t c);
void aiml_ui_label_style_set_corner_radius(void* s, float r);
void aiml_ui_label_style_set_border_width (void* s, float w);
void aiml_ui_label_style_set_font         (void* s, void* f);
void aiml_ui_label_style_set_padding_x    (void* s, float v);
void aiml_ui_label_style_set_padding_y    (void* s, float v);
void aiml_ui_label_style_set_size_w       (void* s, float v);
void aiml_ui_label_style_set_size_h       (void* s, float v);

/* ── Progress style setters ───────────────────────────────────────────────── */

void aiml_ui_progress_style_set_fill        (void* s, void* t);
void aiml_ui_progress_style_set_bg_tex      (void* s, void* t);
void aiml_ui_progress_style_set_color       (void* s, int32_t c);
void aiml_ui_progress_style_set_bg_color    (void* s, int32_t c);
void aiml_ui_progress_style_set_hover_color (void* s, int32_t c);
void aiml_ui_progress_style_set_active_color(void* s, int32_t c);
void aiml_ui_progress_style_set_border_color(void* s, int32_t c);
void aiml_ui_progress_style_set_corner_radius(void* s, float r);
void aiml_ui_progress_style_set_border_width(void* s, float w);
void aiml_ui_progress_style_set_font        (void* s, void* f);
void aiml_ui_progress_style_set_padding_x   (void* s, float v);
void aiml_ui_progress_style_set_padding_y   (void* s, float v);
void aiml_ui_progress_style_set_size_w      (void* s, float v);
void aiml_ui_progress_style_set_size_h      (void* s, float v);

/* ── Panel style setters ──────────────────────────────────────────────────── */

void aiml_ui_panel_style_set_bg_tex       (void* s, void* t);
void aiml_ui_panel_style_set_color        (void* s, int32_t c);
void aiml_ui_panel_style_set_bg_color     (void* s, int32_t c);
void aiml_ui_panel_style_set_hover_color  (void* s, int32_t c);
void aiml_ui_panel_style_set_active_color (void* s, int32_t c);
void aiml_ui_panel_style_set_border_color (void* s, int32_t c);
void aiml_ui_panel_style_set_corner_radius(void* s, float r);
void aiml_ui_panel_style_set_border_width (void* s, float w);
void aiml_ui_panel_style_set_font         (void* s, void* f);
void aiml_ui_panel_style_set_padding_x    (void* s, float v);
void aiml_ui_panel_style_set_padding_y    (void* s, float v);
void aiml_ui_panel_style_set_size_w       (void* s, float v);
void aiml_ui_panel_style_set_size_h       (void* s, float v);

#ifdef __cplusplus
}
#endif
