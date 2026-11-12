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
| `input.init(win: window_handle)` | Register the window (from `engine.create_window`; a raw GLFW `ptr` from FFI is accepted too); call once before the game loop |
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
| `input.key_pressed(key: string)` | `bool` | Key went down **this frame** (rising edge). Idempotent: returns the same value however many times it is polled this frame. |
| `input.key_pressed_once(key: string)` | `bool` | Rising edge, **consumed on first read per frame**: the first poll this frame returns `true`, every later poll returns `false` until the edge resets next frame. Use for edge-triggered *actions* (skip, toggle, confirm) in code paths that may run more than once per frame — e.g. an input handler invoked from `render()`, which is often called several times per frame (once per reactive redraw after an action). Polling `key_pressed` there would fire the action once per redraw. |
| `input.key_released(key: string)` | `bool` | Key came up **this frame** (falling edge) |

**Key names** are the same on every target.  Letters are `"A"`..`"Z"` (always
uppercase, regardless of Shift), digits `"0"`..`"9"`, and the named keys are
`Space`, `Escape`, `Enter`, `Tab`, `Backspace`, `Delete`, `Insert`,
`Up`/`Down`/`Left`/`Right`, `Home`, `End`, `PageUp`, `PageDown`,
`LShift`/`RShift`, `LCtrl`/`RCtrl`, `LAlt`/`RAlt`, `F1`..`F12`.
Polling (`input.key_down(…)`) is **case-insensitive** (`"lctrl"` == `"LCtrl"`),
but the `key` field of a `key_down`/`key_up` **event** carries the canonical
spelling above — compare against that, not against browser spellings.
On wasm32 the browser's own names (`"c"`, `"ArrowLeft"`, `"Control"`, `" "`) are
normalized to this canon, and `Ctrl`/`Shift`/`Alt` state is taken from the
event's modifier flags, so a modifier held *before* the canvas got focus is
still seen.

Key state follows the same per-frame snapshot as mouse buttons (below): a key pressed and released
between two frame boundaries reports `key_pressed`/`key_pressed_once` **and** `key_released` in the
next frame; auto-repeat produces no further press edges.

> **Which press call?** Use `key_pressed` for *queries* (highlight a key, show a hint) — it is idempotent and safe to read from multiple systems. Use `key_pressed_once` for *actions that must happen exactly once per physical press*, especially when the polling site can run multiple times per frame. Because `key_pressed_once` consumes the edge, only the first reader in a frame sees it — do not use it if two independent systems must both react to the same key in the same frame.

### 2.2 Mouse

| Call | Return | Description |
|------|--------|-------------|
| `input.mouse_pos()` | `vec2` | Cursor position in pixels (window coordinates, origin top-left) |
| `input.mouse_delta()` | `vec2` | Cursor movement since last `update()` (good for FPS look) |
| `input.mouse_button(btn: i32)` | `bool` | Button currently pressed |
| `input.mouse_pressed(btn: i32)` | `bool` | Button went down this frame |
| `input.mouse_released(btn: i32)` | `bool` | Button came up this frame |
| `input.mouse_cancelled(btn: i32)` | `bool` | The press was **aborted** this frame — see below |
| `input.mouse_scroll()` | `vec2` | Scroll delta this frame (x=horizontal, y=vertical) |

Button state is a per-frame snapshot: every consumer in one frame sees the same `mouse_button` /
`mouse_pressed` / `mouse_released` values, even when events arrive mid-frame (e.g. during a
blocking network call). Edges are latched, never lost: a press and release that both happen
between two frame boundaries report `mouse_pressed` **and** `mouse_released` in the next frame
(`mouse_button` is then already false).

#### Cancel is not release (normative)

A press can end two ways, and they mean opposite things:

- **Release** — the user let go. The gesture *completed*: a click, a tap, a drop.
- **Cancel** — the press was taken away. The gesture *did not happen*.

`input.mouse_cancelled(btn)` is the second one, latched per frame exactly like
`mouse_pressed`/`mouse_released`. On a cancel the button state goes to
`false` and any drag for that button ends (`drag_cancelled`, §2.4), but
**`mouse_released` is NOT reported** — otherwise a cancel is indistinguishable
from a deliberate release and every consumer treats it as a completed gesture.

