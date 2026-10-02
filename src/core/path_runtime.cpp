#include "path_runtime.h"
#include <filesystem>
#include <unordered_map>
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <climits>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

// ── Intern table ─────────────────────────────────────────────────────────────
// Maps normalized path strings → heap-allocated char* (stable, never freed).
// Pointers are used as path identity: p1 == p2  ↔  same address.

// runtime-owned intern table (spec/61 Class 2 path) — never destroyed, so interned strings stay reachable
static auto& g_intern = *new std::unordered_map<std::string, const char*>();
static std::mutex g_mutex;

static const char* g_empty_path = "";  // sentinel for empty paths

static std::string normalize(const std::string& raw) {
    if (raw.empty()) return "";
    try {
        // lexically_normal: resolves . and .., collapses //, no filesystem access
        return fs::path(raw).lexically_normal().string();
    } catch (...) {
        return raw;
    }
}

static const char* intern_str(const std::string& norm) {
    if (norm.empty()) return g_empty_path;
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_intern.find(norm);
    if (it != g_intern.end()) return it->second;
    char* copy = new char[norm.size() + 1];
    std::memcpy(copy, norm.c_str(), norm.size() + 1);
    g_intern[norm] = copy;
    return copy;
}

extern "C" const char* aiml_path_intern(const char* raw) {
    if (!raw || raw[0] == '\0') return g_empty_path;
    return intern_str(normalize(raw));
}

// ── Composition ──────────────────────────────────────────────────────────────

extern "C" const char* aiml_path_join(const char* lhs, const char* rhs) {
    if (!rhs || rhs[0] == '\0') return aiml_path_intern(lhs);
    if (!lhs || lhs[0] == '\0') return aiml_path_intern(rhs);
    try {
        fs::path result = fs::path(lhs) / fs::path(rhs);
        return intern_str(result.lexically_normal().string());
    } catch (...) {
        return aiml_path_intern(lhs);
    }
}

// ── Decomposition ────────────────────────────────────────────────────────────

extern "C" const char* aiml_path_parent(const char* p) {
    if (!p || p[0] == '\0') return g_empty_path;
    try {
        std::string par = fs::path(p).parent_path().lexically_normal().string();
        return intern_str(par);
    } catch (...) { return g_empty_path; }
}

static char* dup_string(const std::string& s) {
    char* buf = static_cast<char*>(std::malloc(s.size() + 1));
    if (buf) std::memcpy(buf, s.c_str(), s.size() + 1);
    return buf;
}

extern "C" const char* aiml_path_name(const char* p) {
    if (!p || p[0] == '\0') return "";
    try { return dup_string(fs::path(p).filename().string()); } catch (...) { return ""; }
}

extern "C" const char* aiml_path_stem(const char* p) {
    if (!p || p[0] == '\0') return "";
    try { return dup_string(fs::path(p).stem().string()); } catch (...) { return ""; }
}

extern "C" const char* aiml_path_extension(const char* p) {
    if (!p || p[0] == '\0') return "";
    try { return dup_string(fs::path(p).extension().string()); } catch (...) { return ""; }
}

extern "C" const char* aiml_path_root(const char* p) {
    if (!p || p[0] == '\0') return g_empty_path;
    try { return intern_str(fs::path(p).root_path().string()); } catch (...) { return g_empty_path; }
}

// ── Properties ───────────────────────────────────────────────────────────────

extern "C" int aiml_path_is_absolute(const char* p) {
    if (!p || p[0] == '\0') return 0;
    try { return fs::path(p).is_absolute() ? 1 : 0; } catch (...) { return 0; }
}

extern "C" int aiml_path_is_relative(const char* p) {
    if (!p || p[0] == '\0') return 1;
    try { return fs::path(p).is_relative() ? 1 : 0; } catch (...) { return 1; }
}

extern "C" int aiml_path_is_empty(const char* p) {
    return (!p || p[0] == '\0') ? 1 : 0;
}

extern "C" int aiml_path_depth(const char* p) {
    if (!p || p[0] == '\0') return 0;
    try {
        int n = 0;
        for (const auto& comp : fs::path(p)) {
            std::string s = comp.string();
            if (s != "/" && s != "\\" && !s.empty()) ++n;
        }
        return n;
    } catch (...) { return 0; }
}

// ── Relative path ─────────────────────────────────────────────────────────────

extern "C" const char* aiml_path_relative_to(const char* p, const char* base) {
    if (!p || !base) return g_empty_path;
    try {
        fs::path rel = fs::path(p).lexically_relative(fs::path(base));
        return intern_str(rel.string());
    } catch (...) { return g_empty_path; }
}

// ── Safe join ────────────────────────────────────────────────────────────────

extern "C" const char* aiml_path_safe_join(const char* base, const char* rhs) {
    if (!base || !rhs) return nullptr;
    try {
        fs::path result = (fs::path(base) / fs::path(rhs)).lexically_normal();
        // Check that result starts with base (after normalization)
        fs::path norm_base = fs::path(base).lexically_normal();
        std::string rb = result.string();
        std::string bb = norm_base.string();
        if (rb.size() < bb.size() || rb.substr(0, bb.size()) != bb) {
            // Traversal detected: result escapes base
            return nullptr;
        }
        return intern_str(result.string());
    } catch (...) { return nullptr; }
}

// ── Conversion ───────────────────────────────────────────────────────────────

extern "C" const char* aiml_path_to_string(const char* p) {
    return p ? p : g_empty_path;
}

// ── Platform directories ─────────────────────────────────────────────────────

