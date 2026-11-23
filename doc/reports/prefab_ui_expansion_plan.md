# Plan: prefab system UI expansion

## Goal

Define UI widgets as reusable prefabs instead of building every frame
imperatively via ui_runtime. The DataCreatures login panel becomes
the first real application example.

## Design decisions (confirmed by the user)

| Aspect | Decision |
|--------|-------------|
| ui_runtime | stays in parallel — the new prefab UI is additive |
| Event model | Callbacks: `on_click: fn_name` |
| Callback signature | `args: [[p, prefab_instance]]` — instance + getter methods |
| Layout | absolute x/y/w/h against the prefab design size, instance scaling via `is_relative` |
| MVP widgets | button, text_input, label, rect/panel |
| Migration | DataCreatures `client_main.aiml` directly |

## Architektur

### Current state
- `prefab_runtime` has `CHILD_SPRITE` and `CHILD_TEXT`. Children x/y/w/h
  are absolute against `prefab.width/height`.
- `Instance.is_relative` scales the whole instance (all children
  scale along) → suitable for UI that should react to different
  screen sizes.
- `ui_runtime` has finished widget implementations (button, text_input,
  hit test, focus state, SDF rounded rects, font rendering).
- The AIML compiler can use function-by-name as a callback (see
  `engine.run(win, update, render)`).

### New structure

```
prefab_runtime.cpp
├── ChildDef { type, x,y,w,h, ... type-specific ... , on_click_fn,on_change_fn }
├── Instance { existing fields + widget_states: vector<WidgetState> }
├── WidgetState { union: text{buf, cursor}, button{hot, pressed_now} }
├── render_instance() — dispatched per ChildType:
│   ├── SPRITE/TEXT — as today
│   ├── RECT — draw_widget_rect (re-use from ui_runtime, via shared helper)
│   ├── BUTTON — draw_widget_rect + label, writes widget_state, checks hit
│   ├── TEXT_INPUT — draw_widget_rect + text + cursor
│   └── LABEL — push_text (re-use)
└── invoke_callback(inst, child_idx) — looks at ChildDef.on_click_fn,
    looks up FunctionInfo, calls it with (inst*)
```

## Implementation Steps

### Step 1: ChildType enum + parser
**File**: `src/sys/prefab_runtime.cpp`

```cpp
enum PrefabChildType {
    CHILD_SPRITE,
    CHILD_TEXT,
    CHILD_RECT,        // NEW
    CHILD_BUTTON,      // NEW
    CHILD_TEXT_INPUT,  // NEW
    CHILD_LABEL,       // NEW (alias of CHILD_TEXT with a different style group)
};

struct PrefabChildDef {
    // ... existing fields ...
    // Style:
    int32_t   bg_color       = 0;
    int32_t   border_color   = 0;
    float     border_width   = 0.0f;
    float     corner_radius  = 0.0f;
    int32_t   label_color    = 0xFFFFFFFF;
    std::string label_text;   // for button: text to display
    bool      is_secret       = false; // password mode for text_input
    // Callbacks:
    std::string on_click_fn;  // function name (resolved at render time)
    std::string on_change_fn;
};
```

Extend `parse_children_array` with the new keys.

### Step 2: WidgetState per instance
**File**: `src/sys/prefab_runtime.cpp`

```cpp
struct PrefabWidgetState {
    // Button fields:
    bool hot = false;
    bool pressed_now = false;
    bool clicked_this_frame = false;

    // Text input fields:
    std::string text;
    int cursor_pos = 0;
    bool focused = false;
};

struct AimlPrefabInstance {
    // ... existing ...
    std::vector<PrefabWidgetState> widget_states;  // parallel to def->children
};
```

In `aiml_engine_instantiate`, size widget_states to children.size().

### Step 3: drawing dispatch
**File**: `src/sys/prefab_runtime.cpp::render_instance`

Extend the existing dispatch:

```cpp
case CHILD_RECT: {
    // If texture: textured quad with alpha blending
    // Otherwise: draw_widget_rect(x, y, w, h, bg_color, border_color, border_width, corner_radius)
}
case CHILD_BUTTON: {
    auto& ws = inst->widget_states[idx];
    int32_t bg = ws.pressed_now ? ch.active_color
              : ws.hot         ? ch.hover_color
              :                  ch.bg_color;
    draw_widget_rect(ax, ay, aw, ah, bg, ch.border_color, ch.border_width, ch.corner_radius);
    if (!ch.label_text.empty())
        push_text_centered(font, ch.label_text.c_str(), ax, ay, aw, ah, ch.label_color);
}
case CHILD_TEXT_INPUT: {
    auto& ws = inst->widget_states[idx];
    int32_t bg = ws.focused ? ch.active_color : ch.bg_color;
    draw_widget_rect(ax, ay, aw, ah, bg, ch.border_color, ch.border_width, ch.corner_radius);
    const std::string& disp = ch.is_secret ? std::string(ws.text.size(), '*') : ws.text;
    push_text_left(font, disp.c_str(), ax + padding, ay, aw - 2*padding, ch.label_color);
}
case CHILD_LABEL: {
    push_text_centered(font, ch.label_text.c_str(), ax, ay, aw, ah, ch.label_color);
}
```

