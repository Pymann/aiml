# Asset Registry — Stem-Lookup pro Kind, Compile-Time-Validierung

**Status**: Backlog (2026-06-07, refined)
**Scope**: AIML Compiler + Engine-Runtime (gl/audio/io)
**Motivation**: Aktuell stehen Asset-Pfade als String-Literale im Code verteilt:

```aiml
- set: [self.tex_golem,      { call: [gl.load_texture, "resources/creatures/golem.png"] }]
- set: [self.intro_tex,      { call: [gl.load_texture, "resources/intro/intro_bg.png"] }]
- set: [g_combat_atk_tex,    { call: [gl.load_texture, "resources/ui/battle/attack.png"] }]
```

Probleme:
1. **Pfad-Irrgärten**: Wer wissen will wo `crystal-rock.png` liegt muss
   greppen — jedes neue Subverzeichnis erfindet ein neues Pfadschema.
2. **Fehlende Datei** wird erst zur Laufzeit als black-square sichtbar;
   Compiler kann nicht warnen.
3. **Doppellade-Vermeidung**: jedes `gl.load_texture` lädt erneut wenn
   der Caller nicht selbst cached.
4. **WASM-Asset-Bundle**: `asset_directories` in `exodus.json` ist die
   einzige Quelle der Wahrheit was ausgeliefert wird — der Engine-Code
   weiß das aber nicht und muss raten.

## Vision

```aiml
# Compile-time: aiml-Compiler scannt asset_directories, baut pro
# Asset-Kind eine Stem-Map (kein .png-Suffix beim Lookup nötig).
- set: [self.tex_golem,   { call: [gl.tex, "golem"] }]
- set: [self.intro_tex,   { call: [gl.tex, "intro_bg"] }]
- set: [g_combat_atk_tex, { call: [gl.tex, "attack"] }]
# gl.tex(stem) ist Single-Cache-Lookup + lazy-load.
```

Zusätzlich:
- `gl.load_texture(path)` bleibt als low-level Escape-Hatch für
  runtime-generierte Pfade außerhalb des Bundles (User-Uploads,
  Server-gestreamte Assets). Für statische Source-Strings veraltet,
  Compiler emittiert Deprecation-Warning beim alten Call.
- Compiler-Hard-Error wenn `gl.tex("foo")` mit string-literalem Arg
  und `foo` nicht im Texture-Store — *bevor* WASM gebaut wird.
- Dynamische Args (`gl.tex(some_var)`) bleiben Runtime-Hash-Lookup.

## Asset-Kinds (Stores)

Jeder Kind hat eigenen Stem→Pfad-Store. Klassifikation per Extension:

| Kind     | Extensions                       | API                                     |
|----------|----------------------------------|-----------------------------------------|
| texture  | `.png .jpg .webp .svg`           | `gl.tex(stem) → gl_texture`             |
| audio    | `.ogg .wav .mp3 .flac`           | `audio.sfx(stem)`, `audio.music(stem)`  |
| data     | `.json .yml .yaml`               | `io.read_asset(stem) → string`          |
| shader   | `.glsl .frag .vert`              | `gl.shader(stem) → gl_shader`           |
| font     | `.ttf .otf`                      | `gl.font(stem, size) → gl_font`         |

`golem.png` (texture) und `golem.mp3` (audio) sind zulässig — sie
landen in unterschiedlichen Stores, kein Konflikt.

## Architektur

### Phase 1 — Compile-time Scan

1. **exodus** reicht den `asset_directories`-Inhalt aus `exodus.json`
   1:1 als CLI-Flag an den aiml-Compiler durch:
   `--asset-roots=resources/intro,resources/creatures,…`.
   Kein neuer Config-Key, kein Manifest-File auf Disk.
2. **Roots können Dirs ODER Files sein.** `asset_directories` ist
   historisch gewachsen und enthält in DataCreatures bereits einzelne
   Files (z.B. `resources/skills/skill_default.png`).  Der Scanner
   unterscheidet:
   - Datei-Root → ein-Element-Walk (klassifiziere, registriere)
   - Verzeichnis-Root → rekursiver Walk
3. **aiml-Compiler** ruft `AssetScanner::scan(roots)` zum Compile-Start:
   - Pro Root den passenden Walk
   - Pro File: realpath/canonicalize → Kollision-Schlüssel
   - Extension → Kind klassifizieren, Stem = Filename ohne Extension
   - In `unordered_map<Kind, unordered_map<stem, AssetEntry>>` ablegen,
     wobei `AssetEntry = { rel_path, abs_path }`