**Rule: a cancel must never be turned into a click, a tap or a drop.** Code that
acts on `mouse_released` therefore acts only on real releases; code that must
clean up on either outcome checks both.

Sources of a cancel:

| Source | Platform |
|--------|----------|
| `touchcancel` (system gesture, incoming call, browser takes over scrolling, notification overlay) | browser |
| A second finger landing while a one-finger press is active (two fingers = pan/zoom, not game input) | browser |
| Window focus loss / tab becoming hidden while a button is held (`blur`, `visibilitychange`) | browser, desktop |
| `input.post_touch_cancel()` | injection (§8.2) |

Nur der Fokusverlust des **Fensters** zaehlt. Ein Element INNERHALB der Seite,
das den Fokus abgibt, ist kein Abbruch — im Browser gibt genau das bei jedem
Klick auf ein Bedienelement das versteckte Tastatur-Feld der Weichtastatur-
Bruecke (§7, "Web (browser)") ab. Wer den `blur` in der Capture-Phase mitfaengt,
sieht diesen Element-Blur wie einen Fensterwechsel und bricht den gerade
begonnenen Druck ab; `mouseup` meldet die Freigabe dann nicht mehr (der Druck
gilt als nie geschehen), und der **erste Knopfdruck nach jeder Texteingabe
faellt aus**.

> **Why this is normative and not a detail.** Without a distinct cancel edge, an
> aborted touch produces a release, a release completes a drag, and a completed
> drag is a drop — so a system gesture the user never aimed at the application
> commits whatever the drop handler does. In a game that is a move nobody made.

Mouse button indices: `0` = left, `1` = right, `2` = middle.

Diese Reihenfolge ist **normativ und plattformunabhängig**. Sie ist die von
GLFW, also die des Desktop-Pfads. Andere Ereignisquellen nummerieren anders und
**müssen umsetzen**:

| Quelle | deren Reihenfolge | Umsetzung |
|---|---|---|
| GLFW (Desktop) | 0 links, 1 rechts, 2 Mitte | keine nötig |
| DOM `MouseEvent.button` (WASM) | 0 links, **1 Mitte**, **2 rechts** | 1 ↔ 2 tauschen |
| X11 / XTEST (`input.os_mouse_*`) | Button1 links, **Button2 Mitte**, **Button3 rechts** | 1 → Button3, 2 → Button2 |

Ohne diese Umsetzung bedeutet `input.mouse_pressed(2)` je nach Build „rechts"
oder „Mitte", und derselbe Quelltext verhält sich im Browser anders als auf dem
Desktop — ohne dass irgendwo ein Fehler auftritt. Genau das war bis 2026-10-20
der Fall (WASM- und XTEST-Pfad reichten ihren Index unverändert durch).

> **Pixels vs. design coordinates.** `input.mouse_pos_*` returns raw **window pixels**. Prefab positions (`set_pos`, `get_x`/`get_y`) live in **design coordinates** — the engine scales the design canvas to the window by `rel_scale`. `engine.hit_test`/`update_drag`/`update_hover` take pixel input and convert internally, so mixing them is fine; but if you place a widget **directly at the cursor** with `set_pos`, feeding raw pixels is wrong on a scaled window (the widget lands far off). Use the design-space cursor helpers instead:
>
> | Call | Return | Description |
> |------|--------|-------------|
> | `engine.mouse_design_x()` | `f32` | Cursor X in design coords (= pixel / `rel_scale`) |
> | `engine.mouse_design_y()` | `f32` | Cursor Y in design coords (= pixel / `rel_scale`) |
>
> Example — snap a card's centroid to the cursor (card is 140×196 in design units, top-left `set_pos` anchor):
>
> ```yaml
> - call: [card.set_pos, { -: [{ call: [engine.mouse_design_x] }, 70.0] },
>                         { -: [{ call: [engine.mouse_design_y] }, 98.0] }]
> ```

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
| `input.drag_ended(btn: i32)` | `bool` | Drag ended **this frame** by a release (completed → a drop) |
| `input.drag_cancelled(btn: i32)` | `bool` | Drag ended **this frame** by a cancel (§2.2) — **no** drop |

