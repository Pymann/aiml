// package_table.h — `@name/path` imports (spec/21 §3.1).
//
// A package import names a package instead of a place:
//
//   - import: "@ufo/lib/registry.aiml"
//
// The package table says where `ufo` lives.  exodus writes it
// (`exodus pkg install`): `aiml.packages.json` in the project root,
//
//   { "packages": { "ufo": { "path": "__exodus_cache/deps/ufo/<sha256>",
//                            "version": "0.2.0", "sha256": "<hex>" } } }
//
// `path` may be relative — to the directory of the table.  Which table
// applies, in this order:
//   1. the one a host set explicitly (ctx: `kontext.use_packages(path)`)
//   2. $AIML_PACKAGES
//   3. aiml.packages.json in the working directory, then upward from the
//      importing file's directory (a build started elsewhere still finds it)
//   4. the table of the venv the compiler belongs to (spec/68 F68-30)
//
// A resolved path never leaves the package directory ("@ufo/../x" is an
// error), so a package import reaches exactly what was installed.
#pragma once
#include <string>

namespace aiml {
namespace packages {

// True for "@name/…".
bool isPackageImport(const std::string& raw);

// Resolves "@name/rest" to a file path.  `importer` is the importing file
// ("" or "<memory>" for in-memory source), `explicitTable` the host's choice
// ("" = none).  On failure returns false and sets `err` to a message that
// says what to do.
bool resolve(const std::string& raw, const std::string& importer,
             const std::string& explicitTable, std::string& out, std::string& err);

}  // namespace packages
}  // namespace aiml
