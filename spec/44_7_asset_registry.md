# Chapter 44.7 — Asset Registry: stem lookup per kind

> **Core idea:** asset files (textures, audio, shaders, fonts, data)
> are registered at compile time by file-name stem in a per-kind store.
> AIML code refers to assets exclusively by stem:
> `gl.tex("hammer")`, `audio.sfx("click")`.  Paths are forbidden —
> the single source of truth is the `asset_directories` scan.

---

## Why

Scattering asset paths through the code as string literals causes three problems:

1. **Path mazes** — every new subdirectory invents a new
   path scheme; "where is `crystal-rock.png`?" becomes a grep job.
2. **A missing file only shows up at runtime** — as a black square instead
   of a compile error.
3. **Avoiding double loads** — every caller would have to cache by itself.

The asset scanner reads the `asset_directories` from `exodus.json`, classifies
each file by extension into a per-kind store, and provides lookups plus caching
through a per-kind C API.  The compiler validates string-literal
lookups during the build, **not only at runtime**.

## Surface

### Compile time

The build tool (e.g. `exodus`) passes the declared `asset_directories`
through unchanged as a CLI flag — on every `.aiml` compile call:

```
aiml --asset-roots=assets,data  main.aiml
```

`exodus build` does this automatically in `_compile_file` (build.py):
for every `.aiml` source, `--asset-roots=<comma-joined
asset_directories>` is appended to the AIML compiler call.  Other
build tools have to replicate the flag convention.

No manifest file on disk.  The compiler scans the roots at the
start of compilation (singleton `AIML::AssetScanner`).

**The venv's asset root comes on top** (spec/68 §9, F68-121): a compiler
that belongs to a venv adds `<venv>/assets` — where `aiml venv <dir> asset
install` puts asset packages, one directory each — to the roots it was given,
on every compile and every JIT run.  Its files join the same stores; a stem
both in the project and in a package is the duplicate error below.  Asset
packages follow stricter rules than a working tree: snake_case only and no
duplicate stem, refused when publishing and when installing (F68-122/123).

### Asset kinds (stores)

Classification by extension:

| Kind     | Extensions                       | API                                     |
|----------|----------------------------------|-----------------------------------------|
| texture  | `.png .jpg .jpeg .webp .svg`     | `gl.tex(stem) → gl_texture`             |
| audio    | `.ogg .wav .mp3 .flac`           | `audio.sfx(stem)`, `audio.music(stem)`  |
| shader   | `.glsl .frag .vert`              | (V2) `gl.shader(stem) → gl_shader`      |
| font     | `.ttf .otf`                      | `gl.font(stem, size) → ui_font`         |
| data     | `.json .yml .yaml .txt .md`      | `io.read_asset(stem) → string`          |
| mesh     | `.glb .gltf`                     | `gltf.model(stem) → gltf_scene`         |

The same stem in different stores is allowed:
`golem.png` (texture) and `golem.mp3` (audio) coexist without conflict.

### Lookup

```aiml
- decl: [tex, gl_texture,    { call: [gl.tex,      "hammer"]      }]
- decl: [sfx, audio_handle,  { call: [audio.sfx,   "click"]       }]
- decl: [bgm, audio_handle,  { call: [audio.music, "intro_theme"] }]
- decl: [fnt, ui_font,       { call: [gl.font,     "ui", 18.0]    }]   # font + size
- decl: [cfg, string,        { call: [io.read_asset, "balance"]   }]
```

Stem = file name without extension.  No `/`, no path.  Dots in the
file name are not supported (every dot is an extension marker).

### Font: per-(stem, size) cache

`gl.font(stem, size)` caches per **(stem, size)** pair — the same TTF in
several sizes produces several `ui_font` handles.  The internal cache key
is `"<stem>|<size>"` (size rounded to one decimal place).  Example:

```aiml
- decl: [body, ui_font, { call: [gl.font, "ui", 16.0] }]   # body-text
- decl: [head, ui_font, { call: [gl.font, "ui", 24.0] }]   # heading
# Both use the same assets/fonts/ui.ttf, but two separate handles.
```

### Compile-time validation

String literals are checked against the store at build time.  The
following cases are hard compile errors:

| Case | Error |
|---|---|
| `gl.tex("misspelled")` with a string literal and the stem not in the texture store | `gl.tex: stem 'misspelled' not in texture store` |
| `gl.load_texture("resources/...")` with a string-literal path inside an `asset_directory` | `gl.load_texture: literal path '<path>' is in the asset bundle; use gl.tex("<stem>") instead` |
| `gltf.model("misspelled")` with a string literal and the stem not in the mesh store | `gltf.model: stem 'misspelled' not in mesh store` |
| `gltf.load("assets/...")` with a string-literal path inside an `asset_directory` | `gltf.load: literal path '<path>' is in the asset bundle; use gltf.model("<stem>") instead` |
| Prefab spec `texture: "resources/foo.png"` (contains `/`) | `prefab texture must be a stem, not a path: 'resources/foo.png'` |
| Prefab spec `texture: "foo.png"` (contains `.`) | `prefab texture must be a stem (no extension): 'foo.png'` |
| Prefab spec `texture: "unknown"` (stem not in the store) | `prefab texture: stem 'unknown' not in texture store` |

