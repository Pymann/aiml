// venv.cpp — see venv.h (spec/68).
#include "venv.h"

#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "asset_scanner.h"

namespace fs = std::filesystem;

// §6.1 borrows the runtime the compiler already links: the HTTP client of
// spec/32_2 (binary-safe download, headers, redirects, TLS) and the untar of
// spec/33_2 §3.7a.  A second HTTP client in the compiler, or a dependency on
// curl, would be two ways of doing one thing.
extern "C" {
void*   aiml_url_parse(const char* raw);
void*   aiml_http_new_request(const char* method, void* url);
void*   aiml_http_client_get_headers(void* client);
void    aiml_http_map_put(void* map, const char* key, const char* val);
void    aiml_http_client_set_download_to(void* client, const char* path);
void    aiml_http_client_set_body_file(void* client, const char* path);
void    aiml_http_client_set_timeout(void* client, int32_t timeout_ms);
void*   aiml_http_client_send(void* client);
void    aiml_http_client_free(void* client);
int32_t aiml_http_resp_status(void* resp);
char*   aiml_http_resp_body(void* resp);
void    aiml_http_resp_free(void* resp);
char*   aiml_http_last_error(void);
int32_t aiml_compress_untar(const char* src, const char* dst_dir);
char*   aiml_sha256_file(const char* path);
}

