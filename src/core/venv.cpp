// venv.cpp — see venv.h (spec/68).
#include "venv.h"

#include <dlfcn.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

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
    std::cerr << "usage: aiml venv <dir> [--runtime <libaiml.so>]" << std::endl;
    return 1;
}

int fail(const std::string& msg) {
    std::cerr << "aiml venv: " << msg << std::endl;
    return 1;
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

int command(int argc, char** argv) {
    std::string dirArg, runtimeArg;
    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--runtime" && i + 1 < argc) runtimeArg = argv[++i];
        else if (!arg.empty() && arg[0] != '-' && dirArg.empty()) dirArg = arg;
        else return usage();
    }
    if (dirArg.empty()) return usage();

    std::error_code ec;
    fs::path root = fs::weakly_canonical(fs::absolute(dirArg), ec);
    if (ec) return fail("cannot resolve " + dirArg + ": " + ec.message());
    if (root.string().find('\'') != std::string::npos)
        return fail("a venv path must not contain a single quote: " + root.string());
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
    fs::path table = root / "lib" / "aiml" / "aiml.packages.json";
    if (!fs::exists(table, ec) && !writeText(table, "{\"packages\": {}}\n", err)) return fail(err);
    if (!writeText(root / "env", envText(root.string()), err)) return fail(err);

    std::cout << "venv " << root.string() << "\n  use it with: . " << root.string() << "/env"
              << std::endl;
    return 0;
}

}  // namespace venv
}  // namespace aiml
