# Chapter 44.7 — Asset Registry: Stem-Lookup pro Kind

> **Core idea:** Asset-Dateien (Texturen, Audios, Shader, Fonts, Data)
> werden zur Compile-Zeit nach Dateinamen-Stem in einen Kind-Store
> registriert.  AIML-Code spricht Assets ausschließlich über den Stem
> an: `gl.tex("hammer")`, `audio.sfx("click")`.  Pfade sind verboten —
> "single source of truth" liegt im `asset_directories`-Scan.

---

## Why

Asset-Pfade als String-Literale im Code verteilen erzeugt drei Schmerzen:

1. **Pfad-Irrgärten** — jedes neue Subverzeichnis erfindet ein neues
   Pfadschema, "wo liegt `crystal-rock.png`?" wird zur Grep-Aktion.
2. **Fehlende Datei wird zur Runtime sichtbar** — als Black-Square statt
   als Compile-Error.
3. **Doppellade-Vermeidung** — jeder Caller müsste selbst cachen.

Der Asset-Scanner liest die `asset_directories` aus `exodus.json`, klassifiziert
jede Datei nach Extension in einen Kind-Store, und stellt Lookups + Caching
über eine pro-Kind C-API bereit.  Der Compiler validiert String-Literal-
Lookups schon im Build, **nicht erst zur Laufzeit**.

## Surface

### Compile-Time

Im Build-Tool (z.B. `exodus`) werden die deklarierten `asset_directories`
unverändert als CLI-Flag durchgereicht — pro `.aiml`-Compile-Call:

```
aiml --asset-roots=assets,data  main.aiml
```

`exodus build` macht das automatisch in `_compile_file` (build.py):
für jede `.aiml`-Source wird `--asset-roots=<comma-joined
asset_directories>` an den AIML-Compiler-Call angehängt.  Andere
Build-Tools müssen die Flag-Konvention nachbilden.

Kein Manifest-File auf Disk.  Der Compiler scannt die Roots zum
Compile-Start (singleton `AIML::AssetScanner`).

### Asset-Kinds (Stores)

Klassifikation per Extension:

| Kind     | Extensions                       | API                                     |
|----------|----------------------------------|-----------------------------------------|
| texture  | `.png .jpg .jpeg .webp .svg`     | `gl.tex(stem) → gl_texture`             |
| audio    | `.ogg .wav .mp3 .flac`           | `audio.sfx(stem)`, `audio.music(stem)`  |
| shader   | `.glsl .frag .vert`              | (V2) `gl.shader(stem) → gl_shader`      |
| font     | `.ttf .otf`                      | `gl.font(stem, size) → ui_font`         |
| data     | `.json .yml .yaml .txt .md`      | `io.read_asset(stem) → string`          |
| mesh     | `.glb .gltf`                     | `gltf.model(stem) → gltf_scene`         |

Selber Stem in verschiedenen Stores ist erlaubt:
`golem.png` (texture) und `golem.mp3` (audio) koexistieren konfliktfrei.

### Lookup

```aiml
- decl: [tex, gl_texture,    { call: [gl.tex,      "hammer"]      }]
- decl: [sfx, audio_handle,  { call: [audio.sfx,   "click"]       }]
- decl: [bgm, audio_handle,  { call: [audio.music, "intro_theme"] }]
- decl: [fnt, ui_font,       { call: [gl.font,     "ui", 18.0]    }]   # font + size
- decl: [cfg, string,        { call: [io.read_asset, "balance"]   }]
```

Stem = Dateiname ohne Extension.  Kein `/`, kein Pfad.  Punkte im
Dateinamen werden nicht unterstützt (jeder Punkt = Extension-Marker).

### Font: per-(stem, size) Cache

`gl.font(stem, size)` cacht pro **(stem, size)**-Paar — derselbe TTF in
mehreren Größen erzeugt mehrere `ui_font`-Handles.  Cache-Key intern
ist `"<stem>|<size>"` (size auf eine Dezimalstelle gerundet).  Beispiel:

```aiml
- decl: [body, ui_font, { call: [gl.font, "ui", 16.0] }]   # body-text
- decl: [head, ui_font, { call: [gl.font, "ui", 24.0] }]   # heading
# Beide nutzen dieselbe assets/fonts/ui.ttf, aber zwei separate Handles.
```

### Compile-Time-Validierung

String-Literale werden zur Build-Zeit gegen den Store geprüft.  Fünf
Fehlerfälle führen zu Compile-Hard-Errors:

| Fall | Fehler |
|---|---|
| `gl.tex("misspelled")` mit String-Literal und Stem nicht im Texture-Store | `gl.tex: stem 'misspelled' not in texture store` |
| `gl.load_texture("resources/...")` mit String-Literal-Pfad in einem `asset_directory` | `gl.load_texture: literal path '<path>' is in the asset bundle; use gl.tex("<stem>") instead` |
| `gltf.model("misspelled")` mit String-Literal und Stem nicht im Mesh-Store | `gltf.model: stem 'misspelled' not in mesh store` |
| `gltf.load("assets/...")` mit String-Literal-Pfad in einem `asset_directory` | `gltf.load: literal path '<path>' is in the asset bundle; use gltf.model("<stem>") instead` |
| Prefab-Spec `texture: "resources/foo.png"` (enthält `/`) | `prefab texture must be a stem, not a path: 'resources/foo.png'` |
| Prefab-Spec `texture: "foo.png"` (enthält `.`) | `prefab texture must be a stem (no extension): 'foo.png'` |
| Prefab-Spec `texture: "unknown"` (Stem nicht im Store) | `prefab texture: stem 'unknown' not in texture store` |

Dynamische Args (z.B. `gl.tex(some_var)`) sind erlaubt und bleiben
Runtime-Lookup; Miss erzeugt einen Runtime-Error, kein Crash.

## Scan-Semantik

### Roots: Verzeichnis oder Datei

Jeder Wert in `--asset-roots=...` kann ein Verzeichnis **oder eine
einzelne Datei** sein.  Datei-Roots werden als ein-Element-Walk
behandelt; Verzeichnis-Roots rekursiv.

Reason: historisch enthalten `asset_directories`-Listen oft direkte
File-Verweise (z.B. ein Fallback-Icon `resources/skills/skill_default.png`
neben den Subdir-Roots).  Beide Formen müssen funktionieren.

### Überlappende Roots: dedup via absolute Pfad

Wenn zwei Roots überlappen (z.B. `resources/ui` und `resources/ui/action_bar`),
wird derselbe File durch beide Walks erreicht.  Der Scanner canonicalized
jeden Pfad und prüft VOR der Duplicate-Stem-Detection ob `abs_path` bereits
registriert ist — wenn ja: no-op, kein Konflikt.

Echte Duplicate-Stem-Konflikte sind **zwei verschiedene `abs_path`** mit
demselben Stem im selben Kind-Store.  Beispiel: `items/sword.png` und
`icons/sword.png` als zwei separate Roots → Hard-Error mit beiden Pfaden.

### Convention-Enforcement: kein Uppercase

Files und Subdirs mit Großbuchstaben im Namen werden **gewarnt + geskippt**.
Begründung: Lookup ist case-sensitive; halb-aliased Files (`Hammer.png` neben
`hammer.png`) sollen erst gar nicht ins Store, sondern an die Quelle
zurückgemeldet werden.

### Mesh: 3D-Modelle sind Assets wie Texturen

`gltf.load(path)` gibt es seit spec/43_1 — aber ohne Kind im Register waren `.glb`
und `.gltf` **unbekannte Endungen**: der Scanner warnte und übersprang sie, und der
einzige Weg zu einem Modell war ein **Pfad im Quelltext**.  Das ist genau das, was
dieses Kapitel für Texturen verbietet (ein Pfad bricht, sobald jemand das
Arbeitsverzeichnis wechselt oder nach WASM baut).  Gemessen in cosmos
(18.10.2026): 248 Warnungen in einem Client-Build — zwei Dateien, einmal pro
Übersetzungseinheit.

```aiml
- decl: [rack, gltf_scene, { call: [gltf.model, "cosmos_rack"] }]   # assets/shapes/cosmos_rack.glb
```

`gltf.model(stem)` schlägt den Stem im Mesh-Store nach und lädt die Datei mit
demselben Loader wie `gltf.load`.  **Kein Cache**: eine `gltf_scene` ist eine
eindeutige Ressource (spec/61 §2.1) mit `scene.free` — zwei Aufrufer, die sich
ein Handle teilen, gäben ihr zweimal frei.  Wer dasselbe Modell mehrfach
braucht, hält es in seiner Klasse, wie jede andere Ressource auch.

`gltf.load` mit einem **String-Literal**, das in einem `asset_directory` liegt,
ist seither ein Fehler — dieselbe Regel wie `gl.load_texture` (siehe
Compile-Time-Validierung).  Ein dynamischer Pfad (Kommandozeile, Konfiguration)
bleibt erlaubt: `glbview FILE.glb` lädt Dateien, die gar nicht zum Bündel gehören.

