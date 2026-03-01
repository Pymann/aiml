#define DEBUG_MODE
#include "ast_compiler.h"
#include "list_impl.h"
#include "map_impl.h"
#include "chan_impl.h"
#include "code_impl.h"
#include "string_utils.h"
#include "expression.h"
#include "serialize_impl.h"
#include "../../src/sys/db_runtime.h"
#include <iostream>
#include <queue>
#include <set>
#include <cstdio>

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
        
        // sys.serialize / sys.deserialize / sys.bundle need arg type info — intercept before eager eval
        if (baseName == "sys" && (methodName == "serialize" || methodName == "deserialize")) {
            return visitSysSerializeCall(methodName, call);
        }
        if (baseName == "sys" && methodName == "bundle") {
            return visitSysBundleCall(call);
        }
        if (baseName == "sys" && methodName == "exec") {
            return visitSysExecCall(call);
        }

        // Eagerly evaluate arguments for other functions (sys)
        std::vector<IRValue*> argVals;
        for (auto& arg : call->args) {
            IRValue* val = visitExpression(arg.get());
            if (val) argVals.push_back(val);
            else return ctx.ir_gen->getInt32(0);
        }

        // Dispatch any registered namespace (sys, io, time, path, and future ones)
        if (ctx.namespaces.count(baseName)) {
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
    
    // Normal Call — prefer indirect call via fptable_current for hot-reload support
    IRValue* callRes = nullptr;
    CompilerContext::FunctionInfo* funcInfo = resolveFunction(funcName);
    llvm::Function* f = funcInfo ? funcInfo->func : ctx.module.getFunction(funcName);

    // Coerce argument types (pointer/struct adjustments) — shared for both paths
    auto coerceArgs = [&]() {
        if (!f) return;
        for (size_t i = 0; i < argVals.size(); ++i) {
            if (i >= f->arg_size()) continue;
            llvm::Type* expectedTy = f->getFunctionType()->getParamType(i);
            llvm::Value* actualVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[i]));
            if (!actualVal) {
                LOG_ERROR("Null LLVM value passed as argument " + std::to_string(i) +
                          " for function " + funcName);
                continue;
            }
            llvm::Type* actualTy = actualVal->getType();

            if (expectedTy->isPointerTy() && actualTy->isStructTy()) {
                if (auto varExpr = dynamic_cast<VariableExpr*>(call->args[i].get())) {
                    Symbol* sym = resolveVariable(varExpr->name);
                    if (sym) {
                        actualVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
                            sym->ir_alloca_inst ? sym->ir_alloca_inst
                                                : ctx.ir_gen->wrap(sym->alloca_inst)));
                        argVals[i] = ctx.ir_gen->wrap(actualVal);
                        actualTy   = actualVal->getType();
                    }
                }
            }
            if (expectedTy->isPointerTy() && actualTy->isPointerTy() && expectedTy != actualTy)
                argVals[i] = ctx.ir_gen->wrap(ctx.builder.CreateBitCast(actualVal, expectedTy));
        }
    };

    if (f && funcInfo && funcInfo->fptable_index >= 0) {
        // ── Indirect call via __aiml_fptable_current[idx] ────────────────────
        llvm::GlobalVariable* cur_gv =
            ctx.module.getNamedGlobal("__aiml_fptable_current");
        if (cur_gv) {
            coerceArgs();
            if (!f->isVarArg() && argVals.size() < f->arg_size())
                LOG_ERROR("Not enough arguments for function " + funcName);

            auto* i8p  = llvm::PointerType::get(ctx.context, 0);  // opaque ptr (LLVM 18+)
            auto* i32  = llvm::Type::getInt32Ty(ctx.context);
            llvm::Value* slot = ctx.builder.CreateGEP(
                cur_gv->getValueType(), cur_gv,
                {llvm::ConstantInt::get(i32, 0),
                 llvm::ConstantInt::get(i32, funcInfo->fptable_index)},
                "fptable_slot");
            llvm::Value* fp_raw = ctx.builder.CreateLoad(i8p, slot, "fp_raw");
            llvm::Value* fp = ctx.builder.CreateBitCast(
                fp_raw, f->getType(), "fp");

            std::vector<llvm::Value*> rawArgs;
            for (auto& av : argVals)
                rawArgs.push_back(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(av)));

            if (ctx.catchBlock) {
                llvm::Function* cur = ctx.builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* cont =
                    llvm::BasicBlock::Create(ctx.context, "invoke_cont", cur);
                llvm::Value* invRes = ctx.builder.CreateInvoke(
                    f->getFunctionType(), fp, cont, ctx.catchBlock, rawArgs,
                    funcName + "_inv");
                callRes = ctx.ir_gen->wrap(invRes);
                ctx.builder.SetInsertPoint(cont);
            } else {
                llvm::Value* res = ctx.builder.CreateCall(
                    f->getFunctionType(), fp, rawArgs, funcName + "_icall");
                callRes = ctx.ir_gen->wrap(res);
            }
        } else {
            // fptable not yet emitted (e.g. during registerTypes pass) — direct call
            coerceArgs();
            callRes = ctx.ir_gen->createCall(f->getName().str(), argVals);
        }
    } else if (f) {
        // ── Direct call (builtins / external / functions outside fptable) ─────
        if (!f->isVarArg() && argVals.size() < f->arg_size())
            LOG_ERROR("Not enough arguments for function " + funcName +
                      ". Expected " + std::to_string(f->arg_size()) +
                      ", got " + std::to_string(argVals.size()));
        coerceArgs();
        if (ctx.catchBlock) {
            llvm::Function* currentFunc = ctx.builder.GetInsertBlock()->getParent();
            llvm::BasicBlock* invokeCont =
                llvm::BasicBlock::Create(ctx.context, "invoke_cont", currentFunc);
            std::vector<llvm::Value*> rawArgs;
            for (auto arg : argVals)
                rawArgs.push_back(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(arg)));
            llvm::Value* invRes = ctx.builder.CreateInvoke(
                f, invokeCont, ctx.catchBlock, rawArgs, funcName + "_inv");
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
    // Channel Methods: chan<T>.send / chan<T>.recv / chan<T>.init
    else if (className.rfind("chan<", 0) == 0 && className.back() == '>') {
        std::string elemName = className.substr(5, className.length() - 6); // skip "chan<", drop ">"
        llvm::Type* elemTy = static_cast<llvm::Type*>(
            ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
        if (!elemTy) elemTy = ctx.builder.getInt64Ty();

        llvm::Value* rawAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));

        if (methodName == "send" && !argVals.empty()) {
            llvm::Value* val = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            ChanImpl::send(ctx, rawAlloca, val, elemTy);
            return nullptr;
        } else if (methodName == "recv") {
            return ctx.ir_gen->wrap(ChanImpl::recv(ctx, rawAlloca, elemTy));
        } else if (methodName == "init") {
            llvm::Value* cap = argVals.empty()
                ? ctx.builder.getInt64(1)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            if (cap->getType()->isIntegerTy(32))
                cap = ctx.builder.CreateZExt(cap, ctx.builder.getInt64Ty(), "cap_i64");
            ChanImpl::init(ctx, rawAlloca, cap);
            return nullptr;
        }
        LOG_ERROR("Unknown channel method: " + methodName);
        return ctx.ir_gen->getInt32(0);
    }
    // ── Store<T> methods ─────────────────────────────────────────────────────
    else if (className.rfind("Store<", 0) == 0 && className.back() == '>') {
        std::string elemName = className.substr(6, className.size() - 7);
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Value* storePtr = ctx.builder.CreateLoad(i8p,
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), "store_ptr");

        if (methodName == "insert" && !call->args.empty()) {
            // Need raw address of entity element, not loaded value
            llvm::Value* elemPtr = nullptr;
            if (auto* varE = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get())) {
                Symbol* varSym = resolveVariable(varE->name);
                if (varSym) {
                    llvm::Value* allocaV = varSym->alloca_inst
                        ? static_cast<llvm::Value*>(varSym->alloca_inst)
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(varSym->ir_alloca_inst));
                    elemPtr = ctx.builder.CreateBitCast(allocaV, i8p);
                }
            }
            if (!elemPtr && !argVals.empty()) {
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
                if (v->getType()->isPointerTy()) {
                    elemPtr = ctx.builder.CreateBitCast(v, i8p);
                } else {
                    llvm::AllocaInst* tmp = ctx.builder.CreateAlloca(v->getType(), nullptr, "ins_tmp");
                    ctx.builder.CreateStore(v, tmp);
                    elemPtr = ctx.builder.CreateBitCast(tmp, i8p);
                }
            }
            if (!elemPtr) return ctx.ir_gen->getInt32(-1);

            int64_t elemSize = 8;
            if (ctx.classes.count(elemName) && ctx.classes[elemName].type) {
                llvm::DataLayout dl(&ctx.module);
                elemSize = static_cast<int64_t>(dl.getTypeAllocSize(ctx.classes[elemName].type));
            } else if (ctx.classes.count(elemName) && ctx.classes[elemName].packed_size > 0) {
                elemSize = static_cast<int64_t>(ctx.classes[elemName].packed_size);
            }
            llvm::Function* f = ctx.getFunction("aiml_db_store_insert", i32, {i8p, i8p, i64});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {storePtr, elemPtr,
                ctx.builder.getInt64(elemSize)}));

        } else if (methodName == "get" && !argVals.empty()) {
            llvm::Value* idVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            llvm::Function* f = ctx.getFunction("aiml_db_store_get", i8p, {i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {storePtr, idVal}));

        } else if (methodName == "delete" && !argVals.empty()) {
            llvm::Value* idVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            llvm::Function* f = ctx.getFunction("aiml_db_store_delete",
                ctx.builder.getVoidTy(), {i8p, i32});
            ctx.builder.CreateCall(f, {storePtr, idVal});
            return nullptr;

        } else if (methodName == "len") {
            llvm::Function* f = ctx.getFunction("aiml_db_store_len", i32, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {storePtr}));
        }
        LOG_ERROR("Unknown Store method: " + methodName);
        return ctx.ir_gen->getInt32(0);
    }
    // ── Kv<K:V> methods ──────────────────────────────────────────────────────
    else if (className.rfind("Kv<", 0) == 0 && className.back() == '>') {
        std::string inner = className.substr(3, className.size() - 4);
        size_t colon = inner.find(':');
        std::string vName = (colon != std::string::npos) ? inner.substr(colon + 1) : "string";

        int64_t valSize = 0;
        if      (vName == "string") valSize = 0;
        else if (vName == "i32")    valSize = 4;
        else if (vName == "i64")    valSize = 8;
        else if (vName == "f32")    valSize = 4;
        else if (vName == "f64")    valSize = 8;
        else if (ctx.classes.count(vName) && ctx.classes[vName].type) {
            llvm::DataLayout dl(&ctx.module);
            valSize = static_cast<int64_t>(dl.getTypeAllocSize(ctx.classes[vName].type));
        }

        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Value* kvPtr = ctx.builder.CreateLoad(i8p,
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), "kv_ptr");

        if (methodName == "set" && argVals.size() >= 2) {
            llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            llvm::Value* valVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]));
            llvm::Value* valPtr = nullptr;
            if (valSize == 0) {
                valPtr = valVal; // string: pass char* directly
            } else {
                llvm::AllocaInst* tmp = ctx.builder.CreateAlloca(valVal->getType(), nullptr, "kv_val_tmp");
                ctx.builder.CreateStore(valVal, tmp);
                valPtr = ctx.builder.CreateBitCast(tmp, i8p);
            }
            llvm::Function* f = ctx.getFunction("aiml_db_kv_set",
                ctx.builder.getVoidTy(), {i8p, i8p, i8p, i64});
            ctx.builder.CreateCall(f, {kvPtr, keyVal, valPtr, ctx.builder.getInt64(valSize)});
            return nullptr;

        } else if (methodName == "get" && !argVals.empty()) {
            llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            llvm::Function* f = ctx.getFunction("aiml_db_kv_get", i8p, {i8p, i8p});
            llvm::Value* retPtr = ctx.builder.CreateCall(f, {kvPtr, keyVal});
            if (valSize == 0) {
                return ctx.ir_gen->wrap(retPtr); // string: char* directly
            } else if (vName == "f32") {
                return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ctx.builder.getFloatTy(), retPtr, "kv_f32"));
            } else if (vName == "f64") {
                return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ctx.builder.getDoubleTy(), retPtr, "kv_f64"));
            } else if (vName == "i64") {
                return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ctx.builder.getInt64Ty(), retPtr, "kv_i64"));
            } else if (vName == "i32") {
                return ctx.ir_gen->wrap(ctx.builder.CreateLoad(i32, retPtr, "kv_i32"));
            }
            return ctx.ir_gen->wrap(retPtr);

        } else if (methodName == "has" && !argVals.empty()) {
            llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            llvm::Function* f = ctx.getFunction("aiml_db_kv_has", i32, {i8p, i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {kvPtr, keyVal}));

        } else if (methodName == "len") {
            llvm::Function* f = ctx.getFunction("aiml_db_kv_len", i32, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {kvPtr}));
        }
        LOG_ERROR("Unknown Kv method: " + methodName);
        return ctx.ir_gen->getInt32(0);
    }
    // ── VecStore methods ──────────────────────────────────────────────────────
    else if (className == "VecStore") {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
        llvm::Value* vsPtr = ctx.builder.CreateLoad(i8p,
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), "vs_ptr");

        // Helper: get alloca for a VariableExpr argument
        auto getArgAlloca = [&](size_t argIdx) -> llvm::Value* {
            if (argIdx >= call->args.size()) return nullptr;
            if (auto* varE = dynamic_cast<AIML::AST::VariableExpr*>(call->args[argIdx].get())) {
                Symbol* sym = resolveVariable(varE->name);
                if (sym) {
                    return sym->alloca_inst
                        ? static_cast<llvm::Value*>(sym->alloca_inst)
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst));
                }
            }
            return nullptr;
        };

        // Helper: get data ptr and size from list<f32> alloca
        auto getListDataAndSize = [&](llvm::Value* listAlloca)
                -> std::pair<llvm::Value*, llvm::Value*> {
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* sizeFld = ctx.builder.CreateStructGEP(listTy, listAlloca, 0, "vsz_fld");
            llvm::Value* listSz  = ctx.builder.CreateLoad(i32, sizeFld, "list_sz");
            llvm::Value* dataFld = ctx.builder.CreateStructGEP(listTy, listAlloca, 2, "vdata_fld");
            llvm::Value* dataPtr = ctx.builder.CreateLoad(ptrTy, dataFld, "data_ptr");
            return {dataPtr, listSz};
        };

        if (methodName == "insert" && call->args.size() >= 2) {
            llvm::Value* idStr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            llvm::Value* embedAlloca = getArgAlloca(1);
            if (!embedAlloca) return ctx.ir_gen->getInt32(-1);
            auto [dataPtr, listSz] = getListDataAndSize(embedAlloca);
            llvm::Function* f = ctx.getFunction("aiml_db_vec_insert",
                ctx.builder.getVoidTy(), {i8p, i8p, ptrTy, i32});
            ctx.builder.CreateCall(f, {vsPtr, idStr, dataPtr, listSz});
            return nullptr;

        } else if (methodName == "search" && call->args.size() >= 2) {
            llvm::Value* queryAlloca = getArgAlloca(0);
            if (!queryAlloca) return ctx.ir_gen->getInt32(-1);
            auto [queryPtr, querySz] = getListDataAndSize(queryAlloca);
            (void)querySz;
            llvm::Value* kVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]));

            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* listAlloca  = ctx.builder.CreateAlloca(listTy, nullptr, "vecres_alloca");
            ListImpl::initList(ctx, listAlloca, ptrTy, 8);

            llvm::Function* f = ctx.getFunction("aiml_db_vec_search",
                ctx.builder.getVoidTy(), {i8p, ptrTy, i32, ptrTy});
            ctx.builder.CreateCall(f, {vsPtr, queryPtr, kVal, listAlloca});
            return ctx.ir_gen->wrap(listAlloca);
        }
        LOG_ERROR("Unknown VecStore method: " + methodName);
        return ctx.ir_gen->getInt32(0);
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

