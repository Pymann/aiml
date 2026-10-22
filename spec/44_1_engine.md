# Chapter 44.1 — Engine (Cross-Platform)

> **Design principle:** The `engine` namespace abstracts all platform-specific
> mechanisms — window creation, GL context, game loop, and frame timing.
> The same AIML code runs unchanged on desktop (Linux/macOS/Windows),
> browser (WebAssembly via Emscripten), and Android (NDK + GameActivity).
>
> Platform-specific details (GLFW callbacks, `emscripten_set_main_loop`,
> `ANativeWindow`) are fully hidden in the C runtime.
> AIML user code no longer has an `extern: "C"` GLFW block for window management.

---

## Platform Mapping

| AIML | Desktop | Browser (WASM) | Android |
|------|---------|----------------|---------|
| `engine.create_window` | `glfwCreateWindow` | Emscripten Canvas | GameActivity Window |
| `engine.run` | `while (!glfwWindowShouldClose)` | manual Asyncify `while` + `emscripten_sleep(16)` — **not** `emscripten_set_main_loop` (§7.1) | Android main loop |
| `engine.swap` | `glfwSwapBuffers` + `glfwPollEvents` | `emscripten_flush_main_loop` | `ANativeWindow_unlockAndPost` |
| `engine.running` | `!glfwWindowShouldClose` | Asyncify state | `app->destroyRequested == 0` |
| `engine.dt` | `glfwGetTime()` delta | `performance.now()` delta | `clock_gettime` delta |
| GL context | GLFW + GLEW | Emscripten WebGL2 | EGL + OpenGL ES 3 |

---

## 1. Types

### `window_handle`

Opaque handle to the platform-specific window object.
Internally: `GLFWwindow*` (desktop), Emscripten canvas reference (web), `ANativeWindow*` (Android).

```
window_handle   i8*   Platform window (replaces raw GLFW ptr in user code)
```

---

## 2. Window Creation

| Call | Return | Description |
|------|--------|-------------|
| `engine.create_window(w: i32, h: i32, title: string)` | `window_handle` | Create window + GL context; OpenGL 3.3 Core / OpenGL ES 3.0 |
| `engine.create_window_fullscreen(title: string)` | `window_handle` | Fullscreen at native resolution |
| `engine.destroy_window(win: window_handle)` | — | Release window and GL context |
| `engine.window_size(win: window_handle)` | `ivec2` | Current window size in pixels |
| `engine.set_title(win: window_handle, title: string)` | — | Change window title |

`engine.create_window` handles internally:
1. Initialize GLFW/EGL/Canvas
2. Request OpenGL version (3.3 Core on desktop, ES 3.0 on mobile/web)
3. Make GL context current (`glfwMakeContextCurrent` / `eglMakeCurrent`)
4. Initialize GLEW (desktop only)
5. Enable depth test
6. Set viewport to window size

**No `extern: "C"` GLFW block needed** for window management.

---

## 3. GL Context

| Call | Description |
|------|-------------|
| `engine.make_current(win: window_handle)` | Bind GL context to current thread (multi-window / multi-thread) |
| `engine.set_vsync(on: bool)` | Enable/disable VSync (`glfwSwapInterval`) |

---

## 4. Game Loop

### 4.1 `engine.run` — Recommended Cross-Platform Entry Point

```aiml
- call: [engine.run, win, update, render]
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `win` | `window_handle` | Target window |
| `update` | `fn<f32:void>` | Update callback; receives delta time in seconds |
| `render` | `fn<void:void>` | Render callback; called after update |

Per frame internally:
1. Compute `dt`
2. Call `input.update()`
3. Call `update(dt)`
4. Call `render()`
5. Swap buffers + poll events
6. Apply frame limiter

Platform behavior:
- **Desktop**: blocking `while` loop until `glfwWindowShouldClose`
- **Browser**: a manual `while` loop that calls the frame body and then yields with
  `emscripten_sleep(16)` (one Asyncify unwind/rewind per frame). It deliberately does
  **not** use `emscripten_set_main_loop[_arg]` — see §7.1 for the reason and the price.
- **Android**: NDK event loop until `destroyRequested`

### 4.2 Manual Loop Style (Desktop + Android)

Alternative to `engine.run`, the loop can be written directly in AIML.
Equivalent to `engine.run` on desktop and Android. **Not recommended for browser**
(rendering will fail without Asyncify transformation).

```aiml
- while:
    condition: { call: [engine.running, win] }
    body:
      - decl: [dt, f32, { call: [engine.dt] }]
      - call: [input.update]
      - call: [update, dt]
      - call: [render]
      - call: [engine.swap, win]
