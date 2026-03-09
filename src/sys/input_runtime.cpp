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

/* ══════════════════════════════════════════════════════════════════════════ */
/* DESKTOP IMPLEMENTATION                                                     */
/* ══════════════════════════════════════════════════════════════════════════ */

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)

static constexpr int AIML_MAX_KEYS    = 400;    /* GLFW_KEY_LAST + ~50 spare */
static constexpr int AIML_MAX_MOUSE   = 8;
static constexpr int AIML_MAX_GAMEPADS = 4;

/* ── Global input state ─────────────────────────────────────────────────── */

struct AimlInputState {
    GLFWwindow* window;

    /* Keyboard (updated by GLFW callbacks + glfwGetKey in update) */
    uint8_t curr_keys[AIML_MAX_KEYS];
    uint8_t prev_keys[AIML_MAX_KEYS];

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
};

static AimlInputState* g_input = nullptr;

/* ── Key name → GLFW code ───────────────────────────────────────────────── */

static int key_name_to_glfw(const char* name) {
    if (!name || !*name) return -1;
    /* Single letters A-Z */
    if (name[1] == '\0' && name[0] >= 'A' && name[0] <= 'Z')
        return GLFW_KEY_A + (name[0] - 'A');
    /* Digits 0-9 */
    if (name[1] == '\0' && name[0] >= '0' && name[0] <= '9')
        return GLFW_KEY_0 + (name[0] - '0');

    /* Named keys */
    struct KV { const char* n; int k; };
    static const KV table[] = {
        {"Space",       GLFW_KEY_SPACE},
        {"Escape",      GLFW_KEY_ESCAPE},
        {"Enter",       GLFW_KEY_ENTER},
        {"Tab",         GLFW_KEY_TAB},
        {"Backspace",   GLFW_KEY_BACKSPACE},
        {"Delete",      GLFW_KEY_DELETE},
        {"Insert",      GLFW_KEY_INSERT},
        {"LShift",      GLFW_KEY_LEFT_SHIFT},
        {"RShift",      GLFW_KEY_RIGHT_SHIFT},
        {"Shift",       GLFW_KEY_LEFT_SHIFT},   /* alias */
        {"LCtrl",       GLFW_KEY_LEFT_CONTROL},
        {"RCtrl",       GLFW_KEY_RIGHT_CONTROL},
        {"Ctrl",        GLFW_KEY_LEFT_CONTROL},
        {"LAlt",        GLFW_KEY_LEFT_ALT},
        {"RAlt",        GLFW_KEY_RIGHT_ALT},
        {"Alt",         GLFW_KEY_LEFT_ALT},
        {"Up",          GLFW_KEY_UP},
        {"Down",        GLFW_KEY_DOWN},
        {"Left",        GLFW_KEY_LEFT},
        {"Right",       GLFW_KEY_RIGHT},
        {"Home",        GLFW_KEY_HOME},
        {"End",         GLFW_KEY_END},
        {"PageUp",      GLFW_KEY_PAGE_UP},
        {"PageDown",    GLFW_KEY_PAGE_DOWN},
        {"F1",          GLFW_KEY_F1},
        {"F2",          GLFW_KEY_F2},
        {"F3",          GLFW_KEY_F3},
        {"F4",          GLFW_KEY_F4},
        {"F5",          GLFW_KEY_F5},
        {"F6",          GLFW_KEY_F6},
        {"F7",          GLFW_KEY_F7},
        {"F8",          GLFW_KEY_F8},
        {"F9",          GLFW_KEY_F9},
        {"F10",         GLFW_KEY_F10},
        {"F11",         GLFW_KEY_F11},
        {"F12",         GLFW_KEY_F12},
        {"Num0",        GLFW_KEY_KP_0},
        {"Num1",        GLFW_KEY_KP_1},
        {"Num2",        GLFW_KEY_KP_2},
        {"Num3",        GLFW_KEY_KP_3},
        {"Num4",        GLFW_KEY_KP_4},
        {"Num5",        GLFW_KEY_KP_5},
        {"Num6",        GLFW_KEY_KP_6},
        {"Num7",        GLFW_KEY_KP_7},
        {"Num8",        GLFW_KEY_KP_8},
        {"Num9",        GLFW_KEY_KP_9},
        {"NumAdd",      GLFW_KEY_KP_ADD},
        {"NumSub",      GLFW_KEY_KP_SUBTRACT},
        {"NumMul",      GLFW_KEY_KP_MULTIPLY},
        {"NumDiv",      GLFW_KEY_KP_DIVIDE},
        {"NumEnter",    GLFW_KEY_KP_ENTER},
        {"NumDecimal",  GLFW_KEY_KP_DECIMAL},
        {"CapsLock",    GLFW_KEY_CAPS_LOCK},
        {"PrintScreen", GLFW_KEY_PRINT_SCREEN},
        {"Pause",       GLFW_KEY_PAUSE},
        {"Menu",        GLFW_KEY_MENU},
        {nullptr, -1}
    };
    for (int i = 0; table[i].n; i++)
        if (strcmp(name, table[i].n) == 0) return table[i].k;
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

/* ══════════════════════════════════════════════════════════════════════════ */
/* STUB IMPLEMENTATIONS (Emscripten + Android)                               */
/* ══════════════════════════════════════════════════════════════════════════ */

#else /* __EMSCRIPTEN__ || __ANDROID__ */

void    aiml_input_init       (void* /*win*/)   { /* TODO: platform */ }
void    aiml_input_update     (void)            { /* TODO: platform */ }
void    aiml_input_set_capture(int32_t /*on*/)  { /* TODO: platform */ }

int32_t aiml_input_key_down    (const char*) { return 0; }
int32_t aiml_input_key_pressed (const char*) { return 0; }
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

#endif /* platform stubs */