// ---------------------------------------------------------------------------
// sys.serialize / sys.deserialize
// Intercepted before eager arg evaluation so we can inspect argument types.
// ---------------------------------------------------------------------------
IRValue* Compiler::visitSysSerializeCall(const std::string& method, CallExpr* call) {
    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();

    if (method == "serialize") {
        if (call->args.empty())
            return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));

        // Resolve argument type and symbol from the symbol table
        std::string argType;
        Symbol* argSym = nullptr;
        if (auto* var = dynamic_cast<VariableExpr*>(call->args[0].get())) {
            argSym = resolveVariable(var->name);
            if (argSym) argType = argSym->typeName;
        }

        // any → compiler error
        if (argType == "any") {
            LOG_ERROR("sys.serialize: 'any' type is not serialisable");
            return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr("(error)"));
        }

        // ptr<T> → auto-deref: load the pointer value, serialize as T
        if (argType.rfind("ptr<", 0) == 0 && argType.back() == '>') {
            std::string innerType = argType.substr(4, argType.size() - 5);
            if (!SerializeImpl::isSerializable(ctx, innerType)) {
                LOG_ERROR("sys.serialize: ptr<" + innerType + "> inner type is not serialisable");
                return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr("(error)"));
            }
            if (argSym) {
                IRValue* baseAlloca = argSym->ir_alloca_inst
                    ? argSym->ir_alloca_inst : ctx.ir_gen->wrap(argSym->alloca_inst);
                llvm::Value* ptrVal = ctx.builder.CreateLoad(
                    llvm::PointerType::get(ctx.context, 0),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), "deref_ptr");
                return ctx.ir_gen->wrap(SerializeImpl::serialize(ctx, ptrVal, innerType));
            }
        }

        // Validate serializability
        if (!argType.empty() && !SerializeImpl::isSerializable(ctx, argType)) {
            LOG_ERROR("sys.serialize: type '" + argType + "' is not serialisable");
            return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr("(error)"));
        }

        // Structs, lists, maps: pass alloca pointer directly (not loaded value)
        bool needsAlloca = !argType.empty() && argSym &&
            (ctx.classes.count(argType) ||
             argType.find("list<") == 0 ||
             argType.find("map<")  == 0);

        if (needsAlloca) {
            IRValue* baseAlloca = argSym->ir_alloca_inst
                ? argSym->ir_alloca_inst : ctx.ir_gen->wrap(argSym->alloca_inst);
            llvm::Value* rawAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
            return ctx.ir_gen->wrap(SerializeImpl::serialize(ctx, rawAlloca, argType));
        }

        // Scalars and enums: evaluate expression to get the loaded value
        IRValue* argVal = visitExpression(call->args[0].get());
        if (!argVal) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
        llvm::Value* val = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVal));

        return ctx.ir_gen->wrap(SerializeImpl::serialize(ctx, val, argType));

    } else if (method == "deserialize") {
        // Requires at least one argument (the source string).
        if (call->args.empty()) {
            LOG_ERROR("sys.deserialize: requires at least one argument (source string)");
            return ctx.ir_gen->getInt32(0);
        }

        // Type is taken from explicit 2nd string-literal arg, or inferred from
        // the set-statement target variable type (ctx.deserializeTargetType).
        std::string typeName;
        if (call->args.size() > 1) {
            if (auto* lit = dynamic_cast<LiteralExpr*>(call->args[1].get()))
                typeName = lit->value;
            else {
                LOG_ERROR("sys.deserialize: second argument must be a string literal type name");
                return ctx.ir_gen->getInt32(0);
            }
        } else {
            typeName = ctx.deserializeTargetType;
        }

        if (typeName.empty()) {
            LOG_ERROR("sys.deserialize: type cannot be inferred — provide a second argument or use inside a typed set statement");
            return ctx.ir_gen->getInt32(0);
        }

        IRValue* strVal = visitExpression(call->args[0].get());
        if (!strVal) return ctx.ir_gen->getInt32(0);
        llvm::Value* src = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(strVal));

        // any → compiler error
        if (typeName == "any") {
            LOG_ERROR("sys.deserialize: 'any' type is not serialisable");
            return ctx.ir_gen->getInt32(0);
        }

        // ptr<T> → deserialize as T, return heap pointer directly
        if (typeName.rfind("ptr<", 0) == 0 && typeName.back() == '>') {
            std::string innerType = typeName.substr(4, typeName.size() - 5);
            if (!SerializeImpl::isSerializable(ctx, innerType)) {
                LOG_ERROR("sys.deserialize: ptr<" + innerType + "> inner type is not serialisable");
                return ctx.ir_gen->getInt32(0);
            }
            return ctx.ir_gen->wrap(SerializeImpl::deserialize(ctx, src, innerType));
        }

        // Validate type
        if (!SerializeImpl::isSerializable(ctx, typeName)) {
            LOG_ERROR("sys.deserialize: type '" + typeName + "' is not serialisable");
            return ctx.ir_gen->getInt32(0);
        }

        return ctx.ir_gen->wrap(SerializeImpl::deserialize(ctx, src, typeName));
    }

    return ctx.ir_gen->getInt32(0);
}