| `input.drag_origin_x(btn: i32)` | `f32` | X position where the drag began (= the press point, gültig ab `mousedown`, auch ohne aktiven Drag) |
| `input.drag_origin_y(btn: i32)` | `f32` | Y position where the drag began |
| `input.drag_start_x(btn: i32)` | `f32` | X des Punktes, an dem der Drag **begann** — gelatcht mit `drag_started` |
| `input.drag_start_y(btn: i32)` | `f32` | Y dieses Punktes |
| `input.drag_end_x(btn: i32)` | `f32` | X position of the release that ended the drag |
| `input.drag_end_y(btn: i32)` | `f32` | Y position of that release |
| `input.drag_delta_x(btn: i32)` | `f32` | Total movement X since drag start |
| `input.drag_delta_y(btn: i32)` | `f32` | Total movement Y since drag start |
| `input.drag_set_payload(data: ptr)` | — | Attach user data to the active drag |
| `input.drag_get_payload()` | `ptr` | Read drag payload (null if none) |
| `input.drag_set_threshold(px: f32)` | — | Minimum movement in pixels (default: `5.0`) |

**Die drei Drag-Flanken sind gelatcht, genau wie die Tastenflanken in §2.2.**
`drag_started` / `drag_ended` / `drag_cancelled` sind Ein-Frame-Kanten: sie
gelten für **den ganzen** Frame (jeder Leser sieht denselben Wert) und
verschwinden mit dem nächsten `update()`. Entsteht eine Kante *während* eines
Frames — ein Ereignis trifft ein, nachdem die Anwendung ihre Gesten schon
ausgewertet hat, was bei langen Frames (blockierender Netzaufruf, §44_5 §5)
der Normalfall ist — dann wird sie **aufbewahrt und im nächsten Frame
gemeldet**, nicht verworfen. Ohne diese Zusicherung geht ein überschrittener
Schwellwert gelegentlich verloren und der Drag startet nicht, obwohl der
Benutzer gezogen hat — ununterscheidbar von „die Karte lässt sich nicht
greifen".

Daraus folgt normativ: **ein neuer Druck löscht keine noch nicht ausgelieferte
Kante.** Die Ereignis-Handler laufen zwischen den Frames *und* mitten im Frame
(jeder blockierende Netzaufruf gibt an die Ereignisschleife ab). Lässt der
Benutzer eine gezogene Karte los und drückt — weil nichts passiert — gleich
wieder, liegen Release und Druck in derselben Lücke; dürfte der Druck die
`ended`-Kante überschreiben, wäre der Drop ersatzlos weg.

**Der Drop trägt seine eigene Position: `drag_end_x/y`.** Wird die `ended`-Kante
erst einen Frame später gemeldet, steht der Cursor längst woanders — unter
Umständen auf dem neuen Druckpunkt. Wer den Drop dann über `input.mouse_pos_*`
auflöst, trifft das falsche Ziel; das ist schlimmer als ein verlorener Drop,
weil es einen Spielzug auslöst, den niemand wollte. `drag_end_x/y` hält den
Punkt des Loslassens und wird **gemeinsam** mit der Kante gelatcht. Eine
Anwendung löst den Drop also so auf:

```yaml
- if: [{ call: [input.drag_ended, 0] }, true]
  then:
    - call: [drop_item, { call: [input.drag_end_x, 0] }, { call: [input.drag_end_y, 0] }]
```

`input.mouse_pos_*` ist dafür ungeeignet: diese Werte sind **live**, nicht pro
Frame gelatcht, und können sich innerhalb eines Frames ändern.

**`drag_origin_*` ist der lebende Druckpunkt, `drag_start_*` der gelatchte.**
Das ist kein Duplikat, sondern die Gegenstücke zweier verschiedener Fragen:

