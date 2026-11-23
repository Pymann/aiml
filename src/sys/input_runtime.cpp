/*
 * input_runtime.cpp — AIML input namespace (44.0)
 *
 * Platform selection:
 *   __EMSCRIPTEN__ → Browser stubs
 *   __ANDROID__    → Android stubs
 *   default        → Desktop (GLFW 3.x)
 */

#include "input_runtime.h"
#include "engine_runtime.h"     /* aiml_engine_glfw_window, aiml_engine_register_input_update */

#include <cstring>
#include <cmath>
#include <cstdio>

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
    #include <GLFW/glfw3.h>
    #include <string>
    #include <vector>
    #include <deque>
    #include <algorithm>
#endif

/* ── AimlInputEvent (shared across platforms for API type) ───────────────── */

struct AimlInputEvent {
    const char* type;       /* static string literal */
    const char* key;        /* static string literal (AIML key name) */
    int32_t     btn;        /* mouse button index */
    int32_t     _pad;
    float       pos_x, pos_y;
    float       scroll_x, scroll_y;
    char        char_buf[8];    /* UTF-8 char for "text" events (NUL-terminated) */
};

/* Static "current event" buffer — returned by aiml_input_next_event() */
static AimlInputEvent g_current_event;

/* ── Field accessors ─────────────────────────────────────────────────────── */

const char* aiml_input_event_type    (void* ev) { return ev ? ((AimlInputEvent*)ev)->type     : ""; }
const char* aiml_input_event_key     (void* ev) { return ev ? ((AimlInputEvent*)ev)->key      : ""; }
int32_t     aiml_input_event_btn     (void* ev) { return ev ? ((AimlInputEvent*)ev)->btn      : 0;  }
float       aiml_input_event_pos_x   (void* ev) { return ev ? ((AimlInputEvent*)ev)->pos_x    : 0.f;}
float       aiml_input_event_pos_y   (void* ev) { return ev ? ((AimlInputEvent*)ev)->pos_y    : 0.f;}
float       aiml_input_event_scroll_x(void* ev) { return ev ? ((AimlInputEvent*)ev)->scroll_x : 0.f;}
float       aiml_input_event_scroll_y(void* ev) { return ev ? ((AimlInputEvent*)ev)->scroll_y : 0.f;}
const char* aiml_input_event_char_   (void* ev) { return ev ? ((AimlInputEvent*)ev)->char_buf : ""; }
float aiml_input_event_delta_x(void* ev) { return ev ? ((AimlInputEvent*)ev)->scroll_x : 0.f; }
float aiml_input_event_delta_y(void* ev) { return ev ? ((AimlInputEvent*)ev)->scroll_y : 0.f; }

/* ══════════════════════════════════════════════════════════════════════════ */
/* DESKTOP IMPLEMENTATION                                                     */
/* ══════════════════════════════════════════════════════════════════════════ */

/* Shared limits — referenced by both desktop and emscripten branches. */
static constexpr int AIML_MAX_KEYS     = 400;    /* GLFW_KEY_LAST + ~50 spare */
static constexpr int AIML_MAX_MOUSE    = 8;
static constexpr int AIML_MAX_GAMEPADS = 4;

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)

/* ── Global input state ─────────────────────────────────────────────────── */

struct AimlInputState {
    GLFWwindow* window;

    /* Keyboard (updated by GLFW callbacks + glfwGetKey in update) */
    uint8_t curr_keys[AIML_MAX_KEYS];
    uint8_t prev_keys[AIML_MAX_KEYS];
    /* Per-frame "rising edge already consumed" flags for key_pressed_once.
     * Cleared each frame in aiml_input_update; set on first _once read. */
    uint8_t consumed_press[AIML_MAX_KEYS];
    /* spec/44: latched key edges since the last frame (press+release inside one poll is not lost) */
    uint8_t key_press_latch[AIML_MAX_KEYS];
    uint8_t key_release_latch[AIML_MAX_KEYS];

    /* Mouse buttons */
    uint8_t curr_mouse[AIML_MAX_MOUSE];
    uint8_t prev_mouse[AIML_MAX_MOUSE];
    /* spec/44: latched edges since the last frame (a press+release inside one poll is not lost) */
    uint8_t press_latch[AIML_MAX_MOUSE];
    uint8_t release_latch[AIML_MAX_MOUSE];
    uint8_t cancel_latch[AIML_MAX_MOUSE];   /* spec/44_0 §2.2: aborted press, NOT a release */

    /* Mouse position and delta */
    double curr_pos_x, curr_pos_y;
    double prev_pos_x, prev_pos_y;
    float  delta_x, delta_y;

    /* Scroll accumulator (filled by callback; drained per frame) */
    float  scroll_accum_x, scroll_accum_y;
    float  scroll_this_x,  scroll_this_y;

    /* Gamepad (polled each frame) */
    GLFWgamepadstate curr_gamepad[AIML_MAX_GAMEPADS];
    GLFWgamepadstate prev_gamepad[AIML_MAX_GAMEPADS];

    /* Cursor capture (FPS-look mode) */
    bool captured;

    /* Event queue */
    std::deque<AimlInputEvent> events;

    /* Mapping tables */
    enum SrcType { SRC_KEY, SRC_MOUSE, SRC_GAMEPAD };

    struct AxisEntry {
        std::string name;
        SrcType     src;
        int         glfw_key;
        int         component;     /* mouse: 0=x, 1=y */
        int         pad, axis_idx;
        float       scale;
    };
    std::vector<AxisEntry> axes;

    struct ActionEntry {
        std::string name;
        SrcType     src;
        int         glfw_key;
        int         mouse_btn;
        int         pad, btn_idx;
    };
    std::vector<ActionEntry> actions;

    /* ── Drag-and-drop state ──────────────────────────────────────────────── */
    struct ButtonDragState {
        bool  held;               /* button currently down */
        bool  active;             /* drag threshold exceeded */
        bool  started_this_frame; /* active became true this polling cycle */
        bool  ended_this_frame;   /* drag ended this polling cycle (release) */
        bool  cancelled_this_frame; /* drag aborted this polling cycle (no drop) */
        float origin_x, origin_y; /* cursor pos when button pressed */
        float start_x, start_y;   /* press point of the drag that STARTED this frame */
        float end_x, end_y;       /* cursor pos at the release that ended the drag */
        float acc_x, acc_y;       /* total delta from origin */
    };
    ButtonDragState drag[AIML_MAX_MOUSE] = {};
    void*  drag_payload   = nullptr;
    float  drag_threshold = 5.0f;
};

static AimlInputState* g_input = nullptr;

/* ── Key name → GLFW code ───────────────────────────────────────────────── */

static int key_name_to_glfw(const char* name) {
    if (!name || !*name) return -1;
    /* Single letters a-z / A-Z */
    if (name[1] == '\0') {
        char c = name[0];
        if (c >= 'A' && c <= 'Z') return GLFW_KEY_A + (c - 'A');
        if (c >= 'a' && c <= 'z') return GLFW_KEY_A + (c - 'a');
        if (c >= '0' && c <= '9') return GLFW_KEY_0 + (c - '0');
    }

    /* Named keys (case-insensitive lookup) */
    struct KV { const char* n; int k; };
    static const KV table[] = {
        {"space",       GLFW_KEY_SPACE},
        {"escape",      GLFW_KEY_ESCAPE},
        {"enter",       GLFW_KEY_ENTER},
        {"return",      GLFW_KEY_ENTER},        /* alias */
        {"tab",         GLFW_KEY_TAB},
        {"backspace",   GLFW_KEY_BACKSPACE},
        {"delete",      GLFW_KEY_DELETE},
        {"insert",      GLFW_KEY_INSERT},
        {"lshift",      GLFW_KEY_LEFT_SHIFT},
        {"rshift",      GLFW_KEY_RIGHT_SHIFT},
        {"shift",       GLFW_KEY_LEFT_SHIFT},
        {"lctrl",       GLFW_KEY_LEFT_CONTROL},
        {"rctrl",       GLFW_KEY_RIGHT_CONTROL},
        {"ctrl",        GLFW_KEY_LEFT_CONTROL},
        {"lalt",        GLFW_KEY_LEFT_ALT},
        {"ralt",        GLFW_KEY_RIGHT_ALT},
        {"alt",         GLFW_KEY_LEFT_ALT},
        {"up",          GLFW_KEY_UP},
        {"down",        GLFW_KEY_DOWN},
        {"left",        GLFW_KEY_LEFT},
        {"right",       GLFW_KEY_RIGHT},
        {"home",        GLFW_KEY_HOME},
        {"end",         GLFW_KEY_END},
        {"pageup",      GLFW_KEY_PAGE_UP},
        {"pagedown",    GLFW_KEY_PAGE_DOWN},
        {"f1",          GLFW_KEY_F1},
        {"f2",          GLFW_KEY_F2},
        {"f3",          GLFW_KEY_F3},
        {"f4",          GLFW_KEY_F4},
        {"f5",          GLFW_KEY_F5},
        {"f6",          GLFW_KEY_F6},
        {"f7",          GLFW_KEY_F7},
        {"f8",          GLFW_KEY_F8},
        {"f9",          GLFW_KEY_F9},
        {"f10",         GLFW_KEY_F10},
        {"f11",         GLFW_KEY_F11},
        {"f12",         GLFW_KEY_F12},
        {"num0",        GLFW_KEY_KP_0},
        {"num1",        GLFW_KEY_KP_1},
        {"num2",        GLFW_KEY_KP_2},
        {"num3",        GLFW_KEY_KP_3},
        {"num4",        GLFW_KEY_KP_4},
        {"num5",        GLFW_KEY_KP_5},
        {"num6",        GLFW_KEY_KP_6},
        {"num7",        GLFW_KEY_KP_7},
        {"num8",        GLFW_KEY_KP_8},
        {"num9",        GLFW_KEY_KP_9},
        {"numadd",      GLFW_KEY_KP_ADD},
        {"numsub",      GLFW_KEY_KP_SUBTRACT},
        {"nummul",      GLFW_KEY_KP_MULTIPLY},
        {"numdiv",      GLFW_KEY_KP_DIVIDE},
        {"numenter",    GLFW_KEY_KP_ENTER},
        {"numdecimal",  GLFW_KEY_KP_DECIMAL},
        {"capslock",    GLFW_KEY_CAPS_LOCK},
        {"printscreen", GLFW_KEY_PRINT_SCREEN},
        {"pause",       GLFW_KEY_PAUSE},
        {"menu",        GLFW_KEY_MENU},
        {nullptr, -1}
    };
    /* lowercase the input for comparison */
    char lower[32];
    int len = 0;
    for (; name[len] && len < 31; len++)
        lower[len] = (name[len] >= 'A' && name[len] <= 'Z')
                   ? (char)(name[len] + 32) : name[len];
    lower[len] = '\0';
    for (int i = 0; table[i].n; i++)
        if (strcmp(lower, table[i].n) == 0) return table[i].k;
    return -1;
}

/* Reverse: GLFW code → canonical AIML key name */
static const char* glfw_key_to_name(int key) {
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        static char single[2] = {};
        single[0] = 'A' + (key - GLFW_KEY_A);
        return single;   /* NOTE: not thread-safe; fine for sequential input */
    }
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
        static char digit[2] = {};
        digit[0] = '0' + (key - GLFW_KEY_0);
        return digit;
    }
    switch (key) {
    case GLFW_KEY_SPACE:         return "Space";
    case GLFW_KEY_ESCAPE:        return "Escape";
    case GLFW_KEY_ENTER:         return "Enter";
    case GLFW_KEY_TAB:           return "Tab";
    case GLFW_KEY_BACKSPACE:     return "Backspace";
    case GLFW_KEY_DELETE:        return "Delete";
    case GLFW_KEY_INSERT:        return "Insert";
    case GLFW_KEY_LEFT_SHIFT:    return "LShift";
    case GLFW_KEY_RIGHT_SHIFT:   return "RShift";
    case GLFW_KEY_LEFT_CONTROL:  return "LCtrl";
    case GLFW_KEY_RIGHT_CONTROL: return "RCtrl";
    case GLFW_KEY_LEFT_ALT:      return "LAlt";
    case GLFW_KEY_RIGHT_ALT:     return "RAlt";
    case GLFW_KEY_UP:            return "Up";
    case GLFW_KEY_DOWN:          return "Down";
    case GLFW_KEY_LEFT:          return "Left";
    case GLFW_KEY_RIGHT:         return "Right";
    case GLFW_KEY_HOME:          return "Home";
    case GLFW_KEY_END:           return "End";
    case GLFW_KEY_PAGE_UP:       return "PageUp";
    case GLFW_KEY_PAGE_DOWN:     return "PageDown";
    case GLFW_KEY_F1:            return "F1";
    case GLFW_KEY_F2:            return "F2";
    case GLFW_KEY_F3:            return "F3";
    case GLFW_KEY_F4:            return "F4";
    case GLFW_KEY_F5:            return "F5";
    case GLFW_KEY_F6:            return "F6";
    case GLFW_KEY_F7:            return "F7";
    case GLFW_KEY_F8:            return "F8";
    case GLFW_KEY_F9:            return "F9";
    case GLFW_KEY_F10:           return "F10";
    case GLFW_KEY_F11:           return "F11";
    case GLFW_KEY_F12:           return "F12";
    case GLFW_KEY_KP_0:          return "Num0";
    case GLFW_KEY_KP_1:          return "Num1";
    case GLFW_KEY_KP_2:          return "Num2";
    case GLFW_KEY_KP_3:          return "Num3";
    case GLFW_KEY_KP_4:          return "Num4";
    case GLFW_KEY_KP_5:          return "Num5";
    case GLFW_KEY_KP_6:          return "Num6";
    case GLFW_KEY_KP_7:          return "Num7";
    case GLFW_KEY_KP_8:          return "Num8";
    case GLFW_KEY_KP_9:          return "Num9";
    case GLFW_KEY_KP_ADD:        return "NumAdd";
    case GLFW_KEY_KP_SUBTRACT:   return "NumSub";
    case GLFW_KEY_KP_MULTIPLY:   return "NumMul";
    case GLFW_KEY_KP_DIVIDE:     return "NumDiv";
    case GLFW_KEY_KP_ENTER:      return "NumEnter";
    case GLFW_KEY_KP_DECIMAL:    return "NumDecimal";
    case GLFW_KEY_CAPS_LOCK:     return "CapsLock";
    case GLFW_KEY_PRINT_SCREEN:  return "PrintScreen";
    case GLFW_KEY_PAUSE:         return "Pause";
    case GLFW_KEY_MENU:          return "Menu";
    default:                     return "";
    }
}

