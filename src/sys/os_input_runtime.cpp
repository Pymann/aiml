/* OS-level input (spec/44_0 §8.5) — feed mouse movements, clicks and keys into the
 * X server, not into our own process.
 *
 * Why next to input.post_*: post_* writes our own frame state and thereby tests
 * everything FROM the application boundary up.  What lies below — window focus,
 * window position, a foreign window on top — stays untouched and
 * untested.  XTEST starts one level deeper: the application cannot tell an event
 * produced this way from a real one.
 *
 * The price of this fidelity: the cursor is a GLOBAL resource.  A run moves
 * the real pointer of the session owner.  That is why such tests belong on a
 * display of their own (Xvfb), not on the working session — see spec/44_0 §8.5.
 *
 * Coordinates are ROOT coordinates (whole screen).  Widget areas from
 * .child_rect_* are WINDOW pixels, so the caller must add the window position
 * (engine.window_pos_x/y).  This conversion deliberately stays with the
 * caller: only the caller knows which window an area refers to. */

#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(__linux__) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

namespace {

Display* g_dpy      = nullptr;
bool     g_probed   = false;
bool     g_have_xtest = false;

/* Connection + XTEST check once, result remembered.  If the extension is missing,
 * everything stays a no-op instead of crashing: a scenario should be able to report
 * "not available", not abort. */
bool ensure_display() {
    if (g_probed) return g_have_xtest;
    g_probed = true;
    g_dpy = XOpenDisplay(nullptr);
    if (!g_dpy) {
        fprintf(stderr, "[os_input] XOpenDisplay failed — no DISPLAY?\n");
        return false;
    }
    int ev = 0, err = 0, major = 0, minor = 0;
    if (!XTestQueryExtension(g_dpy, &ev, &err, &major, &minor)) {
        fprintf(stderr, "[os_input] XTEST extension not present\n");
        XCloseDisplay(g_dpy);
        g_dpy = nullptr;
        return false;
    }
    g_have_xtest = true;
    return true;
}

/* aiml counts 0=left, 1=middle, 2=right (spec/44_0 §2.2); X11 counts
 * Button1=left, Button2=middle, Button3=right. */
/* spec/44_0 §2: the canonical order is 0=left, 1=RIGHT, 2=middle
 * (that of GLFW).  X11 numbers differently (Button2=middle, Button3=right), so
 * this adapter must convert — before, it passed 1 through as Button2 and an
 * `os_mouse_down(1)` produced a MIDDLE click where the spec says right. */
unsigned int x_button(int32_t btn) {
    switch (btn) {
        case 0:  return Button1;   /* links  */
        case 1:  return Button3;   /* rechts */
        case 2:  return Button2;   /* Mitte  */
        default: return 0;
    }
}

/* Key name (spec/44_0 §5) → KeySym.  Only what a scenario needs; everything
 * else is attempted via the name as a single character, so that "a".."z"/"0".."9"
 * work without a table. */
KeySym keysym_for(const char* name) {
    if (!name || !*name) return NoSymbol;
    struct { const char* n; KeySym k; } map[] = {
        {"Return", XK_Return}, {"Enter", XK_Return}, {"Escape", XK_Escape},
        {"Tab", XK_Tab}, {"Backspace", XK_BackSpace}, {"Delete", XK_Delete},
        {"Space", XK_space}, {"Left", XK_Left}, {"Right", XK_Right},
        {"Up", XK_Up}, {"Down", XK_Down}, {"Home", XK_Home}, {"End", XK_End},
        {"lctrl", XK_Control_L}, {"rctrl", XK_Control_R},
        {"lshift", XK_Shift_L},  {"rshift", XK_Shift_R},
        {"lalt", XK_Alt_L},      {"ralt", XK_Alt_R},
    };
    for (auto& m : map) if (strcmp(name, m.n) == 0) return m.k;
    /* Einzelzeichen: XStringToKeysym kennt "a", "0", "comma", … */
    return XStringToKeysym(name);
}

} // namespace

extern "C" int32_t aiml_os_input_available(void) { return ensure_display() ? 1 : 0; }

extern "C" void aiml_os_mouse_move(int32_t root_x, int32_t root_y) {
    if (!ensure_display()) return;
    XTestFakeMotionEvent(g_dpy, -1 /* aktueller Screen */, (int)root_x, (int)root_y, 0);
    XFlush(g_dpy);
}

extern "C" void aiml_os_mouse_down(int32_t btn) {
    if (!ensure_display()) return;
    unsigned int b = x_button(btn);
    if (!b) return;
    XTestFakeButtonEvent(g_dpy, b, True, 0);
    XFlush(g_dpy);
}

extern "C" void aiml_os_mouse_up(int32_t btn) {
    if (!ensure_display()) return;
    unsigned int b = x_button(btn);
    if (!b) return;
    XTestFakeButtonEvent(g_dpy, b, False, 0);
    XFlush(g_dpy);
}

extern "C" void aiml_os_key_down(const char* name) {
    if (!ensure_display()) return;
    KeySym ks = keysym_for(name);
    if (ks == NoSymbol) { fprintf(stderr, "[os_input] unknown key '%s'\n", name ? name : "(null)"); return; }
    KeyCode kc = XKeysymToKeycode(g_dpy, ks);
    if (!kc) { fprintf(stderr, "[os_input] no keycode mapping for '%s'\n", name); return; }
    XTestFakeKeyEvent(g_dpy, kc, True, 0);
    XFlush(g_dpy);
}

extern "C" void aiml_os_key_up(const char* name) {
    if (!ensure_display()) return;
    KeySym ks = keysym_for(name);
    if (ks == NoSymbol) return;
    KeyCode kc = XKeysymToKeycode(g_dpy, ks);
    if (!kc) return;
    XTestFakeKeyEvent(g_dpy, kc, False, 0);
    XFlush(g_dpy);
}

/* Where the pointer actually is — the cross-check that a movement
 * has arrived before clicking. */
extern "C" int32_t aiml_os_pointer_x(void) {
    if (!ensure_display()) return -1;
    Window root_ret, child_ret;
    int rx = -1, ry = -1, wx = 0, wy = 0;
    unsigned int mask = 0;
    XQueryPointer(g_dpy, DefaultRootWindow(g_dpy), &root_ret, &child_ret,
                  &rx, &ry, &wx, &wy, &mask);
    return (int32_t)rx;
}

extern "C" int32_t aiml_os_pointer_y(void) {
    if (!ensure_display()) return -1;
    Window root_ret, child_ret;
    int rx = 0, ry = -1, wx = 0, wy = 0;
    unsigned int mask = 0;
    XQueryPointer(g_dpy, DefaultRootWindow(g_dpy), &root_ret, &child_ret,
                  &rx, &ry, &wx, &wy, &mask);
    return (int32_t)ry;
}

#else /* no X11 (browser, Android, other platforms) */

extern "C" int32_t aiml_os_input_available(void) { return 0; }
extern "C" void    aiml_os_mouse_move(int32_t, int32_t) {}
extern "C" void    aiml_os_mouse_down(int32_t) {}
extern "C" void    aiml_os_mouse_up(int32_t) {}
extern "C" void    aiml_os_key_down(const char*) {}
extern "C" void    aiml_os_key_up(const char*) {}
extern "C" int32_t aiml_os_pointer_x(void) { return -1; }
extern "C" int32_t aiml_os_pointer_y(void) { return -1; }

#endif