| Frage | Antwort |
|---|---|
| „Wo wurde **jetzt** gedrückt?" (Klick, Tipp) | `drag_origin_x/y` — gesetzt bei jedem `mousedown`, auch ohne Drag |
| „Wo begann der Drag, dessen `started`-Kante dieser Frame meldet?" | `drag_start_x/y` |
| „Wo wurde der Drag losgelassen, dessen `ended`-Kante dieser Frame meldet?" | `drag_end_x/y` |

`drag_origin_*` wird von **jedem** neuen Druck sofort überschrieben, die Kanten
werden aber erst im nächsten Frame gemeldet. Bei der Folge *Drücken A → ziehen →
loslassen → Drücken B*, alles in einer Frame-Lücke, liefert der nächste Frame
`drag_started` **und** `drag_ended` von Drag A — während `drag_origin_*` längst
auf B zeigt. Wer die Aufnahme daran hängt, greift die Karte an **B** statt an
**A**: ein Zug, den niemand begonnen hat. Darum wird der Startpunkt
**gemeinsam mit seiner Kante** gelatcht, genau wie der Loslasspunkt.

`engine.update_drag_gesture` (spec/44_6) nimmt deshalb an `drag_start_*` auf,
nicht an `drag_origin_*`.

`drag_cancelled` hat bewusst keine Position — ein Abbruch ist kein Drop (§2.2).

**Drag events** in the event queue (`input.next_event()`):

| Type | Description |
|------|-------------|
| `"drag_start"` | Drag began (`btn`, `pos`, `delta` set) |
| `"drag_move"` | Cursor moved during drag (`btn`, `pos`, `delta` set) |
| `"drag_end"` | Button released during drag (`btn`, `pos`, `delta` set) |
| `"drag_cancel"` | Drag aborted, not released (`btn`, `pos`, `delta` set) — never a drop |

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
    - call: [drop_item, { call: [input.drag_end_x, 0] }, { call: [input.drag_end_y, 0] }]

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

## 2.5 Clipboard

```yaml
- call: [input.clipboard_set, "text to copy"]
- decl: [pasted, string, { call: [input.clipboard_get] }]
```

| Builtin | Signature | Notes |
|---|---|---|
| `input.clipboard_set` | `input.clipboard_set(s: string) -> void` | Writes to the **system** clipboard. |
| `input.clipboard_get` | `input.clipboard_get() -> string` | Reads it; `""` when unavailable. |

`text_input` uses the same path for `Ctrl+C/X/V` (spec/44_6 §5e), so a copy made
in a widget and one made via `clipboard_set` end up in the same place.

**Platform behaviour**
- **Native:** backed by the GLFW window (`glfwGet/SetClipboardString`); `""`
  before `input.init` or without a window.
- **Browser (wasm32):** `clipboard_set` writes to the real system clipboard via
  `navigator.clipboard.writeText`, falling back to a hidden textarea plus
  `document.execCommand('copy')`.  Both need a **secure context** (https, or
  http on `localhost`/`127.0.0.1`) **and** a running **user gesture**.  The AIML
  frame loop runs *after* the key event, so a `clipboard_set` issued from
  AIML-side key handling would already be outside the gesture.  The runtime
  therefore handles `Ctrl+C`/`Ctrl+X` **inside** the browser's `keydown`: it
  asks the focused widget what it would copy and writes that synchronously,
  then suppresses the browser's own copy (which would otherwise overwrite the
  clipboard with the canvas's empty page selection).  A `clipboard_set` from a
  **button handler** is fine for the same reason — the click is the gesture.
