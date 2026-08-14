#include "asset_runtime.h"
#include "../core/asset_scanner.h"
#include "../../logger.h"

#include <unordered_map>
#include <string>
#include <mutex>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cmath>

// Loaders from sibling runtimes.  Declared here so we don't drag in the full
// gl_runtime / ui_runtime / audio_runtime headers.
extern "C" void* aiml_gl_load_texture(const char* path);
extern "C" void* aiml_ui_load_font(void* path_ptr, float size);
extern "C" void* aiml_audio_load(const char* path);
extern "C" void* aiml_audio_load_stream(const char* path);

namespace {
std::mutex g_mtx;
std::unordered_map<std::string, void*> g_tex_cache;
std::unordered_map<std::string, void*> g_sfx_cache;
std::unordered_map<std::string, void*> g_music_cache;
std::unordered_map<std::string, void*> g_font_cache;  // key = "stem|size"
}  // namespace

extern "C" void* aiml_asset_get_texture(const char* stem) {
    if (!stem) return nullptr;
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_tex_cache.find(stem);
    if (it != g_tex_cache.end()) return it->second;
    const char* abs = AIML::AssetScanner::instance().lookup_abs_path(
        AIML::AssetKind::Texture, stem);
    if (!abs) {
        LOG_ERROR(std::string("gl.tex: stem '") + stem + "' not in texture store");
        return nullptr;
    }
    void* tex = aiml_gl_load_texture(abs);
    g_tex_cache[stem] = tex;
    return tex;
}

// SFX: full decode + cache.  One-shot playback via audio_sound.play().
extern "C" void* aiml_asset_get_sfx(const char* stem) {
    if (!stem) return nullptr;
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_sfx_cache.find(stem);
    if (it != g_sfx_cache.end()) return it->second;
    const char* abs = AIML::AssetScanner::instance().lookup_abs_path(
        AIML::AssetKind::Audio, stem);
    if (!abs) {
        LOG_ERROR(std::string("audio.sfx: stem '") + stem + "' not in audio store");
        return nullptr;
    }
    void* h = aiml_audio_load(abs);
    if (h) g_sfx_cache[stem] = h;
    return h;
}

// Music: streamed (no full decode) since tracks can be multi-minute long.
extern "C" void* aiml_asset_get_music(const char* stem) {
    if (!stem) return nullptr;
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_music_cache.find(stem);
    if (it != g_music_cache.end()) return it->second;
    const char* abs = AIML::AssetScanner::instance().lookup_abs_path(
        AIML::AssetKind::Audio, stem);
    if (!abs) {
        LOG_ERROR(std::string("audio.music: stem '") + stem + "' not in audio store");
        return nullptr;
    }
    void* h = aiml_audio_load_stream(abs);
    if (h) g_music_cache[stem] = h;
    return h;
}

extern "C" void* aiml_asset_get_font(const char* stem, float size) {
    if (!stem) return nullptr;
    // Quantize size to one decimal so 18.0 and 18.00 share a cache slot.
    char key[256];
    std::snprintf(key, sizeof(key), "%s|%.1f", stem, size);
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_font_cache.find(key);
    if (it != g_font_cache.end()) return it->second;
    const char* abs = AIML::AssetScanner::instance().lookup_abs_path(
        AIML::AssetKind::Font, stem);
    if (!abs) {
        LOG_ERROR(std::string("gl.font: stem '") + stem + "' not in font store");
        return nullptr;
    }
    void* f = aiml_ui_load_font((void*)abs, size);
    g_font_cache[key] = f;
    return f;
}

extern "C" void aiml_asset_init_runtime(const char* roots) {
    if (!roots || !*roots) return;
    AIML::AssetScanner::instance().scan(roots);
}

extern "C" void aiml_asset_runtime_register(int32_t kind_int, const char* stem, const char* path) {
    if (!stem || !path) return;
    AIML::AssetKind k = AIML::AssetKind::Unknown;
    switch (kind_int) {
        case 0: k = AIML::AssetKind::Texture; break;
        case 1: k = AIML::AssetKind::Audio;   break;
        case 2: k = AIML::AssetKind::Shader;  break;
        case 3: k = AIML::AssetKind::Font;    break;
        case 4: k = AIML::AssetKind::Data;    break;
        default: return;
    }
    AIML::AssetScanner::instance().register_runtime_entry(k, stem, path);
}

extern "C" const char* aiml_asset_read_data(const char* stem) {
    if (!stem) return nullptr;
    const char* rel = AIML::AssetScanner::instance().lookup_abs_path(
        AIML::AssetKind::Data, stem);
    if (!rel) {
        LOG_ERROR(std::string("io.read_asset: stem '") + stem + "' not in data store");
        return nullptr;
    }
    std::ifstream ifs(rel);
    if (!ifs) return nullptr;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string s = ss.str();
    char* buf = (char*)malloc(s.size() + 1);
    memcpy(buf, s.data(), s.size());
    buf[s.size()] = '\0';
    return buf;
}
