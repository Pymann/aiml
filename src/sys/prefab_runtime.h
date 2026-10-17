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
int32_t aiml_prefab_is_alive (void* inst);

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

/* Scene-resize: nur Screen-Dims + Projection updaten, design_w/h bleibt.
 * Wird vom Framebuffer-Size-Callback gerufen. */
void aiml_prefab_scene_resize(int32_t screen_w, int32_t screen_h);

/* ── Layout helpers (Spec 44_6 §Layout) ────────────────────────────────
 * Positionieren eine Liste von prefab_instances automatisch.  Compiler-Glue
 * extrahiert data + size aus der list<prefab_instance> (AimlList-Struct)
 * via ExtractValue und reicht sie als (data_ptr, count) weiter.
 */
void aiml_engine_layout_hflow(void* data_ptr, int32_t count, float x, float y, float gap);
void aiml_engine_layout_vflow(void* data_ptr, int32_t count, float x, float y, float gap);
void aiml_engine_layout_stack(void* data_ptr, int32_t count,
                              float x, float y, float dx, float dy, int32_t z_step);

/* ── Child property access ────────────────────────────────────────────── */

/* Set text content of a label child. */
void  aiml_prefab_set_text   (void* inst, const char* child_name, const char* text);
/* Set texture of a sprite child. tex = AimlGLTexture* (gl_texture). */
void  aiml_prefab_set_texture(void* inst, const char* child_name, void* tex);
/* Set color of a child (label text color, rect fill color). */
void  aiml_prefab_set_color  (void* inst, const char* child_name, int32_t color);
void  aiml_prefab_set_bg_color    (void* inst, const char* child_name, int32_t color);
void  aiml_prefab_set_border_color(void* inst, const char* child_name, int32_t color);
/* Set child visibility. */
void  aiml_prefab_set_child_visible(void* inst, const char* child_name, int32_t v);
/* Override a child's font_size at runtime (labels / text_inputs / buttons). */
void  aiml_prefab_set_child_font_size(void* inst, const char* child_name, float size_px);
/* Toggle compacting layout on a container child (hidden rows take no space). */
void  aiml_prefab_container_set_compact(void* inst, const char* child_name, int32_t v);
/* Get a child as a prefab_instance (for direct manipulation). */
void* aiml_prefab_child      (void* inst, const char* child_name);
/* Access spawned rows inside a container-widget (bind_list-driven).  Enables
 * AIML code to iterate rows and drive set_text / set_texture / … on each. */
int32_t aiml_prefab_container_row_count(void* inst, const char* child_name);
void*   aiml_prefab_container_row      (void* inst, const char* child_name, int32_t idx);

/* ── Phase 6f: cascade metadata ─────────────────────────────────────────
 * Register a cascade type so the widget-runtime can render a CHILD_CASCADE
 * widget bound to it: dropdown options come from the nodes, payload rows
 * come from the current node's fields.  `nodes_json` layout:
 *   [{"name":"foo","id":1,"label":"Foo","fields":[
 *     {"name":"deck","type":"DeckType","default":""},
 *     {"name":"value","type":"ValueType","default":""}]},
 *    …]
 */
void aiml_prefab_register_cascade(const char* name, const char* nodes_json,
                                  const char* opcode_enum);
/* Persist a full cascade row into a bound list<list<i32>>.  Grows the inner
 * list<i32> to fit tag + N payload slots, zero-fills, and stamps:
 *     row[0]     = tag
 *     row[1..N]  = values[0..N-1]
 * `outer_ptr` is the AIML list<list<i32>> struct.  `bound_index` is the row
 * position within it (must be < outer->size — the caller pushes/pops rows
 * via list.push/list.pop first).  Standalone from the widget-runtime so
 * it's directly unit-testable. */
void aiml_cascade_persist_row(void* outer_ptr, int32_t bound_index,
                              int32_t tag,
                              const int32_t* values, int32_t n_values);
/* Cascade validation (spec/44_7_cascade_validate.md).  `row_data` is the data
 * pointer of a list<i32> (n = len elements).  validate → -1 valid, >=0 the
 * 0-based bad index, -2 unknown cascade.  explain → human reason ("" if valid).
 * Both use the compile-time-registered g_cascades + g_enum_values tables. */
