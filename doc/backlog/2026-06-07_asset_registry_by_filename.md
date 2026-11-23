# Asset registry — stem lookup per kind, compile-time validation

**Status**: backlog (2026-06-07, refined)
**Scope**: AIML compiler + engine runtime (gl/audio/io)
**Motivation**: currently asset paths are spread through the code as string literals:

```aiml
- set: [self.tex_golem,      { call: [gl.load_texture, "resources/creatures/golem.png"] }]
- set: [self.intro_tex,      { call: [gl.load_texture, "resources/intro/intro_bg.png"] }]
- set: [g_combat_atk_tex,    { call: [gl.load_texture, "resources/ui/battle/attack.png"] }]
```

Problems:
1. **Path mazes**: whoever wants to know where `crystal-rock.png` lives has to
   grep — every new subdirectory invents a new path scheme.
2. **A missing file** only becomes visible at runtime as a black square;
   the compiler cannot warn.
3. **Avoiding double loading**: every `gl.load_texture` loads again if
   the caller does not cache it itself.
4. **WASM asset bundle**: `asset_directories` in `exodus.json` is the
   only source of truth for what is shipped — but the engine code
   does not know that and has to guess.

## Vision

```aiml
# Compile time: the aiml compiler scans asset_directories and builds a
# stem map per asset kind (no .png suffix needed in the lookup).
- set: [self.tex_golem,   { call: [gl.tex, "golem"] }]
- set: [self.intro_tex,   { call: [gl.tex, "intro_bg"] }]
- set: [g_combat_atk_tex, { call: [gl.tex, "attack"] }]
# gl.tex(stem) is a single cache lookup + lazy load.
```

In addition:
- `gl.load_texture(path)` stays as a low-level escape hatch for
  runtime-generated paths outside the bundle (user uploads,
  server-streamed assets). Deprecated for static source strings;
  the compiler emits a deprecation warning on the old call.
- Compiler hard error when `gl.tex("foo")` has a string literal arg
  and `foo` is not in the texture store — *before* WASM is built.
- Dynamic args (`gl.tex(some_var)`) stay a runtime hash lookup.

## Asset kinds (stores)

Every kind has its own stem→path store. Classification by extension:

| Kind     | Extensions                       | API                                     |
|----------|----------------------------------|-----------------------------------------|
| texture  | `.png .jpg .webp .svg`           | `gl.tex(stem) → gl_texture`             |
| audio    | `.ogg .wav .mp3 .flac`           | `audio.sfx(stem)`, `audio.music(stem)`  |
| data     | `.json .yml .yaml`               | `io.read_asset(stem) → string`          |
| shader   | `.glsl .frag .vert`              | `gl.shader(stem) → gl_shader`           |
| font     | `.ttf .otf`                      | `gl.font(stem, size) → gl_font`         |

`golem.png` (texture) and `golem.mp3` (audio) are allowed — they
end up in different stores, no conflict.

## Architecture

### Phase 1 — compile-time scan

1. **exodus** passes the `asset_directories` content from `exodus.json`
   1:1 as a CLI flag to the aiml compiler:
   `--asset-roots=resources/intro,resources/creatures,…`.
   No new config key, no manifest file on disk.
2. **Roots can be dirs OR files.** `asset_directories` has grown
   historically and already contains single files in DataCreatures
   (e.g. `resources/skills/skill_default.png`).  The scanner
   distinguishes:
   - file root → one-element walk (classify, register)
   - directory root → recursive walk
3. **The aiml compiler** calls `AssetScanner::scan(roots)` at compile start:
   - per root the matching walk
   - per file: realpath/canonicalize → collision key
   - extension → classify the kind, stem = file name without extension
   - store in `unordered_map<Kind, unordered_map<stem, AssetEntry>>`,
     where `AssetEntry = { rel_path, abs_path }`
