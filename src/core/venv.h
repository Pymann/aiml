// venv.h — aiml environments (spec/69).
//
//   <venv>/bin/aiml  lib/libaiml.so  lib/aiml/aiml.packages.json  env
//
// `aiml venv <dir>` creates or updates one; the compiler reads the package
// table of the venv it belongs to as the last choice (spec/69 F69-30).
#pragma once
#include <string>

namespace aiml {
namespace venv {

// The marker on the first line of <venv>/env (F69-13, F69-20).
extern const char* const kMarker;

// The venv that contains this compiler image (aiml binary or libaiml.so),
// "" when it is in none (F69-31).
std::string ownRoot();

// `aiml venv <dir> [--runtime <libaiml.so>]` — args after "venv".  Prints
// what it did or why not; returns the process exit code.
int command(int argc, char** argv);

}  // namespace venv
}  // namespace aiml
