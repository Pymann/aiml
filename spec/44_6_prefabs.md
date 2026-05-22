# Chapter 44.6 — Prefab System (Scene Graph + Sprite Rendering)

> **Core idea:** Prefabs are reusable visual entity templates defined in YAML.
> The compiler registers them at startup; the runtime creates instances and
> renders all of them in sorted Z-order.

---

## 1. Prefab Definition (YAML)

```aiml
- prefab:
    name: CardFace
    width: 200
    height: 280
    texture: "assets/frame.png"
    draggable: true
    children:
      - { name: title, type: label, x: 100, y: 250, anchor: center, font_size: 11, color: 0xFFFFFF }
      - { name: art,   type: sprite, x: 15, y: 100, width: 170, height: 120 }
      - { name: bg,    type: rect, x: 0, y: 0, width: 200, height: 280, color: 0x1A1A2E }
```

### Child Types

| Type | Description |
|------|-------------|
| `sprite` | Textured quad (requires `width`, `height`) |
| `label` | Text line (requires `font_size`, `color`; optional `anchor`, `label`) |
| `rect` | Rectangle. Solid color (`color`), rounded with border (`bg_color` + `corner_radius` + `border_color`/`border_width`), or textured (`texture`). |
| `button` | Interactive clickable widget. Hover/active states; callback on click. |
| `text_input` | Editable single-line text field. Keyboard focus, cursor, `secret` mode. |
| `checkbox` | Toggleable box + label. State stored on the child; click flips. Use `width × height` to size the surrounding region; the box itself is `min(width, height)` square on the left, label fills the remaining width. |

### Child Style Fields

All UI-widget children (`rect`, `button`, `text_input`) accept the following style fields:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `bg_color` | `0xRRGGBBAA` | `0` | Fill color (with alpha) |
| `border_color` | `0xRRGGBBAA` | `0` | Border color |
| `border_width` | float | `0.0` | Border thickness in pixels |
| `corner_radius` | float | `0.0` | Rounded-corner radius in pixels |
| `hover_color` | `0xRRGGBBAA` | `0` | (button) fill when mouse hovers |
| `active_color` | `0xRRGGBBAA` | `0` | (button) fill while pressed; (text_input) fill when focused |
| `color` | `0xRRGGBBAA` | `0xFFFFFFFF` | Text color (label, button caption, text_input text) |
| `label` | string | `""` | Static text. For `label`: displayed text. For `button`: initial caption (can be re-labeled at runtime via `set_text`). For `text_input`: initial value. |
| `texture` | string (path) | `""` | (button) Optional image. When set, the button renders as a textured sprite — `bg_color` then acts as a tint (`0xFFFFFFFF` = untinted), and `hover_color`/`active_color` provide the interaction tint. `border_color` + `border_width` still draw a frame around the icon. |
| `font_size` | float | `12` | Font size in pixels (auto-scaled with instance scale) |
| `secret` | bool | `false` | (text_input) Render chars as `*` (password mode). |
| `on_click` | function name | `""` | (button) Callback fired when the button is clicked. |
| `on_change` | function name | `""` | (text_input) Callback fired on every keystroke + on Enter. |
| `material` | string | `""` | Name of a registered material (see §5a). Replaces the default fragment shader for this widget. |

### Example: Login Panel

```aiml
- prefab:
    name: LoginPanel
    width: 524
    height: 584
    children:
      - { name: frame, type: rect,
          x: 0, y: 0, width: 524, height: 584,
          texture: "ui/login_frame.png" }
      - { name: email_in, type: text_input,
          x: 100, y: 210, width: 324, height: 38,
          bg_color: 0x0A1830E6, border_color: 0x66B0E0FF,
          border_width: 1.5, corner_radius: 5.0 }
      - { name: pass_in,  type: text_input, secret: true,
          x: 100, y: 300, width: 324, height: 38,
          bg_color: 0x0A1830E6, border_color: 0x66B0E0FF,
          border_width: 1.5, corner_radius: 5.0 }
      - { name: submit,   type: button,
          x: 120, y: 380, width: 284, height: 46,
          bg_color: 0x2A6FB5FF, hover_color: 0x4A8FE0FF, active_color: 0x6AAFFAFF,
          corner_radius: 8.0,
          label: "Sign In", color: 0xFFFFFFFF,
          on_click: handle_submit }
```

