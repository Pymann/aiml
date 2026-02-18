#pragma once
#include "command.h"
#include "../core/context.h"
#include "../core/expression.h"

class FreeCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        // Syntax: - free: [ptr]
        // or - free: ptr
        
        const YAML::Node& args = node["free"];
        llvm::Value* ptrVal = nullptr;

        if (args.IsSequence() && args.size() > 0) {
             ptrVal = Expression::evaluate(ctx, args[0]);
        } else {
             ptrVal = Expression::evaluate(ctx, args);
        }

        if (ptrVal) {
             // Generate free(ptr)
             std::vector<llvm::Type*> freeArgs;
             freeArgs.push_back(ctx.builder.getInt8Ty()->getPointerTo());
             
             llvm::FunctionType* freeType = llvm::FunctionType::get(ctx.builder.getVoidTy(), freeArgs, false);
             llvm::FunctionCallee freeFunc = ctx.module.getOrInsertFunction("free", freeType);
             
             // Cast to i8* if needed (opaque pointers handled by LLVM usually, but explicit cast safe)
             llvm::Value* voidPtr = ctx.builder.CreateBitCast(ptrVal, ctx.builder.getInt8Ty()->getPointerTo());
             ctx.builder.CreateCall(freeFunc, voidPtr);
        }
    }
};
