// venv.h — aiml environments (spec/68).
//
//   <venv>/bin/aiml  lib/libaiml.so  lib/aiml/aiml.packages.json  env
//
// `aiml venv <dir>` creates or updates one; the compiler reads the package
// table of the venv it belongs to as the last choice (spec/68 F68-30).
//
// `aiml venv <dir> install <name> (--git <url> [--ref <r>] | --path <dir>
//                                  | --kontor <url> [--version <v>])`
// fetches a tool, builds it WITH THIS VENV and places its results (§6);
// `install --all --kontor <url>` takes every tool that kontor holds (§6.1).
// `publish [<name>] [--path <dir>] --kontor <url>` puts a tool's sources into
// kontor, where `install --kontor` finds them (§6.3).
// `list` and `remove` work off the record the install leaves behind.
#pragma once
#include <string>
#include <vector>

namespace aiml {
namespace venv {

// The marker on the first line of <venv>/env (F68-13, F68-20).
extern const char* const kMarker;

// The venv that contains this compiler image (aiml binary or libaiml.so),
// "" when it is in none (F68-31).
std::string ownRoot();

// `aiml venv <dir> [--runtime <libaiml.so>]` — args after "venv".  Prints
// what it did or why not; returns the process exit code.
int command(int argc, char** argv);

// Run a program without a shell (argv as given), in `cwd` when not empty,
// with `env` ("K=V") added to this process's environment.  `out` captures
// stdout when given.  Returns the exit code, 127 when it could not be started.
// Shared with `aiml build` (spec/68 §7, F68-86).
int runProgram(const std::vector<std::string>& args, const std::string& cwd,
               const std::vector<std::string>& env, std::string* out);

// GET `url` (redirects, proxy from the environment, TLS) into `toFile`; false with
// `err` set unless the answer was 200.  Shared with `aiml build`'s apt fetch
// (spec/68 F68-100a).
bool download(const std::string& url, const std::string& toFile, std::string& err);

// The sha256 of a file as 64 hex digits, "" when it cannot be read.
std::string sha256File(const std::string& path);

}  // namespace venv
}  // namespace aiml
