# Chapter 44.0 — Input

> **Core idea:** The input system is built in three layers — analogous to Unreal Engine.
> The lowest layer provides raw GLFW state (polling + event queue),
> the top layer abstracts hardware through named virtual axes and actions.
> Game code exclusively queries `input.axis("forward")` and `input.action_pressed("jump")` —
> the concrete key bindings are freely configurable and rebindable without code changes.

---

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│  Mapping layer    input.axis("forward") → f32            │
│                   input.action_pressed("jump") → bool    │
│                   input.map_axis / input.map_action       │
├──────────────────────────────────────────────────────────┤
│  Polling layer    input.key_down("W") → bool             │
│                   input.mouse_delta()  → vec2            │
│                   input.drag_active(btn) → bool          │
│                   input.drag_delta_x/y(btn) → f32        │
│                                                          │
│  Queue layer      input.has_event() → bool               │
│                   input.next_event() → input_event       │
│                   Events: drag_start / drag_move /       │
│                           drag_end                       │
├──────────────────────────────────────────────────────────┤
│  GLFW             glfwGetKey / glfwGetCursorPos          │
│                   glfwSetScrollCallback (accumulated)    │
└──────────────────────────────────────────────────────────┘
```

`input.update()` is called once per frame (internally by `engine.swap()`).
It swaps prev/curr state, flushes the scroll accumulator into the queue, and sets
`mouse_delta` to the movement since the last frame.

---

## 1. Init & Lifecycle

| Call | Description |
|------|-------------|
| `input.init(win: ptr)` | Register GLFW window; call once before the game loop |
| `input.update()` | Swap frame state; called automatically by `engine.swap()` |
| `input.set_capture(on: bool)` | Hide cursor and lock it to the window (FPS look); `false` = visible |

---

## 2. Raw Polling

Queries GLFW internal state directly. Useful for debugging and edge cases;
prefer the mapping layer in game code.

### 2.1 Keyboard

| Call | Return | Description |
|------|--------|-------------|
| `input.key_down(key: string)` | `bool` | Key currently pressed |
| `input.key_pressed(key: string)` | `bool` | Key went down **this frame** (rising edge) |
| `input.key_released(key: string)` | `bool` | Key came up **this frame** (falling edge) |

### 2.2 Mouse

| Call | Return | Description |
|------|--------|-------------|
| `input.mouse_pos()` | `vec2` | Cursor position in pixels (window coordinates, origin top-left) |
| `input.mouse_delta()` | `vec2` | Cursor movement since last `update()` (good for FPS look) |
| `input.mouse_button(btn: i32)` | `bool` | Button currently pressed |
| `input.mouse_pressed(btn: i32)` | `bool` | Button went down this frame |
| `input.mouse_released(btn: i32)` | `bool` | Button came up this frame |
| `input.mouse_scroll()` | `vec2` | Scroll delta this frame (x=horizontal, y=vertical) |

Mouse button indices: `0` = left, `1` = right, `2` = middle.

### 2.3 Gamepad

| Call | Return | Description |
|------|--------|-------------|
| `input.gamepad_connected(pad: i32)` | `bool` | Gamepad with index `pad` connected? |
| `input.gamepad_axis(pad: i32, axis: i32)` | `f32` | Axis value −1.0 … +1.0 (with deadzone) |
| `input.gamepad_button(pad: i32, btn: i32)` | `bool` | Button pressed |
| `input.gamepad_pressed(pad: i32, btn: i32)` | `bool` | Button went down this frame |

Gamepad axes (GLFW_GAMEPAD_AXIS_*):
`0`=Left X, `1`=Left Y, `2`=Right X, `3`=Right Y, `4`=Left Trigger, `5`=Right Trigger.

Gamepad buttons (GLFW_GAMEPAD_BUTTON_*):
`0`=A/Cross, `1`=B/Circle, `2`=X/Square, `3`=Y/Triangle,
`4`=L1, `5`=R1, `6`=L2, `7`=R2,
`8`=Select/Back, `9`=Start/Options,
`10`=L3, `11`=R3,
`12`=Up, `13`=Right, `14`=Down, `15`=Left (D-Pad).

### 2.4 Drag & Drop

Drag-and-drop is detected as soon as a mouse button is held AND the cursor has moved more
than `threshold` pixels (default: 5.0) from the start position. The system tracks up to
`AIML_MAX_MOUSE` buttons simultaneously.

| Call | Return | Description |
|------|--------|-------------|
| `input.drag_active(btn: i32)` | `bool` | Drag currently active for button `btn` |
| `input.drag_started(btn: i32)` | `bool` | Drag started **this frame** |
| `input.drag_ended(btn: i32)` | `bool` | Drag ended **this frame** |
| `input.drag_origin_x(btn: i32)` | `f32` | X position where the drag began |
| `input.drag_origin_y(btn: i32)` | `f32` | Y position where the drag began |
| `input.drag_delta_x(btn: i32)` | `f32` | Total movement X since drag start |
| `input.drag_delta_y(btn: i32)` | `f32` | Total movement Y since drag start |
| `input.drag_set_payload(data: ptr)` | — | Attach user data to the active drag |
| `input.drag_get_payload()` | `ptr` | Read drag payload (null if none) |
| `input.drag_set_threshold(px: f32)` | — | Minimum movement in pixels (default: `5.0`) |

**Drag events** in the event queue (`input.next_event()`):

| Type | Description |
|------|-------------|
| `"drag_start"` | Drag began (`btn`, `pos`, `delta` set) |
| `"drag_move"` | Cursor moved during drag (`btn`, `pos`, `delta` set) |
| `"drag_end"` | Button released during drag (`btn`, `pos`, `delta` set) |

`ev.delta_x` / `ev.delta_y` = total movement since drag start (shortcut for `pos - origin`).

#### Example: Drag-and-Drop

```aiml
# Polling-based (recommended for real-time feedback):
- if: [{ call: [input.drag_started, 0] }, true]
  then:
    - call: [begin_drag, { call: [input.drag_origin_x, 0] }, { call: [input.drag_origin_y, 0] }]