---

## 2. Type: `prefab_instance`

Opaque handle (`i8*`) to a live prefab instance.

```aiml
- decl: [card, prefab_instance, { call: [engine.instantiate, "CardFace"] }]
```

---

## 3. Scene Lifecycle

| Call | Description |
|------|-------------|
| `engine.prefab_scene_init(w: i32, h: i32)` | Initialize the scene (shader, quad VAO, projection). Call after GL context + `ui.init()`. |
| `engine.prefab_set_font(font: ui_font)` | Set font for label children. |
| `engine.instantiate(name: string) -> prefab_instance` | Create a new instance of a registered prefab. |
| `engine.draw_scene()` | Render all visible instances in Z-order. Call after `gl.clear_all()`, before `engine.swap()`. |

---

## 4. Transform Methods (Instance)

| Method | Signature | Description |
|--------|-----------|-------------|
| `.set_pos(x, y)` | `(f32, f32) -> void` | Set position |
| `.get_x()` / `.get_y()` | `-> f32` | Query position |
| `.set_scale(s)` | `(f32) -> void` | Uniform scale (sets `scale_x = scale_y = s`) |
| `.get_scale()` | `-> f32` | Returns `scale_x` |
| `.set_rotation(r)` | `(f32) -> void` | Rotation in radians |
| `.get_rotation()` | `-> f32` | Query rotation |
| `.set_z(z)` | `(i32) -> void` | Z-order (higher = further front) |
| `.get_z()` | `-> i32` | Query Z-order |
| `.set_visible(v)` | `(i32) -> void` | Visibility (0 = invisible) |
| `.get_visible()` | `-> i32` | Query visibility |
| `.set_alpha(a)` | `(f32) -> void` | Transparency (0.0–1.0) |
| `.get_alpha()` | `-> f32` | Query transparency |
| `.set_draggable(d)` | `(i32) -> void` | Enable/disable drag-and-drop |
| `.get_width()` / `.get_height()` | `-> f32` | Definition size of the prefab |

---

## 5. Child Property Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `.set_text(child, text)` | `(string, string) -> void` | Set text of a label / button caption / text_input value |
| `.set_texture(child, tex)` | `(string, gl_texture) -> void` | Set texture of a sprite or textured rect child |
| `.set_color(child, color)` | `(string, i32) -> void` | Set color of a child (label text color, rect fill color) |
| `.set_child_visible(child, v)` | `(string, i32) -> void` | Visibility of a child |
| `.child(child_name)` | `(string) -> prefab_instance` | Child as its own instance for direct manipulation |
| `.text_value(child)` | `(string) -> string` | (text_input) Current text of an input field |
| `.child_clicked(child)` | `(string) -> i32` | (button) `1` if button was clicked this frame, else `0` |
| `.child_focused(child)` | `(string) -> i32` | (text_input) `1` if input has keyboard focus, else `0` |
| `.is_checked(child)` | `(string) -> i32` | (checkbox) `1` if box is currently checked, else `0` |
| `.set_checked(child, v)` | `(string, i32) -> void` | (checkbox) Programmatically set state (no callbacks fired) |

---

## 5a. Materials

A **material** bundles a fragment shader (loaded from disk) with a set of
uniform values, and can be attached to `rect`/`button`/`text_input` children
via the `material:` key on the child.

### Material Definition

```aiml
- material:
    name: CrystalBlue
    shader: "resources/shaders/crystal.frag"
    uniforms:
      u_intensity:    0.6                     # → float
      u_tint:         [0.4, 0.7, 1.0, 1.0]    # → vec4
      u_facet_freq:   [28.0, 22.0]            # → vec2
      u_layer_count:  { int: 3 }              # → int
      u_noise:        { tex: "noise.png" }    # → sampler2D

- prefab:
    name: LoginPanel
    children:
      - { name: submit, type: button, material: CrystalBlue, ... }
```

