#pragma once
#include "command.h"
#include "../core/expression.h"
#include "../core/context.h"

class WhileCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        // Syntax: 
        // - while:
        //     condition: { lt: [x, 10] }
        //     body:
        //       - print: ["Looping..."]
        //       - set: [x, {+: [x, 1]}]
        
        auto whileNode = node["while"];
        
        if (!whileNode["condition"]) {
            LOG_ERROR("While statement missing 'condition'");
            return;
        }
        
        llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
        
        llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(ctx.context, "while_loop", parentFunc);
        llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(ctx.context, "while_body");
        llvm::BasicBlock* endBB = llvm::BasicBlock::Create(ctx.context, "while_end");
        
        // Jump to Loop Check
        ctx.builder.CreateBr(loopBB);
        
        // Loop Check Block
        ctx.builder.SetInsertPoint(loopBB);
        llvm::Value* condVal = Expression::evaluate(ctx, whileNode["condition"]);
        
        if (!condVal) {
             LOG_ERROR("While condition evaluated to null");
             return; // Or jump to end?
        }
        
        // Ensure i1
        if (condVal->getType() != ctx.builder.getInt1Ty()) {
            condVal = ctx.builder.CreateICmpNE(condVal, llvm::Constant::getNullValue(condVal->getType()), "while_cond_bool");
        }
        
        ctx.builder.CreateCondBr(condVal, bodyBB, endBB);
        
        // Body Block
        parentFunc->insert(parentFunc->end(), bodyBB);
        ctx.builder.SetInsertPoint(bodyBB);
        
        if (whileNode["body"] && whileNode["body"].IsSequence()) {
             if (ctx.params.compileCallback) {
                  ctx.params.compileCallback(ctx, whileNode["body"]);
             }
        }
        
        // Jump back to check
        ctx.builder.CreateBr(loopBB);
        
        // End Block
        parentFunc->insert(parentFunc->end(), endBB);
        ctx.builder.SetInsertPoint(endBB);
    }
};
