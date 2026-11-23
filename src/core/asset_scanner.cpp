#include "asset_scanner.h"
#include "../../logger.h"

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace fs = std::filesystem;

namespace AIML {

AssetScanner& AssetScanner::instance() {
    static AssetScanner inst;
    return inst;
}

const char* AssetScanner::kind_name(AssetKind k) {
    switch (k) {
        case AssetKind::Texture: return "texture";
        case AssetKind::Audio:   return "audio";
        case AssetKind::Shader:  return "shader";
        case AssetKind::Font:    return "font";
        case AssetKind::Data:    return "data";
        case AssetKind::Mesh:    return "mesh";
        case AssetKind::Unknown: return "unknown";
    }
    return "unknown";
}

AssetKind AssetScanner::classify(const std::string& filename) {
    auto dot = filename.find_last_of('.');
    if (dot == std::string::npos) return AssetKind::Unknown;
    std::string ext = filename.substr(dot + 1);
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    if (ext == "png"  || ext == "jpg" || ext == "jpeg" ||
        ext == "webp" || ext == "svg") return AssetKind::Texture;
    if (ext == "ogg"  || ext == "wav" || ext == "mp3" ||
        ext == "flac") return AssetKind::Audio;
    if (ext == "glsl" || ext == "frag" || ext == "vert") return AssetKind::Shader;
    if (ext == "ttf"  || ext == "otf") return AssetKind::Font;
    if (ext == "json" || ext == "yml"  || ext == "yaml" ||
        ext == "txt"  || ext == "md") return AssetKind::Data;
    // spec/44_7: 3D models are assets like textures.  `gltf.load` has existed since
    // spec/43_1 — without a kind the file was an unknown extension, and the
    // only way to the model was a path in the source text (248 warnings in cosmos).
    if (ext == "glb"  || ext == "gltf") return AssetKind::Mesh;
    return AssetKind::Unknown;
}

static bool has_uppercase(const std::string& s) {
    for (char c : s) if (std::isupper((unsigned char)c)) return true;
    return false;
}

static std::string stem_of(const std::string& filename) {
    auto dot = filename.find_last_of('.');
    return dot == std::string::npos ? filename : filename.substr(0, dot);
}

void AssetScanner::clear() {
    stores_.clear();
    roots_canonical_.clear();
}

size_t AssetScanner::count(AssetKind k) const {
    auto it = stores_.find(k);
    return it == stores_.end() ? 0 : it->second.size();
}

bool AssetScanner::has_stem(AssetKind k, const std::string& stem) const {
    auto it = stores_.find(k);
    if (it == stores_.end()) return false;
    return it->second.find(stem) != it->second.end();
}

const char* AssetScanner::lookup_rel_path(AssetKind k, const std::string& stem) const {
    auto it = stores_.find(k);
    if (it == stores_.end()) return nullptr;
    auto eit = it->second.find(stem);
    if (eit == it->second.end()) return nullptr;
    return eit->second.rel_path.c_str();
}

void AssetScanner::register_runtime_entry(AssetKind k, const std::string& stem,
                                            const std::string& path) {
    AssetEntry e;
    e.rel_path = path;
    e.abs_path = path;
    stores_[k][stem] = std::move(e);
}

const char* AssetScanner::lookup_abs_path(AssetKind k, const std::string& stem) const {
    auto it = stores_.find(k);
    if (it == stores_.end()) return nullptr;
    auto eit = it->second.find(stem);
    if (eit == it->second.end()) return nullptr;
    return eit->second.abs_path.c_str();
}

bool AssetScanner::any_kind_has_stem(const std::string& stem) const {
    for (const auto& [_, store] : stores_) {
        if (store.find(stem) != store.end()) return true;
    }
    return false;
}

bool AssetScanner::path_under_root(const std::string& path) const {
    std::error_code ec;
    fs::path abs = fs::weakly_canonical(fs::path(path), ec);
    if (ec) abs = fs::absolute(fs::path(path), ec);
    std::string abs_s = abs.string();
    for (const auto& root : roots_canonical_) {
        if (abs_s.rfind(root, 0) == 0) return true;
    }
    return false;
}

void AssetScanner::register_file(const std::string& abs_path,
                                  const std::string& root) {
    fs::path p(abs_path);
    std::string filename = p.filename().string();

    // Check filename for uppercase (warning + skip).
    if (has_uppercase(filename)) {
        LOG_WARNING("[asset-scan] uppercase in filename, skipping: " + abs_path);
        return;
    }
    // Walk parent dirs up to the root, warn if any contains uppercase.
    fs::path probe = p.parent_path();
    fs::path root_p(root);
    while (!probe.empty() && probe != root_p && probe != probe.parent_path()) {
        std::string seg = probe.filename().string();
        if (has_uppercase(seg)) {
            LOG_WARNING("[asset-scan] uppercase subdir, skipping: " + abs_path);
            return;
        }
        probe = probe.parent_path();
    }

    AssetKind kind = classify(filename);
    if (kind == AssetKind::Unknown) {
        LOG_WARNING("[asset-scan] unknown extension, skipping: " + abs_path);
        return;
    }

    std::string stem = stem_of(filename);
    auto& store = stores_[kind];
    auto existing = store.find(stem);
    if (existing != store.end()) {
        if (existing->second.abs_path == abs_path) {
            // Same file via overlapping roots — already registered, no-op.
            // Prefer the shorter rel_path (= less-nested root) for display.
            // rel_path stays as first inserted.
            return;
        }
        // Two different absolute paths share the same (kind, stem) → conflict.
        LOG_ERROR(std::string("[asset-scan] duplicate stem '") + stem +
                  "' in " + kind_name(kind) + " store: " +
                  existing->second.abs_path + "  vs  " + abs_path);
        return;
    }

    // Compute rel_path relative to root.  For file-roots, parent_path
    // of the root is the base.
    fs::path base;
    std::error_code ec;
    if (fs::is_regular_file(root_p, ec)) {
        base = root_p.parent_path();
    } else {
        base = root_p;
    }
    std::error_code rel_ec;
    fs::path rel = fs::relative(p, base, rel_ec);
    std::string rel_s = rel_ec ? p.string() : rel.string();

    AssetEntry e;
    e.rel_path = rel_s;
    e.abs_path = abs_path;
    store[stem] = std::move(e);
}

void AssetScanner::scan_root(const std::string& root_arg) {
    if (root_arg.empty()) return;
    fs::path root(root_arg);
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(root, ec);
    if (ec) canon = fs::absolute(root, ec);
    std::string canon_s = canon.string();
    roots_canonical_.push_back(canon_s);

    if (!fs::exists(canon, ec)) {
        LOG_WARNING("[asset-scan] root does not exist, skipping: " + root_arg);
        return;
    }

    if (fs::is_regular_file(canon, ec)) {
        register_file(canon_s, canon_s);
        return;
    }

    if (!fs::is_directory(canon, ec)) {
        LOG_WARNING("[asset-scan] root is neither file nor dir, skipping: " + root_arg);
        return;
    }

    for (auto it = fs::recursive_directory_iterator(canon, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        if (ec) break;
        const fs::directory_entry& entry = *it;
        if (!entry.is_regular_file(ec)) continue;
        std::error_code abs_ec;
        std::string abs_s = fs::weakly_canonical(entry.path(), abs_ec).string();
        register_file(abs_s, canon_s);
    }
}

void AssetScanner::scan(const std::string& comma_separated_roots) {
    clear();
    if (comma_separated_roots.empty()) return;
    std::stringstream ss(comma_separated_roots);
    std::string token;
    while (std::getline(ss, token, ',')) {
        // Trim whitespace.
        size_t a = token.find_first_not_of(" \t");
        size_t b = token.find_last_not_of(" \t");
        if (a == std::string::npos) continue;
        scan_root(token.substr(a, b - a + 1));
    }
}

}  // namespace AIML
