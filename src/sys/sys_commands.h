#pragma once
#include "syscall.h"
#include "../core/utils.h"

using Utils::resolveValue;

class SysGetArg : public SystemCall {
public:
    llvm::Value* compile(CompilerContext& ctx, const std::vector<YAML::Node>& args) override {
        // sys.get_arg(index) -> string
        // Access global __argv[index]
        
        if (args.size() < 2) return nullptr;
        
        llvm::Value* indexVal = resolveValue(ctx, args[1]); // i64 or i32
        
        // Ensure globals exist (created in Compiler::compile)
        llvm::GlobalVariable* argvVar = ctx.module.getGlobalVariable("__argv", true);
        llvm::GlobalVariable* argcVar = ctx.module.getGlobalVariable("__argc", true);
        
        if (!argvVar || !argcVar) {
             // Fallback or error?
             // If JIT/Compiler didn't create them, return empty string or null?
             LOG_ERROR("[SysGetArg] argv or argc not found!");
             return ctx.builder.CreateGlobalStringPtr("");
        }
        
        // Check argc/argv
        llvm::Value* argvPtr = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo()->getPointerTo(), argvVar);
        llvm::Value* argcVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), argcVar);
        
        // Safety Check: if argv is null (JIT) or index >= argc
        llvm::Value* argvIsNull = ctx.builder.CreateICmpEQ(argvPtr, llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()->getPointerTo()));
        
        llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* safeBB = llvm::BasicBlock::Create(ctx.context, "arg_safe", parentFunc);
        llvm::BasicBlock* errorBB = llvm::BasicBlock::Create(ctx.context, "arg_error", parentFunc);
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "arg_merge", parentFunc);
        
        llvm::Value* idxOk = ctx.builder.CreateICmpSLT(indexVal, argcVal); // index < argc
        llvm::Value* isSafe = ctx.builder.CreateAnd(ctx.builder.CreateNot(argvIsNull), idxOk);
        
        ctx.builder.CreateCondBr(isSafe, safeBB, errorBB);
        
        // Safe Block
        ctx.builder.SetInsertPoint(safeBB);
        llvm::Value* argStrPtr = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty()->getPointerTo(), argvPtr, indexVal);
        llvm::Value* argStr = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), argStrPtr);
        ctx.builder.CreateBr(mergeBB);
        
        // Error Block
        ctx.builder.SetInsertPoint(errorBB);
        llvm::Value* emptyStr = ctx.builder.CreateGlobalStringPtr("");
        ctx.builder.CreateBr(mergeBB);
        
        // Merge
        ctx.builder.SetInsertPoint(mergeBB);
        llvm::PHINode* result = ctx.builder.CreatePHI(ctx.builder.getInt8Ty()->getPointerTo(), 2);
        result->addIncoming(argStr, safeBB);
        result->addIncoming(emptyStr, errorBB);
        
        return result;
    }
};

class SysEnv : public SystemCall {
public:
    llvm::Value* compile(CompilerContext& ctx, const std::vector<YAML::Node>& args) override {
        // sys.env -> map<string, string>
        // TODO: Implement full map population.
        // For now, return null or empty map stub?
        // Or implement sys.getenv(key) if that's what's needed?
        // The plan says `sys.env`.
        // Leaving stub for now as map construction is complex without helper calls.
        return llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo());
    }
};

class SysSize : public SystemCall {
public:
    llvm::Value* compile(CompilerContext& ctx, const std::vector<YAML::Node>& args) override {
        // sys.size(obj or type) -> u64
        if (args.size() < 2) return nullptr;
        
        // 1. Check if argument is a direct type name (compile-time)
        if (args[1].IsScalar()) {
            std::string typeName = args[1].as<std::string>();
            llvm::Type* targetType = ctx.getLLVMType(typeName);
            if (targetType) {
                llvm::DataLayout dl(&ctx.module);
                uint64_t size = dl.getTypeAllocSize(targetType);
                return ctx.builder.getInt64(size);
            }
        }
        
        // 2. Check if nested call to sys.type
        if (args[1].IsMap() && args[1]["call"] && args[1]["call"].IsSequence()) {
             auto callNode = args[1]["call"];
             if (callNode.size() > 1 && callNode[0].as<std::string>() == "sys.type") {
                 std::string varName = callNode[1].as<std::string>();
                 if (ctx.symbol_table.count(varName)) {
                      llvm::Type* symType = ctx.symbol_table[varName].type;
                      llvm::DataLayout dl(&ctx.module);
                      uint64_t size = dl.getTypeAllocSize(symType);
                      return ctx.builder.getInt64(size);
                 }
             }
        }
        
        // 3. Fallback to runtime resolution
        llvm::Value* val = resolveValue(ctx, args[1]);
        if (!val) return ctx.builder.getInt64(0);
        
        llvm::Type* type = val->getType();
        llvm::DataLayout dl(&ctx.module);
        
        // Note: For pointers, this returns pointer size (8).
        uint64_t size = dl.getTypeAllocSize(type);
        return ctx.builder.getInt64(size);
    }
};

class SysType : public SystemCall {
public:
    llvm::Value* compile(CompilerContext& ctx, const std::vector<YAML::Node>& args) override {
         // sys.type(obj) -> string name? or ID?
         // Stub for now.
         return ctx.builder.CreateGlobalStringPtr("unknown");
    }
};
