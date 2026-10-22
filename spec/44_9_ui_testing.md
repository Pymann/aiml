# Kapitel 44.9 — UI-Testfälle (Szenario-Format)

Ein Format, um eine laufende Oberfläche automatisch zu bedienen und zu prüfen: Widgets
werden **beim Namen** angesprochen, nicht über Pixelkoordinaten, und geprüft wird an dem,
was wirklich getroffen wurde.

Grundlage: `input.post_*` (`/spec/44_0_input.md` §8) für die Eingabe, die Geometrie-Getter
`.child_rect_*` und die Treffer-Spur `engine.prefab_trace_hits` (`/spec/44_6_prefabs.md` §5).

Abgrenzung zu `/spec/59_unit_testing.md`: das sind Unit-Tests **einer Funktion** mit
automatisch erzeugten Stubs. Hier geht es um ein Interaktions-Szenario gegen die echte
Anwendung, ohne Stubs.

## 1. Warum ein Format und nicht einfach aiml-Code

Ein Szenario in aiml zu schreiben geht auch — `post_mouse_move`, `update`, `post_mouse_down`,
`update`, … (§8.4). Nur steht dann in jedem Test dreimal dieselbe Frame-Choreografie, und wer
sie einmal falsch schreibt, bekommt einen Test, der grün ist, ohne geklickt zu haben. Das
Format hält die Choreografie an **einer** Stelle, im Treiber.

Das Szenario ist YAML (wie aiml selbst) und wird vom Treiber über `/spec/60_yaml.md` gelesen —
kein Generator, keine zweite Sprache.

## 2. Aufbau

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

### 2.1 Widget-Adresse

`"<Prefab-Def>[<Index>]/<Kind>"`, z. B. `"AgentsScene[0]/run_btn"` — so wie
`engine.prefab_last_hit()` sie zurückgibt. Der Index ist die Position in der Szene, weil der
Def-Name bei mehreren Instanzen desselben Prefabs nicht eindeutig ist. Für `move`/`click` genügt
`"<Prefab-Def>/<Kind>"`, wenn es nur eine Instanz gibt. Der Treiber löst die Adresse
über `.child_rect_*` auf und rechnet daraus den Mittelpunkt.

Existiert die Adresse nicht, **scheitert der Schritt** — ein Szenario kann nicht versehentlich
ins Leere klicken und dabei bestehen.

## 3. Schritte

| Schritt | Wirkung |
|---|---|
| `move: <addr>` | Cursor auf den Mittelpunkt des Widgets, ein Frame. |
| `move_at: [x, y]` | Cursor auf absolute Fensterkoordinaten (Notausgang, wenn kein Widget gemeint ist). |
| `click: <addr>` | Die vollen drei Frames aus §8.4: bewegen, drücken, loslassen. |
| `click_at: [x, y]` | Dasselbe auf absoluten Koordinaten. |
| `press: <addr>` / `release: <addr>` | Druck und Loslassen getrennt — für Drag-Gesten. |
| `drag: { from: <addr>, to: <addr> }` | Drücken, in Schritten bewegen (damit die Drag-Schwelle überschritten wird), loslassen. |
| `scroll: { at: <addr>, dy: <f32> }` | Scrollen über einem Widget. |
| `focus: <addr>` | Tastaturfokus setzen (`.focus`), ohne Klick. |
| `type: "<text>"` | Text ins fokussierte Feld (`input.post_text`). |
| `key: "<name>"` | Eine Taste, kanonische Namen aus §5 (`"Return"`, `"lctrl+c"` als Kombination). |
| `wait_frames: <n>` | `n` Frames weiterlaufen lassen. |
| `wait_for: { … }` | Bis eine Bedingung gilt oder `timeout_ms` abläuft (siehe §4). |
| `screenshot: "<datei>"` | Bild ablegen — für den Layout-Vergleich, den die Treffer-Spur nicht leisten kann (§5). |

## 4. Prüfungen