```

| Call | Return | Description |
|------|--------|-------------|
| `engine.running(win: window_handle)` | `bool` | `false` when window is closed or `engine.quit` was called |
| `engine.swap(win: window_handle)` | — | SwapBuffers + PollEvents + frame limiter |
| `engine.quit(win: window_handle)` | — | Signal loop end (sets `running` to `false`) |
| `engine.is_iconified(win: window_handle)` | `i32` | `1` if window is minimized (GLFW_ICONIFIED), else `0`. Usable for idle/reduced-work paths in the main loop. Stub on Emscripten/Android: always `0`. |
| `engine.is_focused(win: window_handle)` | `i32` | `1` if window has input focus (GLFW_FOCUSED), else `0`. Stub on Emscripten/Android: always `1`. |
| `engine.is_visible(win: window_handle)` | `i32` | `1` if window is visible (GLFW_VISIBLE), else `0`. Stub on Emscripten/Android: always `1`. |

### 4.3 Frame Timing

Event waiting (`engine.wait_events`), framebuffer pixel dimensions
(`engine.framebuffer_size`) and clock reset (`engine.reset_frame_clock`) for idle
rendering are specified in [43.5 §8](43_5_stylized_rendering.md). The target-FPS cap
counts render and swap time against the frame budget. Existing VSync and target-FPS
APIs below are reused, not replaced.

| Call | Return | Description |
|------|--------|-------------|
| `engine.dt()` | `f32` | Delta time in seconds since last frame; `0.016` on first frame |
| `engine.time()` | `f64` | Elapsed time in seconds since program start |
| `engine.fps()` | `f32` | Average FPS over the last 60 frames |
| `engine.frame()` | `i64` | Total frame counter since start (begins at 0) |
| `engine.target_fps(fps: i32)` | — | Set frame rate limit; `0` = unlimited |

`engine.dt()` is clamped to a maximum of `0.1` seconds — prevents jumps after
lags or debugger pauses.

---

## 5. Platform Information

| Call | Return | Description |
|------|--------|-------------|
| `engine.platform()` | `string` | `"desktop"`, `"web"`, `"android"` |
| `engine.screen_size()` | `ivec2` | Physical screen resolution in pixels |
| `engine.dpi()` | `f32` | Display scaling factor (1.0 = normal, 2.0 = HiDPI/Retina) |
| `engine.is_mobile()` | `bool` | `true` on Android; shorthand for touch optimizations |
| `engine.is_web()` | `bool` | `true` in browser |

---

## 6. Complete Example

This code runs **unchanged** on desktop, browser, and Android:

```aiml
# ── Define functions ──────────────────────────────────────────────────────────

- def:
    name: update
    args: [[dt, f32]]
    body:
      - decl: [fwd,  f32,  { call: [input.axis,           "forward"] }]
      - decl: [side, f32,  { call: [input.axis,           "strafe"]  }]
      - decl: [fire, bool, { call: [input.action_pressed, "fire"]    }]
      - call: [player_move, dt, fwd, side]
      - if: [{ ==: [fire, true] }, true]
        then:
          - call: [player_shoot]

- def:
    name: render
    args: []
    body:
      - call: [gl.clear_all, 0.05, 0.05, 0.1, 1.0]
      - call: [scene_draw]

# ── Setup ─────────────────────────────────────────────────────────────────────

# engine.create_window fully replaces the GLFW extern block
- decl: [win, window_handle, { call: [engine.create_window, 1280, 720, "My Game"] }]

# input.init now takes window_handle instead of raw ptr
- call: [input.init, win]
- call: [input.map_axis,   "forward", "W",  1.0]
- call: [input.map_axis,   "forward", "S", -1.0]
- call: [input.map_axis,   "strafe",  "D",  1.0]
- call: [input.map_axis,   "strafe",  "A", -1.0]
- call: [input.map_action, "fire",    "Space"]
- call: [input.set_capture, true]

- call: [engine.target_fps, 60]

# engine.run is the cross-platform entry point for the game loop
- call: [engine.run, win, update, render]

