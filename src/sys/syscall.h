#pragma once
#include "../core/context.h"
#include <yaml-cpp/yaml.h>
#include <llvm/IR/Value.h>

class SystemCall {
public:
    virtual ~SystemCall() = default;
    
    // Compile the system call
    // args includes the function name at index 0
    virtual llvm::Value* compile(CompilerContext& ctx, const std::vector<YAML::Node>& args) = 0;
};
