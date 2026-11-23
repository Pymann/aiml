#pragma once
// Asset Runtime: per-kind cache + lookup helpers used by `gl.tex(stem)`,
// `audio.sfx(stem)`, etc.  Compile-time validation lives in
// `src/core/asset_scanner.h`; this file is the JIT-side counterpart.

#include <cstdint>

extern "C" {

// Texture: returns gl_texture* (loaded + cached) or null when the stem is
// unknown to the asset scanner.  Same return type as aiml_gl_load_texture.
void* aiml_asset_get_texture(const char* stem);
void  aiml_asset_register_texture(const char* stem, void* tex);  // register a decoded texture under a stem (@art:<hash>)
int32_t aiml_asset_has_texture(const char* stem);                // 1 if a texture is cached under this stem

// Audio: separate caches per playback role (sfx = one-shot, music = streamed).
// V1 returns a placeholder handle (i8*); concrete audio engine integration TBD.
void* aiml_asset_get_sfx(const char* stem);
void* aiml_asset_get_music(const char* stem);

// Data: returns the file contents as a heap-allocated C-string (null on miss).
const char* aiml_asset_read_data(const char* stem);

// Font: resolves stem to path and calls ui.load_font internally.  Cache key
// includes size (same TTF at multiple sizes = multiple cached handles).
void* aiml_asset_get_font(const char* stem, float size);

// Mesh: resolves a .glb/.gltf stem to its on-disk path (spec/44_7).  No
// cache: a gltf_scene is a unique resource with `free` (spec/61
// §2.1) — two callers with the same handle would free it twice.
const char* aiml_asset_mesh_path(const char* stem);

// Shader: resolves a fragment-shader stem to its on-disk path for the current
// target (abs on native, preload path on wasm).  Used by the material system.
const char* aiml_asset_shader_path(const char* stem);

// Runtime-side scan trigger.  Native JIT only — calls AssetScanner::scan with
// the same roots the compiler used.  For WASM, std::filesystem walks of
// preloaded directories are unreliable, so the compiler emits explicit
// `aiml_asset_runtime_register` calls instead (see below).
void aiml_asset_init_runtime(const char* comma_separated_roots);

// Explicit per-entry registration emitted by the compiler.  At
// generateEntryPoint() the compiler iterates the build-time AssetScanner
// state and emits one call per (kind, stem, path) — `path` is computed
// cwd-relative so it works in both native (`assets/items/hammer.png` from
// project root) and WASM (`/assets/items/hammer.png` after emcc preload).
// kind: 0=texture, 1=audio, 2=shader, 3=font, 4=data, 5=mesh.
void aiml_asset_runtime_register(int32_t kind, const char* stem, const char* path);

}  // extern "C"
