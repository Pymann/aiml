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

    /* Mouse buttons */
    uint8_t curr_mouse[AIML_MAX_MOUSE];
    uint8_t prev_mouse[AIML_MAX_MOUSE];

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
        bool  ended_this_frame;   /* drag ended this polling cycle */
        float origin_x, origin_y; /* cursor pos when button pressed */
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

static bool prev_key_active(const char* name) {
    if (!g_input) return false;
    if (strcmp(name,"Shift")==0)
        return g_input->prev_keys[GLFW_KEY_LEFT_SHIFT] || g_input->prev_keys[GLFW_KEY_RIGHT_SHIFT];
    if (strcmp(name,"Ctrl")==0)
        return g_input->prev_keys[GLFW_KEY_LEFT_CONTROL] || g_input->prev_keys[GLFW_KEY_RIGHT_CONTROL];
    if (strcmp(name,"Alt")==0)
        return g_input->prev_keys[GLFW_KEY_LEFT_ALT] || g_input->prev_keys[GLFW_KEY_RIGHT_ALT];
    int k = key_name_to_glfw(name);
    if (k < 0 || k >= AIML_MAX_KEYS) return false;
    return g_input->prev_keys[k] != 0;
}

/* ── GLFW callbacks ───────────────────────────────────────────────────────── */

static void glfw_scroll_cb(GLFWwindow*, double x, double y) {
    if (!g_input) return;
    g_input->scroll_accum_x += (float)x;
    g_input->scroll_accum_y += (float)y;
}

static void glfw_key_cb(GLFWwindow*, int key, int /*scan*/, int action, int /*mods*/) {
    if (!g_input || key < 0 || key >= AIML_MAX_KEYS) return;
    if (action == GLFW_PRESS || action == GLFW_REPEAT)
        g_input->curr_keys[key] = 1;
    else if (action == GLFW_RELEASE)
        g_input->curr_keys[key] = 0;
    if (action == GLFW_REPEAT) return;  /* only push events for press/release */

    AimlInputEvent ev = {};
    ev.type = (action == GLFW_RELEASE) ? "key_up" : "key_down";
    ev.key  = glfw_key_to_name(key);
    g_input->events.push_back(ev);
}