int32_t     aiml_cascade_validate(const char* cascade_name, const void* row_data, int32_t len);
const char* aiml_cascade_explain (const char* cascade_name, const void* row_data, int32_t len);
/* Collect the row indices of EVERY payload field of a given type within the
 * node that `row` selects (a type may recur — revive: [47, OwnerRef, OwnerRef,
 * …]).  Walks the node's declared fields (recursing into nested cascades,
 * consuming widths exactly like validate) and pushes each 0-based row index of
 * a field whose declared type == `field_type` (e.g. "TargetType") into the
 * caller-supplied list<i32> `out_list`.  Returns the count (0 = the field does
 * not occur in that node), or -2 for an unknown cascade.  Lets callers ask
 * "where — if at all — does this enum appear in this effect/trigger/cost row?"
 * without hard-coding per-node layouts. */
int32_t     aiml_cascade_field_indices(const char* cascade_name, const void* row_data,
                                       int32_t len, const char* field_type, void* out_list);
/* Register an enum type so cascade payload widgets can render its values as
 * a dropdown.  `labels_pipe` is the same "A|B|C" format widgets already use;
 * `values_pipe` is the parallel "0|1|2" of the members' i32 values so widgets
 * can return the real enum value (not selected_index+1) for sparse/non-1-based
 * enums.  Passed at program init from the compiler.                        */
void aiml_prefab_register_enum(const char* name, const char* labels_pipe,
                               const char* values_pipe);
/* Retrieve the labels_pipe for a registered enum (empty string on miss). */
const char* aiml_prefab_enum_labels(const char* name);
/* The registered i32 value of the enum member at options-index `opt_idx`.
 * Returns -1 if the enum / index is unknown.  Lets a dropdown/radio bound to
 * an enum return the true member value regardless of ordering or gaps.     */
int32_t     aiml_prefab_enum_value_at(const char* name, int32_t opt_idx);
/* Pipe-separated node labels for a registered cascade (dropdown options).
 * Returned pointer is owned by the runtime; caller must not free.       */
const char* aiml_prefab_cascade_options(const char* cascade_name);
/* Bytecode id for the node at options-index (as displayed by the dropdown).
 * -1 if the cascade / index is unknown.                                  */
int32_t     aiml_prefab_cascade_node_id_at(const char* cascade_name, int32_t opt_idx);
/* Options-index for a given node id (inverse of the above).  -1 on miss. */
int32_t     aiml_prefab_cascade_opt_idx_for(const char* cascade_name, int32_t node_id);
/* Field count of the node with the given bytecode id.  -1 on miss.       */
int32_t     aiml_prefab_cascade_field_count(const char* cascade_name, int32_t node_id);
/* Type name of the i-th field of the given node.  Empty string on miss.  */
const char* aiml_prefab_cascade_field_type(const char* cascade_name, int32_t node_id, int32_t field_idx);
/* Field label (falls back to auto-derived name if none set).             */
const char* aiml_prefab_cascade_field_label(const char* cascade_name, int32_t node_id, int32_t field_idx);

/* ── Phase 6: model binding ─────────────────────────────────────────────
 * Bind a `list<list<i32>>` to a prefab-instance.  The prefab's `container`
 * children then read the list length each frame and spawn / kill sub-
 * instances of `spawn:` to match.  `list_ptr` is the raw pointer to the
 * AIML list struct { i32 size, i32 capacity, ptr data } — cast from
 * `ptr<...>` at the call site or take &effects.items on a Model wrapper. */
void  aiml_prefab_bind_list(void* inst, void* list_ptr);
/* Per-widget bind — associate a `list<list<i32>>` with a specific container
 * child.  Use this when a single AbilityPage hosts separate Trigger /
 * Effects / Costs containers each pointing at a different list on the
 * bound model.  Falls back to the instance-level bound_list when unset.  */
