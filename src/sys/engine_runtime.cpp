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

#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>

/* ── Platform includes ───────────────────────────────────────────────────── */

#if defined(__EMSCRIPTEN__)
    #include <emscripten.h>
    #include <emscripten/html5.h>
    /* GL via Emscripten WebGL2 — no GLEW needed */
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
    attrs.antialias        = EM_TRUE;
    attrs.majorVersion     = 2;  /* WebGL2 */
    attrs.minorVersion     = 0;
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

    /* Keep viewport in sync when the user resizes the window */
    glfwSetFramebufferSizeCallback(glfw, [](GLFWwindow*, int fbw, int fbh) {
        glViewport(0, 0, fbw, fbh);
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
    auto* c = (WebLoopCtx*)user;
    if (!c || !c->win) return;
    if (c->win->quit_requested) {
        emscripten_cancel_main_loop();
        return;
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
    c->update(c->win->dt);
    c->render();
    if (g_input_update) g_input_update();
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
    /* Hand the loop to the browser. fps=0 → use requestAnimationFrame (vsync).
     * simulate_infinite_loop=0: main returns normally; emcc keeps the wasm
     * runtime alive while the registered main loop is active. This plays well
     * with Asyncify (the alternative throws an unwind exception that conflicts
     * with Asyncify's own stack switching). */
    g_web_loop.win    = win;
    g_web_loop.update = update;
    g_web_loop.render = render;
    win->last_time    = now_sec();
    emscripten_set_main_loop_arg(web_loop_tick, &g_web_loop, /*fps=*/0,
                                  /*simulate_infinite_loop=*/0);

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