Materials live in a global registry, looked up by name when a child is
instantiated. The same material can be referenced by many children across
many prefabs, and is loaded/compiled exactly once (shader program cache).

Materials can be declared in the same AIML file as the prefab, or in an
imported file (`- import: "..."`) — the registration emission happens at
program-init time regardless of file boundaries.

### Uniform Type Detection

| YAML value form         | GLSL type   |
|-------------------------|-------------|
| `0.6`                   | `float`     |
| `[a, b]`                | `vec2`      |
| `[a, b, c]`             | `vec3`      |
| `[a, b, c, d]`          | `vec4`      |
| `{ int: 42 }`           | `int`       |
| `{ tex: "path/to.png" }`| `sampler2D` |

### Built-in Uniforms (always set, no need to declare)

| Uniform     | Type       | Source                                      |
|-------------|------------|---------------------------------------------|
| `u_proj`    | `mat4`     | Orthographic projection of the prefab scene |
| `u_tex`     | `sampler2D`| The shared white-texture (slot 0)           |
| `u_mode`    | `int`      | Forwarded from `DrawBatch.mode` (see below) |
| `u_time`    | `float`    | Seconds since first `draw_scene` call       |

### Built-in Varyings (provided by the shared vertex shader)

| Varying    | Type   | Description                                |
|------------|--------|--------------------------------------------|
| `v_uv`     | `vec2` | `[0,1]` UV across the quad                 |
| `v_color`  | `vec4` | Fill RGBA (from `bg_color`)                |
| `v_quad`   | `vec2` | Quad pixel dimensions (SDF mode only)      |
| `v_style`  | `vec2` | `(corner_radius, border_width)` in px      |
| `v_bcolor` | `vec4` | Border RGBA (from `border_color`)          |

### Minimal Fragment Shader (no header — runtime prepends one)

```glsl
// my_shader.frag — note: NO #version directive (runtime adds it).

in vec2 v_uv;
in vec4 v_color;
in vec2 v_quad;
in vec2 v_style;
in vec4 v_bcolor;

uniform sampler2D u_tex;
uniform int   u_mode;
uniform float u_time;

uniform float u_intensity;   // material uniform — declared in YAML

out vec4 out_color;

void main() {
    out_color = vec4(v_color.rgb * u_intensity, v_color.a);
}
```

The runtime prepends `#version 300 es\nprecision highp float;\n` (WebGL2)
or `#version 330 core\n` (desktop GL) to your shader file, so you write
portable GLSL bodies.

---

## 5a. Materials (custom shaders)

UI-widget children (`rect`, `button`, `text_input`) can attach a **material**
that swaps in a custom fragment shader plus a set of uniform values. Materials
are declared at the top level of an AIML file (inline or imported) and
referenced from children by name.

### Material declaration

```aiml
- material:
    name: CrystalPrimary
    shader: "resources/shaders/crystal_vein.frag"
    uniforms:
      u_tint:         [0.55, 0.85, 1.00, 1.0]
      u_vein_density: 10.0
      u_vein_glow:    1.10
      u_pulse_speed:  0.55
      u_count:        { int: 42 }
      u_noise:        { tex: "resources/textures/noise.png" }
```

### Uniform-Typ Detection

| YAML | GLSL | Beispiel |
|------|------|---------|
| `0.6` | `float` | `u_intensity: 0.6` |
| `[a, b]` | `vec2` | `u_facet_freq: [28.0, 22.0]` |
| `[a, b, c]` | `vec3` | `u_color: [1.0, 0.5, 0.2]` |
| `[a, b, c, d]` | `vec4` | `u_tint: [0.4, 0.7, 1.0, 1.0]` |
| `{ int: N }` | `int` | `u_count: { int: 42 }` |
| `{ tex: "path" }` | `sampler2D` | `u_noise: { tex: "noise.png" }` |