void  aiml_prefab_child_bind_list(void* inst, const char* child_name, void* list_ptr);
/* Read back the bound list pointer (nullptr if unbound). */
void* aiml_prefab_bound_list(void* inst);
/* For a prefab-instance spawned by a container: which index in the parent's
 * bound_list does this instance correspond to.  Returns -1 if standalone. */
int32_t aiml_prefab_bound_index(void* inst);

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
/* Cursor position in DESIGN coords (raw pixel / rel_scale). */
float aiml_prefab_mouse_design_x(void);
float aiml_prefab_mouse_design_y(void);

/* ── Bar/gauge child updates (type: "bar") ───────────────────────────────── */
void  aiml_prefab_set_bar_value(void* inst, const char* child_name, float value);
void  aiml_prefab_set_bar_max(void* inst, const char* child_name, float max_value);
void  aiml_prefab_set_bar_fill(void* inst, const char* child_name, int32_t color);

/* ── Slider child updates (type: "slider") ───────────────────────────────── */
float aiml_prefab_get_slider_value(void* inst, const char* child_name);
void  aiml_prefab_set_slider_value(void* inst, const char* child_name, float value);
void  aiml_prefab_set_slider_min  (void* inst, const char* child_name, float min_value);
void  aiml_prefab_set_slider_max  (void* inst, const char* child_name, float max_value);

/* ── Nested instances ────────────────────────────────────────────────────── */

/* Attach/detach a prefab instance under another. A nested child inherits the
 * parent's world transform and is skipped by top-level draw + hit-test. */
void  aiml_prefab_add_child(void* parent, void* child);
void  aiml_prefab_remove_child(void* parent, void* child);

/* Scroll viewport: clip nested children to this instance's bounds + scroll. */
void  aiml_prefab_set_clip(void* inst, int32_t enabled);
void  aiml_prefab_set_scroll(void* inst, float sx, float sy);
float aiml_prefab_get_scroll_x(void* inst);
float aiml_prefab_get_scroll_y(void* inst);

/* Give keyboard focus to a text_input child; pin a textbox/scrollable container
 * to its bottom (and re-arm textbox tail-follow). */
void  aiml_prefab_set_focus(void* inst, const char* child_name);
void  aiml_prefab_scroll_to_end(void* inst, const char* child_name);

/* ── Connectors (cables between two instances) ───────────────────────────── */

/* Create a cable from `from` to `to`. Endpoints follow the instances each
 * frame. Returns an opaque AimlConnector* (or NULL). */
void* aiml_prefab_connect(void* from, void* to);
void  aiml_connector_set_colors(void* c, int32_t color0, int32_t color1);
void  aiml_connector_set_highlight(void* c, int32_t hl);
void  aiml_connector_set_width(void* c, float w);
void  aiml_connector_set_bow(void* c, float bow);
void  aiml_connector_set_visible(void* c, int32_t v);
void  aiml_connector_set_z(void* c, int32_t z);
void  aiml_connector_set_port(void* c, void* tex, float scale);
void  aiml_connector_destroy(void* c);
/* Anchor mode for cables touching this instance: 0=center,1=edge,2=distributed. */
void  aiml_prefab_set_connector_anchor(void* inst, int32_t mode);

/* ── Utility ──────────────────────────────────────────────────────────── */

/* Dock prefab to a UI grid cell axis.
 * dock_x: sets inst.x = cell_x, scale_x = cell_w / def.width
 * dock_y: sets inst.y = cell_y, scale_y = cell_h / def.height
 * Must be called while a ui.begin_grid is active. */
void aiml_prefab_dock_x(void* inst, int32_t col, int32_t colspan);
void aiml_prefab_dock_y(void* inst, int32_t row, int32_t rowspan);

/* Set the font used for label children. font = AimlUIFont* from ui.load_font(). */
void aiml_prefab_set_font(void* font);
/* spec/44_6: extra named font, selected per child with `font: "<name>"`. */
void aiml_prefab_add_font(const char* name, void* font);
/* spec/44_6 §5g: colour runs over a text field — triples (start, len, rgba). */
void aiml_prefab_set_color_runs(void* inst, const char* child_name,
                                const void* triples, int32_t count);
