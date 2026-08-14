#pragma once
// Asset Registry: stem→path manifest built at compile-time.
// See aiml/doc/backlog/2026-06-07_asset_registry_by_filename.md
//
// The scanner walks the project's asset_directories (passed via
// `--asset-roots=...` on the aiml CLI) and classifies each file by extension
// into a Kind-Store.  Stems are filenames without extension.  Runtime calls
// `gl.tex(stem)`, `audio.sfx(stem)`, etc. resolve via these stores.

#include <string>
#include <vector>
#include <unordered_map>

namespace AIML {

enum class AssetKind {
    Texture,
    Audio,
    Shader,
    Font,
    Data,
    Unknown
};

struct AssetEntry {
    std::string rel_path;     // relative to first matching scan root
    std::string abs_path;     // canonical absolute path, used for dedup
};

class AssetScanner {
public:
    static AssetScanner& instance();

    // Parse comma-separated roots and walk each.  Empty input → no-op
    // (registry stays empty; lookups return null).  Hard-errors on:
    //   - duplicate stem in same kind across different abs_paths
    // Warnings (+ skip) on:
    //   - files/dirs whose basename contains uppercase
    //   - extensions not in the known kind map
    // Idempotent: calling scan() twice with the same roots is safe.
    void scan(const std::string& comma_separated_roots);

    // Lookup helpers (used by builtin_registry for compile-time validation
    // and by asset_runtime at runtime).
    bool        has_stem(AssetKind kind, const std::string& stem) const;
    const char* lookup_rel_path(AssetKind kind, const std::string& stem) const;
    const char* lookup_abs_path(AssetKind kind, const std::string& stem) const;
    // Does any kind contain this stem?  Used for "did you mean"-style hints.
    bool        any_kind_has_stem(const std::string& stem) const;
    // Returns true if `path` lies under any scan root (used to detect
    // gl.load_texture("resources/...") string-literal calls that should be
    // gl.tex(stem) instead).
    bool        path_under_root(const std::string& path) const;

    // Test/debug helpers.
    void                                clear();
    size_t                              count(AssetKind kind) const;
    static AssetKind                    classify(const std::string& filename);
    static const char*                  kind_name(AssetKind kind);

    // Runtime-side: insert an already-known (stem, path) entry.  Used by
    // the compiler-emitted aiml_asset_runtime_register call sequence so
    // the wasm/aot binary can rebuild the store without filesystem walks.
    void register_runtime_entry(AssetKind kind, const std::string& stem,
                                const std::string& path);

    // Read-only iteration over all entries, used by the compiler to emit
    // the runtime-register call sequence.
    const std::unordered_map<AssetKind,
        std::unordered_map<std::string, AssetEntry>>& all_stores() const {
        return stores_;
    }

private:
    AssetScanner() = default;
    void scan_root(const std::string& root);
    void register_file(const std::string& abs_path, const std::string& root);

    std::unordered_map<AssetKind,
        std::unordered_map<std::string, AssetEntry>> stores_;
    std::vector<std::string> roots_canonical_;  // for path_under_root
};

}  // namespace AIML
