// Filesystem globbing (spec/42_1): io.glob_match, io.glob and the incremental
// Globber.  One segment matcher serves all three.

#include "glob_runtime.h"
#include "string_runtime.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

std::vector<std::string> split_segments(const std::string& text) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t slash = text.find('/', start);
        std::string seg = text.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!seg.empty()) out.push_back(seg);
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return out;
}

bool has_wildcard(const std::string& seg) {
    return seg.find_first_of("*?[\\") != std::string::npos;
}

// `[...]` at pattern[i] (i on '['): sets `matched` for `c`, returns the index
// after ']', or npos when the class is not closed (then '[' is literal).
size_t match_class(const std::string& pat, size_t i, char c, bool& matched) {
    size_t j = i + 1;
    bool negate = j < pat.size() && (pat[j] == '!' || pat[j] == '^');
    if (negate) ++j;
    bool hit = false;
    bool first = true;
    while (j < pat.size() && (pat[j] != ']' || first)) {
        first = false;
        char lo = pat[j];
        if (lo == '\\' && j + 1 < pat.size()) lo = pat[++j];
        char hi = lo;
        if (j + 2 < pat.size() && pat[j + 1] == '-' && pat[j + 2] != ']') {
            hi = pat[j + 2];
            if (hi == '\\' && j + 3 < pat.size()) { hi = pat[j + 3]; ++j; }
            j += 2;
        }
        if (c >= lo && c <= hi) hit = true;
        ++j;
    }
    if (j >= pat.size()) return std::string::npos;
    matched = hit != negate;
    return j + 1;
}

// One segment: `*`, `?`, `[…]`, `\x`; wildcards never match a leading dot
// unless the pattern spells it out.
bool match_segment(const std::string& pat, const std::string& name) {
    if (name == "." || name == "..") return pat == name;
    if (!name.empty() && name[0] == '.' && (pat.empty() || pat[0] != '.')) return false;
    size_t p = 0, n = 0;
    size_t star_p = std::string::npos, star_n = 0;
    while (n < name.size()) {
        if (p < pat.size() && pat[p] == '*') {
            star_p = p++;
            star_n = n;
            continue;
        }
        bool advanced = false;
        if (p < pat.size()) {
            if (pat[p] == '?') { ++p; ++n; advanced = true; }
            else if (pat[p] == '[') {
                bool hit = false;
                size_t after = match_class(pat, p, name[n], hit);
                if (after == std::string::npos) {
                    if (name[n] == '[') { ++p; ++n; advanced = true; }
                } else if (hit) { p = after; ++n; advanced = true; }
            } else {
                char want = pat[p];
                size_t width = 1;
                if (want == '\\' && p + 1 < pat.size()) { want = pat[p + 1]; width = 2; }
                if (want == name[n]) { p += width; ++n; advanced = true; }
            }
        }
        if (advanced) continue;
        if (star_p == std::string::npos) return false;
        p = star_p + 1;                    // let the last * take one more character
        n = ++star_n;
    }
    while (p < pat.size() && pat[p] == '*') ++p;
    return p == pat.size();
}

// Segments of a pattern against segments of a path, with `**`.
bool match_from(const std::vector<std::string>& pat, size_t pi,
                const std::vector<std::string>& path, size_t ni) {
    if (pi == pat.size()) return ni == path.size();
    if (pat[pi] == "**") {
        if (pi + 1 == pat.size()) {        // trailing **: everything below, no hidden levels
            for (size_t k = ni; k < path.size(); ++k) if (path[k][0] == '.') return false;
            return ni < path.size();
        }
        for (size_t k = ni; k <= path.size(); ++k) {
            if (match_from(pat, pi + 1, path, k)) return true;
            if (k < path.size() && path[k][0] == '.') return false;
        }
        return false;
    }
    return ni < path.size() && match_segment(pat[pi], path[ni]) && match_from(pat, pi + 1, path, ni + 1);
}

struct Entry {
    std::string name;
    bool dir;          // a real directory (a link to one is not walked)
};

std::vector<Entry> list_dir(const std::string& dir) {
    std::vector<Entry> out;
    DIR* d = opendir(dir.empty() ? "." : dir.c_str());
    if (!d) return out;
    while (struct dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        bool is_dir = e->d_type == DT_DIR;
        if (e->d_type == DT_UNKNOWN) {
            struct stat st {};
            std::string full = (dir.empty() ? "" : dir + "/") + name;
            is_dir = lstat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        }
        out.push_back({name, is_dir});
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
    return out;
}

std::string join(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    return dir.back() == '/' ? dir + name : dir + "/" + name;
}

}  // namespace

/* ── the walker ──────────────────────────────────────────────────────── */

struct AimlGlobber {
    std::string pattern;
    std::vector<std::string> segs;       // pattern segments after the literal base
    int32_t     max_per_step = 1;
    // pending directories: (directory, index of the segment to match there)
    std::vector<std::pair<std::string, size_t>> stack;
    std::set<std::pair<std::string, size_t>> seen;   // directory states already walked
    std::vector<std::string> ready;      // matches of `location` not yet handed out
    size_t      ready_at = 0;
    std::string location;
    int32_t     done = 0;
};

