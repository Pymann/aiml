# Kapitel 44.0 — Input

> **Leitgedanke:** Das Input-System ist dreischichtig aufgebaut — analog zu Unreal Engine.
> Die unterste Schicht liefert rohen GLFW-Zustand (Polling + Event-Queue),
> die oberste Schicht abstrahiert Hardware durch benannte virtuelle Achsen und Aktionen.
> Spielcode fragt ausschließlich `input.axis("forward")` und `input.action_pressed("jump")` ab —
> die konkrete Tastenbelegung ist frei konfigurierbar und rebindbar ohne Code-Änderung.

---

## Architektur

```
┌──────────────────────────────────────────────────────────┐
│  Mapping-Layer    input.axis("forward") → f32            │
│                   input.action_pressed("jump") → bool    │
│                   input.map_axis / input.map_action       │
├──────────────────────────────────────────────────────────┤
│  Polling-Layer    input.key_down("W") → bool             │
│                   input.mouse_delta()  → vec2            │
│                   input.drag_active(btn) → bool          │
│                   input.drag_delta_x/y(btn) → f32        │
│                                                          │
│  Queue-Layer      input.has_event() → bool               │
│                   input.next_event() → input_event       │
│                   Events: drag_start / drag_move /       │
│                           drag_end                       │
├──────────────────────────────────────────────────────────┤
│  GLFW             glfwGetKey / glfwGetCursorPos          │
│                   glfwSetScrollCallback (akkumuliert)    │
└──────────────────────────────────────────────────────────┘
```

`input.update()` wird einmal pro Frame aufgerufen (intern durch `engine.swap()`).
Es tauscht prev/curr-State, leert den Scroll-Akkumulator in die Queue und setzt
`mouse_delta` auf die Bewegung seit dem letzten Frame.

---

## 1. Init & Lifecycle

| Aufruf | Beschreibung |
|--------|--------------|
| `input.init(win: ptr)` | GLFW-Fenster registrieren; einmalig vor dem Game-Loop |
| `input.update()` | Frame-State tauschen; wird von `engine.swap()` automatisch aufgerufen |
| `input.set_capture(on: bool)` | Cursor verstecken und am Fenster sperren (FPS-Look); `false` = sichtbar |

---

## 2. Raw Polling

Fragt den GLFW-internen State direkt ab. Nützlich für Debugging und Edge-Cases;
im Spielcode bevorzugt Mapping-Layer verwenden.

### 2.1 Tastatur

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `input.key_down(key: string)` | `bool` | Taste aktuell gedrückt |
| `input.key_pressed(key: string)` | `bool` | Taste ging in **diesem Frame** runter (rising edge) |
| `input.key_released(key: string)` | `bool` | Taste kam in **diesem Frame** hoch (falling edge) |

### 2.2 Maus

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `input.mouse_pos()` | `vec2` | Cursor-Position in Pixel (Fenster-Koordinaten, Ursprung oben-links) |
| `input.mouse_delta()` | `vec2` | Cursor-Bewegung seit letztem `update()` (gut für FPS-Look) |
| `input.mouse_button(btn: i32)` | `bool` | Button aktuell gedrückt |
| `input.mouse_pressed(btn: i32)` | `bool` | Button ging diesen Frame runter |
| `input.mouse_released(btn: i32)` | `bool` | Button kam diesen Frame hoch |
| `input.mouse_scroll()` | `vec2` | Scroll-Delta dieses Frames (x=horizontal, y=vertikal) |

Mouse-Button-Indizes: `0` = Links, `1` = Rechts, `2` = Mitte.

### 2.3 Gamepad

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `input.gamepad_connected(pad: i32)` | `bool` | Gamepad mit Index `pad` verbunden? |
| `input.gamepad_axis(pad: i32, axis: i32)` | `f32` | Achswert −1.0 … +1.0 (mit Deadzone) |
| `input.gamepad_button(pad: i32, btn: i32)` | `bool` | Button gedrückt |
| `input.gamepad_pressed(pad: i32, btn: i32)` | `bool` | Button diesen Frame runter |

Gamepad-Achsen (GLFW_GAMEPAD_AXIS_*):
`0`=Left X, `1`=Left Y, `2`=Right X, `3`=Right Y, `4`=Left Trigger, `5`=Right Trigger.

Gamepad-Buttons (GLFW_GAMEPAD_BUTTON_*):
`0`=A/Cross, `1`=B/Circle, `2`=X/Square, `3`=Y/Triangle,
`4`=L1, `5`=R1, `6`=L2, `7`=R2,
`8`=Select/Back, `9`=Start/Options,
`10`=L3, `11`=R3,
`12`=Up, `13`=Right, `14`=Down, `15`=Left (D-Pad).

