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
    input_transparent: false      # Standard; true = Instanzen nehmen keine Eingabe
    children:
      - { name: title, type: label, pos_x: 100, pos_y: 250, anchor: center, font_size: 11, color: 0xFFFFFF }
      - { name: art,   type: sprite, pos_x: 15, pos_y: 100, width: 170, height: 120 }
      - { name: bg,    type: rect, pos_x: 0, pos_y: 0, width: 200, height: 280, color: 0x1A1A2E }
```

**Prefab-Schlüssel.**

| Schlüssel | Wirkung |
|---|---|
| `name` | Registrierungsname für `engine.instantiate` |
| `width` / `height` | Maße in Design-Koordinaten |
| `texture` | optionale Grundtextur |
| `draggable` | `true` = ganze Instanz ziehbar |
| `input_transparent` | `true` = **jede** Instanz dieses Prefabs nimmt keine Eingabe (§Durchklickbar) |

`input_transparent` am Prefab ist der Ort für Dekoration: Schadenszahlen,
Blitze, Rahmen, Banner. Setzt man es stattdessen pro `engine.instantiate`, ist
es eine Zeile, die man beim nächsten Effekt vergisst — und dann stiehlt die
Dekoration wieder den Hover. Die Instanzmethode `set_input_transparent` bleibt
und überschreibt den Prefab-Wert für diese eine Instanz (in beide Richtungen).

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
| `engine.destroy(inst: prefab_instance)` | Remove the instance from the scene and free it. Genestete Kinder werden **nicht** sofort frei, sondern zum Löschen markiert und beim nächsten `draw_scene` aufgeräumt — siehe „Sterbende Instanzen nehmen keine Eingabe". |
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
| `.child_design_x(child)` / `.child_design_y(child)` / `.child_design_w(child)` / `.child_design_h(child)` | `(string) -> f32` | Dasselbe Rechteck in **DESIGN**-Koordinaten (1280×720), also durch die Fenster-Skalierung geteilt. `-1` wenn das Kind nicht existiert. |
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
# "Kopieren"-Button: Auswahl (oder alles) in die Zwischenablage
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

#### Durchklickbar: `set_input_transparent`

| Instance Method | Signature | Description |
|-----------------|-----------|-------------|
| `.set_input_transparent(on)` | `(i32) -> void` | `1` = diese Instanz wird von `engine.hit_test` und `engine.update_hover` **übersprungen**; Eingabe trifft, was darunter liegt. `0` (Standard) = bisheriges Verhalten. |
| `.get_input_transparent()` | `-> i32` | aktueller Wert |

Bis hierher gab es nur zwei Eigenschaften, `visible` und `draggable`, und daraus
folgte eine Lücke: **eine rein dekorative Instanz kann Eingabe stehlen.**
`hit_test` liefert die oberste *sichtbare* Instanz, ohne zu fragen, ob sie
überhaupt bedient werden will — und `update_hover` setzt `hovered` nur auf
dieses eine Ergebnis. Eine große Vorschau, ein Banner oder ein Rahmen über einer
Karte macht die Karte damit unerreichbar: sie wird nie `is_hovered`, also
greifen `was_clicked`, Zielauswahl und jede hover-getriebene Logik darunter
nicht mehr. Sichtbarkeit abzuschalten ist kein Ersatz — dann ist die Vorschau
auch weg.

`set_input_transparent(1)` trennt „wird gezeichnet" von „nimmt Eingabe": die
Instanz bleibt sichtbar und zeichnet normal, wird aber beim Treffer-Test
übersprungen.

```yaml
- call: [preview.set_input_transparent, 1]   # Vorschau zeigt, blockiert aber nicht
```

Gilt für `hit_test` und `update_hover`. Der Drag-Start (`update_drag`,
`update_drag_gesture`) prüft ohnehin nur `draggable`, ist also nicht betroffen.

#### Modal: `set_input_blocking`

| Instance Method | Signature | Description |
|-----------------|-----------|-------------|
| `.set_input_blocking(on)` | `(i32) -> void` | `1` = diese Instanz **blockiert** Eingabe für alles mit kleinerem `z`, solange der Cursor in ihrer Fläche liegt. `0` (Standard) = bisheriges Verhalten. |
| `.get_input_blocking()` | `-> i32` | aktueller Wert |

Das ist das Gegenstück zu `set_input_transparent` und schließt die umgekehrte
Lücke. `hit_test` arbitriert schon über `z`, der **Widget**-Durchlauf aber
nicht: der läuft bewusst über *alle* Instanzen, damit eine transparente Platte
ihre eigenen Widgets durchlässt. Folge war, dass ein Vollbild-Overlay den Hover
korrekt sperrte, **die Buttons darunter aber weiter klickbar blieben** — ein
Klick in den Dialog löste zusätzlich aus, was darunter lag.

```yaml
- call: [leave_confirm.set_input_blocking, 1]   # echter Modal-Dialog
```

Normativ:

- Maßgeblich ist die **oberste sichtbare** blockierende Instanz, deren Fläche
  den Cursor enthält. Widgets von Instanzen mit kleinerem `z` erhalten weder
  Klick noch Tastatur-Fokus.
- Die blockierende Instanz selbst und alles mit **gleichem oder größerem** `z`
  bleibt bedienbar — ein Dialog kann also eigene Buttons haben, und ein noch
  höheres Overlay sperrt wiederum ihn.
- Blockiert wird nur **innerhalb der Fläche** der Instanz. Ein Vollbild-Dim
  sperrt damit alles, ein kleines Panel nur, was es verdeckt.
- Unsichtbare, durchklickbare und zum Löschen markierte Instanzen blockieren
  nicht.
- Auch der **Drag-Start** ist gesperrt: `update_drag` / `update_drag_gesture`
  nehmen unter einer blockierenden Instanz nichts auf. Sonst hieße „blockiert
  Eingabe darunter" nur „blockiert Klicks", und die Auswahl — die
  Nicht-Draggbares überspringt — griffe durch den Dialog hindurch nach der
  Karte darunter.

Canonical: `tests/test_prefab_input_blocking.aiml`.

#### Sterbende Instanzen nehmen keine Eingabe

Eine Instanz wird nicht immer sofort freigegeben. `destroy` gibt die Instanz
selbst frei, markiert ihre genesteten Kinder aber nur zum Löschen und löst deren
Elternbindung — sie werden erst im Aufräumschritt am Ende des nächsten
`draw_scene` freigegeben. Dasselbe gilt für Zeilen, die eine `bind_list` beim
Schrumpfen oder Umschalten aufgibt.

Daraus folgt normativ:

- Eine zum Löschen markierte Instanz nimmt **keine Eingabe** mehr: `hit_test`
  überspringt sie (sie wird also nicht `hovered`, `was_clicked` wird nicht wahr,
  und `engine.get_hovered` nennt sie nicht), **und** ihre Widget-Kinder nehmen
  weder Klicks noch den Tastatur-Fokus an. Der Fokus ist der wichtigere Teil:
  er **klebt** bis zum nächsten Klick woanders, überlebt also die Freigabe.
- Beim Freigeben verliert die Szene **jede** Referenz auf die Instanz:
  `hovered`, die gezogene Instanz, die Drag-Quelle und der Tastatur-Fokus.

Beides ist nötig, weil ein Kind der zweiten Ebene genau **einen Frame** als
Waise überlebt: elternlos, also top-level, und sichtbar. Ohne die erste Regel
konnte diese Waise den Hover an sich nehmen, obwohl sie schon tot ist — und sie
zeichnet in diesem Zustand an rohen lokalen Koordinaten, taucht also irgendwo am
Fensterursprung auf. Ohne die zweite Regel zeigte der Hover- bzw. Fokuszeiger
danach auf freigegebenen Speicher; beim Fokus ist das besonders heikel, weil er
**klebt**, bis woanders geklickt wird.

Canonical: `tests/test_prefab_doomed_no_hover.aiml`.

Das Modell hat damit drei der vier unabhängigen Eigenschaften: **nimmt selbst
Eingabe** (`input_transparent`), **blockiert Eingabe darunter**
(`input_blocking`, siehe unten) und **ist ziehbar** (`draggable`).

> **Offen (vierte Eigenschaft):** „akzeptiert diesen Drop-Typ" fehlt weiter —
> welche Ablage auf welcher Fläche erlaubt ist, entscheidet der Anwendungscode
> im Drop-Handler, nicht die Engine.

#### Treffer-Reihenfolge: wann `set_z` wirkt

Die Z-Reihenfolge, nach der `hit_test` (und der Drag-Start in
`engine.update_drag`) die Instanzen durchläuft, wird **von `engine.draw_scene`
hergestellt** — dort wird die Instanzliste als Seiteneffekt stabil nach
`z_order` sortiert. `.set_z(z)` schreibt nur das Feld und sortiert **nicht**.
Daraus folgt für den aktuellen Stand:

- Ein `set_z` wirkt für Treffer- und Drag-Prüfung erst **nach dem nächsten
  `draw_scene`**, also einen Frame später. Wer im selben Frame eine Karte
  hochzieht (`set_z`) und dann `hit_test` ruft, bekommt die **alte** Ordnung.
- Eine Instanz, die nach dem letzten `draw_scene` erzeugt wurde, hängt am
  **Listenende** und ist damit für den Rückwärtslauf *oberste* Instanz —
  unabhängig von ihrem `z`. Wer eine Szene neu aufbaut und im selben Frame
  Treffer prüft, prüft gegen die Erzeugungsreihenfolge, nicht gegen `z`.
- Bei **gleichem** `z` entscheidet die Einfügereihenfolge (`stable_sort`), also
  die Reihenfolge der `engine.instantiate`-Aufrufe. Flächen, die als Drop-Ziel
  dienen und sich überlappen können, brauchen deshalb **ausdrücklich
  verschiedene** `z`-Werte; sich auf die Erzeugungsreihenfolge zu verlassen ist
  nicht zugesichert.

> **Offen:** Eine Dirty-Markierung bei `set_z` (und beim Erzeugen), die
> `hit_test`/`update_drag` bei Bedarf selbst sortieren lässt, würde alle drei
> Punkte beseitigen und `set_z` sofort wirksam machen. Bis dahin gilt der Stand
> oben.

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

#### Abbruch: `update_drag` beendet den Drag ohne Drop

`update_drag` wertet zusätzlich die **Cancel-Flanke** aus
(`input.mouse_cancelled(0)`, spec/44_0 §2.2 — abgebrochener Touch, zweiter
Finger, Fokusverlust). Ein Cancel

- setzt die gezogene Instanz auf ihren Ursprung zurück (wie `cancel_drag`),
- räumt `engine.get_dragged()` auf **null**,
- erzeugt **keinen** Release-Übergang.

Damit greift das verbreitete Drop-Muster von selbst richtig: wer den Drop am
Übergang „vorher gezogen, jetzt nicht mehr" erkennt, muss zusätzlich fragen, ob
die Geste *vollendet* wurde:

```yaml
- decl: [pre, prefab_instance, { call: [engine.get_dragged] }]
- call: [engine.update_drag, mx, my, btn, rel]
- decl: [post, prefab_instance, { call: [engine.get_dragged] }]
# Drop NUR bei echtem Release — ein Cancel darf keinen Spielzug auslösen.
- if:
    condition: { '&&': [{ neq: [pre, null] }, { '&&': [{ peq: [post, null] }, { eq: [{ call: [input.mouse_cancelled, 0] }, 0] }] }] }
    then: [{ call: [handle_drop, pre] }]