**Was NICHT ins Register gehört:** die QUELLE eines Modells.  ShapeML (`.shaml`)
beschreibt, wie ein `.glb` entsteht; ausgeliefert wird das Ergebnis.  Solche
Quellen liegen neben dem Projekt, nicht im Asset-Verzeichnis — sonst wandert bei
jedem WASM-Build Material ins Bündel, das niemand lädt.

### Unbekannte Extensions: warn + skip

Files mit Extensions die keinem Kind zugeordnet sind (`.tmp`, `.bak`,
`.DS_Store` etc.) werden gewarnt und übersprungen.  Sie blockieren nicht
den Build.

## Runtime-Semantik

### Caching

Per-Kind in-memory Cache (`unordered_map<stem, handle>`).  Erster Lookup
eines Stems lädt + cacht; jeder weitere Aufruf gibt denselben Pointer
zurück.

Konsequenz: identische `gl.tex("hammer")`-Calls überall im Code teilen
sich genau eine GPU-Texture.

### TODO: Zugriffszeitstempel und Cache-Bereinigung

- [ ] Runtime-Asset-Symbole/Cache-Einträge mit einem monotonen
  `last_used`-Zeitstempel versehen; bei Lookup und tatsächlicher Nutzung
  (auch über bereits gespeicherte Render-Handles) aktualisieren.
- [ ] Alte, ungenutzte Einträge periodisch nach konfigurierbarer Ruhezeit
  aufräumen. Noch referenzierte/gepinnte Assets behalten; vor dem Entfernen
  abhängige Prefab-/Material-Caches berücksichtigen und CPU-/GPU-Ressourcen
  genau einmal über den jeweiligen Eigentümer freigeben. Gebündelte
  Stem-/Pfad-Metadaten für erneutes Laden erhalten; für Runtime-Stems
  (`@art:…`) erneutes Anfordern ermöglichen.

### Headless-Modus

Bei fehlendem GL-Kontext (z.B. CLI-Tests ohne Display) gibt
`aiml_gl_load_texture` einen `AimlGLTexture` mit `id=0` zurück (kein
`glGenTextures`-Call).  `gl.tex` returnt damit auch in Headless-Tests
einen non-null Pointer; nur GPU-Upload findet nicht statt.

### Persistente Pfade

Die Stores speichern intern den **absoluten Pfad** zum File (nicht den
rel_path).  Damit funktioniert das Loading unabhängig vom Working-
Directory des aiml-Prozesses.

V2 (WASM-Bundle): wenn das aiml-Binary nach WASM compiled wird, ersetzt
der Bundle-Loader den Disk-Lookup durch einen emscripten-VFS-Lookup über
den rel_path.

### Runtime-Texturen (nicht aus dem Asset-Bundle)

Manche Texturen entstehen erst zur Laufzeit — z.B. **Card-Artwork**, das ein
User hochlädt und der Client per WebSocket vom Media-Server holt (nicht Teil
des gebauten Asset-Bundles).  Sie werden unter einem **synthetischen Stem**
`@art:<sha256>` in denselben Texture-Store gelegt, sodass ein Sprite-`texture_path`
bzw. `gl.tex("@art:<hash>")` sie wie jede gebündelte Textur auflöst.

| Builtin | Signatur | Zweck |
|---|---|---|
| `gl.register_texture` | `gl.register_texture(stem: string, tex: gl_texture) → void` | Eine zur Laufzeit erzeugte Textur unter `stem` registrieren (Store = Cache). |
| `gl.has_texture` | `gl.has_texture(stem: string) → i32` | `1` wenn unter `stem` eine Textur gecacht ist (Hit-Test = Dedup). |
| `gl.decode_texture` | `gl.decode_texture(webp: buffer) → gl_texture` | Kodierte Bytes (WebP/PNG/JPG) in `webp` → GPU-Textur. Cross-target (nativ stb/libwebp, WASM `createImageBitmap`). `null` bei Fehler. Braucht GL-Kontext. |

Muster (Fetch → Decode → Register, idempotent über den Store als Cache):

```
- if:
    condition: { eq: [{ call: [gl.has_texture, "@art:${hsh}"] }, 0] }
    then:
      # … webp-Bytes per WS holen …
      - decl: [tex, gl_texture, { call: [gl.decode_texture, webp] }]
      - if:
          condition: { pneq: [tex, null] }
          then: [ { call: [gl.register_texture, "@art:${hsh}", tex] } ]
```

## Architektur