Dynamic arguments (e.g. `gl.tex(some_var)`) are allowed and remain a
runtime lookup; a miss produces a runtime error, not a crash.

## Scan semantics

### Roots: directory or file

Every value in `--asset-roots=...` can be a directory **or a
single file**.  File roots are treated as a one-element walk;
directory roots are walked recursively.

Reason: historically, `asset_directories` lists often contain direct
file references (e.g. a fallback icon `resources/skills/skill_default.png`
next to the subdirectory roots).  Both forms must work.

### Overlapping roots: dedup via absolute path

If two roots overlap (e.g. `resources/ui` and `resources/ui/action_bar`),
the same file is reached by both walks.  The scanner canonicalises
every path and checks BEFORE duplicate-stem detection whether `abs_path` is already
registered — if so: no-op, no conflict.

Real duplicate-stem conflicts are **two different `abs_path`s** with
the same stem in the same kind store.  Example: `items/sword.png` and
`icons/sword.png` as two separate roots → hard error naming both paths.

### Convention enforcement: no uppercase

Files and subdirectories with uppercase letters in their names are **warned about and skipped**.
Reason: lookup is case-sensitive; half-aliased files (`Hammer.png` next to
`hammer.png`) should not get into the store at all, but be reported back to
the source.

### Mesh: 3D models are assets like textures

`gltf.load(path)` has existed since spec/43_1 — but without a kind in the registry, `.glb`
and `.gltf` were **unknown extensions**: the scanner warned and skipped them, and the
only way to a model was a **path in the source code**.  That is exactly what
this chapter forbids for textures (a path breaks as soon as someone changes the
working directory or builds for WASM).  Measured in cosmos
(2026-10-18): 248 warnings in one client build — two files, once per
translation unit.

```aiml
- decl: [rack, gltf_scene, { call: [gltf.model, "cosmos_rack"] }]   # assets/shapes/cosmos_rack.glb
```

`gltf.model(stem)` looks the stem up in the mesh store and loads the file with
the same loader as `gltf.load`.  **No cache**: a `gltf_scene` is a
unique resource (spec/61 §2.1) with `scene.free` — two callers sharing
one handle would free it twice.  Whoever needs the same model several times
keeps it in their class, like any other resource.

`gltf.load` with a **string literal** that lies inside an `asset_directory`
has been an error since then — the same rule as `gl.load_texture` (see
compile-time validation).  A dynamic path (command line, configuration)
remains allowed: `glbview FILE.glb` loads files that are not part of the bundle at all.

**What does NOT belong in the registry:** the SOURCE of a model.  ShapeML (`.shaml`)
describes how a `.glb` is produced; what ships is the result.  Such
sources live next to the project, not in the asset directory — otherwise every
WASM build carries material into the bundle that nobody loads.

### Unknown extensions: warn + skip

Files with extensions not assigned to any kind (`.tmp`, `.bak`,
`.DS_Store` etc.) are warned about and skipped.  They do not block
the build.

## Runtime semantics

### Caching

Per-kind in-memory cache (`unordered_map<stem, handle>`).  The first lookup
of a stem loads and caches; every further call returns the same pointer.

Consequence: identical `gl.tex("hammer")` calls throughout the code share
exactly one GPU texture.

### TODO: access timestamps and cache eviction

- [ ] Give runtime asset symbols/cache entries a monotonic
  `last_used` timestamp; update it on lookup and on actual use
  (including through render handles that are already stored).
- [ ] Periodically clean up old, unused entries after a configurable idle
  time. Keep assets that are still referenced/pinned; before removal take
  dependent prefab/material caches into account and release CPU/GPU resources
  exactly once through their respective owner. Keep bundled
  stem/path metadata for reloading; allow runtime stems
  (`@art:…`) to be requested again.

### Headless mode

Without a GL context (e.g. CLI tests without a display),
`aiml_gl_load_texture` returns an `AimlGLTexture` with `id=0` (no
`glGenTextures` call).  `gl.tex` therefore returns a non-null pointer
in headless tests too; only the GPU upload does not happen.

### Persistent paths

The stores internally keep the **absolute path** to the file (not the
rel_path).  Loading therefore works independently of the working
directory of the aiml process.