### Built-in Uniforms (immer verfügbar — keine Decl im Material nötig)

| Uniform | GLSL | Beschreibung |
|---------|------|-------------|
| `u_proj` | `mat4` | Orthographic projection matrix |
| `u_tex` | `sampler2D` | White-Tex slot (slot 0) |
| `u_mode` | `int` | Render mode passed from DrawBatch |
| `u_time` | `float` | Sekunden seit erstem Frame |

### Built-in Varyings (vom geteilten Vertex-Shader)

| Varying | GLSL | Beschreibung |
|---------|------|-------------|
| `v_uv` | `vec2` | UV [0,1] über den Quad |
| `v_color` | `vec4` | Fill-Farbe vom Widget (bg/hover/active) |
| `v_quad` | `vec2` | (width, height) in Pixeln |
| `v_style` | `vec2` | (corner_radius, border_width) |
| `v_bcolor` | `vec4` | Border-Farbe |

### Shader-Datei-Format

User-Shader liefern nur `void main()` + eigene `uniform`-Decls. Das Runtime
prepends automatisch:
- `#version 300 es / 330 core` (plattformabhängig)
- `precision highp float;` (GLES)
- alle Built-in `in`/`uniform`/`out` Decls

Beispiel `crystal_vein.frag`:

```glsl
uniform vec4  u_tint;
uniform float u_vein_density;
uniform float u_vein_glow;

void main() {
    // v_uv, v_color, v_quad usw. sind bereits in-scope
    float pulse = 0.5 + 0.5 * sin(u_time * u_vein_density);
    out_color = vec4(v_color.rgb + u_tint.rgb * (u_vein_glow * pulse), v_color.a);
}
```

### Referenz aus einem Prefab-Child

```aiml
- prefab:
    name: LoginPanel
    children:
      - { name: submit, type: button,
          x: 120, y: 320, width: 284, height: 42,
          bg_color: 0x2A6FB5FF,
          material: CrystalPrimary,
          on_click: handle_submit }
```

Materialien werden beim Instantiate per Name aufgelöst. Bei `null` (unbekannter
Name) fällt das Widget still auf den default-Shader zurück.

---

## 5b. Callbacks

Buttons and text_inputs can declare a function name as `on_click` / `on_change`.
The function is called when the event fires, with the prefab instance as its
sole argument. Inside the callback the property-accessor methods (`text_value`,
`child_clicked`, etc.) are used to read the current widget state.

### Signature

```aiml
- def:
    name: handle_submit
    args: [[p, prefab_instance]]
    body:
      - decl: [email, string, { call: [p.text_value, "email_in"] }]
      - decl: [pass,  string, { call: [p.text_value, "pass_in"]  }]
      - call: [log.info, "submit email='${email}'"]
```

### Resolution

The AIML compiler scans each prefab's `on_click` / `on_change` keys and emits,
right after the prefab-register call, one `aiml_prefab_register_callback(name,
fn_ptr)` per unique callback name. The function is forward-declared with
signature `void(i8*)`, so the order of `prefab:` and `def:` blocks in the
source file does not matter.

### Keyboard handling inside text_input

While a `text_input` child has focus:

- Printable characters from `input.next_event()` (`text` events) are appended.
- `Backspace` deletes the last character.
- `Enter` triggers the `on_change` callback (treat as "submit").
- `Escape` clears focus.

Mouse-click anywhere else moves focus to whatever input is under the cursor
(or clears focus if none).

---

## 6. Dock System (Grid-Snapping)

`dock_x` and `dock_y` bind a prefab instance to cells of the currently active
UI grid. They set position and scale **independently per axis**, so that the
prefab exactly fills the cell(s).

**Prerequisite:** A `ui.begin_grid` must be active when `dock_x`/`dock_y`
is called.

