#pragma once
#include "syscall.h"
#include "../core/utils.h"
#include "../../logger.h"
#include <vector>

using Utils::resolveValue;


class MemoryFree : public SystemCall {
public:
    llvm::Value* compile(CompilerContext& ctx, const std::vector<YAML::Node>& args) override {
        // args[0] = "free", args[1] = ptr
        if (args.size() < 2) return nullptr;

        llvm::Value* ptrVal = resolveValue(ctx, args[1]);

        // Declare free if not exists: void free(i8*)
        llvm::Function* freeFunc = ctx.module().getFunction("free");
        if (!freeFunc) {
            std::vector<llvm::Type*> freeArgs = { ctx.builder.getInt8Ty()->getPointerTo() }; // i8*
            llvm::FunctionType* freeType = llvm::FunctionType::get(ctx.builder.getVoidTy(), freeArgs, false);
            freeFunc = llvm::Function::Create(freeType, llvm::Function::ExternalLinkage, "free", ctx.module());
        }
        
        return ctx.builder.CreateCall(freeFunc, {ptrVal});
    }
};
