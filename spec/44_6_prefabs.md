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
      - { name: title, type: label, pos_x: 100, pos_y: 250, anchor: center, font_size: 11, color: 0xFFFFFF }
      - { name: art,   type: sprite, pos_x: 15, pos_y: 100, width: 170, height: 120 }
      - { name: bg,    type: rect, pos_x: 0, pos_y: 0, width: 200, height: 280, color: 0x1A1A2E }
```

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
| `text_input` | Editable single-line text field. Keyboard focus, a movable cursor, text **selection** (mouse drag, Shift+arrows, Ctrl+A) and **clipboard** (Ctrl+C / Ctrl+V / Ctrl+X); UTF-8-aware. `secret` mode. Text longer than the field **scrolls horizontally** so the caret stays visible, and is clipped to the field's inner box (no wrapping — the field is always one line regardless of its `height`). See §5e. |
| `checkbox` | Toggleable box + label. State stored on the child; click flips. Use `width × height` to size the surrounding region; the box itself is `min(width, height)` square on the left, label fills the remaining width. When neither `border_color` nor `border_width` is set, the box falls back to a subtle default outline (so an unstyled checkbox is always visible); set them explicitly to override. |
| `radio` | Exclusive choice within a named `group`. Round toggle + label; visually identical layout to `checkbox` but rendered as a circle. Clicking sets THIS child checked and unchecks every other `radio` with the same `group` string across the entire scene. Use the same `is_checked` / `set_checked` API as checkbox for programmatic read/write. |
| `dropdown` | Single-choice selector. Options come from a pipe-separated `options:` string ("A\|B\|C") **or** from an `enum: TypeName` binding (labels auto-derived from the enum members). Click opens a popup list; selection is stored as an index. Read the label with `dropdown_value`, the index with `dropdown_index`, and — for an `enum:`-bound dropdown — the true enum member value with `dropdown_enum` (see §5b). |
| `bar` | Progress/gauge. A rounded track (`bg_color` + `border_*` + `corner_radius`) with a fill (`fill_color`) sized `value/max` of the width. Update at runtime via `set_bar_value` / `set_bar_max` / `set_bar_fill`. |
| `slider` | Interactive horizontal slider. Same track + fill as `bar`, with a drag-handle knob centered on the value-position. Mouse press inside the track grabs the handle; while held, cursor-x maps linearly to a value in `[min, max]`. Fires `on_change` whenever the value changes. Reads via `get_slider_value(child)`, writes via `set_slider_value`/`set_slider_min`/`set_slider_max`. The handle uses `fill_color` (or white if absent) with `hover_color`/`active_color` tints. |
| `container` | Dynamic list spawner. Bound to a `list<T>` on the instance's model via `bind_list:`; spawns one instance of `spawn: <Prefab>` per element and keeps the count synced with `.len`. `layout:` (`vflow`/`hflow`) + `gap:` control placement; `compact:` re-packs only visible rows; `scrollable:` gives it a **native scrollbar** (mouse wheel + draggable thumb, engine-windowed). See §5c. |
| `textbox` | Multi-line, scrollable, read-only text area (chat logs, event logs). Wraps on width and on `\n`/`\|`; native scrollbar (mouse wheel + draggable thumb). `tail: true` sticks the view to the bottom as content grows. Set the whole text at once with `set_text`. See §5f. |

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

---

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
