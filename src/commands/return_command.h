#pragma once
#include "command.h"
#include "../core/expression.h"
#include "../core/context.h"

class ReturnCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        // Syntax: return: expr
        // or return: [expr] ?
        // functions.aiml uses: return: { +: [a, b] }  (Expression directly)
        // or return: [res] (List? No, just sequence with 1 arg?)
        // Let's support both: scalar/map (expression) or sequence (expression)
        
        auto retNode = node["return"];
        
        llvm::Value* retVal = nullptr;
        
        if (retNode.IsSequence()) {
             if (retNode.size() > 0)
                 retVal = Expression::evaluate(ctx, retNode[0]);
        } else {
             retVal = Expression::evaluate(ctx, retNode);
        }
        
        if (retVal) {
             ctx.builder.CreateRet(retVal);
        } else {
             ctx.builder.CreateRetVoid();
        }
    }
};