namespace aiml {
namespace venv {

const char* const kMarker = "# aiml venv";

namespace {

bool isVenv(const fs::path& root) {
    std::ifstream in(root / "env");
    std::string first;
    return in && std::getline(in, first) && first.rfind(kMarker, 0) == 0;
}

// The file this code was loaded from: the aiml binary or libaiml.so.
fs::path ownImage() {
    Dl_info info;
    if (!dladdr(reinterpret_cast<void*>(&ownRoot), &info) || !info.dli_fname) return {};
    // A shared object is named by the path it was loaded from; the main program by its
    // argv[0], which is relative when it was started via PATH.
    std::string name = info.dli_fname;
    std::error_code ec;
    fs::path p = fs::canonical(name.rfind('/', 0) == 0 ? name : std::string("/proc/self/exe"), ec);
    return ec ? fs::path() : p;
}

fs::path runningBinary() {
    std::error_code ec;
    fs::path p = fs::canonical("/proc/self/exe", ec);
    return ec ? fs::path() : p;
}

// F68-11: --runtime, else next to a venv compiler, else next to a build-tree compiler.
fs::path findRuntime(const fs::path& exe, const std::string& given) {
    if (!given.empty()) return fs::is_regular_file(given) ? fs::absolute(given) : fs::path();
    fs::path dir = exe.parent_path();
    for (const fs::path& cand : {dir / "../lib/libaiml.so", dir / "../libaiml/libaiml.so"}) {
        std::error_code ec;
        if (fs::is_regular_file(cand, ec)) return fs::canonical(cand, ec);
    }
    return {};
}

bool sameFile(const fs::path& lhs, const fs::path& rhs) {
    std::error_code ec;
    return fs::exists(rhs, ec) && fs::equivalent(lhs, rhs, ec);
}

// F68-12: write next to the target, then rename over it.
bool installFile(const fs::path& src, const fs::path& dst, fs::perms mode, std::string& err) {
    if (sameFile(src, dst)) return true;
    fs::path tmp = dst;
    tmp += ".tmp." + std::to_string(getpid());
    std::error_code ec;
    fs::copy_file(src, tmp, fs::copy_options::overwrite_existing, ec);
    if (!ec) fs::permissions(tmp, mode, ec);
    if (!ec) fs::rename(tmp, dst, ec);
    if (ec) {
        fs::remove(tmp);
        err = "cannot install " + dst.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool writeText(const fs::path& dst, const std::string& text, std::string& err) {
    fs::path tmp = dst;
    tmp += ".tmp." + std::to_string(getpid());
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << text;
        if (!out) { err = "cannot write " + tmp.string(); return false; }
    }
    std::error_code ec;
    fs::rename(tmp, dst, ec);
    if (ec) { fs::remove(tmp); err = "cannot write " + dst.string() + ": " + ec.message(); }
    return !ec;
}

std::string envText(const std::string& root) {
    std::ostringstream ss;
    ss << kMarker << " — written by `aiml venv`; use with `. " << root << "/env`\n"
       << "export AIML_VENV='" << root << "'\n"
       << "export AIML='" << root << "/bin/aiml'\n"
       << "export PATH='" << root << "/bin'\"${PATH:+:$PATH}\"\n";
    return ss.str();
}

int usage() {
    std::cerr << "usage: aiml venv <dir> [--runtime <libaiml.so>]\n"
                 "       aiml venv <dir> install <name> (--git <url> [--ref <r>] | --path <dir>\n"
                 "                                       | --kontor <url> [--version <v>])\n"
                 "       aiml venv <dir> install --all --kontor <url>\n"
                 "       aiml venv <dir> publish [<name>] [--path <dir>] --kontor <url> [--overwrite]\n"
                 "       aiml venv <dir> list | remove <name>\n"
                 "       aiml venv <dir> asset install <name> (--kontor <url> --version <v> | --path <archive.tar.gz>)\n"
                 "       aiml venv <dir> asset publish <name> --version <v> --path <dir> --kontor <url> [--overwrite]\n"
                 "       aiml venv <dir> asset list | asset remove <name>\n"
                 "       aiml venv <dir> update [--check] [--version <v>] [--github <owner>/<repo>]\n"
                 "       aiml venv <dir> update --kontor <url> --version <v>" << std::endl;
    return 1;
}

// ── §6: installing tools ──────────────────────────────────────────────────
//
// A tool is fetched, built WITH THIS VENV and only its results are placed.
// What gets placed is the repository's own business (`aiml.tool.json`,
// F68-54); the venv keeps a record of it (`aiml.installed.json`, F68-56) so
// that `list` and `remove` know exactly what they are talking about.

// What a repository declares (F68-54).
struct Manifest {
    std::string name, version, package;
    std::vector<std::string> programs, libraries;
    std::vector<std::string> configs;   // empty: `build --all` (F68-53)
};

// What the venv remembers per installed tool (F68-56).
struct Record {
    std::string source, commit, version;
    std::vector<std::string> files;   // relative to the venv
};

// The object cache (spec/29_2 §12): $AIML_CACHE, else ./__aiml_cache.  A venv
// holds no cache (F68-2), so this is where a clone and its build live.
fs::path cacheRoot() {
    const char* env = std::getenv("AIML_CACHE");
    if (env && *env) return fs::absolute(env);
    std::error_code ec;
    fs::path cwd = fs::current_path(ec);
    return (ec ? fs::path(".") : cwd) / "__aiml_cache";
}

// Run a program — no shell, so nothing in a URL or a name is ever
// interpreted.  `out` captures stdout when given; otherwise the child keeps
// this process's stdout, and the build's output is simply passed through
// (F68-53).  Returns the exit code, 127 when the program was not found.
int runProg(const std::vector<std::string>& args, const fs::path& cwd,
            const std::vector<std::string>& envAdd, std::string* out) {
    int fds[2] = {-1, -1};
    if (out && pipe(fds) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) {
        if (out) { close(fds[0]); close(fds[1]); }
        return -1;
    }
    if (pid == 0) {
        if (out) { dup2(fds[1], 1); close(fds[0]); close(fds[1]); }
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) _exit(127);
        for (const std::string& kv : envAdd) {
            size_t eq = kv.find('=');
            if (eq != std::string::npos)
                setenv(kv.substr(0, eq).c_str(), kv.substr(eq + 1).c_str(), 1);
        }
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    if (out) {
        close(fds[1]);
        char buf[4096];
        ssize_t n;
        while ((n = read(fds[0], buf, sizeof buf)) > 0) out->append(buf, static_cast<size_t>(n));
        close(fds[0]);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

bool allHex(const std::string& s) {
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return !s.empty();
}

// F68-50: the ref becomes a commit BEFORE the clone, so what was built is
// written down.  A 40-hex ref is already one.
bool gitCommit(const std::string& url, const std::string& ref, std::string& sha, std::string& err) {
    std::string want = ref.empty() ? "HEAD" : ref;
    if (want.size() == 40 && allHex(want)) { sha = want; return true; }
    std::string out;
    int rc = runProg({"git", "ls-remote", url, want}, {}, {}, &out);
    if (rc == 127) { err = "git is not on PATH (F68-51)"; return false; }
    if (rc != 0) { err = "git ls-remote " + url + " failed (exit " + std::to_string(rc) + ")"; return false; }
    std::istringstream in(out);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string hex;
        if (ls >> hex && hex.size() == 40 && allHex(hex)) { sha = hex; return true; }
    }
    err = url + " has no ref '" + want + "'";
    return false;
}

// F68-50/52: fetch exactly that commit into the cache.  A directory that
// already holds it is reused — a second install does not clone again.
bool gitFetch(const std::string& url, const std::string& ref, const std::string& sha,
              const fs::path& dir, std::string& err) {
    std::error_code ec;
    if (fs::exists(dir / ".git", ec)) return true;          // already there
    fs::create_directories(dir, ec);
    if (ec) { err = "cannot create " + dir.string() + ": " + ec.message(); return false; }
    if (runProg({"git", "init", "-q", dir.string()}, {}, {}, nullptr) != 0) {
        err = "git init failed in " + dir.string();
        return false;
    }
    std::string want = ref.empty() ? "HEAD" : ref;
    if (runProg({"git", "-C", dir.string(), "fetch", "-q", "--depth", "1", url, want},
                {}, {}, nullptr) != 0) {
        err = "git fetch " + url + " " + want + " failed";
        return false;
    }
    std::string head;
    if (runProg({"git", "-C", dir.string(), "rev-parse", "FETCH_HEAD"}, {}, {}, &head) != 0) {
        err = "cannot read FETCH_HEAD in " + dir.string();
        return false;
    }
    while (!head.empty() && (head.back() == '\n' || head.back() == '\r')) head.pop_back();
    if (head != sha) {
        // Between resolving and fetching, the ref moved.  Saying so is better
        // than recording a commit that is not what was built.
        err = "'" + want + "' moved while installing (" + sha + " → " + head + "); try again";
        return false;
    }
    if (runProg({"git", "-C", dir.string(), "checkout", "-q", "--detach", "FETCH_HEAD"},
                {}, {}, nullptr) != 0) {
        err = "git checkout failed in " + dir.string();
        return false;
    }
    return true;
}

std::vector<std::string> strList(const YAML::Node& node) {
    std::vector<std::string> out;
    if (node && node.IsSequence())
        for (const auto& item : node)
            if (item.IsScalar()) out.push_back(item.as<std::string>());
    return out;
}

// F68-54: the repository says what to install.  No file, no guessing.
bool readManifest(const fs::path& root, Manifest& man, std::string& err) {
    fs::path file = root / "aiml.tool.json";
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        err = root.string() + " has no aiml.tool.json — a venv cannot guess what to install "
              "(spec/68 F68-54)";
        return false;
    }
    try {
        YAML::Node doc = YAML::LoadFile(file.string());   // JSON is YAML
        if (doc["name"]) man.name = doc["name"].as<std::string>();
        if (doc["version"]) man.version = doc["version"].as<std::string>();
        if (doc["package"]) man.package = doc["package"].as<std::string>();
        man.programs = strList(doc["programs"]);
        man.libraries = strList(doc["libraries"]);
        man.configs = strList(doc["configs"]);
    } catch (const std::exception& e) {
        err = file.string() + ": " + e.what();
        return false;
    }
    if (man.name.empty()) { err = file.string() + " has no \"name\""; return false; }
    return true;
}

// F68-53: a tool whose build configuration names somebody's build tree would
// be built by a foreign compiler.  Refused, with the way out.
bool configsUseVenv(const fs::path& root, std::string& err) {
    std::error_code ec;
    for (const auto& ent : fs::directory_iterator(root, ec)) {
        if (ec) break;
        const fs::path& file = ent.path();
        if (file.extension() != ".json") continue;
        std::string base = file.filename().string();
        if (base.rfind("exodus", 0) != 0) continue;
        try {
            YAML::Node doc = YAML::LoadFile(file.string());
            YAML::Node comp = doc["compiler"];
            if (!comp || !comp["name"]) continue;
            std::string name = comp["name"].as<std::string>();
            if (name.find("$AIML") != std::string::npos) continue;
            if (name.find('/') == std::string::npos) continue;   // a bare name: PATH decides
            err = base + " names the compiler as '" + name + "' — a path into a build tree. "
                  "A tool is built with the venv it will live in: write \"$AIML\" and "
                  "\"$AIML_VENV/lib\" (spec/68 F68-23)";
            return false;
        } catch (const std::exception&) {
            continue;   // not our file to judge
        }
    }
    return true;
}

std::string jsonEscape(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out;
}

// ── the two tables of a venv ──────────────────────────────────────────────

// aiml.packages.json: read every entry with all its scalar fields, so that
// rewriting it keeps what somebody else put there (a sha256 pin, say).
using Table = std::map<std::string, std::map<std::string, std::string>>;

Table readTable(const fs::path& file) {
    Table t;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return t;
    try {
        YAML::Node doc = YAML::LoadFile(file.string());
        YAML::Node pkgs = doc["packages"];
        if (!pkgs || !pkgs.IsMap()) return t;
        for (const auto& kv : pkgs) {
            std::string name = kv.first.as<std::string>();
            if (kv.second.IsScalar()) { t[name]["path"] = kv.second.as<std::string>(); continue; }
            if (!kv.second.IsMap()) continue;
            for (const auto& field : kv.second)
                if (field.second.IsScalar())
                    t[name][field.first.as<std::string>()] = field.second.as<std::string>();
        }
    } catch (const std::exception&) {}
    return t;
}

bool writeTable(const fs::path& file, const Table& t, std::string& err) {
    std::ostringstream ss;
    ss << "{\"packages\": {";
    bool firstPkg = true;
    for (const auto& pkg : t) {
        ss << (firstPkg ? "\n  " : ",\n  ") << "\"" << jsonEscape(pkg.first) << "\": {";
        firstPkg = false;
        bool firstField = true;
        for (const auto& field : pkg.second) {
            ss << (firstField ? "" : ", ") << "\"" << jsonEscape(field.first) << "\": \""
               << jsonEscape(field.second) << "\"";
            firstField = false;
        }
        ss << "}";
    }
    ss << (firstPkg ? "}}\n" : "\n}}\n");
    return writeText(file, ss.str(), err);
}

using Records = std::map<std::string, Record>;

Records readRecords(const fs::path& file) {
    Records r;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return r;
    try {
        YAML::Node doc = YAML::LoadFile(file.string());
        YAML::Node tools = doc["tools"];
        if (!tools || !tools.IsMap()) return r;
        for (const auto& kv : tools) {
            Record rec;
            const YAML::Node& v = kv.second;
            if (v["source"]) rec.source = v["source"].as<std::string>();
            if (v["commit"]) rec.commit = v["commit"].as<std::string>();
            if (v["version"]) rec.version = v["version"].as<std::string>();
            rec.files = strList(v["files"]);
            r[kv.first.as<std::string>()] = rec;
        }
    } catch (const std::exception&) {}
    return r;
}

bool writeRecords(const fs::path& file, const Records& r, std::string& err) {
    std::ostringstream ss;
    ss << "{\"tools\": {";
    bool first = true;
    for (const auto& kv : r) {
        ss << (first ? "\n  " : ",\n  ") << "\"" << jsonEscape(kv.first) << "\": {"
           << "\"source\": \"" << jsonEscape(kv.second.source) << "\", "
           << "\"commit\": \"" << jsonEscape(kv.second.commit) << "\", "
           << "\"version\": \"" << jsonEscape(kv.second.version) << "\", "
           << "\"files\": [";
        first = false;
        bool firstFile = true;
        for (const std::string& f : kv.second.files) {
            ss << (firstFile ? "\"" : ", \"") << jsonEscape(f) << "\"";
            firstFile = false;
        }
        ss << "]}";
    }
    ss << (first ? "}}\n" : "\n}}\n");
    return writeText(file, ss.str(), err);
}

// F68-54: the package directory may be the repository root ("."), and a
// repository root also holds what the BUILD left there.  Those are not part
// of the package, and a venv is a layer: copying `out/` and the caches in
// would make it bigger than the tool and different on every machine.
bool skipInPackage(const std::string& name) {
    static const char* const kSkip[] = {".git", "out", "build", "__aiml_cache", "__exodus_cache"};
    for (const char* s : kSkip)
        if (name == s) return true;
    return name.size() > 2 && name.compare(name.size() - 2, 2, ".o") == 0;
}

// `skipPaths` are the build outputs this install places itself: a program
// already in `bin/` has no business being shipped a second time inside the
// package.
bool copyPackage(const fs::path& from, const fs::path& to,
                 const std::vector<fs::path>& skipPaths, std::string& err) {
    std::error_code ec;
    fs::create_directories(to, ec);
    if (ec) { err = "cannot create " + to.string() + ": " + ec.message(); return false; }
    for (const auto& ent : fs::directory_iterator(from, ec)) {
        if (ec) { err = "cannot read " + from.string() + ": " + ec.message(); return false; }
        std::string name = ent.path().filename().string();
        if (skipInPackage(name)) continue;
        bool isOutput = false;
        for (const fs::path& skip : skipPaths) {
            std::error_code same;
            if (fs::equivalent(ent.path(), skip, same) && !same) isOutput = true;
        }
        if (isOutput) continue;
        if (ent.is_directory()) {
            if (!copyPackage(ent.path(), to / name, skipPaths, err)) return false;
            continue;
        }
        fs::copy_file(ent.path(), to / name, fs::copy_options::overwrite_existing, ec);
        if (ec) { err = "cannot copy " + ent.path().string() + ": " + ec.message(); return false; }
    }
    return true;
}

// F68-55: the copy keeps the RUNPATH its build gave it.  Checked, not
// patched: whoever installs must not quietly rewrite a binary.
//
// Asked only of a file that needs the venv's runtime.  A file that needs
// nothing of ours is relocatable without any RUNPATH — cosmos' librt_shim.so
// needs libc and nothing else, and warning about that would be noise that
// teaches people to ignore warnings.
bool runpathOk(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return true;
    std::string blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (blob.find("libaiml.so") == std::string::npos) return true;
    return blob.find("$ORIGIN") != std::string::npos;
}

int fail(const std::string& msg) {
    std::cerr << "aiml venv: " << msg << std::endl;
    return 1;
}

// ── §6 verbs ──────────────────────────────────────────────────────────────

fs::path tableFile(const fs::path& root) { return root / "lib" / "aiml" / "aiml.packages.json"; }
fs::path recordFile(const fs::path& root) { return root / "lib" / "aiml" / "aiml.installed.json"; }

// F68-56: remove exactly what was placed — a directory as a whole, a file as
// a file, and nothing that is not in the record.  A file another install lists
// too (a library both load, F68-58a) stays for that one.
void dropFiles(const fs::path& root, const Record& rec, const std::map<std::string, Record>& all,
               const std::string& self) {
    std::error_code ec;
    for (const std::string& rel : rec.files) {
        bool shared = false;
        for (const auto& other : all)
            if (other.first != self &&
                std::find(other.second.files.begin(), other.second.files.end(), rel) != other.second.files.end())
                shared = true;
        if (shared) continue;
        fs::path at = root / rel;
        if (fs::is_directory(at, ec)) fs::remove_all(at, ec);
        else fs::remove(at, ec);
    }
}

int cmdList(const fs::path& root) {
    Records recs = readRecords(recordFile(root));
    if (recs.empty()) {
        std::cout << "no tools installed in " << root.string() << std::endl;
        return 0;
    }
    for (const auto& kv : recs) {
        const Record& r = kv.second;
        std::cout << kv.first;
        if (!r.version.empty()) std::cout << " " << r.version;
        std::cout << "  " << r.source;
        if (!r.commit.empty()) std::cout << " @ " << r.commit;
        else std::cout << "  (unreproducible: a local tree, F68-50)";
        std::cout << "  " << r.files.size() << " file(s)" << std::endl;
    }
    return 0;
}

int cmdRemove(const fs::path& root, const std::string& name) {
    fs::path recAt = recordFile(root);
    Records recs = readRecords(recAt);
    auto it = recs.find(name);
    if (it == recs.end())
        return fail("'" + name + "' is not installed in " + root.string());
    dropFiles(root, it->second, recs, name);
    recs.erase(it);
    Table table = readTable(tableFile(root));
    table.erase(name);
    std::string err;
    if (!writeTable(tableFile(root), table, err)) return fail(err);
    if (!writeRecords(recAt, recs, err)) return fail(err);
    std::cout << "removed " << name << " from " << root.string() << std::endl;
    return 0;
}

// ── §6.1: kontor as a source ──────────────────────────────────────────────

// One request.  `bodyFile` non-empty: the request body is streamed from that
// file (a PUT of §6.3).  `toFile` empty: the answer's body comes back in `body`;
// otherwise a 2xx body is streamed into that file (the package, binary) and
// any other answer stays in `body`.  `status` is the HTTP status, 0 when there
// was no answer at all (then `err` says why and the call returns false).
// F68-63: a token goes into the request, never onto a command line.
bool httpCall(const std::string& method, const std::string& url, const fs::path& bodyFile,
              const fs::path& toFile, int32_t& status, std::string& body, std::string& err,
              const std::vector<std::pair<std::string, std::string>>& headers = {},
              bool kontorAuth = true) {
    status = 0;
    void* u = aiml_url_parse(url.c_str());
    if (!u) { err = "not a URL: " + url; return false; }
    void* req = aiml_http_new_request(method.c_str(), u);
    if (!req) { err = "cannot build a request for " + url; return false; }
    const char* token = kontorAuth ? std::getenv("KONTOR_TOKEN") : nullptr;   // never to another host
    if (token && *token)
        aiml_http_map_put(aiml_http_client_get_headers(req), "Authorization",
                          (std::string("Bearer ") + token).c_str());
    for (const auto& h : headers)
        aiml_http_map_put(aiml_http_client_get_headers(req), h.first.c_str(), h.second.c_str());
    if (!bodyFile.empty()) aiml_http_client_set_body_file(req, bodyFile.string().c_str());
    if (!toFile.empty()) aiml_http_client_set_download_to(req, toFile.string().c_str());
    aiml_http_client_set_timeout(req, 120000);
    void* resp = aiml_http_client_send(req);
    if (!resp) {
        char* why = aiml_http_last_error();
        err = url + ": " + (why && *why ? why : "no answer");
        aiml_http_client_free(req);
        return false;
    }
    status = aiml_http_resp_status(resp);
    char* text = aiml_http_resp_body(resp);
    body = text ? text : "";
    aiml_http_resp_free(resp);
    aiml_http_client_free(req);
    return true;
}

// One GET that must be answered with 200.
bool httpGet(const std::string& url, const fs::path& toFile, std::string& body,
             std::string& err) {
    int32_t status = 0;
    std::string answer;
    if (!httpCall("GET", url, {}, toFile, status, answer, err)) return false;
    if (status != 200) {
        err = url + ": HTTP " + std::to_string(status);
        return false;
    }
    if (toFile.empty()) body = answer;
    return true;
}

std::string kontorBase(const std::string& url) {
    std::string base = url;
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base;
}

// F68-61: which version is the newest — the answer kontor gives its own graph.
// `want` empty: every project (F68-66).
bool kontorGraph(const std::string& base, const std::string& want,
                 std::vector<std::pair<std::string, std::string>>& out, std::string& err) {
    std::string body;
    if (!httpGet(base + "/api/v1/graph", {}, body, err)) return false;
    try {
        YAML::Node doc = YAML::Load(body);           // JSON is YAML
        YAML::Node projects = doc["projects"];
        if (!projects || !projects.IsSequence()) {
            err = base + "/api/v1/graph: no \"projects\" in the answer";
            return false;
        }
        for (const auto& p : projects) {
            if (!p["name"] || !p["version"]) continue;
            std::string pn = p["name"].as<std::string>();
            std::string pv = p["version"].as<std::string>();
            if (want.empty() || pn == want) out.emplace_back(pn, pv);
        }
    } catch (const std::exception& e) {
        err = base + "/api/v1/graph: " + e.what();
        return false;
    }
    if (!want.empty() && out.empty()) {
        err = "kontor at " + base + " does not know a project '" + want + "'";
        return false;
    }
    return true;
}

// F68-62: manifest, then the package into the cache, unpacked to the one tree
// it contains.  `digest` is what was built (F68-65).
bool kontorFetch(const std::string& base, const std::string& name, const std::string& version,
                 fs::path& srcDir, std::string& digest, std::string& err) {
    std::string prefix = base + "/exodus/" + name + "/" + version;
    std::string manifest;
    if (!httpGet(prefix + "/exodus.json", {}, manifest, err)) return false;

    std::error_code ec;
    fs::path at = cacheRoot() / "venv_tools" / name / ("kontor-" + version);
    fs::create_directories(at, ec);
    if (ec) { err = "cannot make " + at.string() + ": " + ec.message(); return false; }
    fs::path archive = at / "package.tar.gz";
    std::string unused;
    if (!httpGet(prefix + "/package", archive, unused, err)) return false;

    char* hex = aiml_sha256_file(archive.string().c_str());
    digest = hex && *hex ? std::string("sha256:") + hex : std::string();
    if (digest.empty()) { err = "cannot read back " + archive.string(); return false; }

    fs::path tree = at / "tree";
    fs::remove_all(tree, ec);
    fs::create_directories(tree, ec);
    int32_t rc = aiml_compress_untar(archive.string().c_str(), tree.string().c_str());
    if (rc != 0) {
        err = archive.string() + ": not a readable tar.gz (compress.untar " +
              std::to_string(rc) + ")";
        return false;
    }
    // F68-62: exactly one directory, and that is the tree.
    std::vector<fs::path> top;
    for (const auto& ent : fs::directory_iterator(tree, ec)) top.push_back(ent.path());
    if (top.size() != 1 || !fs::is_directory(top[0], ec)) {
        err = "the package of " + name + "@" + version + " unpacks to " +
              std::to_string(top.size()) + " entries — one directory is expected";
        return false;
    }
    srcDir = top[0];
    return true;
}

int cmdInstall(const fs::path& root, const std::string& name, const std::string& gitUrl,
               const std::string& ref, const std::string& localPath, const std::string& kontorUrl,
               const std::string& wantVersion);

// F68-66: every project kontor has, at its newest version.  What is no aiml
// tool is skipped, not failed — kontor is a repository for exodus projects.
int cmdInstallAll(const fs::path& root, const std::string& kontorUrl) {
    if (kontorUrl.empty())
        return fail("install --all needs a source: --kontor <url> (spec/68 F68-66) or --path <dir> "
                    "(F68-68)");
    std::string base = kontorBase(kontorUrl), err;
    std::vector<std::pair<std::string, std::string>> projects;
    if (!kontorGraph(base, "", projects, err)) return fail(err);
    if (projects.empty()) {
        std::cout << "kontor at " << base << " holds no project" << std::endl;
        return 0;
    }
    int installed = 0, skipped = 0, failed = 0;
    for (const auto& p : projects) {
        // Looked at before installing, which fetches the archive a second
        // time: kontor may serve other bytes under the same version, so a
        // cached archive is no answer to "what is there now".
        fs::path probe;
        std::string digest;
        if (!kontorFetch(base, p.first, p.second, probe, digest, err)) {
            std::cout << "  ! " << p.first << " " << p.second << ": " << err << std::endl;
            ++failed;
            continue;
        }
        std::error_code ec;
        if (!fs::is_regular_file(probe / "aiml.tool.json", ec)) {
            std::cout << "  - " << p.first << " " << p.second
                      << ": no aiml.tool.json — not an aiml tool" << std::endl;
            ++skipped;
            continue;
        }
        if (cmdInstall(root, p.first, "", "", "", base, p.second) == 0) ++installed;
        else ++failed;
    }
    std::cout << installed << " installed, " << skipped << " skipped, " << failed << " failed"
              << std::endl;
    return failed == 0 ? 0 : 1;
}

int cmdInstall(const fs::path& root, const std::string& name, const std::string& gitUrl,
               const std::string& ref, const std::string& localPath, const std::string& kontorUrl,
               const std::string& wantVersion);

// F68-68: every tool in a local directory — the directory itself, or each one in it.
int cmdInstallAllPath(const fs::path& root, const std::string& pathArg) {
    std::error_code ec;
    fs::path dir = fs::weakly_canonical(fs::absolute(pathArg), ec);
    if (ec || !fs::is_directory(dir, ec)) return fail("--path " + pathArg + ": not a directory");
    std::vector<fs::path> tools;
    if (fs::is_regular_file(dir / "aiml.tool.json", ec)) {
        tools.push_back(dir);
    } else {
        for (const auto& ent : fs::directory_iterator(dir, ec))
            if (ent.is_directory(ec)) tools.push_back(ent.path());
        std::sort(tools.begin(), tools.end());
    }
    int installed = 0, skipped = 0, failed = 0;
    for (const fs::path& t : tools) {
        if (!fs::is_regular_file(t / "aiml.tool.json", ec)) {
            std::cout << "  - " << t.filename().string() << ": no aiml.tool.json — not an aiml tool"
                      << std::endl;
            ++skipped;
            continue;
        }
        Manifest man;
        std::string err;
        if (!readManifest(t, man, err)) {
            std::cout << "  ! " << t.filename().string() << ": " << err << std::endl;
            ++failed;
            continue;
        }
        if (cmdInstall(root, man.name, "", "", t.string(), "", "") == 0) ++installed;
        else ++failed;
    }
    std::cout << installed << " installed, " << skipped << " skipped, " << failed << " failed"
              << std::endl;
    return failed == 0 ? 0 : 1;
}

// F68-50…57: fetch, build with this venv, place the results.
int cmdInstall(const fs::path& root, const std::string& name, const std::string& gitUrl,
               const std::string& ref, const std::string& localPath, const std::string& kontorUrl,
               const std::string& wantVersion) {
    int sources = (!gitUrl.empty() ? 1 : 0) + (!localPath.empty() ? 1 : 0) +
                  (!kontorUrl.empty() ? 1 : 0);
    if (name.empty() || sources != 1) return usage();

    fs::path recAt = recordFile(root);
    Records recs = readRecords(recAt);

    std::string source, commit;
    fs::path srcDir;
    std::error_code ec;
    if (!gitUrl.empty()) {
        std::string err;
        if (!gitCommit(gitUrl, ref, commit, err)) return fail(err);
        source = "git:" + gitUrl;
        // F68-57: the same commit with its files in place is a no-op — and a
        // real one: nothing is fetched and nothing is built.
        auto have = recs.find(name);
        if (have != recs.end() && have->second.commit == commit) {
            bool complete = !have->second.files.empty();
            for (const std::string& rel : have->second.files)
                if (!fs::exists(root / rel, ec)) complete = false;
            if (complete) {
                std::cout << name << " is already installed at " << commit << " — nothing to do"
                          << std::endl;
                return 0;
            }
        }
        srcDir = cacheRoot() / "venv_tools" / name / commit;
        if (!gitFetch(gitUrl, ref, commit, srcDir, err)) return fail(err);
    } else if (!kontorUrl.empty()) {
        // F68-60…65.  The digest is only known once the bytes are here, so the
        // no-op of F68-57 comes after the fetch — it saves the build, which is
        // what costs.
        std::string base = kontorBase(kontorUrl), err, version = wantVersion;
        if (version.empty()) {
            std::vector<std::pair<std::string, std::string>> found;
            if (!kontorGraph(base, name, found, err)) return fail(err);
            version = found.front().second;
        }
        source = "kontor:" + base;
        if (!kontorFetch(base, name, version, srcDir, commit, err)) return fail(err);
        auto have = recs.find(name);
        if (have != recs.end() && have->second.commit == commit) {
            bool complete = !have->second.files.empty();
            for (const std::string& rel : have->second.files)
                if (!fs::exists(root / rel, ec)) complete = false;
            if (complete) {
                std::cout << name << " is already installed at " << commit << " — nothing to do"
                          << std::endl;
                return 0;
            }
        }
    } else {
        srcDir = fs::weakly_canonical(fs::absolute(localPath), ec);
        if (ec || !fs::is_directory(srcDir, ec))
            return fail("--path " + localPath + ": not a directory");
        // No commit: a working tree is not a version, so this install is
        // rebuilt every time and `list` says it cannot be reproduced.
        source = "path:" + srcDir.string();
    }

    Manifest man;
    std::string err;
    if (!readManifest(srcDir, man, err)) return fail(err);
    if (man.name != name)
        return fail("aiml.tool.json in " + srcDir.string() + " is for '" + man.name +
                    "', not '" + name + "'");
    if (!configsUseVenv(srcDir, err)) return fail(err);
    if (man.programs.empty() && man.libraries.empty() && man.package.empty())
        return fail("aiml.tool.json in " + srcDir.string() +
                    " declares neither programs, libraries nor a package — nothing to install");

    // F68-53: built by the compiler it will live next to.
    if (!man.programs.empty() || !man.libraries.empty()) {
        // §7: the venv's own `aiml build`, as a child of the target venv's
        // compiler — no exodus, no Python (F68-53).
        std::string aimlBin = (root / "bin" / "aiml").string();
        const char* oldPath = std::getenv("PATH");
        std::vector<std::string> env = {
            "AIML_VENV=" + root.string(),
            "AIML=" + (root / "bin" / "aiml").string(),
            "PATH=" + (root / "bin").string() + (oldPath && *oldPath ? ":" + std::string(oldPath) : ""),
        };
        std::cout << "building " << name << " in " << srcDir.string() << std::endl;
        // Either the whole project, or exactly the configurations the manifest
        // names — a repository whose `--all` also builds its tests and its wasm
        // targets would never install (F68-53).
        std::vector<std::vector<std::string>> builds;
        if (man.configs.empty()) {
            builds.push_back({aimlBin, "build", "--all"});
        } else {
            for (const std::string& cfg : man.configs) {
                if (!fs::is_regular_file(srcDir / cfg, ec))
                    return fail("aiml.tool.json names the configuration '" + cfg +
                                "', but " + (srcDir / cfg).string() + " is not there");
                builds.push_back({aimlBin, "build", "-c", cfg});
            }
        }
        for (const auto& cmd : builds) {
            int rc = runProg(cmd, srcDir, env, nullptr);
            if (rc == 127)
                return fail("cannot run " + aimlBin + " build (F68-53)");
            if (rc != 0) {
                std::string what = man.configs.empty() ? std::string("--all") : cmd.back();
                return fail("building " + name + " (" + what + ") failed (exit " +
                            std::to_string(rc) + ")");
            }
        }
    }

    // Collect what will be placed before touching the venv, so a missing
    // build output is found before the old copy is gone.
    std::vector<std::pair<fs::path, fs::path>> copies;   // from → to
    std::vector<std::string> placed;
    for (const std::string& rel : man.programs) {
        fs::path from = srcDir / rel;
        if (!fs::is_regular_file(from, ec))
            return fail("aiml.tool.json names the program '" + rel + "', but the build left no " +
                        from.string());
        copies.emplace_back(from, root / "bin" / from.filename());
        placed.push_back("bin/" + from.filename().string());
    }
    for (const std::string& rel : man.libraries) {
        fs::path from = srcDir / rel;
        if (!fs::is_regular_file(from, ec))
            return fail("aiml.tool.json names the library '" + rel + "', but the build left no " +
                        from.string());
        copies.emplace_back(from, root / "lib" / from.filename());
        placed.push_back("lib/" + from.filename().string());
    }
    fs::path pkgFrom;
    if (!man.package.empty()) {
        pkgFrom = (srcDir / man.package).lexically_normal();
        if (!fs::is_directory(pkgFrom, ec))
            return fail("aiml.tool.json names the package directory '" + man.package +
                        "', but there is no " + pkgFrom.string());
        placed.push_back("lib/aiml/" + name);
    }

    // F68-58a: what a placed program or library loads from the build cache comes
    // along into lib/, under the name the loader asks for.
    {
        fs::path cache;
        const char* ec_env = std::getenv("EXODUS_CACHE");
        cache = ec_env && *ec_env ? fs::weakly_canonical(fs::absolute(ec_env), ec)
                                  : fs::weakly_canonical(srcDir / "__exodus_cache", ec);
        std::map<std::string, fs::path> wanted;   // SONAME → real file
        bool askedLdd = false, haveLdd = true;
        auto never = [](const std::string& so) {
            static const char* const kSystem[] = {"libc.so", "libm.so", "libpthread.so", "libdl.so",
                                                  "librt.so", "ld-linux", "linux-vdso", "libresolv.so",
                                                  "libutil.so"};
            for (const char* s : kSystem)
                if (so.rfind(s, 0) == 0) return true;
            return false;
        };
        if (fs::is_directory(cache, ec)) {
            for (const auto& cp : copies) {
                std::string out;
                askedLdd = true;
                int rc = runProg({"ldd", cp.first.string()}, {}, {}, &out);
                if (rc == 127) { haveLdd = false; break; }
                std::istringstream lines(out);
                std::string line;
                while (std::getline(lines, line)) {
                    size_t arrow = line.find(" => ");
                    if (arrow == std::string::npos) continue;
                    std::string so = line.substr(0, arrow);
                    so.erase(0, so.find_first_not_of(" \t"));
                    std::string rest = line.substr(arrow + 4);
                    size_t paren = rest.find(" (");
                    std::string path = paren == std::string::npos ? rest : rest.substr(0, paren);
                    if (path.empty() || path[0] != '/' || never(so)) continue;
                    fs::path real = fs::weakly_canonical(path, ec);
                    if (ec) continue;
                    fs::path rel = real.lexically_relative(cache);
                    if (rel.empty() || *rel.begin() == "..") continue;
                    wanted.emplace(so, real);
                }
            }
        }
        if (askedLdd && !haveLdd)
            std::cout << "  no ldd: libraries from the build cache are not placed into the venv "
                         "(spec/68 F68-58a)" << std::endl;
        for (const auto& w : wanted) {
            fs::path to = root / "lib" / w.first;
            std::string rel = "lib/" + w.first;
            if (fs::exists(to, ec)) {
                std::string owner;
                for (const auto& r : recs)
                    if (r.first != name &&
                        std::find(r.second.files.begin(), r.second.files.end(), rel) != r.second.files.end())
                        owner = r.first;
                bool mine = false;
                auto me = recs.find(name);
                if (me != recs.end())
                    mine = std::find(me->second.files.begin(), me->second.files.end(), rel) != me->second.files.end();
                if (!mine && sha256File(to.string()) != sha256File(w.second.string())) {
                    if (!owner.empty())
                        return fail(rel + " was placed by '" + owner + "' with other bytes than " + name +
                                    " needs from " + w.second.string() + " (spec/68 F68-58a)");
                    return fail(rel + " is in the venv already, with other bytes than " + name +
                                " needs from " + w.second.string() + " (spec/68 F68-58a)");
                }
                if (!mine && owner.empty()) continue;   // the venv's own file: left alone, not ours
            }
            copies.emplace_back(w.second, to);
            placed.push_back(rel);
        }
        if (!wanted.empty())
            std::cout << "  " << wanted.size() << " librar" << (wanted.size() == 1 ? "y" : "ies")
                      << " from the build cache into lib/ (spec/68 F68-58a)" << std::endl;
    }

    // The old copy goes only now, after the build worked (F68-57).
    auto old = recs.find(name);
    if (old != recs.end()) dropFiles(root, old->second, recs, name);

    using fs::perms;
    const perms exec = perms::owner_all | perms::group_read | perms::group_exec
                     | perms::others_read | perms::others_exec;
    const perms data = perms::owner_read | perms::owner_write | perms::group_read | perms::others_read;
    for (const auto& cp : copies) {
        bool isProgram = cp.second.parent_path().filename() == "bin";
        if (!installFile(cp.first, cp.second, isProgram ? exec : data, err)) return fail(err);
        // F68-55: checked, not patched.
        if (!runpathOk(cp.second))
            std::cerr << "aiml venv: warning: " << cp.second.filename().string()
                      << " has no $ORIGIN-relative RUNPATH — it will only run where its build "
                         "tree is (spec/68 F68-23)" << std::endl;
    }
    if (!pkgFrom.empty()) {
        fs::path pkgTo = root / "lib" / "aiml" / name;
        std::vector<fs::path> outputs;
        for (const auto& cp : copies) outputs.push_back(cp.first);
        fs::remove_all(pkgTo, ec);
        if (!copyPackage(pkgFrom, pkgTo, outputs, err)) return fail(err);
        Table table = readTable(tableFile(root));
        table[name]["path"] = name;                       // relative to lib/aiml (F68-32)
        if (!man.version.empty()) table[name]["version"] = man.version;
        if (!writeTable(tableFile(root), table, err)) return fail(err);
    }

    Record rec;
    rec.source = source;
    rec.commit = commit;
    rec.version = man.version;
    rec.files = placed;
    recs[name] = rec;
    if (!writeRecords(recAt, recs, err)) return fail(err);

    std::cout << "installed " << name;
    if (!man.version.empty()) std::cout << " " << man.version;
    std::cout << " into " << root.string() << " (" << placed.size() << " entries)" << std::endl;
    return 0;
}

// ── §6.3: publishing a tool to kontor ─────────────────────────────────────
//
// The counterpart of `install --kontor`: what `install` fetches from
// /exodus/<name>/<version>/ is put there by `publish`, in the shape install
// expects (F68-62) — so a tool goes from one venv into kontor and out into the
// next without a script in between.

// A field of a JSON manifest, "" when it is missing or no scalar.
std::string jsonField(const fs::path& file, const char* key) {
    try {
        YAML::Node doc = YAML::LoadFile(file.string());
        if (doc[key] && doc[key].IsScalar()) return doc[key].as<std::string>();
    } catch (const std::exception&) {}
    return "";
}

// The `message` kontor puts in a JSON error, else the body as it came.
std::string kontorMessage(const std::string& body) {
    try {
        YAML::Node doc = YAML::Load(body);
        if (doc.IsMap() && doc["error"] && doc["error"].IsScalar()) return doc["error"].as<std::string>();
        if (doc.IsMap() && doc["message"] && doc["message"].IsScalar()) return doc["message"].as<std::string>();
    } catch (const std::exception&) {}
    return body.size() > 200 ? body.substr(0, 200) + "…" : body;
}

// F68-72: what git tracks, in the working tree's state, as <stage>/<dir>/.
bool stageTracked(const fs::path& repo, const fs::path& stage, std::vector<std::string>& files,
                  std::string& err) {
    std::string out;
    int rc = runProg({"git", "-C", repo.string(), "ls-files", "-z"}, {}, {}, &out);
    if (rc == 127) { err = "git is not on PATH (F68-51)"; return false; }
    if (rc != 0) {
        err = repo.string() + " is no git checkout — publish packs what git tracks (F68-72)";
        return false;
    }
    std::error_code ec;
    size_t at = 0;
    while (at < out.size()) {
        size_t end = out.find('\0', at);
        if (end == std::string::npos) end = out.size();
        std::string rel = out.substr(at, end - at);
        at = end + 1;
        if (rel.empty()) continue;
        fs::path from = repo / rel, to = stage / rel;
        fs::file_status st = fs::symlink_status(from, ec);
        if (!fs::exists(st)) continue;            // deleted in the tree, not yet in git
        fs::create_directories(to.parent_path(), ec);
        if (fs::is_symlink(st)) fs::copy_symlink(from, to, ec);
        else if (fs::is_regular_file(st)) fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
        else continue;                            // a submodule: not ours to pack
        if (ec) { err = "cannot stage " + rel + ": " + ec.message(); return false; }
        files.push_back(rel);
    }
    return true;
}

int cmdPublish(const std::string& wantName, const std::string& pathArg, const std::string& kontorUrl,
               bool overwrite) {
    if (kontorUrl.empty()) return fail("publish needs a target: --kontor <url> (spec/68 §6.3)");
    const char* token = std::getenv("KONTOR_TOKEN");
    if (!token || !*token)
        return fail("publish needs $KONTOR_TOKEN, a kontor token with the role writer (F68-74)");

    std::error_code ec;
    fs::path repo = fs::weakly_canonical(fs::absolute(pathArg.empty() ? "." : pathArg), ec);
    if (ec || !fs::is_directory(repo, ec)) return fail("--path " + pathArg + ": no such directory");

    // F68-70: aiml.tool.json says what the tool is, as it does for install.
    Manifest man;
    std::string err;
    if (!readManifest(repo, man, err)) return fail(err);
    if (man.name.empty() || man.version.empty())
        return fail((repo / "aiml.tool.json").string() + " must name both name and version to be published");
    if (!wantName.empty() && wantName != man.name)
        return fail("aiml.tool.json names '" + man.name + "', not '" + wantName + "'");

    // F68-71: the manifest kontor keeps — exodus.json where there is one, and
    // it must say the same name and version as aiml.tool.json.
    fs::path manifest = repo / "exodus.json";
    if (fs::is_regular_file(manifest, ec)) {
        std::string en = jsonField(manifest, "name"), ev = jsonField(manifest, "version");
        if (en != man.name || ev != man.version)
            return fail("exodus.json says " + en + "@" + ev + ", aiml.tool.json says " +
                        man.name + "@" + man.version + " — one version, one name");
    } else {
        manifest = repo / "aiml.tool.json";
    }

    // F68-72: a committed state.  A version is published once (F68-73), so
    // what goes out has to be something that can be named afterwards.
    std::string dirty;
    int rc = runProg({"git", "-C", repo.string(), "status", "--porcelain", "--untracked-files=no"},
                     {}, {}, &dirty);
    if (rc == 127) return fail("git is not on PATH (F68-51)");
    if (rc != 0) return fail(repo.string() + " is no git checkout — publish packs what git tracks (F68-72)");
    if (!dirty.empty())
        return fail(repo.string() + " has uncommitted changes — commit them first, a published "
                    "version cannot be replaced (F68-72/73)");

    std::string base = kontorBase(kontorUrl);
    std::string prefix = base + "/exodus/" + man.name + "/" + man.version;
    std::string dirName = man.name + "-" + man.version;
    fs::path work = cacheRoot() / "venv_publish" / dirName;
    fs::remove_all(work, ec);
    fs::create_directories(work / "stage" / dirName, ec);
    if (ec) return fail("cannot make " + work.string() + ": " + ec.message());

    // F68-73: a version that kontor has is not overwritten — asked before
    // anything is sent, so an older kontor that would overwrite never gets the chance.
    // F68-76: --overwrite asks too, and then leaves the verdict to kontor, which
    // lets only a token with the admin right replace (kontor FW-47a).
    int32_t status = 0;
    std::string answer;
    if (!httpCall("GET", prefix + "/package", {}, work / "existing", status, answer, err))
        return fail(err);
    fs::remove(work / "existing", ec);
    if (status == 200 && !overwrite)
        return fail(man.name + "@" + man.version + " is already in kontor at " + base +
                    " — a published version is not replaced; raise \"version\" in aiml.tool.json (F68-73)");
    if (status == 410 && !overwrite)
        return fail(man.name + "@" + man.version + " was deleted in kontor at " + base +
                    " — a deleted version number is not reused; raise \"version\" in aiml.tool.json (F68-73)");
    if (status == 401 || status == 403)
        return fail(prefix + "/package: HTTP " + std::to_string(status) + " — " + kontorMessage(answer));
    if (status != 404 && !(overwrite && (status == 200 || status == 410)))
        return fail(prefix + "/package: HTTP " + std::to_string(status));
    bool replacing = status == 200 || status == 410;

    std::vector<std::string> files;
    if (!stageTracked(repo, work / "stage" / dirName, files, err)) return fail(err);
    for (const fs::path& need : {fs::path("aiml.tool.json"), manifest.filename()})
        if (!fs::exists(work / "stage" / dirName / need, ec))
            return fail(need.string() + " is in " + repo.string() + " but not in git — 'git add " +
                        need.string() + "' (F68-72)");

    // F68-72: reproducible bytes — sorted, mtime 0, owner 0:0, gzip without a
    // name or a time.  The same commit gives the same digest on every machine.
    fs::path tarFile = work / "package.tar";
    rc = runProg({"tar", "--sort=name", "--mtime=@0", "--owner=0", "--group=0", "--numeric-owner",
                  "--mode=u+rwX,go+rX,go-w", "-C", (work / "stage").string(), "-cf",
                  tarFile.string(), dirName}, {}, {}, nullptr);
    if (rc == 127) return fail("tar is not on PATH (F68-72)");
    if (rc != 0) return fail("tar failed (exit " + std::to_string(rc) + ")");
    rc = runProg({"gzip", "-n", "-9", "-f", tarFile.string()}, {}, {}, nullptr);
    if (rc == 127) return fail("gzip is not on PATH (F68-72)");
    if (rc != 0) return fail("gzip failed (exit " + std::to_string(rc) + ")");
    fs::path archive = work / "package.tar.gz";
    char* hex = aiml_sha256_file(archive.string().c_str());
    std::string digest = hex && *hex ? std::string("sha256:") + hex : std::string();
    uintmax_t size = fs::file_size(archive, ec);

    // F68-71: manifest first — kontor takes no package without one.
    struct Put { std::string what; fs::path file; std::string url; };
    for (const Put& put : {Put{"manifest", manifest, prefix + "/exodus.json"},
                           Put{"package", archive, prefix + "/package"}}) {
        if (!httpCall("PUT", put.url, put.file, {}, status, answer, err)) return fail(err);
        if (status == 409)
            return fail(put.url + ": HTTP 409 — " + kontorMessage(answer) +
                        (overwrite ? " (F68-76: only a token with the admin right overwrites)" : " (F68-73)"));
        if (status != 200 && status != 201)
            return fail(put.url + ": HTTP " + std::to_string(status) + " — " + kontorMessage(answer));
    }
    fs::remove_all(work / "stage", ec);
    std::cout << (replacing ? "replaced " : "published ") << man.name << " " << man.version << " (" << files.size()
              << " files, " << size << " bytes, " << digest << ") → " << prefix << "\n"
              << "  install it with: aiml venv <dir> install " << man.name << " --kontor " << base
              << " --version " << man.version << std::endl;
    return 0;
}


// ── §9: asset packages ────────────────────────────────────────────────────
//
// One root, <venv>/assets, one directory per package (F68-121).  The rules of
// F68-122…124 are checked on the tree before it is published and on the
// unpacked archive before it is placed, so a package that a compile would
// lose files of, or trip over, never gets into kontor or into a venv.

fs::path assetDir(const fs::path& root) { return root / "assets"; }
fs::path assetRecordFile(const fs::path& root) { return root / "lib" / "aiml" / "aiml.assets.json"; }

struct AssetRecord {
    std::string source, version, digest;
};
using AssetRecords = std::map<std::string, AssetRecord>;

AssetRecords readAssetRecords(const fs::path& file) {
    AssetRecords r;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return r;
    try {
        YAML::Node doc = YAML::LoadFile(file.string());
        YAML::Node assets = doc["assets"];
        if (!assets || !assets.IsMap()) return r;
        for (const auto& kv : assets) {
            AssetRecord rec;
            const YAML::Node& v = kv.second;
            if (v["source"]) rec.source = v["source"].as<std::string>();
            if (v["version"]) rec.version = v["version"].as<std::string>();
            if (v["digest"]) rec.digest = v["digest"].as<std::string>();
            r[kv.first.as<std::string>()] = rec;
        }
    } catch (const std::exception&) {}
    return r;
}

bool writeAssetRecords(const fs::path& file, const AssetRecords& r, std::string& err) {
    std::ostringstream ss;
    ss << "{\"assets\": {";
    bool first = true;
    for (const auto& kv : r) {
        ss << (first ? "\n  " : ",\n  ") << "\"" << jsonEscape(kv.first) << "\": {"
           << "\"source\": \"" << jsonEscape(kv.second.source) << "\", "
           << "\"version\": \"" << jsonEscape(kv.second.version) << "\", "
           << "\"digest\": \"" << jsonEscape(kv.second.digest) << "\"}";
        first = false;
    }
    ss << (first ? "}}\n" : "\n}}\n");
    return writeText(file, ss.str(), err);
}

// F68-122: [a-z0-9]+(_[a-z0-9]+)* — words of lowercase letters and digits,
// joined by single underscores.
bool snakeWord(const std::string& s) {
    if (s.empty() || s.front() == '_' || s.back() == '_') return false;
    char prev = 0;
    for (char c : s) {
        bool word = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!word && c != '_') return false;
        if (c == '_' && prev == '_') return false;
        prev = c;
    }
    return true;
}

bool assetPackageName(const std::string& s) { return !s.empty() && s[0] >= 'a' && s[0] <= 'z' && snakeWord(s); }

bool assetVersion(const std::string& s) {
    if (s.empty() || !std::isalnum(static_cast<unsigned char>(s[0]))) return false;
    for (char c : s)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '+' && c != '_' && c != '-')
            return false;
    return true;
}

// (kind, stem) → the path it was found at, relative to its package.
using AssetIndex = std::map<std::pair<int, std::string>, std::string>;

// Walk one package tree.  `problems` gets one line per violation of
// F68-122…124; `index` every asset file by (kind, stem).  `inArchive`: the
// tree was unpacked from an archive, so a second name of one file is a hard
// link the archive carried (a working tree may well have hard links elsewhere).
void checkAssetTree(const fs::path& tree, bool inArchive, AssetIndex& index,
                    std::vector<std::string>& problems) {
    std::error_code ec;
    size_t files = 0;
    auto it = fs::recursive_directory_iterator(tree, fs::directory_options::none, ec);
    if (ec) { problems.push_back(tree.string() + ": " + ec.message()); return; }
    for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { problems.push_back(tree.string() + ": " + ec.message()); return; }
        const fs::path p = it->path();
        const std::string rel = p.lexically_relative(tree).generic_string();
        const std::string leaf = p.filename().string();
        fs::file_status st = fs::symlink_status(p, ec);
        if (fs::is_symlink(st)) {
            problems.push_back(rel + ": a symbolic link — a package holds files and directories only (F68-124)");
            continue;
        }
        if (fs::is_directory(st)) {
            if (!snakeWord(leaf))
                problems.push_back(rel + "/: the directory name '" + leaf + "' is no snake_case (F68-122)");
            continue;
        }
        if (!fs::is_regular_file(st)) {
            problems.push_back(rel + ": neither a file nor a directory (F68-124)");
            continue;
        }
        ++files;
        if (inArchive && fs::hard_link_count(p, ec) > 1) {
            problems.push_back(rel + ": a hard link — one file, one name (F68-124)");
            continue;
        }
        size_t dot = leaf.find('.');
        if (dot == std::string::npos || leaf.find('.', dot + 1) != std::string::npos) {
            problems.push_back(rel + ": a file is <stem>.<ext> with exactly one dot (F68-122)");
            continue;
        }
        std::string stem = leaf.substr(0, dot), ext = leaf.substr(dot + 1);
        AIML::AssetKind kind = AIML::AssetScanner::classify(leaf);
        bool lowerExt = std::none_of(ext.begin(), ext.end(), [](char c) { return std::isupper(static_cast<unsigned char>(c)); });
        if (kind == AIML::AssetKind::Unknown) {
            problems.push_back(rel + ": '." + ext + "' is no asset kind of spec/44_7 (F68-124)");
            continue;
        }
        if (!lowerExt) {
            problems.push_back(rel + ": the extension is spelled in lowercase (F68-122)");
            continue;
        }
        if (!snakeWord(stem)) {
            problems.push_back(rel + ": the name '" + stem + "' is no snake_case (F68-122)");
            continue;
        }
        auto key = std::make_pair(static_cast<int>(kind), stem);
        auto hit = index.find(key);
        if (hit != index.end()) {
            problems.push_back(rel + ": the " + std::string(AIML::AssetScanner::kind_name(kind)) + " '" + stem +
                               "' is there already as " + hit->second + " — a stem is unique per kind (F68-123)");
            continue;
        }
        index.emplace(key, rel);
    }
    if (files == 0) problems.push_back(tree.string() + ": the package holds no file (F68-124)");
}

// F68-123 across packages: every stem of `mine` against the other packages
// installed in the venv.
void checkAgainstInstalled(const fs::path& root, const std::string& name, const AssetIndex& mine,
                           std::vector<std::string>& problems) {
    std::error_code ec;
    fs::path dir = assetDir(root);
    if (!fs::is_directory(dir, ec)) return;
    std::vector<fs::path> others;
    for (const auto& ent : fs::directory_iterator(dir, ec))
        if (ent.is_directory(ec) && ent.path().filename() != name) others.push_back(ent.path());
    std::sort(others.begin(), others.end());
    for (const fs::path& other : others) {
        AssetIndex theirs;
        std::vector<std::string> ignored;
        checkAssetTree(other, false, theirs, ignored);
        for (const auto& kv : mine) {
            auto hit = theirs.find(kv.first);
            if (hit == theirs.end()) continue;
            problems.push_back(name + ": " + kv.second + ": the " +
                               std::string(AIML::AssetScanner::kind_name(static_cast<AIML::AssetKind>(kv.first.first))) +
                               " '" + kv.first.second + "' is in the installed package " +
                               other.filename().string() + " already, as " + hit->second + " (F68-123)");
        }
    }
}

int reportProblems(const std::string& what, const std::vector<std::string>& problems) {
    for (const std::string& p : problems) std::cerr << "  " << p << std::endl;
    return fail(what + ": " + std::to_string(problems.size()) + " problem" +
                (problems.size() == 1 ? "" : "s") + " — nothing done (spec/68 §9)");
}

size_t countFiles(const fs::path& dir) {
    size_t n = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec)) ++n;
    return n;
}

