/*
 * engine_runtime.cpp — AIML engine namespace
 *
 * Platform selection:
 *   __EMSCRIPTEN__ → Browser (stubs + emscripten_set_main_loop_arg)
 *   __ANDROID__    → Android NDK (stubs)
 *   default        → Desktop (GLFW 3.x)
 */

#include "engine_runtime.h"
#include "input_runtime.h"
#include "prefab_runtime.h"  /* aiml_prefab_scene_init für resize-callback */

#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>

/* ── Platform includes ───────────────────────────────────────────────────── */

#if defined(__EMSCRIPTEN__)
    #include <emscripten.h>
    #include <emscripten/html5.h>
    #include <GLES3/gl3.h>   /* glViewport etc. — Emscripten WebGL2, no GLEW */
#elif defined(__ANDROID__)
    /* Android NDK + EGL stubs — filled in later */
    struct android_app; /* forward decl placeholder */
#else
    /* Desktop: GLFW + GLEW */
    #include <GL/glew.h>
    #include <GLFW/glfw3.h>
    #include <time.h>   /* nanosleep */
#endif

/* ── Internal window struct ──────────────────────────────────────────────── */

struct AimlWindow {
#if defined(__EMSCRIPTEN__)
    char canvas_id[64];
#elif defined(__ANDROID__)
    void*   app;         /* android_app* placeholder */
    void*   display;     /* EGLDisplay placeholder   */
    void*   surface;     /* EGLSurface placeholder   */
    void*   egl_ctx;     /* EGLContext placeholder   */
#else
    GLFWwindow* glfw;
#endif
    int     width;
    int     height;
    double  start_time;
    double  last_time;
    float   dt;
    int64_t frame_count;
    float   target_frame_ms;   /* 0 = unlimited */
    int     quit_requested;

    /* FPS tracking — rolling average of last 60 frame durations */
    float   fps_samples[60];
    int     fps_idx;
    float   fps_cached;
};

/* ── Globals ──────────────────────────────────────────────────────────────── */

static AimlWindow*  g_win          = nullptr;
static void       (*g_input_update)(void) = nullptr;

/* ── Helper: get current time in seconds ─────────────────────────────────── */

static double now_sec() {
#if defined(__EMSCRIPTEN__)
    return emscripten_get_now() * 0.001;
#elif defined(__ANDROID__)
    return 0.0; /* stub */
#else
    return glfwGetTime();
#endif
}

/* ── Window creation ─────────────────────────────────────────────────────── */

