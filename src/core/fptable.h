#pragma once
#include "context.h"

// Maximum number of hot-reloadable versions per function.
// Defined outside AIML namespace so exec_impl.cpp can include this without pulling in AIML::.
static constexpr int AIML_MAX_VERSIONS = 16;

namespace AIML {

// Emits the global fptable variables into the module.
// Must be called after all visitFunction() calls (at the end of compile()).
//
// Emitted globals:
//   @__aiml_fptable         [N x [16 x i8*]]  — all versions per function
//   @__aiml_fptable_current [N x i8*]          — pointer to latest version (fast path)
//   @__aiml_fptable_versions[N x i32]          — current version number per function
//   @__aiml_func_names      [N x i8*]          — function name strings
//   @__aiml_func_count      i32                — N (constant)
void emitFptable(CompilerContext& ctx);

} // namespace AIML