- if: [{ call: [input.drag_active, 0] }, true]
  then:
    - call: [update_drag, { call: [input.drag_delta_x, 0] }, { call: [input.drag_delta_y, 0] }]

- if: [{ call: [input.drag_ended, 0] }, true]
  then:
    - call: [drop_item, { call: [input.mouse_pos_x] }, { call: [input.mouse_pos_y] }]

# Event-based (lossless for complex drag paths):
- while:
    condition: { call: [input.has_event] }
    body:
      - decl: [ev, input_event, { call: [input.next_event] }]
      - if: [{ eq: [ev.type, "drag_start"] }, true]
        then:
          - call: [on_drag_start, ev.pos_x, ev.pos_y]
      - if: [{ eq: [ev.type, "drag_move"] }, true]
        then:
          - call: [on_drag_move, ev.delta_x, ev.delta_y]
      - if: [{ eq: [ev.type, "drag_end"] }, true]
        then:
          - call: [on_drag_end, ev.pos_x, ev.pos_y]
```

---

## 3. Event Queue

Events are accumulated (also between frames) and consumed in FIFO order. Useful for
text input, menu navigation, and anything that must not miss events.

| Call | Return | Description |
|------|--------|-------------|
| `input.has_event()` | `bool` | Queue is not empty |
| `input.next_event()` | `input_event` | Consume and remove the next event |
| `input.clear_events()` | — | Clear the queue (e.g. on scene change) |

### `input_event` — Fields

```aiml
model: input_event
fields:
  - [type,   string]   # Type string (see below)
  - [key,    string]   # Key; set for key_down / key_up
  - [btn,    i32]      # Mouse button index; set for mouse_button_*
  - [pos,    vec2]     # Cursor position in pixels; set for mouse_move / mouse_button_*
  - [scroll, vec2]     # Scroll delta; set for mouse_scroll
  - [char_,  string]   # Unicode character (one code point); set for text
  - [delta_x, f32]     # Total delta X since drag start; set for drag_start / drag_move / drag_end
  - [delta_y, f32]     # Total delta Y since drag start