| Method | Signature | Description |
|--------|-----------|-------------|
| `.dock_x(col, colspan)` | `(i32, i32) -> void` | X position = cell X; `scale_x = cell_width / prefab_def_width` |
| `.dock_y(row, rowspan)` | `(i32, i32) -> void` | Y position = cell Y; `scale_y = cell_height / prefab_def_height` |

### Example: Docking a Prefab to a Grid Cell

```aiml
# Define grid (8 columns, 4 rows)
- decl: [cols, buffer<f32>, [0.08, 0.148, 0.148, 0.148, 0.148, 0.148, 0.06, 0.12]]
- decl: [rows, buffer<f32>, [0.35, 0.22, 0.22, 0.21]]
- call: [ui.begin_grid, cols, rows]

# Dock combat zone 1 to column 1, row 0
- call: [zone_combat_1.dock_x, 1, 1]
- call: [zone_combat_1.dock_y, 0, 1]

# Dock trap zone spanning 5 columns (1-5), row 1
- call: [zone_trap.dock_x, 1, 5]
- call: [zone_trap.dock_y, 1, 1]

# Graveyard in column 6, spanning all 4 rows
- call: [graveyard.dock_x, 6, 1]
- call: [graveyard.dock_y, 0, 4]
```

### Behavior

- `dock_x` reads the current grid cell via `aiml_ui_grid_cell_rect(col, 0, colspan, 1)`,
  sets `inst.x = cell_x` and `inst.scale_x = cell_width / def.width`.
- `dock_y` reads via `aiml_ui_grid_cell_rect(0, row, 1, rowspan)`,
  sets `inst.y = cell_y` and `inst.scale_y = cell_height / def.height`.
- X and Y are **independently** controllable — a prefab can, for example, be bound
  horizontally to a grid column via `dock_x`, but freely positioned vertically via `set_pos`.
- Scale is computed separately per axis (`scale_x`, `scale_y`), so that
  non-square cells are covered correctly.

---

## 7. Hit-Testing, Hover + Drag-and-Drop

### Hit-Testing

| Call | Description |
|------|-------------|
| `engine.hit_test(mx, my) -> prefab_instance` | Topmost visible instance at screen coordinate (or `null`) |

Hit-test iterates in reverse Z-order (top-to-bottom).

### Hover Tracking

| Call | Description |
|------|-------------|
| `engine.update_hover(mx, my)` | Update hover state. Call once per frame. Runs `hit_test` and sets the `hovered` flag on the hit instance. |
| `engine.get_hovered() -> prefab_instance` | Currently hovered instance (or `null`) |

| Instance Method | Signature | Description |
|-----------------|-----------|-------------|
| `.is_hovered()` | `-> i32` | 1 if mouse is over this instance, 0 otherwise |
| `.was_clicked(btn)` | `(i32) -> i32` | 1 if this instance was clicked this frame (= hovered + mouse button pressed). `btn`: 0=left, 1=right, 2=middle |

**Important:** `update_hover` must be called before any `was_clicked` queries,
as it sets the hover state and mouse button snapshot for the current frame.

### Example: Hover Preview + Deck Click

```aiml
# In render loop, after update_drag:
- call: [engine.update_hover, mx, my]

# Check deck click
- if:
    condition: { "!=": [{ call: [deck_wesen.was_clicked, 0] }, 0] }
    then:
      - call: [log.info, "Card drawn from creature deck"]

# Show hover preview
- decl: [hov, prefab_instance, { call: [engine.get_hovered] }]
- if:
    condition: { "!=": [hov, 0] }
    then:
      - call: [preview_card.set_visible, 1]
  else:
    - call: [preview_card.set_visible, 0]
```

### Drag-and-Drop

| Call | Description |
|------|-------------|
| `engine.update_drag(mx, my, btn_down, btn_released)` | Update drag state. Call once per frame. |
| `engine.get_dragged() -> prefab_instance` | Currently dragged instance (or `null`) |

Only instances with `draggable = true` and `visible = 1` are considered for dragging.

---

## 8. Rendering