void* aiml_engine_create_window(int32_t w, int32_t h, const char* title) {
#if defined(__EMSCRIPTEN__)
    (void)title;
    AimlWindow* win = new AimlWindow();
    memset(win, 0, sizeof(*win));
    snprintf(win->canvas_id, sizeof(win->canvas_id), "#canvas");

    /* Honor the actual canvas backing store size — shell.html sizes the canvas
     * to fill the viewport, and JS sets its width/height to clientWidth × dpr.
     * Reading those back here keeps the AIML window dimensions, WebGL viewport
     * and ui_runtime layout in sync with what the page actually displays. The
     * `w, h` requested by the AIML caller are treated as a minimum / fallback
     * if the page hasn't sized the canvas yet. */
    int can_w = 0, can_h = 0;
    emscripten_get_canvas_element_size(win->canvas_id, &can_w, &can_h);
    if (can_w <= 0) can_w = w;
    if (can_h <= 0) can_h = h;
    /* Persist this size on the canvas so subsequent JS sees it. */
    emscripten_set_canvas_element_size(win->canvas_id, can_w, can_h);

    /* Explicitly request a WebGL2 context and make it current — emcc does NOT
     * auto-bind a context, so the first glXxx call would crash with
     * "GLctx is undefined". */
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.alpha            = EM_FALSE;
    attrs.depth            = EM_TRUE;
    attrs.stencil          = EM_FALSE;
    /* No MSAA on the default framebuffer: on mobile GPUs an antialiased +
     * preserved backbuffer is a heavy per-frame cost (extra samples + resolve +
     * copy) and the UI is axis-aligned sprites/text that barely benefit.  This
     * was a major cause of unusably-low framerate on phones. */
    attrs.antialias        = EM_FALSE;
    attrs.majorVersion     = 2;  /* WebGL2 */
    attrs.minorVersion     = 0;
    /* Keep the last complete frame in the drawing buffer.  Our WASM frame loop
     * is a manual Asyncify while-loop (emscripten_sleep-paced, NOT rAF), so the
     * browser compositor is not frame-synced to our draws; without this it can
     * composite a cleared/half-drawn buffer → flicker.  Preserving the buffer
     * makes the compositor always show the last finished frame. */
    attrs.preserveDrawingBuffer = EM_TRUE;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE gl =
        emscripten_webgl_create_context(win->canvas_id, &attrs);
    if (gl <= 0) {
        fprintf(stderr, "[engine] emscripten_webgl_create_context failed: %d\n", (int)gl);
        delete win;
        return nullptr;
    }
    emscripten_webgl_make_context_current(gl);

    win->width  = can_w;
    win->height = can_h;
    win->start_time = now_sec();
    win->last_time  = win->start_time;
    g_win = win;
    return win;

#elif defined(__ANDROID__)
    /* stub — Android window arrives via GameActivity callback */
    AimlWindow* win = new AimlWindow();
    memset(win, 0, sizeof(*win));
    win->width  = w;
    win->height = h;
    win->start_time = 0;
    win->last_time  = 0;
    g_win = win;
    return win;

#else
    /* Desktop: GLFW */
    if (!glfwInit()) {
        fprintf(stderr, "[engine] glfwInit failed\n");
        return nullptr;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWwindow* glfw = glfwCreateWindow(w, h, title ? title : "", nullptr, nullptr);
    if (!glfw) {
        fprintf(stderr, "[engine] glfwCreateWindow failed\n");
        glfwTerminate();
        return nullptr;
    }
    glfwMakeContextCurrent(glfw);

    /* GLEW */
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        fprintf(stderr, "[engine] glewInit failed\n");
        glfwDestroyWindow(glfw);
        glfwTerminate();
        return nullptr;
    }

    /* Initial viewport matches actual framebuffer (may differ from window size on HiDPI) */
    {
        int fbw, fbh;
        glfwGetFramebufferSize(glfw, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
    }

    /* Keep viewport + prefab scene size in sync when the user resizes/
     * maximizes the window.  scene_init's re-init-Branch updated nur
     * screen_w/h + Projection — design_w/h bleibt vom ersten Aufruf
     * → ratio (screen/design) ≠ 1 → UI skaliert mit. */
    glfwSetFramebufferSizeCallback(glfw, [](GLFWwindow*, int fbw, int fbh) {
        glViewport(0, 0, fbw, fbh);
        /* scene_resize updated nur screen_w/h — design_w/h bleibt fixed
         * vom ersten scene_init.  Wichtig: nicht scene_init nutzen, sonst
         * würde das initial-create design = maximized-Dims setzen wenn
         * der Callback vor dem explizten scene_init feuert (z.B. weil
         * engine.maximize() aufgerufen wurde). */
        aiml_prefab_scene_resize(fbw, fbh);
    });

    AimlWindow* win = new AimlWindow();
    memset(win, 0, sizeof(*win));
    win->glfw        = glfw;
    win->width       = w;
    win->height      = h;
    win->start_time  = glfwGetTime();
    win->last_time   = win->start_time;
    win->fps_cached  = 60.0f;
    g_win = win;
    return win;
#endif
}

void* aiml_engine_create_window_fullscreen(const char* title) {
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    return aiml_engine_create_window(0, 0, title);
#else
    GLFWmonitor* mon  = glfwGetPrimaryMonitor();
    const GLFWvidmode* vm = glfwGetVideoMode(mon);
    if (!glfwInit()) return nullptr;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* glfw = glfwCreateWindow(vm->width, vm->height, title ? title : "", mon, nullptr);
    if (!glfw) return nullptr;
    glfwMakeContextCurrent(glfw);
    glewExperimental = GL_TRUE;
    glewInit();

    AimlWindow* win = new AimlWindow();
    memset(win, 0, sizeof(*win));
    win->glfw       = glfw;
    win->width      = vm->width;
    win->height     = vm->height;
    win->start_time = glfwGetTime();
    win->last_time  = win->start_time;
    win->fps_cached = 60.0f;
    g_win = win;
    return win;
#endif
}

void aiml_engine_destroy_window(void* win_ptr) {
    if (!win_ptr) return;
    AimlWindow* win = (AimlWindow*)win_ptr;
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    if (win->glfw) {
        glfwDestroyWindow(win->glfw);
        glfwTerminate();
    }
#endif
    if (g_win == win) g_win = nullptr;
    delete win;
}

void aiml_engine_set_title(void* win_ptr, const char* title) {
    if (!win_ptr || !title) return;
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    glfwSetWindowTitle(((AimlWindow*)win_ptr)->glfw, title);
#endif
}

/* Maximize the window (system-defined "maximized" state). Spec/44_1 §window-ops. */
void aiml_engine_maximize_window(void* win_ptr) {
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    if (!win_ptr) return;
    AimlWindow* win = (AimlWindow*)win_ptr;
    if (win->glfw) glfwMaximizeWindow(win->glfw);
#else
    (void)win_ptr;
#endif
}

int32_t aiml_engine_window_size_x(void* win_ptr) {
    if (!win_ptr) return 0;
    AimlWindow* win = (AimlWindow*)win_ptr;
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    int w, h; glfwGetFramebufferSize(win->glfw, &w, &h);
    win->width = w; win->height = h;
    return w;
#else
    return win->width;
#endif
}

int32_t aiml_engine_window_size_y(void* win_ptr) {
    if (!win_ptr) return 0;
    AimlWindow* win = (AimlWindow*)win_ptr;
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    int w, h; glfwGetFramebufferSize(win->glfw, &w, &h);
    win->width = w; win->height = h;
    return h;
#else
    return win->height;
#endif
}

/* ── GL context ──────────────────────────────────────────────────────────── */

void aiml_engine_make_current(void* win_ptr) {
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    if (win_ptr) glfwMakeContextCurrent(((AimlWindow*)win_ptr)->glfw);
#endif
}

void aiml_engine_set_vsync(int32_t on) {
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    glfwSwapInterval(on ? 1 : 0);
#endif
}

/* ── Game loop ───────────────────────────────────────────────────────────── */

static void engine_tick(AimlWindow* win, void (*update)(float), void (*render)(void)) {
    double now = now_sec();
    float  raw = (float)(now - win->last_time);
    win->dt    = (raw < 0.1f) ? raw : 0.1f;
    win->last_time = now;
    win->frame_count++;

    /* FPS rolling average */
    win->fps_samples[win->fps_idx % 60] = win->dt;
    win->fps_idx++;
    if (win->fps_idx >= 60) {
        float sum = 0;
        for (int i = 0; i < 60; i++) sum += win->fps_samples[i];
        win->fps_cached = (sum > 0) ? (60.0f / sum) : 0.0f;
    }

    /* user-update first; THEN input_update saves prev=curr so that the next
     * frame's user-update sees a clean rising/falling edge after new events
     * arrive between frames. Calling input_update before user_update collapses
     * prev=curr and makes mouse_pressed/released always return 0. */
    update(win->dt);
    render();
    if (g_input_update) g_input_update();
}

/* ── Render-loop watchdog heartbeat ──────────────────────────────────────────
 * aiml_rt_bump_frame() is called at the top of every web frame.  A JS-side
 * setInterval polls aiml_rt_frame_counter(); when it stops advancing the loop
 * has stalled.  aiml_rt_rpc_phase() (defined in mirror_runtime) then names the
 * blocking call the frame is stuck inside.  Defined outside the __EMSCRIPTEN__
 * guard so the symbols link on native too (harmless — never polled there). */
#include <cstdint>
#include <cstdio>
#include <exception>
extern "C" const char* aiml_rt_rpc_phase(void);   // breadcrumb (mirror_runtime)
#if defined(__EMSCRIPTEN__)
#  define AIML_RT_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#  define AIML_RT_EXPORT
#endif
namespace { volatile uint32_t g_rt_frame_counter = 0; }
extern "C" void     aiml_rt_bump_frame(void)    { ++g_rt_frame_counter; }
extern "C" AIML_RT_EXPORT uint32_t aiml_rt_frame_counter(void) { return g_rt_frame_counter; }

#if defined(__EMSCRIPTEN__)
/* Web frame-loop bridge — registered once via emscripten_set_main_loop_arg.
 * The arg pointer must remain valid for the lifetime of the loop, so we keep
 * it in a file-scope static (single window per process is the existing
 * assumption: g_win is also a singleton). */
struct WebLoopCtx {
    AimlWindow* win;
    void (*update)(float);
    void (*render)(void);
};
static WebLoopCtx g_web_loop;

static void web_loop_tick(void* user) {
    /* Watchdog heartbeat: a JS-side setInterval reads aiml_rt_frame_counter().
     * If it stops advancing the render loop has stalled (e.g. an Asyncify
     * unwind that never rewound inside a blocking send_and_await poll) and the
     * shell dumps aiml_rt_rpc_phase() to pinpoint the stuck call. */
    aiml_rt_bump_frame();
    auto* c = (WebLoopCtx*)user;
    if (!c || !c->win) return;
    /* Quit is handled by the manual while-loop in aiml_engine_run; just bail. */
    if (c->win->quit_requested) return;
    /* Sync the GL viewport to the JS-resized canvas backing store each frame so
     * the design-resolution UI (prefab scene_init) rescales to fill on window
     * resize / DevTools toggle.  prefab_draw_scene reads GL_VIEWPORT and derives
     * screen_w/screen_h + rsx=screen_w/design_w from it.  Without this the
     * viewport stays at the initial size and the UI renders into a corner. */
    {
        int cw = 0, ch = 0;
        /* Literal "#canvas" (create_window hardcodes it) — avoids a corrupt
         * canvas_id read and the resulting querySelector DOMException. */
        emscripten_get_canvas_element_size("#canvas", &cw, &ch);
        if (cw > 0 && ch > 0) glViewport(0, 0, cw, ch);
    }
    /* timing + input + update + render (same shape as engine_tick desktop) */
    double now = now_sec();
    float  raw = (float)(now - c->win->last_time);
    c->win->dt = (raw < 0.1f) ? raw : 0.1f;
    c->win->last_time = now;
    c->win->frame_count++;

    /* FPS rolling avg */
    c->win->fps_samples[c->win->fps_idx % 60] = c->win->dt;
    c->win->fps_idx++;
    if (c->win->fps_idx >= 60) {
        float sum = 0.0f;
        for (int i = 0; i < 60; i++) sum += c->win->fps_samples[i];
        c->win->fps_cached = (sum > 0) ? (60.0f / sum) : 0.0f;
    }

    /* See note in engine_tick: user_update must run BEFORE input_update so
     * that mouse_pressed sees the rising edge of events that fired between
     * frames. */
    /* Watchdog: a C++ exception escaping the frame callback silently stops the
     * emscripten main loop (rAF never re-armed) — the exact "loop died, no
     * breadcrumb, asyncify=0" signature.  Catch it, name the phase it was in,
     * and shout, instead of letting the render loop die silently. */
    try {
        c->update(c->win->dt);
        c->render();
        if (g_input_update) g_input_update();
    } catch (const std::exception& e) {
        fprintf(stderr, "\n=== AIML FRAME EXCEPTION (loop would die): phase='%s' what='%s' ===\n",
                aiml_rt_rpc_phase(), e.what());
        fflush(stderr);
    } catch (...) {
        fprintf(stderr, "\n=== AIML FRAME EXCEPTION (loop would die): phase='%s' (non-std) ===\n",
                aiml_rt_rpc_phase());
        fflush(stderr);
    }
    /* WebGL implicitly swaps at end of callback — no explicit swap. */
}
#endif

void aiml_engine_run(void* win_ptr, void* update_fn, void* render_fn) {
    if (!win_ptr || !update_fn || !render_fn) return;
    AimlWindow* win = (AimlWindow*)win_ptr;

    typedef void(*UpdateFn)(float);
    typedef void(*RenderFn)(void);
    UpdateFn update = (UpdateFn)update_fn;
    RenderFn render = (RenderFn)render_fn;

    g_win = win;

#if defined(__EMSCRIPTEN__)
    /* Manual Asyncify loop — NOT emscripten_set_main_loop.
     *
     * Root cause of the WASM lobby freeze: a WS send (send_rpc → aiml_ws_send)
     * issued inside a set_main_loop CALLBACK that then RETURNS NORMALLY collides
     * with the main-loop runner's next requestAnimationFrame schedule and
     * silently kills the loop (no exception, no abort, asyncify idle — the loop
     * simply stops being called).  Diagnosed by isolation: an empty frame runs
     * forever; a single fire-only send_rpc kills it even when the reply is never
     * received; the deck-editor survives only because its blocking
     * send_and_await pairs every send with an immediate emscripten_sleep.
     *
     * Here the emscripten_sleep lives in OUR loop, not in an emscripten
     * callback, so there is no "callback returns → runner reschedules rAF" step
     * for the pending WS send to race.  send + unwind/rewind is clean — the same
     * pattern the deck-editor already proved safe, now for every frame.
     * Requires -sASYNCIFY (already set); engine.quit() sets quit_requested. */
    g_web_loop.win    = win;
    g_web_loop.update = update;
    g_web_loop.render = render;
    win->last_time    = now_sec();
    while (!win->quit_requested) {
        web_loop_tick(&g_web_loop);
        emscripten_sleep(16);   /* ~60fps; yields to the browser event loop */
    }

#elif defined(__ANDROID__)
    /* TODO: Android NDK event loop */
    (void)update; (void)render;

#else
    while (!glfwWindowShouldClose(win->glfw) && !win->quit_requested) {
        engine_tick(win, update, render);
        glfwSwapBuffers(win->glfw);
        glfwPollEvents();

        /* Frame limiter */
        if (win->target_frame_ms > 0.0f) {
            double elapsed_ms = (now_sec() - win->last_time) * 1000.0;
            double sleep_ms   = (double)win->target_frame_ms - elapsed_ms;
            if (sleep_ms > 0.5) {
                struct timespec ts;
                ts.tv_sec  = 0;
                ts.tv_nsec = (long)(sleep_ms * 1e6);
                nanosleep(&ts, nullptr);
            }
        }
    }
#endif
}

int32_t aiml_engine_running(void* win_ptr) {
    if (!win_ptr) return 0;
    AimlWindow* win = (AimlWindow*)win_ptr;
#if defined(__EMSCRIPTEN__)
    return win->quit_requested ? 0 : 1;
#elif defined(__ANDROID__)
    return 1; /* stub */
#else
    return (!glfwWindowShouldClose(win->glfw) && !win->quit_requested) ? 1 : 0;
#endif
}

void aiml_engine_swap(void* win_ptr) {
    if (!win_ptr) return;
    AimlWindow* win = (AimlWindow*)win_ptr;

    /* Update timing for manual-loop style */
    double now = now_sec();
    float  raw = (float)(now - win->last_time);
    win->dt    = (raw < 0.1f) ? raw : 0.1f;
    win->last_time = now;
    win->frame_count++;

    if (g_input_update) g_input_update();

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    glfwSwapBuffers(win->glfw);
    glfwPollEvents();

    /* Frame limiter (same as engine.run) */
    if (win->target_frame_ms > 0.0f) {
        double elapsed_ms = (now_sec() - win->last_time) * 1000.0;
        double sleep_ms   = (double)win->target_frame_ms - elapsed_ms;
        if (sleep_ms > 0.5) {
            struct timespec ts;
            ts.tv_sec  = 0;
            ts.tv_nsec = (long)(sleep_ms * 1e6);
            nanosleep(&ts, nullptr);
        }
    }
#endif
}

void aiml_engine_quit(void* win_ptr) {
    if (!win_ptr) return;
    AimlWindow* win = (AimlWindow*)win_ptr;
    win->quit_requested = 1;
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    glfwSetWindowShouldClose(win->glfw, GLFW_TRUE);
#endif
}

int32_t aiml_engine_is_iconified(void* win_ptr) {
    if (!win_ptr) return 0;
    AimlWindow* win = (AimlWindow*)win_ptr;
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    (void)win; return 0;
#else
    return glfwGetWindowAttrib(win->glfw, GLFW_ICONIFIED) ? 1 : 0;
#endif
}

int32_t aiml_engine_is_focused(void* win_ptr) {
    if (!win_ptr) return 0;
    AimlWindow* win = (AimlWindow*)win_ptr;
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    (void)win; return 1;
#else
    return glfwGetWindowAttrib(win->glfw, GLFW_FOCUSED) ? 1 : 0;
#endif
}

int32_t aiml_engine_is_visible(void* win_ptr) {
    if (!win_ptr) return 0;
    AimlWindow* win = (AimlWindow*)win_ptr;
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
    (void)win; return 1;
#else
    return glfwGetWindowAttrib(win->glfw, GLFW_VISIBLE) ? 1 : 0;
#endif
}

/* ── Frame timing ─────────────────────────────────────────────────────────── */

float   aiml_engine_dt   (void) { return g_win ? g_win->dt         : 0.016f; }
double  aiml_engine_time (void) { return g_win ? (now_sec() - g_win->start_time) : 0.0; }
float   aiml_engine_fps  (void) { return g_win ? g_win->fps_cached  : 0.0f;  }
int64_t aiml_engine_frame(void) { return g_win ? g_win->frame_count : 0;     }

void aiml_engine_target_fps(int32_t fps) {
    if (!g_win) return;
    g_win->target_frame_ms = (fps > 0) ? (1000.0f / (float)fps) : 0.0f;
}

/* ── Platform information ────────────────────────────────────────────────── */

const char* aiml_engine_platform(void) {
#if defined(__EMSCRIPTEN__)
    return "web";
#elif defined(__ANDROID__)
    return "android";
#else
    return "desktop";
#endif
}

int32_t aiml_engine_screen_size_x(void) {
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    GLFWmonitor* m = glfwGetPrimaryMonitor();
    if (m) { const GLFWvidmode* v = glfwGetVideoMode(m); if (v) return v->width; }
#endif
    return g_win ? g_win->width : 0;
}

int32_t aiml_engine_screen_size_y(void) {
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    GLFWmonitor* m = glfwGetPrimaryMonitor();
    if (m) { const GLFWvidmode* v = glfwGetVideoMode(m); if (v) return v->height; }
#endif
    return g_win ? g_win->height : 0;
}

float aiml_engine_dpi(void) {
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    GLFWmonitor* m = glfwGetPrimaryMonitor();
    if (m) {
        float xs, ys;
        glfwGetMonitorContentScale(m, &xs, &ys);
        return xs;
    }
#endif
    return 1.0f;
}

int32_t aiml_engine_is_mobile(void) {
#if defined(__ANDROID__)
    return 1;
#else
    return 0;
#endif
}

int32_t aiml_engine_is_web(void) {
#if defined(__EMSCRIPTEN__)
    return 1;
#else
    return 0;
#endif
}

/* ── Input integration ───────────────────────────────────────────────────── */

void aiml_engine_register_input_update(void (*fn)(void)) {
    g_input_update = fn;
}

void* aiml_engine_glfw_window(void* win_ptr) {
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    if (!win_ptr) return nullptr;
    return (void*)((AimlWindow*)win_ptr)->glfw;
#else
    return nullptr;
#endif
}
