#pragma once
#include "syscall.h"
#include "../core/utils.h"

using Utils::resolveValue;

class TimeNow : public SystemCall {
public:
    llvm::Value* compile(CompilerContext& ctx, const std::vector<YAML::Node>& args) override {
        // time.now() -> i64 (milliseconds since epoch)
        // Delegate to runtime function: int64_t time_now_ms()
        
        llvm::Function* timeFunc = ctx.module().getFunction("time_now_ms");
        if (!timeFunc) {
            llvm::FunctionType* funcType = llvm::FunctionType::get(ctx.builder.getInt64Ty(), false);
            timeFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "time_now_ms", ctx.module());
        }
        
        return ctx.builder.CreateCall(timeFunc);
    }
};

class TimeSleepMs : public SystemCall {
public:
    llvm::Value* compile(CompilerContext& ctx, const std::vector<YAML::Node>& args) override {
         // time.sleep_ms(ms)
         // Delegate to runtime function: void time_sleep_ms(int32_t ms)
         
         llvm::Function* sleepFunc = ctx.module().getFunction("time_sleep_ms");
         if (!sleepFunc) {
            std::vector<llvm::Type*> args = { ctx.builder.getInt32Ty() };
            llvm::FunctionType* funcType = llvm::FunctionType::get(ctx.builder.getVoidTy(), args, false);
            sleepFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "time_sleep_ms", ctx.module());
         }
         
         if (args.size() > 1) {
             llvm::Value* msVal = resolveValue(ctx, args[1]);
             if (msVal->getType()->isIntegerTy(64)) {
                 msVal = ctx.builder.CreateTrunc(msVal, ctx.builder.getInt32Ty());
             }
             return ctx.builder.CreateCall(sleepFunc, { msVal });
         }
         return nullptr;
    }
};
