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

        // db.run_system — ECS system loop, intercept before eager arg eval
        if (baseName == "db" && methodName == "run_system") {
            return visitDbRunSystem(call);
        }

        // Pre-parse db namespace option bags before arg eval.
        // Supports both DictLiteralExpr (from nodeToExpr path) and UnaryExpr
        // (from event-based ExpressionBuilder: {key: val} → UnaryExpr(key, val)).
        ctx.dbOpenPersist      = false;
        ctx.dbOpenColumnLayout = false;
        ctx.dbOpenBasePathVal  = nullptr;
        ctx.dbQueryStoreType   = "";
        ctx.dbQueryWhere       = "";
        ctx.dbQueryOrderBy     = "";
        ctx.dbQueryOrderDir    = 0;
        ctx.dbQueryLimit       = -1;
        ctx.dbQueryOffset      = 0;
        if (baseName == "db") {
            // For db.query: capture the store argument's type so order_by can look up field offsets.
            if (methodName == "query" && !call->args.empty()) {
                if (auto* var = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get())) {
                    Symbol* sym = resolveVariable(var->name);
                    if (sym) ctx.dbQueryStoreType = sym->typeName;
                }
            }
            // Helper: apply a single key=value option pair
            auto applyDbOpt = [&](const std::string& key, const std::string& val) {
                if      (key == "persist"  && val == "true")  ctx.dbOpenPersist = true;
                else if (key == "layout"   && val == "column") ctx.dbOpenColumnLayout = true;
                else if (key == "where")    ctx.dbQueryWhere   = val;
                else if (key == "order_by") ctx.dbQueryOrderBy = val;
                else if (key == "dir")      ctx.dbQueryOrderDir = (val == "desc") ? 1 : 0;
                else if (key == "limit")  { try { ctx.dbQueryLimit  = std::stoi(val); } catch (...) {} }
                else if (key == "offset") { try { ctx.dbQueryOffset = std::stoi(val); } catch (...) {} }
            };
            for (size_t i = 1; i < call->args.size(); i++) {
                if (auto* d = dynamic_cast<AIML::AST::DictLiteralExpr*>(call->args[i].get())) {
                    // nodeToExpr path: unrecognized map → DictLiteralExpr
                    for (auto& [k, v] : d->entries) {
                        if (k == "path")
                            ctx.dbOpenBasePathVal = ctx.builder.CreateGlobalStringPtr(v);
                        else
                            applyDbOpt(k, v);
                    }
                } else if (auto* u = dynamic_cast<AIML::AST::UnaryExpr*>(call->args[i].get())) {
                    // ExpressionBuilder path: {key: val} → UnaryExpr(key, LiteralExpr/VariableExpr)
                    if (u->op == "path") {
                        // Evaluate operand as expression to get the runtime i8* value
                        IRValue* pv = visitExpression(u->operand.get());
                        if (pv) ctx.dbOpenBasePathVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(pv));
                    } else {
                        std::string val;
                        if (auto* lit  = dynamic_cast<AIML::AST::LiteralExpr*>(u->operand.get()))
                            val = lit->value;
                        else if (auto* var = dynamic_cast<AIML::AST::VariableExpr*>(u->operand.get()))
                            val = var->name;
                        if (!val.empty()) applyDbOpt(u->op, val);
                    }
                }
            }
        }

        // Eagerly evaluate arguments; skip option bags (DictLiteralExpr or db UnaryExpr options).
        static const std::set<std::string> kDbOpts = {
            "persist","layout","where","order_by","dir","limit","offset","path"};
        std::vector<IRValue*> argVals;
        for (auto& arg : call->args) {
            if (dynamic_cast<AIML::AST::DictLiteralExpr*>(arg.get())) continue;
            if (baseName == "db") {
                if (auto* u = dynamic_cast<AIML::AST::UnaryExpr*>(arg.get()))
                    if (kDbOpts.count(u->op)) continue;
            }
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

    // Builtin class dispatch (buffer, process, http handles, ...)
    {
        auto classIt = ctx.classes.find(className);
        if (classIt != ctx.classes.end() && classIt->second.isBuiltin) {
            auto& bm = classIt->second.builtinMethods;

            // If preLoadBase, load the handle ptr from alloca before invoking the handler.
            // HTTP handles use this. Existing builtins (socket, ws, etc.) load internally.
            llvm::Value* rawAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
            llvm::Value* baseForHandler = classIt->second.preLoadBase
                ? ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), rawAlloca, "h_base")
                : rawAlloca;

            // Direct method match
            auto methodIt = bm.find(methodName);
            if (methodIt != bm.end()) {
                auto evalExpr = [this](AIML::AST::Expr* e) -> IRValue* {
                    return this->visitExpression(e);
                };
                return methodIt->second(ctx, baseForHandler, call, evalExpr);
            }

            // Chained method dispatch: methodName contains a dot (e.g., "headers.put" on resp)
            // The first segment is a zero-arg field accessor, the rest is dispatched on the result.
            size_t chainDot = methodName.find('.');
            if (chainDot != std::string::npos) {
                std::string fieldName = methodName.substr(0, chainDot);
                std::string subMethod = methodName.substr(chainDot + 1);
                auto fieldIt = bm.find(fieldName);
                if (fieldIt != bm.end()) {
                    auto dummyEval = [](AIML::AST::Expr*) -> IRValue* { return nullptr; };
                    IRValue* fieldWrapped = fieldIt->second(ctx, baseForHandler, nullptr, dummyEval);
                    llvm::Value* fieldVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(fieldWrapped));

                    // Evaluate call args eagerly
                    std::vector<llvm::Value*> argRaw;
                    for (auto& arg : call->args) {
                        IRValue* av = visitExpression(arg.get());
                        if (av) argRaw.push_back(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(av)));
                    }

                    // Dispatch map operations on the field value (AimlHttpMap*)
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    if (subMethod == "put" && argRaw.size() >= 2) {
                        llvm::Function* fn = ctx.module.getFunction("aiml_http_map_put");
                        if (!fn) fn = llvm::Function::Create(
                            llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p, i8p}, false),
                            llvm::Function::ExternalLinkage, "aiml_http_map_put", ctx.module);
                        ctx.builder.CreateCall(fn, {fieldVal, argRaw[0], argRaw[1]});
                        return nullptr;
                    } else if (subMethod == "get" && argRaw.size() >= 1) {
                        llvm::Function* fn = ctx.module.getFunction("aiml_http_map_get");
                        if (!fn) fn = llvm::Function::Create(
                            llvm::FunctionType::get(i8p, {i8p, i8p}, false),
                            llvm::Function::ExternalLinkage, "aiml_http_map_get", ctx.module);
                        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {fieldVal, argRaw[0]}));
                    } else if (subMethod == "len") {
                        llvm::Function* fn = ctx.module.getFunction("aiml_http_map_len");
                        if (!fn) fn = llvm::Function::Create(
                            llvm::FunctionType::get(ctx.builder.getInt32Ty(), {i8p}, false),
                            llvm::Function::ExternalLinkage, "aiml_http_map_len", ctx.module);
                        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {fieldVal}));
                    }
                    LOG_ERROR("Unknown chained sub-method: " + subMethod + " on field " + fieldName);
                    return ctx.ir_gen->getInt32(0);
                }
            }

            LOG_ERROR("Unknown builtin method: " + methodName + " on " + className);
            return ctx.ir_gen->getInt32(0);
        }
    }

    // buffer<T>: normalize for existing builtinMethods (len, alloc, write, …)
    // and dispatch typed push / get / ptr / byte_len here.
    if (className.rfind("buffer<", 0) == 0 && className.back() == '>') {
        std::string elemName = className.substr(7, className.size() - 8);
        llvm::Type* elemTy   = ctx.getLLVMType(elemName);
        llvm::Type* bufTy    = ctx.getLLVMType("buffer");
        llvm::Type* ptrTy    = llvm::PointerType::get(ctx.context, 0);
        llvm::Value* buf     = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));

        // Eagerly evaluate args for these methods
        std::vector<IRValue*> bArgVals;
        for (auto& arg : call->args) {
            IRValue* v = visitExpression(arg.get());
            if (v) bArgVals.push_back(v);
        }

        if (methodName == "push" && !bArgVals.empty()) {
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(bArgVals[0]));
            // Coerce to elem type (e.g. double literal → float)
            if (elemTy && v->getType() != elemTy) {
                if (elemTy->isFloatingPointTy() && v->getType()->isFloatingPointTy())
                    v = ctx.builder.CreateFPCast(v, elemTy, "fp_cast");
                else if (elemTy->isIntegerTy() && v->getType()->isIntegerTy())
                    v = ctx.builder.CreateZExtOrTrunc(v, elemTy, "int_cast");
            }
            llvm::Type* useTy = elemTy ? elemTy : v->getType();
            BufferImpl::write(ctx, buf, v, useTy);
            return ctx.ir_gen->getInt32(0);

        } else if (methodName == "get" && !bArgVals.empty()) {
            llvm::Value* idxVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(bArgVals[0]));
            if (!elemTy) { LOG_ERROR("buffer<T>.get: unknown elem type " + elemName); return ctx.ir_gen->getInt32(0); }
            llvm::DataLayout dl(&ctx.module);
            uint64_t esz    = dl.getTypeAllocSize(elemTy);
            llvm::Value* idx64    = ctx.builder.CreateZExtOrTrunc(idxVal, ctx.builder.getInt64Ty(), "bidx64");
            llvm::Value* byteOff  = ctx.builder.CreateMul(idx64, ctx.builder.getInt64(esz), "byte_off");
            llvm::Value* dataFld  = ctx.builder.CreateStructGEP(bufTy, buf, 4, "data_fld");
            llvm::Value* dataPtr  = ctx.builder.CreateLoad(ptrTy, dataFld, "data_ptr");
            llvm::Value* elemPtr  = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), dataPtr, byteOff, "elem_ptr");
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(elemTy, elemPtr, "elem_val"));

        } else if (methodName == "len" || methodName == "byte_len") {
            llvm::Value* szPtr = ctx.builder.CreateStructGEP(bufTy, buf, 0, "sz_ptr");
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), szPtr, "buf_len"));

        } else if (methodName == "ptr") {
            llvm::Value* dataFld = ctx.builder.CreateStructGEP(bufTy, buf, 4, "data_fld");
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ptrTy, dataFld, "data_ptr"));

        } else if (methodName == "append" && !bArgVals.empty()) {
            // append(other: buffer<T>) — memcpy other's data into this buffer
            llvm::Value* src = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(bArgVals[0]));
            // src is alloca of the other buffer; get its size and data
            llvm::Value* srcSzPtr  = ctx.builder.CreateStructGEP(bufTy, src, 0, "src_sz_ptr");
            llvm::Value* srcSz     = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), srcSzPtr, "src_sz");
            llvm::Value* srcDataFl = ctx.builder.CreateStructGEP(bufTy, src, 4, "src_data_fld");
            llvm::Value* srcData   = ctx.builder.CreateLoad(ptrTy, srcDataFl, "src_data");
            // Write each byte via memcpy: get current write pointer
            llvm::Value* wIdxPtr   = ctx.builder.CreateStructGEP(bufTy, buf, 3, "w_idx_ptr");
            llvm::Value* wIdx      = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), wIdxPtr, "w_idx");
            llvm::Value* dataFld   = ctx.builder.CreateStructGEP(bufTy, buf, 4, "data_fld");
            llvm::Value* dataPtr   = ctx.builder.CreateLoad(ptrTy, dataFld, "data_ptr");
            llvm::Value* dstPtr    = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), dataPtr, ctx.builder.CreateZExt(wIdx, ctx.builder.getInt64Ty()), "dst_ptr");
            llvm::Function* mcf    = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.builder.getInt64Ty()});
            ctx.builder.CreateCall(mcf, {dstPtr, srcData, ctx.builder.CreateZExt(srcSz, ctx.builder.getInt64Ty())});
            // Update write index and size
            llvm::Value* newW = ctx.builder.CreateAdd(wIdx, srcSz, "new_w");
            ctx.builder.CreateStore(newW, wIdxPtr);
            llvm::Value* szPtr  = ctx.builder.CreateStructGEP(bufTy, buf, 0, "sz_ptr");
            llvm::Value* curSz  = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), szPtr, "cur_sz");
            llvm::Value* newSz  = ctx.builder.CreateSelect(ctx.builder.CreateICmpSGT(newW, curSz), newW, curSz);
            ctx.builder.CreateStore(newSz, szPtr);
            return ctx.ir_gen->getInt32(0);

        } else {
            // Fall through to normalized builtin dispatch for alloc/write/read/seek/read_versioned
            std::string normName = "buffer";
            auto classIt = ctx.classes.find(normName);
            if (classIt != ctx.classes.end()) {
                auto methodIt = classIt->second.builtinMethods.find(methodName);
                if (methodIt != classIt->second.builtinMethods.end()) {
                    auto evalExpr = [this](AIML::AST::Expr* e) -> IRValue* { return this->visitExpression(e); };
                    return methodIt->second(ctx, buf, call, evalExpr);
                }
            }
        }
        LOG_ERROR("Unknown method: " + methodName + " on " + className);
        return ctx.ir_gen->getInt32(0);
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

        } else if (methodName == "update" && call->args.size() >= 2) {
            // args: [id (i32), elem (struct)] — id already in argVals[0], elem needs alloca
            llvm::Value* idVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            llvm::Value* elemPtr = nullptr;
            if (auto* varE = dynamic_cast<AIML::AST::VariableExpr*>(call->args[1].get())) {
                Symbol* varSym = resolveVariable(varE->name);
                if (varSym) {
                    llvm::Value* allocaV = varSym->alloca_inst
                        ? static_cast<llvm::Value*>(varSym->alloca_inst)
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(varSym->ir_alloca_inst));
                    elemPtr = ctx.builder.CreateBitCast(allocaV, i8p);
                }
            }
            if (!elemPtr && argVals.size() >= 2) {
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]));
                if (v->getType()->isPointerTy()) {
                    elemPtr = ctx.builder.CreateBitCast(v, i8p);
                } else {
                    llvm::AllocaInst* tmp = ctx.builder.CreateAlloca(v->getType(), nullptr, "upd_tmp");
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
            llvm::Function* f = ctx.getFunction("aiml_db_store_update", i32, {i8p, i32, i8p, i64});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
                {storePtr, idVal, elemPtr, ctx.builder.getInt64(elemSize)}));

        } else if (methodName == "get" && !argVals.empty()) {
            llvm::Value* idVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            llvm::Function* f = ctx.getFunction("aiml_db_store_get", i8p, {i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {storePtr, idVal}));

        } else if (methodName == "get_by_key" && !argVals.empty()) {
            if (!ctx.classes.count(elemName) || ctx.classes[elemName].primaryKey.empty() ||
                !ctx.classes[elemName].members.count(ctx.classes[elemName].primaryKey)) {
                LOG_ERROR("get_by_key: no primary_key defined for " + elemName);
                return ctx.ir_gen->getInt32(0);
            }
            auto& ci       = ctx.classes[elemName];
            auto& pkMember = ci.members[ci.primaryKey];
            int32_t keyOffset = 0, keySize = 0;
            if (ci.type) {
                llvm::DataLayout dl(&ctx.module);
                auto* sl  = dl.getStructLayout(llvm::cast<llvm::StructType>(ci.type));
                keyOffset = (int32_t)sl->getElementOffset((unsigned)pkMember.index);
                keySize   = (int32_t)dl.getTypeAllocSize(pkMember.type);
            }
            int32_t isStr = (pkMember.typeName == "string") ? 1 : 0;

            llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            llvm::Value* keyPtr;
            if (keyVal->getType()->isPointerTy()) {
                keyPtr = keyVal;
            } else {
                llvm::AllocaInst* tmp = ctx.builder.CreateAlloca(
                    keyVal->getType(), nullptr, "pk_tmp");
                ctx.builder.CreateStore(keyVal, tmp);
                keyPtr = ctx.builder.CreateBitCast(tmp, i8p);
            }
            llvm::Function* f = ctx.getFunction("aiml_db_store_get_by_key",
                i8p, {i8p, i8p, i32, i32, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                storePtr, keyPtr,
                ctx.builder.getInt32(keyOffset),
                ctx.builder.getInt32(keySize),
                ctx.builder.getInt32(isStr)
            }));

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

        } else if (methodName == "delete" && !argVals.empty()) {
            llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            llvm::Function* f = ctx.getFunction("aiml_db_kv_delete",
                ctx.builder.getVoidTy(), {i8p, i8p});
            ctx.builder.CreateCall(f, {kvPtr, keyVal});
            return nullptr;

        } else if (methodName == "clear") {
            llvm::Function* f = ctx.getFunction("aiml_db_kv_clear",
                ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(f, {kvPtr});
            return nullptr;
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

        } else if (methodName == "len") {
            llvm::Function* f = ctx.getFunction("aiml_db_vec_len", i32, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {vsPtr}));
        }
        LOG_ERROR("Unknown VecStore method: " + methodName);
        return ctx.ir_gen->getInt32(0);
    }
    // ── TimeSeries<T> methods ─────────────────────────────────────────────────
    else if (className.rfind("TimeSeries<", 0) == 0 && className.back() == '>') {
        std::string elemName = className.substr(11, className.size() - 12);
        llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32t  = ctx.builder.getInt32Ty();
        llvm::Type* i64t  = ctx.builder.getInt64Ty();
        llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
        llvm::Value* tsPtr = ctx.builder.CreateLoad(i8p,
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), "ts_ptr");

        // Helper: get alloca for VariableExpr arg (same pattern as store.insert)
        auto getTsArgAlloca = [&](size_t argIdx) -> llvm::Value* {
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

        // Compute elem size from class info
        int64_t elemSize = 8;
        if (ctx.classes.count(elemName) && ctx.classes[elemName].type) {
            llvm::DataLayout dl(&ctx.module);
            elemSize = (int64_t)dl.getTypeAllocSize(ctx.classes[elemName].type);
        }

        if (methodName == "append" && call->args.size() >= 2) {
            llvm::Value* elemPtr = nullptr;
            if (llvm::Value* alloca = getTsArgAlloca(0))
                elemPtr = ctx.builder.CreateBitCast(alloca, i8p);
            if (!elemPtr && !argVals.empty()) {
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
                if (v->getType()->isPointerTy()) {
                    elemPtr = ctx.builder.CreateBitCast(v, i8p);
                } else {
                    llvm::AllocaInst* tmp = ctx.builder.CreateAlloca(v->getType(), nullptr, "ts_tmp");
                    ctx.builder.CreateStore(v, tmp);
                    elemPtr = ctx.builder.CreateBitCast(tmp, i8p);
                }
            }
            if (!elemPtr) return ctx.ir_gen->getInt32(-1);

            // timestamp arg (may come as i32 — cast to i64)
            llvm::Value* tsVal = argVals.size() >= 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]))
                : ctx.builder.getInt64(0);
            if (tsVal->getType() != i64t)
                tsVal = ctx.builder.CreateZExtOrTrunc(tsVal, i64t, "ts64");

            llvm::Function* f = ctx.getFunction("aiml_db_ts_append",
                ctx.builder.getVoidTy(), {i8p, i8p, i64t, i64t});
            ctx.builder.CreateCall(f, {tsPtr, elemPtr,
                ctx.builder.getInt64(elemSize), tsVal});
            return nullptr;

        } else if (methodName == "at" && !argVals.empty()) {
            llvm::Value* tsVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            if (tsVal->getType() != i64t)
                tsVal = ctx.builder.CreateZExtOrTrunc(tsVal, i64t, "at64");
            llvm::Function* f = ctx.getFunction("aiml_db_ts_at", i8p, {i8p, i64t});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tsPtr, tsVal}));

        } else if (methodName == "range" && argVals.size() >= 2) {
            llvm::Value* fromV = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            llvm::Value* toV   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]));
            if (fromV->getType() != i64t) fromV = ctx.builder.CreateZExtOrTrunc(fromV, i64t);
            if (toV->getType()   != i64t) toV   = ctx.builder.CreateZExtOrTrunc(toV,   i64t);

            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* listAlloca  = ctx.builder.CreateAlloca(listTy, nullptr, "tsrange_alloca");
            ListImpl::initList(ctx, listAlloca, ptrTy, 8);

            llvm::Function* f = ctx.getFunction("aiml_db_ts_range",
                ctx.builder.getVoidTy(), {i8p, i64t, i64t, ptrTy});
            ctx.builder.CreateCall(f, {tsPtr, fromV, toV, listAlloca});
            return ctx.ir_gen->wrap(listAlloca);

        } else if (methodName == "len") {
            llvm::Function* f = ctx.getFunction("aiml_db_ts_len", i32t, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tsPtr}));
        }
        LOG_ERROR("Unknown TimeSeries method: " + methodName);
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
            IRValue* result = funcIt->second(ctx, argVals);
            // Void handlers return nullptr — promote to i32(0) so the caller
            // knows the method was found and handled (nullptr = "not found").
            return result ? result : ctx.ir_gen->getInt32(0);
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

