# AIML — File Split Candidates (2026-05-16)

## Ziel

Diese Notiz sammelt die aktuell auffälligen großen Quelldateien in
`aiml/src/` und bewertet, welche davon sinnvoll in kleinere Module
zerlegt werden sollten.

Der Fokus liegt nicht auf Vendor-Dateien, sondern auf eigenen AIML-
Dateien, die fachlich oder strukturell zu breit geworden sind.

## Kurzfazit

Die klarsten Split-Kandidaten sind aktuell:

1. `src/sys/tensor_runtime.cpp`
2. `src/sys/ui_runtime.cpp`
3. `src/sys/db_runtime.cpp`
4. `src/sys/prefab_runtime.cpp`

Ein zusätzlicher, aber etwas anderer Kandidat ist:

5. `src/core/builtin_registry.cpp`

## Gemessene Größen

Relevante große Nicht-Vendor-Dateien:

- `src/core/builtin_registry.cpp` — `9069` Zeilen
- `src/sys/tensor_runtime.cpp` — `2591` Zeilen
- `src/sys/ui_runtime.cpp` — `2249` Zeilen
- `src/sys/db_runtime.cpp` — `2166` Zeilen
- `src/sys/prefab_runtime.cpp` — `1602` Zeilen
- `src/core/ast_compiler.cpp` — `1560` Zeilen
- `src/sys/http_runtime.cpp` — `1437` Zeilen
- `src/sys/mirror_runtime.cpp` — `1221` Zeilen
- `src/sys/input_runtime.cpp` — `1180` Zeilen

Nicht als Architekturproblem gewertet:

- `src/vendor/miniaudio.h`
- `src/vendor/cgltf.h`
- `src/core/yyjson.cpp`
- `src/core/yyjson.h`

Vendor- oder eingebettete Third-Party-Dateien sind naturgemäß groß und
werden hier nicht als Split-Ziel betrachtet.

## 1. `src/sys/tensor_runtime.cpp`

### Warum auffällig

Diese Datei ist nicht nur groß, sondern mischt mehrere klar trennbare
Verantwortlichkeiten:

- Arena- und Tensor-Allokation
- Tensor-Grundoperationen
- Neural-/Math-Ops
- Autograd / Backward-Pfade
- Persistenz / Save / Load / Hashing
- KV-bezogene Hilfslogik

### Sichtbare Trennlinien

Frühe Basisfunktionen:

- Arena / Allocation
- Tensor-Objekterzeugung
- Shape- und Datenzugriff

Mathematische Ops:

- `add`, `sub`, `mul`, `matmul`, `softmax`, `layer_norm`, ...

Autograd-Block:

- `backward_add`, `backward_matmul`, `backward_softmax`, ...

Persistenz-/Hilfsblock:

- `aiml_tensor_save`
- `aiml_tensor_load`
- `aiml_tensor_sha256`

### Empfehlung

Mindestens aufteilen in:

- `tensor_arena.cpp`
- `tensor_ops.cpp`
- `tensor_autograd.cpp`
- `tensor_io.cpp`

Das ist aktuell der stärkste Split-Kandidat.

## 2. `src/sys/ui_runtime.cpp`

### Warum auffällig

Die Datei kombiniert momentan:

- GL-/Shader-Setup
- Font-Baking
- Grid- und Cell-Layout
- konkrete Widgets
- komplette Style-API mit sehr vielen Settern

Damit ist sie nicht nur groß, sondern auch schwer zu navigieren.

### Sichtbare Trennlinien

Render-/Setup-nah:

- Shader-Kompilierung
- Font-Baking
- Initialisierung

Layout / Grid:

- `aiml_ui_begin_grid`
- `aiml_ui_begin_cell`
- `aiml_ui_set_gap`

Widget-Implementierungen:

- `aiml_ui_button`
- `aiml_ui_checkbox`
- `aiml_ui_slider`
- `aiml_ui_text_input`

Style-API:

- `aiml_ui_button_style_set_*`
- `aiml_ui_slider_style_set_*`
- `aiml_ui_progress_style_set_*`

### Empfehlung

Mindestens aufteilen in:

- `ui_render.cpp`
- `ui_layout.cpp`
- `ui_widgets.cpp`
- `ui_style.cpp`

