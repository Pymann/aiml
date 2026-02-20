#pragma once
#include "context.h"
#include <yaml-cpp/yaml.h>
#include <llvm/IR/Value.h>
#include <string>

#include "string_utils.h"
#include "list_impl.h"
#include "map_impl.h"

namespace Utils {



// Forward declarations
llvm::Value* resolveValue(CompilerContext& ctx, const YAML::Node& argNode, CompilerContext::Evaluator evaluator = nullptr);
llvm::Value* resolveLValue(CompilerContext& ctx, const std::string& argStr, CompilerContext::Evaluator evaluator = nullptr, bool forWrite = false);

} // namespace Utils
