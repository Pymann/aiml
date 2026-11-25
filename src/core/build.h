// build.h — `aiml build` (spec/68 §7): an aiml project built from its exodus
// project file by the compiler itself — compile, link, hooks run by the JIT.
// No exodus and no Python: what a venv needs to build a tool is itself and a
// linker.
#pragma once

namespace aiml {
namespace build {

// `aiml build [-c <config.json>]… [--all]` — args after "build".  Prints each
// command before it runs; returns the process exit code.
int command(int argc, char** argv);

}  // namespace build
}  // namespace aiml