### 2.4 Drag & Drop

Drag-and-Drop wird erkannt, sobald eine Maustaste gehalten UND der Cursor mehr als `threshold`
Pixel (Standard: 5.0) von der Startposition entfernt wurde. Das System trackt bis zu `AIML_MAX_MOUSE`
Buttons gleichzeitig.

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `input.drag_active(btn: i32)` | `bool` | Drag läuft gerade für Button `btn` |
| `input.drag_started(btn: i32)` | `bool` | Drag startete in **diesem Frame** |
| `input.drag_ended(btn: i32)` | `bool` | Drag endete in **diesem Frame** |
| `input.drag_origin_x(btn: i32)` | `f32` | X-Position wo der Drag begann |
| `input.drag_origin_y(btn: i32)` | `f32` | Y-Position wo der Drag begann |
| `input.drag_delta_x(btn: i32)` | `f32` | Gesamtbewegung X seit Drag-Start |
| `input.drag_delta_y(btn: i32)` | `f32` | Gesamtbewegung Y seit Drag-Start |
| `input.drag_set_payload(data: ptr)` | — | Benutzer-Daten an den aktiven Drag hängen |
| `input.drag_get_payload()` | `ptr` | Drag-Payload lesen (null wenn keiner) |
| `input.drag_set_threshold(px: f32)` | — | Mindest-Bewegung in Pixel (Standard: `5.0`) |

**Drag-Events** in der Event-Queue (`input.next_event()`):

| Typ | Beschreibung |
|-----|--------------|
| `"drag_start"` | Drag begann (`btn`, `pos`, `delta` gesetzt) |
| `"drag_move"` | Cursor bewegt während Drag (`btn`, `pos`, `delta` gesetzt) |
| `"drag_end"` | Button losgelassen während Drag (`btn`, `pos`, `delta` gesetzt) |

`ev.delta_x` / `ev.delta_y` = Gesamtbewegung seit Drag-Start (Shortcut für `pos - origin`).

#### Beispiel: Drag-and-Drop

```aiml
# Polling-basiert (Empfehlung für Echtzeit-Feedback):
- if: [{ call: [input.drag_started, 0] }, true]
  then:
    - call: [begin_drag, { call: [input.drag_origin_x, 0] }, { call: [input.drag_origin_y, 0] }]

- if: [{ call: [input.drag_active, 0] }, true]
  then:
    - call: [update_drag, { call: [input.drag_delta_x, 0] }, { call: [input.drag_delta_y, 0] }]

- if: [{ call: [input.drag_ended, 0] }, true]
  then:
    - call: [drop_item, { call: [input.mouse_pos_x] }, { call: [input.mouse_pos_y] }]

# Event-basiert (verlustfrei für komplexe Drag-Pfade):
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

## 3. Event-Queue

Events werden akkumuliert (auch zwischen Frames) und in FIFO-Reihenfolge konsumiert.
Nützlich für Text-Input, Menü-Navigation und alles, das keine Ereignisse verpassen darf.

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `input.has_event()` | `bool` | Queue nicht leer |
| `input.next_event()` | `input_event` | Nächstes Event konsumieren und entfernen |
| `input.clear_events()` | — | Queue leeren (z.B. beim Szenen-Wechsel) |

### `input_event` — Felder

```aiml
model: input_event
fields:
  - [type,   string]   # Typ-String (siehe unten)
  - [key,    string]   # Taste; gesetzt bei key_down / key_up
  - [btn,    i32]      # Mouse-Button-Index; gesetzt bei mouse_button_*
  - [pos,    vec2]     # Cursor-Position in Pixel; gesetzt bei mouse_move / mouse_button_*
  - [scroll, vec2]     # Scroll-Delta; gesetzt bei mouse_scroll
  - [char_,  string]   # Unicode-Zeichen (ein Codepoint); gesetzt bei text
  - [delta_x, f32]     # Gesamtdelta X seit Drag-Start; gesetzt bei drag_start / drag_move / drag_end
  - [delta_y, f32]     # Gesamtdelta Y seit Drag-Start