// F68-125
int cmdAssetPublish(const fs::path& root, const std::string& name, const std::string& version,
                    const std::string& pathArg, const std::string& kontorUrl, bool overwrite) {
    if (name.empty() || version.empty() || pathArg.empty() || kontorUrl.empty()) return usage();
    if (!assetPackageName(name))
        return fail("'" + name + "' is no asset package name: [a-z][a-z0-9]*(_[a-z0-9]+)* (F68-122)");
    if (!assetVersion(version)) return fail("'" + version + "' is no version (F68-122)");
    const char* token = std::getenv("KONTOR_TOKEN");
    if (!token || !*token)
        return fail("asset publish needs $KONTOR_TOKEN, a kontor token with the role writer (F68-125)");
    std::error_code ec;
    fs::path dir = fs::weakly_canonical(fs::absolute(pathArg), ec);
    if (ec || !fs::is_directory(dir, ec)) return fail("--path " + pathArg + ": no such directory");

    AssetIndex index;
    std::vector<std::string> problems;
    checkAssetTree(dir, false, index, problems);
    checkAgainstInstalled(root, name, index, problems);
    if (!problems.empty()) return reportProblems("asset publish " + name + " " + version, problems);

    std::string base = kontorBase(kontorUrl);
    std::string url = base + "/generic/" + name + "/" + version + "/" + name + ".tar.gz";
    fs::path work = cacheRoot() / "venv_publish_assets" / (name + "-" + version);
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);
    if (ec) return fail("cannot make " + work.string() + ": " + ec.message());

    int32_t status = 0;
    std::string answer, err;
    if (!httpCall("GET", url, {}, work / "existing", status, answer, err)) return fail(err);
    fs::remove(work / "existing", ec);
    if (status == 200 && !overwrite)
        return fail(name + " " + version + " is already in kontor at " + base +
                    " — a published version is not replaced; raise the version, or --overwrite (F68-125)");
    if (status == 401 || status == 403)
        return fail(url + ": HTTP " + std::to_string(status) + " — " + kontorMessage(answer));
    if (status != 404 && status != 200) return fail(url + ": HTTP " + std::to_string(status));
    bool replacing = status == 200;

    // F68-125/F68-72: reproducible — sorted, mtime 0, owner 0:0, gzip -n; the
    // root is the directory's content (F68-120).
    std::vector<std::string> top;
    for (const auto& ent : fs::directory_iterator(dir, ec)) top.push_back(ent.path().filename().string());
    std::sort(top.begin(), top.end());
    fs::path tarFile = work / (name + ".tar");
    std::vector<std::string> cmd = {"tar", "--sort=name", "--mtime=@0", "--owner=0", "--group=0",
                                    "--numeric-owner", "--hard-dereference", "--mode=u+rw,go+r,go-w,a-s",
                                    "-C", dir.string(), "-cf", tarFile.string()};
    cmd.insert(cmd.end(), top.begin(), top.end());
    int rc = runProg(cmd, {}, {}, nullptr);
    if (rc == 127) return fail("tar is not on PATH (F68-125)");
    if (rc != 0) return fail("tar failed (exit " + std::to_string(rc) + ")");
    rc = runProg({"gzip", "-n", "-9", "-f", tarFile.string()}, {}, {}, nullptr);
    if (rc == 127) return fail("gzip is not on PATH (F68-125)");
    if (rc != 0) return fail("gzip failed (exit " + std::to_string(rc) + ")");
    fs::path archive = work / (name + ".tar.gz");
    std::string hex = sha256File(archive.string());
    if (hex.empty()) return fail("cannot read back " + archive.string());
    uintmax_t size = fs::file_size(archive, ec);

    if (!httpCall("PUT", url, archive, {}, status, answer, err, {{"X-Checksum-Sha256", hex}})) return fail(err);
    if (status != 200 && status != 201)
        return fail(url + ": HTTP " + std::to_string(status) + " — " + kontorMessage(answer));
    std::cout << (replacing ? "replaced " : "published ") << name << " " << version << " (" << index.size()
              << " files, " << size << " bytes, sha256:" << hex << ") → " << url << "\n"
              << "  install it with: aiml venv <dir> asset install " << name << " --kontor " << base
              << " --version " << version << std::endl;
    return 0;
}