namespace {

// Process one pending directory: its matches go to g.ready, its children that
// the pattern can still descend into go on the stack (depth-first, byte order).
void visit(AimlGlobber& g, const std::string& dir, size_t si) {
    std::vector<Entry> entries = list_dir(dir);
    const std::string& seg = g.segs[si];
    std::vector<std::pair<std::string, size_t>> children;
    if (seg == "**") {
        bool trailing = si + 1 == g.segs.size();
        for (const Entry& e : entries) {
            if (e.name[0] == '.') continue;
            if (trailing) g.ready.push_back(join(dir, e.name));
            if (e.dir) children.push_back({join(dir, e.name), si});
        }
        if (!trailing) {                 // ** = zero levels: the next segment here
            const std::string& next = g.segs[si + 1];
            bool last = si + 2 == g.segs.size();
            for (const Entry& e : entries) {
                if (!match_segment(next, e.name)) continue;
                if (last) g.ready.push_back(join(dir, e.name));
                else if (e.dir) children.push_back({join(dir, e.name), si + 2});
            }
        }
    } else {
        bool last = si + 1 == g.segs.size();
        for (const Entry& e : entries) {
            if (!match_segment(seg, e.name)) continue;
            if (last) g.ready.push_back(join(dir, e.name));
            else if (e.dir) children.push_back({join(dir, e.name), si + 1});
        }
    }
    std::sort(g.ready.begin(), g.ready.end());
    g.ready.erase(std::unique(g.ready.begin(), g.ready.end()), g.ready.end());
    std::sort(children.begin(), children.end());
    children.erase(std::unique(children.begin(), children.end()), children.end());
    for (auto it = children.rbegin(); it != children.rend(); ++it) g.stack.push_back(*it);
}

}  // namespace

extern "C" void* aiml_glob_new(const char* pattern, int32_t max_per_step) {
    AimlGlobber* g = new AimlGlobber;
    g->pattern = pattern ? pattern : "";
    g->max_per_step = max_per_step < 1 ? 1 : max_per_step;
    std::vector<std::string> all = split_segments(g->pattern);
    std::string base = (!g->pattern.empty() && g->pattern[0] == '/') ? "/" : "";
    size_t i = 0;
    while (i + 1 < all.size() && !has_wildcard(all[i])) {      // literal leading directories
        base = base.empty() ? all[i] : join(base, all[i]);
        ++i;
    }
    g->segs.assign(all.begin() + (long)i, all.end());
    if (!g->segs.empty()) g->stack.push_back({base, 0});
    return g;
}

namespace {

// Load the next directory that has matches into g.ready.  false = finished.
bool advance(AimlGlobber& g) {
    while (g.ready_at >= g.ready.size()) {
        g.ready.clear();
        g.ready_at = 0;
        if (g.stack.empty()) { g.done = 1; return false; }
        auto top = g.stack.back();
        g.stack.pop_back();
        if (!g.seen.insert(top).second) continue;    // reached again via another ** route
        g.location = top.first.empty() ? "." : top.first;
        visit(g, top.first, top.second);
    }
    return true;
}

}  // namespace

// The next batch: up to max_per_step matches of one directory.  0 = finished.
extern "C" int32_t aiml_glob_next_fill(void* handle, void* list) {
    AimlGlobber* g = static_cast<AimlGlobber*>(handle);
    if (!g || g->done || !advance(*g)) return 0;
    int32_t n = 0;
    while (g->ready_at < g->ready.size() && n < g->max_per_step) {
        const std::string& hit = g->ready[g->ready_at++];
        aiml_list_push_str_copy(list, hit.data(), hit.size());
        ++n;
    }
    return n;
}

extern "C" char* aiml_glob_location(void* handle) {
    AimlGlobber* g = static_cast<AimlGlobber*>(handle);
    return aiml_str_dup(g ? g->location.c_str() : "");
}

extern "C" char* aiml_glob_pattern(void* handle) {
    AimlGlobber* g = static_cast<AimlGlobber*>(handle);
    return aiml_str_dup(g ? g->pattern.c_str() : "");
}

extern "C" int32_t aiml_glob_done(void* handle) {
    AimlGlobber* g = static_cast<AimlGlobber*>(handle);
    return g ? g->done : 1;
}

extern "C" void aiml_glob_free(void* handle) {
    delete static_cast<AimlGlobber*>(handle);
}

// io.glob(pattern): every match, sorted by byte order.
extern "C" int32_t aiml_glob_all_fill(const char* pattern, void* list) {
    AimlGlobber* g = static_cast<AimlGlobber*>(aiml_glob_new(pattern, 1));
    std::vector<std::string> all;
    while (advance(*g)) {
        all.insert(all.end(), g->ready.begin() + (long)g->ready_at, g->ready.end());
        g->ready_at = g->ready.size();
    }
    aiml_glob_free(g);
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end()), all.end());
    for (const std::string& hit : all) aiml_list_push_str_copy(list, hit.data(), hit.size());
    return (int32_t)all.size();
}

// io.glob_match(pattern, text): the path text against the pattern, no filesystem.
extern "C" int32_t aiml_glob_match(const char* pattern, const char* text) {
    std::string pat = pattern ? pattern : "", path = text ? text : "";
    if (pat.empty()) return 0;
    if ((pat[0] == '/') != (!path.empty() && path[0] == '/')) return 0;
    return match_from(split_segments(pat), 0, split_segments(path), 0) ? 1 : 0;
}