/* Check an alias key name that may cover two physical keys */
static bool is_alias_key(const char* name) {
    return strcmp(name,"Shift")==0 || strcmp(name,"Ctrl")==0 || strcmp(name,"Alt")==0;
}

static bool curr_key_active(const char* name) {
    if (!g_input) return false;
    if (strcmp(name,"Shift")==0)
        return g_input->curr_keys[GLFW_KEY_LEFT_SHIFT] || g_input->curr_keys[GLFW_KEY_RIGHT_SHIFT];
    if (strcmp(name,"Ctrl")==0)
        return g_input->curr_keys[GLFW_KEY_LEFT_CONTROL] || g_input->curr_keys[GLFW_KEY_RIGHT_CONTROL];
    if (strcmp(name,"Alt")==0)
        return g_input->curr_keys[GLFW_KEY_LEFT_ALT] || g_input->curr_keys[GLFW_KEY_RIGHT_ALT];
    int k = key_name_to_glfw(name);
    if (k < 0 || k >= AIML_MAX_KEYS) return false;
    return g_input->curr_keys[k] != 0;
}

/* Latched edge (press_latch / release_latch) for a key name, aliases cover both physical keys. */
static bool key_latched(const char* name, const uint8_t* latch) {
    if (!g_input) return false;
    if (strcmp(name,"Shift")==0) return latch[GLFW_KEY_LEFT_SHIFT]   || latch[GLFW_KEY_RIGHT_SHIFT];
    if (strcmp(name,"Ctrl")==0)  return latch[GLFW_KEY_LEFT_CONTROL] || latch[GLFW_KEY_RIGHT_CONTROL];
    if (strcmp(name,"Alt")==0)   return latch[GLFW_KEY_LEFT_ALT]     || latch[GLFW_KEY_RIGHT_ALT];
    int k = key_name_to_glfw(name);
    if (k < 0 || k >= AIML_MAX_KEYS) return false;
    return latch[k] != 0;
}

/* ── GLFW callbacks ───────────────────────────────────────────────────────── */

static void glfw_scroll_cb(GLFWwindow*, double x, double y) {
    aiml_engine_note_event(1);
    if (!g_input) return;
    g_input->scroll_accum_x += (float)x;
    g_input->scroll_accum_y += (float)y;
}

static void glfw_key_cb(GLFWwindow*, int key, int /*scan*/, int action, int /*mods*/) {
    aiml_engine_note_event(1);
    if (!g_input || key < 0 || key >= AIML_MAX_KEYS) return;
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (action == GLFW_PRESS) g_input->key_press_latch[key] = 1;
        g_input->curr_keys[key] = 1;
    } else if (action == GLFW_RELEASE) {
        g_input->curr_keys[key] = 0;
        g_input->key_release_latch[key] = 1;
    }
    if (action == GLFW_REPEAT) return;  /* only push events for press/release */

    AimlInputEvent ev = {};
    ev.type = (action == GLFW_RELEASE) ? "key_up" : "key_down";
    ev.key  = glfw_key_to_name(key);
    g_input->events.push_back(ev);
}

static void glfw_char_cb(GLFWwindow*, unsigned int cp) {
    aiml_engine_note_event(1);
    if (!g_input) return;
    AimlInputEvent ev = {};
    ev.type = "text";
    /* Encode codepoint as UTF-8 in char_buf */
    char* b = ev.char_buf;
    if      (cp < 0x80)     { b[0]=(char)cp; }
    else if (cp < 0x800)    { b[0]=(char)(0xC0|(cp>>6)); b[1]=(char)(0x80|(cp&0x3F)); }
    else if (cp < 0x10000)  { b[0]=(char)(0xE0|(cp>>12)); b[1]=(char)(0x80|((cp>>6)&0x3F)); b[2]=(char)(0x80|(cp&0x3F)); }
    else                    { b[0]=(char)(0xF0|(cp>>18)); b[1]=(char)(0x80|((cp>>12)&0x3F)); b[2]=(char)(0x80|((cp>>6)&0x3F)); b[3]=(char)(0x80|(cp&0x3F)); }
    g_input->events.push_back(ev);
}

static void glfw_cursor_cb(GLFWwindow*, double x, double y) {
    aiml_engine_note_event(1);
    if (!g_input) return;
    g_input->curr_pos_x = x;
    g_input->curr_pos_y = y;
    AimlInputEvent ev = {};
    ev.type  = "mouse_move";
    ev.pos_x = (float)x;
    ev.pos_y = (float)y;
    g_input->events.push_back(ev);

    /* Update drag accumulators for all held buttons */
    for (int b = 0; b < AIML_MAX_MOUSE; b++) {
        auto& ds = g_input->drag[b];
        if (!ds.held) continue;
        ds.acc_x = (float)x - ds.origin_x;
        ds.acc_y = (float)y - ds.origin_y;
        float dist = sqrtf(ds.acc_x * ds.acc_x + ds.acc_y * ds.acc_y);
        if (!ds.active && dist >= g_input->drag_threshold) {
            ds.active             = true;
            ds.started_this_frame = true;
            ds.start_x = ds.origin_x;
            ds.start_y = ds.origin_y;
            AimlInputEvent dev = {};
            dev.type     = "drag_start";
            dev.btn      = b;
            dev.pos_x    = (float)x;
            dev.pos_y    = (float)y;
            dev.scroll_x = ds.acc_x;
            dev.scroll_y = ds.acc_y;
            g_input->events.push_back(dev);
        } else if (ds.active) {
            AimlInputEvent dev = {};
            dev.type     = "drag_move";
            dev.btn      = b;
            dev.pos_x    = (float)x;
            dev.pos_y    = (float)y;
            dev.scroll_x = ds.acc_x;
            dev.scroll_y = ds.acc_y;
            g_input->events.push_back(dev);
        }
    }
}

static void glfw_mbtn_cb(GLFWwindow*, int btn, int action, int /*mods*/) {
    aiml_engine_note_event(1);
    if (!g_input || btn < 0 || btn >= AIML_MAX_MOUSE) return;
    if (action == GLFW_PRESS)   { g_input->curr_mouse[btn] = 1; g_input->press_latch[btn] = 1; }
    if (action == GLFW_RELEASE) { g_input->curr_mouse[btn] = 0; g_input->release_latch[btn] = 1; }
    AimlInputEvent ev = {};
    ev.type  = (action == GLFW_PRESS) ? "mouse_button_down" : "mouse_button_up";
    ev.btn   = btn;
    ev.pos_x = (float)g_input->curr_pos_x;
    ev.pos_y = (float)g_input->curr_pos_y;
    g_input->events.push_back(ev);

    /* Update drag state */
    if (action == GLFW_PRESS) {
        auto& ds = g_input->drag[btn];
        ds.held               = true;
        ds.active             = false;
        /* Do NOT clear started/ended: glfwPollEvents processes all events of
         * a gap at the start of the frame in one go, so a release+press in
         * the same gap lost the drop (spec/44_0 §2.3 — edges are
         * kept, not discarded).  aiml_input_update cleans them up. */
        ds.origin_x = (float)g_input->curr_pos_x;
        ds.origin_y = (float)g_input->curr_pos_y;
        ds.acc_x = 0.f;
        ds.acc_y = 0.f;
    } else { /* RELEASE */
        auto& ds = g_input->drag[btn];
        if (ds.active) {
            ds.ended_this_frame = true;
            ds.end_x = (float)g_input->curr_pos_x;
            ds.end_y = (float)g_input->curr_pos_y;
            g_input->drag_payload = nullptr;
            AimlInputEvent dev = {};
            dev.type     = "drag_end";
            dev.btn      = btn;
            dev.pos_x    = (float)g_input->curr_pos_x;
            dev.pos_y    = (float)g_input->curr_pos_y;
            dev.scroll_x = ds.acc_x;
            dev.scroll_y = ds.acc_y;
            g_input->events.push_back(dev);
        }
        ds.held   = false;
        ds.active = false;
    }
}

/* ── Input injection (spec/44_0 §8) ─────────────────────────────────────────
 *
 * Deliberately routed through the SAME callbacks a real event takes, with a
 * null window: latches, drag accumulators and the event queue are then filled
 * by one code path only.  A parallel implementation would be a test that
 * exercises the test harness instead of the program.
 *
 * In-process: the OS cursor is never moved, so a running test cannot steal the
 * pointer from whoever is at the machine (spec/44_0 §8.1). */
extern "C" void aiml_input_post_mouse_move(float x, float y) {
    if (!g_input) return;
    glfw_cursor_cb(nullptr, (double)x, (double)y);
}
extern "C" void aiml_input_post_mouse_down(int32_t btn) {
    if (!g_input) return;
    glfw_mbtn_cb(nullptr, (int)btn, GLFW_PRESS, 0);
}
extern "C" void aiml_input_post_mouse_up(int32_t btn) {
    if (!g_input) return;
    glfw_mbtn_cb(nullptr, (int)btn, GLFW_RELEASE, 0);
}

/* spec/44_0 §8.3a: the *_pending variants.  The event layer (pend_*)
 * only exists in the browser branch -- there the DOM handlers write into it and
 * `update()` carries the edge over into the next frame.  Here the
 * GLFW callbacks write the frame-visible slots themselves; there is no second
 * layer one could inject into.  "Does what the event handler does"
 * therefore means natively exactly what post_mouse_* already does.
 *
 * Having them here anyway is the point: a program that calls them must
 * link on EVERY platform (Android has them as no-ops).  Before, they were missing
 * only in this branch, and the error only showed up at link time. */
extern "C" void aiml_input_post_mouse_move_pending(float x, float y) {
    aiml_input_post_mouse_move(x, y);
}
extern "C" void aiml_input_post_mouse_down_pending(int32_t btn) {
    aiml_input_post_mouse_down(btn);
}
extern "C" void aiml_input_post_mouse_up_pending(int32_t btn) {
    aiml_input_post_mouse_up(btn);
}
/* spec/44_0 §8.2: abort the active press on button 0 without a release latch. */
/* spec/44_0 §2.1: a key held while the window loses focus can never be released
 * TO US — the keyup goes to whoever has focus now.  Without this cleanup the
 * key stays down forever, and every `key_down` query keeps reporting it: a
 * camera on `key_down("E")` then rotates for the rest of the session (datacreatures,
 * 2026-11-15).  For keys there is no cancel channel as for the mouse, so
 * the focus loss is reported as a RELEASE -- the lesser evil, because whoever
 * tracks "held" from edges would otherwise hang without any event at all. */
static void release_keys_on_focus_loss(void) {
    if (!g_input) return;
    for (int k = 0; k < AIML_MAX_KEYS; k++) {
        if (!g_input->curr_keys[k]) continue;
        g_input->curr_keys[k]        = 0;
        g_input->key_release_latch[k] = 1;
    }
}

/* GLFW focus callback: a button held while the window loses focus can never be
 * completed — cancel it (spec/44_0 §2.2) instead of leaving it stuck down.
 * The same applies to held keys (§2.1). */
static void glfw_focus_cb(GLFWwindow* /*w*/, int focused) {
    if (!g_input || focused) return;
    for (int b = 0; b < AIML_MAX_MOUSE; b++) {
        if (!g_input->curr_mouse[b]) continue;
        g_input->curr_mouse[b]   = 0;
        g_input->cancel_latch[b] = 1;
        auto& ds = g_input->drag[b];
        if (ds.active) ds.cancelled_this_frame = true;
        ds.held = false; ds.active = false;
    }
    release_keys_on_focus_loss();
}

extern "C" void aiml_input_post_touch_cancel(void) {
    if (!g_input) return;
    for (int b = 0; b < AIML_MAX_MOUSE; b++) {
        if (!g_input->curr_mouse[b]) continue;
        g_input->curr_mouse[b]  = 0;
        g_input->cancel_latch[b] = 1;
        auto& ds = g_input->drag[b];
        if (ds.active) ds.cancelled_this_frame = true;
        ds.held = false; ds.active = false;
    }
}
extern "C" void aiml_input_post_scroll(float dx, float dy) {
    if (!g_input) return;
    glfw_scroll_cb(nullptr, (double)dx, (double)dy);
}
/* spec/44_0 §8.2: the window loses focus -- every running press is
 * cancelled, every held key released.  That makes §2.1 testable
 * without opening a second window. */
extern "C" void aiml_input_post_focus_lost(void) {
    if (!g_input) return;
    glfw_focus_cb(nullptr, 0);
}
extern "C" void aiml_input_post_key_down(const char* name) {
    if (!g_input || !name) return;
    int k = key_name_to_glfw(name);
    if (k < 0 || k >= AIML_MAX_KEYS) {
        fprintf(stderr, "[input] post_key_down: unknown key name '%s'\n", name);
        return;
    }
    glfw_key_cb(nullptr, k, 0, GLFW_PRESS, 0);
}
extern "C" void aiml_input_post_key_up(const char* name) {
    if (!g_input || !name) return;
    int k = key_name_to_glfw(name);
    if (k < 0 || k >= AIML_MAX_KEYS) {
        fprintf(stderr, "[input] post_key_up: unknown key name '%s'\n", name);
        return;
    }
    glfw_key_cb(nullptr, k, 0, GLFW_RELEASE, 0);
}
/* One "text" event per code point, exactly as typing produces them — decoding
 * UTF-8 so a multi-byte character arrives as ONE event, not as its bytes. */
