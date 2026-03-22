#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * prefab_runtime.h — AIML prefab system (scene graph + sprite rendering)
 *
 * Prefabs are reusable visual entity templates defined in YAML:
 *   - prefab:
 *       name: CardFace
 *       width: 200
 *       height: 280
 *       texture: "assets/frame.png"
 *       draggable: true
 *       children:
 *         - { name: title, type: label, x: 100, y: 250, ... }
 *         - { name: art,   type: sprite, x: 15, y: 100, ... }
 *
 * Runtime creates instances via engine.instantiate("CardFace") → prefab_instance (i8*).
 * engine.draw_scene() renders all instances sorted by z_order.
 *
 * prefab_instance = i8* (opaque AimlPrefabInstance*)
 *
 * Child types:  sprite (textured quad), label (text), rect (solid color)
 */

/* ── Scene lifecycle ──────────────────────────────────────────────────── */

/* Initialize the prefab scene (shader, quad VAO, projection).
 * Must be called after GL context + ui.init(). */
void aiml_prefab_scene_init(int32_t screen_w, int32_t screen_h);

/* Register a prefab definition from the compiler.
 * children_json: JSON array of child defs (parsed at compile-time). */
void aiml_prefab_register(const char* name, float width, float height,
                          const char* texture_path, int32_t draggable,
                          const char* children_json);

/* ── Instantiation / destruction ──────────────────────────────────────── */

void* aiml_prefab_instantiate(const char* prefab_name);
void  aiml_prefab_destroy    (void* inst);

/* ── Transform (instance) ─────────────────────────────────────────────── */

void  aiml_prefab_set_pos     (void* inst, float x, float y);
/* Set position as ratio of screen (0.0-1.0). Automatically scales on resize. */
void  aiml_prefab_set_pos_rel (void* inst, float rx, float ry);
float aiml_prefab_get_x       (void* inst);
float aiml_prefab_get_y       (void* inst);
void  aiml_prefab_set_scale   (void* inst, float s);
float aiml_prefab_get_scale   (void* inst);
void  aiml_prefab_set_rotation(void* inst, float r);
float aiml_prefab_get_rotation(void* inst);
void  aiml_prefab_set_z       (void* inst, int32_t z);
int32_t aiml_prefab_get_z     (void* inst);
void  aiml_prefab_set_visible (void* inst, int32_t v);
int32_t aiml_prefab_get_visible(void* inst);
void  aiml_prefab_set_alpha   (void* inst, float a);
float aiml_prefab_get_alpha   (void* inst);
void  aiml_prefab_set_draggable(void* inst, int32_t d);
float aiml_prefab_get_width   (void* inst);
float aiml_prefab_get_height  (void* inst);

/* ── Child property access ────────────────────────────────────────────── */

/* Set text content of a label child. */
void  aiml_prefab_set_text   (void* inst, const char* child_name, const char* text);
/* Set texture of a sprite child. tex = AimlGLTexture* (gl_texture). */
void  aiml_prefab_set_texture(void* inst, const char* child_name, void* tex);
/* Set color of a child (label text color, rect fill color). */
void  aiml_prefab_set_color  (void* inst, const char* child_name, int32_t color);
/* Set child visibility. */
void  aiml_prefab_set_child_visible(void* inst, const char* child_name, int32_t v);
/* Get a child as a prefab_instance (for direct manipulation). */
void* aiml_prefab_child      (void* inst, const char* child_name);

/* ── Scene rendering ──────────────────────────────────────────────────── */

/* Draw all visible instances sorted by z_order.
 * Call after gl.clear_all() and before ui.begin(). */
void aiml_prefab_draw_scene(void);

/* ── Hit testing ──────────────────────────────────────────────────────── */

/* Returns topmost visible instance at (mx, my) in screen coords, or NULL.
 * Iterates in reverse z_order (top-to-bottom). */
void* aiml_prefab_hit_test(float mx, float my);

/* ── Hover tracking ──────────────────────────────────────────────────── */

/* Update hover state. Call once per frame with mouse position.
 * Performs hit_test and sets hovered flag on the topmost instance. */
void  aiml_prefab_update_hover(float mx, float my);
/* Returns currently hovered instance, or NULL. */
void* aiml_prefab_get_hovered(void);
/* Returns 1 if this instance is currently hovered, 0 otherwise. */
int32_t aiml_prefab_is_hovered(void* inst);
/* Returns 1 if this instance was clicked this frame (hovered + btn pressed). */
int32_t aiml_prefab_was_clicked(void* inst, int32_t btn);

/* ── Drag-and-drop ────────────────────────────────────────────────────── */

/* Update drag state. Call once per frame with mouse state.
 * btn_down = mouse button currently held; btn_released = released this frame. */
void  aiml_prefab_update_drag(float mx, float my, int32_t btn_down, int32_t btn_released);
/* Returns currently dragged instance, or NULL. */
void* aiml_prefab_get_dragged(void);
/* Cancel active drag: restore position to drag_origin and clear dragged state. */
void  aiml_prefab_cancel_drag(void* inst);

/* ── Utility ──────────────────────────────────────────────────────────── */

/* Dock prefab to a UI grid cell axis.
 * dock_x: sets inst.x = cell_x, scale_x = cell_w / def.width
 * dock_y: sets inst.y = cell_y, scale_y = cell_h / def.height
 * Must be called while a ui.begin_grid is active. */
void aiml_prefab_dock_x(void* inst, int32_t col, int32_t colspan);
void aiml_prefab_dock_y(void* inst, int32_t row, int32_t rowspan);

/* Set the font used for label children. font = AimlUIFont* from ui.load_font(). */
void aiml_prefab_set_font(void* font);

#ifdef __cplusplus
}
#endif
