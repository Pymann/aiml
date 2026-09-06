# WASM frame loop: manual Asyncify while-loop (NOT emscripten_set_main_loop)

Status: **load-bearing** — do not "simplify" this back to `emscripten_set_main_loop`.
Applies to: every WASM app built on the AIML engine (`engine.run`).

## TL;DR

On the WASM/Emscripten target `aiml_engine_run` drives the frame loop with a
**manual Asyncify loop**:

```c
while (!win->quit_requested) {
    web_loop_tick(&ctx);     // clear+update+draw+input for one frame
    emscripten_sleep(16);    // ~60fps; the ONE Asyncify yield per frame
}
```

It must **not** use `emscripten_set_main_loop[_arg]`. Mixing Emscripten's
rAF-driven main-loop scheduler with `-sASYNCIFY` and WebSocket traffic makes the
render loop **silently die**: `web_loop_tick` simply stops being called — no
exception, no abort, `Asyncify.state == 0`, and the browser event loop keeps
running (a JS `setInterval` still fires). See `src/sys/engine_runtime.cpp`.

## The failure it fixes

Symptom: after the first screen that talks to the server over the WebSocket
(e.g. the lobby), the canvas freezes. The card editor showed the same as a
black screen. Reachable only on some entry paths — visiting the deck editor
first "cured" it for the rest of the session.

Verified by isolation (all under `emscripten_set_main_loop` + `-sASYNCIFY`):

| Frame does...                                   | Result             |
|-------------------------------------------------|--------------------|
| nothing (empty frame)                           | loops forever      |
| a WS send, then returns to the rAF scheduler    | **loop dies**      |
| a WS send, then an `emscripten_sleep` same frame| survives           |

Key observations that rule out the usual suspects:
- Not the scene/render, not the response decode, not the data — an empty lobby
  renders forever; the freeze needs a WS round-trip.
- Not a busy hang / infinite loop — a JS `setInterval` watchdog keeps firing
  during the "freeze", so the main thread is idle; the loop just stopped being
  scheduled.
- Not a stuck Asyncify unwind — `Asyncify.state == 0` at the stall.
- Not a C++ exception — a `try/catch` around the frame callback catches nothing.
- `aiml_ws_send` is a **synchronous** JS call (`emscripten_websocket_send_*`,
  no `emscripten_sleep`). The blocking waits that DO unwind Asyncify live in the
  recv/poll/connect path (`aiml_ws_recv_raw`, `aiml_ws_poll`, connect).

## Why it happens (mechanism)

Emscripten's `Browser.mainLoop` re-arms `requestAnimationFrame` **after** the
callback returns. `-sASYNCIFY` implements "blocking" by unwinding the whole C
stack back to the browser and rewinding it on a later event. WebSocket
callbacks (`onopen`/`onmessage`/`onclose`) re-enter wasm from the browser event
loop. When a frame touches the socket and then hands control straight back to
the rAF scheduler **without an Asyncify yield of its own**, the scheduler and the
Asyncify stack-switching stop composing and the next rAF is never armed. The
operative rule that always held in testing:

> A frame that touches the WebSocket must perform an `emscripten_sleep`
> (Asyncify unwind/rewind) before control returns to Emscripten's rAF scheduler.

The manual while-loop satisfies this **structurally and unconditionally**: there
is no Emscripten rAF scheduler to lose, and every iteration ends in exactly one
`emscripten_sleep`. Blocking `send_and_await` and fire-only `send_rpc` are then
both safe, so real async polling (send + poll, no in-frame block) works.

This is the engine-level root fix. It is why the exact reason a blocking
in-frame round-trip (deck editor) "warmed" the old `set_main_loop` path no longer
matters — that whole fragile path is gone.

## Two companion rules (also in engine_runtime.cpp / the app frame)

1. **Clear right before the draw, not at frame start.** With a manual loop a
   blocking mid-frame send yields *after* the frame started. If `gl.clear_all`
   ran at the top, the compositor can show the cleared (background-colored)
   canvas during the yield → a flash on **every** send. Put `gl.clear_all`
   immediately before `engine.draw_scene` so the last complete frame stays up
   during any mid-frame yield.

2. **`preserveDrawingBuffer = EM_TRUE`** on the WebGL context. The manual loop is
   `emscripten_sleep`-paced (setTimeout), not rAF-synced, so the compositor is
   not frame-locked to our draws; preserving the buffer makes it always show the
   last finished frame instead of an in-between one.

## Browser mouse coords (hover/hit-test offset)

`input_runtime.cpp` registers `mousemove`/`mouseup` on `EMSCRIPTEN_EVENT_TARGET_WINDOW`
(so a drag keeps tracking when the cursor leaves the canvas), but `mousedown`/
`wheel` on `#canvas`. For a **window-target** event Emscripten sets
`EmscriptenMouseEvent.targetX == clientX` (viewport-relative, **no** canvas
offset). The canvas is centered + letterboxed, so using `targetX` shifts hover /
hit-testing by the letterbox margin — hover fires "a bit next to" the widget
while clicks (from the canvas-target `mousedown`) are correct.

Fix (`bin_event_xy`): always derive coords from `clientX/clientY` **minus the
canvas `getBoundingClientRect()`** (via `EM_ASM_DOUBLE`), then scale CSS→backing.
This is correct for both window- and canvas-registered callbacks. Do **not** go
back to `e->targetX/targetY`.

## Safety net left in place (lean)

A cheap watchdog stays in the runtime so a future regression names itself
instead of freezing silently:
- `aiml_rt_bump_frame()` / `aiml_rt_frame_counter()` — per-frame heartbeat counter.
- `aiml_rt_set_phase()` / `aiml_rt_rpc_phase()` — a breadcrumb naming the current
  blocking call (`send_and_await` method, `ws.recv_raw`, `mirror.session_update`).
- `aiml_rpc_stall_trace()` — `emscripten_get_callstack` dump on RPC
  timeout / slow / decode-failure only (error path, no per-frame cost).
- `try/catch` around the frame callback logging the phase.
- A JS `setInterval` in the web shell (`client/web/shell.html`) that, when the
  frame counter stops advancing, logs `rpc_phase` + `Asyncify.state`.

The expensive diagnostics used during the hunt (per-frame `[hb]` / `[rx]`
prints, per-instance render breadcrumbs) were removed — do not reintroduce them
in the hot path.

## Do NOT

- Do not switch WASM back to `emscripten_set_main_loop` for "vsync" — it
  reintroduces the freeze the moment a frame does WS I/O.
- Do not move `gl.clear_all` back to the top of the frame.
- Do not add blocking `send_and_await` calls in the per-frame render path where a
  fire-and-poll async request will do — blocking round-trips still stall the
  frame for one RTT (visible as a hitch), even though they no longer freeze.