// ---------------------------------------------------------------------------
// sys.bundle helpers (file-scope, not members)
// ---------------------------------------------------------------------------
namespace {

// DFS post-order: dependencies are inserted before the node that references them.
static void dfsCollect(CompilerContext& ctx,
                       const std::string& name,
                       std::set<std::string>& visited,
                       std::vector<std::string>& result) {
    if (!visited.insert(name).second) return;

    if (ctx.functions.count(name)) {
        auto& fi = ctx.functions[name];
        for (auto& dep : fi.referencedTypes) dfsCollect(ctx, dep, visited, result);
        if (!fi.sourceYaml.empty()) result.push_back(fi.sourceYaml);
    } else if (ctx.classes.count(name)) {
        auto& ci = ctx.classes[name];
        if (!ci.isBuiltin) {
            for (auto& dep : ci.referencedTypes) dfsCollect(ctx, dep, visited, result);
            // Also pull in types used inside method bodies (tracked via bundleTrackingTarget)
            for (const auto& mname : ci.methodNames) {
                if (ctx.functions.count(mname)) {
                    for (auto& dep : ctx.functions[mname].referencedTypes)
                        dfsCollect(ctx, dep, visited, result);
                }
            }
            if (!ci.sourceYaml.empty()) result.push_back(ci.sourceYaml);
        }
    } else if (ctx.enumSources.count(name)) {
        result.push_back(ctx.enumSources[name]);
    } else if (ctx.interfaceSources.count(name)) {
        result.push_back(ctx.interfaceSources[name]);
    }
}

// Transitively collect YAML source strings for rootName.
// Dependencies come before the root in the result vector (post-order DFS).
static std::vector<std::string> collectCodeChunks(CompilerContext& ctx,
                                                   const std::string& rootName) {
    std::set<std::string>    visited;
    std::vector<std::string> result;
    dfsCollect(ctx, rootName, visited, result);
    return result;
}

// FNV-1a 32-bit hash, returned as 8-char hex string.
static std::string fnv1a_hex(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%08x", h);
    return std::string(buf);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// sys.bundle intercept
// ---------------------------------------------------------------------------
IRValue* Compiler::visitSysBundleCall(CallExpr* call) {
    if (call->args.empty()) {
        LOG_ERROR("sys.bundle: requires 1 argument (function name or instance)");
        return ctx.ir_gen->getInt32(0);
    }

    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
    llvm::Type* i64 = ctx.builder.getInt64Ty();

    // Helper: malloc a new empty GenericList, init it, return raw i8*
    llvm::StructType* listTy  = ListImpl::getListStructType(ctx);
    llvm::DataLayout  dl(&ctx.module);
    uint64_t          listBytes = dl.getTypeAllocSize(listTy);
    llvm::Function*   mallocFn  = ctx.getFunction("malloc", i8p, {i64});

    auto makeEmptyList = [&]() -> llvm::Value* {
        llvm::Value* mem = ctx.builder.CreateCall(mallocFn,
            {ctx.builder.getInt64(listBytes)}, "bl_mem");
        llvm::Value* lp = ctx.builder.CreateBitCast(mem, listTy->getPointerTo(), "bl_ptr");
        ListImpl::initList(ctx, lp, i8p, 4);
        return mem; // returned as i8*
    };

    // ── Case 1: string literal → function/class bundle ─────────────────────
    if (auto* lit = dynamic_cast<LiteralExpr*>(call->args[0].get())) {
        std::string selector = lit->value;
        std::vector<std::string> chunks = collectCodeChunks(ctx, selector);

        if (chunks.empty()) {
            LOG_ERROR("sys.bundle: no YAML source found for '" + selector + "'");
            return ctx.ir_gen->getInt32(0);
        }

        // id = FNV-1a of all chunks joined
        std::string allChunks;
        for (auto& c : chunks) allChunks += c;
        llvm::Value* idVal = ctx.builder.CreateGlobalStringPtr(fnv1a_hex(allChunks), "b_id");

        // code_chunks list: push each YAML chunk
        llvm::Value* ccMem  = makeEmptyList();
        llvm::Value* ccList = ctx.builder.CreateBitCast(ccMem, listTy->getPointerTo(), "b_ccl");
        for (auto& chunk : chunks) {
            llvm::Value* s = ctx.builder.CreateGlobalStringPtr(chunk, "b_chunk");
            ListImpl::push(ctx, ccList, s);
        }

        // data_chunks: empty
        llvm::Value* dcMem = makeEmptyList();

        llvm::Value* startCall = ctx.builder.CreateGlobalStringPtr(
            "{\"call\":[\"" + selector + "\"]}", "b_sc");

        // Check for options argument: sys.bundle("fn", {context: true})
        int8_t newCtxVal = 0;
        if (call->args.size() >= 2) {
            if (auto* dict = dynamic_cast<DictLiteralExpr*>(call->args[1].get())) {
                auto it = dict->entries.find("context");
                if (it != dict->entries.end() && it->second == "true")
                    newCtxVal = 1;
            }
        }
        llvm::Value* newCtxF = ctx.builder.getInt8(newCtxVal);
        return ctx.ir_gen->wrap(CodeImpl::create(ctx, idVal, startCall, ccMem, dcMem, newCtxF));
    }

    // ── Case 2: variable → instance bundle ─────────────────────────────────
    if (auto* var = dynamic_cast<VariableExpr*>(call->args[0].get())) {
        Symbol* sym = resolveVariable(var->name);
        if (!sym) {
            LOG_ERROR("sys.bundle: unknown variable '" + var->name + "'");
            return ctx.ir_gen->getInt32(0);
        }
        std::string typeName = sym->typeName;

        std::vector<std::string> chunks = collectCodeChunks(ctx, typeName);
        if (chunks.empty()) {
            LOG_ERROR("sys.bundle: no YAML source found for type '" + typeName + "'");
            return ctx.ir_gen->getInt32(0);
        }

        // code_chunks list
        llvm::Value* ccMem  = makeEmptyList();
        llvm::Value* ccList = ctx.builder.CreateBitCast(ccMem, listTy->getPointerTo(), "bi_ccl");
        for (auto& chunk : chunks) {
            llvm::Value* s = ctx.builder.CreateGlobalStringPtr(chunk, "bi_chunk");
            ListImpl::push(ctx, ccList, s);
        }

        // id
        std::string allChunks;
        for (auto& c : chunks) allChunks += c;
        llvm::Value* idVal = ctx.builder.CreateGlobalStringPtr(fnv1a_hex(allChunks), "bi_id");

        // Serialize the instance to JSON at runtime
        IRValue* baseAlloca = sym->ir_alloca_inst
            ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst);
        llvm::Value* rawAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
        llvm::Value* jsonStr   = SerializeImpl::serialize(ctx, rawAlloca, typeName);

        // Build data chunk: "- decl: [_v, <T>]\n- set: [_v, {call:[sys.deserialize,'<json>','<T>']}]\n"
        // prefix + jsonStr + suffix are concatenated at runtime
        std::string prefix = "- decl: [_v, " + typeName + "]\n"
                             "- set: [_v, {call:[sys.deserialize,'";
        std::string suffix = "','" + typeName + "']}]\n";

        llvm::Value* prefixVal = ctx.builder.CreateGlobalStringPtr(prefix, "bi_pfx");
        llvm::Value* suffixVal = ctx.builder.CreateGlobalStringPtr(suffix, "bi_sfx");
        llvm::Value* part1     = StringUtils::concat(ctx, prefixVal, jsonStr);
        llvm::Value* dataChunk = StringUtils::concat(ctx, part1, suffixVal);

        // data_chunks list: push the single data chunk
        llvm::Value* dcMem  = makeEmptyList();
        llvm::Value* dcList = ctx.builder.CreateBitCast(dcMem, listTy->getPointerTo(), "bi_dcl");
        ListImpl::push(ctx, dcList, dataChunk);

        llvm::Value* startCall = ctx.builder.CreateGlobalStringPtr("", "bi_sc");

        llvm::Value* newCtxI = ctx.builder.getInt8(0); // default: shared context
        return ctx.ir_gen->wrap(CodeImpl::create(ctx, idVal, startCall, ccMem, dcMem, newCtxI));
    }

    LOG_ERROR("sys.bundle: argument must be a string literal (function name) or a variable (instance)");
    return ctx.ir_gen->getInt32(0);
}

// ---------------------------------------------------------------------------
// sys.exec intercept
// ---------------------------------------------------------------------------
IRValue* Compiler::visitSysExecCall(CallExpr* call) {
    if (call->args.empty()) {
        LOG_ERROR("sys.exec: requires exactly 1 argument (Code object)");
        return ctx.ir_gen->getInt32(-2);
    }

    // Validate arg type: must be a Code variable
    if (auto* var = dynamic_cast<VariableExpr*>(call->args[0].get())) {
        Symbol* sym = resolveVariable(var->name);
        if (!sym || sym->typeName != "Code") {
            LOG_ERROR("sys.exec: argument must be a Code variable (got: " +
                      (sym ? sym->typeName : "unknown") + ")");
            return ctx.ir_gen->getInt32(-1);
        }
    }

    // Evaluate the Code argument (load the i8* pointer from alloca)
    IRValue* codeVal = visitExpression(call->args[0].get());
    if (!codeVal) {
        LOG_ERROR("sys.exec: failed to evaluate Code argument");
        return ctx.ir_gen->getInt32(-1);
    }

    // Emit call to __aiml_code_exec(i8*) → i32 via CodeImpl::exec
    llvm::Value* codePtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(codeVal));
    return ctx.ir_gen->wrap(CodeImpl::exec(ctx, codePtr));
}

} // namespace AST
} // namespace AIML
