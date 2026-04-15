# Kapitel 44.1 — Engine (Cross-Platform)

> **Leitgedanke:** Der `engine`-Namespace abstrahiert alle Plattform-spezifischen
> Mechanismen — Fenster-Erstellung, GL-Kontext, Game-Loop und Frame-Timing.
> Derselbe AIML-Code läuft unverändert auf Desktop (Linux/macOS/Windows),
> Browser (WebAssembly via Emscripten) und Android (NDK + GameActivity).
>
> Plattform-spezifische Details (GLFW-Callbacks, `emscripten_set_main_loop`,
> `ANativeWindow`) sind vollständig im C-Runtime versteckt.
> AIML-Usercode hat **keinen** `extern: "C"` GLFW-Block mehr für Fenster-Management.

---

## Plattform-Mapping

| AIML | Desktop | Browser (WASM) | Android |
|------|---------|----------------|---------|
| `engine.create_window` | `glfwCreateWindow` | Emscripten Canvas | GameActivity Window |
| `engine.run` | `while (!glfwWindowShouldClose)` | `emscripten_set_main_loop` + Asyncify | Android main loop |
| `engine.swap` | `glfwSwapBuffers` + `glfwPollEvents` | `emscripten_flush_main_loop` | `ANativeWindow_unlockAndPost` |
| `engine.running` | `!glfwWindowShouldClose` | Asyncify-Zustand | `app->destroyRequested == 0` |
| `engine.dt` | `glfwGetTime()` Delta | `performance.now()` Delta | `clock_gettime` Delta |
| GL-Kontext | GLFW + GLEW | Emscripten WebGL2 | EGL + OpenGL ES 3 |

---

## 1. Typen

### `window_handle`

Opaker Handle auf das plattformspezifische Fenster-Objekt.
Intern: `GLFWwindow*` (Desktop), Emscripten Canvas-Referenz (Web), `ANativeWindow*` (Android).

```
window_handle   i8*   Plattform-Fenster (ersetzte rohen GLFW-ptr in Usercode)
```

---

## 2. Fenster-Erstellung

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `engine.create_window(w: i32, h: i32, title: string)` | `window_handle` | Fenster + GL-Kontext erstellen; OpenGL 3.3 Core / OpenGL ES 3.0 |
| `engine.create_window_fullscreen(title: string)` | `window_handle` | Vollbild in nativer Auflösung |
| `engine.destroy_window(win: window_handle)` | — | Fenster und GL-Kontext freigeben |
| `engine.window_size(win: window_handle)` | `ivec2` | Aktuelle Fenstergröße in Pixel |
| `engine.set_title(win: window_handle, title: string)` | — | Fenstertitel ändern |

`engine.create_window` übernimmt intern:
1. GLFW/EGL/Canvas initialisieren
2. OpenGL-Version anfordern (3.3 Core auf Desktop, ES 3.0 auf Mobile/Web)
3. GL-Context aktuell machen (`glfwMakeContextCurrent` / `eglMakeCurrent`)
4. GLEW initialisieren (nur Desktop)
5. Depth-Test aktivieren
6. Viewport auf Fenstergröße setzen

**Kein `extern: "C"` GLFW-Block mehr nötig** für Fenster-Management.

---

## 3. GL-Kontext

| Aufruf | Beschreibung |
|--------|--------------|
| `engine.make_current(win: window_handle)` | GL-Context auf aktuellen Thread binden (Multi-Window / Multi-Thread) |
| `engine.set_vsync(on: bool)` | VSync ein-/ausschalten (`glfwSwapInterval`) |

---

## 4. Game-Loop

### 4.1 `engine.run` — empfohlener Cross-Platform-Einstiegspunkt

```aiml
- call: [engine.run, win, update, render]
```

| Parameter | Typ | Beschreibung |
|-----------|-----|--------------|
| `win` | `window_handle` | Ziel-Fenster |
| `update` | `fn<f32:void>` | Update-Callback; erhält Delta-Time in Sekunden |
| `render` | `fn<void:void>` | Render-Callback; wird nach update aufgerufen |