```

Event-Typen (`type`-Feld):

| Typ | Beschreibung |
|-----|--------------|
| `"key_down"` | Taste gedrückt (`key` gesetzt) |
| `"key_up"` | Taste losgelassen (`key` gesetzt) |
| `"mouse_move"` | Cursor bewegt (`pos` gesetzt) |
| `"mouse_button_down"` | Mouse-Button gedrückt (`btn`, `pos` gesetzt) |
| `"mouse_button_up"` | Mouse-Button losgelassen (`btn`, `pos` gesetzt) |
| `"mouse_scroll"` | Mausrad (`scroll` gesetzt) |
| `"text"` | Text-Input eines Unicode-Zeichens (`char_` gesetzt) |
| `"drag_start"` | Drag-Schwellwert überschritten (`btn`, `pos_x/y`, `delta_x/y` gesetzt) |
| `"drag_move"` | Cursor bewegt während Drag (`btn`, `pos_x/y`, `delta_x/y` gesetzt) |
| `"drag_end"` | Button losgelassen während Drag (`btn`, `pos_x/y`, `delta_x/y` gesetzt) |

### Beispiel: Text-Eingabefeld

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

## 4. Mapping-Layer (Haupt-API)

### 4.1 Achsen definieren (`map_axis`)

Eine Achse akkumuliert mehrere Quellen zu einem `f32`-Wert (typischerweise −1.0 … +1.0).
Der Endwert ist die **Summe aller aktiven Quellen**, geclampd auf −1 … +1.

| Aufruf | Beschreibung |
|--------|--------------|
| `input.map_axis(name: string, key: string, scale: f32)` | Taste → Achse; `scale` = Beitrag wenn gedrückt (z.B. `1.0` oder `-1.0`) |
| `input.map_axis_mouse(name: string, component: string, scale: f32)` | Mouse-Delta → Achse; `component`: `"x"` oder `"y"` |
| `input.map_axis_gamepad(name: string, pad: i32, axis: i32, scale: f32)` | Gamepad-Stick → Achse |

### 4.2 Aktionen definieren (`map_action`)

Eine Aktion ist ein binäres Ereignis (on/off) — mehrere Quellen können dieselbe Aktion auslösen.

| Aufruf | Beschreibung |
|--------|--------------|
| `input.map_action(name: string, key: string)` | Taste → Aktion |
| `input.map_action_mouse(name: string, btn: i32)` | Mouse-Button → Aktion |
| `input.map_action_gamepad(name: string, pad: i32, btn: i32)` | Gamepad-Button → Aktion |

### 4.3 Mappings entfernen

| Aufruf | Beschreibung |
|--------|--------------|
| `input.unmap(name: string)` | Alle Quellen für diesen Namen entfernen |
| `input.clear_mappings()` | Alle Mappings löschen |

### 4.4 Achsen und Aktionen abfragen

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `input.axis(name: string)` | `f32` | Aggregierter Achswert; `0.0` wenn kein Mapping oder keine Taste aktiv |
| `input.action_down(name: string)` | `bool` | Mindestens eine gemappte Quelle aktuell aktiv |
| `input.action_pressed(name: string)` | `bool` | Aktion ging **diesen Frame** an |
| `input.action_released(name: string)` | `bool` | Aktion ging **diesen Frame** aus |

---

## 5. Key-Namen

| Kategorie | Namen |
|-----------|-------|
| Buchstaben | `"A"`–`"Z"` (immer Großbuchstabe) |
| Ziffern | `"0"`–`"9"` |
| Sonderzeichen | `"Space"`, `"Escape"`, `"Enter"`, `"Tab"`, `"Backspace"`, `"Delete"`, `"Insert"` |
| Modifikatoren | `"Shift"`, `"LShift"`, `"RShift"`, `"Ctrl"`, `"LCtrl"`, `"RCtrl"`, `"Alt"`, `"LAlt"`, `"RAlt"` |
| Navigation | `"Up"`, `"Down"`, `"Left"`, `"Right"`, `"Home"`, `"End"`, `"PageUp"`, `"PageDown"` |
| Funktionstasten | `"F1"`–`"F12"` |
| Numpad | `"Num0"`–`"Num9"`, `"NumAdd"`, `"NumSub"`, `"NumMul"`, `"NumDiv"`, `"NumEnter"`, `"NumDecimal"` |
| Sonstige | `"CapsLock"`, `"PrintScreen"`, `"Pause"`, `"Menu"` |

`"Shift"` ist ein Alias für `"LShift"` — löst aus, wenn eine der beiden Shift-Tasten gedrückt ist.
Analog für `"Ctrl"` und `"Alt"`.

---

## 6. Vollständiges Beispiel: FPS-Bewegung

```aiml
# ── Input-Mappings einmalig konfigurieren ─────────────────────────────────────

- call: [input.init, win]

