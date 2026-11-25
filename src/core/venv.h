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

}  // namespace venv
}  // namespace aiml