V2 (WASM bundle): when the aiml binary is compiled to WASM, the
bundle loader replaces the disk lookup with an emscripten VFS lookup via
the rel_path.

### Runtime textures (not from the asset bundle)

Some textures only come into being at runtime — e.g. **card artwork** that a
user uploads and the client fetches from the media server over WebSocket (not part
of the built asset bundle).  They are put into the same texture store under a **synthetic stem**
`@art:<sha256>`, so that a sprite `texture_path`
or `gl.tex("@art:<hash>")` resolves them like any bundled texture.

| Builtin | Signature | Purpose |
|---|---|---|
| `gl.register_texture` | `gl.register_texture(stem: string, tex: gl_texture) → void` | Register a texture created at runtime under `stem` (store = cache). |
| `gl.has_texture` | `gl.has_texture(stem: string) → i32` | `1` if a texture is cached under `stem` (hit test = dedup). |
| `gl.decode_texture` | `gl.decode_texture(webp: buffer) → gl_texture` | Encoded bytes (WebP/PNG/JPG) in `webp` → GPU texture. Cross-target (native stb/libwebp, WASM `createImageBitmap`). `null` on error. Needs a GL context. |

Pattern (fetch → decode → register, idempotent with the store as cache):

```
- if:
    condition: { eq: [{ call: [gl.has_texture, "@art:${hsh}"] }, 0] }
    then:
      # … fetch the webp bytes over WS …
      - decl: [tex, gl_texture, { call: [gl.decode_texture, webp] }]
      - if:
          condition: { pneq: [tex, null] }
          then: [ { call: [gl.register_texture, "@art:${hsh}", tex] } ]
```

## Architecture

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
│   • kind classification       │
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

### Singleton pattern

`AssetScanner::instance()` lives in the compiler AND the JIT process (the same one,
because aiml runs as a JIT).  At compile time the compiler fills the
singleton; at JIT time `asset_runtime` reads from the same one.

Consequence: JIT mode needs no manifest format, no persistence
layer, no schema versioning.

## Migration / backwards compatibility

Three surfaces are strict:

1. **Imperative API**: `gl.load_texture(path)` remains as an escape hatch
   for **runtime-generated paths** (user uploads, streamed assets).
   String literals under an `asset_directory` → hard error.
2. **Prefab spec fields** (`texture:` / V2 `shader:` / V2 `font:`):
   stems only, no paths.
3. **Material shader includes** (V2): only `#include "@stem"`.

When the feature is enabled in a project, everything must be migrated to stems,
otherwise the build breaks.  A feature-flag phase is optional
if a migration safety line is wanted.

## C API (`src/sys/asset_runtime.h`)

```c
void*       aiml_asset_get_texture(const char* stem);   // gl_texture* or null
void*       aiml_asset_get_sfx    (const char* stem);   // audio_handle V1: strdup'd path
void*       aiml_asset_get_music  (const char* stem);   // audio_handle V1: strdup'd path
const char* aiml_asset_read_data  (const char* stem);   // heap-allocated string content
void*       aiml_asset_get_font   (const char* stem, float size);  // ui_font*; size in cache key
```

Compile-time helpers (`src/core/asset_scanner.h`):

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

Paths can be relative or absolute.  Trailing slashes are
tolerated.  Empty value → the scanner singleton stays empty; all lookups
return null (unproblematic for projects that use no assets).

## Test infrastructure

`tests/_asset_helper/run_asset_test.sh` parses a header of the form
`# AssetRoots: a,b,c` and calls `aiml` with the corresponding roots.
`run_all_tests.sh` routes test files with this header through
the helper automatically.

Fixtures live under `tests/_asset_fixtures/<scenario>/` (minimal
valid PNG/WAV files for the scanner and loader paths).

## Out of scope (V1)

- **Shader builtin** (`gl.shader(stem)`): the API is in the spec; the implementation
  comes with the material-compiler wave (see next entry)
- **Material `#include "@stem"`**: the material-compiler pipeline is a world
  of its own; follows in its own wave
- **Asset hot reload at runtime**: native could manage a filesystem watcher;
  WASM could not.  Out of scope for V1
- **Locale-specific variants** (`hammer.de.png` vs `hammer.en.png`):
  still unresolved; potentially a kind of its own or per-stem metadata
- **Procedurally generated textures** (e.g. atlas-on-build): stay on the
  direct `gl.create_texture` path

## Related spec chapters

- Chapter 44.6 — Prefab system (consumes `texture:` stems)
- Chapter 44.1 — Engine (initialisation, GL context)
- Chapter 44.2 — Audio (consumes `audio.sfx/music` stems)

## Tests

`tests/test_asset_registry_*.aiml` (4 positive) and
`tests/test_asset_neg_*.aiml` (6 negative) cover all guarantees
specified here.
