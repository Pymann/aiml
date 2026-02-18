#pragma once
#include "command.h"
#include "../core/context.h"
#include "../core/expression.h"

class ThrowCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        // Syntax: throw: { new: DivisionByZeroException, message: "..." }
        auto throwNode = node["throw"];
        
        // 1. Declare __cxa_allocate_exception
        std::vector<llvm::Type*> allocArgs = { llvm::Type::getInt64Ty(ctx.context) };
        llvm::FunctionType* allocType = llvm::FunctionType::get(
            llvm::PointerType::getUnqual(ctx.context), allocArgs, false
        );
        llvm::FunctionCallee allocFunc = ctx.module.getOrInsertFunction("__cxa_allocate_exception", allocType);

        // 2. Declare __cxa_throw
        std::vector<llvm::Type*> throwArgs = {
            llvm::PointerType::getUnqual(ctx.context), // obj
            llvm::PointerType::getUnqual(ctx.context), // type info where? for now null or valid RTTI? 
            // We need valid TypeInfo for the catch to work?
            // If we use null here, can we catch it? 
            // The catch block uses "null" to catch ANY. 
            // But if we want to catch specific types later, we need real RTTI.
            // For iteration 1 (DivisionByZero), let's use a Dummy TypeInfo pointer?
            // Or just null? __cxa_throw requires valid typeinfo usually. 
            // Let's use i8* null for now and see if it crashes or just catches everything.
            llvm::PointerType::getUnqual(ctx.context)  // destructor
        };
        llvm::FunctionType* throwFuncType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(ctx.context), throwArgs, false
        );
        llvm::FunctionCallee throwFunc = ctx.module.getOrInsertFunction("__cxa_throw", throwFuncType);

        // 3. Allocate Exception Object (Size 8 for i64? or sizeof struct?)
        // DivisionByZeroException has "message" (string = char*) -> 8 bytes (ptr)
        llvm::Constant* sizeVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx.context), 8); 
        llvm::Value* exceptionObj = ctx.builder.CreateCall(allocFunc, {sizeVal}, "ex_obj");

        // 4. Initialize Exception (Pseudo-Constructor)
        // Set message if provided
        if (throwNode["message"]) {
             // Store message ptr at offset 0
             // Cast i8* to i8** (pointer to string)
             llvm::Value* msgPtr = ctx.builder.CreateBitCast(exceptionObj, llvm::PointerType::getUnqual(ctx.context));
             llvm::Value* msgVal = Expression::evaluate(ctx, throwNode["message"]);
             ctx.builder.CreateStore(msgVal, msgPtr);
        }

        // 5. Throw
        // We use null for typeinfo and dtor for now (Simplification)
        llvm::Value* nullPtr = llvm::Constant::getNullValue(llvm::PointerType::getUnqual(ctx.context));
        
        if (ctx.currentExceptionBlock) {
             llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
             llvm::BasicBlock* normalBlock = llvm::BasicBlock::Create(ctx.context, "throw_cont", parentFunc);
             ctx.builder.CreateInvoke(throwFunc, normalBlock, ctx.currentExceptionBlock, {exceptionObj, nullPtr, nullPtr});
             ctx.builder.SetInsertPoint(normalBlock);
        } else {
             ctx.builder.CreateCall(throwFunc, {exceptionObj, nullPtr, nullPtr});
        }

        // 6. Unreachable (Throw never returns)
        ctx.builder.CreateUnreachable();
    }
};