# Bewegungsachsen: WASD + linker Gamepad-Stick
- call: [input.map_axis, "forward", "W",  1.0]
- call: [input.map_axis, "forward", "S", -1.0]
- call: [input.map_axis, "strafe",  "D",  1.0]
- call: [input.map_axis, "strafe",  "A", -1.0]
- call: [input.map_axis_gamepad, "forward", 0, 1, -1.0]  # Left Y (invertiert)
- call: [input.map_axis_gamepad, "strafe",  0, 0,  1.0]  # Left X

# Look-Achsen: Maus-Delta + rechter Gamepad-Stick
- call: [input.map_axis_mouse,   "look_x", "x", 0.1]
- call: [input.map_axis_mouse,   "look_y", "y", 0.1]
- call: [input.map_axis_gamepad, "look_x", 0, 2, 2.0]   # Right X
- call: [input.map_axis_gamepad, "look_y", 0, 3, 2.0]   # Right Y

# Aktionen
- call: [input.map_action, "jump",   "Space"]
- call: [input.map_action, "sprint", "Shift"]
- call: [input.map_action, "fire",   "LCtrl"]
- call: [input.map_action_mouse,     "fire", 0]           # auch Linke Maustaste
- call: [input.map_action_gamepad,   "jump", 0, 0]        # A/Cross

# Cursor sperren für FPS-Look
- call: [input.set_capture, true]

# ── Game-Loop ──────────────────────────────────────────────────────────────────

- decl: [running, bool, true]
- while:
    condition: { call: [engine.running, win] }
    body:
      - decl: [dt,      f32,  { call: [engine.dt] }]

      # Mapping-Layer: virtuelle Achsen und Aktionen
      - decl: [fwd,     f32,  { call: [input.axis,           "forward"] }]
      - decl: [side,    f32,  { call: [input.axis,           "strafe"]  }]
      - decl: [lx,      f32,  { call: [input.axis,           "look_x"]  }]
      - decl: [ly,      f32,  { call: [input.axis,           "look_y"]  }]
      - decl: [jumped,  bool, { call: [input.action_pressed, "jump"]    }]
      - decl: [firing,  bool, { call: [input.action_down,    "fire"]    }]
      - decl: [sprints, bool, { call: [input.action_down,    "sprint"]  }]

      # Menü via Queue (kein Ereignis geht verloren)
      - if: [{ call: [input.action_pressed, "Escape"] }, true]
        then:
          - call: [open_pause_menu]

      - call: [player_update, dt, fwd, side, lx, ly, jumped, firing, sprints]
      - call: [render]
      - call: [engine.swap, win]   # SwapBuffers + PollEvents + input.update()
```

---

## 7. Implementierungshinweise

### 7.1 Frame-State-Buffer

Der C-Runtime hält zwei Arrays der Größe `GLFW_KEY_LAST + 1`:
`curr_keys[]` und `prev_keys[]`. `input.update()` kopiert `curr → prev` und
liest dann via `glfwGetKey()` den neuen `curr`-State ein.

`key_pressed(k)` = `curr[k] && !prev[k]`
`key_released(k)` = `!curr[k] && prev[k]`

### 7.2 Scroll-Akkumulator

GLFW liefert Scroll-Events nur über einen Callback (`glfwSetScrollCallback`).
Der Runtime registriert diesen einmalig in `input.init()` und akkumuliert die
Deltas in `scroll_accum`. `input.update()` überträgt den Akkumulator in
`scroll_this_frame` und setzt `scroll_accum` auf `{0, 0}` zurück.

### 7.3 Maus-Delta

`mouse_delta` = `curr_pos − prev_pos`, berechnet in `input.update()`.
Im Capture-Modus (`set_capture(true)`) wird der Cursor nach jedem Frame zur
Fenstermitte zurückgesetzt (`glfwSetCursorPos`) um unbegrenztes Look zu ermöglichen.

### 7.4 Gamepad-Deadzone

`input.gamepad_axis()` wendet automatisch eine radiale Deadzone von `0.1` an.
Werte innerhalb der Deadzone werden auf `0.0` gesetzt, außerhalb linear auf
den vollen Bereich −1…+1 remappt.

### 7.5 Axis-Aggregation

`input.axis(name)` summiert alle aktiven Quellen und clampd auf −1.0 … +1.0:
```
value = clamp(sum(source_i * scale_i for active source_i), -1.0, 1.0)
```
Bei gleichzeitig gedrückten entgegengesetzten Tasten (`W` + `S`) ergibt sich `0.0`.