## 3. `src/sys/db_runtime.cpp`

### Warum auffällig

Die Datei enthält mehrere Subsysteme, die logisch getrennt sind:

- typed store / column store
- mmap / grow / replay
- kv-store
- WAL-Logik
- decoded cache / LRU
- vector store

### Sichtbare Trennlinien

Store-/Column-Logik:

- `store_col_insert`
- `store_col_get`
- `store_col_delete`

KV-Logik:

- `kv_mmap_attach`
- `kv_wal_write_set`
- `kv_wal_replay`
- `kv_decoded_*`

Vector-Store-Logik:

- `vec_wal_write_insert`
- `vec_wal_replay`

### Empfehlung

Mindestens aufteilen in:

- `db_store_runtime.cpp`
- `db_kv_runtime.cpp`
- `db_vec_runtime.cpp`
- optional `db_wal_runtime.cpp`

## 4. `src/sys/prefab_runtime.cpp`

### Warum auffällig

Die Datei mischt im Moment:

- Renderpfad / Shader / VAO / Texture-Cache
- Text-Wrapping und Font-Handling
- JSON-Parsing
- Prefab-Instanziierung
- Child-Lookup
- Hit-Test / Hover / Focus / Drag
- Docking

Das sind mehrere natürliche Module.

### Sichtbare Trennlinien

Render- und GPU-Teil:

- `compile_shader_src`
- `create_prefab_shader`
- `flush_batch`
- `render_instance`

JSON-/Definitionsteil:

- `parse_json_string`
- `parse_json_number`
- `parse_children_json`

Interaktions-/UI-Teil:

- `aiml_prefab_hit_test`
- `aiml_prefab_update_hover`
- `aiml_prefab_update_drag`
- `aiml_prefab_child_clicked`

### Empfehlung

Mindestens aufteilen in:

- `prefab_render.cpp`
- `prefab_parse.cpp`
- `prefab_instance.cpp`
- `prefab_interaction.cpp`

## 5. `src/core/builtin_registry.cpp`

### Warum auffällig

Mit `9069` Zeilen ist dies die größte eigene Nicht-Vendor-Datei.
Allerdings ist sie strukturell anders als die Runtime-Monolithen:

- sie ist im Kern eine große Registry-Datei
- zentraler Einstieg ist `registerBuiltins(CompilerContext&)`

### Einschätzung

Das ist eher ein Fall von:

- sehr groß
- aber eventuell absichtlich zentral gehalten

Wenn sie manuell gepflegt wird, lohnt sich dennoch ein Split nach
Fachbereichen.

### Empfehlung

Falls angefasst, eher so trennen:

- `builtin_registry_core.cpp`
- `builtin_registry_io.cpp`
- `builtin_registry_net.cpp`
- `builtin_registry_ui.cpp`
- `builtin_registry_tensor.cpp`
- `builtin_registry_db.cpp`

Dann bleibt `registerBuiltins(...)` als Aggregator erhalten.

## Dateien, die aktuell noch okay wirken

Diese Dateien sind zwar nicht klein, wirken aber noch nicht so stark
überfrachtet:

- `src/sys/http_runtime.cpp`
- `src/sys/mirror_runtime.cpp`
- `src/sys/input_runtime.cpp`
- `src/sys/gl_runtime.cpp`

Sie können später trotzdem Kandidaten werden, sind aber aktuell nicht
die dringendsten.

## Priorisierte Reihenfolge

Wenn der Split in Etappen erfolgen soll, wäre die sinnvolle Reihenfolge:

1. `tensor_runtime.cpp`
2. `ui_runtime.cpp`
3. `db_runtime.cpp`
4. `prefab_runtime.cpp`
5. `builtin_registry.cpp`

## Vorläufiges Fazit

Die aktuelle Hauptbaustelle ist nicht einfach „große Dateien“ im
Allgemeinen, sondern vor allem das erneute Zusammenwachsen mehrerer
fachlich separater Subsysteme in einzelne Runtime-Dateien.

Besonders deutlich ist das in:

- Tensor
- UI
- DB
- Prefab

Dort lohnt sich ein Split nicht nur für die Dateigröße, sondern vor
allem für:

- Navigierbarkeit
- Testbarkeit
- Build-Verständlichkeit
- künftige Backend-Erweiterungen
