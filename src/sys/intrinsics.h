#pragma once
#include "syscall.h"
#include "../core/utils.h"
#include "../../logger.h"
#include <vector>

using Utils::resolveValue;

class SysExit : public SystemCall {
public:
    llvm::Value* compile(CompilerContext& ctx, const std::vector<YAML::Node>& args) override {
        llvm::Function* exitFunc = ctx.module.getFunction("exit");
        if (!exitFunc) {
            std::vector<llvm::Type*> exitArgs = { ctx.builder.getInt32Ty() };
            llvm::FunctionType* exitType = llvm::FunctionType::get(ctx.builder.getVoidTy(), exitArgs, false);
            exitFunc = llvm::Function::Create(exitType, llvm::Function::ExternalLinkage, "exit", ctx.module);
        }

        llvm::Value* code = ctx.builder.getInt32(0);
        if (args.size() > 1) {
             code = resolveValue(ctx, args[1]);
        }
        return ctx.builder.CreateCall(exitFunc, {code});
    }
};




class MemoryFree : public SystemCall {
public:
    llvm::Value* compile(CompilerContext& ctx, const std::vector<YAML::Node>& args) override {
        // args[0] = "free", args[1] = ptr
        if (args.size() < 2) return nullptr;

        llvm::Value* ptrVal = resolveValue(ctx, args[1]);

        // Declare free if not exists: void free(i8*)
        llvm::Function* freeFunc = ctx.module.getFunction("free");
        if (!freeFunc) {
            std::vector<llvm::Type*> freeArgs = { ctx.builder.getInt8Ty()->getPointerTo() }; // i8*
            llvm::FunctionType* freeType = llvm::FunctionType::get(ctx.builder.getVoidTy(), freeArgs, false);
            freeFunc = llvm::Function::Create(freeType, llvm::Function::ExternalLinkage, "free", ctx.module);
        }
        
        return ctx.builder.CreateCall(freeFunc, {ptrVal});
    }
};
