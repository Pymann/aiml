// build.cpp — see build.h (spec/68 §7).
#include "build.h"
#include "venv.h"

#include <fcntl.h>
#include <fnmatch.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <yaml-cpp/yaml.h>

extern char** environ;

namespace fs = std::filesystem;

namespace aiml {
namespace build {

namespace {

const char* const kSchema = "exodus.project.config";   // F68-80: "…-1.0"
const char* const kLaunchpad = "https://launchpad.net/ubuntu/+archive/primary/+files";

struct Failure {
    std::string what;
};

[[noreturn]] void refuse(const std::string& what) { throw Failure{what}; }

std::mutex outMutex;   // parallel compiles print whole lines (F68-112)

void say(const std::string& line) {
    std::lock_guard<std::mutex> lock(outMutex);
    std::cout << line << std::endl;
}

std::string scalar(const YAML::Node& n, const std::string& dflt = "") {
    return n && n.IsScalar() ? n.as<std::string>() : dflt;
}

bool flag(const YAML::Node& n, bool dflt) {
    if (!n || !n.IsScalar()) return dflt;
    try { return n.as<bool>(); } catch (const std::exception&) { return dflt; }
}

std::vector<std::string> strings(const YAML::Node& n) {
    std::vector<std::string> out;
    if (n && n.IsSequence())
        for (const auto& item : n)
            if (item.IsScalar()) out.push_back(item.as<std::string>());
    return out;
}

bool isVenvRoot(const fs::path& root) {
    std::ifstream in(root / "env");
    std::string first;
    return in && std::getline(in, first) && first.rfind("# aiml venv", 0) == 0;
}

std::string which(const std::string& prog) {
    if (prog.find('/') != std::string::npos) {
        std::error_code ec;
        return fs::is_regular_file(prog, ec) && access(prog.c_str(), X_OK) == 0 ? prog : "";
    }
    const char* path = std::getenv("PATH");
    if (!path) return "";
    std::string all = path;
    size_t at = 0;
    while (at <= all.size()) {
        size_t end = all.find(':', at);
        if (end == std::string::npos) end = all.size();
        std::string dir = all.substr(at, end - at);
        at = end + 1;
        if (dir.empty()) continue;
        fs::path cand = fs::path(dir) / prog;
        std::error_code ec;
        if (fs::is_regular_file(cand, ec) && access(cand.c_str(), X_OK) == 0) return cand.string();
    }
    return "";
}

// F68-21: $AIML, else $AIML_VENV/bin/aiml, else aiml from PATH.
std::string compilerFromEnv() {
    const char* a = std::getenv("AIML");
    if (a && *a) return a;
    const char* v = std::getenv("AIML_VENV");
    if (v && *v) return (fs::path(v) / "bin" / "aiml").string();
    return which("aiml");
}

// F68-23: $AIML_VENV, else the venv the resolved compiler belongs to.
std::string venvRoot() {
    const char* v = std::getenv("AIML_VENV");
    if (v && *v) return v;
    std::string comp = compilerFromEnv();
    if (!comp.empty()) {
        std::error_code ec;
        fs::path root = fs::weakly_canonical(fs::path(comp), ec).parent_path().parent_path();
        if (!ec && isVenvRoot(root)) return root.string();
    }
    return "";
}

// F68-100: exodus' cache — $EXODUS_CACHE, else <working directory>/__exodus_cache.
fs::path cacheRoot() {
    const char* env = std::getenv("EXODUS_CACHE");
    std::error_code ec;
    if (env && *env) return fs::weakly_canonical(fs::absolute(env), ec);
    return fs::absolute(fs::current_path() / "__exodus_cache");
}

// F68-85 / F68-101: __exodus_cache, then $AIML_VENV (it starts with $AIML), then
// $AIML; nothing else — $ORIGIN stays.
std::string expand(std::string s) {
    size_t at;
    const std::string cacheWord = "__exodus_cache";
    if (s.find(cacheWord) != std::string::npos) {
        std::string root = cacheRoot().string();
        size_t from = 0;
        while ((at = s.find(cacheWord, from)) != std::string::npos) {
            s.replace(at, cacheWord.size(), root);
            from = at + root.size();
        }
    }
    while ((at = s.find("$AIML_VENV")) != std::string::npos) {
        std::string root = venvRoot();
        if (root.empty())
            refuse("$AIML_VENV cannot be resolved: no AIML_VENV set and the compiler belongs to "
                   "no venv — activate one with `. <venv>/env` (spec/68 F68-23)");
        s.replace(at, 10, root);
    }
    while ((at = s.find("$AIML")) != std::string::npos) {
        std::string comp = compilerFromEnv();
        if (comp.empty())
            refuse("$AIML cannot be resolved: no AIML set and no aiml on PATH — activate a venv "
                   "with `. <venv>/env` (spec/68 F68-23)");
        s.replace(at, 5, comp);
    }
    return s;
}

bool nonEmpty(const YAML::Node& n) {
    return n && !n.IsNull() && ((n.IsSequence() || n.IsMap()) ? n.size() > 0 : true);
}

std::string show(const std::vector<std::string>& cmd) {
    std::string line = "  $";
    for (const std::string& a : cmd) line += " " + a;
    return line;
}

// F68-86/112: a program run without a shell.  posix_spawn rather than fork, so that
// compiles started from several threads are safe; the environment is built here, in
// the caller.  `quiet` sends the program's stderr to /dev/null (apt-get's „E: Unable
// to locate" for a package the mirror dropped is not an error of the build).
int spawnWait(const std::vector<std::string>& args, const std::vector<std::string>& envAdd,
              const std::string& cwd, bool quiet, std::string* out = nullptr) {
    std::map<std::string, std::string> env;
    for (char** e = environ; e && *e; ++e) {
        std::string kv = *e;
        size_t eq = kv.find('=');
        if (eq != std::string::npos) env[kv.substr(0, eq)] = kv.substr(eq + 1);
    }
    for (const std::string& kv : envAdd) {
        size_t eq = kv.find('=');
        if (eq != std::string::npos) env[kv.substr(0, eq)] = kv.substr(eq + 1);
    }
    std::vector<std::string> envStore;
    for (const auto& kv : env) envStore.push_back(kv.first + "=" + kv.second);
    std::vector<char*> envp, argv;
    for (std::string& s : envStore) envp.push_back(s.data());
    envp.push_back(nullptr);
    std::vector<std::string> argStore = args;
    for (std::string& s : argStore) argv.push_back(s.data());
    argv.push_back(nullptr);

    // posix_spawnp searches PATH of THIS process; the configuration's env may set
    // another PATH, so the program is resolved against the child's PATH first.
    std::string prog = args[0];
    if (prog.find('/') == std::string::npos) {
        auto it = env.find("PATH");
        if (it != env.end()) {
            size_t at = 0;
            const std::string& all = it->second;
            while (at <= all.size()) {
                size_t end = all.find(':', at);
                if (end == std::string::npos) end = all.size();
                std::string dir = all.substr(at, end - at);
                at = end + 1;
                if (dir.empty()) continue;
                fs::path cand = fs::path(dir) / prog;
                if (access(cand.c_str(), X_OK) == 0) { prog = cand.string(); break; }
            }
        }
    }

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    int fds[2] = {-1, -1};
    if (out) {
        if (pipe(fds) != 0) { posix_spawn_file_actions_destroy(&fa); return 127; }
        posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
        posix_spawn_file_actions_addclose(&fa, fds[0]);
        posix_spawn_file_actions_addclose(&fa, fds[1]);
    }
    if (quiet) posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    std::string oldCwd;
    // posix_spawn_file_actions_addchdir_np is glibc ≥ 2.29; cwd is only used for
    // one-off steps (apt-get, git, setup commands), never from several threads.
    if (!cwd.empty()) {
        std::error_code ec;
        oldCwd = fs::current_path(ec).string();
        if (chdir(cwd.c_str()) != 0) { posix_spawn_file_actions_destroy(&fa); return 127; }
    }
    pid_t pid = 0;
    int rc = posix_spawn(&pid, prog.c_str(), &fa, nullptr, argv.data(), envp.data());
    if (!oldCwd.empty() && chdir(oldCwd.c_str()) != 0) {}
    posix_spawn_file_actions_destroy(&fa);
    if (out) close(fds[1]);
    if (rc != 0) {
        if (out) close(fds[0]);
        return 127;
    }
    if (out) {
        char buf[4096];
        ssize_t n;
        while ((n = read(fds[0], buf, sizeof buf)) > 0) out->append(buf, static_cast<size_t>(n));
        close(fds[0]);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128;
}

void run(const std::vector<std::string>& cmd, const std::vector<std::string>& env,
         const std::string& what) {
    say(show(cmd));
    int rc = spawnWait(cmd, env, "", false);
    if (rc == 127) refuse(what + ": '" + cmd[0] + "' could not be run (not on PATH?)");
    if (rc != 0) refuse(what + " failed (exit " + std::to_string(rc) + ")");
}

// ── sources: paths and glob patterns, `**` as in exodus (pathlib) ─────────

void globInto(const fs::path& base, const std::vector<std::string>& segs, size_t i,
              std::vector<std::string>& out) {
    std::error_code ec;
    if (i == segs.size()) {
        if (fs::is_regular_file(base, ec)) out.push_back(base.lexically_normal().string());
        return;
    }
    const std::string& seg = segs[i];
    if (seg == "**") {
        globInto(base, segs, i + 1, out);                      // zero directories
        if (!fs::is_directory(base.empty() ? "." : base, ec)) return;
        for (const auto& ent : fs::directory_iterator(base.empty() ? "." : base, ec)) {
            std::string name = ent.path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            if (ent.is_directory(ec) && !ent.is_symlink(ec)) globInto(base / name, segs, i, out);
        }
        return;
    }
    if (seg.find_first_of("*?[") == std::string::npos) {
        globInto(base / seg, segs, i + 1, out);
        return;
    }
    fs::path dir = base.empty() ? fs::path(".") : base;
    if (!fs::is_directory(dir, ec)) return;
    for (const auto& ent : fs::directory_iterator(dir, ec)) {
        std::string name = ent.path().filename().string();
        if (name.empty() || (name[0] == '.' && seg[0] != '.')) continue;
        if (fnmatch(seg.c_str(), name.c_str(), 0) == 0) globInto(base / name, segs, i + 1, out);
    }
}

// Sources are paths, or glob patterns as exodus took them (relative to the working
// directory, F68-80).  A pattern that matches nothing adds nothing, as in exodus
// (F68-81); a path that is not there is an error.
std::vector<std::string> resolveSources(const std::vector<std::string>& patterns) {
    std::vector<std::string> out;
    for (const std::string& p : patterns) {
        if (p.find_first_of("*?[") == std::string::npos) {
            std::error_code ec;
            if (!fs::is_regular_file(p, ec)) refuse("the source '" + p + "' is not there");
            out.push_back(fs::path(p).lexically_normal().string());
            continue;
        }
        std::vector<std::string> segs;
        std::stringstream ss(p);
        std::string seg;
        while (std::getline(ss, seg, '/'))
            if (!seg.empty() && seg != ".") segs.push_back(seg);
        std::vector<std::string> found;
        globInto(p[0] == '/' ? fs::path("/") : fs::path(), segs, 0, found);
        out.insert(out.end(), found.begin(), found.end());
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool isCSource(const std::string& ext) { return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx"; }

struct AptPkg {
    std::string name, version, arch, digest;
    bool required = true;
};

struct GitPkg {
    std::string name, repo, ref, digest;
    std::vector<std::vector<std::string>> setup;
    bool required = true;
};

struct Config {
    std::string file, name, outputType;
    std::vector<std::string> sources, linkerFlags, libraries, libraryPaths, assets;
    std::vector<std::string> includes, defines;
    std::string compiler, linker, script, mapFile;
    std::vector<std::string> compilerFlags, warnings;
    std::string langStd, optimization = "0";
    bool debug = true, werror = false;
    bool rpath = true, inCwd = false;
    int jobs = 0;
    std::vector<std::string> env;   // K=V, expanded
    std::map<std::string, std::string> hooks;   // stage → .aiml file
    std::vector<AptPkg> apt;
    std::vector<GitPkg> git;
};

Config load(const std::string& file) {
    YAML::Node d;
    try {
        d = YAML::LoadFile(file);   // JSON is YAML
    } catch (const std::exception& e) {
        refuse(file + ": " + e.what());
    }
    if (!d.IsMap()) refuse(file + " is no project file");
    Config c;
    c.file = file;
    c.name = scalar(d["name"]);
    if (c.name.empty()) refuse(file + " has no \"name\"");

    // F68-82: what no build step here can do, refused by name.
    c.outputType = scalar(d["output_type"], "executable");
    if (c.outputType != "executable" && c.outputType != "shared_lib" && c.outputType != "wasm" &&
        c.outputType != "static_lib")
        refuse(file + ": output_type '" + c.outputType + "' is none that `aiml build` knows "
               "(executable, shared_lib, static_lib, wasm — spec/68 F68-81)");
    const bool wasm = c.outputType == "wasm";
    if (nonEmpty(d["conan_packages"]))
        refuse(file + ": conan_packages — `aiml build` fetches apt and git packages, not Conan "
               "packages (spec/68 F68-82)");

    // §7.2 / §7.3: the packages, read here and fetched before anything is built.
    if (d["apt_packages"] && d["apt_packages"].IsSequence())
        for (const auto& p : d["apt_packages"]) {
            AptPkg a;
            a.name = scalar(p["name"]);
            a.version = scalar(p["version"]);
            a.arch = scalar(p["arch"], "amd64");
            a.digest = scalar(p["digest"]);
            a.required = flag(p["required"], true);
            if (a.name.empty() || a.version.empty())
                refuse(file + ": an apt_packages entry needs \"name\" and \"version\" (spec/68 F68-100)");
            c.apt.push_back(a);
        }
    if (d["git_packages"] && d["git_packages"].IsSequence())
        for (const auto& p : d["git_packages"]) {
            GitPkg g;
            g.name = scalar(p["name"]);
            g.repo = scalar(p["repo"]);
            g.ref = scalar(p["ref"], "HEAD");
            g.digest = scalar(p["digest"]);
            g.required = flag(p["required"], true);
            if (p["setup_commands"] && p["setup_commands"].IsSequence())
                for (const auto& cmd : p["setup_commands"]) g.setup.push_back(strings(cmd));
            if (g.name.empty() || g.repo.empty())
                refuse(file + ": a git_packages entry needs \"name\" and \"repo\" (spec/68 F68-105)");
            c.git.push_back(g);
        }

    for (const std::string& src : resolveSources(strings(d["sources"]))) {
        std::string ext = fs::path(src).extension().string();
        if (ext != ".aiml" && ext != ".o" && !isCSource(ext))
            refuse(file + ": the source '" + src + "' is neither .aiml, .o nor C/C++ (.c .cc .cpp "
                   ".cxx) — spec/68 F68-82");
        // F68-88: a native object does not link into a wasm module.
        if (wasm && ext == ".o")
            refuse(file + ": the source '" + src + "' is a native object — a wasm build links "
                   "only what the compiler emits for wasm32 (spec/68 F68-88)");
        c.sources.push_back(src);
    }
    if (c.sources.empty()) refuse(file + " names no sources");

    // F68-87: hooks are aiml, run by the JIT.
    for (const char* stage : {"pre_compilation", "pre_linkage", "post_linkage"}) {
        std::string hook = scalar(d[stage]);
        if (hook.empty()) continue;
        if (fs::path(hook).extension() != ".aiml")
            refuse(file + ": the " + stage + " hook '" + hook +
                   "' is no aiml file — port it to an .aiml hook, run by the JIT (spec/68 F68-87)");
        std::error_code ec;
        if (!fs::is_regular_file(hook, ec))
            refuse(file + ": the " + std::string(stage) + " hook '" + hook + "' is not there");
        c.hooks[stage] = hook;
    }

    YAML::Node comp = d["compiler"], link = d["linker"];
    bool hasC = false;
    for (const std::string& s : c.sources) hasC = hasC || isCSource(fs::path(s).extension().string());
    c.compiler = expand(scalar(comp ? comp["name"] : YAML::Node(), hasC ? "cc" : "$AIML"));
    if (comp) {
        for (const std::string& f : strings(comp["flags"])) c.compilerFlags.push_back(expand(f));
        c.langStd = scalar(comp["lang_standard"]);
    }
    c.optimization = scalar(d["optimization"], "0");
    c.debug = flag(d["debug"], true);
    c.werror = flag(d["werror"], false);
    if (d["warnings"] && d["warnings"].IsSequence()) c.warnings = strings(d["warnings"]);
    else c.warnings = {"all", "extra"};
    if (d["jobs"] && d["jobs"].IsScalar()) {
        try { c.jobs = d["jobs"].as<int>(); } catch (const std::exception&) {}
    }

    // F68-88: emscripten links a wasm build — $EMCC, else emcc from PATH.
    std::string defaultLinker = "cc";
    if (wasm) {
        const char* em = std::getenv("EMCC");
        defaultLinker = em && *em ? em : "emcc";
    }
    if (c.outputType == "static_lib") defaultLinker = "ar";     // F68-113
    c.linker = expand(scalar(link ? link["name"] : YAML::Node(), defaultLinker));
    if (link) {
        for (const std::string& f : strings(link["flags"])) c.linkerFlags.push_back(expand(f));
        c.libraries = strings(link["libraries"]);
        for (const std::string& p : strings(link["library_paths"])) c.libraryPaths.push_back(expand(p));
        c.rpath = flag(link["rpath"], true);
        if (!scalar(link["script"]).empty()) c.script = expand(scalar(link["script"]));
        if (!scalar(link["map_file"]).empty()) c.mapFile = expand(scalar(link["map_file"]));
    }
    for (const std::string& p : strings(d["asset_directories"])) c.assets.push_back(expand(p));
    for (const std::string& p : strings(d["search_paths"])) c.includes.push_back("-I" + expand(p));
    if (d["defines"] && d["defines"].IsMap())
        for (const auto& kv : d["defines"]) {
            std::string k = kv.first.as<std::string>(), v = scalar(kv.second);
            c.defines.push_back(v.empty() ? "-D" + k : "-D" + k + "=" + v);
        }
    c.inCwd = flag(d["artifact_in_cwd"], false);
    if (d["env"] && d["env"].IsMap())
        for (const auto& kv : d["env"])
            c.env.push_back(kv.first.as<std::string>() + "=" + expand(scalar(kv.second)));
    return c;
}

std::string safeName(const std::string& name) {
    std::string out;
    for (char ch : name) out += (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_' || ch == '.') ? ch : '_';
    return out.empty() ? "project" : out;
}

// ── §7.2: apt packages ────────────────────────────────────────────────────

std::vector<fs::path> debsIn(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    for (const auto& ent : fs::directory_iterator(dir, ec))
        if (ent.path().extension() == ".deb") out.push_back(ent.path());
    std::sort(out.begin(), out.end());
    return out;
}

// F68-100a: dpkg-deb -x, else ar x + tar -xf data.tar.*.
void unpackDeb(const fs::path& deb, const fs::path& payload, const std::string& what) {
    std::error_code ec;
    fs::create_directories(payload, ec);
    if (!which("dpkg-deb").empty()) {
        if (spawnWait({"dpkg-deb", "-x", deb.string(), payload.string()}, {}, "", false) == 0) return;
        refuse(what + ": dpkg-deb -x " + deb.string() + " failed");
    }
    fs::path tmp = payload.parent_path() / "_unpack";
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    if (spawnWait({"ar", "x", fs::absolute(deb).string()}, {}, tmp.string(), false) != 0)
        refuse(what + ": neither dpkg-deb nor `ar x` could unpack " + deb.string());
    fs::path data;
    for (const auto& ent : fs::directory_iterator(tmp, ec))
        if (ent.path().filename().string().rfind("data.tar", 0) == 0) data = ent.path();
    if (data.empty()) refuse(what + ": " + deb.string() + " has no data.tar member");
    if (spawnWait({"tar", "-xf", data.string(), "-C", payload.string()}, {}, "", false) != 0)
        refuse(what + ": tar could not unpack " + data.string());
    fs::remove_all(tmp, ec);
}

// One package present in the cache, or fetched there (F68-100…100c).  false: an
// optional package that could not be fetched (skipped, said).  A fetch goes to a
// directory of its own and replaces the cached entry only once it is verified and
// unpacked: a failed fetch leaves the cache as it was (F68-100b).
bool ensureApt(const AptPkg& p, const fs::path& cache) {
    const std::string what = p.name + ":" + p.arch + "=" + p.version;
    fs::path dir = cache / "apt" / p.name / p.arch / p.version;
    std::error_code ec;
    std::vector<fs::path> debs = debsIn(dir / "_download");
    bool stale = false;
    if (fs::is_directory(dir / "payload", ec) && !debs.empty()) {
        std::string got = venv::sha256File(debs.front().string());
        if (p.digest.empty() || got == p.digest) {
            if (p.digest.empty())
                say("  apt " + what + " has no digest — pin \"digest\": \"" + got + "\" (spec/68 F68-100b)");
            return true;
        }
        stale = true;
        say("  apt " + what + ": the cached .deb has sha256 " + got + ", not the pinned " + p.digest +
            " — fetching the pinned one");
    }
    fs::path work = dir.parent_path() / ("." + p.version + ".fetch." + std::to_string(getpid()));
    fs::path dl = work / "_download";
    fs::remove_all(work, ec);
    fs::create_directories(dl, ec);
    if (ec) refuse("cannot make " + dl.string() + ": " + ec.message());
    auto keep = stale ? std::string(" — the cached entry " + dir.string() + " is left as it was") : std::string();

    // The mirror first: fast, and apt-get download needs no root.
    std::string fromWhere;
    if (!which("apt-get").empty()) {
        int rc = spawnWait({"apt-get", "download", what}, {}, dl.string(), true);
        if (rc == 0 && !debsIn(dl).empty()) fromWhere = "the mirror";
    }
    std::string tried = "apt-get download " + what;
    if (fromWhere.empty()) {
        const char* baseEnv = std::getenv("AIML_DEB_URL");
        std::string base = baseEnv && *baseEnv ? baseEnv : kLaunchpad;
        while (!base.empty() && base.back() == '/') base.pop_back();
        std::string bare = p.version.find(':') == std::string::npos ? p.version : p.version.substr(p.version.find(':') + 1);
        std::vector<std::string> arches = {p.arch};
        if (p.arch != "all") arches.push_back("all");
        for (const std::string& a : arches) {
            std::string fname = p.name + "_" + bare + "_" + a + ".deb";
            std::string url = base + "/" + fname, err;
            if (venv::download(url, (dl / fname).string(), err)) { fromWhere = url; break; }
            fs::remove(dl / fname, ec);
            tried += ", " + url;
        }
    }
    if (fromWhere.empty()) {
        fs::remove_all(work, ec);
        if (!p.required) {
            say("  apt " + what + " (optional) is not to be had — skipped (tried " + tried + ")" + keep);
            return false;
        }
        refuse("the apt package " + what + " is neither in the mirror nor in the archive (tried " +
               tried + ") — spec/68 F68-100c" + keep);
    }
    fs::path deb = debsIn(dl).front();
    std::string got = venv::sha256File(deb.string());
    if (!p.digest.empty() && got != p.digest) {
        fs::remove_all(work, ec);
        refuse("the apt package " + what + " from " + fromWhere + " has sha256 " + got +
               ", but the configuration pins " + p.digest + " — nothing unpacked (spec/68 F68-100b)" + keep);
    }
    unpackDeb(deb, work / "payload", "apt " + what);
    fs::remove_all(dir, ec);
    fs::rename(work, dir, ec);
    if (ec) refuse("cannot move the fetched " + what + " to " + dir.string() + ": " + ec.message());
    say("  apt " + what + " from " + fromWhere);
    if (p.digest.empty())
        say("  apt " + what + " has no digest — pin \"digest\": \"" + got + "\" (spec/68 F68-100b)");
    return true;
}

// F68-100d: a -dev package's libfoo.so → libfoo.so.1 points into another payload.
void repairLinks(const std::vector<fs::path>& payloads) {
    std::map<std::string, fs::path> index;
    std::error_code ec;
    for (const fs::path& pl : payloads)
        for (auto it = fs::recursive_directory_iterator(pl, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            std::error_code e2;
            if (fs::is_regular_file(it->path(), e2))        // a file, or a working link to one
                index.emplace(it->path().filename().string(), it->path());
        }
    for (const fs::path& pl : payloads)
        for (auto it = fs::recursive_directory_iterator(pl, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            std::error_code e2;
            const fs::path link = it->path();
            if (!fs::is_symlink(fs::symlink_status(link, e2)) || fs::exists(link, e2)) continue;
            fs::path target = fs::read_symlink(link, e2);
            auto hit = index.find(target.filename().string());
            fs::remove(link, e2);
            if (hit != index.end()) {
                fs::create_symlink(hit->second, link, e2);
            } else {
                say("  removed the broken link " + link.string() + " → " + target.string() +
                    ": its runtime package is not in the configuration (spec/68 F68-100d)");
            }
        }
}

// ── §7.3: git packages ────────────────────────────────────────────────────

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

std::string gitHead(const fs::path& dir) {
    std::string out;
    if (spawnWait({"git", "-C", dir.string(), "rev-parse", "HEAD"}, {}, "", true, &out) != 0) return "";
    return trim(out);
}

bool ensureGit(const GitPkg& g, const fs::path& cache) {
    if (which("git").empty()) refuse("git_packages need git, and there is no git on PATH (spec/68 F68-105)");
    std::error_code ec;
    fs::path base = cache / "git" / g.name;
    fs::path dir = g.digest.empty() ? fs::path() : base / g.digest;
    auto failOrSkip = [&](const std::string& why) -> bool {
        if (!g.required) {
            say("  git " + g.name + " (optional): " + why + " — skipped");
            return false;
        }
        refuse("the git package " + g.name + ": " + why);
    };
    if (dir.empty() || !fs::is_directory(dir, ec)) {
        fs::create_directories(base, ec);
        fs::path tmp = base / ("_clone." + std::to_string(getpid()));
        fs::remove_all(tmp, ec);
        std::vector<std::string> cmd = {"git", "clone", "--depth", "1"};
        if (!g.ref.empty() && g.ref != "HEAD") { cmd.push_back("--branch"); cmd.push_back(g.ref); }
        cmd.push_back(g.repo);
        cmd.push_back(tmp.string());
        say(show(cmd));
        if (spawnWait(cmd, {}, "", false) != 0) {
            fs::remove_all(tmp, ec);
            return failOrSkip("git clone " + g.repo + " (ref " + g.ref + ") failed");
        }
        std::string head = gitHead(tmp);
        if (!g.digest.empty() && head != g.digest) {
            std::vector<std::string> fetch = {"git", "-C", tmp.string(), "fetch", "--depth", "1", "origin", g.digest};
            say(show(fetch));
            bool ok = spawnWait(fetch, {}, "", false) == 0 &&
                      spawnWait({"git", "-C", tmp.string(), "checkout", "-q", "--detach", g.digest}, {}, "", false) == 0;
            head = gitHead(tmp);
            if (!ok || head != g.digest) {
                fs::remove_all(tmp, ec);
                return failOrSkip("the pinned commit " + g.digest + " is not to be had from " + g.repo +
                                  " (ref " + g.ref + " is at " + (head.empty() ? "?" : head) + ")");
            }
        }
        if (head.empty()) { fs::remove_all(tmp, ec); return failOrSkip("the clone has no HEAD"); }
        if (g.digest.empty())
            say("  git " + g.name + " has no digest — pin \"digest\": \"" + head + "\" (spec/68 F68-105)");
        dir = base / head;
        if (fs::is_directory(dir, ec)) fs::remove_all(tmp, ec);
        else fs::rename(tmp, dir, ec);
        if (ec) refuse("cannot move the clone to " + dir.string() + ": " + ec.message());
    }
    // F68-106: the setup commands, until they have all succeeded once.
    fs::path marker = dir / ".aiml_setup_done";
    if (!g.setup.empty() && !fs::exists(marker, ec)) {
        for (const auto& cmd : g.setup) {
            if (cmd.empty()) continue;
            say(show(cmd));
            int rc = spawnWait(cmd, {}, dir.string(), false);
            if (rc != 0)
                return failOrSkip("the setup command '" + cmd[0] + "' failed (exit " + std::to_string(rc) + ")");
        }
        std::ofstream(marker) << "done\n";
    }
    // F68-107: current → the commit directory.
    fs::path current = base / "current";
    fs::remove(current, ec);
    fs::create_directory_symlink(dir.filename(), current, ec);
    return true;
}

void fetchPackages(const Config& c) {
    if (c.apt.empty() && c.git.empty()) return;
    fs::path cache = cacheRoot();
    std::vector<fs::path> payloads;
    for (const AptPkg& p : c.apt)
        if (ensureApt(p, cache)) payloads.push_back(cache / "apt" / p.name / p.arch / p.version / "payload");
    if (!payloads.empty()) repairLinks(payloads);
    for (const GitPkg& g : c.git) ensureGit(g, cache);
}

// F68-102: a ':' in a library path splits the RUNPATH; inside the cache it gets a
// colon-free alias.  "" → leave the entry out.
std::string rpathEntry(const std::string& dir) {
    if (dir.find(':') == std::string::npos) return dir;
    fs::path cache = cacheRoot();
    fs::path real = fs::path(dir).lexically_normal();
    fs::path rel = real.lexically_relative(cache);
    if (rel.empty() || *rel.begin() == ".." || cache.string().find(':') != std::string::npos) {
        say("  " + dir + " contains ':' and lies outside the cache — not added to the rpath (spec/68 F68-102)");
        return "";
    }
    fs::path alias = cache / "rpath";
    for (const auto& part : rel) {
        std::string s = part.string(), esc;
        for (char ch : s) esc += ch == ':' ? std::string("%3a") : std::string(1, ch);
        alias /= esc;
    }
    std::error_code ec;
    if (fs::is_symlink(fs::symlink_status(alias, ec))) {
        if (fs::read_symlink(alias, ec) == real) return alias.string();
        fs::remove(alias, ec);
    } else if (fs::exists(alias, ec)) {
        say("  " + alias.string() + " exists and is no link — " + dir + " not added to the rpath");
        return "";
    }
    fs::create_directories(alias.parent_path(), ec);
    fs::create_directory_symlink(real, alias, ec);
    if (ec) {
        say("  cannot make the rpath alias " + alias.string() + ": " + ec.message());
        return "";
    }
    return alias.string();
}

// The first x.y.z in a text, "" when there is none.
std::string firstVersion(const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) continue;
        if (i > 0 && (std::isalnum(static_cast<unsigned char>(text[i - 1])) || text[i - 1] == '.')) continue;
        size_t j = i;
        int dots = 0;
        while (j < text.size() && (std::isdigit(static_cast<unsigned char>(text[j])) || text[j] == '.')) {
            if (text[j] == '.') ++dots;
            ++j;
        }
        std::string cand = text.substr(i, j - i);
        while (!cand.empty() && cand.back() == '.') { cand.pop_back(); --dots; }
        if (dots == 2) return cand;
        i = j;
    }
    return "";
}

// F68-89: the wasm32 runtime and the emcc that links against it come from the same
// emsdk.  A library path that holds libaiml_wasm.a and its emsdk.version (F68-90)
// names the version; an emcc of another version is refused before anything is built.
void checkEmsdk(const Config& c, const std::string& file) {
    for (const std::string& p : c.libraryPaths) {
        std::error_code ec;
        fs::path stamp = fs::path(p) / "emsdk.version";
        if (!fs::is_regular_file(fs::path(p) / "libaiml_wasm.a", ec) || !fs::is_regular_file(stamp, ec))
            continue;
        std::ifstream in(stamp);
        std::string want;
        std::getline(in, want);
        want = firstVersion(want);
        if (want.empty()) continue;
        std::string out;
        int rc = spawnWait({c.linker, "--version"}, c.env, "", false, &out);
        if (rc == 127) refuse(file + ": the linker '" + c.linker + "' could not be run (not on PATH? "
                              "activate emsdk " + want + ", or set $EMCC)");
        std::string have = firstVersion(out);
        if (have != want)
            refuse(file + ": " + p + "/libaiml_wasm.a was built with emsdk " + want + ", but '" +
                   c.linker + "' is " + (have.empty() ? std::string("of no version it names") : have) +
                   " — activate emsdk " + want + " (spec/68 F68-89)");
        return;
    }
}

// ── §7.4: C and C++ ───────────────────────────────────────────────────────

fs::file_time_type mtime(const fs::path& p, bool& exists) {
    std::error_code ec;
    auto t = fs::last_write_time(p, ec);
    exists = !ec;
    return t;
}

// The files a .d (Makefile format) names after its first ':'.
std::vector<std::string> depfile(const fs::path& d) {
    std::ifstream in(d);
    if (!in) return {};
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string flat;
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i] == '\\' && i + 1 < all.size() && all[i + 1] == '\n') { flat += ' '; ++i; continue; }
        flat += all[i];
    }
    // only the first rule: `-MP` adds empty phony rules after it
    size_t nl = flat.find('\n');
    std::string rule = nl == std::string::npos ? flat : flat.substr(0, nl);
    size_t colon = rule.find(": ");
    if (colon == std::string::npos) colon = rule.find(':');
    if (colon == std::string::npos) return {};
    std::vector<std::string> out;
    std::stringstream ss(rule.substr(colon + 1));
    std::string w;
    while (ss >> w) out.push_back(w);
    return out;
}

// F68-112: compile again or not.
bool upToDate(const fs::path& src, const fs::path& obj, const Config& c) {
    bool have = false, e = false;
    auto objT = mtime(obj, have);
    if (!have) return false;
    if (mtime(src, e) > objT || !e) return false;
    if (mtime(c.file, e) > objT && e) return false;
    if (c.compiler.find('/') != std::string::npos && mtime(c.compiler, e) > objT && e) return false;
    fs::path d = obj;
    d.replace_extension(".d");
    std::error_code ec;
    if (!fs::is_regular_file(d, ec)) return false;
    for (const std::string& dep : depfile(d)) {
        auto t = mtime(dep, e);
        if (!e || t > objT) return false;
    }
    return true;
}

std::vector<std::string> cCompileLine(const Config& c, const std::string& src, const fs::path& obj) {
    std::vector<std::string> cmd = {c.compiler};
    cmd.insert(cmd.end(), c.compilerFlags.begin(), c.compilerFlags.end());
    cmd.insert(cmd.end(), {"-c", fs::absolute(src).lexically_normal().string(), "-o", obj.string(), "-MMD", "-MP"});
    std::string ext = fs::path(src).extension().string();
    if (!c.langStd.empty()) cmd.push_back((ext == ".c" ? "-std=c" : "-std=c++") + c.langStd);
    cmd.push_back("-O" + c.optimization);
    if (c.debug) cmd.push_back("-g");
    for (const std::string& w : c.warnings) cmd.push_back("-W" + w);
    if (c.werror) cmd.push_back("-Werror");
    cmd.insert(cmd.end(), c.includes.begin(), c.includes.end());
    cmd.insert(cmd.end(), c.defines.begin(), c.defines.end());
    return cmd;
}

// Compile what is not up to date, `jobs` at a time; the first failure is named.
std::vector<std::string> compileC(const Config& c, const std::vector<std::string>& srcs,
                                  const fs::path& dir, int jobs, const std::string& file) {
    std::vector<std::string> objects;
    struct Job { std::string src; fs::path obj; };
    std::vector<Job> todo;
    std::error_code ec;
    for (const std::string& src : srcs) {
        fs::path obj = dir / fs::path(src).lexically_normal().replace_extension(".o");
        objects.push_back(obj.string());
        fs::create_directories(obj.parent_path(), ec);
        if (upToDate(src, obj, c)) say("  " + src + " is up to date");
        else todo.push_back({src, obj});
    }
    if (todo.empty()) return objects;
    if (jobs <= 0) jobs = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    jobs = std::min<int>(jobs, static_cast<int>(todo.size()));
    std::atomic<size_t> next{0};
    std::atomic<bool> failed{false};
    std::string failedSrc;
    int failedRc = 0;
    std::mutex failMutex;
    auto worker = [&]() {
        for (;;) {
            if (failed) return;
            size_t i = next++;
            if (i >= todo.size()) return;
            std::vector<std::string> cmd = cCompileLine(c, todo[i].src, todo[i].obj);
            say(show(cmd));
            int rc = spawnWait(cmd, c.env, "", false);
            if (rc != 0) {
                std::lock_guard<std::mutex> lock(failMutex);
                if (!failed) { failedSrc = todo[i].src; failedRc = rc; }
                failed = true;
                fs::remove(todo[i].obj, ec);   // a half-written object is not up to date
            }
        }
    };
    std::vector<std::thread> pool;
    for (int i = 0; i < jobs; ++i) pool.emplace_back(worker);
    for (auto& th : pool) th.join();
    if (failed) {
        if (failedRc == 127) refuse(file + ": compiling " + failedSrc + ": '" + c.compiler + "' could not be run (not on PATH?)");
        refuse(file + ": compiling " + failedSrc + " failed (exit " + std::to_string(failedRc) + ")");
    }
    return objects;
}

void buildOne(const std::string& file, int jobsArg) {
    say("building " + file);
    Config c = load(file);
    fetchPackages(c);                                           // §7.2, §7.3: before anything
    std::error_code ec;
    fs::path cwd = fs::current_path();
    fs::path dir = cwd / "out" / safeName(c.name);           // F68-83

    std::vector<std::string> cSrcs, aimlSrcs, givenObjs;
    for (const std::string& src : c.sources) {
        std::string ext = fs::path(src).extension().string();
        if (ext == ".o") givenObjs.push_back(fs::absolute(src).string());
        else if (isCSource(ext)) cSrcs.push_back(src);
        else aimlSrcs.push_back(src);
    }
    // F68-83/112: a pure aiml build empties its directory; with C/C++ sources the
    // directory stays (incremental), and the aiml objects get an emptied one of their own.
    fs::path aimlDir = cSrcs.empty() ? dir : dir / "aiml";
    if (cSrcs.empty()) fs::remove_all(dir, ec);
    else fs::remove_all(aimlDir, ec);
    fs::create_directories(dir, ec);
    if (ec) refuse("cannot make " + dir.string() + ": " + ec.message());

    const bool wasm = c.outputType == "wasm";
    std::string outName = c.name;                              // F68-84
    std::string bare = c.name.rfind("lib", 0) == 0 ? c.name.substr(3) : c.name;
    if (c.outputType == "shared_lib") outName = "lib" + bare + ".so";
    if (c.outputType == "static_lib") outName = "lib" + bare + ".a";   // F68-113
    if (wasm) outName = c.name + ".html";                      // F68-88: emcc writes .html, .js, .wasm
    if (wasm) checkEmsdk(c, file);
    fs::path output = (c.inCwd ? cwd : dir) / outName;

    auto hook = [&](const char* stage) {
        auto it = c.hooks.find(stage);
        if (it == c.hooks.end()) return;
        std::vector<std::string> env = c.env;
        env.push_back(std::string("AIML_BUILD_STAGE=") + stage);
        env.push_back("AIML_BUILD_CONFIG=" + c.file);
        env.push_back("AIML_BUILD_NAME=" + c.name);
        env.push_back("AIML_BUILD_DIR=" + dir.string());
        env.push_back("AIML_BUILD_OUTPUT=" + output.string());
        std::string aimlc = c.compiler;
        if (!cSrcs.empty()) aimlc = compilerFromEnv();         // a hook is aiml, whatever compiles C
        run({aimlc, it->second}, env, file + ": the " + std::string(stage) + " hook " + it->second);
    };

    hook("pre_compilation");
    std::vector<std::string> objects = givenObjs;
    if (!cSrcs.empty()) {
        std::vector<std::string> cObjs = compileC(c, cSrcs, dir, jobsArg > 0 ? jobsArg : c.jobs, file);
        objects.insert(objects.end(), cObjs.begin(), cObjs.end());
    }
    if (!aimlSrcs.empty()) {
        std::string aimlc = cSrcs.empty() ? c.compiler : compilerFromEnv();
        if (aimlc.empty()) refuse(file + ": .aiml sources need $AIML (spec/68 F68-21)");
        fs::create_directories(aimlDir, ec);
        for (const std::string& src : aimlSrcs) {
            fs::path obj = aimlDir / fs::path(src).lexically_normal().replace_extension(wasm ? ".ll" : ".o");
            fs::create_directories(obj.parent_path(), ec);
            std::vector<std::string> cmd = {aimlc, fs::absolute(src).string(), "-o", obj.string()};
            if (wasm) {                                        // F68-88, as tools/run_wasm_test.sh
                cmd.push_back("--target");
                cmd.push_back("wasm32");
                cmd.push_back("--emit-main");
            }
            if (!c.assets.empty()) {
                std::string roots = "--asset-roots=";
                for (size_t i = 0; i < c.assets.size(); ++i) roots += (i ? "," : "") + c.assets[i];
                cmd.push_back(roots);
            }
            cmd.insert(cmd.end(), c.includes.begin(), c.includes.end());
            cmd.insert(cmd.end(), c.defines.begin(), c.defines.end());
            run(cmd, c.env, file + ": compiling " + src);
        }
        // F68-83: the aiml directory was emptied, so every object in it is ours.
        // For wasm the compiler writes LLVM IR (.ll) — the module and its imports.
        const std::string objExt = wasm ? ".ll" : ".o";
        std::vector<std::string> built;
        for (const auto& ent : fs::recursive_directory_iterator(aimlDir, ec))
            if (ent.is_regular_file() && ent.path().extension() == objExt) built.push_back(ent.path().string());
        std::sort(built.begin(), built.end());
        objects.insert(objects.end(), built.begin(), built.end());
    }

    hook("pre_linkage");
    std::vector<std::string> cmd;
    if (c.outputType == "static_lib") {                        // F68-113
        fs::remove(output, ec);
        fs::create_directories(output.parent_path(), ec);
        cmd = {c.linker, "rcs", output.string()};
        cmd.insert(cmd.end(), objects.begin(), objects.end());
    } else {
        cmd = {c.linker};
        cmd.insert(cmd.end(), objects.begin(), objects.end());
        cmd.push_back("-o");
        cmd.push_back(output.string());
        if (c.outputType == "shared_lib") cmd.push_back("-shared");
        if (wasm)                                              // F68-88: assets go into the bundle
            for (const std::string& a : c.assets) cmd.push_back("--preload-file=" + a);
        cmd.insert(cmd.end(), c.linkerFlags.begin(), c.linkerFlags.end());
        if (!c.script.empty()) cmd.push_back("-T" + c.script);
        if (!c.mapFile.empty()) cmd.push_back("-Wl,-Map=" + c.mapFile);
        for (const std::string& p : c.libraryPaths) {
            cmd.push_back("-L" + p);
            // A wasm module has no RUNPATH: it loads nothing at run time.
            if (c.rpath && !wasm) {
                std::string entry = rpathEntry(p);
                if (!entry.empty()) cmd.push_back("-Wl,-rpath," + entry);
            }
        }
        for (const std::string& l : c.libraries) cmd.push_back("-l" + l);
    }
    run(cmd, c.env, file + ": linking " + outName);
    hook("post_linkage");
    say("built " + output.string());
}

bool isProjectFile(const fs::path& file) {
    try {
        YAML::Node d = YAML::LoadFile(file.string());
        return d.IsMap() && scalar(d["$schema"]).rfind(kSchema, 0) == 0;
    } catch (const std::exception&) {
        return false;
    }
}

int usage() {
    std::cerr << "usage: aiml build [-c <config.json>]... [--all] [-j <n>]   (spec/68 §7)" << std::endl;
    return 1;
}

}  // namespace

int command(int argc, char** argv) {
    std::vector<std::string> configs;
    bool all = false;
    int jobs = 0;
    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) configs.push_back(argv[++i]);
        else if (arg == "--all") all = true;
        else if ((arg == "-j" || arg == "--jobs") && i + 1 < argc) {   // F68-112
            try { jobs = std::stoi(argv[++i]); } catch (const std::exception&) { return usage(); }
        }
        else return usage();
    }
    if (all && !configs.empty()) return usage();
    if (all) {
        std::error_code ec;
        std::vector<std::string> found;
        for (const auto& ent : fs::directory_iterator(fs::current_path(), ec))
            if (ent.path().extension() == ".json" && isProjectFile(ent.path()))
                found.push_back(ent.path().filename().string());
        std::sort(found.begin(), found.end());
        if (found.empty()) {
            std::cerr << "aiml build: no project file (\"$schema\": \"" << kSchema
                      << "-1.0\") in " << fs::current_path().string() << std::endl;
            return 1;
        }
        configs = found;
    }
    if (configs.empty()) configs.push_back("exodus.json");
    for (const std::string& file : configs) {
        std::error_code ec;
        if (!fs::is_regular_file(file, ec)) {
            std::cerr << "aiml build: " << file << " is not there" << std::endl;
            return 1;
        }
        try {
            buildOne(file, jobs);
        } catch (const Failure& f) {
            std::cerr << "aiml build: " << f.what << std::endl;
            return 1;
        }
    }
    return 0;
}

}  // namespace build
}  // namespace aiml
