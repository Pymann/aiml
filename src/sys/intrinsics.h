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

class IoWrite : public SystemCall {
public:
    llvm::Value* compile(CompilerContext& ctx, const std::vector<YAML::Node>& args) override {
        if (args.size() < 3) return nullptr;

        // args[1] = handle ("io.stdout" -> 1, "io.stderr" -> 2)
        // args[2] = message (string)
        
        // 1. Resolve Handle (File Descriptor)
        llvm::Value* fdVal = nullptr;
        std::string handleStr = args[1].as<std::string>();
        if (handleStr == "io.stdout") fdVal = ctx.builder.getInt32(1);
        else if (handleStr == "io.stderr") fdVal = ctx.builder.getInt32(2);
        else fdVal = resolveValue(ctx, args[1]); // Custom handle?

        // 2. Resolve or Create 'write' function: ssize_t write(int fd, const void *buf, size_t count);
        // We use i32 for fd, i8* for buf, i64 for count (size_t). Return is i64 (ssize_t).
        llvm::Function* writeFunc = ctx.module.getFunction("write");
        if (!writeFunc) {
            std::vector<llvm::Type*> writeArgs = { 
                ctx.builder.getInt32Ty(), 
                ctx.builder.getInt8Ty()->getPointerTo(),
                ctx.builder.getInt64Ty() 
            };
            llvm::FunctionType* writeType = llvm::FunctionType::get(ctx.builder.getInt64Ty(), writeArgs, false);
            writeFunc = llvm::Function::Create(writeType, llvm::Function::ExternalLinkage, "write", ctx.module);
        }

        // 3. Resolve Message
        llvm::Value* msgVal = resolveValue(ctx, args[2]);
        
        // 4. Calculate Length Manually (Loop until \0)
        // Basic Blocks
        llvm::Function* func = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* preLoopBlock = ctx.builder.GetInsertBlock();
        llvm::BasicBlock* checkBlock = llvm::BasicBlock::Create(ctx.context, "strlen_check", func);
        llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(ctx.context, "strlen_body", func);
        llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(ctx.context, "strlen_end", func);

        // Jump to check loop
        ctx.builder.CreateBr(checkBlock);

        // --- CHECK BLOCK ---
        ctx.builder.SetInsertPoint(checkBlock);
        llvm::PHINode* currPtr = ctx.builder.CreatePHI(ctx.builder.getInt8Ty()->getPointerTo(), 2, "ptr_cursor");
        currPtr->addIncoming(msgVal, preLoopBlock);

        llvm::Value* charVal = ctx.builder.CreateLoad(ctx.builder.getInt8Ty(), currPtr, "char");
        llvm::Value* isNull = ctx.builder.CreateICmpEQ(charVal, ctx.builder.getInt8(0), "is_null");
        ctx.builder.CreateCondBr(isNull, endBlock, bodyBlock);

        // --- BODY BLOCK ---
        ctx.builder.SetInsertPoint(bodyBlock);
        llvm::Value* nextPtr = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), currPtr, ctx.builder.getInt32(1), "next_ptr");
        ctx.builder.CreateBr(checkBlock);
        
        // Complete PHI
        currPtr->addIncoming(nextPtr, bodyBlock);

        // --- END BLOCK ---
        ctx.builder.SetInsertPoint(endBlock);
        
        // Length = currPtr - startPtr (pointer difference)
        llvm::Value* endInt = ctx.builder.CreatePtrToInt(currPtr, ctx.builder.getInt64Ty());
        llvm::Value* startInt = ctx.builder.CreatePtrToInt(msgVal, ctx.builder.getInt64Ty());
        llvm::Value* lenVal = ctx.builder.CreateSub(endInt, startInt, "len");

        // 5. Call write(fd, msg, len)
        return ctx.builder.CreateCall(writeFunc, {fdVal, msgVal, lenVal});
    }
};

class TimeSleepMs : public SystemCall {
public:
    llvm::Value* compile(CompilerContext& ctx, const std::vector<YAML::Node>& args) override {
         llvm::Function* usleepFunc = ctx.module.getFunction("usleep");
         if (!usleepFunc) {
            std::vector<llvm::Type*> usArgs = { ctx.builder.getInt32Ty() };
            llvm::FunctionType* usType = llvm::FunctionType::get(ctx.builder.getInt32Ty(), usArgs, false);
            usleepFunc = llvm::Function::Create(usType, llvm::Function::ExternalLinkage, "usleep", ctx.module);
         }
         
         if (args.size() > 1) {
             int ms = args[1].as<int>();
             return ctx.builder.CreateCall(usleepFunc, { ctx.builder.getInt32(ms * 1000) });
         }
         return nullptr;
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