# Reached after engine.run when window is closed
- call: [engine.destroy_window, win]
```

---

## 7. Platform-Specific Behavior

### 7.1 Browser (WebAssembly)

- `engine.run` drives the loop **itself**, with a manual Asyncify `while`:

  ```c
  while (!win->quit_requested) {
      web_loop_tick(&ctx);     // dt + update + render + input snapshot
      emscripten_sleep(16);    // the ONE Asyncify yield per frame
  }
  ```

  It must **not** be "simplified" back to `emscripten_set_main_loop[_arg]`.
  Mixing Emscripten's rAF-driven main-loop scheduler with `-sASYNCIFY` and
  WebSocket traffic makes the render loop **silently die**: the frame body simply
  stops being called — no exception, no abort, `Asyncify.state == 0`, while the
  browser event loop keeps running. Reproduced by isolation: an empty frame loops
  forever, a frame that issues a WS send and then returns to the rAF scheduler
  dies, the same send followed by an `emscripten_sleep` survives. Full write-up
  and the isolation table: [`doc/wasm_frame_loop.md`](../doc/wasm_frame_loop.md).
  This is load-bearing, not an accident.
- **The price, and it is not small.** The sleep is *additive*, so the frame period
  is `work + 16 ms`, never `max(16 ms, work)`:

  | work per frame | resulting fps |
  |---|---|
  | ~1 ms | ~58 |
  | ~3 ms | ~52 |
  | ~18 ms | ~29 |

  So 16 ms of work yields 30 fps here where a rAF-driven loop would still yield
  60, and **60 fps is unreachable by construction**. Presentation is not coupled
  to the display refresh, so even a steady frame rate can judder against the
  compositor. In a background tab `setTimeout` is throttled to ~1 Hz (rAF would
  pause cleanly), so the session keeps being pumped slowly rather than stopping —
  background timeout and reconnect behaviour are therefore undefined. Nested
  `setTimeout` is clamped to ≥4 ms by browsers, which also applies to the 2 ms
  poll ticks inside blocking calls such as `session.send_and_await`.
- **Asyncify**: the WASM output is compiled with Asyncify transformation, so the
  manual `while:` style also works in the browser (transparent coroutine
  transformation by the Emscripten linker)
- Window size = canvas size; `engine.screen_size()` = `window.innerWidth/Height`
- `engine.target_fps` is ignored; the cadence is the `emscripten_sleep(16)` above
- Filesystem: virtual FS (Emscripten), assets must be packed beforehand

> **Offen: der rAF-Pfad.** Der Zielzustand sind browsergesteuerte Frames
> (`emscripten_set_main_loop_arg` oder `requestAnimationFrame` direkt) — damit
> fällt der additive 16-ms-Posten weg und die Darstellung hängt am Bildschirmtakt.
> Vorbedingung ist, dass **kein** suspendierender Aufruf mehr im Frame liegt:
> `session.send_and_await`, `ws.recv`/`ws.poll`/`ws.connect` und der synchrone
> Artwork-Pfad müssen vorher aus der Frameloop heraus (asynchrone Requests,
> inkrementeller WS-Empfang, Asset-Aufträge mit Platzhalter). Erst danach ist der
> Schleifenwechsel prüfbar; ein isolierter Austausch reproduziert den oben
> beschriebenen Schleifen-Tod. Zwischenschritt ohne Vertragsänderung: nur das
> **restliche** Framebudget schlafen (monotone Zeit) statt fester 16 ms.

### 7.2 Android

- `engine.create_window` internally waits for `APP_CMD_INIT_WINDOW` from NDK
- Orientation changes: `engine.window_size` returns the current size at any time;
  viewport resize is handled automatically by `engine.swap`
- Back button → `engine.quit` event (catchable via `input.has_event` / `"app_quit"`)
- Touch input is mapped to `input.mouse_*` (first touch = left mouse button);
  multi-touch via separate `touch` namespace (future)

### 7.3 Desktop

- GLFW 3.x; supports Linux (X11 + Wayland), macOS, Windows
- `engine.create_window` sets OpenGL 3.3 Core Profile
- `engine.set_vsync(true)` → `glfwSwapInterval(1)`

---

## 8. Migration from Raw GLFW

**Before** (platform-specific, desktop only):

```aiml
- extern: "C"
  defs:
    - def: { name: glfwInit,               args: [],                                                          return: i32 }
    - def: { name: glfwCreateWindow,       args: [[w, i32], [h, i32], [title, string], [mon, ptr], [sh, ptr]], return: ptr }
    - def: { name: glfwMakeContextCurrent, args: [[win, ptr]],                                                return: void }
    - def: { name: glfwSwapBuffers,        args: [[win, ptr]],                                                return: void }
    - def: { name: glfwPollEvents,         args: [],                                                          return: void }
    - def: { name: glfwTerminate,          args: [],                                                          return: void }

- decl: [ok,  i32, { call: [glfwInit] }]
- decl: [win, ptr, { call: [glfwCreateWindow, 800, 600, "Title", null, null] }]
- call: [glfwMakeContextCurrent, win]
- call: [gl.init]

- while:
    condition: { call: [engine.running, win] }   # win was ptr
    body:
      - call: [render]
      - call: [glfwSwapBuffers, win]
      - call: [glfwPollEvents]

