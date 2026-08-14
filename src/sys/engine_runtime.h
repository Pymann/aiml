#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * engine_runtime.h — AIML engine namespace (cross-platform window + game loop)
 *
 * Desktop (default): GLFW 3.x, OpenGL 3.3 Core
 * Browser (stub):    Emscripten Canvas + emscripten_set_main_loop_arg + Asyncify
 * Android (stub):    GameActivity + EGL
 *
 * All functions operate on an opaque AimlWindow* (void*).
 * The global active window is set during engine.run / engine.swap so that
 * engine.dt() / engine.fps() / engine.frame() work without a win argument.
 */

/* ── Window creation / destruction ──────────────────────────────────────── */

void* aiml_engine_create_window           (int32_t w, int32_t h, const char* title);
void* aiml_engine_create_window_fullscreen(const char* title);
void  aiml_engine_destroy_window          (void* win);
void  aiml_engine_set_title               (void* win, const char* title);
void  aiml_engine_maximize_window         (void* win);
int32_t aiml_engine_window_size_x         (void* win);
int32_t aiml_engine_window_size_y         (void* win);

/* ── GL context ──────────────────────────────────────────────────────────── */

void aiml_engine_make_current(void* win);
void aiml_engine_set_vsync   (int32_t on);

/* ── Game loop ───────────────────────────────────────────────────────────── */

/* engine.run: cross-platform entry point.
 * update_fn: void(*)(float dt)   — called once per frame
 * render_fn: void(*)(void)       — called after update each frame         */
void    aiml_engine_run    (void* win, void* update_fn, void* render_fn);
int32_t aiml_engine_running(void* win);
void    aiml_engine_swap   (void* win);
void    aiml_engine_quit   (void* win);
int32_t aiml_engine_is_iconified(void* win);
int32_t aiml_engine_is_focused  (void* win);
int32_t aiml_engine_is_visible  (void* win);

/* ── Frame timing (read global active window) ────────────────────────────── */

float   aiml_engine_dt        (void);  /* seconds since last frame, clamped 0.1 */
double  aiml_engine_time      (void);  /* seconds since program start           */
float   aiml_engine_fps       (void);  /* rolling average over last 60 frames   */
int64_t aiml_engine_frame     (void);  /* total frame counter (starts at 0)     */
void    aiml_engine_target_fps(int32_t fps); /* 0 = unlimited                   */

/* ── Platform information ────────────────────────────────────────────────── */

const char* aiml_engine_platform   (void);  /* "desktop" | "web" | "android" */
int32_t     aiml_engine_screen_size_x(void);
int32_t     aiml_engine_screen_size_y(void);
float       aiml_engine_dpi        (void);
int32_t     aiml_engine_is_mobile  (void);  /* 1 on Android */
int32_t     aiml_engine_is_web     (void);  /* 1 in Browser */

/* ── Called by input_runtime during engine.run ───────────────────────────── */
void aiml_engine_register_input_update(void (*fn)(void));

/* ── Internal helpers for other runtime modules ──────────────────────────── */
/* Returns the platform-native window pointer (GLFWwindow* on desktop, NULL elsewhere).
 * Used by input_runtime to attach GLFW callbacks. */
void* aiml_engine_glfw_window(void* win_ptr);

#ifdef __cplusplus
}
#endif