| Prüfung | Bedeutung |
|---|---|
| `expect_hit: <addr>` | `engine.prefab_last_hit()` enthält diese Adresse. |
| `expect_no_hit: <addr>` | Sie ist **nicht** enthalten. Deckt „ein anderes Widget hat den Klick geschluckt" ab. |
| `expect_only_hit: <addr>` | Sie ist die **einzige** — fängt den Fall, dass zwei überlappende Widgets gemeinsam feuern (spec/44_6). |
| `expect_miss: true` | `prefab_last_hit()` ist leer: der Klick traf nichts. Als Erwartung formulierbar, nicht nur als Unfall. |
| `expect_text: { child, value }` | `.text_value(child)` ist genau `value`. |
| `expect_contains: { child, value }` | Teilstring-Prüfung (Textbox-Ausgabe). |
| `expect_checked: { child, value }` | `.is_checked`. |
| `expect_visible: { child, value }` | Widget ist sichtbar / verborgen. |
| `expect_focused: <addr>` | `.child_focused`. |

`wait_for` nimmt dieselben Bedingungen plus `timeout_ms`; beim Ablauf scheitert der Schritt mit
der Bedingung im Fehlertext.

**`expect_hit` ist die wichtige Prüfung.** Ein Szenario, das nur Endzustände prüft, kann
„Button tut nichts" nicht von „Klick kam nie an" unterscheiden. Erst `prefab_last_hit()` trennt
das: es nennt die Widgets, die den Druck tatsächlich aufgelöst haben. Geprüft wird über die
Abfrage, nicht über Log-Parsing — das Log ist für Menschen, die Abfrage für Tests.

## 5. Was dieses Format nicht prüfen kann

Der Treiber fragt die Anwendung, **wo** ein Widget liegt, und klickt dann dorthin. Liegt das
Widget woanders, als die Anwendung glaubt — falsch gezeichnet, außerhalb des Fensters, hinter
einem Element, das keinen Hit-Test hat —, dann klickt der Test mit und merkt nichts. Diese
Klasse fängt nur ein Bildvergleich (`screenshot:`) oder die OS-Ebene
(`/spec/44_0_input.md` §8.5, noch nicht umgesetzt).

Was es prüft: dass Verdrahtung, Handler, Fokus-Fluss, Zustandsübergänge und die
Trefferauflösung inklusive z-Reihenfolge stimmen — die Fehlerklassen, die beim Umbauen einer
Oberfläche tatsächlich entstehen.

## 6. Treiber

Der Treiber ist ein aiml-Programm: Szenario über `/spec/60_yaml.md` lesen, Fenster öffnen,
Schritte abarbeiten, Treffer-Spur mitlesen, am Ende Bilanz. Ein Szenario, das nie klickt,
besteht nicht — ein Lauf ohne einen einzigen aufgelösten Treffer gilt als Fehler, nicht als
leeres Bestehen.

Der Treiber MUSS `engine.prefab_trace_hits(1)` setzen und die Frame-Reihenfolge aus §8.3 einhalten
(injizieren nach dem Input-Update des Frames, vor dem App-Update).

## 7. Status

Format: hier festgelegt. Vorbedingungen **umgesetzt und nachgewiesen**: `input.post_*`,
`.child_rect_*`, `engine.prefab_trace_hits`, `engine.prefab_last_hit`, `engine.window_pos` und
`input.os_*` (Nachweise siehe `/spec/44_0_input.md` §8.5). Offen ist nur noch der Treiber (§6).

Der Treiber bekommt **einen** Schalter für den Kanal: dieselben Schritte laufen über
`input.post_*` (schnell, headless, parallelisierbar) oder über `input.os_*` (echte Kette,
eigener Display).  Die Schritte selbst bleiben gleich, weil die Frame-Choreografie im Treiber
steckt und nicht im Szenario. OS-Ebene (XTEST auf eigenem Xvfb-Display) ist als
zweiter Kanal vorgesehen und in `/spec/44_0_input.md` §8.5 als TODO festgehalten.
