#pragma once
#include <yaml-cpp/yaml.h>
#include "../core/context.h"

class AIMLCommand {
public:
    virtual ~AIMLCommand() = default;
    
    // Compile this command (node is the full command node, e.g. { set: ... })
    virtual void compile(CompilerContext& ctx, const YAML::Node& node) = 0;
};