// F68-126
int cmdAssetInstall(const fs::path& root, const std::string& name, const std::string& kontorUrl,
                    const std::string& version, const std::string& pathArg) {
    if (name.empty() || kontorUrl.empty() == pathArg.empty()) return usage();
    if (!kontorUrl.empty() && version.empty())
        return fail("asset install --kontor needs --version <v> — an asset package has no latest (F68-129)");
    if (!assetPackageName(name))
        return fail("'" + name + "' is no asset package name: [a-z][a-z0-9]*(_[a-z0-9]+)* (F68-122)");
    if (!version.empty() && !assetVersion(version)) return fail("'" + version + "' is no version (F68-122)");
    std::error_code ec;
    std::string err, source;
    fs::path archive;
    fs::path at = cacheRoot() / "venv_assets" / name / (version.empty() ? std::string("_path") : version);
    fs::create_directories(at, ec);
    if (ec) return fail("cannot make " + at.string() + ": " + ec.message());
    if (!kontorUrl.empty()) {
        std::string base = kontorBase(kontorUrl);
        archive = at / (name + ".tar.gz");
        std::string body;
        if (!httpGet(base + "/generic/" + name + "/" + version + "/" + name + ".tar.gz", archive, body, err)) {
            fs::remove(archive, ec);
            return fail(err);
        }
        source = "kontor:" + base;
    } else {
        archive = fs::weakly_canonical(fs::absolute(pathArg), ec);
        if (ec || !fs::is_regular_file(archive, ec)) return fail("--path " + pathArg + ": no such file");
        source = "path:" + archive.string();
    }
    std::string hex = sha256File(archive.string());
    if (hex.empty()) return fail("cannot read " + archive.string());
    std::string digest = "sha256:" + hex;

    fs::path recAt = assetRecordFile(root);
    AssetRecords recs = readAssetRecords(recAt);
    fs::path target = assetDir(root) / name;
    auto old = recs.find(name);
    if (old != recs.end() && old->second.digest == digest && fs::is_directory(target, ec)) {
        std::cout << name << " " << (old->second.version.empty() ? "" : old->second.version + " ")
                  << "is installed already (" << digest << ")" << std::endl;
        return 0;
    }

    // Unpacked and checked in the cache (F68-52), placed only when it passed.
    fs::path stage = at / ("_stage." + std::to_string(getpid()));
    fs::remove_all(stage, ec);
    fs::create_directories(stage, ec);
    auto cleanup = [&]() { std::error_code e; fs::remove_all(stage, e); };
    int32_t urc = aiml_compress_untar(archive.string().c_str(), stage.string().c_str());
    if (urc != 0) {
        cleanup();
        return fail(archive.string() + ": not a readable tar.gz, or an entry outside the package "
                    "(compress.untar " + std::to_string(urc) + ", F68-124)");
    }
    AssetIndex index;
    std::vector<std::string> problems;
    checkAssetTree(stage, true, index, problems);
    checkAgainstInstalled(root, name, index, problems);
    if (!problems.empty()) {
        cleanup();
        return reportProblems("asset install " + name, problems);
    }

    // Into the venv beside the root (not under assets/, where a compile would
    // see it), then renamed into place; the old tree goes after that.
    fs::path pid = std::to_string(getpid());
    fs::path incoming = root / "lib" / "aiml" / (".asset_new." + pid.string());
    fs::path outgoing = root / "lib" / "aiml" / (".asset_old." + pid.string());
    fs::remove_all(incoming, ec);
    fs::copy(stage, incoming, fs::copy_options::recursive, ec);
    cleanup();
    if (ec) { fs::remove_all(incoming, ec); return fail("cannot copy the package into " + root.string() + ": " + ec.message()); }
    fs::create_directories(assetDir(root), ec);
    bool hadOld = fs::exists(target, ec);
    if (hadOld) {
        fs::rename(target, outgoing, ec);
        if (ec) { fs::remove_all(incoming, ec); return fail("cannot move " + target.string() + " aside: " + ec.message()); }
    }
    fs::rename(incoming, target, ec);
    if (ec) {
        std::error_code e2;
        if (hadOld) fs::rename(outgoing, target, e2);
        fs::remove_all(incoming, e2);
        return fail("cannot place " + target.string() + ": " + ec.message());
    }
    fs::remove_all(outgoing, ec);
    recs[name] = AssetRecord{source, version, digest};
    if (!writeAssetRecords(recAt, recs, err)) return fail(err);
    std::cout << (hadOld ? "updated " : "installed ") << name << (version.empty() ? "" : " " + version)
              << " (" << index.size() << " files, " << digest << ") → " << target.string() << std::endl;
    return 0;
}

