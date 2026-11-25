// build.cpp — see build.h (spec/68 §7).
#include "build.h"
#include "venv.h"

#include <glob.h>
#include <unistd.h>

#include <cctype>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace aiml {
namespace build {

namespace {

const char* const kSchema = "exodus.project.config";   // F68-80: "…-1.0"

struct Failure {
    std::string what;
};

[[noreturn]] void refuse(const std::string& what) { throw Failure{what}; }

std::string scalar(const YAML::Node& n, const std::string& dflt = "") {
    return n && n.IsScalar() ? n.as<std::string>() : dflt;
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

// F68-85: $AIML_VENV first (it starts with $AIML), nothing else; $ORIGIN stays.
std::string expand(std::string s) {
    size_t at;
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

// F68-82: anything that reaches into exodus' cache is exodus' business.
bool mentionsExodusCache(const YAML::Node& n) {
    if (!n) return false;
    if (n.IsScalar()) return n.as<std::string>().find("__exodus_cache") != std::string::npos;
    if (n.IsSequence())
        for (const auto& item : n)
            if (mentionsExodusCache(item)) return true;
    if (n.IsMap())
        for (const auto& kv : n)
            if (mentionsExodusCache(kv.second)) return true;
    return false;
}

bool nonEmpty(const YAML::Node& n) {
    return n && !n.IsNull() && ((n.IsSequence() || n.IsMap()) ? n.size() > 0 : true);
}

std::string show(const std::vector<std::string>& cmd) {
    std::string line = "  $";
    for (const std::string& a : cmd) line += " " + a;
    return line;
}

// Sources are paths, or glob patterns as exodus took them (relative to the
// working directory, F68-80).  A pattern that matches nothing is an error:
// exodus built nothing for it and said 0.
std::vector<std::string> resolveSources(const std::vector<std::string>& patterns) {
    std::vector<std::string> out;
    for (const std::string& p : patterns) {
        if (p.find_first_of("*?[") == std::string::npos) {
            std::error_code ec;
            if (!fs::is_regular_file(p, ec)) refuse("the source '" + p + "' is not there");
            out.push_back(p);
            continue;
        }
        glob_t g{};
        if (::glob(p.c_str(), 0, nullptr, &g) == 0)
            for (size_t i = 0; i < g.gl_pathc; ++i) out.push_back(g.gl_pathv[i]);
        globfree(&g);
        if (out.empty()) refuse("the source pattern '" + p + "' matches nothing");
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

struct Config {
    std::string file, name, outputType;
    std::vector<std::string> sources, linkerFlags, libraries, libraryPaths, assets;
    std::vector<std::string> includes, defines;
    std::string compiler, linker;
    bool rpath = true, inCwd = false;
    std::vector<std::string> env;   // K=V, expanded
    std::map<std::string, std::string> hooks;   // stage → .aiml file
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

    // F68-82: what is not a tool's build, refused by name.
    const std::string needs = " — that needs exodus, `aiml build` builds aiml tools (spec/68 F68-82)";
    c.outputType = scalar(d["output_type"], "executable");
    if (c.outputType != "executable" && c.outputType != "shared_lib")
        refuse(file + ": output_type '" + c.outputType + "'" + needs);
    if (nonEmpty(d["apt_packages"])) refuse(file + ": apt_packages" + needs);
    if (nonEmpty(d["git_packages"])) refuse(file + ": git_packages" + needs);
    if (mentionsExodusCache(d)) refuse(file + ": a path into __exodus_cache" + needs);
    if (d["linker"] && nonEmpty(d["linker"]["script"])) refuse(file + ": linker.script" + needs);

    for (const std::string& src : resolveSources(strings(d["sources"]))) {
        std::string ext = fs::path(src).extension().string();
        if (ext != ".aiml" && ext != ".o") refuse(file + ": the source '" + src + "'" + needs);
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
    c.compiler = expand(scalar(comp ? comp["name"] : YAML::Node(), "$AIML"));
    c.linker = expand(scalar(link ? link["name"] : YAML::Node(), "cc"));
    if (link) {
        for (const std::string& f : strings(link["flags"])) c.linkerFlags.push_back(expand(f));
        c.libraries = strings(link["libraries"]);
        for (const std::string& p : strings(link["library_paths"])) c.libraryPaths.push_back(expand(p));
        if (link["rpath"] && link["rpath"].IsScalar()) c.rpath = link["rpath"].as<bool>();
    }
    for (const std::string& p : strings(d["asset_directories"])) c.assets.push_back(expand(p));
    for (const std::string& p : strings(d["search_paths"])) c.includes.push_back("-I" + expand(p));
    if (d["defines"] && d["defines"].IsMap())
        for (const auto& kv : d["defines"]) {
            std::string k = kv.first.as<std::string>(), v = scalar(kv.second);
            c.defines.push_back(v.empty() ? "-D" + k : "-D" + k + "=" + v);
        }
    if (d["artifact_in_cwd"] && d["artifact_in_cwd"].IsScalar()) c.inCwd = d["artifact_in_cwd"].as<bool>();
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

void run(const std::vector<std::string>& cmd, const std::vector<std::string>& env,
         const std::string& what) {
    std::cout << show(cmd) << std::endl;
    int rc = venv::runProgram(cmd, "", env, nullptr);
    if (rc == 127) refuse(what + ": '" + cmd[0] + "' could not be run (not on PATH?)");
    if (rc != 0) refuse(what + " failed (exit " + std::to_string(rc) + ")");
}

void buildOne(const std::string& file) {
    std::cout << "building " << file << std::endl;
    Config c = load(file);
    std::error_code ec;
    fs::path cwd = fs::current_path();
    fs::path dir = cwd / "out" / safeName(c.name);           // F68-83
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    if (ec) refuse("cannot make " + dir.string() + ": " + ec.message());

    std::string outName = c.name;                              // F68-84
    if (c.outputType == "shared_lib") outName = (c.name.rfind("lib", 0) == 0 ? "" : "lib") + c.name + ".so";
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
        run({c.compiler, it->second}, env, file + ": the " + std::string(stage) + " hook " + it->second);
    };

    hook("pre_compilation");
    std::vector<std::string> objects;
    for (const std::string& src : c.sources) {
        if (fs::path(src).extension() == ".o") { objects.push_back(fs::absolute(src).string()); continue; }
        fs::path obj = dir / fs::path(src).lexically_normal().replace_extension(".o");
        fs::create_directories(obj.parent_path(), ec);
        std::vector<std::string> cmd = {c.compiler, fs::absolute(src).string(), "-o", obj.string()};
        if (!c.assets.empty()) {
            std::string roots = "--asset-roots=";
            for (size_t i = 0; i < c.assets.size(); ++i) roots += (i ? "," : "") + c.assets[i];
            cmd.push_back(roots);
        }
        cmd.insert(cmd.end(), c.includes.begin(), c.includes.end());
        cmd.insert(cmd.end(), c.defines.begin(), c.defines.end());
        run(cmd, c.env, file + ": compiling " + src);
    }
    // F68-83: the build directory was emptied, so every object in it is ours.
    std::vector<std::string> built;
    for (const auto& ent : fs::recursive_directory_iterator(dir, ec))
        if (ent.is_regular_file() && ent.path().extension() == ".o") built.push_back(ent.path().string());
    std::sort(built.begin(), built.end());
    objects.insert(objects.end(), built.begin(), built.end());

    hook("pre_linkage");
    std::vector<std::string> cmd = {c.linker};
    cmd.insert(cmd.end(), objects.begin(), objects.end());
    cmd.push_back("-o");
    cmd.push_back(output.string());
    if (c.outputType == "shared_lib") cmd.push_back("-shared");
    cmd.insert(cmd.end(), c.linkerFlags.begin(), c.linkerFlags.end());
    for (const std::string& p : c.libraryPaths) {
        cmd.push_back("-L" + p);
        if (c.rpath && p.find(':') == std::string::npos) cmd.push_back("-Wl,-rpath," + p);
    }
    for (const std::string& l : c.libraries) cmd.push_back("-l" + l);
    run(cmd, c.env, file + ": linking " + outName);
    hook("post_linkage");
    std::cout << "built " << output.string() << std::endl;
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
    std::cerr << "usage: aiml build [-c <config.json>]... [--all]   (spec/68 §7)" << std::endl;
    return 1;
}

}  // namespace

int command(int argc, char** argv) {
    std::vector<std::string> configs;
    bool all = false;
    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) configs.push_back(argv[++i]);
        else if (arg == "--all") all = true;
        else if (arg == "-j" && i + 1 < argc) ++i;            // exodus' option, accepted
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
            buildOne(file);
        } catch (const Failure& f) {
            std::cerr << "aiml build: " << f.what << std::endl;
            return 1;
        }
    }
    return 0;
}

}  // namespace build
}  // namespace aiml