extern "C" const char* aiml_path_home(void) {
    const char* h = std::getenv("HOME");
    if (h && h[0]) return intern_str(normalize(h));
    // Fallback: read from /etc/passwd
    return intern_str("/");
}

extern "C" const char* aiml_path_temp_dir(void) {
    try { return intern_str(fs::temp_directory_path().string()); }
    catch (...) { return intern_str("/tmp"); }
}

extern "C" const char* aiml_path_cwd(void) {
    try { return intern_str(fs::current_path().string()); }
    catch (...) {
        char buf[PATH_MAX];
        if (getcwd(buf, sizeof(buf))) return intern_str(buf);
        return g_empty_path;
    }
}

extern "C" const char* aiml_path_current_exe(void) {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = '\0'; return intern_str(buf); }
    return g_empty_path;
}

extern "C" const char* aiml_path_config_dir(const char* app_name) {
    std::string base;
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        base = xdg;
    } else {
        const char* h = std::getenv("HOME");
        base = h ? std::string(h) + "/.config" : "/tmp";
    }
    if (app_name && app_name[0])
        base += std::string("/") + app_name;
    return intern_str(normalize(base));
}

// ── Filesystem operations ─────────────────────────────────────────────────────

extern "C" int aiml_io_exists(const char* p) {
    if (!p || p[0] == '\0') return 0;
    try { return fs::exists(p) ? 1 : 0; } catch (...) { return 0; }
}

extern "C" int aiml_io_is_dir(const char* p) {
    if (!p || p[0] == '\0') return 0;
    try { return fs::is_directory(p) ? 1 : 0; } catch (...) { return 0; }
}

extern "C" int aiml_io_is_file(const char* p) {
    if (!p || p[0] == '\0') return 0;
    try { return fs::is_regular_file(p) ? 1 : 0; } catch (...) { return 0; }
}

extern "C" long long aiml_io_file_size(const char* p) {
    if (!p || p[0] == '\0') return -1;
    try { return static_cast<long long>(fs::file_size(p)); }
    catch (...) { return -1; }
}

extern "C" int aiml_io_mkdir(const char* p) {
    if (!p || p[0] == '\0') return -1;
    try { return fs::create_directory(p) ? 0 : -1; } catch (...) { return -1; }
}

extern "C" int aiml_io_mkdir_all(const char* p) {
    if (!p || p[0] == '\0') return -1;
    try { fs::create_directories(p); return 0; } catch (...) { return -1; }
}

extern "C" int aiml_io_remove(const char* p) {
    if (!p || p[0] == '\0') return -1;
    try { return fs::remove(p) ? 0 : -1; } catch (...) { return -1; }
}

extern "C" int aiml_io_remove_all(const char* p) {
    if (!p || p[0] == '\0') return -1;
    try { fs::remove_all(p); return 0; } catch (...) { return -1; }
}

extern "C" int aiml_io_rename(const char* src, const char* dst) {
    if (!src || !dst) return -1;
    try { fs::rename(src, dst); return 0; } catch (...) { return -1; }
}

extern "C" char* aiml_io_read_file(const char* p) {
    if (!p || p[0] == '\0') return nullptr;
    FILE* f = std::fopen(p, "rb");
    if (!f) return nullptr;
    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return nullptr; }
    long sz = std::ftell(f);
    if (sz < 0) { std::fclose(f); return nullptr; }
    std::rewind(f);
    char* buf = static_cast<char*>(std::malloc(static_cast<size_t>(sz) + 1));
    if (!buf) { std::fclose(f); return nullptr; }
    size_t rd = std::fread(buf, 1, static_cast<size_t>(sz), f);
    buf[rd] = '\0';
    std::fclose(f);
    return buf;
}

extern "C" int aiml_io_write_file(const char* p, const char* content) {
    if (!p || p[0] == '\0') return -1;
    FILE* f = std::fopen(p, "wb");
    if (!f) return -1;
    size_t len = content ? std::strlen(content) : 0;
    if (len > 0) {
        size_t written = std::fwrite(content, 1, len, f);
        std::fclose(f);
        return (written == len) ? 0 : -1;
    }
    std::fclose(f);
    return 0;
}

// ── AimlList layout (must match GenericList in list_impl.cpp) ────────────────
struct AimlListRaw {
    int32_t size;
    int32_t capacity;
    void**  data;
};

extern "C" int aiml_io_list_dir_fill(const char* p, void* list_ptr) {
    if (!p || p[0] == '\0' || !list_ptr) return -1;
    AimlListRaw* list = static_cast<AimlListRaw*>(list_ptr);
    int count = 0;
    try {
        for (auto& entry : fs::directory_iterator(p)) {
            const char* interned = intern_str(entry.path().lexically_normal().string());
            // Grow data array if needed
            if (list->size >= list->capacity) {
                int newCap = list->capacity * 2;
                void** newData = static_cast<void**>(std::realloc(list->data, static_cast<size_t>(newCap) * sizeof(void*)));
                if (!newData) return count;
                list->data = newData;
                list->capacity = newCap;
            }
            list->data[list->size++] = const_cast<char*>(interned);
            ++count;
        }
    } catch (...) { return count; }
    return count;
}

extern "C" int aiml_io_can_read(const char* p) {
    return (p && access(p, R_OK) == 0) ? 1 : 0;
}

extern "C" int aiml_io_can_write(const char* p) {
    return (p && access(p, W_OK) == 0) ? 1 : 0;
}

extern "C" int aiml_io_can_exec(const char* p) {
    return (p && access(p, X_OK) == 0) ? 1 : 0;
}