// F68-127
int cmdAssetList(const fs::path& root) {
    AssetRecords recs = readAssetRecords(assetRecordFile(root));
    if (recs.empty()) {
        std::cout << "no asset packages installed in " << root.string() << std::endl;
        return 0;
    }
    for (const auto& kv : recs) {
        std::cout << kv.first;
        if (!kv.second.version.empty()) std::cout << " " << kv.second.version;
        std::cout << "  " << kv.second.source << " @ " << kv.second.digest << "  "
                  << countFiles(assetDir(root) / kv.first) << " file(s)" << std::endl;
    }
    return 0;
}

int cmdAssetRemove(const fs::path& root, const std::string& name) {
    fs::path recAt = assetRecordFile(root);
    AssetRecords recs = readAssetRecords(recAt);
    auto it = recs.find(name);
    std::error_code ec;
    if (it == recs.end()) return fail("no asset package '" + name + "' in " + root.string());
    fs::remove_all(assetDir(root) / name, ec);
    if (ec) return fail("cannot remove " + (assetDir(root) / name).string() + ": " + ec.message());
    recs.erase(it);
    std::string err;
    if (!writeAssetRecords(recAt, recs, err)) return fail(err);
    std::cout << "removed the asset package " << name << " from " << root.string() << std::endl;
    return 0;
}


