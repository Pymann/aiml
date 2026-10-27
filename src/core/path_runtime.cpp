#include "path_runtime.h"
#include "../sys/string_runtime.h"
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

// spec/61 §7: string results are owned rc strings (header at ptr-8), never raw malloc.
static char* dup_string(const std::string& s) {
    return aiml_str_ndup(s.c_str(), s.size());
}

extern "C" const char* aiml_path_name(const char* p) {
    if (!p || p[0] == '\0') return aiml_str_dup("");
    try { return dup_string(fs::path(p).filename().string()); } catch (...) { return aiml_str_dup(""); }
}

extern "C" const char* aiml_path_stem(const char* p) {
    if (!p || p[0] == '\0') return aiml_str_dup("");
    try { return dup_string(fs::path(p).stem().string()); } catch (...) { return aiml_str_dup(""); }
}

extern "C" const char* aiml_path_extension(const char* p) {
    if (!p || p[0] == '\0') return aiml_str_dup("");
    try { return dup_string(fs::path(p).extension().string()); } catch (...) { return aiml_str_dup(""); }
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
    return aiml_str_dup(p ? p : g_empty_path);
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

// spec/42: Ausführbar-Bit setzen/entfernen.  Für inhaltsadressierte Bäume ist es
// das EINZIGE Modus-Bit, das im Hash steckt (doc/spec/92 §92.2): es ändert das
// Verhalten, während uid/gid/Zeitstempel Builds nur unreproduzierbar machen.
extern "C" int aiml_io_set_exec(const char* p, int on) {
    if (!p || !*p) return -1;
    try {
        const fs::perms bits = fs::perms::owner_exec | fs::perms::group_exec
                             | fs::perms::others_exec;
        fs::permissions(p, bits,
                        on ? fs::perm_options::add : fs::perm_options::remove);
        return 0;
    } catch (...) { return -1; }
}

// spec/42: symbolischer Link.  `target` darf relativ sein und muss (noch) nicht
// existieren — genau das braucht die Materialisierung eines Baums, dessen
// Link-Ziel erst später entsteht.
extern "C" int aiml_io_symlink(const char* target, const char* link_path) {
    if (!target || !link_path || !*target || !*link_path) return -1;
    try { fs::create_symlink(target, link_path); return 0; } catch (...) { return -1; }
}

// spec/42: ist der Pfad SELBST ein symbolischer Link (ohne ihm zu folgen)?
extern "C" int aiml_io_is_symlink(const char* p) {
    if (!p || !*p) return 0;
    try { return fs::is_symlink(fs::symlink_status(p)) ? 1 : 0; } catch (...) { return 0; }
}

// spec/42 + spec/61 §7: Ziel eines symbolischen Links als owned rc-string;
// "" wenn der Pfad keiner ist oder nicht gelesen werden kann.
extern "C" char* aiml_io_read_link(const char* p) {
    if (!p || !*p) return aiml_str_dup("");
    try {
        if (!fs::is_symlink(fs::symlink_status(p))) return aiml_str_dup("");
        return aiml_str_dup(fs::read_symlink(p).string().c_str());
    } catch (...) { return aiml_str_dup(""); }
}

// spec/42: harter Link — zwei Namen auf denselben Inhalt, ohne zu kopieren.
// Grundlage für inhaltsadressierte Ablagen: eine Datei in den Store aufnehmen
// und einen Baum daraus materialisieren kostet so keinen zweiten Datenblock.
// -1 auch bei Dateisystem-Grenze (dann muss der Aufrufer kopieren) oder wenn
// das Ziel existiert — der Aufrufer entscheidet, ob das ein Fehler ist.
extern "C" int aiml_io_link(const char* src, const char* dst) {
    if (!src || !dst || !*src || !*dst) return -1;
    try { fs::create_hard_link(src, dst); return 0; } catch (...) { return -1; }
}

// spec/42 + spec/61 §7: owned rc string; "" when the file cannot be read.
// Virtuelle Dateien (/proc, /sys) melden GROESSE 0 und liefern trotzdem Inhalt.  Wer die
// Groesse zum Dimensionieren nimmt, bekommt dort eine leere Zeichenkette — gemessen
// 17.10.2026 an `/proc/self/stat` (cosmos doc/spec/109 wollte die eigene CPU-Last lesen
// und bekam 0).  Darum: Groesse als HINWEIS fuer die erste Allokation, gelesen wird bis
// EOF.  Fuer gewoehnliche Dateien bleibt es bei genau einem `fread`.
static char* aiml_io_read_stream(FILE* f, size_t hint) {
    size_t cap = hint > 0 ? hint : 4096;
    size_t len = 0;
    char* buf = aiml_str_alloc(cap);
    if (!buf) return nullptr;
    for (;;) {
        size_t room = cap - len;
        if (room == 0) {
            size_t ncap = cap * 2;
            char* nbuf = aiml_str_alloc(ncap);
            if (!nbuf) { aiml_str_decref(buf); return nullptr; }
            std::memcpy(nbuf, buf, len);
            aiml_str_decref(buf);
            buf = nbuf;
            cap = ncap;
            room = cap - len;
        }
        size_t rd = std::fread(buf + len, 1, room, f);
        len += rd;
        if (rd < room) break;          // EOF oder Fehler
    }
    buf[len] = '\0';
    return buf;
}

extern "C" char* aiml_io_read_file(const char* p) {
    if (!p || p[0] == '\0') return aiml_str_dup("");
    FILE* f = std::fopen(p, "rb");
    if (!f) return aiml_str_dup("");
    long sz = 0;
    if (std::fseek(f, 0, SEEK_END) == 0) {
        sz = std::ftell(f);
        std::rewind(f);
    }
    if (sz < 0) sz = 0;
    char* buf = aiml_io_read_stream(f, static_cast<size_t>(sz));
    std::fclose(f);
    if (!buf) return aiml_str_dup("");
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

// Binary-safe variant used when io.write_file receives a buffer<u8>:
// writes exactly `len` bytes (embedded NULs included).
extern "C" int aiml_io_write_file_buf(const char* p, const void* data, int32_t len) {
    if (!p || p[0] == '\0' || len < 0) return -1;
    FILE* f = std::fopen(p, "wb");
    if (!f) return -1;
    size_t n = static_cast<size_t>(len);
    size_t written = (n > 0 && data) ? std::fwrite(data, 1, n, f) : 0;
    int rc = std::fclose(f);
    if (n > 0 && !data) return -1;
    return (written == n && rc == 0) ? 0 : -1;
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