- call: [glfwTerminate]
```

**After** (cross-platform):

```aiml
- decl: [win, window_handle, { call: [engine.create_window, 800, 600, "Title"] }]
- call: [engine.run, win, update, render]
- call: [engine.destroy_window, win]
```

---

## 9. Implementation Notes

### 9.1 `window_handle` Internals

```cpp
struct AimlWindow {
#if defined(__EMSCRIPTEN__)
    // Canvas selector as string, e.g. "#canvas"
    char canvas_id[64];
#elif defined(__ANDROID__)
    struct android_app* app;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
#else
    GLFWwindow* glfw;
#endif
    int width, height;
    double last_time;
    double dt;
    int64_t frame_count;
    float target_frame_ms;  // 0 = unlimited
};
```

### 9.2 Delta Time Calculation

```cpp
double now = /* glfwGetTime() / emscripten_get_now() / clock_gettime() */;
double raw_dt = now - win->last_time;
win->dt = (float)fmin(raw_dt, 0.1);  // clamp at 100ms
win->last_time = now;
```

### 9.3 Frame Limiter

Desktop: after `render()` and `swap()`, sleep remaining time via `time.sleep_ms`.
Browser: a fixed `emscripten_sleep(16)` **after** the frame body, so the period is
`work + 16 ms` and `engine.target_fps` has no effect (§7.1).
Android: `ALooper_pollAll` with timeout.

### 9.4 Asyncify (Browser)

Browser targets are linked with `-sASYNCIFY=1`, which lets any
`emscripten_sleep` "freeze" the WASM stack and yield to the browser event loop —
without any changes to the AIML `while:` source code. The yields live in
`engine.run`'s own loop (§7.1), in `engine.swap`, and in the blocking `ws`/
`mirror` calls.

> **Ist-Zustand, nicht Zielzustand:** real gebaut wird mit **globalem**
> `-sASYNCIFY=1` **ohne** `ASYNCIFY_ONLY`/`ASYNCIFY_ADD`-Liste (siehe
> `9rooms_v2/exodus-client.json`). Asyncify instrumentiert damit jede Funktion,
> die es nicht als „erreicht nie einen Sleep" beweisen kann — bei `-O0` ist der
> Aufrufgraph maximal unpräzise, also praktisch alles. Das kostet Codegröße und
> Laufzeit auf *allen* instrumentierten Funktionen, nicht nur auf den
> suspendierenden. Eine ermittelte `ASYNCIFY_ONLY`-Liste (die suspendierenden
> Pfade plus ihre Aufrufer) ist der offene Optimierungspunkt; JSPI (`-sJSPI`)
> wäre die Variante, die die Instrumentierung ganz ersetzt.

## Offen: `engine.run` trägt keinen Kontext — und erzwingt damit ein Global

`engine.run` nimmt die Rückrufe als **nackte Funktionszeiger** mit fester Signatur:

```cpp
void aiml_engine_run(void* win_ptr, void* update_fn, void* render_fn);   // kein ctx
```

Der Update-Rückruf bekommt nur `dt` und hat keinen Platz für seinen Empfänger. Jede App, die
in `update` an ihren eigenen Zustand will, braucht deshalb **ein Global** als Brücke — auch
wenn sie sonst keinen einzigen hat.

**Gemessen 2026-10-14 an zwei unabhängig gewachsenen Clients:**

| Projekt | Globals im Client | davon erzwungen durch diesen Rückruf |
|---|---|---|
| 9rooms | 2 | 1 (`g_app`) |
| cosmos | 27 → (Migration läuft) | 1 (`g_cosmos_app`) |

Beide landen aus demselben Grund bei demselben einen Global. Das ist kein Projektfehler,
sondern eine Lücke dieser Schnittstelle — und sie steht quer zu spec/21 §6.2 Regel 5, die
modulweite Globals als Geruch meldet.

**Vorgeschlagene Form** (additiv, die Drei-Argument-Fassung bleibt gültig):

```cpp
void aiml_engine_run(void* win_ptr, void* update_fn, void* render_fn, void* ctx);
//  update_fn wird (f32 dt, void* ctx)
```

```yaml
- def:
    name: on_frame
    args: [[dtf, f32], [app, ref<MyApp>]]     # Empfänger reist mit
    body: [{ call: [app.frame] }]

- def:
    name: main
    body:
      - decl: [app, ptr<MyApp>, { new: MyApp }]      # lokal in main, kein Global
      - call: [engine.run, app.win, on_frame, on_render, app]
```

Der WASM-Pfad hat das Gegenstück bereits: `emscripten_set_main_loop_arg` statt
`emscripten_set_main_loop`. Zu ändern wären die Signatur, der native Schleifen-Pfad, der
WASM-Pfad und die Builtin-Deklaration; heutige Aufrufer (je einer in cosmos und 9rooms)
bleiben gültig.

**Entscheid 2026-10-14 (User):** jetzt **nicht** umsetzen — der cosmos-Client wird zuerst nach
dem 9rooms-Muster aufgebaut (Kompositionswurzel). Dieses TODO bleibt der Weg, das letzte
Global danach ersatzlos zu streichen.

