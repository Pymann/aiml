#define DEBUG_MODE
#include "ast_compiler.h"
#include "list_impl.h"
#include "map_impl.h"
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

    // Builtin class dispatch (buffer, process, ...)
    {
        auto classIt = ctx.classes.find(className);
        if (classIt != ctx.classes.end() && classIt->second.isBuiltin) {
            auto methodIt = classIt->second.builtinMethods.find(methodName);
            if (methodIt != classIt->second.builtinMethods.end()) {
                auto evalExpr = [this](AIML::AST::Expr* e) -> IRValue* {
                    return this->visitExpression(e);
                };
                return methodIt->second(ctx,
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)),
                    call, evalExpr);
            }
            LOG_ERROR("Unknown builtin method: " + methodName + " on " + className);
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
    auto nsIt = ctx.namespaces.find(baseName);
    if (nsIt != ctx.namespaces.end()) {
        auto funcIt = nsIt->second.builtinFuncs.find(methodName);
        if (funcIt != nsIt->second.builtinFuncs.end()) {
            return funcIt->second(ctx, argVals);
        }
    }
    return nullptr; // No matching handler — caller tries FQN fallback
}
} // namespace AST
} // namespace AIML