4. **Dedup overlapping roots BEFORE the stem collision check.**
   `resources/ui` AND `resources/ui/action_bar` as roots is legitimate
   (DataCreatures' current exodus-client.json has exactly that).  Same
   absolute path → exactly ONE entry — NO error.  Before the insert,
   check whether the `abs_path` is already registered; if so, prefer the shorter
   `rel_path`, otherwise skip.
5. **Hard errors** at compile time, abort without IR emission:
   - Upper-case letter in a file or subdir name → warning + skip
     (convention enforcement; the lookup is case-sensitive, half-aliased
     files should not get into the store at all).
   - Duplicate stem in the same kind at different `abs_path`s (not:
     the same file via overlapping roots) → error with both paths
     in the error text.
   - Unknown extension → warning + skip.
6. **Codegen**: per kind a static LLVM constant
   `__aiml_asset_table_<kind>` as an array `{i8* stem, i8* rel_path}[]`,
   sorted for binary search in the runtime.

### Phase 2 — runtime API

`src/sys/asset_runtime.h/cpp` (new) registers the builtins in
`src/core/builtin_registry.cpp`:

- `gl.tex(stem)`:
  1. Lookup in the cache `unordered_map<string, AimlGLTexture*>`. Hit → return.
  2. Lookup in `__aiml_asset_table_texture` (binary search).
  3. Miss → runtime error with the hint "stem '<x>' not in texture store".
  4. Hit → `gl_load_texture(rel_path)` + cache + return.
- `audio.sfx(stem)` / `audio.music(stem)`: the same stem map, a separate
  cache table per playback API.
- `io.read_asset(stem)`: returns the file content as a string, no cache.
- `gl.shader(stem)` / `gl.font(stem, size)`: analogous.

### Phase 2.5 — prefab/material literals: stem only

The imperative API is only part of the asset surface.  AIML prefab
specs and material files reference textures/shaders/fonts via
string fields.  **Strict: stem form only. No paths. No
escape hatch.**

```yaml
# Old (hard error):
- { name: icon, type: rect, texture: "resources/items/hammer.png" }
# New (the only way):
- { name: icon, type: rect, texture: "hammer" }
```

Rules:

1. **Prefab fields `texture:` / `shader:` / `font:` accept ONLY stems.**
   A value with `/` or a file extension (`.png`/`.glsl`/…) → compile hard error
   with the hint "use stem `hammer` instead of `resources/items/hammer.png`".
2. **Mandatory existence check**: a stem that does not exist in the respective kind store
   → hard error with YAML path+line.  Typos are caught at
   build time, not as a black square at runtime.
3. **Material files** (`.glsl`/`.frag`/`.vert`): only the
   `#include "@stem"` syntax.  `#include "common.glsl"` → hard error in the
   material compiler with the same "use @common" hint.

**Touchpoints in the prefab parser** (src/sys/prefab_runtime.cpp):
- Texture/shader/font resolver: stem lookup in the kind store.  No more
  `gl_load_texture(path)` fallbacks for prefab specs.
- Material compiler: parse the `@`-prefixed `#include` directive + resolve via
  the shader store.

DataCreatures migration needed BEFORE the feature can be merged —
otherwise every build breaks immediately.  Order in the migration block below.

### Phase 3 — compile-time lookup (optional)

- `{call: [gl.tex, "golem"]}` with a string literal arg is statically
  resolvable. The compiler can:
  - do an existence check at compile time (stem in the store?) — hard error if
    not. Includes a "did you mean" suggestion at a close Levenshtein
    distance.
  - emit a constant i32 (index into the asset table) instead of a string →
    the runtime is an O(1) array index instead of a binary search.
- Dynamic args stay a binary search.

### Phase 4 — bundle awareness in the engine runtime

- **WASM**: rel_path points into the emscripten VFS (`--preload-file=...`
  has already mounted the directory). The asset is loaded via fetch.
- **Native**: rel_path is relative to cwd, or an explicit
  `--asset-root` runtime flag for deployed builds.

## Files & code changes

- `exodus/core/build.py` (or wherever the aiml compile call is assembled):
  add `--asset-roots=<comma-joined asset_directories>`.
  One line.
- `src/core/asset_scanner.h/cpp` (new): walks roots (dir+file),
  canonicalises, dedups via abs_path, classifies, errors, emits
  `__aiml_asset_table_<kind>` as an LLVM constant.
- `src/sys/asset_runtime.h/cpp` (new): per-kind caches +
  lookup helpers. C ABI for calls from LLVM-emitted code.
- `src/core/builtin_registry.cpp`: register the `gl.tex/shader/font`, `audio.sfx/music`,
  `io.read_asset` builtins; one `AddSymbol` per C ABI
  lookup function in `compiler.cpp`.
- `src/sys/prefab_runtime.cpp` (phase 2.5): texture/shader/font resolvers
  try a stem lookup first, fall back to a path load with a
  deprecation warning.  Affecting: rect/sprite children with `texture:`,
  material refs with `material:`, in future `font:` if it gets added.
- Spec doc `aiml/spec/44_7_asset_registry.md` (new): syntax, stem rules,
  kind classification, guarantees.

## Test plan

- `tests/test_asset_registry.aiml`:
  - `gl.tex("known")` → texture not null
  - `gl.tex("known")` twice → identical pointer (cache hit)
  - `audio.sfx("known")` + `audio.music("known")` cached separately
  - `gl.tex("golem")` + `audio.sfx("golem")` coexist (different
    stores) without a conflict
  - **Overlapping roots**: two roots where one is a subset of the other
    (e.g. `resources/ui` + `resources/ui/action_bar`) → the same file
    is registered exactly once, no duplicate error
  - **File root**: a single PNG as a root → registered like any other entry
- `tests/_asset_negatives/`:
  - string literal lookup with an unknown stem → compiler hard error
  - upper-case file in the scan root → warning, file not findable
  - duplicate stem in the same kind at two different abs_paths →
    compiler hard error
  - prefab spec with a path literal (`texture: "resources/..."`) →
    compile hard error with the "use stem" hint
  - prefab spec with an unknown `texture:` stem → compile hard error
  - material file with `#include "common.glsl"` → hard error with "use @common"
  - `gl.load_texture("resources/...")` literal call with a path in the bundle →
    compile hard error

## Migration

Three surfaces, all three are **forcibly** switched over when the feature
is activated (no backward compat mode):

1. **Imperative API**: `gl.load_texture(path)` stays as an escape hatch
   for runtime-generated paths (user uploads etc.), but the compiler
   raises a hard error when the argument is a string literal **and** the path lies in
   an `asset_directory` → use `gl.tex(stem)`.
2. **Prefab spec fields** (texture/shader/font): stems only.
   Path literals → compile hard error.
3. **Material/shader includes**: only `#include "@stem"`.
   Path includes → hard error in the material compiler.

**DataCreatures must be migrated in a single wave** before
the aiml feature is switched on:
- all `gl.load_texture("resources/...")` literal calls (~20-30 places)
- all prefab `texture: "resources/..."` fields (~50 places)
- all material `#include "..."` paths (a few places)

Approach: implement the asset scan + new API first behind the feature flag
`AIML_ASSET_REGISTRY=1`.  Per-file DataCreatures migration and testing with the
flag.  When the whole wave is through and the tests are green, flag default-on, remove the old
code paths.

## Remaining open questions

- **Loose files with an unknown extension**: warning+skip is the default.
  Do we need an override (`asset_extensions` in exodus.json to
  pull in e.g. `.lua` as data)? — not for now, can be added later.
- **Asset hot reload at runtime**: native could get a file system watcher,
  WASM not. **Out of scope** for V1; a separate backlog
  entry when needed.

## Related backlog entries

- `2026-06-06_aiondb_indexed_fields.md` — both features address the
  "linear scan pain" with compiler-supported lookups.