extern "C" void aiml_input_post_text(const char* s) {
    if (!g_input || !s) return;
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        unsigned int cp = 0;
        int len = 1;
        if      (*p < 0x80)        { cp = *p;            len = 1; }
        else if ((*p & 0xE0)==0xC0){ cp = *p & 0x1F;     len = 2; }
        else if ((*p & 0xF0)==0xE0){ cp = *p & 0x0F;     len = 3; }
        else if ((*p & 0xF8)==0xF0){ cp = *p & 0x07;     len = 4; }
        for (int i = 1; i < len; i++) {
            if ((p[i] & 0xC0) != 0x80) { len = i; break; }   /* truncated → stop */
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        glfw_char_cb(nullptr, cp);
        p += len;
    }
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

void aiml_input_init(void* win_handle) {
    GLFWwindow* glfw = (GLFWwindow*)aiml_engine_glfw_window(win_handle);
    if (!glfw) {
        fprintf(stderr, "[input] aiml_input_init: no GLFWwindow available\n");
        return;
    }
    if (g_input) delete g_input;
    g_input = new AimlInputState();   // value-init: PODs = 0, C++ members properly constructed
    g_input->window = glfw;

    /* Seed mouse position so first delta is 0 */
    glfwGetCursorPos(glfw, &g_input->curr_pos_x, &g_input->curr_pos_y);
    g_input->prev_pos_x = g_input->curr_pos_x;
    g_input->prev_pos_y = g_input->curr_pos_y;

    glfwSetScrollCallback  (glfw, glfw_scroll_cb);
#ifndef __EMSCRIPTEN__
    /* Keyboard: in the browser it is owned by its own backend path (bin_keydown /
     * bin_keypress on the window target, see below) — that delivers canonical
     * key names, the clipboard and the paste event.  If one ADDITIONALLY registered
     * the GLFW callbacks here, every key press reported TWO
     * events: the cursor jumped two characters per arrow key and a
     * typed character appeared twice (reported 2026-10-15 on the cosmos
     * source text field; natively it never showed, because there is only this one source
     * there).  ONE source per event — otherwise someone counts twice. */
    glfwSetKeyCallback     (glfw, glfw_key_cb);
    glfwSetCharCallback    (glfw, glfw_char_cb);
#endif
    glfwSetCursorPosCallback(glfw, glfw_cursor_cb);
    glfwSetMouseButtonCallback(glfw, glfw_mbtn_cb);
    glfwSetWindowFocusCallback(glfw, glfw_focus_cb);

    /* Register per-frame update with the engine */
    aiml_engine_register_input_update(aiml_input_update);
}

void aiml_input_update(void) {
    if (!g_input) return;

    /* 0. Clear events from previous frame (prevents unbounded deque growth) */
    g_input->events.clear();

    /* 1. Save prev state */
    memcpy(g_input->prev_keys,  g_input->curr_keys,  AIML_MAX_KEYS);
    memcpy(g_input->prev_mouse, g_input->curr_mouse, AIML_MAX_MOUSE);
    memset(g_input->press_latch,   0, AIML_MAX_MOUSE);
    memset(g_input->release_latch, 0, AIML_MAX_MOUSE);
    memset(g_input->cancel_latch,  0, AIML_MAX_MOUSE);
    memset(g_input->key_press_latch,   0, AIML_MAX_KEYS);
    memset(g_input->key_release_latch, 0, AIML_MAX_KEYS);
    /* New frame → rising-edge consumption for key_pressed_once resets, so the
     * first _once read this frame sees the edge (subsequent reads get 0). */
    memset(g_input->consumed_press, 0, AIML_MAX_KEYS);
    for (int i = 0; i < AIML_MAX_GAMEPADS; i++)
        g_input->prev_gamepad[i] = g_input->curr_gamepad[i];

    /* 2. Poll gamepad state (event-less) */
    for (int i = 0; i < AIML_MAX_GAMEPADS; i++) {
        if (glfwJoystickIsGamepad(GLFW_JOYSTICK_1 + i))
            glfwGetGamepadState(GLFW_JOYSTICK_1 + i, &g_input->curr_gamepad[i]);
        else
            memset(&g_input->curr_gamepad[i], 0, sizeof(GLFWgamepadstate));
    }

    /* 3. Drain scroll accumulator */
    g_input->scroll_this_x   = g_input->scroll_accum_x;
    g_input->scroll_this_y   = g_input->scroll_accum_y;
    g_input->scroll_accum_x  = 0.f;
    g_input->scroll_accum_y  = 0.f;

    /* 4. Push scroll event if any */
    if (g_input->scroll_this_x != 0.f || g_input->scroll_this_y != 0.f) {
        AimlInputEvent ev = {};
        ev.type     = "mouse_scroll";
        ev.scroll_x = g_input->scroll_this_x;
        ev.scroll_y = g_input->scroll_this_y;
        g_input->events.push_back(ev);
    }

    /* 5. Compute mouse delta */
    g_input->delta_x = (float)(g_input->curr_pos_x - g_input->prev_pos_x);
    g_input->delta_y = (float)(g_input->curr_pos_y - g_input->prev_pos_y);

    if (g_input->captured) {
        /* Re-center cursor so delta doesn't saturate */
        int w, h;
        glfwGetWindowSize(g_input->window, &w, &h);
        double cx = w * 0.5, cy = h * 0.5;
        glfwSetCursorPos(g_input->window, cx, cy);
        g_input->curr_pos_x = cx;
        g_input->curr_pos_y = cy;
    }
    g_input->prev_pos_x = g_input->curr_pos_x;
    g_input->prev_pos_y = g_input->curr_pos_y;

    /* Reset per-frame drag flags */
    for (int b = 0; b < AIML_MAX_MOUSE; b++) {
        g_input->drag[b].started_this_frame   = false;
        g_input->drag[b].ended_this_frame     = false;
        g_input->drag[b].cancelled_this_frame = false;
    }
}

void aiml_input_set_capture(int32_t on) {
    if (!g_input) return;
    g_input->captured = (on != 0);
    if (g_input->window) {
        glfwSetInputMode(g_input->window, GLFW_CURSOR,
            on ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
}

/* ── Raw polling: keyboard ───────────────────────────────────────────────── */

int32_t aiml_input_key_down    (const char* k) { return curr_key_active(k) ? 1 : 0; }
int32_t aiml_input_key_pressed (const char* k) { return (g_input && key_latched(k, g_input->key_press_latch)) ? 1 : 0; }
int32_t aiml_input_key_released(const char* k) { return (g_input && key_latched(k, g_input->key_release_latch)) ? 1 : 0; }

/* System clipboard (text_input copy/paste).  Backed by the GLFW window; empty
 * string when no window/clipboard is available. */
extern "C" const char* aiml_input_clipboard_get(void) {
    if (!g_input || !g_input->window) return "";
    const char* s = glfwGetClipboardString(g_input->window);
    return s ? s : "";
}
extern "C" void aiml_input_clipboard_set(const char* s) {
    if (!g_input || !g_input->window || !s) return;
    glfwSetClipboardString(g_input->window, s);
}

/* Single per-frame consume slot for a key name (aliases → their left key). */
static int consume_slot_for(const char* name) {
    if (!name) return -1;
    if (strcmp(name,"Shift")==0) return GLFW_KEY_LEFT_SHIFT;
    if (strcmp(name,"Ctrl")==0)  return GLFW_KEY_LEFT_CONTROL;
    if (strcmp(name,"Alt")==0)   return GLFW_KEY_LEFT_ALT;
    int k = key_name_to_glfw(name);
    if (k < 0 || k >= AIML_MAX_KEYS) return -1;
    return k;
}
/* key_pressed_once: rising edge, but consumed on first read per frame — so a
 * handler polled multiple times per frame (e.g. from render() called after each
 * action) fires exactly ONCE per physical press.  Use for edge-triggered actions
 * (skip, toggles); key_pressed stays idempotent for queries. */
int32_t aiml_input_key_pressed_once(const char* k) {
    if (!g_input) return 0;
    if (!key_latched(k, g_input->key_press_latch)) return 0;      /* not a rising edge */
    int slot = consume_slot_for(k);
    if (slot < 0) return 1;                       /* edge but unmappable → report, no dedup */
    if (g_input->consumed_press[slot]) return 0;  /* already consumed this frame */
    g_input->consumed_press[slot] = 1;
    return 1;
}

/* ── Raw polling: mouse ──────────────────────────────────────────────────── */

float   aiml_input_mouse_pos_x   (void) { return g_input ? (float)g_input->curr_pos_x : 0.f; }
float   aiml_input_mouse_pos_y   (void) { return g_input ? (float)g_input->curr_pos_y : 0.f; }
float   aiml_input_mouse_delta_x (void) { return g_input ? g_input->delta_x : 0.f; }
float   aiml_input_mouse_delta_y (void) { return g_input ? g_input->delta_y : 0.f; }
float   aiml_input_mouse_scroll_x(void) { return g_input ? g_input->scroll_this_x : 0.f; }
float   aiml_input_mouse_scroll_y(void) { return g_input ? g_input->scroll_this_y : 0.f; }

int32_t aiml_input_mouse_button  (int32_t b) {
    if (!g_input || b < 0 || b >= AIML_MAX_MOUSE) return 0;
    return g_input->curr_mouse[b] ? 1 : 0;
}
int32_t aiml_input_mouse_pressed (int32_t b) {
    if (!g_input || b < 0 || b >= AIML_MAX_MOUSE) return 0;
    return g_input->press_latch[b] ? 1 : 0;
}
int32_t aiml_input_mouse_released(int32_t b) {
    if (!g_input || b < 0 || b >= AIML_MAX_MOUSE) return 0;
    return g_input->release_latch[b] ? 1 : 0;
}
int32_t aiml_input_mouse_cancelled(int32_t b) {
    if (!g_input || b < 0 || b >= AIML_MAX_MOUSE) return 0;
    return g_input->cancel_latch[b] ? 1 : 0;
}

/* ── Raw polling: gamepad ────────────────────────────────────────────────── */

static float apply_deadzone(float v, float dz = 0.1f) {
    if (fabsf(v) < dz) return 0.f;
    float s = (v > 0.f) ? 1.f : -1.f;
    return s * (fabsf(v) - dz) / (1.f - dz);
}

int32_t aiml_input_gamepad_connected(int32_t pad) {
    if (pad < 0 || pad >= AIML_MAX_GAMEPADS) return 0;
    return glfwJoystickIsGamepad(GLFW_JOYSTICK_1 + pad) ? 1 : 0;
}
float aiml_input_gamepad_axis(int32_t pad, int32_t axis) {
    if (!g_input || pad < 0 || pad >= AIML_MAX_GAMEPADS) return 0.f;
    if (axis < 0 || axis >= GLFW_GAMEPAD_AXIS_LAST + 1) return 0.f;
    return apply_deadzone(g_input->curr_gamepad[pad].axes[axis]);
}
int32_t aiml_input_gamepad_button(int32_t pad, int32_t btn) {
    if (!g_input || pad < 0 || pad >= AIML_MAX_GAMEPADS) return 0;
    if (btn < 0 || btn > GLFW_GAMEPAD_BUTTON_LAST) return 0;
    return g_input->curr_gamepad[pad].buttons[btn] == GLFW_PRESS ? 1 : 0;
}
int32_t aiml_input_gamepad_pressed(int32_t pad, int32_t btn) {
    if (!g_input || pad < 0 || pad >= AIML_MAX_GAMEPADS) return 0;
    if (btn < 0 || btn > GLFW_GAMEPAD_BUTTON_LAST) return 0;
    return (g_input->curr_gamepad[pad].buttons[btn] == GLFW_PRESS &&
            g_input->prev_gamepad[pad].buttons[btn] != GLFW_PRESS) ? 1 : 0;
}

/* ── Event queue ─────────────────────────────────────────────────────────── */

int32_t aiml_input_has_event(void) {
    return (g_input && !g_input->events.empty()) ? 1 : 0;
}

void* aiml_input_next_event(void) {
    if (!g_input || g_input->events.empty()) {
        memset(&g_current_event, 0, sizeof(g_current_event));
        g_current_event.type = "";
        g_current_event.key  = "";
        return &g_current_event;
    }
    /* Copy front to static buffer, fix up char_buf pointer */
    g_current_event = g_input->events.front();
    g_input->events.pop_front();
    /* char_ is always the char_buf of g_current_event (copy-on-pop) */
    if (!g_current_event.type) g_current_event.type = "";
    if (!g_current_event.key)  g_current_event.key  = "";
    return &g_current_event;
}

void aiml_input_clear_events(void) {
    if (g_input) g_input->events.clear();
}

/* ── Mapping: axis ────────────────────────────────────────────────────────── */

void aiml_input_map_axis(const char* name, const char* key, float scale) {
    if (!g_input || !name || !key) return;
    AimlInputState::AxisEntry e;
    e.name     = name;
    e.src      = AimlInputState::SRC_KEY;
    e.glfw_key = key_name_to_glfw(key);
    e.scale    = scale;
    g_input->axes.push_back(e);
}
void aiml_input_map_axis_mouse(const char* name, const char* comp, float scale) {
    if (!g_input || !name || !comp) return;
    AimlInputState::AxisEntry e;
    e.name      = name;
    e.src       = AimlInputState::SRC_MOUSE;
    e.component = (strcmp(comp, "y") == 0) ? 1 : 0;
    e.scale     = scale;
    g_input->axes.push_back(e);
}
void aiml_input_map_axis_gamepad(const char* name, int32_t pad, int32_t axis, float scale) {
    if (!g_input || !name) return;
    AimlInputState::AxisEntry e;
    e.name     = name;
    e.src      = AimlInputState::SRC_GAMEPAD;
    e.pad      = pad;
    e.axis_idx = axis;
    e.scale    = scale;
    g_input->axes.push_back(e);
}

/* ── Mapping: action ─────────────────────────────────────────────────────── */

void aiml_input_map_action(const char* name, const char* key) {
    if (!g_input || !name || !key) return;
    AimlInputState::ActionEntry e;
    e.name     = name;
    e.src      = AimlInputState::SRC_KEY;
    e.glfw_key = key_name_to_glfw(key);
    g_input->actions.push_back(e);
}
void aiml_input_map_action_mouse(const char* name, int32_t btn) {
    if (!g_input || !name) return;
    AimlInputState::ActionEntry e;
    e.name      = name;
    e.src       = AimlInputState::SRC_MOUSE;
    e.mouse_btn = btn;
    g_input->actions.push_back(e);
}
void aiml_input_map_action_gamepad(const char* name, int32_t pad, int32_t btn) {
    if (!g_input || !name) return;
    AimlInputState::ActionEntry e;
    e.name    = name;
    e.src     = AimlInputState::SRC_GAMEPAD;
    e.pad     = pad;
    e.btn_idx = btn;
    g_input->actions.push_back(e);
}

/* ── Mapping: removal ────────────────────────────────────────────────────── */

void aiml_input_unmap(const char* name) {
    if (!g_input || !name) return;
    auto& ax = g_input->axes;
    ax.erase(std::remove_if(ax.begin(), ax.end(), [&](auto& e){ return e.name == name; }), ax.end());
    auto& ac = g_input->actions;
    ac.erase(std::remove_if(ac.begin(), ac.end(), [&](auto& e){ return e.name == name; }), ac.end());
}
void aiml_input_clear_mappings(void) {
    if (!g_input) return;
    g_input->axes.clear();
    g_input->actions.clear();
}

/* ── Mapping: query ──────────────────────────────────────────────────────── */

float aiml_input_axis(const char* name) {
    if (!g_input || !name) return 0.f;
    float sum = 0.f;
    for (auto& e : g_input->axes) {
        if (e.name != name) continue;
        float contrib = 0.f;
        switch (e.src) {
        case AimlInputState::SRC_KEY:
            if (e.glfw_key >= 0 && e.glfw_key < AIML_MAX_KEYS && g_input->curr_keys[e.glfw_key])
                contrib = e.scale;
            break;
        case AimlInputState::SRC_MOUSE:
            contrib = (e.component == 0 ? g_input->delta_x : g_input->delta_y) * e.scale;
            break;
        case AimlInputState::SRC_GAMEPAD:
            if (e.pad >= 0 && e.pad < AIML_MAX_GAMEPADS)
                contrib = aiml_input_gamepad_axis(e.pad, e.axis_idx) * e.scale;
            break;
        }
        sum += contrib;
    }
    if (sum >  1.f) sum =  1.f;
    if (sum < -1.f) sum = -1.f;
    return sum;
}

static bool action_source_active_curr(const AimlInputState::ActionEntry& e) {
    switch (e.src) {
    case AimlInputState::SRC_KEY:
        if (e.glfw_key < 0 || e.glfw_key >= AIML_MAX_KEYS) return false;
        return g_input->curr_keys[e.glfw_key] != 0;
    case AimlInputState::SRC_MOUSE:
        if (e.mouse_btn < 0 || e.mouse_btn >= AIML_MAX_MOUSE) return false;
        return g_input->curr_mouse[e.mouse_btn] != 0;
    case AimlInputState::SRC_GAMEPAD:
        return aiml_input_gamepad_button(e.pad, e.btn_idx) != 0;
    }
    return false;
}
static bool action_source_active_prev(const AimlInputState::ActionEntry& e) {
    switch (e.src) {
    case AimlInputState::SRC_KEY:
        if (e.glfw_key < 0 || e.glfw_key >= AIML_MAX_KEYS) return false;
        return g_input->prev_keys[e.glfw_key] != 0;
    case AimlInputState::SRC_MOUSE:
        if (e.mouse_btn < 0 || e.mouse_btn >= AIML_MAX_MOUSE) return false;
        return g_input->prev_mouse[e.mouse_btn] != 0;
    case AimlInputState::SRC_GAMEPAD:
        if (e.pad < 0 || e.pad >= AIML_MAX_GAMEPADS) return false;
        if (e.btn_idx < 0 || e.btn_idx > GLFW_GAMEPAD_BUTTON_LAST) return false;
        return g_input->prev_gamepad[e.pad].buttons[e.btn_idx] == GLFW_PRESS;
    }
    return false;
}

int32_t aiml_input_action_down(const char* name) {
    if (!g_input || !name) return 0;
    for (auto& e : g_input->actions)
        if (e.name == name && action_source_active_curr(e)) return 1;
    return 0;
}
int32_t aiml_input_action_pressed(const char* name) {
    if (!g_input || !name) return 0;
    bool was_down = false;
    for (auto& e : g_input->actions)
        if (e.name == name) {
            if (action_source_active_curr(e) && !action_source_active_prev(e)) return 1;
            if (action_source_active_prev(e)) was_down = true;
        }
    return 0;
}
int32_t aiml_input_action_released(const char* name) {
    if (!g_input || !name) return 0;
    for (auto& e : g_input->actions)
        if (e.name == name && !action_source_active_curr(e) && action_source_active_prev(e))
            return 1;
    return 0;
}

/* ── Drag-and-drop polling ───────────────────────────────────────────────── */

int32_t aiml_input_drag_active(int32_t btn) {
    if (!g_input || btn < 0 || btn >= AIML_MAX_MOUSE) return 0;
    return g_input->drag[btn].active ? 1 : 0;
}
int32_t aiml_input_drag_started(int32_t btn) {
    if (!g_input || btn < 0 || btn >= AIML_MAX_MOUSE) return 0;
    return g_input->drag[btn].started_this_frame ? 1 : 0;
}
int32_t aiml_input_drag_ended(int32_t btn) {
    if (!g_input || btn < 0 || btn >= AIML_MAX_MOUSE) return 0;
    return g_input->drag[btn].ended_this_frame ? 1 : 0;
}
int32_t aiml_input_drag_cancelled(int32_t btn) {
    if (!g_input || btn < 0 || btn >= AIML_MAX_MOUSE) return 0;
    return g_input->drag[btn].cancelled_this_frame ? 1 : 0;
}
float aiml_input_drag_origin_x(int32_t btn) {
    if (!g_input || btn < 0 || btn >= AIML_MAX_MOUSE) return 0.f;
    return g_input->drag[btn].origin_x;
}
float aiml_input_drag_origin_y(int32_t btn) {
    if (!g_input || btn < 0 || btn >= AIML_MAX_MOUSE) return 0.f;
    return g_input->drag[btn].origin_y;
}
float aiml_input_drag_start_x(int32_t btn) {
    if (!g_input || btn < 0 || btn >= AIML_MAX_MOUSE) return 0.f;
    return g_input->drag[btn].start_x;
}
float aiml_input_drag_start_y(int32_t btn) {
    if (!g_input || btn < 0 || btn >= AIML_MAX_MOUSE) return 0.f;
    return g_input->drag[btn].start_y;
}
float aiml_input_drag_end_x(int32_t btn) {
    if (!g_input || btn < 0 || btn >= AIML_MAX_MOUSE) return 0.f;
    return g_input->drag[btn].end_x;
}
float aiml_input_drag_end_y(int32_t btn) {
    if (!g_input || btn < 0 || btn >= AIML_MAX_MOUSE) return 0.f;
    return g_input->drag[btn].end_y;
}
float aiml_input_drag_delta_x(int32_t btn) {
    if (!g_input || btn < 0 || btn >= AIML_MAX_MOUSE) return 0.f;
    return g_input->drag[btn].acc_x;
}
float aiml_input_drag_delta_y(int32_t btn) {
    if (!g_input || btn < 0 || btn >= AIML_MAX_MOUSE) return 0.f;
    return g_input->drag[btn].acc_y;
}
void aiml_input_drag_set_payload(void* data) {
    if (g_input) g_input->drag_payload = data;
}
void* aiml_input_drag_get_payload(void) {
    return g_input ? g_input->drag_payload : nullptr;
}
void aiml_input_drag_set_threshold(float px) {
    if (g_input) g_input->drag_threshold = (px > 0.f) ? px : 0.f;
}

/* ══════════════════════════════════════════════════════════════════════════ */
/* STUB IMPLEMENTATIONS (Emscripten + Android)                               */
/* ══════════════════════════════════════════════════════════════════════════ */

#elif defined(__EMSCRIPTEN__)
/* ─── Browser implementation via emscripten HTML5 callbacks ──────────────── */
#include <emscripten.h>          /* EM_ASM_DOUBLE (canvas getBoundingClientRect) */
#include <emscripten/html5.h>
#include <math.h>
#include <deque>
#include <string>

struct BrowserDrag {
    bool  held;
    bool  active;
    /* frame-visible edges (what the queries return) … */
    bool  started_this_frame;
    bool  ended_this_frame;
    bool  cancelled_this_frame;
    /* … and the pending ones the DOM handlers write.  Same split as
     * pend_press/frame_press: an edge that happens while a frame is still
     * running (long frame, e.g. a blocking RPC) must be reported in the NEXT
     * frame, not dropped (spec/44_0 §2.4). */
    bool  pend_started;
    bool  pend_ended;
    bool  pend_cancelled;
    /* Where the release took place.  The latched edge alone is not enough: it
     * may only be reported in the next frame, and by then the
     * cursor is somewhere else — without this position the drop landed at the new
     * press point (wrong target) instead of at the release point (spec/44_0 §2.3). */
    float pend_end_x;
    float pend_end_y;
    float end_x;
    float end_y;
    /* Where the drag BEGAN, latched with the started edge.  `origin_*` alone
     * is not enough: it is overwritten immediately by EVERY new press,
     * while the edge is only reported in the next frame.  With
     * "press A -> drag -> release -> press B" in one gap,
     * origin_* then pointed to B, and the pickup grabbed at B instead of at A. */
    float pend_start_x;
    float pend_start_y;
    float start_x;
    float start_y;
    float origin_x;
    float origin_y;
    float acc_x;
    float acc_y;
};

struct BrowserInputState {
    /* mouse */
    double curr_pos_x, curr_pos_y;
    double prev_pos_x, prev_pos_y;
    float  delta_x, delta_y;
    uint8_t curr_mouse[AIML_MAX_MOUSE];   // per-frame snapshot (spec/44)
    uint8_t prev_mouse[AIML_MAX_MOUSE];
    uint8_t raw_mouse[AIML_MAX_MOUSE];    // live state, written by the DOM handlers
    uint8_t pend_press[AIML_MAX_MOUSE];   // edges since the last snapshot
    uint8_t pend_release[AIML_MAX_MOUSE];
    uint8_t frame_press[AIML_MAX_MOUSE];  // edges visible this frame
    uint8_t frame_release[AIML_MAX_MOUSE];
    uint8_t pend_cancel[AIML_MAX_MOUSE];  // spec/44_0 §2.2: aborted press, NOT a release
    uint8_t frame_cancel[AIML_MAX_MOUSE];
    float   scroll_accum_x, scroll_accum_y;
    float   scroll_this_x, scroll_this_y;
    BrowserDrag drag[AIML_MAX_MOUSE];
    float   drag_threshold;
    void*   drag_payload;
    /* keyboard */
    uint8_t curr_keys[AIML_MAX_KEYS];  // per-frame snapshot (spec/44), indexed by name_to_slot
    uint8_t prev_keys[AIML_MAX_KEYS];
    uint8_t raw_keys[AIML_MAX_KEYS];        // live state, written by the DOM handlers
    uint8_t pend_kpress[AIML_MAX_KEYS];     // edges since the last snapshot
    uint8_t pend_krelease[AIML_MAX_KEYS];
    uint8_t frame_kpress[AIML_MAX_KEYS];    // edges visible this frame
    uint8_t frame_krelease[AIML_MAX_KEYS];
    uint8_t consumed_press[AIML_MAX_KEYS];  // per-frame edge consumption (key_pressed_once)
    /* event queue for "text" + "key_down" — consumed by ui_runtime */
    std::deque<AimlInputEvent> events;
    /* small backing store so the char* fields returned in event accessors stay
     * alive after pop. We reuse g_current_event.char_buf / type slots. */
    std::deque<std::string> key_strings;
};

static BrowserInputState* g_b = nullptr;

/* Map a browser mouse event's viewport coords (clientX/Y) into the GL
 * backing-store coordinate space the engine/UI hit-test in.
 *
 * We compute from clientX/Y minus the canvas bounding rect — NOT from the
 * event's targetX/Y.  Reason: the mousemove/mouseup callbacks are registered on
 * the WINDOW (so drags keep tracking when the cursor leaves the canvas), and for
 * a window-target event Emscripten sets targetX == clientX (viewport-relative,
 * with NO canvas offset).  The canvas is centered + letterboxed, so using
 * targetX directly shifts hover/hit-testing by the letterbox margin (the "hover
 * fires a bit next to the widget" bug).  Subtracting the canvas rect works for
 * both window- and canvas-registered callbacks.  Then scale CSS→backing (the
 * canvas is displayed CSS-scaled while its backing store stays at the logical
 * size, e.g. 1280x720). */
static void bin_event_xy(double clientX, double clientY, double* out_x, double* out_y) {
    double rectL = EM_ASM_DOUBLE({
        var c = document.getElementById('canvas');
        return c ? c.getBoundingClientRect().left : 0;
    });
    double rectT = EM_ASM_DOUBLE({
        var c = document.getElementById('canvas');
        return c ? c.getBoundingClientRect().top : 0;
    });
    int    bw = 0, bh = 0;
    double cw = 0.0, ch = 0.0;
    emscripten_get_canvas_element_size("#canvas", &bw, &bh);
    emscripten_get_element_css_size("#canvas", &cw, &ch);
    double sx = (cw > 0.0) ? (double)bw / cw : 1.0;
    double sy = (ch > 0.0) ? (double)bh / ch : 1.0;
    *out_x = (clientX - rectL) * sx;
    *out_y = (clientY - rectT) * sy;
}

/* spec/44_0 §2: canonical is 0=left, 1=RIGHT, 2=middle (the GLFW order,
 * which the native path keeps).  DOM `MouseEvent.button` numbers 0=left,
 * 1=middle, 2=right.  This adapter passed the DOM index through unchanged,
 * which swapped 1 and 2 relative to the spec in the WASM build. */
static inline int dom_btn_to_aiml(int dom_btn) {
    switch (dom_btn) {
        case 0:  return 0;   /* links  */
        case 1:  return 2;   /* Mitte  */
        case 2:  return 1;   /* rechts */
        default: return -1;
    }
}

static EM_BOOL bin_mousemove(int /*type*/, const EmscriptenMouseEvent* e, void* /*ud*/) {
    if (!g_b) return EM_FALSE;
    double mx, my; bin_event_xy(e->clientX, e->clientY, &mx, &my);
    g_b->curr_pos_x = mx;
    g_b->curr_pos_y = my;
    /* drag accumulator update */
    for (int b = 0; b < AIML_MAX_MOUSE; b++) {
        auto& ds = g_b->drag[b];
        if (!ds.held) continue;
        ds.acc_x = (float)mx - ds.origin_x;
        ds.acc_y = (float)my - ds.origin_y;
        float dist = sqrtf(ds.acc_x * ds.acc_x + ds.acc_y * ds.acc_y);
        if (!ds.active && dist >= g_b->drag_threshold) {
            ds.active             = true;
            ds.pend_started       = true;
            ds.pend_start_x       = ds.origin_x;
            ds.pend_start_y       = ds.origin_y;
        }
    }
    return EM_TRUE;
}
static EM_BOOL bin_mousedown(int /*type*/, const EmscriptenMouseEvent* e, void* /*ud*/) {
    if (!g_b) return EM_FALSE;
    int b = dom_btn_to_aiml(e->button);
    if (b < 0 || b >= AIML_MAX_MOUSE) return EM_TRUE;
    g_b->raw_mouse[b] = 1; g_b->pend_press[b] = 1;
    double mx, my; bin_event_xy(e->clientX, e->clientY, &mx, &my);
    g_b->curr_pos_x = mx; g_b->curr_pos_y = my;
    auto& ds = g_b->drag[b];
    ds.held = true; ds.active = false;
    /* started/ended_this_frame stay as well: those are THIS
     * frame's values, and spec/44_0 §2.3 says they hold for the whole frame (every
     * reader sees the same value).  A press arriving in the middle of the frame
     * would otherwise have zeroed them for the rest of the frame — the drop would
     * vanish already in the RUNNING frame, even before the latch.
     * `aiml_input_update` sets them anew from the pend_* at the start of the frame.
     *
     * pend_started/ended/cancelled are also deliberately not cleared.
     * They are edges not yet delivered: the DOM handlers run between
     * frames AND in the middle of a frame (every blocking network call polls with
     * emscripten_sleep and yields to the JS event loop).  If a
     * new press cleared them, a release+press within the same
     * frame would lose the drop without replacement — the user drags, releases, presses
     * again impatiently, and the drop is gone.  spec/44_0 §2.3 says on this
     * normatively: kept and reported in the next frame, not discarded.
     * `aiml_input_update` clears them on delivery, that is enough. */
    ds.origin_x = (float)mx; ds.origin_y = (float)my;
    ds.acc_x = ds.acc_y = 0.f;
    return EM_TRUE;
}
static EM_BOOL bin_mouseup(int /*type*/, const EmscriptenMouseEvent* e, void* /*ud*/) {
    if (!g_b) return EM_FALSE;
    int b = dom_btn_to_aiml(e->button);
    if (b < 0 || b >= AIML_MAX_MOUSE) return EM_TRUE;
    if (g_b->raw_mouse[b]) g_b->pend_release[b] = 1;   // mouseup is window-wide: only after our mousedown
    g_b->raw_mouse[b] = 0;
    auto& ds = g_b->drag[b];
    if (ds.active) {
        double ux, uy; bin_event_xy(e->clientX, e->clientY, &ux, &uy);
        ds.pend_ended = true;
        ds.pend_end_x = (float)ux;
        ds.pend_end_y = (float)uy;
    }
    ds.held = false; ds.active = false;
    return EM_TRUE;
}
static void bin_cancel_press(int btn);   /* defined with the touch handlers below */

/* spec/44_0 §2.1: release held keys on focus loss.  The `keyup`
 * goes to the window that has the focus NOW -- without this cleanup
 * `raw_keys` stays set forever, and a camera on `key_down("E")` rotates for the
 * rest of the session (datacreatures, 2026-11-15). */
static void bin_release_keys(void) {
    if (!g_b) return;
    for (int k = 0; k < AIML_MAX_KEYS; k++) {
        if (!g_b->raw_keys[k] && !g_b->curr_keys[k]) continue;
        g_b->raw_keys[k]  = 0;
        g_b->curr_keys[k] = 0;
        g_b->pend_krelease[k]  = 1;
        g_b->frame_krelease[k] = 1;
    }
}

/* touchcancel = the system took the press away (incoming call, edge gesture,
 * browser took over scrolling, notification overlay).  The gesture did NOT
 * happen, so this must not look like touchend (spec/44_0 §2.2). */
static EM_BOOL bin_touchcancel(int /*type*/, const EmscriptenTouchEvent* /*e*/, void* /*ud*/) {
    if (!g_b) return EM_FALSE;
    bin_cancel_press(0);
    return EM_TRUE;
}
/* Focus loss / tab hidden with a button still held: same reasoning — the press
 * cannot be completed, so cancel it instead of leaving it stuck down. */
/* spec/44_0 §2.2 names as a cancel source "WINDOW focus loss / tab becoming
 * hidden".  An element INSIDE the page that loses focus is
 * not that -- and that is exactly what arrived here: when a button is clicked, the
 * hidden keyboard field (aiml_web_kbd, see below) gives up its focus.  The
 * cancellation set raw_mouse[0]=0, and bin_mouseup reports the release ONLY
 * if raw_mouse was set -- so the first button press after every text input
 * was lost without replacement.  In the DataCreatures login that meant: type,
 * click, nothing happens; only the second click counted.
 *
 * Hence two safeguards: the registration below no longer catches in the
 * capture phase (blur does not bubble, so an element blur no longer reaches the window
 * at all), and if a browser does deliver it anyway, it is recognised here
 * by the node name and skipped. */
static EM_BOOL bin_blur(int /*type*/, const EmscriptenFocusEvent* e, void* /*ud*/) {
    if (e && e->nodeName[0]) {
        const char* n = e->nodeName;
        if (strcmp(n, "INPUT")    == 0 || strcmp(n, "TEXTAREA") == 0 ||
            strcmp(n, "SELECT")   == 0 || strcmp(n, "BUTTON")   == 0 ||
            strcmp(n, "CANVAS")   == 0 || strcmp(n, "A")        == 0)
            return EM_FALSE;   /* element focus, not window focus */
    }
    for (int b = 0; b < AIML_MAX_MOUSE; b++) bin_cancel_press(b);
    bin_release_keys();
    return EM_FALSE;   /* don't swallow the event */
}
static EM_BOOL bin_wheel(int /*type*/, const EmscriptenWheelEvent* e, void* /*ud*/) {
    if (!g_b) return EM_FALSE;
    g_b->scroll_accum_x += (float)e->deltaX;
    g_b->scroll_accum_y += (float)e->deltaY;
    return EM_TRUE;
}

/* ── Touch → synthesized left-mouse (button 0) ───────────────────────────────
 * Mobile browsers deliver touch events, not mouse events (and do NOT reliably
 * synthesize mouse events for canvas apps).  We map the FIRST touch point to
 * mouse button 0 using the same clientX/Y→backing mapping as the mouse path, so
 * taps, hover-position and press-move-release DRAGS all work with one finger.
 * Returning EM_TRUE preventDefault()s → no page scroll/zoom/synthetic-mouse. */
/* spec/44_0 §2.2: abort the active press on `btn` WITHOUT reporting a release.
 * A cancel means the gesture did not happen, so it must never reach the drop
 * path.  Used by touchcancel, the two-finger takeover and focus loss. */
static void bin_cancel_press(int btn) {
    if (!g_b || btn < 0 || btn >= AIML_MAX_MOUSE) return;
    if (g_b->raw_mouse[btn]) g_b->pend_cancel[btn] = 1;
    g_b->raw_mouse[btn] = 0;
    auto& ds = g_b->drag[btn];
    if (ds.active) ds.pend_cancelled = true;
    ds.held = false; ds.active = false;
}

static EM_BOOL bin_touchstart(int /*type*/, const EmscriptenTouchEvent* e, void* /*ud*/) {
    if (!g_b || e->numTouches <= 0) return EM_FALSE;
    if (e->numTouches >= 2) {   /* two fingers = pan/zoom gesture (shell), not game
                                 * input — CANCEL the in-progress single-tap press.
                                 * A release here would look like a deliberate lift
                                 * and commit a drop (spec/44_0 §2.2). */
        bin_cancel_press(0);
        return EM_FALSE;
    }
    double mx, my; bin_event_xy(e->touches[0].clientX, e->touches[0].clientY, &mx, &my);
    g_b->curr_pos_x = mx; g_b->curr_pos_y = my;
    g_b->raw_mouse[0] = 1; g_b->pend_press[0] = 1;
    auto& ds = g_b->drag[0];
    ds.held = true; ds.active = false;
    /* started/ended + pend_* stay — see bin_mousedown (spec/44_0 §2.3). */
    ds.origin_x = (float)mx; ds.origin_y = (float)my;
    ds.acc_x = ds.acc_y = 0.f;
    return EM_TRUE;
}
static EM_BOOL bin_touchmove(int /*type*/, const EmscriptenTouchEvent* e, void* /*ud*/) {
    if (!g_b || e->numTouches <= 0) return EM_FALSE;
    if (e->numTouches >= 2) return EM_FALSE;   /* two-finger pan/zoom → shell, not game */
    double mx, my; bin_event_xy(e->touches[0].clientX, e->touches[0].clientY, &mx, &my);
    g_b->curr_pos_x = mx; g_b->curr_pos_y = my;
    auto& ds = g_b->drag[0];
    if (ds.held) {
        ds.acc_x = (float)mx - ds.origin_x;
        ds.acc_y = (float)my - ds.origin_y;
        float dist = sqrtf(ds.acc_x * ds.acc_x + ds.acc_y * ds.acc_y);
        if (!ds.active && dist >= g_b->drag_threshold) {
            ds.active = true; ds.pend_started = true;
            ds.pend_start_x = ds.origin_x; ds.pend_start_y = ds.origin_y;
        }
    }
    return EM_TRUE;
}
/* touchend = the finger was lifted deliberately → a real release.  The last
 * touchmove already set the final position, so we only release button 0 here
 * (touchend's `touches` list no longer contains the lifted finger).
 * touchcancel does NOT come here — see bin_touchcancel. */
static EM_BOOL bin_touchend(int /*type*/, const EmscriptenTouchEvent* /*e*/, void* /*ud*/) {
    if (!g_b) return EM_FALSE;
    if (g_b->raw_mouse[0]) g_b->pend_release[0] = 1;
    g_b->raw_mouse[0] = 0;
    auto& ds = g_b->drag[0];
    if (ds.active) {
        ds.pend_ended = true;
        ds.pend_end_x = (float)g_b->curr_pos_x;
        ds.pend_end_y = (float)g_b->curr_pos_y;
    }
    ds.held = false; ds.active = false;
    return EM_TRUE;
}

/* Forward declaration — defined further below in this same WASM section. */
static int32_t name_to_slot(const char* k);

/* Target of the 'paste' listener (registered in aiml_input_init, filled by JS,
 * read by aiml_input_paste_ready). */
static char g_em_paste_buf[65536];

/* Stash a string in the persistent backing store and return a stable ptr. */
static const char* bin_keep_string(const char* s) {
    if (!g_b || !s) return "";
    g_b->key_strings.emplace_back(s);
    return g_b->key_strings.back().c_str();
}

/* Bring JS key names to the NATIVE canon (cf. glfw_key_to_name):
 * letters UPPER-case, ArrowX → X, " " → Space, Control/Shift/Alt → LCtrl/LShift/
 * LAlt.  Without that every strcmp(key, "C") / "Left" / … fails in the browser —
 * that is exactly why Ctrl+C/Ctrl+V/Ctrl+A and the arrow keys in text_input
 * had no effect, while they worked natively. */
static const char* bin_key_canon(const char* k) {
    if (!k || !k[0]) return "";
    if (!k[1]) {
        if (k[0] >= 'a' && k[0] <= 'z') {
            static char up[2];                    /* like glfw_key_to_name: not thread-safe */
            up[0] = (char)(k[0] - 32); up[1] = '\0';
            return up;
        }
        if (k[0] == ' ') return "Space";
        return k;
    }
    if (strcmp(k, "ArrowUp")    == 0) return "Up";
    if (strcmp(k, "ArrowDown")  == 0) return "Down";
    if (strcmp(k, "ArrowLeft")  == 0) return "Left";
    if (strcmp(k, "ArrowRight") == 0) return "Right";
    if (strcmp(k, "Control")    == 0) return "LCtrl";
    if (strcmp(k, "Shift")      == 0) return "LShift";
    if (strcmp(k, "Alt")        == 0) return "LAlt";
    return k;
}

/* Take over the modifier state from the event flags.  More reliable than the
 * keydown/keyup pairs: if the user already holds Ctrl BEFORE the canvas gets the focus,
 * its keydown is otherwise missing forever. */
static void bin_sync_mods(const EmscriptenKeyboardEvent* e) {
    if (!g_b) return;
    const char* names[6] = {"lctrl", "rctrl", "lshift", "rshift", "lalt", "ralt"};
    bool on[6] = { (bool)e->ctrlKey,  (bool)e->ctrlKey,
                   (bool)e->shiftKey, (bool)e->shiftKey,
                   (bool)e->altKey,   (bool)e->altKey };
    for (int i = 0; i < 6; ++i) {
        int s = name_to_slot(names[i]);
        if (s >= 0) g_b->raw_keys[s] = on[i] ? 1 : 0;
    }
}

/* Defined later (clipboard block) — already needed here. */
extern "C" void aiml_input_clipboard_set(const char* s);

/* What a Ctrl+C in the focused widget would copy — implemented in
 * prefab_runtime.cpp.  The browser needs the answer SYNCHRONOUSLY within the
 * key interaction (see below); the aiml frame loop only runs later. */
extern "C" const char* aiml_focused_copy_text(void);

static EM_BOOL bin_keydown(int /*type*/, const EmscriptenKeyboardEvent* e, void* /*ud*/) {
    if (!g_b) return EM_FALSE;
    bin_sync_mods(e);
    const char* canon = bin_key_canon(e->key);
    bool single = (canon[0] != '\0' && canon[1] == '\0');
    /* Ctrl+C/Ctrl+X: the clipboard write MUST happen here, not later in
     * the frame loop — Firefox requires a running user interaction for writeText,
     * and so does execCommand('copy').  For that we read the
     * current selection state of the focused widget. */
    if (e->ctrlKey && single && (canon[0] == 'C' || canon[0] == 'X')) {
        const char* sel = aiml_focused_copy_text();
        if (sel && *sel) {
            aiml_input_clipboard_set(sel);
            /* Suppress the browser's default action: it would copy the canvas's (empty)
             * page selection and overwrite the clipboard we just
             * wrote.  The "C"/"X" is still
             * queued, so that the aiml side keeps its state in step.
             * Do NOT suppress on Ctrl+V — otherwise the 'paste' event does not come. */
            AimlInputEvent cev = {};
            cev.type = "key_down";
            cev.key  = bin_keep_string(canon);
            g_b->events.push_back(cev);
            int cslot = name_to_slot(canon);
            if (cslot >= 0) { if (!g_b->raw_keys[cslot]) g_b->pend_kpress[cslot] = 1; g_b->raw_keys[cslot] = 1; }
            return EM_TRUE;
        }
    }
    /* Ctrl+V: do NOT queue.  The browser only delivers the content in the
     * subsequent 'paste' event; a "V" queued now would paste the
     * OLD clipboard content.  The paste handler supplies the "V" afterwards. */
    if (e->ctrlKey && single && canon[0] == 'V') {
        int vslot = name_to_slot(canon);
        if (vslot >= 0) { if (!g_b->raw_keys[vslot]) g_b->pend_kpress[vslot] = 1; g_b->raw_keys[vslot] = 1; }
        return EM_FALSE;
    }
    /* Push a "key_down" event with the key name (canonical — e.g. "C", "Backspace") */
    AimlInputEvent ev = {};
    ev.type = "key_down";
    ev.key  = bin_keep_string(canon);
    g_b->events.push_back(ev);
    /* Update curr_keys using name_to_slot so that polling
     * input.key_down("up") sees the same slot we wrote here when the user
     * pressed ArrowUp. */
    int slot = name_to_slot(canon);
    if (slot >= 0) {
        if (!g_b->raw_keys[slot]) g_b->pend_kpress[slot] = 1;   // auto-repeat is no new press
        g_b->raw_keys[slot] = 1;
    }
    /* Swallow Backspace/Tab so the browser doesn't navigate or change focus
     * while the user types.  Also swallow Arrow keys / PageUp/PageDown /
     * Space so the page doesn't scroll while the user navigates a graph. */
    if (strcmp(e->key, " ") == 0) {
        /* Space is special: it is BOTH a printable char (text_input) AND a
         * page-scroll trigger we must preventDefault (EM_TRUE below).  But
         * preventDefault() on keydown suppresses the browser's follow-up
         * `keypress` event — which is the ONLY source of our "text" event
         * (see bin_keypress).  So the space would never reach a focused
         * text_input.  Emit the "text" event here ourselves; bin_keypress
         * skips space to avoid any double-insert on browsers that still fire
         * keypress.  Ctrl-held space is ignored by the text consumer
         * (apply_text_events_to_focus skips text events while Ctrl is down). */
        AimlInputEvent tev = {};
        tev.type = "text";
        tev.char_buf[0] = ' ';
        tev.char_buf[1] = '\0';
        g_b->events.push_back(tev);
        return EM_TRUE;
    }
    if (strcmp(e->key, "Backspace") == 0 || strcmp(e->key, "Tab") == 0 ||
        strcmp(e->key, "ArrowUp")  == 0 || strcmp(e->key, "ArrowDown") == 0 ||
        strcmp(e->key, "ArrowLeft")== 0 || strcmp(e->key, "ArrowRight")== 0 ||
        strcmp(e->key, "PageUp")   == 0 || strcmp(e->key, "PageDown")  == 0)
        return EM_TRUE;
    return EM_FALSE;
}

static EM_BOOL bin_keyup(int /*type*/, const EmscriptenKeyboardEvent* e, void* /*ud*/) {
    if (!g_b) return EM_FALSE;
    bin_sync_mods(e);
    const char* canon = bin_key_canon(e->key);
    AimlInputEvent ev = {};
    ev.type = "key_up";
    ev.key  = bin_keep_string(canon);
    g_b->events.push_back(ev);
    int slot = name_to_slot(canon);
    if (slot >= 0) {
        if (g_b->raw_keys[slot]) g_b->pend_krelease[slot] = 1;
        g_b->raw_keys[slot] = 0;
    }
    return EM_FALSE;
}

static EM_BOOL bin_keypress(int /*type*/, const EmscriptenKeyboardEvent* e, void* /*ud*/) {
    if (!g_b) return EM_FALSE;
    /* keypress fires for printable chars. e->key is the typed character (UTF-8
     * sequence in e->key as a string). Use it as the text payload. */
    if (e->key[0] == '\0') return EM_FALSE;
    /* Space is emitted from bin_keydown (its keypress is usually suppressed by
     * our preventDefault); skip here so a browser that still fires keypress for
     * space cannot double-insert it. */
    if (e->key[0] == ' ' && e->key[1] == '\0') return EM_TRUE;
    /* Filter: only forward keys that look like a single visible character.
     * Skips names like "Enter", "Shift", "Control" which arrive on some
     * platforms. */
    int klen = 0; while (e->key[klen] && klen < 32) ++klen;
    if (klen > 4) return EM_FALSE;  /* > UTF-8 max for one codepoint */
    AimlInputEvent ev = {};
    ev.type = "text";
    /* Copy into char_buf — accessor reads from there */
    strncpy(ev.char_buf, e->key, sizeof(ev.char_buf) - 1);
    ev.char_buf[sizeof(ev.char_buf) - 1] = '\0';
    g_b->events.push_back(ev);
    return EM_TRUE;
}

/* ── Web soft-keyboard DOM bridge ────────────────────────────────────────────
 * Canvas text_inputs are drawn on the GL surface, not real DOM elements, so
 * mobile browsers never raise the soft keyboard for them.  The prefab runtime
 * calls aiml_web_kbd_open/close when a text_input gains/loses focus; a tiny
 * transparent REAL <input> is focused so the (Android) keyboard appears, and
 * its beforeinput/composition events are translated into the SAME "text" /
 * "key_down" events the queue already carries.  Character flow:
 *   DOM <input>  →  aiml_input_web_text / aiml_input_web_key  →  events queue
 *                →  apply_text_events_to_focus() (prefab_runtime)  →  canvas. */

/* Push UTF-8 text as one or more "text" events.  char_buf holds only 7 usable
 * bytes, so chunk on codepoint boundaries — each chunk inserts sequentially at
 * the caret, so autocorrect words / pastes / emoji all arrive intact. */
extern "C" EMSCRIPTEN_KEEPALIVE void aiml_input_web_text(const char* utf8) {
    if (!g_b || !utf8) return;
    const unsigned char* p = (const unsigned char*)utf8;
    while (*p) {
        AimlInputEvent ev = {};
        ev.type = "text";
        int n = 0;
        while (*p && n < 7) {
            unsigned char c = *p;
            int clen = 1;
            if      ((c & 0x80) == 0x00) clen = 1;
            else if ((c & 0xE0) == 0xC0) clen = 2;
            else if ((c & 0xF0) == 0xE0) clen = 3;
            else if ((c & 0xF8) == 0xF0) clen = 4;
            if (n + clen > 7) break;                 /* don't split a codepoint */
            for (int i = 0; i < clen && *p; ++i) ev.char_buf[n++] = (char)*p++;
        }
        ev.char_buf[n] = '\0';
        if (n == 0) break;                            /* malformed byte guard */
        g_b->events.push_back(ev);
    }
}

/* Push a control key ("Backspace", "Enter", "Delete", "Left", "Right") as a
 * "key_down" event — consumed by apply_text_events_to_focus (prefab_runtime). */
extern "C" EMSCRIPTEN_KEEPALIVE void aiml_input_web_key(const char* key) {
    if (!g_b || !key) return;
    AimlInputEvent ev = {};
    ev.type = "key_down";
    ev.key  = bin_keep_string(key);
    g_b->events.push_back(ev);
}

/* Open/focus the hidden DOM <input> so the mobile soft keyboard appears, and
 * (first call) install the beforeinput/composition translation listeners. */
EM_JS(void, aiml_web_kbd_open, (int is_secret, int is_numeric, int is_email), {
    var el = document.getElementById('aiml_kbd');
    if (!el) {
        el = document.createElement('input');
        el.id = 'aiml_kbd';
        el.setAttribute('autocomplete', 'off');
        el.setAttribute('autocorrect', 'off');
        el.setAttribute('autocapitalize', 'off');
        el.setAttribute('spellcheck', 'false');
        /* Tiny, transparent, parked off-canvas — but NOT display:none, so it
         * stays focusable (that is what raises the keyboard). */
        el.style.cssText = 'position:fixed;left:-2000px;top:0;width:1px;height:1px;' +
                           'opacity:0;border:0;padding:0;margin:0;z-index:-1;';
        document.body.appendChild(el);
        var pushText = Module.cwrap('aiml_input_web_text', null, ['string']);
        var pushKey  = Module.cwrap('aiml_input_web_key',  null, ['string']);
        /* Zero-width sentinel: keeps the field non-empty so Backspace always
         * raises a deleteContentBackward beforeinput even when the AIML field
         * is empty (an empty <input> emits no delete event). */
        var ZWSP = String.fromCharCode(0x200b);
        var reset = function () { el.value = ZWSP; try { el.setSelectionRange(1, 1); } catch (e) {} };
        el._composing = false;
        /* Did this input come from a REAL key?  Then the window
         * keydown handler (bin_keydown) has already queued it, and the bridge
         * must stay silent — otherwise a backspace deletes TWO characters and an
         * arrow jumps two places.  Reported on the console source text field
         * of cosmos: "backspace always deleted 2 characters" (2026-10-18).
         *
         * 'Unidentified'/'Process' do NOT count as a real key: exactly those
         * are reported by soft keyboards and IMEs, and there the bridge is the only
         * source. */
        el._hwkey = false;
        el.addEventListener('keydown', function (e) {
            el._hwkey = (e.key && e.key !== 'Unidentified' && e.key !== 'Process' && !e.isComposing);
        });
        el.addEventListener('keyup', function () { el._hwkey = false; });
        el.addEventListener('compositionstart', function () { el._composing = true; });
        el.addEventListener('compositionend', function (e) {
            el._composing = false;
            if (e.data) pushText(e.data);
            reset();
        });
        el.addEventListener('beforeinput', function (e) {
            if (el._composing) return;               /* commit handled at compositionend */
            var t = e.inputType;
            /* The DOM field is only a PASSAGE, never a text store: every
             * input is intercepted and the sentinel restored.
             * Without that `el.value` drifts and the next backspace no longer
             * reports at all. */
            e.preventDefault();
            var ctrl_op = (t === 'insertLineBreak' || t === 'insertParagraph' ||
                           t === 'deleteContentBackward' || t === 'deleteContentForward');
            if (ctrl_op && el._hwkey) { reset(); return; }   /* bin_keydown already has it */
            if (t === 'insertText' || t === 'insertFromPaste' ||
                t === 'insertReplacementText' || t === 'insertFromComposition') {
                if (e.data) pushText(e.data);
            } else if (t === 'insertLineBreak' || t === 'insertParagraph') {
                pushKey('Enter');
            } else if (t === 'deleteContentBackward') {
                pushKey('Backspace');
            } else if (t === 'deleteContentForward') {
                pushKey('Delete');
            }
            reset();
        });
    }
    el.type = is_secret ? 'password' : (is_email ? 'email' : 'text');
    if (is_numeric) el.setAttribute('inputmode', 'numeric'); else el.removeAttribute('inputmode');
    el.value = String.fromCharCode(0x200b);
    try { el.focus({ preventScroll: true }); } catch (e) { el.focus(); }
    try { el.setSelectionRange(1, 1); } catch (e) {}
});

/* Blur the hidden input → mobile keyboard hides. */
EM_JS(void, aiml_web_kbd_close, (), {
    var el = document.getElementById('aiml_kbd');
    if (el) el.blur();
});

void aiml_input_init(void* /*win*/) {
    if (g_b) { delete g_b; g_b = nullptr; }
    g_b = new BrowserInputState();
    memset(g_b, 0, sizeof(*g_b));
    g_b->drag_threshold = 5.0f;
    /* Bind to the window so releases outside the canvas still end the drag.
     * Earlier we double-bound mouseup (canvas + window), which made the
     * handler fire twice per release — harmless for curr_mouse[] but messed
     * with drag-ended_this_frame logic. Window-only is sufficient because
     * the event still propagates through. */
    emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, bin_mousemove);
    emscripten_set_mousedown_callback("#canvas", nullptr, EM_TRUE, bin_mousedown);
    emscripten_set_mouseup_callback  (EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, bin_mouseup);
    emscripten_set_wheel_callback    ("#canvas", nullptr, EM_TRUE, bin_wheel);
    /* Touch → mouse-button-0 (mobile).  Start on the canvas; move/end on the
     * window so a drag keeps tracking if the finger leaves the canvas. */
    emscripten_set_touchstart_callback ("#canvas", nullptr, EM_TRUE, bin_touchstart);
    emscripten_set_touchmove_callback  (EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, bin_touchmove);
    emscripten_set_touchend_callback   (EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, bin_touchend);
    emscripten_set_touchcancel_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, bin_touchcancel);
    /* WITHOUT capture: `blur` does not bubble, but a window blur is
     * fired on the window itself.  With capture this handler also caught every
     * element blur of the page (see bin_blur). */
    emscripten_set_blur_callback       (EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_FALSE, bin_blur);
    /* Keyboard goes on the window target — canvas elements need explicit
     * tabindex to receive key events, but window always works. */
    emscripten_set_keydown_callback (EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, bin_keydown);
    emscripten_set_keyup_callback   (EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, bin_keyup);
    emscripten_set_keypress_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, bin_keypress);
    /* Paste: the browser hands out the clipboard content ONLY in the 'paste' event
     * (navigator.clipboard.readText is asynchronous and therefore not usable from a
     * synchronous query).  The handler stores the text and
     * supplies the "V" that bin_keydown deliberately did not queue. */
    /* `document` only exists in the browser.  Without the check
     * input.init tears down the whole application in every DOM-free environment
     * (node/ENVIRONMENT=node) with "ReferenceError: document is not defined"
     * — and that before any of it is even needed. */
    EM_ASM({
        if (typeof document === 'undefined') return;
        var buf = $0;
        var cap = $1;
        document.addEventListener('paste', function(ev) {
            try {
                var txt = ((ev.clipboardData || window.clipboardData).getData('text')) || "";
                stringToUTF8(txt, buf, cap);
                Module._aiml_input_paste_ready();
                ev.preventDefault();
            } catch (err) { /* no clipboard rights → paste nothing */ }
        });
    }, g_em_paste_buf, (int)sizeof(g_em_paste_buf));
    /* Register per-frame update with the engine — same hook as desktop. */
    aiml_engine_register_input_update(aiml_input_update);
}