Intern pro Frame:
1. `dt` berechnen
2. `input.update()` aufrufen
3. `update(dt)` aufrufen
4. `render()` aufrufen
5. Buffer swappen + Events pollen
6. Frame-Limiter anwenden

Plattform-Verhalten:
- **Desktop**: blockierende `while`-Schleife bis `glfwWindowShouldClose`
- **Browser**: `emscripten_set_main_loop_arg` — gibt Kontrolle ans Browser-Event-Loop zurück; Asyncify ermöglicht identische AIML-Quelle
- **Android**: NDK-Event-Loop bis `destroyRequested`

### 4.2 Manueller Loop-Stil (Desktop + Android)

Alternativ zu `engine.run` kann der Loop direkt in AIML geschrieben werden.
Auf Desktop und Android äquivalent zu `engine.run`. **Nicht empfohlen für Browser**
(ohne Asyncify-Transformation schlägt das Rendering fehl).

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

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `engine.running(win: window_handle)` | `bool` | `false` wenn Fenster geschlossen oder `engine.quit` aufgerufen |
| `engine.swap(win: window_handle)` | — | SwapBuffers + PollEvents + Frame-Limiter |
| `engine.quit(win: window_handle)` | — | Loop-Ende signalisieren (setzt `running` auf `false`) |
| `engine.is_iconified(win: window_handle)` | `i32` | `1` wenn Fenster minimiert (GLFW_ICONIFIED), sonst `0`. Nutzbar für Idle-/Reduced-Work-Pfade in der Mainloop. Stub auf Emscripten/Android: immer `0`. |
| `engine.is_focused(win: window_handle)` | `i32` | `1` wenn Fenster Input-Fokus hat (GLFW_FOCUSED), sonst `0`. Stub auf Emscripten/Android: immer `1`. |
| `engine.is_visible(win: window_handle)` | `i32` | `1` wenn Fenster sichtbar (GLFW_VISIBLE), sonst `0`. Stub auf Emscripten/Android: immer `1`. |

### 4.3 Frame-Timing

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `engine.dt()` | `f32` | Delta-Time in Sekunden seit letztem Frame; beim ersten Frame `0.016` |
| `engine.time()` | `f64` | Verstrichene Zeit in Sekunden seit Programmstart |
| `engine.fps()` | `f32` | Durchschnittliche FPS der letzten 60 Frames |
| `engine.frame()` | `i64` | Gesamt-Frame-Zähler seit Start (beginnt bei 0) |
| `engine.target_fps(fps: i32)` | — | Frame-Rate-Limit setzen; `0` = unbegrenzt |

`engine.dt()` ist geclampt auf maximal `0.1` Sekunden — verhindert Sprünge nach
Lags oder Debugger-Pausen.

---

## 5. Plattform-Informationen

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `engine.platform()` | `string` | `"desktop"`, `"web"`, `"android"` |
| `engine.screen_size()` | `ivec2` | Physische Bildschirmauflösung in Pixel |
| `engine.dpi()` | `f32` | Display-Skalierungsfaktor (1.0 = normal, 2.0 = HiDPI/Retina) |
| `engine.is_mobile()` | `bool` | `true` auf Android; Shorthand für Touch-Optimierungen |
| `engine.is_web()` | `bool` | `true` im Browser |

---

## 6. Vollständiges Beispiel

Dieser Code läuft **unverändert** auf Desktop, Browser und Android:

```aiml
# ── Funktionen definieren ─────────────────────────────────────────────────────

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

# engine.create_window ersetzt den GLFW extern-Block vollständig
- decl: [win, window_handle, { call: [engine.create_window, 1280, 720, "My Game"] }]

# input.init nimmt jetzt window_handle statt rohem ptr
- call: [input.init, win]
- call: [input.map_axis,   "forward", "W",  1.0]
- call: [input.map_axis,   "forward", "S", -1.0]
- call: [input.map_axis,   "strafe",  "D",  1.0]
- call: [input.map_axis,   "strafe",  "A", -1.0]
- call: [input.map_action, "fire",    "Space"]
- call: [input.set_capture, true]

- call: [engine.target_fps, 60]

# engine.run ist der Cross-Platform-Einstiegspunkt für den Game-Loop
- call: [engine.run, win, update, render]

# Wird nach engine.run erreicht wenn Fenster geschlossen wird
- call: [engine.destroy_window, win]
```