void aiml_prefab_clear_color_runs(void* inst, const char* child_name);

/* ── UI-widget callbacks ─────────────────────────────────────────────── */

/* Register a callback function pointer by name. The AIML compiler emits one
 * call per (prefab, child, event)-tuple at program init. Subsequent button
 * clicks / text-input changes look up the function via this table and call
 * it with the prefab_instance as the single argument. */
void aiml_prefab_register_callback(const char* fn_name, void* fn_ptr);

/* ── Material registration ─────────────────────────────────────────────
 * Materials bundle a fragment shader with a list of uniform values. The
 * AIML compiler emits one `aiml_material_register` call per `- material:`
 * declaration. The shader is lazily compiled on first use.
 *
 * `uniforms_json` is a compact JSON object whose keys are uniform names and
 * whose values describe the uniform's type and value:
 *
 *   {"u_intensity": {"k":"f", "v":0.6},
 *    "u_tint":      {"k":"v4","v":[0.4,0.7,1.0,1.0]},
 *    "u_facet":     {"k":"v2","v":[28.0,22.0]},
 *    "u_count":     {"k":"i", "v":42},
 *    "u_noise":     {"k":"t", "v":"resources/noise.png"}}
 *
 * "k" type tags: "f"=float, "v2"/"v3"/"v4"=vecN, "i"=int, "t"=sampler2D.
 */
void aiml_material_register(const char* name, const char* shader_path,
                            const char* uniforms_json);

/* ── UI-widget property accessors ────────────────────────────────────── */

/* Returns the current text contents of a text_input child. Lifetime: valid
 * until the next aiml_prefab_set_text or destroy on this child. */
const char* aiml_prefab_text_value(void* inst, const char* child_name);
/* spec/44_6 §5f — Auswahl eines textbox/text_input programmatisch setzen/lesen. */
void        aiml_prefab_select_all(void* inst, const char* child_name);
const char* aiml_prefab_selection(void* inst, const char* child_name);
/* Returns 1 if a button child has been clicked this frame, 0 otherwise. */
int32_t     aiml_prefab_child_clicked(void* inst, const char* child_name);
int32_t     aiml_prefab_child_contains(void* inst, const char* child_name, float sx, float sy);
/* Returns 1 if a child has keyboard focus (text_input). */
int32_t     aiml_prefab_child_focused(void* inst, const char* child_name);
/* Returns the toggle state of a checkbox child (1 = checked). */
int32_t     aiml_prefab_child_checked(void* inst, const char* child_name);
/* Set the toggle state of a checkbox child (0/1). Does not fire callbacks. */
void        aiml_prefab_set_checked(void* inst, const char* child_name, int32_t checked);

/* Dropdown widget: read/write selected index; read the current option label. */
int32_t     aiml_prefab_dropdown_index(void* inst, const char* child_name);
const char* aiml_prefab_dropdown_value(void* inst, const char* child_name);
/* Selected option's enum value for a dropdown bound to an enum (`enum:` in the
 * prefab child).  Returns the real i32 member value via the enum registry;
 * -1 if the child isn't found or isn't enum-bound. */
int32_t     aiml_prefab_dropdown_enum_value(void* inst, const char* child_name);
void        aiml_prefab_set_dropdown_index(void* inst, const char* child_name, int32_t idx);
/* Set selection by value: matches value against the child's pipe-separated
 * options and sets the corresponding index.  No-op if value isn't in the list. */
void        aiml_prefab_set_dropdown_selected(void* inst, const char* child_name, const char* value);
void        aiml_prefab_set_dropdown_options(void* inst, const char* child_name, const char* options);
/* Widget↔model binding (spec 44.6 §5d): attach a model instance to a prefab
 * whose children carry compiler-baked (bind_offset, bind_tag), then move each
 * bound field to/from its widget.  Sync with no model attached is a no-op. */
void        aiml_prefab_bind_model(void* inst, void* model_ptr);
void        aiml_prefab_sync_from_model(void* inst);
void        aiml_prefab_sync_to_model(void* inst);

#ifdef __cplusplus
}
#endif
