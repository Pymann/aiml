// package_table.cpp — see package_table.h (spec/21 §3.1).
#include "package_table.h"

#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace aiml {
namespace packages {

namespace {

struct Table {
    bool loaded = false;
    fs::file_time_type mtime{};
    std::string file;                          // where it came from
    std::map<std::string, std::string> dirs;   // name → absolute directory
    std::string error;
};

std::mutex g_mutex;
std::map<std::string, Table> g_cache;          // table file → parsed

// Cached per file and reread when it changed: a long-running host (cosmosd)
// sees what a later `exodus deps install` wrote.
const Table& load(const std::string& file) {
    std::error_code ec;
    fs::file_time_type now = fs::last_write_time(file, ec);
    auto it = g_cache.find(file);
    if (it != g_cache.end() && !ec && it->second.mtime == now) return it->second;
    if (it != g_cache.end()) g_cache.erase(it);
    Table t;
    t.file = file;
    t.mtime = now;
    try {
        // JSON is YAML; yaml-cpp reads the table as it is.
        YAML::Node root = YAML::LoadFile(file);
        YAML::Node pkgs = root["packages"];
        if (pkgs && pkgs.IsMap()) {
            fs::path base = fs::path(file).parent_path();
            for (const auto& kv : pkgs) {
                std::string name = kv.first.as<std::string>();
                std::string dir;
                if (kv.second.IsMap() && kv.second["path"]) dir = kv.second["path"].as<std::string>();
                else if (kv.second.IsScalar()) dir = kv.second.as<std::string>();
                if (dir.empty()) continue;
                fs::path p(dir);
                if (p.is_relative()) p = base / p;
                std::error_code ec;
                fs::path canon = fs::weakly_canonical(p, ec);
                t.dirs[name] = (ec ? p : canon).string();
            }
        }
        t.loaded = true;
    } catch (const std::exception& e) {
        t.error = file + ": " + e.what();
    }
    return g_cache.emplace(file, std::move(t)).first->second;
}

// The table that applies (see header); "" when there is none.
std::string findTable(const std::string& importer, const std::string& explicitTable) {
    if (!explicitTable.empty()) return explicitTable;
    if (const char* env = std::getenv("AIML_PACKAGES")) {
        if (*env) return env;
    }
    std::error_code ec;
    fs::path cwdTable = fs::current_path(ec) / "aiml.packages.json";
    if (!ec && fs::exists(cwdTable)) return cwdTable.string();
    if (!importer.empty() && importer != "<memory>") {
        fs::path cur = fs::absolute(fs::path(importer), ec).parent_path();
        while (!cur.empty()) {
            fs::path cand = cur / "aiml.packages.json";
            if (fs::exists(cand)) return cand.string();
            if (cur == cur.root_path()) break;
            cur = cur.parent_path();
        }
    }
    return "";
}

}  // namespace

bool isPackageImport(const std::string& raw) {
    return raw.size() > 1 && raw[0] == '@';
}

bool resolve(const std::string& raw, const std::string& importer,
             const std::string& explicitTable, std::string& out, std::string& err) {
    std::string spec = raw.substr(1);
    size_t slash = spec.find('/');
    std::string name = slash == std::string::npos ? spec : spec.substr(0, slash);
    std::string rest = slash == std::string::npos ? "" : spec.substr(slash + 1);
    if (name.empty() || rest.empty()) {
        err = "package import '" + raw + "' must name a package and a file: @<package>/<path>";
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string file = findTable(importer, explicitTable);
    if (file.empty()) {
        err = "package '" + name + "' is not installed (no aiml.packages.json; run exodus deps install or set AIML_PACKAGES)";
        return false;
    }
    const Table& t = load(file);
    if (!t.error.empty()) {
        err = "package table " + t.error;
        return false;
    }
    auto it = t.dirs.find(name);
    if (it == t.dirs.end()) {
        err = "package '" + name + "' is not in " + file + " (add it to dependencies and run exodus deps install)";
        return false;
    }
    std::error_code ec;
    fs::path dir(it->second);
    fs::path cand = fs::weakly_canonical(dir / rest, ec);
    if (ec) cand = dir / rest;
    // Must stay inside the package: "@ufo/../secret" is not a package file.
    auto rel = cand.lexically_relative(dir);
    if (rel.empty() || *rel.begin() == "..") {
        err = "package import '" + raw + "' leaves the package directory";
        return false;
    }
    if (!fs::exists(cand)) {
        err = "package '" + name + "' has no file '" + rest + "' (" + dir.string() + ")";
        return false;
    }
    out = cand.string();
    return true;
}

}  // namespace packages
}  // namespace aiml
