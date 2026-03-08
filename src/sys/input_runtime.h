#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * input_runtime.h — AIML input namespace (44.0)
 *
 * Three-layer architecture:
 *   Mapping-Layer   input.axis("forward") / input.action_pressed("jump")
 *   Polling-Layer   input.key_down("W")   / input.mouse_delta()
 *   Queue-Layer     input.has_event()     / input.next_event()
 *
 * Desktop: GLFW 3.x callbacks + glfwGetGamepadState
 * Browser/Android: stubs
 *
 * input_event is an opaque AimlInputEvent* returned by input.next_event().
 * Field access via aiml_input_event_*() functions (registered as builtinMethods).
 */

/* ── input_event field accessors ─────────────────────────────────────────── */

const char* aiml_input_event_type    (void* ev);  /* "key_down"|"key_up"|"mouse_move"|
                                                      "mouse_button_down"|"mouse_button_up"|
                                                      "mouse_scroll"|"text" */
const char* aiml_input_event_key     (void* ev);  /* AIML key name; set for key_down/key_up */
int32_t     aiml_input_event_btn     (void* ev);  /* button index; set for mouse_button_* */
float       aiml_input_event_pos_x   (void* ev);  /* cursor x; set for mouse_move / mouse_button_* */
float       aiml_input_event_pos_y   (void* ev);  /* cursor y; set for mouse_move / mouse_button_* */
float       aiml_input_event_scroll_x(void* ev);  /* scroll delta x; set for mouse_scroll */
float       aiml_input_event_scroll_y(void* ev);  /* scroll delta y; set for mouse_scroll */
const char* aiml_input_event_char_   (void* ev);  /* UTF-8 char string; set for text */

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

/* Call once after engine.create_window.  win is a window_handle (AimlWindow*).
 * Registers GLFW callbacks and enables gamepad mapping. */
void aiml_input_init       (void* win_handle);
/* Called automatically by engine.swap/engine.run each frame (via registered callback). */
void aiml_input_update     (void);
/* Cursor lock: true = hidden + confined to window (FPS-Look); false = visible. */
void aiml_input_set_capture(int32_t on);

/* ── Raw polling: keyboard ────────────────────────────────────────────────── */

int32_t aiml_input_key_down    (const char* key);  /* currently held */
int32_t aiml_input_key_pressed (const char* key);  /* went down this frame (rising edge) */
int32_t aiml_input_key_released(const char* key);  /* came up this frame (falling edge) */

/* ── Raw polling: mouse ───────────────────────────────────────────────────── */

float   aiml_input_mouse_pos_x    (void);
float   aiml_input_mouse_pos_y    (void);
float   aiml_input_mouse_delta_x  (void);  /* movement since last update() */
float   aiml_input_mouse_delta_y  (void);
int32_t aiml_input_mouse_button   (int32_t btn);   /* currently held */
int32_t aiml_input_mouse_pressed  (int32_t btn);   /* went down this frame */
int32_t aiml_input_mouse_released (int32_t btn);   /* came up this frame */
float   aiml_input_mouse_scroll_x (void);  /* scroll delta this frame */
float   aiml_input_mouse_scroll_y (void);

/* ── Raw polling: gamepad ─────────────────────────────────────────────────── */

int32_t aiml_input_gamepad_connected(int32_t pad);
/* axis: 0=LeftX 1=LeftY 2=RightX 3=RightY 4=LeftTrigger 5=RightTrigger
 * Radial deadzone of 0.1 applied; output remapped to full -1..+1. */
float   aiml_input_gamepad_axis     (int32_t pad, int32_t axis);
int32_t aiml_input_gamepad_button   (int32_t pad, int32_t btn);
int32_t aiml_input_gamepad_pressed  (int32_t pad, int32_t btn);

/* ── Event queue ──────────────────────────────────────────────────────────── */

int32_t aiml_input_has_event  (void);
/* Returns AimlInputEvent* (pointer to internal static buffer) — valid until
 * the next call to aiml_input_next_event(). */
void*   aiml_input_next_event (void);
void    aiml_input_clear_events(void);

/* ── Mapping: axis ────────────────────────────────────────────────────────── */

void aiml_input_map_axis        (const char* name, const char* key,   float scale);
void aiml_input_map_axis_mouse  (const char* name, const char* comp,  float scale);  /* comp: "x"|"y" */
void aiml_input_map_axis_gamepad(const char* name, int32_t pad, int32_t axis, float scale);

/* ── Mapping: action ─────────────────────────────────────────────────────── */

void aiml_input_map_action        (const char* name, const char* key);
void aiml_input_map_action_mouse  (const char* name, int32_t btn);
void aiml_input_map_action_gamepad(const char* name, int32_t pad, int32_t btn);

/* ── Mapping: removal ────────────────────────────────────────────────────── */

void aiml_input_unmap        (const char* name);
void aiml_input_clear_mappings(void);

/* ── Mapping: query ──────────────────────────────────────────────────────── */

float   aiml_input_axis           (const char* name);
int32_t aiml_input_action_down    (const char* name);
int32_t aiml_input_action_pressed (const char* name);
int32_t aiml_input_action_released(const char* name);

#ifdef __cplusplus
}
#endif
