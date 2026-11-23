# Chapter 44.9 — UI test cases (scenario format)

A format for driving and checking a running user interface automatically: widgets
are addressed **by name**, not by pixel coordinates, and the check is made against
what was actually hit.

Building blocks: `input.post_*` (`/spec/44_0_input.md` §8) for input, the geometry getters
`.child_rect_*` and the hit trace `engine.prefab_trace_hits` (`/spec/44_6_prefabs.md` §5).

Distinction from `/spec/59_unit_testing.md`: those are unit tests of **one function** with
automatically generated stubs. This chapter is about an interaction scenario against the real
application, without stubs.

## 1. Why a format and not just aiml code

A scenario can be written in aiml as well — `post_mouse_move`, `update`, `post_mouse_down`,
`update`, … (§8.4). But then every test repeats the same frame choreography three times, and
whoever gets it wrong once ends up with a test that is green without having clicked. The
format keeps the choreography in **one** place, the driver.

The scenario is YAML (like aiml itself) and the driver reads it via `/spec/60_yaml.md` —
no generator, no second language.

## 2. Structure

```yaml
name: agent_ausfuehren
window: { width: 1280, height: 800 }
steps:
  - click:  "AgentsScene/agent_row_0"
  - expect_hit: "AgentsScene/agent_row_0"

  - focus:  "AgentsScene/cmd_input"
  - type:   "echo hallo"
  - expect_text: { child: "AgentsScene/cmd_input", value: "echo hallo" }

  - click:  "AgentsScene/run_btn"
  - wait_for: { child: "AgentsScene/out_box", contains: "hallo", timeout_ms: 5000 }

  - expect_no_hit: "AgentsScene/token_overlay_bg"
```

### 2.1 Widget address

`"<prefab-def>[<index>]/<child>"`, e.g. `"AgentsScene[0]/run_btn"` — exactly as
`engine.prefab_last_hit()` returns it. The index is the position in the scene, because the
def name is not unique when there are several instances of the same prefab. For `move`/`click`
`"<prefab-def>/<child>"` is enough if there is only one instance. The driver resolves the
address via `.child_rect_*` and computes the centre point from it.

If the address does not exist, **the step fails** — a scenario cannot accidentally click into
nothing and still pass.

## 3. Steps

| Step | Effect |
|---|---|
| `move: <addr>` | Cursor to the centre of the widget, one frame. |
| `move_at: [x, y]` | Cursor to absolute window coordinates (escape hatch when no widget is meant). |
| `click: <addr>` | The full three frames from §8.4: move, press, release. |
| `click_at: [x, y]` | The same at absolute coordinates. |
| `press: <addr>` / `release: <addr>` | Press and release separately — for drag gestures. |
| `drag: { from: <addr>, to: <addr> }` | Press, move in steps (so the drag threshold is exceeded), release. |
| `scroll: { at: <addr>, dy: <f32> }` | Scroll over a widget. |
| `focus: <addr>` | Set keyboard focus (`.focus`) without a click. |
| `type: "<text>"` | Text into the focused field (`input.post_text`). |
| `key: "<name>"` | One key, canonical names from §5 (`"Return"`, `"lctrl+c"` as a combination). |
| `wait_frames: <n>` | Let `n` frames run. |
| `wait_for: { … }` | Until a condition holds or `timeout_ms` expires (see §4). |
| `screenshot: "<file>"` | Save an image — for the layout comparison the hit trace cannot provide (§5). |

## 4. Checks

| Check | Meaning |
|---|---|
| `expect_hit: <addr>` | `engine.prefab_last_hit()` contains this address. |
| `expect_no_hit: <addr>` | It is **not** contained. Covers "another widget swallowed the click". |
| `expect_only_hit: <addr>` | It is the **only** one — catches the case where two overlapping widgets fire together (spec/44_6). |
| `expect_miss: true` | `prefab_last_hit()` is empty: the click hit nothing. Expressible as an expectation, not only as an accident. |
| `expect_text: { child, value }` | `.text_value(child)` is exactly `value`. |
| `expect_contains: { child, value }` | Substring check (text box output). |
| `expect_checked: { child, value }` | `.is_checked`. |
| `expect_visible: { child, value }` | Widget is visible / hidden. |
| `expect_focused: <addr>` | `.child_focused`. |

`wait_for` takes the same conditions plus `timeout_ms`; when it expires the step fails with
the condition in the error text.

**`expect_hit` is the important check.** A scenario that only checks end states cannot tell
"button does nothing" from "click never arrived". Only `prefab_last_hit()` separates the two:
it names the widgets that actually resolved the press. The check uses the query, not log
parsing — the log is for humans, the query is for tests.

## 5. What this format cannot check

The driver asks the application **where** a widget is and then clicks there. If the widget is
somewhere other than where the application believes — drawn wrongly, outside the window,
behind an element without a hit test — the test clicks along and notices nothing. This class
of error is caught only by an image comparison (`screenshot:`) or at the OS level
(`/spec/44_0_input.md` §8.5, not yet implemented).

What it does check: that wiring, handlers, focus flow, state transitions and hit resolution
including z-order are correct — the error classes that actually arise when a user interface
is reworked.

## 6. Driver

The driver is an aiml program: read the scenario via `/spec/60_yaml.md`, open the window,
work through the steps, read the hit trace along the way, report a summary at the end. A
scenario that never clicks does not pass — a run without a single resolved hit counts as a
failure, not as an empty pass.

The driver MUST set `engine.prefab_trace_hits(1)` and keep the frame order from §8.3
(inject after the frame's input update, before the app update).

## 7. Status

Format: defined here. Prerequisites **implemented and verified**: `input.post_*`,
`.child_rect_*`, `engine.prefab_trace_hits`, `engine.prefab_last_hit`, `engine.window_pos` and
`input.os_*` (evidence in `/spec/44_0_input.md` §8.5). Only the driver (§6) is still open.

The driver gets **one** switch for the channel: the same steps run either via
`input.post_*` (fast, headless, parallelisable) or via `input.os_*` (the real chain, its
own display).  The steps themselves stay the same, because the frame choreography lives in
the driver and not in the scenario. The OS level (XTEST on a dedicated Xvfb display) is
planned as the second channel and recorded as a TODO in `/spec/44_0_input.md` §8.5.
