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
             llvm::Function* currentFunc = ctx.builder.GetInsertBlock()->getParent();
             llvm::Type* expectedType = currentFunc->getReturnType();

             if (expectedType != retVal->getType()) {
                 if (retVal->getType()->isPointerTy() && expectedType->isIntegerTy()) {
                      // String to Int (atoi)
                      llvm::Function* atoiFunc = ctx.module.getFunction("atoi");
                      if (!atoiFunc) {
                           llvm::FunctionType* ft = llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false);
                           atoiFunc = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "atoi", ctx.module);
                      }
                      llvm::Value* castVal = ctx.builder.CreateCall(atoiFunc, {retVal});
                      if (expectedType->isIntegerTy(64)) castVal = ctx.builder.CreateSExt(castVal, expectedType);
                      retVal = castVal;
                 } else if (retVal->getType()->isIntegerTy() && expectedType->isIntegerTy()) {
                      retVal = ctx.builder.CreateIntCast(retVal, expectedType, true);
                 } else if (retVal->getType()->isPointerTy() && expectedType->isPointerTy()) {
                      retVal = ctx.builder.CreateBitCast(retVal, expectedType);
                 } else {
                      // LOG_ERROR("Return type mismatch"); // Just warn or let LLVM catch it?
                      // Better to warn.
                      // LOG_ERROR("Return type mismatch: Expected " + ...);
                 }
             }

             ctx.builder.CreateRet(retVal);
        } else {
             ctx.builder.CreateRetVoid();
        }
    }
};
