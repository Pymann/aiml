# Kapitel 44.6 — Prefab System (Scene Graph + Sprite Rendering)

> **Leitgedanke:** Prefabs sind wiederverwendbare visuelle Entity-Templates,
> definiert in YAML. Der Compiler registriert sie beim Start; Runtime erzeugt
> Instanzen und rendert alle in sortierter Z-Order.

---

## 1. Prefab-Definition (YAML)

```aiml
- prefab:
    name: CardFace
    width: 200
    height: 280
    texture: "assets/frame.png"
    draggable: true
    children:
      - { name: title, type: label, x: 100, y: 250, anchor: center, font_size: 11, color: 0xFFFFFF }
      - { name: art,   type: sprite, x: 15, y: 100, width: 170, height: 120 }
      - { name: bg,    type: rect, x: 0, y: 0, width: 200, height: 280, color: 0x1A1A2E }
```

### Child-Typen

| Typ | Beschreibung |
|-----|-------------|
| `sprite` | Texturiertes Quad (benötigt `width`, `height`) |
| `label` | Textzeile (benötigt `font_size`, `color`; optional `anchor`) |
| `rect` | Einfarbiges Rechteck (benötigt `width`, `height`, `color`) |

---

## 2. Typ: `prefab_instance`

Opaker Handle (`i8*`) auf eine lebende Prefab-Instanz.

```aiml
- decl: [card, prefab_instance, { call: [engine.instantiate, "CardFace"] }]
```

---

## 3. Scene Lifecycle

| Aufruf | Beschreibung |
|--------|-------------|
| `engine.prefab_scene_init(w: i32, h: i32)` | Scene initialisieren (Shader, Quad-VAO, Projektion). Nach GL-Context + `ui.init()` aufrufen. |
| `engine.prefab_set_font(font: ui_font)` | Font für Label-Children setzen. |
| `engine.instantiate(name: string) -> prefab_instance` | Neue Instanz eines registrierten Prefabs erzeugen. |
| `engine.draw_scene()` | Alle sichtbaren Instanzen in Z-Order rendern. Nach `gl.clear_all()`, vor `engine.swap()`. |

---

## 4. Transform-Methoden (Instanz)

| Methode | Signatur | Beschreibung |
|---------|----------|-------------|
| `.set_pos(x, y)` | `(f32, f32) -> void` | Position setzen |
| `.get_x()` / `.get_y()` | `-> f32` | Position abfragen |
| `.set_scale(s)` | `(f32) -> void` | Uniform-Scale (setzt `scale_x = scale_y = s`) |
| `.get_scale()` | `-> f32` | Gibt `scale_x` zurueck |
| `.set_rotation(r)` | `(f32) -> void` | Rotation in Radians |
| `.get_rotation()` | `-> f32` | Rotation abfragen |
| `.set_z(z)` | `(i32) -> void` | Z-Order (hoeher = weiter vorn) |
| `.get_z()` | `-> i32` | Z-Order abfragen |
| `.set_visible(v)` | `(i32) -> void` | Sichtbarkeit (0 = unsichtbar) |
| `.get_visible()` | `-> i32` | Sichtbarkeit abfragen |
| `.set_alpha(a)` | `(f32) -> void` | Transparenz (0.0-1.0) |
| `.get_alpha()` | `-> f32` | Transparenz abfragen |
| `.set_draggable(d)` | `(i32) -> void` | Drag-and-Drop aktivieren/deaktivieren |
| `.get_width()` / `.get_height()` | `-> f32` | Definitionsgroesse des Prefabs |

---

## 5. Child-Property-Methoden

| Methode | Signatur | Beschreibung |
|---------|----------|-------------|
| `.set_text(child, text)` | `(string, string) -> void` | Text eines Label-Children setzen |
| `.set_texture(child, tex)` | `(string, gl_texture) -> void` | Textur eines Sprite-Children setzen |
| `.set_color(child, color)` | `(string, i32) -> void` | Farbe eines Children setzen (Label-Textfarbe, Rect-Fuellfarbe) |
| `.set_child_visible(child, v)` | `(string, i32) -> void` | Sichtbarkeit eines Children |
| `.child(child_name)` | `(string) -> prefab_instance` | Child als eigene Instanz fuer direkte Manipulation |

---

## 6. Dock-System (Grid-Snapping)

`dock_x` und `dock_y` binden eine Prefab-Instanz an Zellen des aktuell aktiven
UI-Grids. Sie setzen Position und Scale **unabhaengig pro Achse**, sodass das
Prefab die Zelle(n) exakt ausfuellt.

**Voraussetzung:** Ein `ui.begin_grid` muss aktiv sein, wenn `dock_x`/`dock_y`
aufgerufen wird.

| Methode | Signatur | Beschreibung |
|---------|----------|-------------|
| `.dock_x(col, colspan)` | `(i32, i32) -> void` | X-Position = Zell-X; `scale_x = cell_width / prefab_def_width` |
| `.dock_y(row, rowspan)` | `(i32, i32) -> void` | Y-Position = Zell-Y; `scale_y = cell_height / prefab_def_height` |

