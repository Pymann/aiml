#pragma once
#include "context.h"
#include "utils.h"
#include "../../logger.h"
#include <yaml-cpp/yaml.h>
#include <llvm/IR/IRBuilder.h>
#include "call_dispatcher.h"
#include "string_utils.h"

namespace Expression {

    using OpHandler = std::function<llvm::Value*(CompilerContext&, const YAML::Node&)>;

    // Forward Declaration of evaluate for recursive usage
    inline llvm::Value* evaluate(CompilerContext& ctx, const YAML::Node& node);

    // --- Handler Implementations ---

    // Forward declarations for handlers
    llvm::Value* handleNew(CompilerContext& ctx, const YAML::Node& args);
    llvm::Value* handleCall(CompilerContext& ctx, const YAML::Node& args);
    llvm::Value* handleBinary(CompilerContext& ctx, const YAML::Node& args, const std::string& op);
    llvm::Value* handleUnary(CompilerContext& ctx, const YAML::Node& args, const std::string& op);
    llvm::Value* handleCast(CompilerContext& ctx, const YAML::Node& args);
    llvm::Value* handleFormat(CompilerContext& ctx, const YAML::Node& args);
    
    extern std::map<std::string, OpHandler> registry;
    
} // namespace Expression
