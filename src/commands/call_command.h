#pragma once
#include "command.h"
#include "../core/context.h"
#include "../core/expression.h"
#include "../core/call_dispatcher.h"
#include "../../logger.h"
#include <vector>

class CallCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        // Syntax: call: [func_name, arg1, arg2, ...]
        auto argsNode = node["call"];
        std::string funcName = argsNode[0].as<std::string>();
        
        LOG_DEBUG("Compiling call to " + funcName);
        
        // Convert YAML args to vector for CallDispatcher interface
        std::vector<YAML::Node> args;
        for (const auto& arg : argsNode) {
            args.push_back(arg);
        }

        // Use CallDispatcher
        CallDispatcher::dispatch(ctx, funcName, args, [](CompilerContext& c, const YAML::Node& n) {
            return Expression::evaluate(c, n);
        });
    }
};
