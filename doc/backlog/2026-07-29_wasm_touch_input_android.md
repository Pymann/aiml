# WASM/browser: touch input for mobile (Android/iOS)

Date: 2026-07-29
Status: backlog

## Summary

The WASM/browser build (emscripten + WebGL2, `src/sys/engine_runtime.cpp`)
already renders and networks portably — but **input handling only knows
mouse + keyboard**. There is no touch/pointer event path. In an
Android/iOS browser **drag & drop therefore does not work** — and 9rooms_v2
is heavily drag-based (dragging cards from the hand into zones, attacking by drag,
equipping, moving). Desktop browsers (Windows/macOS/Linux) are fine, because there
mouse events arrive.

## What is missing

- Register emscripten touch events: `emscripten_set_touchstart_callback` /
  `touchmove` / `touchend` (or pointer events `pointerdown/move/up`, which unify mouse
  AND touch — probably the cleaner way).
- Map them onto the same internal input state as the mouse (position +
  "primary button down"), so that the existing drag logic (pick → move → drop)
  works without changes.
- Ignore multi-touch for now (only the first finger = cursor).
- Optional: long press → right-click equivalent (ability picker etc.), since
  touch has no second mouse button.

## Scope / location

- `src/sys/engine_runtime.cpp` (emscripten branch, `#if defined(__EMSCRIPTEN__)`)
  — canvas size/WebGL init are already handled there; add the touch callbacks.
- No AIML-side change needed if touch is mapped onto the mouse state.

## Context

Came up with the question "does 9rooms_v2 run as a browser game on Windows/Android?"
(2026-07-29): Windows browser yes, Android only to a limited extent — exactly because of the
missing touch input. Networking is portable (the WS client has an `#ifdef
__EMSCRIPTEN__` path in `ws_runtime.cpp`; only `ws://`/`wss://`, no raw TCP).