void aiml_input_update(void) {
    if (!g_b) return;
    memcpy(g_b->prev_mouse, g_b->curr_mouse, AIML_MAX_MOUSE);
    memcpy(g_b->curr_mouse, g_b->raw_mouse, AIML_MAX_MOUSE);
    memcpy(g_b->frame_press, g_b->pend_press, AIML_MAX_MOUSE);
    memcpy(g_b->frame_release, g_b->pend_release, AIML_MAX_MOUSE);
    memcpy(g_b->frame_cancel, g_b->pend_cancel, AIML_MAX_MOUSE);
    memset(g_b->pend_cancel, 0, AIML_MAX_MOUSE);
    memset(g_b->pend_press, 0, AIML_MAX_MOUSE);
    memset(g_b->pend_release, 0, AIML_MAX_MOUSE);
    memcpy(g_b->prev_keys,  g_b->curr_keys,  AIML_MAX_KEYS);
    memcpy(g_b->curr_keys, g_b->raw_keys, AIML_MAX_KEYS);
    memcpy(g_b->frame_kpress, g_b->pend_kpress, AIML_MAX_KEYS);
    memcpy(g_b->frame_krelease, g_b->pend_krelease, AIML_MAX_KEYS);
    memset(g_b->pend_kpress, 0, AIML_MAX_KEYS);
    memset(g_b->pend_krelease, 0, AIML_MAX_KEYS);
    memset(g_b->consumed_press, 0, AIML_MAX_KEYS);  // new frame → reset _once edges
    g_b->delta_x = (float)(g_b->curr_pos_x - g_b->prev_pos_x);
    g_b->delta_y = (float)(g_b->curr_pos_y - g_b->prev_pos_y);
    g_b->prev_pos_x = g_b->curr_pos_x;
    g_b->prev_pos_y = g_b->curr_pos_y;
    g_b->scroll_this_x  = g_b->scroll_accum_x;
    g_b->scroll_this_y  = g_b->scroll_accum_y;
    g_b->scroll_accum_x = 0.f;
    g_b->scroll_accum_y = 0.f;
    for (int b = 0; b < AIML_MAX_MOUSE; b++) {
        auto& ds = g_b->drag[b];
        ds.started_this_frame   = ds.pend_started;
        ds.ended_this_frame     = ds.pend_ended;
        ds.cancelled_this_frame = ds.pend_cancelled;
        /* Latch the position WITH the edge, otherwise by delivery time it already
         * belongs to the next gesture. */
        if (ds.pend_started) { ds.start_x = ds.pend_start_x; ds.start_y = ds.pend_start_y; }
        if (ds.pend_ended) { ds.end_x = ds.pend_end_x; ds.end_y = ds.pend_end_y; }
        ds.pend_started = ds.pend_ended = ds.pend_cancelled = false;
    }
    /* Trim event queue if it grew unbounded (e.g. event consumer never ran). */
    while (g_b->events.size() > 256) g_b->events.pop_front();
    while (g_b->key_strings.size() > 1024) g_b->key_strings.pop_front();
}

