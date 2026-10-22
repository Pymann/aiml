/* OS-Ebenen-Eingabe (spec/44_0 §8.5) — Mausbewegungen, Klicks und Tasten in den
 * X-Server einspeisen, nicht in den eigenen Prozess.
 *
 * Warum neben input.post_*: post_* schreibt den eigenen Frame-Zustand und prüft
 * damit alles AB der Anwendungsgrenze.  Was darunter liegt — Fensterfokus,
 * Fensterposition, ein fremdes Fenster darüber — bleibt dabei unberührt und
 * ungeprüft.  XTEST setzt eine Ebene tiefer an: die Anwendung kann ein so
 * erzeugtes Ereignis nicht von einem echten unterscheiden.
 *
 * Preis dieser Treue: der Cursor ist eine GLOBALE Ressource.  Ein Lauf bewegt
 * den echten Zeiger des Sitzungsinhabers.  Darum gehören solche Tests auf einen
 * eigenen Display (Xvfb), nicht auf die Arbeitssitzung — siehe spec/44_0 §8.5.
 *
 * Koordinaten sind ROOT-Koordinaten (ganzer Bildschirm).  Widget-Flächen aus
 * .child_rect_* sind FENSTER-Pixel, also muss der Aufrufer die Fensterposition
 * addieren (engine.window_pos_x/y).  Diese Umrechnung bleibt bewusst beim
 * Aufrufer: nur er weiß, auf welches Fenster sich eine Fläche bezieht. */

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

/* Verbindung + XTEST-Prüfung einmal, Ergebnis gemerkt.  Fehlt die Erweiterung,
 * bleibt alles ein No-op statt zu stürzen: ein Szenario soll "nicht verfügbar"
 * melden können, nicht abbrechen. */
bool ensure_display() {
    if (g_probed) return g_have_xtest;
    g_probed = true;
    g_dpy = XOpenDisplay(nullptr);
    if (!g_dpy) {
        fprintf(stderr, "[os_input] XOpenDisplay fehlgeschlagen — kein DISPLAY?\n");
        return false;
    }
    int ev = 0, err = 0, major = 0, minor = 0;
    if (!XTestQueryExtension(g_dpy, &ev, &err, &major, &minor)) {
        fprintf(stderr, "[os_input] XTEST-Erweiterung nicht vorhanden\n");
        XCloseDisplay(g_dpy);
        g_dpy = nullptr;
        return false;
    }
    g_have_xtest = true;
    return true;
}

/* aiml zählt 0=links, 1=mitte, 2=rechts (spec/44_0 §2.2); X11 zählt
 * Button1=links, Button2=mitte, Button3=rechts. */
/* spec/44_0 §2: die kanonische Reihenfolge ist 0=links, 1=RECHTS, 2=Mitte
 * (die von GLFW).  X11 nummeriert anders (Button2=Mitte, Button3=rechts), also
 * muss dieser Adapter umsetzen — vorher reichte er 1 als Button2 durch und ein
 * `os_mouse_down(1)` erzeugte einen MITTELklick, wo die Spec rechts sagt. */
unsigned int x_button(int32_t btn) {
    switch (btn) {
        case 0:  return Button1;   /* links  */
        case 1:  return Button3;   /* rechts */
        case 2:  return Button2;   /* Mitte  */
        default: return 0;
    }
}

/* Tastenname (spec/44_0 §5) → KeySym.  Nur was ein Szenario braucht; alles
 * andere wird über den Namen als Einzelzeichen versucht, damit "a".."z"/"0".."9"
 * ohne Tabelle funktionieren. */
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
    if (ks == NoSymbol) { fprintf(stderr, "[os_input] unbekannte Taste '%s'\n", name ? name : "(null)"); return; }
    KeyCode kc = XKeysymToKeycode(g_dpy, ks);
    if (!kc) { fprintf(stderr, "[os_input] keine Keycode-Belegung fuer '%s'\n", name); return; }
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

/* Wo der Zeiger tatsaechlich steht — die Gegenprobe, dass eine Bewegung
 * angekommen ist, bevor geklickt wird. */
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

#else /* kein X11 (Browser, Android, andere Plattformen) */

extern "C" int32_t aiml_os_input_available(void) { return 0; }
extern "C" void    aiml_os_mouse_move(int32_t, int32_t) {}
extern "C" void    aiml_os_mouse_down(int32_t) {}
extern "C" void    aiml_os_mouse_up(int32_t) {}
extern "C" void    aiml_os_key_down(const char*) {}
extern "C" void    aiml_os_key_up(const char*) {}
extern "C" int32_t aiml_os_pointer_x(void) { return -1; }
extern "C" int32_t aiml_os_pointer_y(void) { return -1; }

#endif
