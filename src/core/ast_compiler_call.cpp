#define DEBUG_MODE
#include "ast_compiler.h"
#include "list_impl.h"
#include "map_impl.h"
#include "buffer_impl.h"
#include "string_utils.h"
#include "expression.h"
#include <iostream>

namespace AIML {
namespace AST {

IRValue* Compiler::visitCall(CallExpr* call) {
    std::string funcName = call->functionName;
    size_t dotPos = funcName.find('.');
    
    // Check for lazy-evaluated method arguments before typical eager evaluation
    if (dotPos != std::string::npos) {
        std::string baseName = funcName.substr(0, dotPos);
        std::string methodName = funcName.substr(dotPos + 1);
        
        Symbol* sym = resolveVariable(baseName);
        if (sym) {
            return visitObjectMethodCall(baseName, methodName, sym, call);
        }
        
        // Eagerly evaluate arguments for other functions (sys)
        std::vector<IRValue*> argVals;
        for (auto& arg : call->args) {
            IRValue* val = visitExpression(arg.get());
            if (val) argVals.push_back(val);
            else return ctx.ir_gen->getInt32(0);
        }
        
        // System Namespaces (sys, io, time)
        if (baseName == "sys" || baseName == "io" || baseName == "time") {
            IRValue* sysResult = visitSysNamespaceCall(baseName, methodName, argVals);
            if (sysResult) return sysResult;
        }
        // For standard compilation, we just emit a call to their mangled C names.
        std::string mangledSys = baseName + "_" + methodName;
        if (ctx.module.getFunction(mangledSys)) {
             return ctx.ir_gen->createCall(mangledSys, argVals, "call_sys");
        }
        
        // FQN Fallback (e.g. my.math.add)
        CompilerContext::FunctionInfo* fInfo = resolveFunction(funcName);
        llvm::Function* f = fInfo ? fInfo->func : ctx.module.getFunction(funcName);
        if (f) {
            std::vector<llvm::Value*> finalArgs;
            for (size_t i = 0; i < argVals.size(); ++i) {
                 finalArgs.push_back(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[i])));
            }
            LOG_DEBUG("Call Result Ty: " + std::to_string(f->getReturnType()->getTypeID()));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, finalArgs));
        }
        
        LOG_ERROR("Unknown method or namespace function: " + funcName);
        return ctx.ir_gen->getInt32(0);
    }
    
    // Eagerly evaluate arguments for system functions and standard functions
    std::vector<IRValue*> argVals;
    for (size_t i = 0; i < call->args.size(); ++i) {
        IRValue* val = visitExpression(call->args[i].get());
        if (val) {
            argVals.push_back(val);
        } else {
            LOG_ERROR("Failed to evaluate argument " + std::to_string(i) + " for call to: " + funcName);
            return ctx.ir_gen->getInt32(0);
        }
    }

    // System Call Hook (Print)
    if (funcName == "print") {
         std::vector<IRValue*> printArgs;
         std::string formatStr = "";
         
         // Build format string and arguments list
         for (size_t i = 0; i < argVals.size(); ++i) {
             IRValue* argVal = argVals[i];
             llvm::Type* argTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(argVal->getType()));
             
             if (argTy->isPointerTy()) {
                 formatStr += "%s";
                 
                 // Null pointer guard
                 llvm::Value* rawVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVal));
                 llvm::Value* isNull = ctx.builder.CreateIsNull(rawVal, "print_null_check");
                 llvm::Value* emptyStr = ctx.builder.CreateGlobalStringPtr("(null)", "print_null_str");
                 llvm::Value* safeStr = ctx.builder.CreateSelect(isNull, emptyStr, rawVal, "print_safe_str");
                 printArgs.push_back(ctx.ir_gen->wrap(safeStr));
             } else {
                 formatStr += "%d";
                 printArgs.push_back(argVal);
             }
         }
         formatStr += "\n";
         
         // Insert format string as first argument
         IRValue* fmt = ctx.ir_gen->createGlobalStringPtr(formatStr);
         printArgs.insert(printArgs.begin(), fmt);
         
         llvm::Function* printfFunc = ctx.module.getFunction("printf");
         if (!printfFunc) {
             llvm::FunctionType* printfType = llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, true);
             printfFunc = llvm::Function::Create(printfType, llvm::Function::ExternalLinkage, "printf", ctx.module);
         }
         return ctx.ir_gen->createCall("printf", printArgs, "print_call");
    } else if (funcName == "free") {
         llvm::Function* freeFunc = ctx.module.getFunction("free");
         if (!freeFunc) freeFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "free", ctx.module);
         if (argVals.empty()) return ctx.ir_gen->getInt32(0);
         llvm::Value* ptrVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
         if (ptrVal->getType() != ctx.builder.getInt8Ty()->getPointerTo()) {
             ptrVal = ctx.builder.CreateBitCast(ptrVal, ctx.builder.getInt8Ty()->getPointerTo());
         }
         ctx.builder.CreateCall(freeFunc, {ptrVal});
         return ctx.ir_gen->getInt32(0);
    }
    
    // Normal Call
    IRValue* callRes = nullptr;
    CompilerContext::FunctionInfo* funcInfo = resolveFunction(funcName);
    llvm::Function* f = funcInfo ? funcInfo->func : ctx.module.getFunction(funcName);
    
    if (f) {
        if (!f->isVarArg() && argVals.size() < f->arg_size()) {
            LOG_ERROR("Not enough arguments for function " + funcName + ". Expected " + std::to_string(f->arg_size()) + ", got " + std::to_string(argVals.size()));
        }
        for (size_t i = 0; i < argVals.size(); ++i) {
            if (i < f->arg_size()) {
                llvm::Type* expectedTy = f->getFunctionType()->getParamType(i);
                llvm::Value* actualVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[i]));
                if (!actualVal) {
                    LOG_ERROR("Null LLVM value passed as argument " + std::to_string(i) + " for function " + funcName);
                    continue;
                }
                llvm::Type* actualTy = actualVal->getType();
                
                if (expectedTy->isPointerTy() && actualTy->isStructTy()) {
                    if (auto varExpr = dynamic_cast<VariableExpr*>(call->args[i].get())) {
                        Symbol* sym = resolveVariable(varExpr->name);
                        if (sym) {
                            actualVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
                            argVals[i] = ctx.ir_gen->wrap(actualVal);
                            actualTy = actualVal->getType();
                        }
                    }
                }
                
                if (expectedTy->isPointerTy() && actualTy->isPointerTy() && expectedTy != actualTy) {
                    argVals[i] = ctx.ir_gen->wrap(ctx.builder.CreateBitCast(actualVal, expectedTy));
                }
            }
        }
        if (ctx.catchBlock) {
             llvm::Function* currentFunc = ctx.builder.GetInsertBlock()->getParent();
             llvm::BasicBlock* invokeCont = llvm::BasicBlock::Create(ctx.context, "invoke_cont", currentFunc);
             
             std::vector<llvm::Value*> rawArgs;
             for (auto arg : argVals) rawArgs.push_back(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(arg)));
             
             llvm::Value* invRes = ctx.builder.CreateInvoke(f, invokeCont, ctx.catchBlock, rawArgs, funcName + "_inv");
             callRes = ctx.ir_gen->wrap(invRes);
             ctx.builder.SetInsertPoint(invokeCont);
        } else {
             callRes = ctx.ir_gen->createCall(f->getName().str(), argVals);
        }
    }
    
    if (!callRes) {
         LOG_ERROR("Unknown function: " + funcName);
         return nullptr;
    }
    return callRes;
}
IRValue* Compiler::visitObjectMethodCall(const std::string& baseName, const std::string& methodName, Symbol* sym, CallExpr* call) {
    std::string className = sym->typeName;
    if ((className.rfind("ptr<", 0) == 0 || className.rfind("ref<", 0) == 0) && className.back() == '>') {
         className = className.substr(4, className.length() - 5);
    }

    IRValue* baseAlloca = sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst);
    IRType* baseElemTy = sym->ir_element_type ? sym->ir_element_type : ctx.ir_gen->wrapType(sym->element_type);

    // Buffer methods (lazy arg evaluation)
    if (className == "buffer") {
        if (methodName == "alloc" && call->args.size() == 1) {
            IRValue* sizeVal = visitExpression(call->args[0].get());
            if (sizeVal) BufferImpl::alloc(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sizeVal)));
            return ctx.ir_gen->getInt32(0);
        } else if (methodName == "write" && call->args.size() >= 2) {
            IRValue* val = visitExpression(call->args[0].get());
            std::string typeName = "unknown";
            if (auto varExpr = dynamic_cast<VariableExpr*>(call->args[1].get())) typeName = varExpr->name;
            else if (auto litExpr = dynamic_cast<LiteralExpr*>(call->args[1].get())) typeName = litExpr->value;
            llvm::Type* ty = ctx.getLLVMType(typeName);
            if (ty && val) BufferImpl::write(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)), ty);
            return ctx.ir_gen->getInt32(0);
        } else if (methodName == "read" && call->args.size() >= 1) {
            std::string typeName = "unknown";
            if (auto varExpr = dynamic_cast<VariableExpr*>(call->args[0].get())) typeName = varExpr->name;
            else if (auto litExpr = dynamic_cast<LiteralExpr*>(call->args[0].get())) typeName = litExpr->value;
            llvm::Type* ty = ctx.getLLVMType(typeName);
            if (ty) return ctx.ir_gen->wrap(BufferImpl::read(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), ty));
            return ctx.ir_gen->getInt32(0);
        } else if (methodName == "seek" && call->args.size() >= 1) {
            IRValue* rawPos = visitExpression(call->args[0].get());
            llvm::Value* rawVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rawPos));
            llvm::Value* pos32 = ctx.builder.CreateZExtOrTrunc(rawVal, ctx.builder.getInt32Ty());
            BufferImpl::seek(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), pos32, false);
            BufferImpl::seek(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), pos32, true);
            return ctx.ir_gen->getInt32(0);
        } else if (methodName == "read_versioned" && call->args.size() >= 1) {
            std::string typeName = "unknown";
            if (auto varExpr = dynamic_cast<VariableExpr*>(call->args[0].get())) typeName = varExpr->name;
            else if (auto litExpr = dynamic_cast<LiteralExpr*>(call->args[0].get())) typeName = litExpr->value;
            llvm::Type* ty = ctx.getLLVMType(typeName);
            if (ty) {
                auto realEval = [this](CompilerContext& c, std::shared_ptr<AIML::AST::Expr> e) -> llvm::Value* {
                     IRValue* v = this->visitExpression(e.get());
                     if (!v) return llvm::Constant::getNullValue(ctx.getLLVMType("buffer"));
                     return static_cast<llvm::Value*>(c.ir_gen->unwrap(v));
                };
                llvm::Value* explicitVer = nullptr;
                if (call->args.size() >= 2) {
                    IRValue* verVal = visitExpression(call->args[1].get());
                    if (verVal) explicitVer = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(verVal));
                }
                return ctx.ir_gen->wrap(BufferImpl::read_versioned(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), ty, realEval, explicitVer));
            }
            return ctx.ir_gen->getInt32(0);
        }
    }

    // String.len (lazy — needs only the alloca, no args)
    if (className == "string" && methodName == "len") {
        llvm::Function* strlenFunc = ctx.module.getFunction("strlen");
        if (!strlenFunc) strlenFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "strlen", ctx.module);
        llvm::Value* strPtr = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)));
        llvm::Value* len64 = ctx.builder.CreateCall(strlenFunc, {strPtr});
        llvm::Value* len32 = ctx.builder.CreateTrunc(len64, ctx.builder.getInt32Ty());
        return ctx.ir_gen->wrap(len32);
    }

    // Eagerly evaluate arguments for collection and class methods
    std::vector<IRValue*> argVals;
    for (auto& arg : call->args) {
        IRValue* val = visitExpression(arg.get());
        if (val) argVals.push_back(val);
        else return ctx.ir_gen->getInt32(0);
    }

    // List Methods
    if (className.find("list<") == 0) {
         std::string listElemName = className.substr(5, className.length() - 6);
         llvm::Type* llvmListElemType = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(listElemName)));
         if (methodName == "push" && argVals.size() > 0) {
             ListImpl::push(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])));
             return nullptr;
         } else if (methodName == "pop") {
             return ctx.ir_gen->wrap(ListImpl::pop(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), llvmListElemType));
         } else if (methodName == "get" && argVals.size() > 0) {
             return ctx.ir_gen->wrap(ListImpl::get(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])), llvmListElemType));
         } else if (methodName == "insert" && argVals.size() > 1) {
             ListImpl::insert(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1])));
             return nullptr;
         } else if (methodName == "remove" && argVals.size() > 0) {
             ListImpl::remove(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])), llvmListElemType);
             return nullptr;
         } else if (methodName == "clear") {
             ListImpl::clear(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)));
             return nullptr;
         } else if (methodName == "len") {
             return ctx.ir_gen->wrap(ListImpl::getSize(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca))));
         }
    }
    // Map Methods
    else if (className.find("map<") == 0) {
         llvm::StructType* mapStructTy = static_cast<llvm::StructType*>(ctx.ir_gen->unwrapType(baseElemTy));
         llvm::Type* vTy = nullptr;
         std::string inner = className.substr(4, className.length() - 5);
         size_t colon = inner.find(':');
         if (colon != std::string::npos) {
             std::string vName = inner.substr(colon + 1);
             vTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(vName)));
         }
         if (!vTy) vTy = ctx.builder.getInt8Ty()->getPointerTo();
         if (methodName == "put" && argVals.size() > 1) {
             llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
             llvm::Value* valVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]));
             if (vTy->isPointerTy() && valVal->getType()->isStructTy()) {
                 if (auto varExpr = dynamic_cast<VariableExpr*>(call->args[1].get())) {
                     Symbol* s = resolveVariable(varExpr->name);
                     if (s) valVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(s->ir_alloca_inst ? s->ir_alloca_inst : ctx.ir_gen->wrap(s->alloca_inst)));
                 }
             }
             if (vTy->isPointerTy() && valVal->getType()->isPointerTy() && valVal->getType() != vTy) {
                 valVal = ctx.builder.CreateBitCast(valVal, vTy);
             }
             if (keyVal->getType()->isPointerTy() && keyVal->getType() != ctx.builder.getInt8Ty()->getPointerTo()) {
                 keyVal = ctx.builder.CreateBitCast(keyVal, ctx.builder.getInt8Ty()->getPointerTo());
             }
             MapImpl::put(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), keyVal, valVal, mapStructTy);
             return nullptr;
         } else if (methodName == "get" && argVals.size() > 0) {
             llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
             if (keyVal->getType()->isPointerTy() && keyVal->getType() != ctx.builder.getInt8Ty()->getPointerTo()) {
                 keyVal = ctx.builder.CreateBitCast(keyVal, ctx.builder.getInt8Ty()->getPointerTo());
             }
             return ctx.ir_gen->wrap(MapImpl::get(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), keyVal, vTy, mapStructTy));
         } else if (methodName == "remove" && argVals.size() > 0) {
             llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
             if (keyVal->getType()->isPointerTy() && keyVal->getType() != ctx.builder.getInt8Ty()->getPointerTo()) {
                 keyVal = ctx.builder.CreateBitCast(keyVal, ctx.builder.getInt8Ty()->getPointerTo());
             }
             MapImpl::remove(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), keyVal, vTy, mapStructTy);
             return nullptr;
         } else if (methodName == "contains" && argVals.size() > 0) {
             return ctx.ir_gen->wrap(MapImpl::contains(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])), mapStructTy));
         } else if (methodName == "len") {
             return ctx.ir_gen->wrap(MapImpl::getSize(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), mapStructTy));
         }
    }
    // Process Methods
    else if (className == "process") {
         llvm::Value* procPtr = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)));
         if (methodName == "wait") {
             llvm::Function* waitFunc = ctx.module.getFunction("process_wait");
             if (!waitFunc) waitFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "process_wait", ctx.module);
             return ctx.ir_gen->wrap(ctx.builder.CreateCall(waitFunc, {procPtr}));
         } else if (methodName == "kill") {
             llvm::Function* killFunc = ctx.module.getFunction("process_kill");
             if (!killFunc) killFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "process_kill", ctx.module);
             ctx.builder.CreateCall(killFunc, {procPtr});
             return ctx.ir_gen->getInt32(0);
         }
    }
    // User Class Methods
    else if (!className.empty() && ctx.classes.count(className)) {
         std::string mangledName = className + "_" + methodName;
         llvm::Function* func = ctx.module.getFunction(mangledName);
         if (!func) {
             std::string currClass = className;
             while (!currClass.empty() && ctx.classes.count(currClass)) {
                 std::string pName = ctx.classes[currClass].parentName;
                 if (!pName.empty()) {
                     currClass = pName;
                     func = ctx.module.getFunction(currClass + "_" + methodName);
                     if (func) break;
                 } else {
                     break;
                 }
             }
         }
         if (!func) {
             LOG_ERROR("Unknown method: " + className + "_" + methodName);
             return ctx.ir_gen->getInt32(0);
         }
         IRValue* thisPtr = baseAlloca;
         if (baseElemTy->isPointer()) {
             thisPtr = ctx.ir_gen->createLoad(baseElemTy, baseAlloca, "this_ptr");
         }
         if (!thisPtr) return ctx.ir_gen->getInt32(0);
         llvm::Type* expectedThisTy = func->getFunctionType()->getParamType(0);
         llvm::Value* rawThis = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(thisPtr));
         if (rawThis->getType() != expectedThisTy) {
             rawThis = ctx.builder.CreateBitCast(rawThis, expectedThisTy, "this_cast");
             thisPtr = ctx.ir_gen->wrap(rawThis);
         }
         std::vector<IRValue*> methodArgs;
         methodArgs.push_back(thisPtr);
         for (auto* a : argVals) methodArgs.push_back(a);
         return ctx.ir_gen->createCall(func->getName().str(), methodArgs, "call_method");
    }

    LOG_ERROR("Unknown method: " + methodName + " on variable " + baseName);
    return ctx.ir_gen->getInt32(0);
}
IRValue* Compiler::visitSysNamespaceCall(const std::string& baseName, const std::string& methodName, const std::vector<IRValue*>& argVals) {
    if (baseName == "sys" && methodName == "exit") {
        llvm::Function* exitFunc = ctx.module.getFunction("exit");
        if (!exitFunc) exitFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "exit", ctx.module);
        llvm::Value* exitCode = argVals.empty() ? ctx.builder.getInt32(0) : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
        ctx.builder.CreateCall(exitFunc, {exitCode});
        return ctx.ir_gen->getInt32(0);
    }

    if (baseName == "sys" && methodName == "get_arg") {
        if (argVals.empty()) return ctx.ir_gen->createGlobalStringPtr("");
        llvm::Value* indexVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
        llvm::GlobalVariable* argvVar = ctx.module.getGlobalVariable("__argv", true);
        llvm::GlobalVariable* argcVar = ctx.module.getGlobalVariable("__argc", true);
        if (!argvVar || !argcVar) return ctx.ir_gen->createGlobalStringPtr("");

        // Emit outlined helper once per module to avoid IR duplication
        llvm::Function* impl = ctx.module.getFunction("__aiml_sys_get_arg");
        if (!impl) {
            llvm::PointerType* i8Ptr    = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::PointerType* i8PtrPtr = i8Ptr->getPointerTo();
            llvm::Type* i32      = ctx.builder.getInt32Ty();
            llvm::IRBuilder<>::InsertPoint savedIP = ctx.builder.saveIP();
            llvm::FunctionType* ft = llvm::FunctionType::get(i8Ptr, {i32}, false);
            impl = llvm::Function::Create(ft, llvm::Function::InternalLinkage, "__aiml_sys_get_arg", ctx.module);
            impl->addFnAttr(llvm::Attribute::NoInline);
            llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(ctx.context, "entry",     impl);
            llvm::BasicBlock* safeBB  = llvm::BasicBlock::Create(ctx.context, "arg_safe",  impl);
            llvm::BasicBlock* errorBB = llvm::BasicBlock::Create(ctx.context, "arg_error", impl);
            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "arg_merge", impl);
            ctx.builder.SetInsertPoint(entryBB);
            llvm::Value* idxArg  = impl->getArg(0);
            llvm::Value* argvPtr = ctx.builder.CreateLoad(i8PtrPtr, argvVar);
            llvm::Value* argcVal = ctx.builder.CreateLoad(i32, argcVar);
            llvm::Value* argvIsNull = ctx.builder.CreateICmpEQ(argvPtr, llvm::ConstantPointerNull::get(i8PtrPtr));
            llvm::Value* idxOk   = ctx.builder.CreateICmpSLT(idxArg, argcVal);
            llvm::Value* isSafe  = ctx.builder.CreateAnd(ctx.builder.CreateNot(argvIsNull), idxOk);
            ctx.builder.CreateCondBr(isSafe, safeBB, errorBB);
            ctx.builder.SetInsertPoint(safeBB);
            llvm::Value* argStrPtr = ctx.builder.CreateInBoundsGEP(i8Ptr, argvPtr, idxArg);
            llvm::Value* argStr    = ctx.builder.CreateLoad(i8Ptr, argStrPtr);
            ctx.builder.CreateBr(mergeBB);
            ctx.builder.SetInsertPoint(errorBB);
            llvm::Value* emptyStr = ctx.builder.CreateGlobalStringPtr("");
            ctx.builder.CreateBr(mergeBB);
            ctx.builder.SetInsertPoint(mergeBB);
            llvm::PHINode* result = ctx.builder.CreatePHI(i8Ptr, 2);
            result->addIncoming(argStr, safeBB);
            result->addIncoming(emptyStr, errorBB);
            ctx.builder.CreateRet(result);
            ctx.builder.restoreIP(savedIP);
        }
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(impl, {indexVal}));
    }

    if (baseName == "sys" && methodName == "env") {
        llvm::Type* strTy = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::StructType* mapTy = llvm::StructType::get(ctx.context, {
            ctx.builder.getInt32Ty(), ctx.builder.getInt32Ty(),
            ctx.builder.getInt8Ty()->getPointerTo(),
            strTy->getPointerTo(), strTy->getPointerTo()
        });
        llvm::DataLayout dl(&ctx.module);
        uint64_t mapSize = dl.getTypeAllocSize(mapTy);
        llvm::Function* callocFunc = ctx.module.getFunction("calloc");
        if (!callocFunc) callocFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty()}, false), llvm::Function::ExternalLinkage, "calloc", ctx.module);
        llvm::Value* mapVoidPtr = ctx.builder.CreateCall(callocFunc, {ctx.builder.getInt64(1), ctx.builder.getInt64(mapSize)});
        llvm::Value* mapPtr = ctx.builder.CreateBitCast(mapVoidPtr, mapTy->getPointerTo(), "sys_env_map");
        MapImpl::initMap(ctx, mapPtr, strTy, strTy, 4);
        return ctx.ir_gen->wrap(mapPtr);
    }

    if (baseName == "sys" && methodName == "size") {
        if (argVals.empty()) return ctx.ir_gen->getInt32(0);
        llvm::Type* ty = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(argVals[0]->getType()));
        llvm::DataLayout dl(&ctx.module);
        return ctx.ir_gen->wrap(ctx.builder.getInt64(dl.getTypeAllocSize(ty)));
    }

    if (baseName == "sys" && methodName == "exe") {
        if (argVals.empty()) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()));
        llvm::Value* argvList = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
        if (argvList->getType()->isStructTy()) {
            llvm::AllocaInst* temp = ctx.builder.CreateAlloca(argvList->getType());
            ctx.builder.CreateStore(argvList, temp);
            argvList = ctx.builder.CreateBitCast(temp, ctx.builder.getInt8Ty()->getPointerTo());
        } else if (argvList->getType()->isPointerTy()) {
            argvList = ctx.builder.CreateBitCast(argvList, ctx.builder.getInt8Ty()->getPointerTo());
        } else return ctx.ir_gen->getInt32(0);
        llvm::Value* cwd     = llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo());
        llvm::Value* env     = llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo());
        llvm::Value* shell   = ctx.builder.getInt1(false);
        llvm::Value* timeout = ctx.builder.getInt32(-1);
        llvm::Value* capture = ctx.builder.getInt1(false);
        llvm::Function* runtimeFunc = ctx.module.getFunction("sys_exe_runtime");
        if (!runtimeFunc) runtimeFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt1Ty(), ctx.builder.getInt32Ty(), ctx.builder.getInt1Ty()}, false), llvm::Function::ExternalLinkage, "sys_exe_runtime", ctx.module);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(runtimeFunc, {argvList, cwd, env, shell, timeout, capture}));
    }

    if (baseName == "io" && methodName == "write") {
        if (argVals.size() < 2) return ctx.ir_gen->getInt32(0);
        llvm::Value* handle = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
        llvm::Value* text   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]));
        llvm::Function* parentFunc    = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* validBB     = llvm::BasicBlock::Create(ctx.context, "valid_write_handle",   parentFunc);
        llvm::BasicBlock* invalidBB   = llvm::BasicBlock::Create(ctx.context, "invalid_write_handle", parentFunc);
        llvm::BasicBlock* mergeBB     = llvm::BasicBlock::Create(ctx.context, "merge_write",          parentFunc);
        llvm::Value* isNull = ctx.builder.CreateICmpEQ(handle, llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()));
        ctx.builder.CreateCondBr(isNull, invalidBB, validBB);
        ctx.builder.SetInsertPoint(invalidBB);
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(validBB);
        llvm::Function* strlenFunc = ctx.module.getFunction("strlen");
        if (!strlenFunc) strlenFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "strlen", ctx.module);
        llvm::Value* len = ctx.builder.CreateCall(strlenFunc, {text});
        llvm::Function* fwFunc = ctx.module.getFunction("fwrite");
        if (!fwFunc) fwFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty(), ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "fwrite", ctx.module);
        llvm::Value* writeBytes = ctx.builder.CreateCall(fwFunc, {text, ctx.builder.getInt64(1), len, handle});
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(mergeBB);
        llvm::PHINode* phi = ctx.builder.CreatePHI(ctx.builder.getInt64Ty(), 2, "write_res");
        phi->addIncoming(ctx.builder.getInt64(0), invalidBB);
        phi->addIncoming(writeBytes, validBB);
        return ctx.ir_gen->wrap(ctx.builder.CreateTrunc(phi, ctx.builder.getInt32Ty()));
    }

    if (baseName == "io" && methodName == "read") {
        if (argVals.size() < 2) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
        llvm::Value* handle = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
        llvm::Function* parentFunc  = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* validBB   = llvm::BasicBlock::Create(ctx.context, "valid_handle",   parentFunc);
        llvm::BasicBlock* invalidBB = llvm::BasicBlock::Create(ctx.context, "invalid_handle", parentFunc);
        llvm::BasicBlock* mergeBB   = llvm::BasicBlock::Create(ctx.context, "merge_read",     parentFunc);
        llvm::Value* isNull = ctx.builder.CreateICmpEQ(handle, llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()));
        ctx.builder.CreateCondBr(isNull, invalidBB, validBB);
        ctx.builder.SetInsertPoint(invalidBB);
        llvm::Value* emptyStr = ctx.builder.CreateGlobalStringPtr("");
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(validBB);
        llvm::Value* size = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]));
        if (size->getType()->isIntegerTy(32)) size = ctx.builder.CreateZExt(size, ctx.builder.getInt64Ty());
        llvm::BasicBlock* doReadBB  = llvm::BasicBlock::Create(ctx.context, "do_read", parentFunc);
        llvm::Value* isValidSize    = ctx.builder.CreateICmpSGT(size, ctx.builder.getInt64(0));
        ctx.builder.CreateCondBr(isValidSize, doReadBB, invalidBB);
        ctx.builder.SetInsertPoint(doReadBB);
        llvm::Function* mallocFunc = ctx.module.getFunction("malloc");
        if (!mallocFunc) mallocFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt64Ty()}, false), llvm::Function::ExternalLinkage, "malloc", ctx.module);
        llvm::Value* sizePlus1 = ctx.builder.CreateAdd(size, ctx.builder.getInt64(1));
        llvm::Value* buffer    = ctx.builder.CreateCall(mallocFunc, {sizePlus1});
        llvm::Function* freadFunc = ctx.module.getFunction("fread");
        if (!freadFunc) freadFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty(), ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "fread", ctx.module);
        llvm::Value* readBytes = ctx.builder.CreateCall(freadFunc, {buffer, ctx.builder.getInt64(1), size, handle});
        llvm::Value* endPtr    = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), buffer, readBytes);
        ctx.builder.CreateStore(ctx.builder.getInt8(0), endPtr);
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(mergeBB);
        llvm::PHINode* phi = ctx.builder.CreatePHI(ctx.builder.getInt8Ty()->getPointerTo(), 2, "read_res");
        phi->addIncoming(emptyStr, invalidBB);
        phi->addIncoming(buffer,   doReadBB);
        return ctx.ir_gen->wrap(phi);
    }

    if (baseName == "io" && methodName == "open") {
        if (argVals.size() < 2) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()));
        llvm::Value* path = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
        llvm::Value* mode = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]));
        llvm::Function* fopenFunc = ctx.module.getFunction("fopen");
        if (!fopenFunc) fopenFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "fopen", ctx.module);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fopenFunc, {path, mode}));
    }

    if (baseName == "io" && methodName == "close") {
        if (argVals.empty()) return ctx.ir_gen->getInt32(0);
        llvm::Function* fcloseFunc = ctx.module.getFunction("fclose");
        if (!fcloseFunc) fcloseFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "fclose", ctx.module);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fcloseFunc, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]))}));
    }

    if (baseName == "io" && methodName == "read_all") {
        if (argVals.empty()) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
        llvm::Value* handle = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
        llvm::Function* parentFunc  = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* validBB   = llvm::BasicBlock::Create(ctx.context, "valid_handle",     parentFunc);
        llvm::BasicBlock* invalidBB = llvm::BasicBlock::Create(ctx.context, "invalid_handle",   parentFunc);
        llvm::BasicBlock* mergeBB   = llvm::BasicBlock::Create(ctx.context, "merge_read_all",   parentFunc);
        llvm::Value* isNull = ctx.builder.CreateICmpEQ(handle, llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()));
        ctx.builder.CreateCondBr(isNull, invalidBB, validBB);
        ctx.builder.SetInsertPoint(invalidBB);
        llvm::Value* emptyStr = ctx.builder.CreateGlobalStringPtr("");
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(validBB);
        llvm::Function* fseekFunc = ctx.module.getFunction("fseek");
        if (!fseekFunc) fseekFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt64Ty(), ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "fseek", ctx.module);
        llvm::Function* ftellFunc = ctx.module.getFunction("ftell");
        if (!ftellFunc) ftellFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "ftell", ctx.module);
        llvm::Function* rewindFunc = ctx.module.getFunction("rewind");
        if (!rewindFunc) rewindFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "rewind", ctx.module);
        ctx.builder.CreateCall(fseekFunc, {handle, ctx.builder.getInt64(0), ctx.builder.getInt32(2)});
        llvm::Value* size = ctx.builder.CreateCall(ftellFunc, {handle});
        ctx.builder.CreateCall(rewindFunc, {handle});
        llvm::BasicBlock* doReadBB = llvm::BasicBlock::Create(ctx.context, "do_read_all", parentFunc);
        llvm::Value* isValidSize   = ctx.builder.CreateICmpSGT(size, ctx.builder.getInt64(0));
        ctx.builder.CreateCondBr(isValidSize, doReadBB, invalidBB);
        ctx.builder.SetInsertPoint(doReadBB);
        llvm::Function* mallocFunc = ctx.module.getFunction("malloc");
        if (!mallocFunc) mallocFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt64Ty()}, false), llvm::Function::ExternalLinkage, "malloc", ctx.module);
        llvm::Value* sizePlus1 = ctx.builder.CreateAdd(size, ctx.builder.getInt64(1));
        llvm::Value* buffer    = ctx.builder.CreateCall(mallocFunc, {sizePlus1});
        llvm::Function* freadFunc = ctx.module.getFunction("fread");
        if (!freadFunc) freadFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty(), ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "fread", ctx.module);
        llvm::Value* readBytes = ctx.builder.CreateCall(freadFunc, {buffer, ctx.builder.getInt64(1), size, handle});
        llvm::Value* endPtr    = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), buffer, readBytes);
        ctx.builder.CreateStore(ctx.builder.getInt8(0), endPtr);
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(mergeBB);
        llvm::PHINode* phi = ctx.builder.CreatePHI(ctx.builder.getInt8Ty()->getPointerTo(), 2, "read_all_res");
        phi->addIncoming(emptyStr, invalidBB);
        phi->addIncoming(buffer,   doReadBB);
        return ctx.ir_gen->wrap(phi);
    }

    if (baseName == "time" && methodName == "now") {
        llvm::Function* timeFunc = ctx.module.getFunction("time_now_ms");
        if (!timeFunc) timeFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), false), llvm::Function::ExternalLinkage, "time_now_ms", ctx.module);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(timeFunc));
    }

    if (baseName == "time" && methodName == "sleep_ms") {
        if (argVals.empty()) return ctx.ir_gen->getInt32(0);
        llvm::Function* sleepFunc = ctx.module.getFunction("time_sleep_ms");
        if (!sleepFunc) sleepFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "time_sleep_ms", ctx.module);
        llvm::Value* msVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
        if (msVal->getType()->isIntegerTy(64)) msVal = ctx.builder.CreateTrunc(msVal, ctx.builder.getInt32Ty());
        ctx.builder.CreateCall(sleepFunc, {msVal});
        return ctx.ir_gen->getInt32(0);
    }

    return nullptr; // No matching handler — caller tries FQN fallback
}
} // namespace AST
} // namespace AIML