// ─────────────────────────────────────────────────────────────────────────────
// db.run_system — ECS intersection loop
//
// Syntax: - call: [db.run_system, system_fn, store1, store2, ...]
//
// The compiler:
//   1. Extracts fn name from arg[0] (VariableExpr)
//   2. Looks up the LLVM function, checks if first param is i32 (eid)
//   3. For each store arg, resolves the store pointer
//   4. Generates a loop over slot 1..max_total, calling fn for every slot
//      that is active in ALL stores simultaneously.
// ─────────────────────────────────────────────────────────────────────────────
IRValue* Compiler::visitDbRunSystem(CallExpr* call) {
    if (call->args.empty()) {
        LOG_ERROR("db.run_system: requires at least one argument (function name)");
        return ctx.ir_gen->getInt32(0);
    }

    // arg[0] must be the system function name (VariableExpr)
    std::string fnName;
    if (auto* v = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get()))
        fnName = v->name;
    else {
        LOG_ERROR("db.run_system: first arg must be a function name");
        return ctx.ir_gen->getInt32(0);
    }

    llvm::Function* sysFn = ctx.module.getFunction(fnName);
    if (!sysFn) {
        LOG_ERROR("db.run_system: function '" + fnName + "' not found in module");
        return ctx.ir_gen->getInt32(0);
    }

    // Does the fn start with an eid (i32) parameter?
    llvm::FunctionType* fnTy = sysFn->getFunctionType();
    bool hasEid = (fnTy->getNumParams() > 0 &&
                   fnTy->getParamType(0)->isIntegerTy(32));

    int numStoreArgs = (int)call->args.size() - 1;
    if (numStoreArgs < 1) {
        LOG_ERROR("db.run_system: need at least one store argument");
        return ctx.ir_gen->getInt32(0);
    }

    llvm::Type* i8p  = llvm::PointerType::getUnqual(ctx.context);
    llvm::Type* i32t = ctx.builder.getInt32Ty();

    // Resolve store pointers (load from variable alloca)
    std::vector<llvm::Value*> storePtrs;
    for (int i = 1; i < (int)call->args.size(); i++) {
        if (auto* varE = dynamic_cast<AIML::AST::VariableExpr*>(call->args[i].get())) {
            Symbol* sym = resolveVariable(varE->name);
            if (sym) {
                llvm::Value* alloca = sym->alloca_inst
                    ? static_cast<llvm::Value*>(sym->alloca_inst)
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst));
                storePtrs.push_back(ctx.builder.CreateLoad(i8p, alloca, varE->name + "_sptr"));
                continue;
            }
        }
        IRValue* v = visitExpression(call->args[i].get());
        if (!v) return ctx.ir_gen->getInt32(0);
        storePtrs.push_back(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v)));
    }

    // Declare runtime helpers
    llvm::Function* totalFn    = ctx.getFunction("aiml_db_store_total",     i32t, {i8p});
    llvm::Function* isActiveFn = ctx.getFunction("aiml_db_store_is_active", i32t, {i8p, i32t});
    llvm::Function* getPtrFn   = ctx.getFunction("aiml_db_store_get_ptr",   i8p,  {i8p, i32t});

    // Compute max total across stores (loop bound)
    llvm::Value* maxTotal = ctx.builder.getInt32(0);
    for (auto* sp : storePtrs) {
        llvm::Value* t   = ctx.builder.CreateCall(totalFn, {sp}, "st_total");
        llvm::Value* cmp = ctx.builder.CreateICmpSGT(t, maxTotal, "is_bigger");
        maxTotal = ctx.builder.CreateSelect(cmp, t, maxTotal, "max_total");
    }

    // Alloca for loop counter (1-based slot index)
    llvm::AllocaInst* eidAlloca = ctx.builder.CreateAlloca(i32t, nullptr, "rs_eid");
    ctx.builder.CreateStore(ctx.builder.getInt32(1), eidAlloca);
    llvm::AllocaInst* maxAlloca = ctx.builder.CreateAlloca(i32t, nullptr, "rs_max");
    ctx.builder.CreateStore(maxTotal, maxAlloca);

    // Basic blocks
    llvm::Function* parentFn   = ctx.builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* hdrBB    = llvm::BasicBlock::Create(ctx.context, "rs_hdr",  parentFn);
    llvm::BasicBlock* checkBB  = llvm::BasicBlock::Create(ctx.context, "rs_chk",  parentFn);
    llvm::BasicBlock* callBB   = llvm::BasicBlock::Create(ctx.context, "rs_call", parentFn);
    llvm::BasicBlock* nextBB   = llvm::BasicBlock::Create(ctx.context, "rs_next", parentFn);
    llvm::BasicBlock* exitBB   = llvm::BasicBlock::Create(ctx.context, "rs_exit", parentFn);

    ctx.builder.CreateBr(hdrBB);

    // rs_hdr: eid <= max?
    ctx.builder.SetInsertPoint(hdrBB);
    {
        llvm::Value* eid = ctx.builder.CreateLoad(i32t, eidAlloca, "eid");
        llvm::Value* mx  = ctx.builder.CreateLoad(i32t, maxAlloca,  "mx");
        llvm::Value* ok  = ctx.builder.CreateICmpSLE(eid, mx, "in_range");
        ctx.builder.CreateCondBr(ok, checkBB, exitBB);
    }

    // rs_chk: all stores active at eid?
    ctx.builder.SetInsertPoint(checkBB);
    {
        llvm::Value* eid = ctx.builder.CreateLoad(i32t, eidAlloca, "eid");
        llvm::Value* allOk = ctx.builder.getInt32(1);
        for (auto* sp : storePtrs) {
            llvm::Value* a = ctx.builder.CreateCall(isActiveFn, {sp, eid}, "active");
            allOk = ctx.builder.CreateAnd(allOk, a, "all_ok");
        }
        llvm::Value* allBool = ctx.builder.CreateICmpNE(allOk, ctx.builder.getInt32(0), "all_bool");
        ctx.builder.CreateCondBr(allBool, callBB, nextBB);
    }

    // rs_call: get component ptrs, call system fn
    ctx.builder.SetInsertPoint(callBB);
    {
        llvm::Value* eid = ctx.builder.CreateLoad(i32t, eidAlloca, "eid");
        std::vector<llvm::Value*> fnArgs;
        if (hasEid) fnArgs.push_back(eid);
        for (auto* sp : storePtrs) {
            llvm::Value* ptr = ctx.builder.CreateCall(getPtrFn, {sp, eid}, "comp_ptr");
            fnArgs.push_back(ptr);
        }
        ctx.builder.CreateCall(sysFn, fnArgs);
        ctx.builder.CreateBr(nextBB);
    }

    // rs_next: eid++
    ctx.builder.SetInsertPoint(nextBB);
    {
        llvm::Value* eid  = ctx.builder.CreateLoad(i32t, eidAlloca, "eid");
        llvm::Value* next = ctx.builder.CreateAdd(eid, ctx.builder.getInt32(1), "eid_next");
        ctx.builder.CreateStore(next, eidAlloca);
        ctx.builder.CreateBr(hdrBB);
    }

    // rs_exit: continue after loop
    ctx.builder.SetInsertPoint(exitBB);
    return nullptr;
}

} // namespace AST
} // namespace AIML