static void glfw_char_cb(GLFWwindow*, unsigned int cp) {
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
    if (!g_input || btn < 0 || btn >= AIML_MAX_MOUSE) return;
    if (action == GLFW_PRESS)   g_input->curr_mouse[btn] = 1;
    if (action == GLFW_RELEASE) g_input->curr_mouse[btn] = 0;
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
        ds.started_this_frame = false;
        ds.ended_this_frame   = false;
        ds.origin_x = (float)g_input->curr_pos_x;
        ds.origin_y = (float)g_input->curr_pos_y;
        ds.acc_x = 0.f;
        ds.acc_y = 0.f;
    } else { /* RELEASE */
        auto& ds = g_input->drag[btn];
        if (ds.active) {
            ds.ended_this_frame = true;
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
    glfwSetKeyCallback     (glfw, glfw_key_cb);
    glfwSetCharCallback    (glfw, glfw_char_cb);
    glfwSetCursorPosCallback(glfw, glfw_cursor_cb);
    glfwSetMouseButtonCallback(glfw, glfw_mbtn_cb);

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
        g_input->drag[b].started_this_frame = false;
        g_input->drag[b].ended_this_frame   = false;
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
int32_t aiml_input_key_pressed (const char* k) { return (curr_key_active(k) && !prev_key_active(k)) ? 1 : 0; }
int32_t aiml_input_key_released(const char* k) { return (!curr_key_active(k) && prev_key_active(k)) ? 1 : 0; }

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
    if (!(curr_key_active(k) && !prev_key_active(k))) return 0;   /* not a rising edge */
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
    return (g_input->curr_mouse[b] && !g_input->prev_mouse[b]) ? 1 : 0;
}
int32_t aiml_input_mouse_released(int32_t b) {
    if (!g_input || b < 0 || b >= AIML_MAX_MOUSE) return 0;
    return (!g_input->curr_mouse[b] && g_input->prev_mouse[b]) ? 1 : 0;
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
float aiml_input_drag_origin_x(int32_t btn) {
    if (!g_input || btn < 0 || btn >= AIML_MAX_MOUSE) return 0.f;
    return g_input->drag[btn].origin_x;
}
float aiml_input_drag_origin_y(int32_t btn) {
    if (!g_input || btn < 0 || btn >= AIML_MAX_MOUSE) return 0.f;
    return g_input->drag[btn].origin_y;
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
#include <emscripten/html5.h>
#include <math.h>
#include <deque>
#include <string>

struct BrowserDrag {
    bool  held;
    bool  active;
    bool  started_this_frame;
    bool  ended_this_frame;
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
    uint8_t curr_mouse[AIML_MAX_MOUSE];
    uint8_t prev_mouse[AIML_MAX_MOUSE];
    float   scroll_accum_x, scroll_accum_y;
    float   scroll_this_x, scroll_this_y;
    BrowserDrag drag[AIML_MAX_MOUSE];
    float   drag_threshold;
    void*   drag_payload;
    /* keyboard */
    uint8_t curr_keys[AIML_MAX_KEYS];  // indexed by emscripten DOM_PK_* keyCode mod-256
    uint8_t prev_keys[AIML_MAX_KEYS];
    uint8_t consumed_press[AIML_MAX_KEYS];  // per-frame edge consumption (key_pressed_once)
    /* event queue for "text" + "key_down" — consumed by ui_runtime */
    std::deque<AimlInputEvent> events;
    /* small backing store so the char* fields returned in event accessors stay
     * alive after pop. We reuse g_current_event.char_buf / type slots. */
    std::deque<std::string> key_strings;
};

static BrowserInputState* g_b = nullptr;

static EM_BOOL bin_mousemove(int /*type*/, const EmscriptenMouseEvent* e, void* /*ud*/) {
    if (!g_b) return EM_FALSE;
    g_b->curr_pos_x = e->targetX;
    g_b->curr_pos_y = e->targetY;
    /* drag accumulator update */
    for (int b = 0; b < AIML_MAX_MOUSE; b++) {
        auto& ds = g_b->drag[b];
        if (!ds.held) continue;
        ds.acc_x = (float)e->targetX - ds.origin_x;
        ds.acc_y = (float)e->targetY - ds.origin_y;
        float dist = sqrtf(ds.acc_x * ds.acc_x + ds.acc_y * ds.acc_y);
        if (!ds.active && dist >= g_b->drag_threshold) {
            ds.active             = true;
            ds.started_this_frame = true;
        }
    }
    return EM_TRUE;
}
static EM_BOOL bin_mousedown(int /*type*/, const EmscriptenMouseEvent* e, void* /*ud*/) {
    if (!g_b) return EM_FALSE;
    int b = e->button; /* 0=left 1=middle 2=right */
    if (b < 0 || b >= AIML_MAX_MOUSE) return EM_TRUE;
    g_b->curr_mouse[b] = 1;
    auto& ds = g_b->drag[b];
    ds.held = true; ds.active = false;
    ds.started_this_frame = ds.ended_this_frame = false;
    ds.origin_x = (float)e->targetX; ds.origin_y = (float)e->targetY;
    ds.acc_x = ds.acc_y = 0.f;
    return EM_TRUE;
}
static EM_BOOL bin_mouseup(int /*type*/, const EmscriptenMouseEvent* e, void* /*ud*/) {
    if (!g_b) return EM_FALSE;
    int b = e->button;
    if (b < 0 || b >= AIML_MAX_MOUSE) return EM_TRUE;
    g_b->curr_mouse[b] = 0;
    auto& ds = g_b->drag[b];
    if (ds.active) ds.ended_this_frame = true;
    ds.held = false; ds.active = false;
    return EM_TRUE;
}
static EM_BOOL bin_wheel(int /*type*/, const EmscriptenWheelEvent* e, void* /*ud*/) {
    if (!g_b) return EM_FALSE;
    g_b->scroll_accum_x += (float)e->deltaX;
    g_b->scroll_accum_y += (float)e->deltaY;
    return EM_TRUE;
}

/* Forward declaration — defined further below in this same WASM section. */
static int32_t name_to_slot(const char* k);

/* Stash a string in the persistent backing store and return a stable ptr. */
static const char* bin_keep_string(const char* s) {
    if (!g_b || !s) return "";
    g_b->key_strings.emplace_back(s);
    return g_b->key_strings.back().c_str();
}

static EM_BOOL bin_keydown(int /*type*/, const EmscriptenKeyboardEvent* e, void* /*ud*/) {
    if (!g_b) return EM_FALSE;
    /* Push a "key_down" event with the key name (e->key — e.g. "a", "Backspace") */
    AimlInputEvent ev = {};
    ev.type = "key_down";
    ev.key  = bin_keep_string(e->key);
    g_b->events.push_back(ev);
    /* Update curr_keys using name_to_slot so that polling
     * input.key_down("up") sees the same slot we wrote here when the user
     * pressed ArrowUp. */
    int slot = name_to_slot(e->key);
    if (slot >= 0) g_b->curr_keys[slot] = 1;
    /* Swallow Backspace/Tab so the browser doesn't navigate or change focus
     * while the user types.  Also swallow Arrow keys / PageUp/PageDown /
     * Space so the page doesn't scroll while the user navigates a graph. */
    if (strcmp(e->key, "Backspace") == 0 || strcmp(e->key, "Tab") == 0 ||
        strcmp(e->key, "ArrowUp")  == 0 || strcmp(e->key, "ArrowDown") == 0 ||
        strcmp(e->key, "ArrowLeft")== 0 || strcmp(e->key, "ArrowRight")== 0 ||
        strcmp(e->key, "PageUp")   == 0 || strcmp(e->key, "PageDown")  == 0 ||
        strcmp(e->key, " ")        == 0)
        return EM_TRUE;
    return EM_FALSE;
}

static EM_BOOL bin_keyup(int /*type*/, const EmscriptenKeyboardEvent* e, void* /*ud*/) {
    if (!g_b) return EM_FALSE;
    AimlInputEvent ev = {};
    ev.type = "key_up";
    ev.key  = bin_keep_string(e->key);
    g_b->events.push_back(ev);
    int slot = name_to_slot(e->key);
    if (slot >= 0) g_b->curr_keys[slot] = 0;
    return EM_FALSE;
}

static EM_BOOL bin_keypress(int /*type*/, const EmscriptenKeyboardEvent* e, void* /*ud*/) {
    if (!g_b) return EM_FALSE;
    /* keypress fires for printable chars. e->key is the typed character (UTF-8
     * sequence in e->key as a string). Use it as the text payload. */
    if (e->key[0] == '\0') return EM_FALSE;
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
    /* Keyboard goes on the window target — canvas elements need explicit
     * tabindex to receive key events, but window always works. */
    emscripten_set_keydown_callback (EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, bin_keydown);
    emscripten_set_keyup_callback   (EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, bin_keyup);
    emscripten_set_keypress_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, bin_keypress);
    /* Register per-frame update with the engine — same hook as desktop. */
    aiml_engine_register_input_update(aiml_input_update);
}

void aiml_input_update(void) {
    if (!g_b) return;
    memcpy(g_b->prev_mouse, g_b->curr_mouse, AIML_MAX_MOUSE);
    memcpy(g_b->prev_keys,  g_b->curr_keys,  AIML_MAX_KEYS);
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
        g_b->drag[b].started_this_frame = false;
        g_b->drag[b].ended_this_frame   = false;
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
    return (g_b->curr_keys[slot] && !g_b->prev_keys[slot]) ? 1 : 0;
}
int32_t aiml_input_key_pressed_once(const char* k) {
    if (!g_b) return 0;
    int slot = name_to_slot(k);
    if (slot < 0) return 0;
    if (!(g_b->curr_keys[slot] && !g_b->prev_keys[slot])) return 0;  /* not a rising edge */
    if (g_b->consumed_press[slot]) return 0;                          /* consumed this frame */
    g_b->consumed_press[slot] = 1;
    return 1;
}
int32_t aiml_input_key_released(const char* k) {
    if (!g_b) return 0;
    int slot = name_to_slot(k);
    if (slot < 0) return 0;
    return (!g_b->curr_keys[slot] && g_b->prev_keys[slot]) ? 1 : 0;
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
    return (g_b->curr_mouse[b] && !g_b->prev_mouse[b]) ? 1 : 0;
}
int32_t aiml_input_mouse_released(int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0;
    return (!g_b->curr_mouse[b] && g_b->prev_mouse[b]) ? 1 : 0;
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
float   aiml_input_drag_origin_x(int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0.f;
    return g_b->drag[b].origin_x;
}
float   aiml_input_drag_origin_y(int32_t b) {
    if (!g_b || b < 0 || b >= AIML_MAX_MOUSE) return 0.f;
    return g_b->drag[b].origin_y;
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

#else /* __ANDROID__ */

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
float   aiml_input_drag_origin_x     (int32_t) { return 0.f; }
float   aiml_input_drag_origin_y     (int32_t) { return 0.f; }
float   aiml_input_drag_delta_x      (int32_t) { return 0.f; }
float   aiml_input_drag_delta_y      (int32_t) { return 0.f; }
void    aiml_input_drag_set_payload  (void*)    {}
void*   aiml_input_drag_get_payload  (void)     { return nullptr; }
void    aiml_input_drag_set_threshold(float)    {}

#endif /* platform stubs */
