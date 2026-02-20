#pragma once
#include "command.h"
#include "../core/context.h"
#include "../core/expression.h"

class TryCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        // try: { body: [...], catch: [[Type, var, body]] }
        auto tryNode = node["try"];
        
        llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
        
        if (!parentFunc->hasPersonalityFn()) {
             parentFunc->setPersonalityFn(llvm::cast<llvm::Constant>(ctx.getPersonalityFunction().getCallee()));
        }
        
        // 1. Create Blocks
        llvm::BasicBlock* tryCont = llvm::BasicBlock::Create(ctx.context, "try_cont", parentFunc);
        llvm::BasicBlock* catchBlock = llvm::BasicBlock::Create(ctx.context, "catch_block", parentFunc);
        llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(ctx.context, "try_end");
        
        // 2. Set Unwind Destination
        llvm::BasicBlock* oldUnwind = ctx.currentExceptionBlock;
        ctx.currentExceptionBlock = catchBlock;
        
        // 3. Compile Try Body
        // Calls inside here will now generate 'invoke' to catchBlock
        if (tryNode["body"] && tryNode["body"].IsSequence()) {
             if (ctx.params.compileCallback) {
                  ctx.params.compileCallback(ctx, tryNode["body"]);
             }
        }
        
        // Restore Unwind
        ctx.currentExceptionBlock = oldUnwind;
        
        // Jump to End (Success Path)
        // If last instruction is not terminator (e.g. return)
        if (!ctx.builder.GetInsertBlock()->getTerminator()) {
            ctx.builder.CreateBr(endBlock);
        }
        
        // 4. Generate Catch Block (Landing Pad)
        ctx.builder.SetInsertPoint(catchBlock);
        
        llvm::Type* lpType = llvm::StructType::get(
            llvm::PointerType::getUnqual(ctx.context), 
            llvm::Type::getInt32Ty(ctx.context)
        );
        llvm::LandingPadInst* lp = ctx.builder.CreateLandingPad(lpType, 1);
        lp->setCleanup(true); // Acts as catch-all / finally for now
        lp->addClause(llvm::Constant::getNullValue(llvm::PointerType::getUnqual(ctx.context))); // Catch Any
        
        // Call __cxa_begin_catch(obj) -> void*
        llvm::FunctionCallee beginCatch = ctx.module.getOrInsertFunction(
            "__cxa_begin_catch", 
            llvm::FunctionType::get(llvm::PointerType::getUnqual(ctx.context), {llvm::PointerType::getUnqual(ctx.context)}, false)
        );
        // Extract exception object from landingpad {i8*, i32}
        llvm::Value* exObj = ctx.builder.CreateExtractValue(lp, 0);
        llvm::Value* adjustedEx = ctx.builder.CreateCall(beginCatch, {exObj});
        
        // Compile Catch Body
        // For now, we assume simple catch-all and bind generic 'e' if requested
        if (tryNode["catch"] && tryNode["catch"].IsSequence()) {
            // catch: [{type: ..., var: ..., body: ...}]
            auto catchClause = tryNode["catch"][0]; // Only first clause supported for MVP
            
            // Expected Syntax:
            // - type: DivisionByZeroException
            //   var: e
            //   body: [...]
            
            std::string exType = catchClause["type"].as<std::string>();
            std::string varName = catchClause["var"].as<std::string>();
            
            // Bind 'e' 
            llvm::Value* msgPtrPtr = ctx.builder.CreateBitCast(adjustedEx, llvm::PointerType::getUnqual(ctx.context));
            llvm::Value* msgPtr = ctx.builder.CreateLoad(llvm::PointerType::getUnqual(ctx.context), msgPtrPtr, "ex_msg");
            
            // Create or lookup a pseudo-struct for the exception to allow `e.message`
            llvm::StructType* exStructTy = nullptr;
            if (ctx.classes.count(exType)) {
                 exStructTy = llvm::cast<llvm::StructType>(ctx.classes[exType].type);
            } else {
                 exStructTy = llvm::StructType::create(ctx.context, exType);
                 exStructTy->setBody({ llvm::PointerType::getUnqual(ctx.context) });
                 
                 CompilerContext::ClassInfo ci;
                 ci.type = exStructTy;
                 CompilerContext::StructMember sm;
                 sm.index = 0;
                 sm.type = llvm::PointerType::getUnqual(ctx.context);
                 sm.typeName = "string";
                 sm.access = AccessLevel::Public;
                 ci.members["message"] = sm;
                 ctx.classes[exType] = ci;
            }
            
            llvm::Value* exAlloca = ctx.builder.CreateAlloca(exStructTy, nullptr, varName + "_alloc");
            llvm::Value* exMsgDest = ctx.builder.CreateStructGEP(exStructTy, exAlloca, 0);
            ctx.builder.CreateStore(msgPtr, exMsgDest);
            
            Symbol sym;
            sym.alloca_inst = exAlloca;
            sym.type = exStructTy;
            sym.typeName = exType;
            sym.className = exType;
            
            bool shadowed = ctx.symbol_table.count(varName) > 0;
            Symbol oldSym;
            if (shadowed) oldSym = ctx.symbol_table[varName];
            
            ctx.symbol_table[varName] = sym;
            
            if (ctx.params.compileCallback) {
                  ctx.params.compileCallback(ctx, catchClause["body"]);
            }
            
            if (shadowed) ctx.symbol_table[varName] = oldSym;
            else ctx.symbol_table.erase(varName);
        }
        
        // Call __cxa_end_catch()
        llvm::FunctionCallee endCatch = ctx.module.getOrInsertFunction(
            "__cxa_end_catch", 
            llvm::FunctionType::get(llvm::Type::getVoidTy(ctx.context), {}, false)
        );
        ctx.builder.CreateCall(endCatch, {});
        
        ctx.builder.CreateBr(endBlock);
        
        // 5. Resume Normal Flow
        parentFunc->insert(parentFunc->end(), endBlock);
        ctx.builder.SetInsertPoint(endBlock);
    }
};