```

Event types (`type` field):

| Type | Description |
|------|-------------|
| `"key_down"` | Key pressed (`key` set) |
| `"key_up"` | Key released (`key` set) |
| `"mouse_move"` | Cursor moved (`pos` set) |
| `"mouse_button_down"` | Mouse button pressed (`btn`, `pos` set) |
| `"mouse_button_up"` | Mouse button released (`btn`, `pos` set) |
| `"mouse_scroll"` | Mouse wheel (`scroll` set) |
| `"text"` | Text input of a unicode character (`char_` set) |
| `"drag_start"` | Drag threshold exceeded (`btn`, `pos_x/y`, `delta_x/y` set) |
| `"drag_move"` | Cursor moved during drag (`btn`, `pos_x/y`, `delta_x/y` set) |
| `"drag_end"` | Button released during drag (`btn`, `pos_x/y`, `delta_x/y` set) |

### Example: Text Input Field

```aiml
- while:
    condition: { call: [input.has_event] }
    body:
      - decl: [ev, input_event, { call: [input.next_event] }]
      - if: [{ eq: [ev.type, "text"] }, true]
        then:
          - set: [buffer, { +: [buffer, ev.char_] }]
      - if: [{ eq: [ev.type, "key_down"] }, true]
        then:
          - if: [{ eq: [ev.key, "Backspace"] }, true]
            then:
              - call: [buffer.chop_last]
```

---

## 4. Mapping Layer (Main API)

### 4.1 Defining axes (`map_axis`)

An axis accumulates multiple sources into a single `f32` value (typically −1.0 … +1.0).
The final value is the **sum of all active sources**, clamped to −1 … +1.

| Call | Description |
|------|-------------|
| `input.map_axis(name: string, key: string, scale: f32)` | Key → axis; `scale` = contribution when pressed (e.g. `1.0` or `-1.0`) |
| `input.map_axis_mouse(name: string, component: string, scale: f32)` | Mouse delta → axis; `component`: `"x"` or `"y"` |
| `input.map_axis_gamepad(name: string, pad: i32, axis: i32, scale: f32)` | Gamepad stick → axis |

### 4.2 Defining actions (`map_action`)

An action is a binary event (on/off) — multiple sources can trigger the same action.

| Call | Description |
|------|-------------|
| `input.map_action(name: string, key: string)` | Key → action |
| `input.map_action_mouse(name: string, btn: i32)` | Mouse button → action |
| `input.map_action_gamepad(name: string, pad: i32, btn: i32)` | Gamepad button → action |

### 4.3 Removing mappings

| Call | Description |
|------|-------------|
| `input.unmap(name: string)` | Remove all sources for this name |
| `input.clear_mappings()` | Delete all mappings |

### 4.4 Querying axes and actions

| Call | Return | Description |
|------|--------|-------------|
| `input.axis(name: string)` | `f32` | Aggregated axis value; `0.0` if no mapping or no key active |
| `input.action_down(name: string)` | `bool` | At least one mapped source currently active |
| `input.action_pressed(name: string)` | `bool` | Action went active **this frame** |
| `input.action_released(name: string)` | `bool` | Action went inactive **this frame** |

---

## 5. Key Names

| Category | Names |
|----------|-------|
| Letters | `"A"`–`"Z"` (always uppercase) |
| Digits | `"0"`–`"9"` |
| Special keys | `"Space"`, `"Escape"`, `"Enter"`, `"Tab"`, `"Backspace"`, `"Delete"`, `"Insert"` |
| Modifiers | `"Shift"`, `"LShift"`, `"RShift"`, `"Ctrl"`, `"LCtrl"`, `"RCtrl"`, `"Alt"`, `"LAlt"`, `"RAlt"` |
| Navigation | `"Up"`, `"Down"`, `"Left"`, `"Right"`, `"Home"`, `"End"`, `"PageUp"`, `"PageDown"` |
| Function keys | `"F1"`–`"F12"` |
| Numpad | `"Num0"`–`"Num9"`, `"NumAdd"`, `"NumSub"`, `"NumMul"`, `"NumDiv"`, `"NumEnter"`, `"NumDecimal"` |
| Other | `"CapsLock"`, `"PrintScreen"`, `"Pause"`, `"Menu"` |

`"Shift"` is an alias for `"LShift"` — triggers when either shift key is pressed.
Likewise for `"Ctrl"` and `"Alt"`.

---

## 6. Complete Example: FPS Movement

```aiml
# ── Configure input mappings once ─────────────────────────────────────────────