### Beispiel: Prefab an Grid-Zelle andocken

```aiml
# Grid definieren (8 Spalten, 4 Zeilen)
- decl: [cols, buffer<f32>, [0.08, 0.148, 0.148, 0.148, 0.148, 0.148, 0.06, 0.12]]
- decl: [rows, buffer<f32>, [0.35, 0.22, 0.22, 0.21]]
- call: [ui.begin_grid, cols, rows]

# Combat-Zone 1 in Spalte 1, Zeile 0 andocken
- call: [zone_combat_1.dock_x, 1, 1]
- call: [zone_combat_1.dock_y, 0, 1]

# Trap-Zone ueber 5 Spalten (1-5), Zeile 1
- call: [zone_trap.dock_x, 1, 5]
- call: [zone_trap.dock_y, 1, 1]

# Graveyard in Spalte 6, ueber alle 4 Zeilen
- call: [graveyard.dock_x, 6, 1]
- call: [graveyard.dock_y, 0, 4]
```

### Verhalten

- `dock_x` liest die aktuelle Grid-Zelle via `aiml_ui_grid_cell_rect(col, 0, colspan, 1)`,
  setzt `inst.x = cell_x` und `inst.scale_x = cell_width / def.width`.
- `dock_y` liest via `aiml_ui_grid_cell_rect(0, row, 1, rowspan)`,
  setzt `inst.y = cell_y` und `inst.scale_y = cell_height / def.height`.
- X und Y sind **unabhaengig** steuerbar — ein Prefab kann z.B. per `dock_x` horizontal
  an eine Grid-Spalte gebunden sein, aber per `set_pos` vertikal frei positioniert werden.
- Scale wird pro Achse getrennt berechnet (`scale_x`, `scale_y`), sodass
  nicht-quadratische Zellen korrekt abgedeckt werden.

---

## 7. Hit-Testing, Hover + Drag-and-Drop

### Hit-Testing

| Aufruf | Beschreibung |
|--------|-------------|
| `engine.hit_test(mx, my) -> prefab_instance` | Oberste sichtbare Instanz an Bildschirmkoordinate (oder `null`) |

Hit-Test iteriert in umgekehrter Z-Order (top-to-bottom).

### Hover-Tracking

| Aufruf | Beschreibung |
|--------|-------------|
| `engine.update_hover(mx, my)` | Hover-State aktualisieren. Pro Frame einmal aufrufen. Fuehrt `hit_test` aus und setzt `hovered`-Flag auf der getroffenen Instanz. |
| `engine.get_hovered() -> prefab_instance` | Aktuell gehoverte Instanz (oder `null`) |

| Instanz-Methode | Signatur | Beschreibung |
|-----------------|----------|-------------|
| `.is_hovered()` | `-> i32` | 1 wenn Maus ueber dieser Instanz, sonst 0 |
| `.was_clicked(btn)` | `(i32) -> i32` | 1 wenn diese Instanz diesen Frame geklickt wurde (= gehovert + Maustaste gedrueckt). `btn`: 0=links, 1=rechts, 2=mitte |

**Wichtig:** `update_hover` muss vor `was_clicked`-Abfragen aufgerufen werden,
da es den Hover-State und den Mouse-Button-Snapshot fuer den aktuellen Frame setzt.

### Beispiel: Hover-Preview + Deck-Click

```aiml
# Im Render-Loop, nach update_drag:
- call: [engine.update_hover, mx, my]

# Deck-Click pruefen
- if:
    condition: { "!=": [{ call: [deck_wesen.was_clicked, 0] }, 0] }
    then:
      - call: [log.info, "Karte aus Wesen-Deck gezogen"]

# Hover-Preview anzeigen
- decl: [hov, prefab_instance, { call: [engine.get_hovered] }]
- if:
    condition: { "!=": [hov, 0] }
    then:
      - call: [preview_card.set_visible, 1]
  else:
    - call: [preview_card.set_visible, 0]
```

### Drag-and-Drop

| Aufruf | Beschreibung |
|--------|-------------|
| `engine.update_drag(mx, my, btn_down, btn_released)` | Drag-State aktualisieren. Pro Frame einmal aufrufen. |
| `engine.get_dragged() -> prefab_instance` | Aktuell gedraggte Instanz (oder `null`) |

Nur Instanzen mit `draggable = true` und `visible = 1` werden fuer Drag beruecksichtigt.

---

## 8. Rendering

```aiml
- call: [gl.clear_all, 0.05, 0.05, 0.1, 1.0]

# UI als Background-Layer
- call: [ui.begin, win]
# ... grid layout ...
- call: [ui.end]

# Prefabs ON TOP of UI
- call: [engine.draw_scene]

- call: [engine.swap, win]
```

`engine.draw_scene()` rendert alle Instanzen sortiert nach Z-Order (niedrig zu hoch).
Sprites und Base-Texturen werden als texturierte Quads gezeichnet,
Labels als Text, Rects als einfarbige Quads.

---

## 9. C-API (Runtime)

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
void* aiml_prefab_child(void* inst, const char* child_name);

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