4. **Overlapping Roots dedup'en BEFORE Stem-Collision-Check.**
   `resources/ui` UND `resources/ui/action_bar` als Roots ist legitim
   (DataCreatures' aktuelle exodus-client.json hat genau das).  Same
   absolute path → exakt EIN Entry — KEIN Error.  Vor dem Insert
   prüfen ob das `abs_path` schon registriert ist; wenn ja, kürzeren
   `rel_path` bevorzugen, sonst skip.
5. **Hard-Errors** zur Compile-Zeit, Abbruch ohne IR-Emission:
   - Großbuchstabe in File- oder Subdir-Namen → Warning + skip
     (Convention-Enforcement; Lookup ist case-sensitive, halb-aliased
     Files sollen erst gar nicht ins Store).
   - Duplikat-Stem im gleichen Kind in verschiedenen `abs_path` (nicht:
     denselben File via überlappende Roots) → Error mit beiden Pfaden
     im Fehlertext.
   - Unbekannte Extension → Warning + skip.
6. **Codegen**: pro Kind eine statische LLVM-Konstante
   `__aiml_asset_table_<kind>` als Array `{i8* stem, i8* rel_path}[]`,
   sortiert für binary-search im Runtime.

### Phase 2 — Runtime-API

`src/sys/asset_runtime.h/cpp` (neu) registriert die Builtins in
`src/core/builtin_registry.cpp`:

- `gl.tex(stem)`:
  1. Lookup im Cache `unordered_map<string, AimlGLTexture*>`. Hit → return.
  2. Lookup im `__aiml_asset_table_texture` (binary search).
  3. Miss → runtime-error mit Hinweis "stem '<x>' not in texture store".
  4. Hit → `gl_load_texture(rel_path)` + cachen + return.
- `audio.sfx(stem)` / `audio.music(stem)`: gleiche Stem-Map, eigene
  Cache-Tabelle pro Wiedergabe-API.
- `io.read_asset(stem)`: returnt File-Inhalt als string, kein Cache.
- `gl.shader(stem)` / `gl.font(stem, size)`: analog.

### Phase 2.5 — Prefab/Material-Literale: Stem-only

Die imperative API ist nur ein Teil der Asset-Surface.  AIML-Prefab-
Specs und Material-Files referenzieren Texturen/Shader/Fonts über
String-Felder.  **Strikt: nur noch Stem-Form. Keine Pfade. Kein
Escape-Hatch.**

```yaml
# Alt (Hard-Error):
- { name: icon, type: rect, texture: "resources/items/hammer.png" }
# Neu (einziger Pfad):
- { name: icon, type: rect, texture: "hammer" }
```

Regeln:

1. **Prefab-Felder `texture:` / `shader:` / `font:` akzeptieren NUR Stems.**
   Wert mit `/` oder Datei-Extension (`.png`/`.glsl`/…) → Compile-Hard-Error
   mit Hinweis "use stem `hammer` instead of `resources/items/hammer.png`".
2. **Pflicht-Existenz-Check**: Stem der nicht im jeweiligen Kind-Store
   existiert → Hard-Error mit YAML-Pfad+Zeile.  Schreibfehler werden zur
   Build-Zeit gefangen, nicht als Black-Square zur Runtime.
3. **Material-Files** (`.glsl`/`.frag`/`.vert`): Nur noch
   `#include "@stem"` Syntax.  `#include "common.glsl"` → Hard-Error im
   Material-Compiler mit dem gleichen "use @common"-Hinweis.

**Touchpoints im Prefab-Parser** (src/sys/prefab_runtime.cpp):
- Texture/Shader/Font-Resolver: Stem-Lookup im Kind-Store.  Keine
  `gl_load_texture(path)` Fallbacks mehr für Prefab-Specs.
- Material-Compiler: `@`-prefixed `#include`-Direktive parsen + via
  shader-Store auflösen.

DataCreatures-Migration nötig BEVOR das Feature gemerged werden kann —
sonst bricht jeder build sofort.  Reihenfolge im Migration-Block unten.

### Phase 3 — Compile-time Lookup (Optional)

- `{call: [gl.tex, "golem"]}` mit String-Literal-Arg ist statisch
  auflösbar. Compiler kann:
  - Existenzcheck zur Compile-Zeit (Stem im Store?) — Hard-Error falls
    nicht. Inkludiert "did you mean"-Vorschlag bei naher Levenshtein-
    Distanz.
  - Konstante i32 (Index ins Asset-Table) statt String emittieren →
    Runtime ist O(1) Array-Index statt Binary-Search.
- Dynamische Args bleiben Binary-Search.

### Phase 4 — Bundle-Awareness im Engine-Runtime

- **WASM**: rel_path zeigt in den emscripten-VFS (`--preload-file=...`
  hat das Verzeichnis schon gemountet). Asset wird via fetch geladen.
- **Native**: rel_path ist relativ zu cwd, oder explizit
  `--asset-root` Runtime-Flag für deployed Builds.

## Files & Code-Changes

- `exodus/core/build.py` (oder wo der aiml-Compile-Call zusammengebaut
  wird): füge `--asset-roots=<comma-joined asset_directories>` hinzu.
  Eine Zeile.
- `src/core/asset_scanner.h/cpp` (neu): walkt roots (Dir+File),
  canonicalize, dedup via abs_path, klassifiziert, errort, emittiert
  `__aiml_asset_table_<kind>` als LLVM-Konstante.
- `src/sys/asset_runtime.h/cpp` (neu): per-kind Caches +
  Lookup-Helper. C-ABI zum Aufruf aus LLVM-emittiertem Code.
- `src/core/builtin_registry.cpp`: `gl.tex/shader/font`, `audio.sfx/music`,
  `io.read_asset` Builtins registrieren; je ein `AddSymbol` pro C-ABI
  Lookup-Funktion in `compiler.cpp`.
- `src/sys/prefab_runtime.cpp` (Phase 2.5): Texture/Shader/Font-Resolver
  versuchen Stem-Lookup zuerst, fallen auf Pfad-Load zurück mit
  Deprecation-Warning.  Affecting: rect/sprite-Children mit `texture:`,
  Material-Refs mit `material:`, künftig `font:` falls hinzukommt.
- Spec-Doc `aiml/spec/44_7_asset_registry.md` (neu): Syntax, Stem-Regeln,
  Kind-Klassifikation, Garantien.

## Test-Plan

- `tests/test_asset_registry.aiml`:
  - `gl.tex("known")` → texture nicht null
  - `gl.tex("known")` zweimal → identischer Pointer (Cache-Hit)
  - `audio.sfx("known")` + `audio.music("known")` separat gecacht
  - `gl.tex("golem")` + `audio.sfx("golem")` koexistieren (verschiedene
    Stores) ohne Konflikt
  - **Overlapping Roots**: zwei Roots wo einer Subset des anderen ist
    (z.B. `resources/ui` + `resources/ui/action_bar`) → derselbe File
    wird genau einmal registriert, kein Duplikat-Error
  - **File-Root**: einzelne PNG als Root → registriert wie sonst ein Eintrag
- `tests/_asset_negatives/`:
  - String-Literal-Lookup mit unbekanntem Stem → Compiler-Hard-Error
  - Großbuchstabe-Datei im scan-root → Warning, Datei nicht auffindbar
  - Duplikat-Stem im selben Kind in zwei verschiedenen abs_paths →
    Compiler-Hard-Error
  - Prefab-Spec mit Pfad-Literal (`texture: "resources/..."`) →
    Compile-Hard-Error mit "use stem" Hinweis
  - Prefab-Spec mit unbekanntem `texture:` Stem → Compile-Hard-Error
  - Material-File mit `#include "common.glsl"` → Hard-Error mit "use @common"
  - `gl.load_texture("resources/...")` literal call mit Pfad im Bundle →
    Compile-Hard-Error

## Migration

Drei Surfaces, alle drei werden bei der Aktivierung des Features
**erzwungen** umgestellt (kein Backward-Compat-Modus):

1. **Imperative API**: `gl.load_texture(path)` bleibt als Escape-Hatch
   für runtime-generierte Pfade (User-Uploads etc.), aber Compiler
   Hard-Errort wenn das Argument string-literal **und** der Pfad in
   einem `asset_directory` liegt → `gl.tex(stem)` benutzen.
2. **Prefab-Spec-Felder** (texture/shader/font): nur noch Stems.
   Pfad-Literale → Compile-Hard-Error.
3. **Material/Shader-Includes**: nur noch `#include "@stem"`.
   Pfad-Includes → Hard-Error im Material-Compiler.

**DataCreatures muss in einer einzigen Welle migriert werden** bevor
das aiml-Feature aktiv geschaltet wird:
- Alle `gl.load_texture("resources/...")` literal calls (~20-30 Stellen)
- Alle Prefab `texture: "resources/..."` Felder (~50 Stellen)
- Alle Material `#include "..."` Pfade (paar Stellen)

Lösungsweg: Asset-Scan + neue API erst implementieren mit feature-flag
`AIML_ASSET_REGISTRY=1`.  Per-File DataCreatures-Migration und Test mit
Flag.  Wenn alle Welle durch und Tests grün, Flag default-on, alte
Code-Pfade entfernen.

## Verbleibende Open Questions

- **Loose files mit unbekannter Extension**: Warning+skip ist Default.
  Brauchen wir einen Override (`asset_extensions` in exodus.json um
  z.B. `.lua` als data einzuziehen)? — vorerst nein, später ergänzbar.
- **Asset-Hot-Reload zur Laufzeit**: Native könnte filesystem-watcher
  hinzubekommen, WASM nicht. **Out-of-scope** für V1; eigener Backlog-
  Eintrag wenn benötigt.

## Verwandte Backlog-Einträge

- `2026-06-06_aiondb_indexed_fields.md` — beide Features adressieren
  "linear-scan-Schmerz" mit Compiler-unterstützten Lookups.
