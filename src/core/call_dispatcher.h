#pragma once
#include "context.h"
#include "../sys/registry.h"
#include "list_impl.h"
#include "map_impl.h"
#include "buffer_impl.h"
#include <vector>
#include <string>
#include <functional>
#include <yaml-cpp/yaml.h>
#include "../../logger.h"

class CallDispatcher {
public:
    using Evaluator = std::function<llvm::Value*(CompilerContext&, const YAML::Node&)>;

    static llvm::Value* dispatch(CompilerContext& ctx, const std::string& funcName, const std::vector<YAML::Node>& args, Evaluator evaluator, llvm::BasicBlock* exceptionBlock = nullptr);

};