- **Paste in the browser** cannot be read synchronously
  (`navigator.clipboard.readText` is async), so the runtime listens for the
  `paste` event instead: it stores the pasted text and only *then* queues the
  `Ctrl+V` key event, so the widget inserts the new content rather than the
  previously held one.  `clipboard_get` returns that in-page value.  A paste
  therefore needs the real `Ctrl+V` (or the browser's paste menu) — synthesizing
  it from AIML cannot pull the system clipboard.

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

### 3.1 One press, one event (normative)

A single physical key press puts **exactly one** `key_down` event in the queue, and a
single typed character **exactly one** `text` event. A consumer may therefore treat
"one event = one edit" and must not try to de-duplicate.

This is an invariant about the *producers*, and every platform backend has to honour
it:

- **Native (GLFW):** the key callback queues on `GLFW_PRESS` only; `GLFW_REPEAT`
  updates the polling state but queues nothing.
- **Web (browser):** the window-level `keydown`/`keypress` handlers are the single
  source for a hardware keyboard. The hidden DOM `<input>` that raises the mobile
  soft keyboard is a *fallback* for input that never produces a usable `keydown`
  (soft keyboards, IMEs, autocorrect, composition). It must stay silent while a
  named hardware key is in flight — otherwise one press is queued twice.

A doubled event is invisible in a screenshot and in any structural test: the widget
is correct, the queue is not. It shows up only as behaviour — a Backspace that eats
two characters, an arrow key that jumps two positions, a callback that fires twice.
Measure the count, do not reason about it (`tests/test_prefab_backspace.aiml`).

### 3.2 Queue vs. per-frame latch

The queue is consumed; the polling values are **not**. `input.mouse_scroll_y()`,
`input.mouse_button(…)`, `input.mouse_released(…)` and a prefab's
`child_clicked(…)` all return the same value to *every* reader within the frame.

That is the second shape of the same problem: one input, several consumers. A wheel
notch over a scrollable container that lies under an application-drawn viewport is
delivered to both — the list scrolls *and* the camera zooms. Nothing marks the input
as used, so whoever asks, acts. When two consumers can overlap, the application has
to decide by region (hit-test first, then act) rather than letting both read the
latch.

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

The C runtime holds three arrays of size `GLFW_KEY_LAST + 1`:
`curr_keys[]`, `prev_keys[]` and `consumed_press[]`. `input.update()` copies
`curr → prev`, clears `consumed_press`, and then reads the new `curr` state via
`glfwGetKey()`.

`key_pressed(k)` = `curr[k] && !prev[k]`
`key_released(k)` = `!curr[k] && prev[k]`
`key_pressed_once(k)` = `curr[k] && !prev[k] && !consumed_press[k]`, and sets
`consumed_press[k] = 1` on a `true` result — so only the first poll per frame
sees the edge. `consumed_press` is reset once per frame in `input.update()`.

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

---

## 8. Input Injection (`input.post_*`)

Lets a program feed cursor movement, button and key events into its **own** input
state, so an automated test can drive a real UI the way a user would.
See `/spec/44_9_ui_testing.md` for the test-case format built on this.

### 8.1 Why at this layer

Widgets do not read the event queue of §3 — the prefab runtime polls the frame
state (`mouse_pos`, `mouse_pressed`, `mouse_released`, `key_down`).  Injection
therefore writes **the same frame-state buffer and the same latches** that the
GLFW callbacks write (§7.1).  There is no second code path: whatever a widget,
a drag gesture or hand-written polling code does with a real event, it does
with an injected one.

Injection is **in-process**.  It does not touch the windowing system and does
not move the operating system's cursor, so a test cannot steal the pointer from
the person running it, and several tests can run in parallel.

> **Not covered by injection.** Everything *below* the application: window
> focus, window position, a compositor putting another window on top.  A test
> driven this way proves that the UI reacts correctly to input it receives — not
> that the input would have arrived.  See §8.5.

### 8.2 Calls

| Call | Description |
|------|-------------|
| `input.post_mouse_move(x: f32, y: f32)` | Place the cursor at window coordinates `(x, y)` (origin top-left), as a motion event would.  Feeds `mouse_pos` and, on the next frame, `mouse_delta`. |
| `input.post_mouse_down(btn: i32)` | Press button `btn` (0 = left): sets the current state **and** the press latch. |
| `input.post_mouse_up(btn: i32)` | Release button `btn`: sets the current state **and** the release latch. |
| `input.post_touch_cancel()` | Abort the active press on button 0: clears the current state and sets the **cancel** latch (§2.2) — never the release latch. Models `touchcancel`, the two-finger takeover and focus loss, so a scenario can prove that an aborted gesture produces no drop. |
| `input.post_scroll(dx: f32, dy: f32)` | Add to the scroll accumulator; drained by the next `update()` exactly like a real wheel event. |
| `input.post_key_down(name: string)` | Press a key.  `name` uses the canonical names of §5 (`"lctrl"`, `"Left"`, `"a"`). |
| `input.post_key_up(name: string)` | Release a key. |
| `input.post_text(s: string)` | Queue `s` as character input for the focused `text_input` — one `char` event per code point, as typing would. |
| `input.post_mouse_move_pending(x: f32, y: f32)` | wie `post_mouse_move`, schreibt aber in die **Ereignis-Schicht** (§8.3a) |
| `input.post_mouse_down_pending(btn: i32)` | wie `post_mouse_down`, Ereignis-Schicht |
| `input.post_mouse_up_pending(btn: i32)` | wie `post_mouse_up`, Ereignis-Schicht |

`btn` ist überall der **kanonische** Index aus §2 (`0`=links, `1`=rechts,
`2`=Mitte); die Injektion setzt nichts um, weil sie unterhalb der
Plattform-Adapter ansetzt.

All calls are no-ops when input was never initialised (`input.init`), and
`post_key_*` is a no-op for an unknown key name (it reports via `log.warn`
rather than failing, so a scenario with a typo is diagnosable).

### 8.3 Frame contract

`input.update()` clears the latches at the start of a frame (§7.1).  Injected
state is therefore visible to **the frame that follows the injection**, and only
to that frame:

```
engine.swap(win)     # input.update() clears the latches, then PollEvents
input.post_mouse_*   # inject
<app reads input>    # sees it
engine.swap(win)     # cleared again
```

`engine.swap` is where `input.update()` runs (`/spec/44_1_engine.md` §Loop), so a
driver MUST inject **after** the swap and before the application reads — not
before the swap, where the clear would discard it.

**One contract on every platform.** The two backends keep per-frame edges
differently — desktop clears latches in `update()` and the GLFW callbacks refill
them, the browser snapshots pending edges into the frame on `update()`. Taken
naively, injection would need the *opposite* ordering in the browser, and a
scenario written on the desktop would behave differently there without saying
so. The browser injection therefore writes the frame-visible slots directly
(`curr_*`, `frame_press/release`) and leaves the pending ones alone, so that an
injected edge fires exactly once and the rule above holds everywhere.

### 8.3a Die Ereignis-Schicht (`*_pending`)

Die Hooks aus §8.2 ohne Zusatz schreiben die **frame-sichtbaren** Slots:
injizieren, lesen, beim nächsten `update()` weg. Das ist der richtige Vertrag
für ein Szenario, das Bedienung nachspielt — aber es macht eine Schicht
unerreichbar.

Im Browser-Zweig schreiben die Ereignis-Handler nämlich nicht in die
frame-sichtbaren Slots, sondern in eine **Ereignis-Schicht** (`pend_*`), die
`update()` am Frame-Anfang in den Frame übernimmt. Diese Schicht trägt die
Zusicherung aus §2.3: eine Kante, die *während* eines Frames entsteht, wird
aufbewahrt und im nächsten Frame gemeldet. Und sie ist der Ort, an dem die
interessanten Fälle liegen, weil die Handler **mitten im Frame** laufen,
sobald ein blockierender Netzaufruf an die Ereignisschleife abgibt.

Die `*_pending`-Varianten tun genau das, was die Ereignis-Handler tun. Damit
kann ein Szenario eine echte Ereignisfolge nachspielen — insbesondere die, auf
die es ankommt: **Loslassen und erneutes Drücken in derselben Lücke.** Danach
`input.update()` aufrufen und prüfen, was ausgeliefert wird:

```yaml
# Drag läuft, dann Release UND neuer Druck, beides vor dem nächsten update
- call: [input.post_mouse_up_pending, 0]
- call: [input.post_mouse_down_pending, 0]
- call: [input.update]
# drag_ended MUSS 1 sein — der Druck darf die Kante nicht gelöscht haben (§2.3)
```

Ohne diese Hooks war genau diese Zusicherung von außen nicht prüfbar, und ein
Verstoß dagegen blieb über Monate unentdeckt. Canonical:
`tests/test_input_pending_layer_wasm.aiml`.

> Für Szenarien, die Bedienung nachspielen, bleiben die Hooks **ohne** Zusatz
> die richtige Wahl. Die `*_pending`-Varianten sind dafür da, den Frame-Vertrag
> selbst zu prüfen.

### 8.4 A click spans frames

`engine.update_hover(mx, my)` is the pass that resolves which widget is under the
cursor and fires the click (`/spec/44_6_prefabs.md`), so a driver calls it each
frame with the injected position.

A click is a press **and** a release inside the widget's hit rect
(`/spec/44_6_prefabs.md` §5), and the widget records the rect at press time so
the click survives a rebuild in between.  Press and release in the *same* frame
would therefore not be a faithful click, and there is deliberately **no**
`input.post_click`: the driver advances a frame between down and up.

```yaml
- call: [input.post_mouse_move, 412.0, 388.0]
- call: [engine.update_hover, 412.0, 388.0]       # cursor is there
- call: [engine.swap, win]
- call: [input.post_mouse_down, 0]
- call: [engine.update_hover, 412.0, 388.0]       # widget records the press
- call: [engine.swap, win]
- call: [input.post_mouse_up, 0]
- call: [engine.update_hover, 412.0, 388.0]       # click fires → child_clicked
```

### 8.5 OS-level injection (`input.os_*`)

The second channel injects **below** the application, into the X server, so it also covers window
focus, window geometry and overlapping windows. The application cannot tell such an event from a
real one.

| Call | Description |
|------|-------------|
| `input.os_available()` | `1` if a display could be opened **and** it has the XTEST extension. Everything below is a no-op otherwise, so a scenario can report "not available" instead of failing. |
| `input.os_mouse_move(x, y)` | Move the pointer to **screen** coordinates. |
| `input.os_mouse_down(btn)` / `input.os_mouse_up(btn)` | Press / release. `btn` ist der **kanonische** Index aus §2 (`0`=left, `1`=right, `2`=middle); der Adapter setzt auf X11s Button1/3/2 um. |
| `input.os_key_down(name)` / `input.os_key_up(name)` | Press / release a key by the §5 name. |
| `input.os_pointer_x()` / `input.os_pointer_y()` | Where the pointer actually is — the check that a move arrived before clicking. |

**Coordinates are screen coordinates**, while `.child_rect_*` gives **window** pixels. The caller
adds the window origin (`engine.window_pos`), because only the caller knows which window a rect
belongs to:

```yaml
- decl: [wpos, ivec2, { call: [engine.window_pos, win] }]
- decl: [scr_x, f32, { +: [{ cast: [wpos.x, f32] }, mid_x] }]
- call: [input.os_mouse_move, scr_x, scr_y]
```

**Run it on its own display.** XTEST moves the real, global cursor: a test sharing the session
would yank the pointer away from whoever is at the machine. Use `Xvfb :99` (declared as an apt
dependency, so `exodus pkg install` provides it) and point `DISPLAY` at it.

**Wayland has no XTEST**, by design. There the equivalents are `/dev/uinput` (root or group
`input`) or libei; neither is implemented. `os_available()` returns `0` there, which is not a test
failure.

#### Which channel proves what

| | `input.post_*` | `input.os_*` |
|---|---|---|
| Covers | everything from the application boundary inward: widget resolution, handlers, state machine | additionally the X server, window focus, window position, foreign windows on top |
| Needs a display | no | yes, and its own |
| Moves the real cursor | no | **yes** — hence the separate display |
| Parallel runs | yes | no (one cursor per display) |
| Browser/WASM | yes | not applicable |

Proven by `tests/test_input_injection_click.aiml` (in-process, desktop),
`tests/test_input_injection_wasm.aiml` (in-process, browser code path under node),
`tools/run_browser_test.sh` running the former in headless Chrome (in-process, real browser) and
`tests/test_input_injection_os.aiml` (XTEST on Xvfb).