---

## 7. Plattform-spezifisches Verhalten

### 7.1 Browser (WebAssembly)

- `engine.run` nutzt intern `emscripten_set_main_loop_arg` — der Browser-Event-Loop
  läuft weiter, Animationen und JS-Events bleiben funktional
- **Asyncify**: der WASM-Output wird mit Asyncify-Transformation kompiliert, sodass
  der manuelle `while:`-Stil ebenfalls im Browser funktioniert (transparente
  Coroutinen-Transformation durch den Emscripten-Linker)
- Fenstergröße = Canvas-Größe; `engine.screen_size()` = `window.innerWidth/Height`
- VSync via `requestAnimationFrame` (immer aktiv, `engine.target_fps` ignoriert)
- Dateisystem: virtuelles FS (Emscripten), Assets müssen vorher gepackt werden

### 7.2 Android

- `engine.create_window` wartet intern auf `APP_CMD_INIT_WINDOW` vom NDK
- Orientation-Changes: `engine.window_size` gibt die jeweils aktuelle Größe zurück;
  Viewport-Resize wird automatisch durch `engine.swap` behandelt
- Back-Button → `engine.quit`-Event (fangbar über `input.has_event` / `"app_quit"`)
- Touch-Input wird auf `input.mouse_*` gemappt (erster Touch = linke Maustaste);
  Multi-Touch über separaten `touch`-Namespace (zukünftig)

### 7.3 Desktop

- GLFW 3.x; unterstützt Linux (X11 + Wayland), macOS, Windows
- `engine.create_window` setzt OpenGL 3.3 Core Profile
- `engine.set_vsync(true)` → `glfwSwapInterval(1)`

---

## 8. Migration von raw GLFW

**Vorher** (plattform-spezifisch, nur Desktop):

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
- decl: [win, ptr, { call: [glfwCreateWindow, 800, 600, "Titel", null, null] }]
- call: [glfwMakeContextCurrent, win]
- call: [gl.init]

- while:
    condition: { call: [engine.running, win] }   # win war ptr
    body:
      - call: [render]
      - call: [glfwSwapBuffers, win]
      - call: [glfwPollEvents]

- call: [glfwTerminate]
```

**Nachher** (cross-platform):

```aiml
- decl: [win, window_handle, { call: [engine.create_window, 800, 600, "Titel"] }]
- call: [engine.run, win, update, render]
- call: [engine.destroy_window, win]
```

---

## 9. Implementierungshinweise

### 9.1 `window_handle` intern

```cpp
struct AimlWindow {
#if defined(__EMSCRIPTEN__)
    // Canvas-Selektor als String, z.B. "#canvas"
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

### 9.2 Delta-Time-Berechnung

```cpp
double now = /* glfwGetTime() / emscripten_get_now() / clock_gettime() */;
double raw_dt = now - win->last_time;
win->dt = (float)fmin(raw_dt, 0.1);  // clamp bei 100ms
win->last_time = now;
```

### 9.3 Frame-Limiter

Desktop: nach `render()` und `swap()` Restzeit via `time.sleep_ms` schlafen.
Browser: `requestAnimationFrame` limitiert ohnehin auf Display-Refresh-Rate.
Android: `ALooper_pollAll` mit Timeout.

### 9.4 Asyncify (Browser)

Der AIML-AOT-Compiler ruft beim Browser-Target den Emscripten-Linker mit
`-s ASYNCIFY -s ASYNCIFY_IMPORTS=[engine_swap_internal]` auf.
Dadurch kann `engine.swap` (das intern `emscripten_sleep(0)` aufruft) den
WASM-Stack "einfrieren" und dem Browser-Event-Loop Rechenzeit geben —
ohne Änderung am AIML-`while:`-Quellcode.