void aiml_input_set_capture(int32_t /*on*/) { /* PointerLock not wired yet */ }

/* Map a key name (JS event.key OR AIML canonical) to a curr_keys[] slot.
 *
 * Both the WASM keydown-callback (which receives JS event.key like
 * "ArrowUp" / " " / "Escape") and AIML polling (which uses canonical names
 * like "up" / "space" / "escape" / "plus") go through this function — that
 * way both sides agree on a single slot per logical key.
 *
 * Normalisation: known JS aliases are rewritten to AIML canonical, then the
 * name is lowercased.  Single-character names hash to ASCII; multi-char names
 * use a small string hash. */
static int32_t name_to_slot(const char* k) {
    if (!k || !k[0]) return -1;

    /* Step 1: JS aliases → AIML canonical. */
    const char* in = k;
    if      (strcmp(k, "ArrowUp")    == 0) in = "up";
    else if (strcmp(k, "ArrowDown")  == 0) in = "down";
    else if (strcmp(k, "ArrowLeft")  == 0) in = "left";
    else if (strcmp(k, "ArrowRight") == 0) in = "right";
    else if (strcmp(k, " ")          == 0) in = "space";
    else if (strcmp(k, "+")          == 0) in = "plus";
    else if (strcmp(k, "-")          == 0) in = "minus";
    else if (strcmp(k, "=")          == 0) in = "equal";
    else if (strcmp(k, "PageUp")     == 0) in = "pageup";
    else if (strcmp(k, "PageDown")   == 0) in = "pagedown";

    /* Step 2: copy + lowercase. */
    char canon[32];
    int len = 0;
    for (; in[len] && len < 31; ++len)
        canon[len] = (in[len] >= 'A' && in[len] <= 'Z')
                   ? (char)(in[len] + 32) : in[len];
    canon[len] = '\0';

    /* Step 3: single ASCII char → uppercase code mod MAX (compact slot for
     * letter/digit keys). Multi-char → small hash. */
    if (canon[1] == '\0') {
        char c = canon[0];
        if (c >= 'a' && c <= 'z') c -= 32;
        return ((int32_t)(uint8_t)c) % AIML_MAX_KEYS;
    }
    uint32_t h = 0;
    for (const char* p = canon; *p; ++p) h = h * 131u + (uint8_t)*p;
    return (int32_t)(h % AIML_MAX_KEYS);
}
/* ── Input injection, browser (spec/44_0 §8) ─────────────────────────────────
 *
 * Writes raw_* + pend_* exactly as the DOM handlers do (bin_mousedown etc.),
 * so the per-frame snapshot in aiml_input_update picks injected edges up the
 * same way as real ones.  Coordinates are already in the engine's
 * backing-store space, so bin_event_xy (CSS→backing conversion) is NOT applied
 * here — a caller feeding .child_rect_* values is in that space already. */