// ── §10: updating the venv's aiml ─────────────────────────────────────────

// F68-116: dot-separated non-negative integers, a leading v dropped; anything
// else is no version (a git-<sha> build, a tag of another kind).
bool parseVersion(const std::string& raw, std::vector<long>& out) {
    out.clear();
    std::string s = !raw.empty() && (raw[0] == 'v' || raw[0] == 'V') ? raw.substr(1) : raw;
    if (s.empty()) return false;
    size_t at = 0;
    while (at <= s.size()) {
        size_t end = s.find('.', at);
        if (end == std::string::npos) end = s.size();
        std::string seg = s.substr(at, end - at);
        if (seg.empty() || seg.size() > 9 || !std::all_of(seg.begin(), seg.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return false;
        out.push_back(std::stol(seg));
        at = end + 1;
    }
    return true;
}

int cmpVersion(std::vector<long> a, std::vector<long> b) {   // a missing segment counts as 0
    size_t n = std::max(a.size(), b.size());
    a.resize(n, 0);
    b.resize(n, 0);
    return a < b ? -1 : (a > b ? 1 : 0);
}

std::string firstLine(const fs::path& file) {
    std::ifstream in(file);
    std::string line;
    if (!in || !std::getline(in, line)) return "";
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
    return line;
}

fs::path releaseRecordFile(const fs::path& root) { return root / "lib" / "aiml" / "aiml.release.json"; }

struct GithubRelease {
    std::string tag, archiveUrl, sumUrl;
    std::vector<long> version;
};

// F68-130: the release listing, the only request that carries a GitHub token.
bool githubReleases(const std::string& repo, std::vector<GithubRelease>& out, std::string& err) {
    const char* apiEnv = std::getenv("AIML_GITHUB_API");
    std::string api = apiEnv && *apiEnv ? apiEnv : "https://api.github.com";
    while (!api.empty() && api.back() == '/') api.pop_back();
    std::string url = api + "/repos/" + repo + "/releases";
    std::vector<std::pair<std::string, std::string>> headers = {
        {"Accept", "application/vnd.github+json"}, {"User-Agent", "aiml-venv-update"}};
    const char* tok = std::getenv("GITHUB_TOKEN");
    if (!tok || !*tok) tok = std::getenv("GH_TOKEN");
    if (tok && *tok) headers.push_back({"Authorization", std::string("Bearer ") + tok});
    int32_t status = 0;
    std::string body;
    if (!httpCall("GET", url, {}, {}, status, body, err, headers, false)) return false;
    if (status != 200) {
        err = url + ": HTTP " + std::to_string(status) + " — " + kontorMessage(body);
        return false;
    }
    try {
        YAML::Node doc = YAML::Load(body);   // JSON is YAML
        if (!doc.IsSequence()) { err = url + ": no list of releases"; return false; }
        for (const auto& r : doc) {
            if (!r.IsMap() || !r["tag_name"]) continue;
            if (r["draft"] && r["draft"].IsScalar() && r["draft"].as<std::string>() == "true") continue;
            GithubRelease rel;
            rel.tag = r["tag_name"].as<std::string>();
            if (!parseVersion(rel.tag, rel.version)) continue;
            std::string want = "aiml-" + rel.tag + "-linux-x86_64.tar.gz";
            if (r["assets"] && r["assets"].IsSequence())
                for (const auto& a : r["assets"]) {
                    if (!a["name"] || !a["browser_download_url"]) continue;
                    std::string n = a["name"].as<std::string>();
                    if (n == want) rel.archiveUrl = a["browser_download_url"].as<std::string>();
                    if (n == want + ".sha256") rel.sumUrl = a["browser_download_url"].as<std::string>();
                }
            out.push_back(rel);
        }
    } catch (const std::exception& e) {
        err = url + ": " + e.what();
        return false;
    }
    return true;
}

bool fetchTo(const std::string& url, const fs::path& file, std::string& err) {
    int32_t status = 0;
    std::string body;
    if (!httpCall("GET", url, {}, file, status, body, err, {{"User-Agent", "aiml-venv-update"}}, false)) return false;
    if (status != 200) {
        err = url + ": HTTP " + std::to_string(status);
        return false;
    }
    return true;
}

int cmdUpdate(const fs::path& root, const std::string& wantVersion, const std::string& githubRepo,
              const std::string& kontorUrl, bool checkOnly) {
    std::error_code ec;
    std::string err;
    if (!kontorUrl.empty() && wantVersion.empty())
        return fail("update --kontor needs --version <v> — kontor's generic store has no listing (F68-130)");
    if (!kontorUrl.empty() && !githubRepo.empty()) return usage();

    // F68-131: the venv's version and its major.
    std::string current = firstLine(root / "lib" / "aiml.version");
    std::vector<long> curV;
    bool released = parseVersion(current, curV);
    std::vector<long> wantV;
    if (!wantVersion.empty() && !parseVersion(wantVersion, wantV))
        return fail("'" + wantVersion + "' is no version (F68-116)");
    if (!released && wantVersion.empty())
        return fail(root.string() + " has no released version (" +
                    (current.empty() ? std::string("no lib/aiml.version") : current) +
                    ") — name the release to take with --version <v> (F68-131)");
    if (released && !wantVersion.empty() && wantV[0] != curV[0])
        return fail(wantVersion + " is of major " + std::to_string(wantV[0]) + ", the venv of major " +
                    std::to_string(curV[0]) + " — another major is a new venv, not an update (F68-131)");

    std::string tag, archiveUrl, sumUrl, source;
    if (kontorUrl.empty()) {
        std::string repo = githubRepo.empty() ? "Pymann/aiml" : githubRepo;
        std::vector<GithubRelease> rels;
        if (!githubReleases(repo, rels, err)) return fail(err);
        const GithubRelease* pick = nullptr;
        for (const GithubRelease& r : rels) {
            if (!wantVersion.empty()) {
                if (cmpVersion(r.version, wantV) == 0) pick = &r;
                continue;
            }
            if (r.version[0] != curV[0]) continue;
            if (!pick || cmpVersion(r.version, pick->version) > 0) pick = &r;
        }
        if (!pick)
            return fail(wantVersion.empty()
                ? "no release of major " + std::to_string(curV[0]) + " in " + repo + " (F68-131)"
                : "no release " + wantVersion + " in " + repo + " (F68-131)");
        if (wantVersion.empty() && cmpVersion(pick->version, curV) <= 0) {
            std::cout << root.string() << " is up to date: " << current << " (newest of major " << curV[0]
                      << " in " << repo << ": " << pick->tag << ")" << std::endl;
            return 0;
        }
        if (pick->archiveUrl.empty() || pick->sumUrl.empty())
            return fail(pick->tag + " in " + repo + " has no aiml-" + pick->tag +
                        "-linux-x86_64.tar.gz with its .sha256 (F68-132)");
        tag = pick->tag;
        archiveUrl = pick->archiveUrl;
        sumUrl = pick->sumUrl;
        source = "github:" + repo;
    } else {
        tag = wantVersion;
        archiveUrl = kontorBase(kontorUrl) + "/generic/aiml/" + tag + "/aiml-x86_64-linux.tar.gz";
        source = "kontor:" + kontorBase(kontorUrl);
    }
    std::string from = current.empty() ? std::string("(no version)") : current;
    if (checkOnly) {
        std::cout << from << " → " << tag << " (" << source << ")" << std::endl;
        return 0;
    }

    // F68-132: into the cache, checked, unpacked.
    fs::path at = cacheRoot() / "venv_update" / tag;
    fs::create_directories(at, ec);
    if (ec) return fail("cannot make " + at.string() + ": " + ec.message());
    fs::path archive = at / (kontorUrl.empty() ? "aiml-" + tag + "-linux-x86_64.tar.gz" : "aiml-x86_64-linux.tar.gz");
    if (!fetchTo(archiveUrl, archive, err)) return fail(err);
    std::string hex = sha256File(archive.string());
    if (hex.empty()) return fail("cannot read back " + archive.string());
    if (!sumUrl.empty()) {
        fs::path sumFile = at / (archive.filename().string() + ".sha256");
        if (!fetchTo(sumUrl, sumFile, err)) return fail(err);
        std::string want = firstLine(sumFile);
        want = want.substr(0, want.find_first_of(" \t"));
        if (want != hex)
            return fail(archive.filename().string() + " has sha256 " + hex + ", its .sha256 says " + want +
                        " — nothing changed (F68-132)");
    }
    fs::path tree = at / "tree";
    fs::remove_all(tree, ec);
    fs::create_directories(tree, ec);
    if (aiml_compress_untar(archive.string().c_str(), tree.string().c_str()) != 0)
        return fail(archive.string() + ": not a readable tar.gz (F68-132)");
    fs::path rel = tree;
    if (kontorUrl.empty()) {
        std::vector<fs::path> top;
        for (const auto& ent : fs::directory_iterator(tree, ec)) top.push_back(ent.path());
        if (top.size() != 1 || !fs::is_directory(top[0], ec))
            return fail(archive.string() + " unpacks to " + std::to_string(top.size()) +
                        " entries — one directory is expected (F68-132)");
        rel = top[0];
    }
    if (!fs::is_regular_file(rel / "bin" / "aiml", ec) || !fs::is_regular_file(rel / "lib" / "libaiml.so", ec))
        return fail(archive.string() + " has no bin/aiml and lib/libaiml.so (F68-132)");
    std::string says = firstLine(rel / "lib" / "aiml.version");
    if (says != tag)
        return fail(archive.string() + ": lib/aiml.version says '" + says + "', not " + tag + " (F68-132)");

    // F68-133: what is replaced — bin/aiml and the files directly in lib/ and lib/wasm32/.
    std::vector<std::string> files = {"bin/aiml"};
    for (const char* sub : {"lib", "lib/wasm32"}) {
        fs::path d = rel / sub;
        if (!fs::is_directory(d, ec)) continue;
        std::vector<std::string> here;
        for (const auto& ent : fs::directory_iterator(d, ec)) {
            fs::file_status st = fs::symlink_status(ent.path(), ec);
            if (fs::is_regular_file(st) || fs::is_symlink(st))
                here.push_back(std::string(sub) + "/" + ent.path().filename().string());
        }
        std::sort(here.begin(), here.end());
        files.insert(files.end(), here.begin(), here.end());
    }
    Records tools = readRecords(recordFile(root));
    auto toolOwning = [&](const std::string& f) -> std::string {
        for (const auto& kv : tools)
            if (std::find(kv.second.files.begin(), kv.second.files.end(), f) != kv.second.files.end()) return kv.first;
        return "";
    };
    for (const std::string& f : files) {
        std::string owner = toolOwning(f);
        if (owner.empty() || !fs::exists(root / f, ec)) continue;
        if (sha256File((root / f).string()) != sha256File((rel / f).string()))
            return fail(f + " is the tool '" + owner + "''s, with other bytes than " + tag +
                        " brings — nothing changed (F68-133)");
    }

    using fs::perms;
    const perms exec = perms::owner_all | perms::group_read | perms::group_exec | perms::others_read | perms::others_exec;
    const perms data = perms::owner_read | perms::owner_write | perms::group_read | perms::others_read;
    fs::create_directories(root / "lib" / "wasm32", ec);
    for (const std::string& f : files) {
        fs::path src = rel / f, dst = root / f;
        fs::create_directories(dst.parent_path(), ec);
        if (fs::is_symlink(fs::symlink_status(src, ec))) {
            fs::path tmp = dst;
            tmp += ".tmp." + std::to_string(getpid());
            fs::remove(tmp, ec);
            fs::create_symlink(fs::read_symlink(src, ec), tmp, ec);
            if (!ec) fs::rename(tmp, dst, ec);
            if (ec) return fail("cannot place " + dst.string() + ": " + ec.message());
            continue;
        }
        bool isExec = (fs::status(src, ec).permissions() & perms::owner_exec) != perms::none;
        if (!installFile(src, dst, (f == "bin/aiml" || isExec) ? exec : data, err)) return fail(err);
    }
    if (fs::is_empty(root / "lib" / "wasm32", ec)) fs::remove(root / "lib" / "wasm32", ec);

    // The previous update's files that this release no longer has.
    size_t dropped = 0;
    try {
        YAML::Node prev = YAML::LoadFile(releaseRecordFile(root).string());
        for (const std::string& f : strList(prev["files"])) {
            if (std::find(files.begin(), files.end(), f) != files.end() || !toolOwning(f).empty()) continue;
            if (fs::remove(root / f, ec)) ++dropped;
        }
    } catch (const std::exception&) {}

    std::ostringstream ss;
    ss << "{\"version\": \"" << jsonEscape(tag) << "\", \"source\": \"" << jsonEscape(source)
       << "\", \"digest\": \"sha256:" << hex << "\", \"files\": [";
    for (size_t i = 0; i < files.size(); ++i) ss << (i ? ", \"" : "\"") << jsonEscape(files[i]) << "\"";
    ss << "]}\n";
    if (!writeText(releaseRecordFile(root), ss.str(), err)) return fail(err);
    std::cout << "updated " << root.string() << ": " << from << " → " << tag << " (" << files.size()
              << " files, sha256:" << hex << (dropped ? ", " + std::to_string(dropped) + " old file(s) removed" : "")
              << ") from " << source << "\n  installed tools are not rebuilt; `install <name>` again rebuilds "
                 "one with the new compiler (F68-134)" << std::endl;
    return 0;
}

}  // namespace

std::string ownRoot() {
    static const std::string root = [] {
        fs::path img = ownImage();
        if (img.empty()) return std::string();
        fs::path dir = img.parent_path();
        bool placed = (img.filename() == "aiml" && dir.filename() == "bin")
                   || (img.filename() == "libaiml.so" && dir.filename() == "lib");
        return placed && isVenv(dir.parent_path()) ? dir.parent_path().string() : std::string();
    }();
    return root;
}

std::string ownAssetRoot() {
    std::string root = ownRoot();
    if (root.empty()) return "";
    std::error_code ec;
    fs::path dir = fs::path(root) / "assets";
    return fs::is_directory(dir, ec) ? dir.string() : std::string();
}

namespace {

// §2: create or update (F68-10…14).
int createVenv(const fs::path& root, const std::string& runtimeArg) {
    std::error_code ec;
    // F68-13: missing, empty or a venv already.
    if (fs::exists(root, ec) && !(fs::is_directory(root, ec) && fs::is_empty(root, ec)) && !isVenv(root))
        return fail(root.string() + " exists and is not a venv (no " + kMarker + " in env); "
                    "choose a new or empty directory");

    fs::path exe = runningBinary();
    if (exe.empty()) return fail("cannot find the running compiler (/proc/self/exe)");
    fs::path runtime = findRuntime(exe, runtimeArg);
    if (runtime.empty())
        return fail(runtimeArg.empty()
            ? "no libaiml.so next to " + exe.string() + "; name it with --runtime <libaiml.so>"
            : "--runtime " + runtimeArg + ": no such file");

    fs::create_directories(root / "bin", ec);
    if (!ec) fs::create_directories(root / "lib" / "aiml", ec);
    if (ec) return fail("cannot create " + root.string() + ": " + ec.message());

    using fs::perms;
    const perms exec = perms::owner_all | perms::group_read | perms::group_exec
                     | perms::others_read | perms::others_exec;
    const perms data = perms::owner_read | perms::owner_write | perms::group_read | perms::others_read;
    std::string err;
    if (!installFile(exe, root / "bin" / "aiml", exec, err)) return fail(err);
    if (!installFile(runtime, root / "lib" / "libaiml.so", data, err)) return fail(err);
    // F68-94: the wasm32 runtime of a release (or of the venv we run from) comes along.
    fs::path wasmSrc = runtime.parent_path() / "wasm32";
    if (fs::is_directory(wasmSrc, ec)) {
        fs::path wasmDst = root / "lib" / "wasm32";
        fs::create_directories(wasmDst, ec);
        if (ec) return fail("cannot create " + wasmDst.string() + ": " + ec.message());
        for (const auto& ent : fs::directory_iterator(wasmSrc, ec)) {
            if (!ent.is_regular_file(ec)) continue;
            if (!installFile(ent.path(), wasmDst / ent.path().filename(), data, err)) return fail(err);
        }
    }
    // F68-117: the version of a release comes along, next to the runtime it describes.
    fs::path stamp = runtime.parent_path() / "aiml.version";
    if (fs::is_regular_file(stamp, ec) && !installFile(stamp, root / "lib" / "aiml.version", data, err))
        return fail(err);
    fs::path table = tableFile(root);
    if (!fs::exists(table, ec) && !writeText(table, "{\"packages\": {}}\n", err)) return fail(err);
    if (!writeText(root / "env", envText(root.string()), err)) return fail(err);

    std::cout << "venv " << root.string() << "\n  use it with: . " << root.string() << "/env"
              << std::endl;
    return 0;
}

}  // namespace

int runProgram(const std::vector<std::string>& args, const std::string& cwd,
               const std::vector<std::string>& env, std::string* out) {
    return runProg(args, cwd.empty() ? fs::path() : fs::path(cwd), env, out);
}

bool download(const std::string& url, const std::string& toFile, std::string& err) {
    std::string body;
    return httpGet(url, fs::path(toFile), body, err);
}

std::string sha256File(const std::string& path) {
    char* hex = aiml_sha256_file(path.c_str());
    return hex ? std::string(hex) : std::string();
}

int command(int argc, char** argv) {
    std::string dirArg, runtimeArg, verb, name, sub;
    std::string gitUrl, ref, pathArg, kontorUrl, versionArg;
    bool all = false, overwrite = false, checkOnly = false;
    std::string githubRepo;
    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--runtime" && i + 1 < argc) runtimeArg = argv[++i];
        else if (arg == "--git" && i + 1 < argc) gitUrl = argv[++i];
        else if (arg == "--ref" && i + 1 < argc) ref = argv[++i];
        else if (arg == "--path" && i + 1 < argc) pathArg = argv[++i];
        else if (arg == "--kontor" && i + 1 < argc) kontorUrl = argv[++i];
        else if (arg == "--version" && i + 1 < argc) versionArg = argv[++i];
        else if (arg == "--all") all = true;
        else if (arg == "--overwrite") overwrite = true;
        else if (arg == "--check") checkOnly = true;                       // §10
        else if (arg == "--github" && i + 1 < argc) githubRepo = argv[++i];
        else if (!arg.empty() && arg[0] != '-') {
            if (dirArg.empty()) dirArg = arg;
            else if (verb.empty()) verb = arg;
            else if (verb == "asset" && sub.empty()) sub = arg;   // §9: asset <sub> [<name>]
            else if (name.empty()) name = arg;
            else return usage();
        }
        else return usage();
    }
    if (dirArg.empty()) return usage();

    std::error_code ec;
    fs::path root = fs::weakly_canonical(fs::absolute(dirArg), ec);
    if (ec) return fail("cannot resolve " + dirArg + ": " + ec.message());
    if (root.string().find('\'') != std::string::npos)
        return fail("a venv path must not contain a single quote: " + root.string());

    if (verb.empty()) return createVenv(root, runtimeArg);

    // §6 works ON a venv, so there has to be one.
    if (!isVenv(root))
        return fail(root.string() + " is not a venv — create it with `aiml venv " + dirArg + "`");
    if (overwrite && verb != "publish" && !(verb == "asset" && sub == "publish"))
        return usage();                                    // F68-76/F68-125: publish only
    if (verb == "asset") {                                 // §9
        if (all) return usage();
        if (sub == "install") return cmdAssetInstall(root, name, kontorUrl, versionArg, pathArg);
        if (sub == "publish") return cmdAssetPublish(root, name, versionArg, pathArg, kontorUrl, overwrite);
        if (sub == "list")    return name.empty() ? cmdAssetList(root) : usage();
        if (sub == "remove")  return name.empty() ? usage() : cmdAssetRemove(root, name);
        return usage();
    }
    if (verb == "install") {
        if (all) {
            if (!name.empty() || (!pathArg.empty() && !kontorUrl.empty())) return usage();
            return pathArg.empty() ? cmdInstallAll(root, kontorUrl) : cmdInstallAllPath(root, pathArg);
        }
        return cmdInstall(root, name, gitUrl, ref, pathArg, kontorUrl, versionArg);
    }
    if (verb == "update")                                  // §10
        return name.empty() && !all ? cmdUpdate(root, versionArg, githubRepo, kontorUrl, checkOnly) : usage();
    if ((checkOnly || !githubRepo.empty())) return usage();
    if (verb == "publish") return all ? usage() : cmdPublish(name, pathArg, kontorUrl, overwrite);
    if (verb == "list")    return name.empty() ? cmdList(root) : usage();
    if (verb == "remove")  return name.empty() ? usage() : cmdRemove(root, name);
    return usage();
}

}  // namespace venv
}  // namespace aiml
