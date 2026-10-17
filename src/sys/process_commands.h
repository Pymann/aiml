#pragma once
#include "syscall.h"
#include "../core/utils.h"

using Utils::resolveValue;
using Utils::resolveLValue;

class SysExe : public SystemCall {
public:
    llvm::Value* compile(CompilerContext& ctx, const std::vector<YAML::Node>& args) override {
        // sys.exe(argv: list<string>, options: map/model)
        // Signature to runtime: void* sys_exe_runtime(void* argvList, char* cwd, void* env, bool shell, int32_t timeout, bool capture);
        
        if (args.size() < 2) return nullptr;
        
        llvm::Value* argvList = nullptr;
        std::string listArg = args[1].as<std::string>();
        
        // Try to resolve as LValue (variable address) first to avoid copy
        if (ctx.symbol_table.count(listArg)) {
            llvm::Value* lval = resolveLValue(ctx, listArg);
            if (lval) {
                argvList = ctx.builder.CreateBitCast(lval, ctx.builder.getInt8Ty()->getPointerTo());
            }
        }
        
        if (!argvList) {
            // Fallback: Resolve as value (expression)
            llvm::Value* val = resolveValue(ctx, args[1]);
            
            if (val->getType()->isStructTy()) {
                // Runtime expects pointer to struct, so store temp
                llvm::AllocaInst* temp = ctx.builder.CreateAlloca(val->getType());
                ctx.builder.CreateStore(val, temp);
                argvList = ctx.builder.CreateBitCast(temp, ctx.builder.getInt8Ty()->getPointerTo());
            } else if (val->getType()->isPointerTy()) {
                argvList = ctx.builder.CreateBitCast(val, ctx.builder.getInt8Ty()->getPointerTo());
            } else {
                LOG_ERROR("Sys.exe: Invalid argument type for argv list");
                return nullptr;
            }
        }
        
        // Defaults
        llvm::Value* cwd = llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo());
        llvm::Value* env = llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()); // TODO: Map support
        llvm::Value* shell = ctx.builder.getInt1(false);
        llvm::Value* timeout = ctx.builder.getInt32(-1);
        llvm::Value* capture = ctx.builder.getInt1(false);
        
        // Parse Options if present (args[2])
        if (args.size() > 2) {
             // If it's a map or model... runtime resolution of struct fields is hard without helpers.
             // For now, let's assume it's ignored or basic support.
             // TODO: Extract fields from Map/Model.
             // This requires `ctx.helpers.getMapValue` or similar which might not be ready.
             // I will stick to defaults for V1 or allow basic overrides if I can parse them easily.
        }
        
        // Declare runtime function
        llvm::Function* runtimeFunc = ctx.module().getFunction("sys_exe_runtime");
        if (!runtimeFunc) {
            std::vector<llvm::Type*> runArgs = { 
                ctx.builder.getInt8Ty()->getPointerTo(), // list
                ctx.builder.getInt8Ty()->getPointerTo(), // cwd
                ctx.builder.getInt8Ty()->getPointerTo(), // env
                ctx.builder.getInt1Ty(), // shell
                ctx.builder.getInt32Ty(), // timeout
                ctx.builder.getInt1Ty()  // capture
            };
            llvm::FunctionType* runType = llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), runArgs, false);
            runtimeFunc = llvm::Function::Create(runType, llvm::Function::ExternalLinkage, "sys_exe_runtime", ctx.module());
        }
        
        return ctx.builder.CreateCall(runtimeFunc, {argvList, cwd, env, shell, timeout, capture});
    }
};