extern "C" void aiml_input_post_mouse_move(float x, float y) {
    if (!g_b) return;
    g_b->curr_pos_x = (double)x;
    g_b->curr_pos_y = (double)y;
    for (int b = 0; b < AIML_MAX_MOUSE; b++) {
        auto& ds = g_b->drag[b];
        if (!ds.held) continue;
        ds.acc_x = x - ds.origin_x;
        ds.acc_y = y - ds.origin_y;
        float dist = sqrtf(ds.acc_x * ds.acc_x + ds.acc_y * ds.acc_y);
        if (!ds.active && dist >= g_b->drag_threshold) {
            ds.active = true; ds.started_this_frame = true;
            ds.start_x = ds.origin_x; ds.start_y = ds.origin_y;
        }
    }
}
extern "C" void aiml_input_post_mouse_down(int32_t btn) {
    if (!g_b || btn < 0 || btn >= AIML_MAX_MOUSE) return;
    /* raw = held (survives the next snapshot), curr + frame_press =
     * visible NOW.  pend_press deliberately stays 0: otherwise the edge would fire
     * twice — now and after the next update.  That way the same
     * frame contract applies as on the desktop (spec/44_0 §8.3): inject, read,
     * gone at the next update. */
    g_b->raw_mouse[btn] = 1;
    g_b->curr_mouse[btn] = 1;
    g_b->frame_press[btn] = 1;
    auto& ds = g_b->drag[btn];
    ds.held = true; ds.active = false;
    ds.started_this_frame = ds.ended_this_frame = false;
    ds.pend_started = ds.pend_ended = ds.pend_cancelled = false;
    ds.origin_x = (float)g_b->curr_pos_x; ds.origin_y = (float)g_b->curr_pos_y;
    ds.acc_x = ds.acc_y = 0.f;
}
extern "C" void aiml_input_post_mouse_up(int32_t btn) {
    if (!g_b || btn < 0 || btn >= AIML_MAX_MOUSE) return;
    g_b->raw_mouse[btn] = 0;
    g_b->curr_mouse[btn] = 0;
    g_b->frame_release[btn] = 1;
    auto& ds = g_b->drag[btn];
    if (ds.active) {
        ds.ended_this_frame = true;
        ds.end_x = (float)g_b->curr_pos_x;
        ds.end_y = (float)g_b->curr_pos_y;
    }
    ds.held = false; ds.active = false;
}
/* ── Injection into the EVENT layer (spec/44_0 §8.4) ───────────────────────
 *
 * The three hooks above write the FRAME-visible slots: inject, read,
 * gone at the next update.  But that makes exactly the layer unreachable
 * that §2.3 is about — the `pend_*` layer, into which the DOM handlers
 * write between frames AND in the middle of a frame.  The promise "an
 * edge created during a frame is kept and reported in the next
 * frame" was therefore not testable from outside.
 *
 * These variants do exactly what `bin_mousedown`/`bin_mouseup`/
 * `bin_mousemove` do.  A test can thereby replay a real event sequence
 * — in particular release AND press in the same gap — and afterwards
 * check with `input.update()` what is delivered. */