```

Ohne diese Zusatzbedingung ist ein Abbruch von einem Loslassen nicht zu
unterscheiden und löst die Drop-Aktion aus — in einem Spiel ein Zug, den
niemand gemacht hat. Die `input.drag_cancelled(btn)`-Abfrage (spec/44_0 §2.4)
ist das Gegenstück auf der Eingabeschicht.

#### `engine.update_drag_gesture` — Aufnehmen am Druckpunkt

| Call | Return | Description |
|------|--------|-------------|
| `engine.update_drag_gesture(mx, my)` | `i32` | Gestenzustand dieses Frames (Tabelle unten). Alternative zu `update_drag`, die die Gestenmaschine aus spec/44_0 §2.4 benutzt. |
| `engine.get_drag_source()` | `prefab_instance` | Die Instanz, die diese Geste aufgenommen hat — auch noch im Drop-/Abbruch-Frame, wenn `get_dragged()` schon `null` ist. |

Rückgabewerte:

| Wert | Bedeutung |
|---|---|
| `0` | keine Geste |
| `1` | gedrückt, Schwelle noch nicht überschritten (**Kandidat**, noch kein Drag) |
| `2` | zieht |
| `3` | **in diesem Frame losgelassen** → Drop ausführen |
| `4` | **in diesem Frame abgebrochen** → nichts ausführen |

Der Unterschied zu `update_drag` ist, *wann* und *wo* aufgenommen wird:

| | `update_drag` | `update_drag_gesture` |
|---|---|---|
| Auslöser | Taste ist **gedrückt** (Pegel) | `input.drag_started(0)` (Flanke, nach Schwelle) |
| Aufnahmepunkt | Mausposition **dieses Frames** | `input.drag_origin_*` = **Druckpunkt** |
| Tippen (Press ohne Bewegung) | startet einen Drag | bleibt Kandidat (`1`), kein Drag |
| Abbruch | nur über `input.mouse_cancelled` im Aufrufer | eigener Rückgabewert `4` |

Das behebt zwei Fehlerklassen, die mit `update_drag` unvermeidbar sind:

1. **Aufnahme an der falschen Karte.** Browser-Callbacks schreiben die
   Mausposition live, Tastenflanken erst beim Frame-Snapshot (spec/44_0 §7.1) —
   die Flanke gehört deshalb zu einer *älteren* Position als die, die derselbe
   Frame liest. Bei bewegter Maus nimmt der Pegel-Trigger die Karte auf, die
   *jetzt* unter dem Cursor liegt, nicht die, auf die gedrückt wurde. Der
   Druckpunkt aus `drag_origin_*` hat dieses Problem nicht.
2. **Tippen wird zum Drag.** Ohne Schwelle ist jeder Klick auf ein
   ziehbares Element ein Mini-Drag; Anwendungen unterscheiden Tap und Drop
   dann nachträglich an der Bewegungsdistanz, also erst, nachdem der Drag schon
   gelaufen ist. Die Schwelle (`input.drag_set_threshold`, Default 5 px) trennt
   beides **vorher**.

```yaml
- decl: [gst, i32, { call: [engine.update_drag_gesture, mx, my] }]
# 3 = losgelassen → Drop.  4 = abgebrochen → NICHTS tun.
- if:
    condition: { eq: [gst, 3] }
    then:
      - decl: [src, prefab_instance, { call: [engine.get_drag_source] }]
      - call: [handle_drop, src]
