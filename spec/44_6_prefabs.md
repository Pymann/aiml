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
    input_transparent: false      # default; true = instances take no input
    children:
      - { name: title, type: label, pos_x: 100, pos_y: 250, anchor: center, font_size: 11, color: 0xFFFFFF }
      - { name: art,   type: sprite, pos_x: 15, pos_y: 100, width: 170, height: 120 }
      - { name: bg,    type: rect, pos_x: 0, pos_y: 0, width: 200, height: 280, color: 0x1A1A2E }
```

**Prefab keys.**

| Key | Effect |
|---|---|
| `name` | registration name for `engine.instantiate` |
| `width` / `height` | dimensions in design coordinates |
| `texture` | optional base texture |
| `draggable` | `true` = whole instance draggable |
| `input_transparent` | `true` = **every** instance of this prefab takes no input (§click-through) |

`input_transparent` on the prefab is the place for decoration: damage numbers,
flashes, frames, banners. Setting it per `engine.instantiate` instead makes it
a line that is forgotten on the next effect — and then the decoration steals the
hover again. The instance method `set_input_transparent` remains
and overrides the prefab value for this one instance (in both directions).

> **Position keys.** Children accept either **`x`/`y`** (legacy short form) or
> **`pos_x`/`pos_y`** (recommended). Both are aliased by the parser. New code
> should use `pos_x`/`pos_y` for consistency with the AIML naming-lint, which
> rejects ≤2-char identifiers in the rest of the language — using the same
> form here keeps style uniform when prefab definitions and AIML bodies sit
> side by side. A global rename of `x:` → `pos_x:` is safe; conversely, dropping
> `pos_x:` → `x:` also still parses. Mixing is allowed but discouraged.

### Child Types

| Type | Description |
|------|-------------|
| `sprite` | Textured quad (requires `width`, `height`) |
| `label` | Text line (requires `font_size`, `color`; optional `anchor`, `valign`, `label`). Supports `\n` for multi-line; combine with `valign: top` to align the text block to the top of the box (default centers a single line vertically). |
| `rect` | Rectangle. Solid color (`color`), rounded with border (`bg_color` + `corner_radius` + `border_color`/`border_width`), or textured (`texture`). |
| `button` | Interactive clickable widget. Hover/active states; callback on click. |
| `text_input` | Editable text field, **single-line or multi-line** (see below). Keyboard focus, a movable cursor, text **selection** (mouse drag, Shift+arrows, Ctrl+A) and **clipboard** (Ctrl+C / Ctrl+V / Ctrl+X); UTF-8-aware. `secret` mode. A field shorter than ~1.8 line heights is single-line and **scrolls horizontally** so the caret stays visible; a taller field is **multi-line** — it wraps at the inner width and scrolls vertically with the caret. Both clip to the inner box. With `gutter: true` it shows line numbers, and `set_color_runs` colours parts of the content (§5e, §5g). |
| `checkbox` | Toggleable box + label. State stored on the child; click flips. Use `width × height` to size the surrounding region; the box itself is `min(width, height)` square on the left, label fills the remaining width. When neither `border_color` nor `border_width` is set, the box falls back to a subtle default outline (so an unstyled checkbox is always visible); set them explicitly to override. |
| `radio` | Exclusive choice within a named `group`. Round toggle + label; visually identical layout to `checkbox` but rendered as a circle. Clicking sets THIS child checked and unchecks every other `radio` with the same `group` string across the entire scene. Use the same `is_checked` / `set_checked` API as checkbox for programmatic read/write. |
| `dropdown` | Single-choice selector. Options come from a pipe-separated `options:` string ("A\|B\|C") **or** from an `enum: TypeName` binding (labels auto-derived from the enum members). Click opens a popup list; selection is stored as an index. Read the label with `dropdown_value`, the index with `dropdown_index`, and — for an `enum:`-bound dropdown — the true enum member value with `dropdown_enum` (see §5b). |
| `bar` | Progress/gauge. A rounded track (`bg_color` + `border_*` + `corner_radius`) with a fill (`fill_color`) sized `value/max` of the width. Update at runtime via `set_bar_value` / `set_bar_max` / `set_bar_fill`. |
| `slider` | Interactive horizontal slider. Same track + fill as `bar`, with a drag-handle knob centered on the value-position. Mouse press inside the track grabs the handle; while held, cursor-x maps linearly to a value in `[min, max]`. Fires `on_change` whenever the value changes. Reads via `get_slider_value(child)`, writes via `set_slider_value`/`set_slider_min`/`set_slider_max`. The handle uses `fill_color` (or white if absent) with `hover_color`/`active_color` tints. |
| `container` | Dynamic list spawner. Bound to a `list<T>` on the instance's model via `bind_list:`; spawns one instance of `spawn: <Prefab>` per element and keeps the count synced with `.len`. `layout:` (`vflow`/`hflow`) + `gap:` control placement; `compact:` re-packs only visible rows; `scrollable:` gives it a **native scrollbar** (mouse wheel + draggable thumb, engine-windowed). See §5c. |
| `textbox` | Multi-line, scrollable, read-only text area (chat logs, event logs). Wraps on width and on `\n`/`\|`; native scrollbar (mouse wheel + draggable thumb). Text can be **selected with the mouse and copied** (`Ctrl+A`/`Ctrl+C`), but not edited. `tail: true` sticks the view to the bottom as content grows. Set the whole text at once with `set_text`. See §5f. |

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
| `anchor` | `"left"` \| `"center"` \| `"right"` | `"left"` | (label) horizontal alignment of the text inside the box. |
| `valign` | `"top"` \| `"center"` \| `"bottom"` | `"center"` (when `height>0`) | (label) vertical alignment. Default centers a single-line label inside its `height` box. Use `"top"` for multi-line text blocks (e.g. tooltip bodies) — without it, a 3-line string is centered around the first line's vertical center, pushing the block downward. `"bottom"` aligns the block flush to the bottom edge. |
| `secret` | bool | `false` | (text_input) Render chars as `*` (password mode). |
| `visible` | bool | `true` | Start state. `false` instantiates the child hidden, to be revealed with `set_visible`. Declaring it beats instantiating visible and hiding it in the first frame, which shows one frame of the wrong thing. |
| `scrollable` | bool | `false` | (container) Engine-managed row window + native scrollbar (see §5c). |
| `tail` | bool | `false` | (textbox) Keep the view pinned to the bottom (newest line) as content grows; releases while the user scrolls up, re-follows once back at the bottom (see §5f). |
| `on_click` | function name | `""` | (button, checkbox, radio) Callback fired when the widget is clicked. |
| `on_change` | function name | `""` | (text_input, checkbox, radio, slider) Callback fired when the value changes. |
| `group` | string | `""` | (radio) Group name for exclusive-choice behavior. Clicking one radio deselects every other radio (across the whole scene) sharing the same `group` string. Ignored on non-radio widgets. |
| `material` | string | `""` | Name of a registered material (see §5a). Replaces the default fragment shader for this widget. |
| `value` | float | `0.0` | (bar / slider) Current value. |
| `min` | float | `0.0` | (slider) Lower bound. `bar` always treats `0` as the lower bound and ignores this field. |
| `max` | float | `1.0` | (bar / slider) Upper bound. For `bar`: fill width = `value/max`. For `slider`: handle position = `(value - min) / (max - min)`. |
| `fill_color` | `0xRRGGBBAA` | `0` | (bar / slider) Colour of the filled portion of the track. For `slider` it also colours the handle (modulated by `hover_color` / `active_color`). |
| `cascade_type` | string | `""` | (dropdown) Drive the options from a registered **function cascade** (spec/57) instead of `options`: the widget lists that cascade's nodes, and selecting one spawns a typed payload widget per field. See §5h. |
| `payload_spawn` | string (prefab) | `""` | (dropdown, with `cascade_type`) Prefab instantiated once per field of the selected node; the runtime rewrites its `control` child to match the field type (enum → dropdown, i32/alias → numeric, nested cascade → sub-dropdown). See §5h. |
| `cascade_flow` | `"horizontal"` \| `"vertical"` | `"horizontal"` | (dropdown, with `cascade_type`) Where the payload widgets go: in a row to the right of the dropdown, or stacked underneath it. See §5h. |
| `payload_scroll` | bool | `false` | (dropdown, with `cascade_type`, horizontal flow) Clip the payload row to the widget's own prefab width and give it a **horizontal scrollbar** (wheel + draggable thumb), instead of letting a deep cascade run out of the prefab. See §5h. |

**An unknown child attribute is a compile error.** The attribute names above are
the complete set; anything else is rejected at build time, naming the child, its
type and the offending key. A silently dropped key is the worst of both worlds —
the declaration reads as if the property were set, and nothing says otherwise.
Masking is the case that proves it: `secret` is the only key that masks a
`text_input`, and a plausible-looking `password: true` left an SSH and a sudo
password readable on screen in the cosmos client (2026-10-13), because the key
was accepted by YAML and then thrown away. The same rule catches a typo in an
attribute name exactly as a typo in an asset stem is caught.

> This cuts both ways: a key documented here must actually reach the runtime.
> `min`, `scrollable` and `tail` were in this table while the **declarative**
> path dropped them (only the runtime's JSON loader read them), so a prefab
> declaring `tail: true` silently did nothing. Any key added to this table needs
> the parser, the emitted descriptor and the runtime together — a table entry is
> not an implementation.

### Example: Login Panel

```aiml
- prefab:
    name: LoginPanel
    width: 524
    height: 584
    children:
      - { name: frame, type: rect,
          pos_x: 0, pos_y: 0, width: 524, height: 584,
          texture: "ui/login_frame.png" }
      - { name: email_in, type: text_input,
          pos_x: 100, pos_y: 210, width: 324, height: 38,
          bg_color: 0x0A1830E6, border_color: 0x66B0E0FF,
          border_width: 1.5, corner_radius: 5.0 }
      - { name: pass_in,  type: text_input, secret: true,
          pos_x: 100, pos_y: 300, width: 324, height: 38,
          bg_color: 0x0A1830E6, border_color: 0x66B0E0FF,
          border_width: 1.5, corner_radius: 5.0 }
      - { name: submit,   type: button,
          pos_x: 120, pos_y: 380, width: 284, height: 46,
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
| `engine.destroy(inst: prefab_instance)` | Remove the instance from the scene and free it. Nested children are **not** freed immediately, but marked for deletion and cleaned up at the next `draw_scene` — see "Dying instances take no input". |
| `engine.draw_scene()` | Render all visible instances in Z-order. Call after `gl.clear_all()`, before `engine.swap()`. Ends the frame: dropdown popups, the dead-instance sweep and the partial-draw watermark (§8.1) all happen here. |
| `engine.draw_scene_upto(z: i32)` | Render only the instances **below** `z` and leave the rest for a later call — the hook for putting your own drawing (3D, raw GL) **inside** the Z-order. See §8.1. |

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
| `.set_compact(child, on)` | `(string, i32) -> void` | (container) Enable/disable compacting layout — hidden rows take no space (see §5c). |
| `.focus(child)` | `(string) -> void` | (text_input) Give this input keyboard focus programmatically (e.g. re-focus the chat box after the Send button stole focus). No-op if the child isn't a `text_input`. |
| `.scroll_to_end(child)` | `(string) -> void` | (textbox / scrollable container) Pin the child to its bottom (newest line / last row) and re-arm a textbox's `tail` follow. |
| `.child(child_name)` | `(string) -> prefab_instance` | Child as its own instance for direct manipulation |
| `.text_value(child)` | `(string) -> string` | (text_input) Current text of an input field — an owned copy (spec/61 §8), freed by the receiving local |
| `.child_clicked(child)` | `(string) -> i32` | (button) `1` if button was clicked this frame, else `0` |
| `.child_contains(child, sx, sy)` | `(string, f32, f32) -> i32` | `1` if the SCREEN point `(sx,sy)` is inside the child's bounds. Works for ANY child type (incl. non-interactive `rect`s), unlike `child_clicked` which only tracks widgets. Handles window scaling. Used for drop-target hit tests. |
| `.child_focused(child)` | `(string) -> i32` | (text_input) `1` if input has keyboard focus, else `0` |
| `.child_rect_x(child)` / `.child_rect_y(child)` | `(string) -> f32` | Top-left of the child's hit rect in SCREEN coordinates, window scaling applied — the inverse of `child_contains`, which only tests a point you already have. `-1` if the child does not exist. |
| `.child_rect_w(child)` / `.child_rect_h(child)` | `(string) -> f32` | Width / height of the hit rect in screen coordinates. `-1` if the child does not exist. |
| `.child_design_x(child)` / `.child_design_y(child)` / `.child_design_w(child)` / `.child_design_h(child)` | `(string) -> f32` | The same rectangle in **DESIGN** coordinates (1280×720), i.e. divided by the window scaling. `-1` if the child does not exist. |
| `.is_checked(child)` | `(string) -> i32` | (checkbox) `1` if box is currently checked, else `0` |
| `.set_checked(child, v)` | `(string, i32) -> void` | (checkbox) Programmatically set state (no callbacks fired) |
| `.set_bar_value(child, value)` | `(string, f32) -> void` | (bar) Set current value. |
| `.set_bar_max(child, max)` | `(string, f32) -> void` | (bar) Set range. |
| `.set_bar_fill(child, color)` | `(string, i32) -> void` | (bar) Recolor the fill. |
| `.get_slider_value(child)` | `(string) -> f32` | (slider) Read current value. |
| `.set_slider_value(child, value)` | `(string, f32) -> void` | (slider) Write current value (clamped to `[min, max]`). |
| `.set_slider_min(child, min)` | `(string, f32) -> void` | (slider) Set lower bound. |
| `.set_slider_max(child, max)` | `(string, f32) -> void` | (slider) Set upper bound. |
| `.dropdown_index(child)` | `(string) -> i32` | (dropdown) Selected option index (0-based). |
| `.dropdown_value(child)` | `(string) -> string` | (dropdown) Selected option label. |
| `.dropdown_enum(child)` | `(string) -> i32` | (dropdown) Selected option's real enum member value (for an `enum:`-bound dropdown). `-1` if the child isn't found or isn't enum-bound. See §5b. |
| `.set_dropdown_index(child, idx)` | `(string, i32) -> void` | (dropdown) Set selection by index (clamped to option count). |
| `.set_dropdown_selected(child, value)` | `(string, string) -> void` | (dropdown) Set selection by matching an option label. |
| `.set_dropdown_options(child, opts)` | `(string, string) -> void` | (dropdown) Replace the pipe-separated option list. |

A click (button, checkbox, radio, dropdown) is a press **and** release of mouse button 0 both inside
the widget's hit rect. It survives the widget being destroyed and re-created between press and
release (e.g. a screen rebuild): the new instance at the same rect receives the click.

Four scalar getters rather than one rect type, matching `.get_x()`/`.get_y()`: a caller that wants
the centre computes `x + w/2`, and one that wants a point *inside* a wide widget (a slider at 25 %)
computes that just as directly.

### Hit trace — `engine.prefab_trace_hits(on)` / `engine.prefab_last_hit()`

| Call | Description |
|------|-------------|
| `engine.prefab_trace_hits(on)` | Off by default. When on, every mouse press writes the resolved target(s) to the log at **info** level — enabling the trace *is* the intent to see it, so it does not additionally depend on the log threshold. |
| `engine.prefab_last_hit()` | The targets the last press resolved to, `"\|"`-separated. `""` = the press hit nothing; `"?"` = not collected because tracing is off. Collection runs only while tracing is on, so it costs an app that never enables it nothing; `"?"` keeps an assertion on "hit nothing" from passing for the wrong reason in that state. |

```
[prefab] hit ClickScene[0]/ok_btn at (200.0,120.0) kind=3
[prefab] miss at (8.0,8.0)
```

The address is `<prefab-def>[<instance-index>]/<child>`. The def name alone would be ambiguous with
several instances of the same prefab, so the scene index is part of it.

**It reports every widget whose rect contains the press point, not one winner.** The widget pass
deliberately does not gate on which instance owns the cursor (a transparent panel must let widgets
through), so overlapping widgets can *both* fire. The trace shows that instead of hiding it — the
`"|"`-separated list is the honest answer, and two entries where one was expected is itself the
finding. Dropdown popups are the exception: an open popup absorbs clicks, so only it is hit.

This is what makes an automated UI test meaningful. A test that only asserts on the end state cannot
distinguish "the button did nothing" from "the click never reached the button" — three pixels off, a
widget on top, or a rebuild between press and release all look alike. `prefab_last_hit` separates
them, and `""` (miss) is an assertable outcome rather than a silent pass.
`/spec/44_9_ui_testing.md` §4 builds `expect_hit` on it.

## 5b. Enum-Bound Dropdowns

A dropdown can be bound to an enum instead of a hand-written `options:` list:

```yaml
- child:
    type: dropdown
    name: zone_dd
    enum: GameZone        # ← bind to the GameZone enum
```

**Option labels** come from the enum members, snake_case auto-derived to Title
Case: `spell_zone` → `Spell Zone`, `mana_zone` → `Mana Zone`. An explicit
`labels:` map on the enum declaration overrides the derived text.

**Reading the value** — three accessors, three meanings:

- `dropdown_value(child)` → the display **label** string (`"Spell Zone"`).
- `dropdown_index(child)` → the 0-based **option index**.
- `dropdown_enum(child)`  → the true **enum member value** (i32).

`dropdown_enum` is the one to use in game/UI logic: it returns the enum's actual
member value from the registry, so it is correct for **sparse or non-1-based
enums**. For `GameZone` with members `hand=1, graveyard=2, mana_zone=10,
room_soulmage=30`, selecting the 4th option returns `30` — not `4`. The result
is i32 (enum-compatible), so it compares and assigns directly against the enum:

```yaml
- decl: [zone, GameZone, { call: [<.scene.dropdown_enum, "zone_dd"] }]
- if: { condition: { eq: [zone, GameZone.room_soulmage] }, then: [ ... ] }
```

This is the enum-first replacement for the old pattern of reading
`dropdown_value` as a string and running it through a string→enum parser (which
now trips the `str_cmp` static check, spec/49 §16). It also supersedes the
legacy cascade `selected_index + 1` value derivation, which only held for
gap-free 1-based option lists.

> Radio-button groups will gain the same enum binding + `radio_enum` accessor;
> the underlying enum registry (label + value per member) is already shared.

---

## 5c. Model-Bound Containers (dynamic lists)

A `type: container` child is a **dynamic spawner**: it renders one prefab
instance per element of a `list<T>` on the enclosing instance's bound model, and
keeps that count synchronized with `.len` every frame (grows on `push`, shrinks
on `pop`). This is how variable-length UI (card libraries, deck rows, player
lists, ability chips) is built without manual add/remove bookkeeping.

### Definition fields

| Field | Type | Description |
|-------|------|-------------|
| `bind_list` | string | Name of the `list<T>` field to bind (on the instance's model, or supplied at runtime via `bind_list(child, list)`). |
| `spawn` | string | Prefab name to instantiate once per list element. |
| `layout` | `"vflow"` \| `"hflow"` | `vflow` (default) stacks rows top-down; `hflow` lays them left-to-right. |
| `gap` | float | Pixel spacing between spawned rows. |
| `compact` | bool | When `true`, **hidden rows take no layout space** — see below. Default `false`. |
| `scrollable` | bool | When `true`, the engine windows the rows and draws a **native scrollbar** (mouse wheel + draggable thumb) — see "Native scrolling" below. Default `false`. |

```yaml
- { name: lib_container, type: container, pos_x: 18, pos_y: 526,
    width: 1244, height: 188,
    spawn: CardFull, layout: hflow, gap: 6, compact: true }
```

Bind the backing list once (usually right after `instantiate`, and again after
any reload that reassigns the list):

```yaml
- call: [<.scene.bind_list, "lib_container", <.library]
```

Populate each spawned row per frame by walking the container's rows:

```yaml
- decl: [n, i32, { call: [<.scene.container_row_count, "lib_container"] }]
- decl: [i, i32, 0]
- while:
    condition: { lt: [i, n] }
    body:
      - decl: [row, prefab_instance, { call: [<.scene.container_row, "lib_container", i] }]
      - if:
          condition: { '&&': [{ neq: [row, null] }, { lt: [i, <.library.len] }] }
          then:
            - decl: [cd, MockCard, { call: [<.library.get, i] }]
            - call: [populate_card_full, row, cd]
      - set: [i, { +: [i, 1] }]
```

### `compact` — gap-free filtering via visibility

By default the container lays out **every** spawned row by its bound index, so a
row hidden with `set_visible(0)` still occupies its slot → visible rows leave
gaps. With `compact: true` the runtime instead re-packs only the **visible** rows
contiguously from the container origin, every frame. This lets you filter a bound
list purely by toggling row visibility — no separate filtered list, no rebuilds:

```yaml
# non-matching cards vanish AND close their gap, list stays left-aligned
- decl: [show, i32, { call: [card_passes, cd, flt] }]
- call: [row.set_visible, show]
```

`compact` can also be toggled at runtime instead of (or in addition to) the
definition field:

| Method | Signature | Effect |
|--------|-----------|--------|
| `.set_compact(container, on)` | `(string, i32) -> void` | Enable/disable compaction on a container child. |

The compaction pass is pure arithmetic over the spawned rows (cost is negligible)
and composes with `layout`: `hflow` packs left-to-right, `vflow` top-down. The
row currently being dragged is **skipped** by the re-pack (its slot is reserved)
so a drag follows the pointer instead of being yanked back into the grid.

### `scrollable` — native scrollbar

`scrollable: true` makes the **engine** own the scrolling: it shows only a window
of rows (visible count = container extent ÷ row pitch), packs them from the origin
like `compact`, and draws a track + draggable thumb on the right edge whenever
there are more rows than fit. The mouse wheel over the container and dragging the
thumb both move the window — no AIML scroll bookkeeping.

```yaml
- { name: pl_container, type: container, x: 20, y: 96, width: 224, height: 400,
    spawn: PlayerRow, layout: vflow, gap: 4, scrollable: true }
```

The AIML side just binds the list and fills each row's text (per §5c) — it must
**not** call `set_visible` on the rows, since the engine controls visibility.
`scrollable` windows **every** spawned row, so it is for **unfiltered** lists;
a filtered browser (where AIML hides non-matching rows) keeps its own paging.
Jump to the last row with `scroll_to_end(child)`.

---

## 5h. Cascade widgets (`cascade_type` + `payload_spawn`)

A `dropdown` with `cascade_type:` takes its options from a registered **function
cascade** (spec/57) rather than from `options:`. Picking a node makes the runtime
spawn one `payload_spawn:` instance per field of that node and rewrite each one's
`control` child to match the field's type — enum → dropdown, i32/alias → numeric
input, nested cascade → another cascade dropdown. Changing a node higher up
destroys the payloads below it and spawns the new ones, which is what makes the
chain visibly collapse and re-expand.

```yaml
- { name: effect, type: dropdown, x: 0, y: 2, width: 200, height: 22,
    cascade_type: Effect, payload_spawn: AAFieldChip, payload_scroll: true }
```

The payload instances are **nested children** of the enclosing prefab instance
(§6b), positioned by the runtime from the *actual* rendered extent of the
previous sibling — so a field never lands on top of the expanded payload of the
field before it.

### `cascade_flow` — where the payloads go

`horizontal` (the default) lays the payloads in a row to the right of the
dropdown; `vertical` stacks them underneath it. Vertical trades width for height:
the row grows downward, which an enclosing `scrollable` container does **not**
account for — it packs rows by the row prefab's declared height (§5c), so
vertically-flowed rows inside a scrollable container overlap. Use `vertical`
only outside a row-packed container.

### `payload_scroll` — horizontal scrollbar for a deep chain

A cascade with many fields outgrows its prefab: `200` for the dropdown plus one
payload width per field, which quickly passes the prefab's own `width` and then
draws over whatever sits to the right of it. `payload_scroll: true` bounds it:

- the payload row is **clipped** to the widget's prefab width (the clip box starts
  right of the dropdown, so panned payloads never slide under it);
- a horizontal **track + thumb** is drawn along the bottom `6 px` of the prefab
  whenever the chain is wider than the clipped box — leave that much room below
  the dropdown (a `26 px` row with a `22 px` dropdown needs to become `34 px`);
- the **mouse wheel** over the payload row and dragging the thumb both pan it.

Panning snaps to the payload pitch (payload width + gap) because clipping is
whole-child (§6c): a payload that straddles the clip edge is not drawn at all, so
a sub-pitch offset would blank a field instead of half-showing it. Stepping by a
whole pitch swaps fields cleanly.

`payload_scroll` is for `horizontal` flow; with `cascade_flow: vertical` there is
nothing to pan sideways and the key is ignored.

---

## 5d. Model-Bound Widgets (scalar fields)

Where §5c binds a *container* to a `list<T>`, a prefab can bind its **scalar
widgets** (sliders, dropdowns, text) to the **fields of one model type** — the
form's field mapping becomes a declaration the compiler checks, and load/save
collapse to one call each. This is the same "schema known at compile time,
generic access at runtime" coupling as bytecode cascades and `db_indexed`.

### Declaration

A prefab opts in with `binds: <TypeName>` and marks each participating
child with `bind: <field>`:

```yaml
- prefab:
    name: CardEditForm
    binds: CardDraft          # the one model type this form edits
    children:
      - { name: name_in, type: text_input, x: 0, y: 0,  width: 200, height: 24, bind: name }
      - { name: type_dd, type: dropdown, enum: CardType, x: 0, y: 30, width: 200, height: 24, bind: card_type }
      - { name: atk_sl,  type: slider, x: 0, y: 60, width: 200, height: 16, max: 20.0, bind: attack }
      # Unbound children (labels, buttons, borders) carry no `bind:`.
```

`bind:` on a child of a prefab **without** `binds` is a compile error.

### Widget ↔ field compatibility (compile-checked)

| Widget `type:`        | Field type   | Stored value |
|-----------------------|--------------|--------------|
| `slider`, `bar`       | `i32`, `f32` | numeric (`bar_value`) |
| `dropdown`            | enum, `i32`  | the selected option's **enum value** (not its row index) |
| `text_input`, `label` | `string`     | the widget's text |

A mismatched pairing, or a `bind:` naming a field absent from `binds`, is a
compile error. A bound dropdown's `enum:` must match the field's enum type (the
same check that makes `dropdown_enum` type-safe).

### Attaching a model + syncing

| Method | Signature | Effect |
|--------|-----------|--------|
| `.bind_model(m)` | `(ptr<T>) -> void` | Attach a model instance (borrowed, caller-owned). Re-call to rebind. `T` **should** be the prefab's `binds` type; the field-level checks below are compile-enforced, but the instance-type match is by convention in Phase 1 (passing the wrong type reads wrong offsets). |
| `.sync_from_model()` | `() -> void` | Model fields → widgets (load / populate). |
| `.sync_to_model()`   | `() -> void` | Widgets → model fields (read back / save). |

```yaml
- decl: [form,  prefab_instance, { call: [engine.instantiate, "CardEditForm"] }]
- decl: [draft, ptr<CardDraft>,  { new: CardDraft }]
- call: [form.bind_model, draft]
- call: [form.sync_from_model]     # populate the form from `draft`
# … user edits …
- call: [form.sync_to_model]       # harvest the form back into `draft`
```

Sync is **explicit** (not per-frame): typically `sync_from_model` right after
`bind_model`, and `sync_to_model` on the Save button. Per widget:
`sync_from_model` sets `bar_value` (i32→float, clamped) / selects the option
whose enum value equals the field / sets the text; `sync_to_model` writes the
field back (i32 rounded) / stores the selected option's enum value / copies the
text. Sync with no model bound is a silent no-op.

### How it works

For each bound child the compiler resolves `bind: field` against `binds`,
computes the field's byte offset (module `DataLayout`) and a type tag
(`0=i32, 1=f32, 2=string, 3=enum`), and bakes `bind_offset` / `bind_tag` into
the child's registration next to `bind`. `bind_model` stores the model `void*`
on the instance; the sync calls read/write `*(model_ptr + bind_offset)` per tag
against the widget's existing state. Because a prefab binds to exactly one model
type the offsets are constant — no runtime schema registry is needed.

*Phase 1* covers numeric + enum fields (sliders, bars, dropdowns); *Phase 2*
adds `string` fields (text/label). Nested (`bind: a.b`) and per-frame auto-sync
are out of scope.

---

## 5e. `text_input` — editing, selection, clipboard

A focused `text_input` behaves like a normal single-line editor. All navigation
and deletion is **UTF-8-aware** (moves/deletes whole codepoints, so `ä`/`ö`/`ü`
are never split), and cursor/selection positions are **byte offsets** into the
text.

**Cursor + editing**
- Typing inserts at the cursor; `Backspace` deletes before it, `Delete` after it.
- `←` / `→` move by one character; `Home` / `End` jump to start / end.

**Selection**
- Click-drag with the mouse selects a range; `Shift`+`←/→/Home/End` extends it;
  `Ctrl+A` selects all. The selection is drawn as a highlight.
- Typing, paste, or `Backspace`/`Delete` over a selection **replaces** it.

**Clipboard** (system clipboard, via the window)
- `Ctrl+C` copies the selection, `Ctrl+X` cuts it, `Ctrl+V` pastes at the cursor
  (replacing any selection).
- `AltGr` is **not** treated as `Ctrl` (so `@`, `€`, … still type on layouts
  where `AltGr` reports as `Ctrl+Alt`).

**Focus** — clicking a `text_input` focuses it; clicking elsewhere (e.g. a button)
blurs it. Re-focus programmatically with `focus(child)` — e.g. to keep the caret
in a chat box after its Send button was clicked:

```yaml
- if:
    condition: { eq: [{ call: [<.scene.child_clicked, "chat_send"] }, 1] }
    then:
      - call: [net_send_chat, msg]
      - call: [<.scene.set_text, "chat_input", ""]
      - call: [<.scene.focus, "chat_input"]     # keep typing without re-clicking
```

---

## 5f. `textbox` — multi-line scrollable text

A `type: textbox` is a **read-only, scrollable** multi-line text area — the widget
for chat logs, event logs, and other growing text (a `label` can't scroll). It
accepts the usual style fields (`font_size`, `color`, optional `bg_color` +
`border_*`).

- **Content:** set the whole text with `set_text(child, text)`. It wraps to the
  box width and breaks on `\n` **and** `|` (so a pipe-joined line list works).
- **Scrolling:** the engine windows the wrapped lines and draws a track + thumb
  when the text overflows; mouse wheel over the box and dragging the thumb scroll
  it. No AIML scroll bookkeeping.
- **`tail: true`:** keeps the view stuck to the **bottom** (newest line) as the
  text grows — ideal for chat. It releases while the user scrolls up and
  re-follows once they scroll back to the bottom. `scroll_to_end(child)` pins it
  to the bottom on demand (e.g. right after the user sends a message).

```yaml
- { name: chat_log, type: textbox, x: 276, y: 96, width: 496, height: 340,
    font_size: 12, color: 0xDDDDDDFF, tail: true }
```

```yaml
# on each poll: hand the full log to the box; windowing + tail are automatic
- call: [<.scene.set_text, "chat_log", { call: [join_pipe, clog.lines] }]
```

- **`wrap: false`** turns the wrapping off: a line stays one line, however long, and the box
  scrolls **sideways**.  That is what a build log or a YAML source needs — wrapping turns a
  column of 200-character lines into porridge, and the eye loses the structure the author put
  there.  With `wrap: false` the box draws a **second scrollbar along the bottom**, and the
  horizontal window follows the caret like the vertical one does.  Default is `true`
  (wrapping), because a chat log wants it.

**Keys — all four directions** (spec/44_6 §5f, 2026-10-22)

| Key | Effect |
|---|---|
| `↑` / `↓` | one line up/down (the view follows the caret) |
| `←` / `→` | one character; at the line's edge it steps to the previous/next line |
| `PageUp` / `PageDown` | one window height |
| `Home` / `End` | start/end of the **line** |
| `Ctrl+Home` / `Ctrl+End` | start/end of the **text** |
| `Shift`+any of them | extends the selection instead of collapsing it |

The caret is always scrolled **into view**: a key that moves it out of the window moves the
window.  Before this, `textbox` knew only `←/→/Home/End` — in a 2000-line log that is not
navigation, and a multi-line `text_input` (the pipeline source) had **no** vertical arrows at
all: the caret could not leave line one by keyboard.

**The horizontal bar has a grip** (2026-10-22): `Shift`+wheel moves the window sideways, the
thumb can be dragged, and a click **next to** it jumps there and keeps dragging.  A bar you can
see but not move is worse than none — it shows that there is more and refuses to go.  The same
applies to the inset: the text is clipped to the **inner** area, not the frame, otherwise the
last glyphs sit exactly on the border line; and when the bottom bar is shown it takes a line's
worth of room instead of covering the last line.

**Selection & copy** (read-only — the text can be marked and copied, never typed into)

- Clicking the box **focuses** it and places the cursor; **dragging** selects a
  range across wrapped lines; dragging past the top/bottom edge scrolls along.
  A click on the scrollbar still scrolls instead of selecting.
- `Ctrl+A` marks everything, `Shift`+`←/→/Home/End` extends, plain arrows
  collapse the selection.
- `Ctrl+C` copies the selection — or, with **nothing** marked, the **whole**
  text (for a log box that is practically always what was meant). It copies the
  **source** text verbatim, including the `\n` line breaks, not the wrapped
  rendering.  The system clipboard is used (spec/44_0 §2.5), so in the browser
  the copy needs a secure context; `input.init` must have run.
- `Escape` drops the focus.
- Because the widget is read-only there is no `Ctrl+V`/`Ctrl+X`, no typing, and
  no caret is drawn.

**Programmatic selection** — works for `textbox` and `text_input`:

| Method | Signature | Description |
|---|---|---|
| `select_all` | `scene.select_all(child)` | Marks the whole content and gives the widget focus, so a following `Ctrl+C` copies it. |
| `selection` | `scene.selection(child) -> string` | The marked text, `""` when nothing is marked. |

```yaml
# "Copy" button: selection (or everything) to the clipboard
- call: [<.scene.select_all, "token_box"]
- call: [input.clipboard_set, { call: [<.scene.selection, "token_box"] }]
```

An unknown child name throws, like every other widget method (§find_child is
deliberately loud — it is almost always a typo).

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

### Uniform type detection

| YAML | GLSL | Example |
|------|------|---------|
| `0.6` | `float` | `u_intensity: 0.6` |
| `[a, b]` | `vec2` | `u_facet_freq: [28.0, 22.0]` |
| `[a, b, c]` | `vec3` | `u_color: [1.0, 0.5, 0.2]` |
| `[a, b, c, d]` | `vec4` | `u_tint: [0.4, 0.7, 1.0, 1.0]` |
| `{ int: N }` | `int` | `u_count: { int: 42 }` |
| `{ tex: "path" }` | `sampler2D` | `u_noise: { tex: "noise.png" }` |

### Built-in uniforms (always available — no declaration needed in the material)

| Uniform | GLSL | Description |
|---------|------|-------------|
| `u_proj` | `mat4` | Orthographic projection matrix |
| `u_tex` | `sampler2D` | White-Tex slot (slot 0) |
| `u_mode` | `int` | Render mode passed from DrawBatch |
| `u_time` | `float` | Seconds since the first frame |

### Built-in varyings (from the shared vertex shader)

| Varying | GLSL | Description |
|---------|------|-------------|
| `v_uv` | `vec2` | UV [0,1] across the quad |
| `v_color` | `vec4` | Fill colour of the widget (bg/hover/active) |
| `v_quad` | `vec2` | (width, height) in pixels |
| `v_style` | `vec2` | (corner_radius, border_width) |
| `v_bcolor` | `vec4` | Border colour |

### Shader file format

User shaders supply only `void main()` + their own `uniform` declarations. The runtime
automatically prepends:
- `#version 300 es / 330 core` (platform-dependent)
- `precision highp float;` (GLES)
- all built-in `in`/`uniform`/`out` declarations

Example `crystal_vein.frag`:

```glsl
uniform vec4  u_tint;
uniform float u_vein_density;
uniform float u_vein_glow;

void main() {
    // v_uv, v_color, v_quad etc. are already in scope
    float pulse = 0.5 + 0.5 * sin(u_time * u_vein_density);
    out_color = vec4(v_color.rgb + u_tint.rgb * (u_vein_glow * pulse), v_color.a);
}
```

### Reference from a prefab child

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

Materials are resolved by name on instantiate. On `null` (unknown
name) the widget silently falls back to the default shader.

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

## 6b. Nested Instances

A prefab instance can host **other instances** as children. A nested child
inherits the parent's world transform (position + scale), so moving or scaling
the parent moves/scales the whole group. Use this to compose reusable widgets
(e.g. a tile hosting a badge + a bar) from separate prefab definitions.

```aiml
- decl: [tile,  prefab_instance, { call: [engine.instantiate, "Tile"]  }]
- decl: [badge, prefab_instance, { call: [engine.instantiate, "Badge"] }]
- call: [badge.set_pos, 56.0, 8.0]   # position is LOCAL to the tile
- call: [tile.add_child, badge]
- call: [tile.set_pos, 160.0, 120.0] # moves tile + badge together
- call: [tile.set_scale, 1.25]       # scales both
- call: [tile.remove_child, badge]   # detach → badge becomes top-level again
```

| Method | Signature | Description |
|--------|-----------|-------------|
| `.add_child(other)` | `(prefab_instance) -> void` | Nest `other` under this instance. Its position/scale become local to the parent. |
| `.remove_child(other)` | `(prefab_instance) -> void` | Detach a nested child (back to top-level). |

Notes:
- Nested children are skipped by the top-level draw loop and by `hit_test`
  (their stored coords are parent-local). Keep interaction on the parent or on
  non-nested instances.
- `destroy` detaches an instance from its parent and orphans its own nested
  children (they become top-level again).

---

## 6c. Scroll Viewport + Clipping

Mark an instance as a **clip/scroll container**: its nested children are
clipped to the instance's world bounds (GL scissor) and shifted by a scroll
offset. Combined with nested instances (§6b) this gives a scroll viewport.

```aiml
- decl: [vp, prefab_instance, { call: [engine.instantiate, "Viewport"] }]
- call: [vp.set_pos, 100.0, 80.0]
- call: [vp.set_clip, 1]                 # clip nested children to vp's bounds
# ... add_child rows ...
- call: [vp.set_scroll, 0.0, 60.0]       # scroll content up by 60 local px
- decl: [sy, f32, { call: [vp.get_scroll_y] }]
```

| Method | Signature | Description |
|--------|-----------|-------------|
| `.set_clip(enabled)` | `(i32) -> void` | Clip nested children to this instance's world bounds. |
| `.set_scroll(sx, sy)` | `(f32, f32) -> void` | Scroll offset (local px) applied to nested children. |
| `.get_scroll_x()` / `.get_scroll_y()` | `-> f32` | Current scroll offset. |

Notes:
- Scrolling shifts nested children only; the container frame stays put.
- Nested clips replace (don't intersect) an outer clip — keep one scroll
  container per region for now.
- Clamp scrolling in AIML (min/max) as needed; the runtime does not bound it.
- Clipping is **whole-child**: a nested child renders only if its full
  bounding box fits inside the clip container. Children that straddle a
  container edge are skipped entirely (no per-pixel partial render). This is
  intentional — it guarantees no edge leakage regardless of how the underlying
  GL/WebGL2 scissor behaves. For smooth scrolling, snap `scroll_y` to the row
  pitch (i.e. step by the height of one entry per click/key) so transitions
  swap whole rows cleanly. The runtime also sets `GL_SCISSOR_TEST` as a
  second line of defence.

---

## 6a. Connectors (Cables between instances)

A **connector** is a cable drawn between two prefab instances. Endpoints are
resolved **every frame** from the live position/size of both instances, so a
cable follows its prefabs as they move. Cables render in `engine.draw_scene()`
**behind** all prefabs.

The cable is a tessellated quadratic-Bézier ribbon shaded like a round cable
seen from the side (bright spine, darker rim, specular streak) with a colour
gradient along its length.

### Type: `connector`

Opaque handle (`i8*`) to a live cable.

```aiml
- decl: [cab, connector, { call: [engine.connect, node_a, node_b] }]
- call: [cab.set_colors, 0x44CCFFFF, 0x2266AAFF]   # gradient: from-end → to-end
- call: [cab.set_highlight, 0xFFFFFFFF]            # specular streak tint
- call: [cab.set_width, 8.0]                       # cable thickness (px)
- call: [cab.set_bow, 0.15]                        # perpendicular curve (fraction of chord)
- call: [cab.set_port, port_tex, 1.6]              # optional endpoint sprite (size = width*scale)
- call: [cab.set_visible, 1]
- call: [cab.destroy]                              # remove the cable
```

| Call | Signature | Description |
|------|-----------|-------------|
| `engine.connect(from, to)` | `(prefab_instance, prefab_instance) -> connector` | Create a cable. |
| `.set_colors(c0, c1)` | `(i32, i32) -> void` | Gradient `0xRRGGBBAA` from `from`-end to `to`-end. |
| `.set_highlight(c)` | `(i32) -> void` | Specular streak colour. |
| `.set_width(w)` | `(f32) -> void` | Thickness in px. |
| `.set_bow(amount)` | `(f32) -> void` | Perpendicular bow; `0` = straight, `0.15` ≈ gentle arc (fraction of chord length). |
| `.set_port(tex, scale)` | `(gl_texture, f32) -> void` | Optional sprite at both endpoints; size = `width * scale`. |
| `.set_visible(v)` | `(i32) -> void` | Show/hide. |
| `.destroy()` | `-> void` | Remove and free. |

### Anchor modes (per instance)

How cables attach to an instance is set on the **instance**, not the cable —
so all cables touching it stay consistent:

```aiml
- call: [hub.set_connector_anchor, 2]   # distributed
- call: [leaf.set_connector_anchor, 1]  # edge
```

| Mode | Value | Behaviour |
|------|-------|-----------|
| center | `0` (default) | All cables meet the instance centre. |
| edge | `1` | Cables meet the centre of the edge facing the partner. |
| distributed | `2` | Cables spread evenly along the facing edge. |

The facing edge is chosen automatically from the dominant axis of the
direction to the partner instance.

---

## 7. Hit-Testing, Hover + Drag-and-Drop

### Hit-Testing

| Call | Description |
|------|-------------|
| `engine.hit_test(mx, my) -> prefab_instance` | Topmost visible instance at screen coordinate (or `null`) |

Hit-test iterates in reverse Z-order (top-to-bottom).

#### Click-through: `set_input_transparent`

| Instance Method | Signature | Description |
|-----------------|-----------|-------------|
| `.set_input_transparent(on)` | `(i32) -> void` | `1` = this instance is **skipped** by `engine.hit_test` and `engine.update_hover`; input hits whatever lies beneath. `0` (default) = previous behaviour. |
| `.get_input_transparent()` | `-> i32` | current value |

Until now there were only two properties, `visible` and `draggable`, and that
left a gap: **a purely decorative instance can steal input.**
`hit_test` returns the topmost *visible* instance without asking whether it
wants to be operated at all — and `update_hover` sets `hovered` only on
this one result. A large preview, a banner or a frame over a
card thereby makes the card unreachable: it never becomes `is_hovered`, so
`was_clicked`, target selection and any hover-driven logic beneath it
no longer take effect. Switching visibility off is no substitute — then the preview
is gone too.

`set_input_transparent(1)` separates "is drawn" from "takes input": the
instance stays visible and draws normally, but is skipped in the hit
test.

```yaml
- call: [preview.set_input_transparent, 1]   # preview shows, but does not block
```

Applies to `hit_test` and `update_hover`. The drag start (`update_drag`,
`update_drag_gesture`) only checks `draggable` anyway, so it is not affected.

#### Modal: `set_input_blocking`

| Instance Method | Signature | Description |
|-----------------|-----------|-------------|
| `.set_input_blocking(on)` | `(i32) -> void` | `1` = this instance **blocks** input for everything with a smaller `z`, as long as the cursor is within its area. `0` (default) = previous behaviour. |
| `.get_input_blocking()` | `-> i32` | current value |

This is the counterpart of `set_input_transparent` and closes the opposite
gap. `hit_test` already arbitrates by `z`, but the **widget** pass
does not: it deliberately runs over *all* instances, so that a transparent panel
lets its own widgets through. The consequence was that a full-screen overlay correctly
blocked the hover, **but the buttons beneath it remained clickable** — a
click into the dialog additionally triggered whatever lay beneath.

```yaml
- call: [leave_confirm.set_input_blocking, 1]   # real modal dialog
```

Normative:

- What counts is the **topmost visible** blocking instance whose area
  contains the cursor. Widgets of instances with a smaller `z` receive neither
  clicks nor keyboard focus.
- The blocking instance itself and everything with an **equal or greater** `z`
  remains operable — so a dialog can have its own buttons, and an even
  higher overlay in turn blocks it.
- Blocking happens only **within the area** of the instance. A full-screen dim
  thereby blocks everything, a small panel only what it covers.
- Invisible, click-through and deletion-marked instances do not
  block.
- The **drag start** is blocked too: `update_drag` / `update_drag_gesture`
  pick up nothing beneath a blocking instance. Otherwise "blocks
  input beneath" would only mean "blocks clicks", and the selection — which
  skips non-draggables — would reach through the dialog for the
  card beneath.

Canonical: `tests/test_prefab_input_blocking.aiml`.

#### Dying instances take no input

An instance is not always freed immediately. `destroy` frees the instance
itself, but only marks its nested children for deletion and detaches their
parent binding — they are only freed in the cleanup step at the end of the next
`draw_scene`. The same applies to rows that a `bind_list` gives up when
shrinking or switching.

It follows normatively:

- An instance marked for deletion takes **no input** any more: `hit_test`
  skips it (so it does not become `hovered`, `was_clicked` does not become true,
  and `engine.get_hovered` does not name it), **and** its widget children accept
  neither clicks nor keyboard focus. The focus is the more important part:
  it **sticks** until the next click elsewhere, so it outlives the free.
- When freeing, the scene drops **every** reference to the instance:
  `hovered`, the dragged instance, the drag source and the keyboard focus.

Both are necessary because a second-level child survives exactly **one frame** as an
orphan: parentless, therefore top-level, and visible. Without the first rule
this orphan could take the hover although it is already dead — and in this state it
draws at raw local coordinates, so it turns up somewhere at the
window origin. Without the second rule the hover or focus pointer would point
to freed memory afterwards; with the focus that is particularly delicate, because it
**sticks** until a click elsewhere.

Canonical: `tests/test_prefab_doomed_no_hover.aiml`.

The model thus has three of the four independent properties: **takes input
itself** (`input_transparent`), **blocks input beneath**
(`input_blocking`, see below) and **is draggable** (`draggable`).

> **Open (fourth property):** "accepts this drop type" is still missing —
> which drop is allowed on which surface is decided by the application code
> in the drop handler, not by the engine.

#### Hit order: when `set_z` takes effect

The Z-order in which `hit_test` (and the drag start in
`engine.update_drag`) traverses the instances is **established by `engine.draw_scene`**
— there the instance list is stably sorted by
`z_order` as a side effect. `.set_z(z)` only writes the field and does **not** sort.
For the current state this means:

- A `set_z` takes effect for hit and drag checks only **after the next
  `draw_scene`**, i.e. one frame later. Whoever raises a card in the same frame
  (`set_z`) and then calls `hit_test` gets the **old** order.
- An instance created after the last `draw_scene` is appended at the
  **end of the list** and is therefore the *topmost* instance for the reverse pass —
  regardless of its `z`. Whoever rebuilds a scene and checks hits in the same frame
  checks against creation order, not against `z`.
- With **equal** `z` the insertion order decides (`stable_sort`), i.e.
  the order of the `engine.instantiate` calls. Surfaces that serve as drop targets
  and can overlap therefore need **explicitly
  different** `z` values; relying on creation order is
  not guaranteed.

> **Open:** A dirty mark on `set_z` (and on creation) that lets
> `hit_test`/`update_drag` sort by themselves when needed would remove all three
> points and make `set_z` take effect immediately. Until then the state
> above applies.

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
    condition: { neq: [{ call: [deck_wesen.was_clicked, 0] }, 0] }
    then:
      - call: [log.info, "Card drawn from creature deck"]

# Show hover preview
- decl: [hov, prefab_instance, { call: [engine.get_hovered] }]
- if:
    condition: { neq: [hov, 0] }
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

#### Cancellation: `update_drag` ends the drag without a drop

`update_drag` additionally evaluates the **cancel edge**
(`input.mouse_cancelled(0)`, spec/44_0 §2.2 — cancelled touch, second
finger, loss of focus). A cancel

- resets the dragged instance to its origin (like `cancel_drag`),
- clears `engine.get_dragged()` to **null**,
- produces **no** release transition.

This makes the common drop pattern work correctly by itself: whoever detects the drop at the
transition "dragged before, not any more" must additionally ask whether
the gesture was *completed*:

```yaml
- decl: [pre, prefab_instance, { call: [engine.get_dragged] }]
- call: [engine.update_drag, mx, my, btn, rel]
- decl: [post, prefab_instance, { call: [engine.get_dragged] }]
# Drop ONLY on a real release — a cancel must not trigger a move.
- if:
    condition: { '&&': [{ neq: [pre, null] }, { '&&': [{ peq: [post, null] }, { eq: [{ call: [input.mouse_cancelled, 0] }, 0] }] }] }
    then: [{ call: [handle_drop, pre] }]
```

Without this extra condition a cancel cannot be told apart from a release
and triggers the drop action — in a game, a move that
nobody made. The `input.drag_cancelled(btn)` query (spec/44_0 §2.4)
is the counterpart on the input layer.

#### `engine.update_drag_gesture` — pick-up at the press point

| Call | Return | Description |
|------|--------|-------------|
| `engine.update_drag_gesture(mx, my)` | `i32` | Gesture state of this frame (table below). Alternative to `update_drag` that uses the gesture machine from spec/44_0 §2.4. |
| `engine.get_drag_source()` | `prefab_instance` | The instance that picked up this gesture — still in the drop/cancel frame, when `get_dragged()` is already `null`. |

Return values:

| Value | Meaning |
|---|---|
| `0` | no gesture |
| `1` | pressed, threshold not yet exceeded (**candidate**, no drag yet) |
| `2` | dragging |
| `3` | **released in this frame** → perform the drop |
| `4` | **cancelled in this frame** → perform nothing |

The difference from `update_drag` is *when* and *where* the pick-up happens:

| | `update_drag` | `update_drag_gesture` |
|---|---|---|
| Trigger | button is **down** (level) | `input.drag_started(0)` (edge, after threshold) |
| Pick-up point | mouse position **of this frame** | `input.drag_origin_*` = **press point** |
| Tap (press without movement) | starts a drag | stays a candidate (`1`), no drag |
| Cancel | only via `input.mouse_cancelled` in the caller | its own return value `4` |

This fixes two classes of bugs that are unavoidable with `update_drag`:

1. **Pick-up on the wrong card.** Browser callbacks write the
   mouse position live, button edges only at the frame snapshot (spec/44_0 §7.1) —
   so the edge belongs to an *older* position than the one the same
   frame reads. With a moving mouse the level trigger picks up the card that is
   *now* under the cursor, not the one that was pressed. The
   press point from `drag_origin_*` does not have this problem.
2. **A tap becomes a drag.** Without a threshold every click on a
   draggable element is a mini drag; applications then distinguish tap and drop
   after the fact by movement distance, i.e. only after the drag has already
   run. The threshold (`input.drag_set_threshold`, default 5 px) separates
   the two **beforehand**.

```yaml
- decl: [gst, i32, { call: [engine.update_drag_gesture, mx, my] }]
# 3 = released → drop.  4 = cancelled → do NOTHING.
- if:
    condition: { eq: [gst, 3] }
    then:
      - decl: [src, prefab_instance, { call: [engine.get_drag_source] }]
      - call: [handle_drop, src]
```

`update_drag` remains valid unchanged; both ways access the same
`engine.get_dragged()` state, but must **not be mixed in the same
frame** (they would take the pick-up away from each other).

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

### 8.1 Hooking a layer of your own into the Z-order

`draw_scene` draws the prefabs in **one** pass. Whoever wants to draw in between
themselves — a 3D model, raw GL — could for a long time only do so **before** or
**after**, and both are wrong as soon as there are windows: before, the
background (negative `z`) covers the own picture, after, it lies over **every**
window and shows through semi-transparent surfaces.

`engine.draw_scene_upto(z)` draws only the instances with `z_order < z` and
remembers how far it got. The concluding `engine.draw_scene()`
draws the **rest** — every instance exactly once:

```aiml
- call: [gl.clear_all, 0.05, 0.05, 0.1, 1.0]
- call: [engine.draw_scene_upto, 10]   # background (z=-10) and stage (z=5)
- call: [zeichne_modell]               # the own layer, here at z=10
- call: [engine.draw_scene]            # windows (z=50+) and everything above
- call: [engine.swap, win]
```

Rules:

- **`draw_scene` ends the frame.** Only there do the dropdown popups run
  (second pass, always on top), the sweep of dying instances and the
  reset of the watermark. Whoever calls `draw_scene_upto` and leaves out `draw_scene`
  does not see the upper layers and cleans nothing up.
- **Z-sorting happens on the FIRST draw call of the frame**, so that
  all partial draws of the same frame see the same order. A `set_z`
  between two partial draws only takes effect in the next frame.
- **Half-open interval.** `draw_scene_upto(10)` draws `z < 10`, not
  `z <= 10`. Several own layers are allowed: `upto(10)`, draw,
  `upto(60)`, draw, `draw_scene`.
- **Without `draw_scene_upto` nothing changes.** A `draw_scene` without
  a preceding partial draw draws the whole scene as always.
- **The engine takes back blending and depth test**, on EVERY
  partial draw, not just once per frame. A 3D layer of your own switches
  the depth test on and clears the depth buffer; if that stayed, the
  remaining UI would fail the depth test and be invisible. So the
  caller does not have to watch out for this.
- **The caller restores the viewport.** It is not part of the GL state
  the engine sets, but determines the projection derived from it on the first
  draw call of the frame. Whoever sets a sub-rectangle for their layer sets the
  whole window again afterwards — otherwise the
  rest of the frame is clamped into this rectangle.

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
void  aiml_prefab_container_set_compact(void* inst, const char* child_name, int32_t v);
void* aiml_prefab_child(void* inst, const char* child_name);

// UI-widget callbacks + accessors
void        aiml_prefab_register_callback(const char* fn_name, void* fn_ptr);
const char* aiml_prefab_text_value(void* inst, const char* child_name);
int32_t     aiml_prefab_child_clicked(void* inst, const char* child_name);
int32_t     aiml_prefab_child_contains(void* inst, const char* child_name, float sx, float sy);
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
void  aiml_prefab_draw_scene_upto(int32_t z);   // §8.1 — partial draw

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

## Leading whitespace and per-child fonts

Both were added for a source-code viewer (cosmos `doc/spec/98` F98-14/F98-15); both apply to every
prefab text.

**Whitespace is preserved verbatim.**  `wrap_text_lines` used to treat every space as a word
separator, so indentation vanished and runs between words collapsed to one.  Both are wrong for
what the widget is actually used for: indented source and column-aligned logs.  Collapsing was a
prose habit — a log table lost its columns and YAML lost its structure.

Now every space is kept.  The only whitespace that disappears is the run at which a line is
**wrapped**, because a continuation line starts at the left edge.  Selection became *simpler* as a
result: a rendered line is a plain slice of the source, so the caret walk is 1:1, with that one
break-run skipped at the start of a continuation line.

**A child may choose its font.**

| Attribute | Type | Default | Meaning |
|---|---|---|---|
| `font` | string | `""` (scene font) | name of a font registered with `engine.prefab_add_font` |

| Call | Description |
|---|---|
| `engine.prefab_set_font(font)` | the scene's default font, as before |
| `engine.prefab_add_font(name, font)` | register an extra font under a name |

```yaml
- { name: src_box, type: textbox, font: "mono", font_size: 12, ... }
```

```yaml
- decl: [mono, ui_font, { call: [gl.font, "mono", 13.0] }]
- if: { condition: { neq: [mono, null] }, then: [{ call: [engine.prefab_add_font, "mono", mono] }] }
```

An unknown name falls back to the scene font, so a missing asset degrades to the default face
instead of drawing nothing.  One global font cannot serve both a proportional interface and a
monospace code box, which is why this is per child rather than per scene.

## Addendum: `set_bg_color` / `set_border_color` were missing from the method list

Both have existed for some time (`aiml_prefab_set_bg_color`, `aiml_prefab_set_border_color`, registered as
instance methods), but were not in the table of child setters.  The consequence while
building the cosmos shell: `set_color` was used because it was the only one visible — and that
sets, for a `rect`, the mostly transparent `color`, not the visible fill `bg_color`, and
for a `button` the text colour instead of the border.  So the call did nothing recognisable.

| Method | Effect |
|---|---|
| `.set_color(child, rgba)` | `color` — sprite tint, rect solid, **label/button text colour** |
| `.set_bg_color(child, rgba)` | `bg_color` — the visible fill of a `rect`/button |
| `.set_border_color(child, rgba)` | `border_color` — frame/stroke |

For a `rect` the fill is `bg_color` and `color` is usually 0; whoever wants to recolour a surface
needs `set_bg_color`.


## 5g. Code fields: multi-line editing, gutter, colour runs

**Correction.**  This chapter used to state that `text_input` "is always one line regardless of its
`height`".  That was never true of the implementation: `prefab_runtime.cpp` switches to a
multi-line mode — wrapping plus vertical caret-follow scrolling — as soon as the field is taller
than ~1.8 line heights.  The behaviour was there, undocumented, while cosmos `doc/spec/98` planned
a second widget to get it (F98-2).  One editable multi-line field is enough; what it lacked was
presentation.

### Line numbers

| Attribute | Type | Default | Meaning |
|---|---|---|---|
| `gutter` | bool | `false` | draw a line-number column at the left edge of a multi-line field |

Numbers count **source** lines (`\n`-separated), not wrapped display lines: a long line that
wraps three times keeps one number, because the number has to match what a compiler reports
(cosmos `doc/spec/98` F98-9).

### One press, one character (fixed 2026-10-18)

The same family as click positioning, one level deeper: the field **was** correct,
the event queue was not.  On the web, the hidden DOM bridge for the mobile
keyboard queued control keys a second time that the window `keydown` handler had already
queued — measured on the cosmos console field: `Delete`, `Enter` and the
arrow keys **twice**, i.e. one character too many deleted and two positions jumped.

The rule is now in spec/44_0 §3.1 (one press = exactly one event) together with the
responsibility: the DOM bridge only speaks when no named hardware key is in flight
(soft keyboard, IME, autocorrect).  Evidence: `tests/test_prefab_backspace.aiml` counts
what one press does — structurally, such a bug is invisible.

### The Enter key in a multi-line field (fixed 2026-10-18)

While re-measuring, a second contradiction came up: this table and §5g promise multi-line
**editing**, but `Enter` never inserted a line break — it only called `on_change`.
In a code field that means: one cannot start a new line.  The source text had to be
pasted in to have more than one line.

Now the distinction applies that the field already makes anyway:

| Field kind | `Enter` |
|---|---|
| single-line (< ~1.8 line heights) | calls `on_change` — the submit |
| multi-line | inserts `\n` at the caret (replaces a selection) |

Multi-line or not is decided at **draw** time (height against line height) and noted on the
child (`ti_multiline`); the input pass reads the same result instead of making the
decision a second time.

### Click positioning (fixed 2026-10-15)

The widget table promises for `text_input` "a movable cursor, text **selection** (mouse
drag…)".  For **multi-line** fields this was not kept: the render pass created exactly
**one** cursor stop (byte 0) with the comment "multi-line click-positioning falls back
to the start".  Every click therefore jumped to the start of the document — in a code field that means
that typing and deleting happen somewhere, just not where the pointer is.

Now the render pass builds the same **2D map** as the text box (§5f): cursor stops per
visible line, plus the line tops.  From that the input pass first determines the
**line** from `y`, then the **column** from `x` — a pure x search across all lines would
almost always land in the wrong line.

**The starting point includes the gutter width.**  With `gutter: true` the text is drawn shifted right by the
number column; a map without this offset is off by exactly this
width — the cursor would visibly sit next to the character that was clicked.
In `secret` mode the map is **not** built: there the drawn characters are dots,
whose widths say nothing about the bytes of the text.

Regression test: `tests/test_prefab_multiline_click.aiml` — clicks to the right in the first and
then far down in the last line and checks **where** the typed character lands.  Without the
fix it reports "Klick landete am DOKUMENTANFANG".

### Colour runs

| Method | Description |
|---|---|
| `.set_color_runs(child, runs: list<i32>)` | three entries per run: `start`, `len`, `rgba` |
| `.clear_color_runs(child)` | back to the field's single `color` |

`start`/`len` are **byte offsets into the field's text**, the same offsets the caret, the selection
and `yaml.tokens` (spec/60) use — so a highlighter maps token kind to colour and passes the result
through unchanged:

```yaml
- decl: [toks, list<i32>, { call: [yaml.tokens, src] }]
- decl: [runs, list<i32>]
- decl: [idx, i32, 0]
- while:
    condition: { lt: [idx, toks.len] }
    body:
      - call: [runs.push, { at: [toks, idx] }]
      - call: [runs.push, { at: [toks, { +: [idx, 1] }] }]
      - call: [runs.push, { call: [color_for_kind, { at: [toks, { +: [idx, 2] }] }] }]
      - set: [idx, { +: [idx, 3] }]
- call: [scene.set_color_runs, "src_box", runs]
```

Bytes not covered by a run keep the field's `color`.  Runs are applied in order; where two overlap,
the later one wins.  A run that reaches past the end of the text is clipped rather than refused —
a highlighter running one frame behind an edit must not be an error.

## Colour runs apply to `textbox` too

`set_color_runs` at first only worked in the input field (`text_input`, §5g) — that is where it was created for
syntax highlighting.  That was a limitation of the renderer, not of the concept: **colour is a
property of text, not of editability.**  A status column in a display needs it
just as much (cosmos `doc/spec/97` F97-17: the colour carries the status, the text still names it).

Since 2026-10-12 `textbox` draws the runs too, with the same rule: per byte the last
set run applies, same-coloured neighbours are drawn in one go, and the byte offsets refer
to the box's **source text**, not to the wrapped line.  Without runs the
fast path (one batch for all visible lines) stays unchanged.


- **F44_6-…: A scrollbar that nothing moves is a picture.**  The multi-line `text_input`
  had always DRAWN a vertical bar, but had no input for it at all: no wheel,
  no dragging the handle.  Worse, the offset was pulled back to the cursor in **every** frame
  (and set to line 0 when focus was missing) — even someone who had moved it
  would have been yanked back immediately.  Reported on 2026-10-22 on the source field of a pipeline.
  Now the same rule applies as in the text box: the view follows the cursor only when the
  cursor **has moved**; after that it belongs to the reader (`ti_free_scroll`).  Wheel, Shift+wheel
  (horizontal) and dragging the handle work, and a click on the bar sets **no**
  cursor and selects nothing — otherwise every scroll drags text along.

### Screen vs design coordinates for child rectangles

`child_rect_*` returns **screen** pixels (window scaling included) — right
for hit tests and cursor positions. But `set_pos`/`x`/`y` of an instance expect
**design** pixels (1280×720). Whoever wanted to place an instance next to a child
(tooltip, speech bubble, hint arrow) therefore inevitably mixed two spaces:
with a window ≠ 1280×720 the instance lands shifted by the scaling factor
— and the further right/down the target is, the further off.

That is what `child_design_*` is for. Rule: **hit/cursor → `child_rect_*`,
placing → `child_design_*`.**

```yaml
# Place a speech bubble to the right of an input field:
- decl: [rect_x, f32, { call: [<.scene.child_design_x, "name_input"] }]
- decl: [rect_w, f32, { call: [<.scene.child_design_w, "name_input"] }]
- call: [<.bubble.set_pos, { +: [{ +: [rect_x, rect_w] }, 16.0] }, 100.0]
```

Noticed on 2026-10-25 on the tour speech bubble in 9rooms: with a scaled
window it stood far away from the name field, because `child_rect_*` are screen and
`set_pos` design coordinates.