extern "C" void aiml_input_post_mouse_down_pending(int32_t btn) {
    if (!g_b || btn < 0 || btn >= AIML_MAX_MOUSE) return;
    g_b->raw_mouse[btn] = 1;
    g_b->pend_press[btn] = 1;
    auto& ds = g_b->drag[btn];
    ds.held = true; ds.active = false;
    /* pend_* and started/ended_this_frame stay — like bin_mousedown. */
    ds.origin_x = (float)g_b->curr_pos_x; ds.origin_y = (float)g_b->curr_pos_y;
    ds.acc_x = ds.acc_y = 0.f;
}
extern "C" void aiml_input_post_mouse_up_pending(int32_t btn) {
    if (!g_b || btn < 0 || btn >= AIML_MAX_MOUSE) return;
    if (g_b->raw_mouse[btn]) g_b->pend_release[btn] = 1;
    g_b->raw_mouse[btn] = 0;
    auto& ds = g_b->drag[btn];
    if (ds.active) {
        ds.pend_ended = true;
        ds.pend_end_x = (float)g_b->curr_pos_x;
        ds.pend_end_y = (float)g_b->curr_pos_y;
    }
    ds.held = false; ds.active = false;
}
extern "C" void aiml_input_post_mouse_move_pending(float x, float y) {
    if (!g_b) return;
    g_b->curr_pos_x = (double)x;
    g_b->curr_pos_y = (double)y;
    for (int b = 0; b < AIML_MAX_MOUSE; b++) {
        auto& ds = g_b->drag[b];
        if (!ds.held) continue;
        ds.acc_x = x - ds.origin_x;
        ds.acc_y = y - ds.origin_y;
        float dist = sqrtf(ds.acc_x * ds.acc_x + ds.acc_y * ds.acc_y);
        if (!ds.active && dist >= g_b->drag_threshold) {
            ds.active = true; ds.pend_started = true;
            ds.pend_start_x = ds.origin_x; ds.pend_start_y = ds.origin_y;
        }
    }
}

/* spec/44_0 §8.2: focus loss -- cancel the press AND release held keys
 * (§2.1).  The cancellation is further below, hence the forward declaration. */
extern "C" void aiml_input_post_touch_cancel(void);
extern "C" void aiml_input_post_focus_lost(void) {
    if (!g_b) return;
    aiml_input_post_touch_cancel();
    bin_release_keys();
}

/* spec/44_0 §8.2 + §8.3: write the FRAME-visible cancel slot directly (same rule
 * as post_mouse_up here), so an injected cancel fires exactly once. */