If needed, extract the helper functions (`draw_widget_rect`, `push_text_centered`) from
ui_runtime into a shared header `gfx_primitives.h`.

### Step 4: input + callbacks
**File**: `src/sys/prefab_runtime.cpp::aiml_engine_update_hover` or a new function

```cpp
void aiml_prefab_update_input(float mx, float my, int mouse_pressed, int mouse_released) {
    for (auto* inst : g_scene->instances) {
        for (size_t i = 0; i < inst->def->children.size(); i++) {
            auto& ch = inst->def->children[i];
            if (ch.type != CHILD_BUTTON && ch.type != CHILD_TEXT_INPUT) continue;

            // bbox in screen space:
            float ax = inst_x + ch.x * inst_sx;
            // ... etc, scale-aware ...

            bool hit = (mx >= ax && mx < ax+aw && my >= ay && my < ay+ah);
            auto& ws = inst->widget_states[i];
            ws.hot = hit;

            if (ch.type == CHILD_BUTTON) {
                if (hit && mouse_pressed) ws.pressed_now = true;
                if (mouse_released) {
                    if (ws.pressed_now && hit) {
                        ws.clicked_this_frame = true;
                        if (!ch.on_click_fn.empty())
                            invoke_aiml_callback(ch.on_click_fn, inst);
                    }
                    ws.pressed_now = false;
                }
            } else if (ch.type == CHILD_TEXT_INPUT) {
                if (hit && mouse_pressed) {
                    set_focused_input(inst, i);  // global focus
                }
            }
        }
    }

    // Forward text events to focused_input (backspace, char input)
    apply_text_events_to_focused();
}

void invoke_aiml_callback(const std::string& fn_name, AimlPrefabInstance* inst) {
    auto it = g_fn_table.find(fn_name);  // <- lives in fptable.cpp
    if (it == g_fn_table.end()) return;
    using cb_t = void(*)(void*);  // function takes instance ptr
    ((cb_t)it->second)(inst);
}
```

Function table: `g_fn_table` is `__aiml_fptable_current[]` today. We
need a name→slot lookup. schema_registry probably already has it
(check `src/core/schema_registry.h/cpp`).

### Step 5: AIML compiler callback resolution
**File**: `src/core/ast_compiler.cpp::visitTopLevel` (prefab decl)

When parsing `on_click: fn_name`: add an entry to the function symbol table
that marks the function as a `prefab_callback`
(so that it is not garbage-collected away). Pass the string unmodified
to `aiml_prefab_register_child`.

### Step 6: prefab_instance property accessors
**File**: `src/core/builtin_registry.cpp`

```cpp
ci.builtinMethods["__getattr__"] = [](ctx, base, call, eval) -> Value* {
    // base = prefab_instance, call.member_name = e.g. "email_in"
    // Returns a small handle value (i8*) that encodes the child name
    // Compile time: emit a call to aiml_prefab_child_handle(inst, name)
};
```

Then per WidgetHandle: `.value()` → `aiml_prefab_child_text(handle)`,
`.clicked()` → `aiml_prefab_child_clicked(handle)`.

Simpler: direct methods on prefab_instance, name as a string:

```aiml
- decl: [email, string, { call: [p.text_value, "email_in"] }]
- if:
    condition: { call: [p.was_clicked, "submit"] }
```

Makes the compiler effort much smaller — we register only 2-3 methods on `prefab_instance`, not one per child name.

### Step 7: DataCreatures migration
**File**: `sources/client/client_main.aiml`

Replace `render_login` with a prefab definition + instantiation:

```aiml
- def:
    name: handle_submit
    args: [[p, prefab_instance]]
    body:
      - decl: [email, string, { call: [p.text_value, "email_in"] }]
      - decl: [pass,  string, { call: [p.text_value, "pass_in"]  }]
      - call: [log.info, "submit: ${email}"]

- def:
    name: handle_toggle
    args: [[p, prefab_instance]]
    body:
      - if:
          condition: { eq: [g_mode, 0] }
          then:
            - set: [g_mode, 1]
          else:
            - set: [g_mode, 0]

- prefab:
    name: LoginPanel
    width: 524
    height: 584
    children:
      - { name: frame, type: rect, x: 0, y: 0, width: 524, height: 584,
          texture: "ui/ui_surround.png" }
      - { name: email_in, type: text_input,
          x: 100, y: 200, width: 324, height: 32,
          bg_color: 0x0A1830E6, border_color: 0x66B0E0FF, border_width: 1.5,
          corner_radius: 5.0, label_color: 0x0A1A40FF }
      - { name: pass_in, type: text_input, secret: true,
          x: 100, y: 280, width: 324, height: 32,
          bg_color: 0x0A1830E6, border_color: 0x66B0E0FF, border_width: 1.5,
          corner_radius: 5.0, label_color: 0x0A1A40FF }
      - { name: submit, type: button,
          x: 100, y: 360, width: 324, height: 40,
          bg_color: 0x2A6FB5FF, hover_color: 0x4A8FE0FF, active_color: 0x6AAFFAFF,
          corner_radius: 8.0, label: "Sign In", label_color: 0xFFFFFFFF,
          on_click: handle_submit }
      - { name: toggle, type: button,
          x: 100, y: 410, width: 324, height: 28,
          bg_color: 0x0A1830A0, hover_color: 0x1A3060C0, active_color: 0x2A4080D0,
          corner_radius: 4.0, label: "Register", label_color: 0xFFFFFFFF,
          on_click: handle_toggle }

# In setup:
- decl: [panel, prefab_instance, { call: [engine.instantiate, "LoginPanel"] }]
- call: [panel.set_relative_pos, 0.5, 0.7]   # bottom centre, scale-aware
```

`dc_render` then becomes:

```aiml
- def:
    name: dc_render
    args: []
    body:
      - call: [gl.clear, 0.02, 0.02, 0.04, 1.0]
      - call: [engine.draw_scene]   # draws bg + panel
```

**`render_login` is dropped completely.**

## Tests

**`tests/test_prefab_ui.aiml`** (~100 LOC):
- Phase A: a button click triggers the callback → a global counter is incremented
- Phase B: the text input takes key events, `text_value` returns the entered text
- Phase C: the label renders (visual, skipped headless)
- Phase D: rect with texture + bg_color (visual, skipped headless)

`# Requires: display` for the visual phases; phases A/B can run without a display by firing mouse/keyboard programmatically via test helpers.

## Risks & open questions

1. **Font sharing**: ui_runtime has `ui.set_default_font`. Prefab uses
   `engine.prefab_set_font`. Today these are two separate fonts. We want
   to draw the login panel with ui.ttf → check whether both paths
   can share the same font.

2. **SDF rendering from prefab_runtime**: prefab_runtime has its own
   shader (no SDF). We want rounded-rect borders. Options:
   - extend prefab_shader with an SDF path (like ui_runtime),
   - or emit the UI quads via ui_runtime's draw_list (wrap ui.begin/ui.end around the prefab draw).
   → the latter is cleaner.

3. **Scaling**: with `is_relative`, children are scaled by `inst_sx/sy`.
   UI widgets, however, mostly want a CONSTANT pixel size for
   text (otherwise it becomes huge on large screens). Optionally a per-child
   `no_scale: true` flag — but only in iteration 2.

4. **Hot reload**: prefab definitions are hardcoded at compile time.
   Hot reload must detect new ChildDefs → see the fptable/schema_registry
   hot reload mechanism.

## Size

| Phase | Files | LOC |
|-------|-------|-----|
| 1: ChildType+Parser | prefab_runtime.h/cpp | +60 |
| 2: WidgetState | prefab_runtime.cpp | +50 |
| 3: Drawing | prefab_runtime.cpp (+ gfx_primitives.h?) | +120 |
| 4: Input+Callbacks | prefab_runtime.cpp | +100 |
| 5: Compiler side | ast_compiler.cpp (prefab decl) | +30 |
| 6: Property API | builtin_registry.cpp | +50 |
| 7: Tests | tests/test_prefab_ui.aiml | +100 |
| 8: Migration | datacreatures client_main.aiml | net ~-50 (removes more than it adds) |

**Total: ~500 LOC**, half a working day with test+debug. Native tests
stay green (everything additive, ui_runtime unchanged).

## Implementation order

1. Steps 1+2 (parser + state) without drawing/input → the build must be green
2. Step 3 (drawing) → minimal test_prefab_ui phase D (rect+label)
3. Step 4 (input/callback) → test phase A (button click)
4. Step 6 (property API) → test phase B (text_value)
5. Step 7 (migration) → DataCreatures must be visually+functionally unchanged

Each step is a single build/test cycle.
