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
        
        if (!ctx.ir_gen->hasPersonalityFunction()) {
             ctx.ir_gen->setPersonalityFunction(ctx.ir_gen->getPersonalityFunction());
        }
        
        // 1. Create Blocks
        AIML::IRBlock* tryCont = ctx.ir_gen->createBasicBlock("try_cont");
        AIML::IRBlock* catchBlock = ctx.ir_gen->createBasicBlock("catch_block");
        AIML::IRBlock* endBlock = ctx.ir_gen->createBasicBlock("try_end");
        
        // 2. Set Unwind Destination
        AIML::IRBlock* oldUnwind = ctx.ir_exception_block;
        ctx.ir_exception_block = catchBlock;
        
        // 3. Compile Try Body
        // Calls inside here will now generate 'invoke' to catchBlock
        if (tryNode["body"] && tryNode["body"].IsSequence()) {
             if (ctx.params.compileCallback) {
                  ctx.params.compileCallback(ctx, tryNode["body"]);
             }
        }
        
        // Restore Unwind
        ctx.ir_exception_block = oldUnwind;
        
        // Jump to End (Success Path)
        // If last instruction is not terminator (e.g. return)
        // Note: For now, we assume if we reach here we can branch
        ctx.ir_gen->createBr(endBlock);
        
        // 4. Generate Catch Block (Landing Pad)
        ctx.ir_gen->setInsertPoint(catchBlock);
        
        llvm::Type* llType = llvm::StructType::get(
            llvm::PointerType::getUnqual(ctx.context), 
            llvm::Type::getInt32Ty(ctx.context)   
        );
        AIML::IRType* lpType = ctx.ir_gen->wrapType((void*)llType);
        
        AIML::IRValue* lp = ctx.ir_gen->createLandingPad(lpType, 1, "lp");
        ctx.ir_gen->setCleanupToLandingPad(lp, true);
        
        AIML::IRValue* nullPtr = ctx.ir_gen->wrap((void*)llvm::Constant::getNullValue(llvm::PointerType::getUnqual(ctx.context)));
        ctx.ir_gen->addClauseToLandingPad(lp, nullPtr);
        
        // Call __cxa_begin_catch(obj) -> void*
        llvm::FunctionCallee beginCatch = ctx.module.getOrInsertFunction(
            "__cxa_begin_catch", 
            llvm::FunctionType::get(llvm::PointerType::getUnqual(ctx.context), {llvm::PointerType::getUnqual(ctx.context)}, false)
        );
        
        AIML::IRValue* exObj = ctx.ir_gen->createExtractValue(lp, 0, "ex_obj");
        AIML::IRValue* adjustedEx = ctx.ir_gen->createCall("__cxa_begin_catch", {exObj}, "adjusted_ex");
        
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
            AIML::IRValue* msgPtrPtr = ctx.ir_gen->createBitCast(adjustedEx, ctx.ir_gen->getPointerTy(), "msg_ptr_ptr");
            AIML::IRValue* msgPtr = ctx.ir_gen->createLoad(ctx.ir_gen->getPointerTy(), msgPtrPtr, "ex_msg");
            
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
            
            AIML::IRType* irExStructTy = ctx.ir_gen->wrapType((void*)exStructTy);
            AIML::IRValue* exAlloca = ctx.ir_gen->createAlloca(irExStructTy, varName + "_alloc");
            AIML::IRValue* exMsgDest = ctx.ir_gen->createStructGEP(irExStructTy, exAlloca, 0, "msg_dest");
            ctx.ir_gen->createStore(msgPtr, exMsgDest);
            
            Symbol sym;
            sym.alloca_inst = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(exAlloca));
            sym.ir_alloca_inst = exAlloca;
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
        ctx.module.getOrInsertFunction(
            "__cxa_end_catch", 
            llvm::FunctionType::get(llvm::Type::getVoidTy(ctx.context), {}, false)
        );
        
        ctx.ir_gen->createCall("__cxa_end_catch", {}, "end_catch");
        
        ctx.ir_gen->createBr(endBlock);
        
        // 5. Resume Normal Flow
        ctx.ir_gen->setInsertPoint(endBlock);
    }
};