extern "C" void aiml_input_post_touch_cancel(void) {
    if (!g_b) return;
    for (int b = 0; b < AIML_MAX_MOUSE; b++) {
        if (!g_b->raw_mouse[b] && !g_b->curr_mouse[b]) continue;
        g_b->raw_mouse[b]    = 0;
        g_b->curr_mouse[b]   = 0;
        g_b->frame_cancel[b] = 1;
        auto& ds = g_b->drag[b];
        if (ds.active) ds.cancelled_this_frame = true;
        ds.held = false; ds.active = false;
    }
}
extern "C" void aiml_input_post_scroll(float dx, float dy) {
    if (!g_b) return;
    /* scroll_this is the value this frame sees; accum would only be
     * drained at the next update (same reasoning as for the keys). */
    g_b->scroll_this_x += dx;
    g_b->scroll_this_y += dy;
}
extern "C" void aiml_input_post_key_down(const char* name) {
    if (!g_b || !name) return;
    int slot = name_to_slot(name);
    if (slot < 0) return;
    if (!g_b->raw_keys[slot]) g_b->frame_kpress[slot] = 1;
    g_b->raw_keys[slot]  = 1;
    g_b->curr_keys[slot] = 1;
    AimlInputEvent ev = {};
    ev.type = "key_down";
    g_b->key_strings.push_back(name);
    ev.key = g_b->key_strings.back().c_str();
    g_b->events.push_back(ev);
}
extern "C" void aiml_input_post_key_up(const char* name) {
    if (!g_b || !name) return;
    int slot = name_to_slot(name);
    if (slot < 0) return;
    if (g_b->raw_keys[slot]) g_b->frame_krelease[slot] = 1;
    g_b->raw_keys[slot]  = 0;
    g_b->curr_keys[slot] = 0;
}
extern "C" void aiml_input_post_text(const char* s) {
    if (!g_b || !s) return;
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        int len = 1;
        if      (*p < 0x80)         len = 1;
        else if ((*p & 0xE0)==0xC0) len = 2;
        else if ((*p & 0xF0)==0xE0) len = 3;
        else if ((*p & 0xF8)==0xF0) len = 4;
        AimlInputEvent ev = {};
        ev.type = "text";
        for (int i = 0; i < len && i < 4; i++) {
            if (i && (p[i] & 0xC0) != 0x80) { len = i; break; }
            ev.char_buf[i] = (char)p[i];
        }
        g_b->events.push_back(ev);
        p += len;
    }
}

int32_t aiml_input_key_down    (const char* k) {
    if (!g_b) return 0;
    int slot = name_to_slot(k);
    if (slot < 0) return 0;
    return g_b->curr_keys[slot] ? 1 : 0;
}
int32_t aiml_input_key_pressed (const char* k) {
    if (!g_b) return 0;
    int slot = name_to_slot(k);
    if (slot < 0) return 0;
    return g_b->frame_kpress[slot] ? 1 : 0;
}
int32_t aiml_input_key_pressed_once(const char* k) {
    if (!g_b) return 0;
    int slot = name_to_slot(k);
    if (slot < 0) return 0;
    if (!g_b->frame_kpress[slot]) return 0;                           /* not a rising edge */
    if (g_b->consumed_press[slot]) return 0;                          /* consumed this frame */
    g_b->consumed_press[slot] = 1;
    return 1;
}
int32_t aiml_input_key_released(const char* k) {
    if (!g_b) return 0;
    int slot = name_to_slot(k);
    if (slot < 0) return 0;
    return g_b->frame_krelease[slot] ? 1 : 0;
}

float   aiml_input_mouse_pos_x   (void) { return g_b ? (float)g_b->curr_pos_x : 0.f; }
float   aiml_input_mouse_pos_y   (void) { return g_b ? (float)g_b->curr_pos_y : 0.f; }
float   aiml_input_mouse_delta_x (void) { return g_b ? g_b->delta_x : 0.f; }
float   aiml_input_mouse_delta_y (void) { return g_b ? g_b->delta_y : 0.f; }
int32_t aiml_input_mouse_button  (int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0;
    return g_b->curr_mouse[b] ? 1 : 0;
}
int32_t aiml_input_mouse_pressed (int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0;
    return g_b->frame_press[b] ? 1 : 0;
}
int32_t aiml_input_mouse_released(int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0;
    return g_b->frame_release[b] ? 1 : 0;
}
int32_t aiml_input_mouse_cancelled(int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0;
    return g_b->frame_cancel[b] ? 1 : 0;
}
float   aiml_input_mouse_scroll_x(void) { return g_b ? g_b->scroll_this_x : 0.f; }
float   aiml_input_mouse_scroll_y(void) { return g_b ? g_b->scroll_this_y : 0.f; }

int32_t aiml_input_gamepad_connected(int32_t) { return 0; }
float   aiml_input_gamepad_axis     (int32_t, int32_t) { return 0.f; }
int32_t aiml_input_gamepad_button   (int32_t, int32_t) { return 0; }
int32_t aiml_input_gamepad_pressed  (int32_t, int32_t) { return 0; }

int32_t aiml_input_has_event(void) {
    return (g_b && !g_b->events.empty()) ? 1 : 0;
}
void*   aiml_input_next_event(void) {
    if (!g_b || g_b->events.empty()) {
        memset(&g_current_event, 0, sizeof(g_current_event));
        g_current_event.type = ""; g_current_event.key = "";
        return &g_current_event;
    }
    g_current_event = g_b->events.front();
    g_b->events.pop_front();
    if (!g_current_event.type) g_current_event.type = "";
    if (!g_current_event.key)  g_current_event.key  = "";
    return &g_current_event;
}
void    aiml_input_clear_events(void) { if (g_b) g_b->events.clear(); }

void aiml_input_map_axis        (const char*, const char*, float) {}
void aiml_input_map_axis_mouse  (const char*, const char*, float) {}
void aiml_input_map_axis_gamepad(const char*, int32_t, int32_t, float) {}
void aiml_input_map_action        (const char*, const char*) {}
void aiml_input_map_action_mouse  (const char*, int32_t) {}
void aiml_input_map_action_gamepad(const char*, int32_t, int32_t) {}
void aiml_input_unmap        (const char*) {}
void aiml_input_clear_mappings(void) {}
float   aiml_input_axis           (const char*) { return 0.f; }
int32_t aiml_input_action_down    (const char*) { return 0; }
int32_t aiml_input_action_pressed (const char*) { return 0; }
int32_t aiml_input_action_released(const char*) { return 0; }

int32_t aiml_input_drag_active (int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0;
    return g_b->drag[b].active ? 1 : 0;
}
int32_t aiml_input_drag_started(int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0;
    return g_b->drag[b].started_this_frame ? 1 : 0;
}
int32_t aiml_input_drag_ended  (int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0;
    return g_b->drag[b].ended_this_frame ? 1 : 0;
}
int32_t aiml_input_drag_cancelled(int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0;
    return g_b->drag[b].cancelled_this_frame ? 1 : 0;
}
float   aiml_input_drag_origin_x(int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0.f;
    return g_b->drag[b].origin_x;
}
float   aiml_input_drag_origin_y(int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0.f;
    return g_b->drag[b].origin_y;
}
float   aiml_input_drag_start_x (int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0.f;
    return g_b->drag[b].start_x;
}
float   aiml_input_drag_start_y (int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0.f;
    return g_b->drag[b].start_y;
}
float   aiml_input_drag_end_x   (int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0.f;
    return g_b->drag[b].end_x;
}
float   aiml_input_drag_end_y   (int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0.f;
    return g_b->drag[b].end_y;
}
float   aiml_input_drag_delta_x (int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0.f;
    return g_b->drag[b].acc_x;
}
float   aiml_input_drag_delta_y (int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0.f;
    return g_b->drag[b].acc_y;
}
void    aiml_input_drag_set_payload  (void* p) { if (g_b) g_b->drag_payload = p; }
void*   aiml_input_drag_get_payload  (void)    { return g_b ? g_b->drag_payload : nullptr; }
void    aiml_input_drag_set_threshold(float t) { if (g_b) g_b->drag_threshold = t; }

/* Browser clipboard.  The in-page buffer keeps within-app copy/paste working
 * (paste needs navigator.clipboard.readText, which is async — not usable from a
 * synchronous getter).  WRITING additionally goes to the real system clipboard
 * so copied text reaches other applications; navigator.clipboard.writeText
 * needs a secure context (https or localhost) and a user gesture (a button
 * click or Ctrl+C qualifies), with a textarea + execCommand fallback. */
static std::string g_em_clipboard;
/* Called from the paste listener: take over the content and supply the "V". */
extern "C" EMSCRIPTEN_KEEPALIVE void aiml_input_paste_ready(void) {
    if (!g_b) return;
    g_em_paste_buf[sizeof(g_em_paste_buf) - 1] = '\0';
    g_em_clipboard = g_em_paste_buf;
    AimlInputEvent ev = {};
    ev.type = "key_down";
    ev.key  = bin_keep_string("V");
    g_b->events.push_back(ev);
}
extern "C" const char* aiml_input_clipboard_get(void)         { return g_em_clipboard.c_str(); }
extern "C" void        aiml_input_clipboard_set(const char* s) {
    g_em_clipboard = s ? s : "";
    EM_ASM({
        var txt = UTF8ToString($0);
        try {
            if (navigator.clipboard && navigator.clipboard.writeText) {
                navigator.clipboard.writeText(txt);
                return;
            }
        } catch (e) { /* fall through to the legacy path */ }
        try {
            var ta = document.createElement('textarea');
            ta.value = txt;
            ta.setAttribute('readonly', '');
            ta.style.position = 'fixed';
            ta.style.opacity = '0';
            document.body.appendChild(ta);
            ta.select();
            document.execCommand('copy');
            document.body.removeChild(ta);
        } catch (e2) { /* clipboard unavailable — in-page buffer still holds it */ }
    }, g_em_clipboard.c_str());
}

#else /* __ANDROID__ */

/* spec/44_0 §8: injection not implemented on this platform. */
void aiml_input_post_mouse_move(float, float) {}
void aiml_input_post_mouse_down(int32_t) {}
void aiml_input_post_mouse_up  (int32_t) {}
void aiml_input_post_mouse_move_pending(float, float) {}
void aiml_input_post_mouse_down_pending(int32_t) {}
void aiml_input_post_mouse_up_pending  (int32_t) {}
void aiml_input_post_touch_cancel(void)   {}
void aiml_input_post_focus_lost(void)     {}
void aiml_input_post_scroll    (float, float) {}
void aiml_input_post_key_down  (const char*) {}
void aiml_input_post_key_up    (const char*) {}
void aiml_input_post_text      (const char*) {}

void    aiml_input_init       (void* /*win*/)   { /* TODO: platform */ }
void    aiml_input_update     (void)            { /* TODO: platform */ }
void    aiml_input_set_capture(int32_t /*on*/)  { /* TODO: platform */ }

int32_t aiml_input_key_down    (const char*) { return 0; }
int32_t aiml_input_key_pressed (const char*) { return 0; }
int32_t aiml_input_key_pressed_once(const char*) { return 0; }
int32_t aiml_input_key_released(const char*) { return 0; }

float   aiml_input_mouse_pos_x   (void) { return 0.f; }
float   aiml_input_mouse_pos_y   (void) { return 0.f; }
float   aiml_input_mouse_delta_x (void) { return 0.f; }
float   aiml_input_mouse_delta_y (void) { return 0.f; }
int32_t aiml_input_mouse_button  (int32_t) { return 0; }
int32_t aiml_input_mouse_pressed (int32_t) { return 0; }
int32_t aiml_input_mouse_released(int32_t) { return 0; }
int32_t aiml_input_mouse_cancelled(int32_t) { return 0; }
float   aiml_input_mouse_scroll_x(void) { return 0.f; }
float   aiml_input_mouse_scroll_y(void) { return 0.f; }

int32_t aiml_input_gamepad_connected(int32_t) { return 0; }
float   aiml_input_gamepad_axis     (int32_t, int32_t) { return 0.f; }
int32_t aiml_input_gamepad_button   (int32_t, int32_t) { return 0; }
int32_t aiml_input_gamepad_pressed  (int32_t, int32_t) { return 0; }

int32_t aiml_input_has_event  (void) { return 0; }
void*   aiml_input_next_event (void) {
    memset(&g_current_event, 0, sizeof(g_current_event));
    g_current_event.type = ""; g_current_event.key = "";
    return &g_current_event;
}
void    aiml_input_clear_events(void) {}

void aiml_input_map_axis        (const char*, const char*, float) {}
void aiml_input_map_axis_mouse  (const char*, const char*, float) {}
void aiml_input_map_axis_gamepad(const char*, int32_t, int32_t, float) {}
void aiml_input_map_action        (const char*, const char*) {}
void aiml_input_map_action_mouse  (const char*, int32_t) {}
void aiml_input_map_action_gamepad(const char*, int32_t, int32_t) {}
void aiml_input_unmap        (const char*) {}
void aiml_input_clear_mappings(void) {}

float   aiml_input_axis           (const char*) { return 0.f; }
int32_t aiml_input_action_down    (const char*) { return 0; }
int32_t aiml_input_action_pressed (const char*) { return 0; }
int32_t aiml_input_action_released(const char*) { return 0; }

int32_t aiml_input_drag_active       (int32_t) { return 0; }
int32_t aiml_input_drag_started      (int32_t) { return 0; }
int32_t aiml_input_drag_ended        (int32_t) { return 0; }
int32_t aiml_input_drag_cancelled    (int32_t) { return 0; }
float   aiml_input_drag_origin_x     (int32_t) { return 0.f; }
float   aiml_input_drag_origin_y     (int32_t) { return 0.f; }
float   aiml_input_drag_start_x      (int32_t) { return 0.f; }
float   aiml_input_drag_start_y      (int32_t) { return 0.f; }
float   aiml_input_drag_end_x        (int32_t) { return 0.f; }
float   aiml_input_drag_end_y        (int32_t) { return 0.f; }
float   aiml_input_drag_delta_x      (int32_t) { return 0.f; }
float   aiml_input_drag_delta_y      (int32_t) { return 0.f; }
void    aiml_input_drag_set_payload  (void*)    {}
void*   aiml_input_drag_get_payload  (void)     { return nullptr; }
void    aiml_input_drag_set_threshold(float)    {}

extern "C" const char* aiml_input_clipboard_get(void)        { return ""; }
extern "C" void        aiml_input_clipboard_set(const char*) {}

#endif /* platform stubs */