- call: [input.init, win]

# Movement axes: WASD + left gamepad stick
- call: [input.map_axis, "forward", "W",  1.0]
- call: [input.map_axis, "forward", "S", -1.0]
- call: [input.map_axis, "strafe",  "D",  1.0]
- call: [input.map_axis, "strafe",  "A", -1.0]
- call: [input.map_axis_gamepad, "forward", 0, 1, -1.0]  # Left Y (inverted)
- call: [input.map_axis_gamepad, "strafe",  0, 0,  1.0]  # Left X

# Look axes: mouse delta + right gamepad stick
- call: [input.map_axis_mouse,   "look_x", "x", 0.1]
- call: [input.map_axis_mouse,   "look_y", "y", 0.1]
- call: [input.map_axis_gamepad, "look_x", 0, 2, 2.0]   # Right X
- call: [input.map_axis_gamepad, "look_y", 0, 3, 2.0]   # Right Y

# Actions
- call: [input.map_action, "jump",   "Space"]
- call: [input.map_action, "sprint", "Shift"]
- call: [input.map_action, "fire",   "LCtrl"]
- call: [input.map_action_mouse,     "fire", 0]           # also left mouse button
- call: [input.map_action_gamepad,   "jump", 0, 0]        # A/Cross

# Lock cursor for FPS look
- call: [input.set_capture, true]

# ── Game loop ──────────────────────────────────────────────────────────────────

- decl: [running, bool, true]
- while:
    condition: { call: [engine.running, win] }
    body:
      - decl: [dt,      f32,  { call: [engine.dt] }]

      # Mapping layer: virtual axes and actions
      - decl: [fwd,     f32,  { call: [input.axis,           "forward"] }]
      - decl: [side,    f32,  { call: [input.axis,           "strafe"]  }]
      - decl: [lx,      f32,  { call: [input.axis,           "look_x"]  }]
      - decl: [ly,      f32,  { call: [input.axis,           "look_y"]  }]
      - decl: [jumped,  bool, { call: [input.action_pressed, "jump"]    }]
      - decl: [firing,  bool, { call: [input.action_down,    "fire"]    }]
      - decl: [sprints, bool, { call: [input.action_down,    "sprint"]  }]

      # Menu via queue (no events are lost)
      - if: [{ call: [input.action_pressed, "Escape"] }, true]
        then:
          - call: [open_pause_menu]

      - call: [player_update, dt, fwd, side, lx, ly, jumped, firing, sprints]
      - call: [render]
      - call: [engine.swap, win]   # SwapBuffers + PollEvents + input.update()
```

---

## 7. Implementation Notes

### 7.1 Frame State Buffer

The C runtime holds two arrays of size `GLFW_KEY_LAST + 1`:
`curr_keys[]` and `prev_keys[]`. `input.update()` copies `curr → prev` and
then reads the new `curr` state via `glfwGetKey()`.

`key_pressed(k)` = `curr[k] && !prev[k]`
`key_released(k)` = `!curr[k] && prev[k]`

### 7.2 Scroll Accumulator

GLFW delivers scroll events only via a callback (`glfwSetScrollCallback`).
The runtime registers this once in `input.init()` and accumulates the deltas in
`scroll_accum`. `input.update()` transfers the accumulator into `scroll_this_frame`
and resets `scroll_accum` to `{0, 0}`.

### 7.3 Mouse Delta

`mouse_delta` = `curr_pos − prev_pos`, computed in `input.update()`.
In capture mode (`set_capture(true)`) the cursor is reset to the window center after
each frame (`glfwSetCursorPos`) to enable unlimited look.

### 7.4 Gamepad Deadzone

`input.gamepad_axis()` automatically applies a radial deadzone of `0.1`.
Values within the deadzone are set to `0.0`, values outside are linearly remapped to
the full range −1…+1.

### 7.5 Axis Aggregation

`input.axis(name)` sums all active sources and clamps to −1.0 … +1.0:
```
value = clamp(sum(source_i * scale_i for active source_i), -1.0, 1.0)
```
When opposite keys are pressed simultaneously (`W` + `S`) the result is `0.0`.