```

`update_drag` bleibt unverändert gültig; beide Wege greifen auf denselben
`engine.get_dragged()`-Zustand zu, dürfen aber **nicht im selben Frame
gemischt** werden (sie würden sich die Aufnahme gegenseitig wegnehmen).

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

## Nachtrag: `set_bg_color` / `set_border_color` fehlten in der Methodenliste

Beide existieren seit längerem (`aiml_prefab_set_bg_color`, `aiml_prefab_set_border_color`, als
Instanzmethoden registriert), standen aber nicht in der Tabelle der Kind-Setter.  Folge beim
Bauen der cosmos-Hülle: `set_color` wurde genommen, weil es das einzig sichtbare war — und das
setzt bei einem `rect` die meist transparente `color`, nicht die sichtbare Füllung `bg_color`, und
bei einem `button` die Textfarbe statt des Rahmens.  Der Aufruf tat also nichts Erkennbares.

| Methode | Wirkung |
|---|---|
| `.set_color(child, rgba)` | `color` — Sprite-Tint, Rect-Solid, **Label-/Button-Textfarbe** |
| `.set_bg_color(child, rgba)` | `bg_color` — die sichtbare Füllung eines `rect`/Buttons |
| `.set_border_color(child, rgba)` | `border_color` — Rahmen/Strich |

Bei einem `rect` ist die Füllung `bg_color` und `color` üblicherweise 0; wer eine Fläche umfärben
will, braucht `set_bg_color`.


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

### Ein Druck, ein Zeichen (behoben 2026-10-18)

Dieselbe Familie wie die Klick-Positionierung, eine Ebene tiefer: das Feld **war** richtig,
die Ereignis-Schlange nicht.  Im Web reihte die versteckte DOM-Brücke für die mobile
Tastatur Steuertasten ein zweites Mal ein, die der Fenster-`keydown`-Handler schon
eingereiht hatte — gemessen am Konsolenfeld von cosmos: `Delete`, `Enter` und die
Pfeiltasten **doppelt**, also ein Zeichen zu viel gelöscht und zwei Stellen gesprungen.

Die Regel steht jetzt in spec/44_0 §3.1 (ein Druck = genau ein Ereignis) samt der
Zuständigkeit: die DOM-Brücke spricht nur, wenn keine benannte Hardware-Taste im Flug ist
(Soft-Keyboard, IME, Autokorrektur).  Nachweis: `tests/test_prefab_backspace.aiml` zählt,
was ein Druck bewirkt — strukturell ist so ein Fehler unsichtbar.

### Die Eingabetaste im mehrzeiligen Feld (behoben 2026-10-18)

Beim Nachmessen fiel ein zweiter Widerspruch auf: diese Tabelle und §5g sagen mehrzeiliges
**Bearbeiten** zu, aber `Enter` fügte nie einen Zeilenumbruch ein — es rief nur `on_change`.
In einem Code-Feld heißt das: man kann keine neue Zeile anfangen.  Der Quelltext musste
eingefügt werden, um mehr als eine Zeile zu haben.

Jetzt gilt die Unterscheidung, die das Feld ohnehin schon trifft:

| Feldart | `Enter` |
|---|---|
| einzeilig (< ~1.8 Zeilenhöhen) | ruft `on_change` — das Abschicken |
| mehrzeilig | fügt `\n` an der Schreibmarke ein (ersetzt eine Auswahl) |

Mehrzeilig-oder-nicht wird beim **Zeichnen** entschieden (Höhe gegen Zeilenhöhe) und auf dem
Kind vermerkt (`ti_multiline`); der Eingabe-Pass liest dasselbe Ergebnis, statt die
Entscheidung ein zweites Mal zu treffen.

### Klick-Positionierung (behoben 2026-10-15)

Die Widget-Tabelle verspricht für `text_input` „a movable cursor, text **selection** (mouse
drag…)".  Für **mehrzeilige** Felder war das nicht eingelöst: der Render-Pass legte genau
**einen** Cursor-Halt an (Byte 0) mit dem Kommentar „multi-line click-positioning falls back
to the start".  Jeder Klick sprang also an den Dokumentanfang — in einem Code-Feld heißt das,
dass Tippen und Löschen irgendwo passieren, nur nicht dort, wo der Zeiger steht.

Jetzt baut der Render-Pass dieselbe **2D-Karte** wie die Textbox (§5f): Cursor-Halte je
sichtbarer Zeile, dazu die Zeilen-Oberkanten.  Der Eingabe-Pass bestimmt daraus erst die
**Zeile** aus `y`, dann die **Spalte** aus `x` — eine reine x-Suche über alle Zeilen würde
fast immer in der falschen Zeile landen.

**Der Startpunkt enthält die Gutter-Breite.**  Mit `gutter: true` wird der Text um die
Nummernspalte nach rechts gezeichnet; eine Karte ohne diesen Versatz liegt um genau diese
Breite daneben — der Cursor stünde sichtbar neben dem Zeichen, das man angeklickt hat.
Im `secret`-Modus wird die Karte **nicht** gebaut: dort sind die gezeichneten Zeichen Punkte,
deren Breiten nichts über die Bytes des Textes aussagen.

Regressionstest: `tests/test_prefab_multiline_click.aiml` — klickt rechts in die erste und
danach weit unten in die letzte Zeile und prüft, **wo** das getippte Zeichen landet.  Ohne den
Fix meldet er „Klick landete am DOKUMENTANFANG".

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

## Farb-Läufe gelten auch für `textbox`

`set_color_runs` wirkte zunächst nur im Eingabefeld (`text_input`, §5g) — dort entstand es für die
Syntax-Hervorhebung.  Das war eine Einschränkung des Zeichners, keine der Sache: **Farbe ist eine
Eigenschaft von Text, nicht von Editierbarkeit.**  Eine Statusspalte in einer Anzeige braucht sie
genauso (cosmos `doc/spec/97` F97-17: die Farbe trägt den Status, der Text nennt ihn trotzdem).

Seit 2026-10-12 zeichnet auch `textbox` die Läufe, mit derselben Regel: je Byte gilt der zuletzt
gesetzte Lauf, gleichfarbige Nachbarn werden in einem Zug gezeichnet, und die Byte-Offsets beziehen
sich auf den **Quelltext** der Box, nicht auf die umgebrochene Zeile.  Ohne Läufe bleibt der
schnelle Pfad (eine Charge für alle sichtbaren Zeilen) unverändert.


- **F44_6-…: Ein Rollbalken, den nichts bewegt, ist ein Bild.**  Das mehrzeilige `text_input`
  ZEICHNETE seit jeher einen senkrechten Balken, hatte aber keinerlei Eingabe dafür: kein Rad,
  kein Ziehen am Griff.  Schlimmer, der Versatz wurde in **jedem** Rahmen zum Cursor
  zurückgezogen (und bei fehlendem Fokus auf Zeile 0 gesetzt) — selbst wer ihn bewegt hätte,
  wäre sofort zurückgerissen worden.  Gemeldet am 22.10.2026 am Quelltext-Feld einer Pipeline.
  Jetzt gilt dieselbe Regel wie in der Textbox: dem Cursor folgt die Ansicht nur, wenn der
  Cursor sich **bewegt hat**; danach gehört sie dem Leser (`ti_free_scroll`).  Rad, Shift+Rad
  (waagerecht) und das Ziehen am Griff wirken, und ein Klick auf den Balken setzt **keinen**
  Cursor und markiert nichts — sonst zieht jedes Rollen Text mit.

### Screen- vs Design-Koordinaten bei Kind-Rechtecken

`child_rect_*` liefert **Screen**-Pixel (Fenster-Skalierung eingerechnet) — richtig
für Treffertests und Cursor-Positionen. `set_pos`/`x`/`y` einer Instanz erwarten
aber **Design**-Pixel (1280×720). Wer eine Instanz neben ein Kind stellen will
(Tooltip, Sprechblase, Hinweis-Pfeil), mischte damit zwangsläufig zwei Räume:
bei einem Fenster ≠ 1280×720 landet die Instanz um den Skalierungsfaktor
verschoben — und zwar umso weiter, je weiter rechts/unten das Ziel liegt.

Dafür gibt es `child_design_*`. Regel: **Treffer/Cursor → `child_rect_*`,
Platzieren → `child_design_*`.**

```yaml
# Sprechblase rechts neben ein Eingabefeld stellen:
- decl: [rect_x, f32, { call: [<.scene.child_design_x, "name_input"] }]
- decl: [rect_w, f32, { call: [<.scene.child_design_w, "name_input"] }]
- call: [<.bubble.set_pos, { +: [{ +: [rect_x, rect_w] }, 16.0] }, 100.0]
```

Aufgefallen 2026-10-25 an der Tour-Sprechblase in 9rooms: sie stand bei
skaliertem Fenster weit neben dem Namensfeld, weil `child_rect_*` Screen- und
`set_pos` Design-Koordinaten sind.