```
┌───────────────────────────────┐
│  exodus.json                  │  asset_directories: [...]
└───────────────┬───────────────┘
                │ build-time:
                │   exodus → aiml --asset-roots=...
                ▼
┌───────────────────────────────┐
│  AssetScanner (Singleton)     │  src/core/asset_scanner.h
│   • walk roots (dir+file)     │
│   • dedup via abs_path        │
│   • Kind-Klassifikation       │
│   • Hard-Errors on dup-stem   │
│   • Warning+skip uppercase    │
└───────────────┬───────────────┘
                │ compile-time:
                │   builtin_registry / prefab_builder
                │   validate string-literal lookups
                ▼
┌───────────────────────────────┐
│  Asset Runtime                │  src/sys/asset_runtime.h
│   • per-kind in-memory cache  │
│   • gl.tex / audio.* / io.*   │
│   • C-API for JIT             │
└───────────────────────────────┘
```

### Singleton-Pattern

`AssetScanner::instance()` lebt im Compiler- UND JIT-Prozess (gleich,
weil aiml JIT-mäßig läuft).  Zur Compile-Zeit befüllt der Compiler den
Singleton, zur JIT-Zeit liest der `asset_runtime` aus demselben.

Konsequenz: für JIT-Mode kein Manifest-Format nötig, kein Persistenz-
Layer, kein Schema-Versioning.

## Migration / Backwards-Compat

Drei Surfaces sind strict:

1. **Imperative API**: `gl.load_texture(path)` bleibt als Escape-Hatch
   für **runtime-generierte Pfade** (User-Uploads, gestreamte Assets).
   String-Literale unter einem `asset_directory` → Hard-Error.
2. **Prefab-Spec-Felder** (`texture:` / V2 `shader:` / V2 `font:`):
   nur Stems, keine Pfade.
3. **Material-Shader-Includes** (V2): nur `#include "@stem"`.

Bei Aktivierung des Features in einem Projekt muss alles auf Stems
migriert sein, sonst bricht der Build.  Feature-Flag-Phase optional
falls eine Migrations-Sicherheitslinie gewünscht ist.

## C-API (`src/sys/asset_runtime.h`)

```c
void*       aiml_asset_get_texture(const char* stem);   // gl_texture* or null
void*       aiml_asset_get_sfx    (const char* stem);   // audio_handle V1: strdup'd path
void*       aiml_asset_get_music  (const char* stem);   // audio_handle V1: strdup'd path
const char* aiml_asset_read_data  (const char* stem);   // heap-allocated string content
void*       aiml_asset_get_font   (const char* stem, float size);  // ui_font*; size in cache key
```

Compile-Time-Helpers (`src/core/asset_scanner.h`):

```cpp
bool        AssetScanner::has_stem(AssetKind, std::string stem);
const char* AssetScanner::lookup_rel_path(AssetKind, std::string stem);
const char* AssetScanner::lookup_abs_path(AssetKind, std::string stem);
bool        AssetScanner::path_under_root(std::string path);
```

## CLI

```
aiml [...] --asset-roots=<comma,separated,paths> input.aiml
```

Pfade können relativ oder absolut sein.  Trailing Slashes werden
toleriert.  Leerer Wert → Scanner-Singleton bleibt leer; alle Lookups
returnen null (für nicht-asset-using Projekte unproblematisch).

## Test-Infrastruktur

`tests/_asset_helper/run_asset_test.sh` parsed einen Header der Form
`# AssetRoots: a,b,c` und ruft `aiml` mit den entsprechenden Roots.
`run_all_tests.sh` route Test-Files mit diesem Header automatisch durch
den Helper.

Fixtures liegen unter `tests/_asset_fixtures/<scenario>/` (minimal
valid PNG/WAV-Files für Scanner+Loader-Pfade).

## Out-of-Scope (V1)

- **Shader-Builtin** (`gl.shader(stem)`): API steht im Spec; Implementation
  kommt mit der Material-Compiler-Welle (siehe nächster Eintrag)
- **Material `#include "@stem"`**: Material-Compiler-Pipeline ist eigene
  Welt; folgt in einer eigenen Welle
- **Asset-Hot-Reload zur Laufzeit**: Native könnte filesystem-watcher
  hinzubekommen; WASM nicht.  Out-of-scope für V1
- **Locale-spezifische Varianten** (`hammer.de.png` vs `hammer.en.png`):
  noch ungeklärt; potentiell eigener Kind oder per-Stem-Metadata
- **Procedural-Generated Textures** (z.B. atlas-on-build): bleibt bei
  `gl.create_texture` direkt-Pfad

## Verwandte Spec-Kapitel

- Chapter 44.6 — Prefab-System (consumes `texture:` Stems)
- Chapter 44.1 — Engine (Initialisation, GL-Context)
- Chapter 44.2 — Audio (consumes `audio.sfx/music` Stems)

## Tests

`tests/test_asset_registry_*.aiml` (4 positive) und
`tests/test_asset_neg_*.aiml` (6 negative) decken alle hier
spezifizierten Garantien ab.