```aiml
- call: [gl.clear_all, 0.05, 0.05, 0.1, 1.0]

# UI as background layer
- call: [ui.begin, win]
# ... grid layout ...
- call: [ui.end]

# Prefabs ON TOP of UI
- call: [engine.draw_scene]

- call: [engine.swap, win]
```

`engine.draw_scene()` renders all instances sorted by Z-order (low to high).
Sprites and base textures are drawn as textured quads,
labels as text, rects as solid-color quads.

---

## 9. C API (Runtime)

```c
void  aiml_prefab_scene_init(int32_t screen_w, int32_t screen_h);
void  aiml_prefab_register(const char* name, float width, float height,
                           const char* texture_path, int32_t draggable,
                           const char* children_json);
void* aiml_prefab_instantiate(const char* prefab_name);
void  aiml_prefab_destroy(void* inst);

// Transform
void  aiml_prefab_set_pos(void* inst, float x, float y);
float aiml_prefab_get_x(void* inst);
float aiml_prefab_get_y(void* inst);
void  aiml_prefab_set_scale(void* inst, float s);
float aiml_prefab_get_scale(void* inst);
void  aiml_prefab_set_rotation(void* inst, float r);
float aiml_prefab_get_rotation(void* inst);
void  aiml_prefab_set_z(void* inst, int32_t z);
int32_t aiml_prefab_get_z(void* inst);
void  aiml_prefab_set_visible(void* inst, int32_t v);
int32_t aiml_prefab_get_visible(void* inst);
void  aiml_prefab_set_alpha(void* inst, float a);
float aiml_prefab_get_alpha(void* inst);
void  aiml_prefab_set_draggable(void* inst, int32_t d);
float aiml_prefab_get_width(void* inst);
float aiml_prefab_get_height(void* inst);

// Child properties
void  aiml_prefab_set_text(void* inst, const char* child_name, const char* text);
void  aiml_prefab_set_texture(void* inst, const char* child_name, void* tex);
void  aiml_prefab_set_color(void* inst, const char* child_name, int32_t color);
void  aiml_prefab_set_child_visible(void* inst, const char* child_name, int32_t v);
void* aiml_prefab_child(void* inst, const char* child_name);

// UI-widget callbacks + accessors
void        aiml_prefab_register_callback(const char* fn_name, void* fn_ptr);
const char* aiml_prefab_text_value(void* inst, const char* child_name);
int32_t     aiml_prefab_child_clicked(void* inst, const char* child_name);
int32_t     aiml_prefab_child_focused(void* inst, const char* child_name);
int32_t     aiml_prefab_child_checked(void* inst, const char* child_name);
void        aiml_prefab_set_checked  (void* inst, const char* child_name, int32_t v);

// Material registration
// uniforms_json format: {"u_x":{"k":"f","v":0.6}, "u_v":{"k":"v4","v":[...]}}
void aiml_material_register(const char* name, const char* shader_path,
                            const char* uniforms_json);

// Material registration. The uniforms_json format is described in
// prefab_runtime.h — tagged value objects keyed by uniform name.
void aiml_material_register(const char* name, const char* shader_path,
                            const char* uniforms_json);

// Dock (grid-snapping)
void  aiml_prefab_dock_x(void* inst, int32_t col, int32_t colspan);
void  aiml_prefab_dock_y(void* inst, int32_t row, int32_t rowspan);

// Scene rendering
void  aiml_prefab_draw_scene(void);

// Hit testing
void* aiml_prefab_hit_test(float mx, float my);

// Hover tracking
void  aiml_prefab_update_hover(float mx, float my);
void* aiml_prefab_get_hovered(void);
int32_t aiml_prefab_is_hovered(void* inst);
int32_t aiml_prefab_was_clicked(void* inst, int32_t btn);

// Drag-and-drop
void  aiml_prefab_update_drag(float mx, float my, int32_t btn_down, int32_t btn_released);
void* aiml_prefab_get_dragged(void);

// Utility
void  aiml_prefab_set_font(void* font);
```
