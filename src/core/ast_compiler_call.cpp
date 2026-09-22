#define DEBUG_MODE
#include "ast_compiler.h"
#include "list_impl.h"
#include "map_impl.h"
#include "buffer_impl.h"
#include "chan_impl.h"
#include "code_impl.h"
#include "string_utils.h"
#include "serialize_impl.h"
#include "../../src/sys/db_runtime.h"
#include <iostream>
#include <queue>
#include <set>
#include <cstdio>

namespace AIML {
namespace AST {

// Reconstruct an expression back to single-line AIML flow-YAML.  Function
// bodies are built by the event-based parser, which keeps only a source Mark
// (no DOM) on each node — so YAML::Dump(sourceNode) is empty.  We render from
// the AST instead.  Used by the deprecation warning so the full call node is
// emitted greppable / parse-and-replaceable by downstream tooling.
static std::string exprToAiml(const Expr* e) {
    if (!e) return "null";
    if (auto* lit = dynamic_cast<const LiteralExpr*>(e)) return lit->value;
    if (auto* var = dynamic_cast<const VariableExpr*>(e)) return var->name;
    if (auto* call = dynamic_cast<const CallExpr*>(e)) {
        std::string s = "{ call: [" + call->functionName;
        for (const auto& a : call->args) s += ", " + exprToAiml(a.get());
        return s + "] }";
    }
    if (auto* nw = dynamic_cast<const NewExpr*>(e)) return "{ new: " + nw->typeName + " }";
    if (auto* at = dynamic_cast<const AtExpr*>(e)) {
        std::string s = "{ at: [" + exprToAiml(at->container.get());
        for (const auto& idx : at->indices) s += ", " + exprToAiml(idx.get());
        return s + "] }";
    }
    if (auto* bin = dynamic_cast<const BinaryExpr*>(e))
        return "{ " + bin->op + ": [" + exprToAiml(bin->lhs.get()) + ", " +
               exprToAiml(bin->rhs.get()) + "] }";
    if (auto* un = dynamic_cast<const UnaryExpr*>(e))
        return "{ " + un->op + ": [" + exprToAiml(un->operand.get()) + "] }";
    if (auto* dl = dynamic_cast<const DictLiteralExpr*>(e)) {
        std::string s = "{"; bool first = true;
        for (const auto& kv : dl->entriesExpr) {
            s += (first ? " " : ", ") + kv.first + ": " + exprToAiml(kv.second.get());
            first = false;
        }
        return s + " }";
    }
    if (dynamic_cast<const StringInterpolationExpr*>(e)) return "\"...\"";
    return e->toString();
}

// ── fn<params:ret> type parser ────────────────────────────────────────────────
// Handles nested generics correctly, e.g. fn<map<string:string>:bool>
struct FnTypeInfo {
    std::vector<std::string> paramTypes;
    std::string retType; // empty = void
};
static FnTypeInfo parseFnType(const std::string& typeName) {
    // Strip "fn<" (3 chars) and ">" (1 char)
    std::string inner = typeName.substr(3, typeName.size() - 4);

    // Find the last ":" at angle-bracket depth 0 — that separates params from return type
    int depth = 0, retSep = -1;
    for (int i = 0; i < (int)inner.size(); ++i) {
        if      (inner[i] == '<') ++depth;
        else if (inner[i] == '>') --depth;
        else if (inner[i] == ':' && depth == 0) retSep = i;
    }

    std::string paramStr = (retSep >= 0) ? inner.substr(0, retSep) : inner;
    FnTypeInfo info;
    info.retType = (retSep >= 0) ? inner.substr(retSep + 1) : "";

    // Split paramStr by "," at depth 0
    if (!paramStr.empty()) {
        depth = 0;
        std::string cur;
        auto push = [&]() {
            while (!cur.empty() && cur.front() == ' ') cur.erase(cur.begin());
            while (!cur.empty() && cur.back()  == ' ') cur.pop_back();
            if (!cur.empty()) info.paramTypes.push_back(cur);
            cur.clear();
        };
        for (char c : paramStr) {
            if      (c == '<') { ++depth; cur += c; }
            else if (c == '>') { --depth; cur += c; }
            else if (c == ',' && depth == 0) push();
            else cur += c;
        }
        push();
    }
    return info;
}

IRValue* Compiler::visitCall(CallExpr* call) {
    std::string funcName = call->functionName;

    // ── __builtin_tolerance(a, b, eps) → i1: |a - b| <= eps ──────────────
    if (funcName == "__builtin_tolerance" && call->args.size() == 3) {
        IRValue* aVal = visitExpression(call->args[0].get());
        IRValue* bVal = visitExpression(call->args[1].get());
        IRValue* epsVal = visitExpression(call->args[2].get());
        if (!aVal || !bVal || !epsVal) return nullptr;
        llvm::Value* a = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(aVal));
        llvm::Value* b = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(bVal));
        llvm::Value* eps = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(epsVal));
        // Promote to float if needed
        auto toF32 = [&](llvm::Value* v) -> llvm::Value* {
            if (v->getType()->isFloatTy()) return v;
            if (v->getType()->isDoubleTy()) return ctx.builder.CreateFPTrunc(v, ctx.builder.getFloatTy());
            if (v->getType()->isIntegerTy()) return ctx.builder.CreateSIToFP(v, ctx.builder.getFloatTy());
            return v;
        };
        a = toF32(a); b = toF32(b); eps = toF32(eps);
        // |a - b| <= eps  →  fabs(a-b) <= eps
        llvm::Value* diff = ctx.builder.CreateFSub(a, b, "tol_diff");
        // fabs via: diff >= 0 ? diff : -diff
        llvm::Value* neg = ctx.builder.CreateFNeg(diff, "tol_neg");
        llvm::Value* isPos = ctx.builder.CreateFCmpOGE(diff, llvm::ConstantFP::get(ctx.builder.getFloatTy(), 0.0));
        llvm::Value* absDiff = ctx.builder.CreateSelect(isPos, diff, neg, "tol_abs");
        llvm::Value* cmp = ctx.builder.CreateFCmpOLE(absDiff, eps, "tol_cmp");
        return ctx.ir_gen->wrap(cmp);
    }

    // Normalize <. self-reference to this. (same as visitVariable)
    if (funcName.find("<.") == 0) {
        funcName = "this." + funcName.substr(2);
    }
    size_t dotPos = funcName.find('.');
    
    // Check for lazy-evaluated method arguments before typical eager evaluation
    if (dotPos != std::string::npos) {
        size_t lastDotPos = funcName.rfind('.');
        std::string baseName = funcName.substr(0, dotPos);
        std::string methodName = funcName.substr(dotPos + 1);

        // Prefer the deepest receiver split for chained receivers such as:
        //   root.nodes.len
        //   root.nodes-0.stats.cost.get
        // This reuses the existing recursive property/write resolver instead
        // of adding another ad hoc chain parser in the call path.
        if (lastDotPos != std::string::npos && lastDotPos > 0) {
            std::string receiverExpr = funcName.substr(0, lastDotPos);
            std::string finalMethod  = funcName.substr(lastDotPos + 1);

            size_t recvSep = receiverExpr.find_first_of(".-");
            std::string receiverBase = (recvSep == std::string::npos)
                ? receiverExpr
                : receiverExpr.substr(0, recvSep);

            Symbol* receiverSym = resolveVariable(receiverBase);
            if (receiverSym) {
                if (recvSep == std::string::npos) {
                    return visitObjectMethodCall(receiverExpr, finalMethod, receiverSym, call);
                }

                std::string receiverPath = receiverExpr.substr(recvSep + 1);
                std::string recvTypeName;
                IRValue* receiverPtr = resolvePropertyWritePtr(receiverSym, receiverPath, recvTypeName);
                if (receiverPtr) {
                    Symbol fieldSym;
                    fieldSym.typeName = recvTypeName;
                    fieldSym.ir_alloca_inst = receiverPtr;

                    llvm::Type* recvTy = ctx.getLLVMType(recvTypeName);
                    if (recvTy) {
                        fieldSym.ir_element_type = ctx.ir_gen->wrapType(recvTy);
                        fieldSym.element_type = recvTy;
                    }
                    return visitObjectMethodCall(receiverExpr, finalMethod, &fieldSym, call);
                }
            }
        }
        
        Symbol* sym = resolveVariable(baseName);
        if (sym) {
            return visitObjectMethodCall(baseName, methodName, sym, call);
        }
        
        // sys.serialize / sys.deserialize / sys.bundle need arg type info — intercept before eager eval
        if (baseName == "sys" && (methodName == "serialize" || methodName == "deserialize")) {
            return visitSysSerializeCall(methodName, call);
        }
        if (baseName == "sys" && methodName == "clone") {
            return visitSysCloneCall(call);
        }
        if (baseName == "sys" && methodName == "copy") {
            // spec/61 §5.1: shallow copy. list<T> copies are intercepted at the decl (handle
            // path); for other types a shallow copy of a value with only shared/immutable
            // referents coincides with the deep-copy machinery, so route there for now.
            return visitSysCloneCall(call);
        }
        if (baseName == "sys" && methodName == "bundle") {
            return visitSysBundleCall(call);
        }
        if (baseName == "sys" && methodName == "exec") {
            return visitSysExecCall(call);
        }
        if (baseName == "sys" && methodName == "arg_dump") {
            return visitSysArgDumpCall(call);
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
                        if (k == "path") {
                            // v may be a variable name (e.g. path: db_path) or a literal string
                            Symbol* sym = resolveVariable(v);
                            if (sym) {
                                // Evaluate v as a variable expression to get the runtime i8* value
                                VariableExpr tmp(v);
                                IRValue* pv = visitExpression(&tmp);
                                if (pv) ctx.dbOpenBasePathVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(pv));
                            } else {
                                ctx.dbOpenBasePathVal = ctx.builder.CreateGlobalStringPtr(v);
                            }
                        } else {
                            applyDbOpt(k, v);
                        }
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

        // Spec 46: inject source location as hidden 2nd argument for log.* calls
        if (baseName == "log" && methodName != "set_level" && methodName != "set_file") {
            std::string loc = ctx.currentSourceFile;
            if (call->sourceNode.IsDefined() && call->sourceNode.Mark().line >= 0)
                loc += ":" + std::to_string(call->sourceNode.Mark().line + 1);
            else
                loc += ":?";
            llvm::Value* locStr = ctx.builder.CreateGlobalStringPtr(loc, "log_loc");
            argVals.push_back(ctx.ir_gen->wrap(locStr));
        }

        // Dispatch any registered namespace (sys, io, time, path, and future ones)
        if (ctx.namespaces.count(baseName)) {
            AIML::AST::CallExpr* prevCall = ctx.currentCall;
            ctx.currentCall = call;
            IRValue* sysResult = visitSysNamespaceCall(baseName, methodName, argVals);
            ctx.currentCall = prevCall;
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
        
        LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "Unknown method or namespace function: " + funcName);
        return ctx.ir_gen->getInt32(0);
    }

    // Eagerly evaluate arguments for system functions and standard functions
    std::vector<IRValue*> argVals;
    for (size_t i = 0; i < call->args.size(); ++i) {
        IRValue* val = visitExpression(call->args[i].get());
        if (val) {
            argVals.push_back(val);
        } else {
            LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "Failed to evaluate argument " + std::to_string(i) + " for call to: " + funcName);
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
         if (argVals.empty()) return ctx.ir_gen->getInt32(0);
         // spec/61 §10 (memV3): `free` is only for a ptr<T> new-box (and the raw dyn_array /
         // iterator handles). A shared<T> is released by refcount, a ref<T> never owns, and a
         // stack value is auto-released at scope — freeing any of those is a compile error. Now
         // reachable because the `free:` statement materializes as a node under memV3.
         if (ctx.memV3) {
             if (auto* fv = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get())) {
                 Symbol* fs = (fv->name.find('.') == std::string::npos) ? resolveVariable(fv->name) : nullptr;
                 if (fs) {
                     const std::string& tn = fs->typeName;
                     if (tn.rfind("shared<", 0) == 0)
                         LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "free of a shared<T> ('" + fv->name +
                             "'): a shared value is released by refcount at rc 0, never freed (§10/§6.2). "
                             "Let it drop at scope exit.");
                     else if (tn.rfind("ref<", 0) == 0)
                         LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "free of a ref<T> ('" + fv->name +
                             "'): a borrow never owns and must not be freed (§10).");
                     else if (tn.rfind("ptr<", 0) != 0 && tn.rfind("dyn_array<", 0) != 0
                              && tn.rfind("iterator<", 0) != 0)
                         LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "free of a non-heap value '" + fv->name +
                             "' of type '" + tn + "': only a ptr<T> new-box is released with free (§10). "
                             "Stack values are released automatically at scope exit.");
                 }
             }
         }
         // iterator<T> handles are allocated with C++ `new` (aiml_iter_create_kv)
         // → must be released via aiml_iter_free (delete), not libc free.  Mixing
         // new/free is UB (see spec/54 §5).  Detect the arg's declared type.
         if (auto* itVarE = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get())) {
             Symbol* itSym = resolveVariable(itVarE->name);
             if (itSym && itSym->typeName.rfind("iterator<", 0) == 0) {
                 llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                 llvm::Function* itFree = ctx.getFunction("aiml_iter_free", ctx.builder.getVoidTy(), {i8p});
                 llvm::Value* itPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
                 if (itPtr->getType() != i8p) itPtr = ctx.builder.CreateBitCast(itPtr, i8p);
                 ctx.builder.CreateCall(itFree, {itPtr});
                 return ctx.ir_gen->getInt32(0);
             }
         }
         llvm::Function* freeFunc = ctx.module.getFunction("free");
         if (!freeFunc) freeFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "free", ctx.module);
         llvm::Value* ptrVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
         if (ptrVal->getType() != ctx.builder.getInt8Ty()->getPointerTo()) {
             ptrVal = ctx.builder.CreateBitCast(ptrVal, ctx.builder.getInt8Ty()->getPointerTo());
         }
         // spec/61 §6.1 (memV3): `free` on a ptr<T> box must run the class-based release — deep-free
         // the pointee's owned fields (strings decref, list/map/buffer handles release, owning ptr
         // children followed, nested value models recursed) BEFORE freeing the block. Mirrors the
         // kind-4 scope-drop (emitScopeDrop). Without this, `free:` freed only the shell and every
         // heap field leaked (probe 09). OFF is unaffected (the `free:` node only exists under memV3).
         if (ctx.memV3) {
             if (auto* fv = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get())) {
                 Symbol* fs = (fv->name.find('.') == std::string::npos) ? resolveVariable(fv->name) : nullptr;
                 if (fs && fs->typeName.rfind("ptr<", 0) == 0 && fs->typeName.back() == '>') {
                     std::string boxed = fs->typeName.substr(4, fs->typeName.size() - 5);
                     auto bi = ctx.classes.find(boxed);
                     if (bi != ctx.classes.end() && bi->second.hasHeapFields && bi->second.serNodesVar
                         && bi->second.type && !ctx.classGroupContainers.count(boxed)
                         && !ctx.cascadeClasses.count(boxed)) {
                         auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                         llvm::Function* dfree = ctx.getFunction("aiml_ser_free_decoded",
                             ctx.builder.getVoidTy(), {i8p, i8p, ctx.builder.getInt32Ty()});
                         ctx.builder.CreateCall(dfree, { ptrVal,
                             static_cast<llvm::Value*>(bi->second.serNodesVar),
                             ctx.builder.getInt32(bi->second.serNodesCount) });
                     }
                 }
             }
         }
         ctx.builder.CreateCall(freeFunc, {ptrVal});
         // spec/61 §6.1 (memV3): `free` is an EARLY release — null the slot so the scope-default
         // drop (kind 4/7, null-safe) becomes a no-op instead of double-freeing. Only when the
         // operand is a plain local we can address. (Under memV3 the `free:` node only exists at
         // all — OFF leaves the statement dropped, byte-identical.)
         if (ctx.memV3) {
             if (auto* fv = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get())) {
                 if (fv->name.find('.') == std::string::npos) {
                     Symbol* fs = resolveVariable(fv->name);
                     if (fs) {
                         llvm::Value* slot = fs->ir_alloca_inst
                             ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(fs->ir_alloca_inst))
                             : fs->alloca_inst;
                         auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                         if (slot) ctx.builder.CreateStore(
                             llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)), slot);
                     }
                 }
             }
         }
         return ctx.ir_gen->getInt32(0);
    }
    
    // Normal Call — prefer indirect call via fptable_current for hot-reload support
    IRValue* callRes = nullptr;
    CompilerContext::FunctionInfo* funcInfo = resolveFunction(funcName);
    llvm::Function* f = funcInfo ? funcInfo->func : ctx.module.getFunction(funcName);

    // meta: [deprecated] → warn at this call site (one per usage location).
    // Emit the full call YAML node so callers can grep/parse+replace the output.
    if (funcInfo && funcInfo->deprecated) {
        LOG_WARNING(ctx.fmtLoc(call->sourceNode) + "'" + funcName +
                    "' is deprecated — " + exprToAiml(call));
    }

    // spec/61 §3.1 caller-move: an argument passed to a `consumed` parameter is MOVED into
    // the callee — the caller must no longer free it.  Remove its pending scope-drop (safe:
    // consumeLocalDrop only drops a matching plain-local's drop, so at worst a leak, never a
    // double-free).  All consumed params are pointer/handle types (validated at the def), so
    // the callee receives the same handle — a true move, not a copy.
    if (funcInfo && ctx.lifetimeDropsEnabled && !funcInfo->argLifetimes.empty()) {
        for (size_t i = 0; i < call->args.size() && i < funcInfo->argLifetimes.size(); ++i) {
            if (funcInfo->argLifetimes[i] != "consumed") continue;
            consumeLocalDrop(call->args[i].get());                 // moved-out local: no scope-drop
            // A directly-passed interpolation temp ("x-${i}") is also moved into the callee —
            // remove it from the statement-temp decref list, else the caller frees it at stmt end
            // AND the callee frees it → double-free/UAF (spec/61 §3.1). consumeLocalDrop misses it
            // (not a VariableExpr); consumeStringTemp matches the evaluated temp value.
            if (i < argVals.size())
                consumeStringTemp(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[i])));
        }
    }

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
                // Take the address of the caller's lvalue — plain variable OR
                // dotted member-access (out.inner).  emitLValueAddress handles
                // by-ref sources (load stored ptr) and nested field GEPs.
                std::string lvTy;
                if (IRValue* addr = emitLValueAddress(call->args[i].get(), lvTy)) {
                    actualVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(addr));
                    argVals[i] = ctx.ir_gen->wrap(actualVal);
                    actualTy   = actualVal->getType();
                }
            }
            if (expectedTy->isPointerTy() && actualTy->isPointerTy() && expectedTy != actualTy)
                argVals[i] = ctx.ir_gen->wrap(ctx.builder.CreateBitCast(actualVal, expectedTy));
            // list<T>/map<T> field access returns ptr to struct; load by value when callee expects struct
            if (expectedTy->isStructTy() && actualTy->isPointerTy())
                argVals[i] = ctx.ir_gen->wrap(ctx.builder.CreateLoad(expectedTy, actualVal, "struct_arg_load"));
            // Integer width promotion: e.g. i32 literal passed to a function expecting i64
            if (expectedTy->isIntegerTy() && actualTy->isIntegerTy() &&
                expectedTy->getIntegerBitWidth() > actualTy->getIntegerBitWidth())
                argVals[i] = ctx.ir_gen->wrap(ctx.builder.CreateZExt(actualVal, expectedTy, "arg_zext"));
            // Float promotion: f32 → f64
            if (expectedTy->isDoubleTy() && actualTy->isFloatTy())
                argVals[i] = ctx.ir_gen->wrap(ctx.builder.CreateFPExt(actualVal, expectedTy, "arg_fpext"));

            // Hard type-mismatch guard: after all coercions, a numeric parameter
            // (i32/enum/bool/float) that receives a pointer value (e.g. a string
            // literal) can NEVER be valid.  Catch it HERE with a source location
            // instead of letting the LLVM IR verifier fail later with the
            // unhelpful "'__aiml_user_main' verification FAILED".  Mirrors the
            // enum-assignment check in visitSetStmt.
            llvm::Type* finalTy =
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[i]))->getType();
            if ((expectedTy->isIntegerTy() || expectedTy->isFloatingPointTy()) &&
                finalTy->isPointerTy()) {
                std::string pname = (funcInfo && i < funcInfo->argNames.size())
                                        ? funcInfo->argNames[i]
                                        : ("arg" + std::to_string(i));
                std::string hint =
                    expectedTy->isIntegerTy()
                        ? " — passing a string/pointer where an integer- or "
                          "enum-typed argument is expected; use EnumType.<value> "
                          "or an int literal"
                        : " — passing a string/pointer where a float-typed "
                          "argument is expected";
                LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "Call to '" + funcName +
                          "': argument " + std::to_string(i + 1) + " ('" + pname +
                          "') type mismatch" + hint);
            }
        }
    };

    if (f && funcInfo && funcInfo->fptable_index >= 0) {
        // ── Indirect call via __aiml_fptable_current[idx] ────────────────────
        llvm::GlobalVariable* cur_gv =
            ctx.module.getNamedGlobal("__aiml_fptable_current");
        if (cur_gv) {
            coerceArgs();
            if (!f->isVarArg() && argVals.size() < f->arg_size())
                LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "Not enough arguments for function " + funcName);

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
            // fptable not yet emitted (e.g. during registerTypes pass) — direct call.
            // MUST still honor an active try/catch: inside a try block every call
            // has to be an `invoke` unwinding to the catch landingpad, otherwise a
            // thrown exception sails past the handler (uncaught → AOT std::terminate
            // / JIT top-level).  This branch previously emitted a plain call and
            // silently broke try/catch around any fptable-eligible user function.
            coerceArgs();
            if (ctx.catchBlock) {
                llvm::Function* cur = ctx.builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* cont =
                    llvm::BasicBlock::Create(ctx.context, "invoke_cont", cur);
                std::vector<llvm::Value*> rawArgs;
                for (auto& av : argVals)
                    rawArgs.push_back(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(av)));
                llvm::Value* invRes = ctx.builder.CreateInvoke(
                    f, cont, ctx.catchBlock, rawArgs, funcName + "_inv");
                callRes = ctx.ir_gen->wrap(invRes);
                ctx.builder.SetInsertPoint(cont);
            } else {
                callRes = ctx.ir_gen->createCall(f->getName().str(), argVals);
            }
        }
    } else if (f) {
        // ── Direct call (builtins / external / functions outside fptable) ─────
        if (!f->isVarArg() && argVals.size() < f->arg_size())
            LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "Not enough arguments for function " + funcName +
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

    // ── fn<...> variable — indirect call through stored function pointer ──────
    if (!callRes) {
        Symbol* fnSym = resolveVariable(funcName);
        if (fnSym && fnSym->typeName.rfind("fn<", 0) == 0 && fnSym->typeName.back() == '>') {
            auto* i8p = llvm::PointerType::get(ctx.context, 0);

            // Load the i8* function pointer from its alloca
            llvm::Value* fnAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
                fnSym->ir_alloca_inst ? fnSym->ir_alloca_inst
                                      : ctx.ir_gen->wrap(fnSym->alloca_inst)));
            llvm::Value* fnPtr = ctx.builder.CreateLoad(i8p, fnAlloca, funcName + "_fp");

            // Build LLVM FunctionType from the fn<params:ret> annotation
            FnTypeInfo fi = parseFnType(fnSym->typeName);
            std::vector<llvm::Type*> paramTys;
            for (auto& pt : fi.paramTypes) {
                llvm::Type* t = ctx.getLLVMType(pt);
                paramTys.push_back(t ? t : i8p);
            }
            llvm::Type* retTy = (fi.retType.empty() || fi.retType == "void")
                ? llvm::Type::getVoidTy(ctx.context)
                : (ctx.getLLVMType(fi.retType) ? ctx.getLLVMType(fi.retType) : i8p);

            llvm::FunctionType* fnTy = llvm::FunctionType::get(retTy, paramTys, false);

            std::vector<llvm::Value*> rawArgs;
            for (auto& av : argVals)
                rawArgs.push_back(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(av)));

            llvm::Value* res = ctx.builder.CreateCall(fnTy, fnPtr, rawArgs, funcName + "_icall");
            return ctx.ir_gen->wrap(res);
        }
    }

    if (!callRes) {
        LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "Unknown function: " + funcName);
        return nullptr;
    }
    // spec/61 §3.2 Part 2: a `producer` free function returning a fresh STRING, used as a TRANSIENT
    // sub-expression (e.g. `set_text(…, build_card_text(card))`), must be decref'd at statement end.
    // Register it as an owned stmt-temp; a consuming decl/set/push/return removes it via
    // consumeStringTemp (its own scope-drop then handles it) — so bound uses don't double-free.
    if (callRes && ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty()) {
        size_t dp = funcName.rfind('.');
        std::string shortName = (dp == std::string::npos) ? funcName : funcName.substr(dp + 1);
        if (ctx.stringProducerFns.count(shortName) > 0)
            ctx.stmtTempStack.back().push_back(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(callRes)));
    }
    return callRes;
}
IRValue* Compiler::visitObjectMethodCall(const std::string& baseName, const std::string& methodName, Symbol* sym, CallExpr* call) {
    // ── dyn_array<T>.free ─────────────────────────────────────────────────────
    if (sym->typeName.rfind("dyn_array<", 0) == 0 && sym->typeName.back() == '>' && methodName == "free") {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* ptrAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
            sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
        llvm::Value* rawPtr = ctx.builder.CreateLoad(i8p, ptrAlloca, "darr_free_ptr");
        llvm::Function* freeFn = ctx.module.getFunction("free");
        if (!freeFn) {
            llvm::FunctionType* ft = llvm::FunctionType::get(
                ctx.builder.getVoidTy(), {i8p}, false);
            freeFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "free", ctx.module);
        }
        ctx.builder.CreateCall(freeFn, {rawPtr});
        // spec/61 §5.6 (memV3): `.free` is an EARLY release — null the slot so the Class-4
        // scope-default drop (kind 9) becomes a null no-op (free(NULL)), preventing a double free.
        if (ctx.memV3)
            ctx.builder.CreateStore(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)), ptrAlloca);
        return ctx.ir_gen->getInt32(0);
    }

    std::string className = sym->typeName;
    if ((className.rfind("ptr<", 0) == 0 || className.rfind("ref<", 0) == 0) && className.back() == '>') {
         className = className.substr(4, className.length() - 5);
    }

    // meta: [deprecated] on a class/interface method → warn at this call site.
    {
        auto dep = ctx.functions.find(className + "_" + methodName);
        if (dep != ctx.functions.end() && dep->second.deprecated) {
            LOG_WARNING(ctx.fmtLoc(call->sourceNode) +
                        "'" + className + "." + methodName + "' is deprecated — " +
                        exprToAiml(call));
        }
    }

    IRValue* baseAlloca = sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst);
    IRType* baseElemTy = sym->ir_element_type ? sym->ir_element_type : ctx.ir_gen->wrapType(sym->element_type);

    // list/map by-ref args: alloca enthält einen pointer auf das echte struct,
    // nicht das struct selbst.  Method-dispatch erwartet baseAlloca = ptr-to-struct,
    // also einmal deref.  Spec/22_call_semantics.md.
    if (sym->is_byref_arg) {
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* allocaRaw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
        llvm::Value* loaded = ctx.builder.CreateLoad(i8p, allocaRaw, "byref_deref");
        baseAlloca = ctx.ir_gen->wrap(loaded);
    }

    // Interface virtual dispatch: if sym is ptr<Iface>, lookup method slot,
    // load vtbl from fat pointer, indirect call with obj as `this`.
    {
        auto ifaceIt = ctx.interfaces.find(className);
        if (ifaceIt != ctx.interfaces.end() && sym->typeName.rfind("ptr<", 0) == 0) {
            const auto& iface = ifaceIt->second;
            int slot = -1;
            for (size_t i = 0; i < iface.methods.size(); ++i) {
                if (iface.methods[i].name == methodName) { slot = (int)i; break; }
            }
            if (slot < 0) {
                LOG_ERROR(ctx.fmtLoc(call->sourceNode) +
                    "interface '" + className + "' has no method '" + methodName + "'");
                return ctx.ir_gen->getInt32(0);
            }
            const auto& m = iface.methods[slot];
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* fpTy = iface.fatPtrType;
            if (!fpTy) {
                LOG_ERROR(ctx.fmtLoc(call->sourceNode) +
                    "interface '" + className + "' fat-pointer type not materialised");
                return ctx.ir_gen->getInt32(0);
            }
            llvm::Value* fpPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
            llvm::Value* objField  = ctx.builder.CreateStructGEP(fpTy, fpPtr, 0, "fp_obj_p");
            llvm::Value* vtblField = ctx.builder.CreateStructGEP(fpTy, fpPtr, 1, "fp_vtbl_p");
            llvm::Value* objPtr  = ctx.builder.CreateLoad(i8p, objField,  "fp_obj");
            llvm::Value* vtblPtr = ctx.builder.CreateLoad(i8p, vtblField, "fp_vtbl");

            // Build function type from interface signature — MUST mirror visitFunction's lowering
            // (and the interface conformance check) so the indirect signature matches the real vtable
            // function: a memV3 container / refcount-model RETURN is a handle pointer (i8p), not the
            // inline struct. Otherwise a list<string> return took the >8-byte struct/sret path here
            // while the callee returned i8p → garbage data pointer (test_iface_list_return*).
            llvm::Type* retTy = nullptr;
            if (m.returnType.empty() || m.returnType == "void")
                retTy = llvm::Type::getVoidTy(ctx.context);
            else if (ctx.isRefcountModel(m.returnType) || ctx.isMemV3ContainerHandle(m.returnType))
                retTy = i8p;
            else {
                IRType* irt = ctx.ir_gen->resolveType(m.returnType);
                retTy = irt ? static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(irt)) : i8p;
            }
            std::vector<llvm::Type*> paramTys;
            paramTys.push_back(i8p); // this
            for (const auto& a : m.args) {
                const std::string& at = a.second;
                bool byref = at.rfind("list<", 0) == 0 || at.rfind("map<", 0) == 0
                             || at == "buffer" || at.rfind("buffer<", 0) == 0
                             || at.rfind("cascade<", 0) == 0;
                if (byref || ctx.isRefcountModel(at)) { paramTys.push_back(i8p); continue; }
                IRType* irt = ctx.ir_gen->resolveType(at);
                paramTys.push_back(irt ? static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(irt)) : i8p);
            }
            auto* fnTy = llvm::FunctionType::get(retTy, paramTys, false);

            // Load slot from vtbl.  Vtbl layout: [i8* method0, i8* method1, ...].
            // GEP slot then load function pointer.
            auto* slotsTy = llvm::ArrayType::get(i8p, iface.methods.size());
            llvm::Value* slotsBase = ctx.builder.CreateBitCast(vtblPtr,
                slotsTy->getPointerTo(), "vtbl_slots");
            llvm::Value* slotPtr = ctx.builder.CreateInBoundsGEP(slotsTy, slotsBase,
                {ctx.builder.getInt32(0), ctx.builder.getInt32(slot)}, "vtbl_slot");
            llvm::Value* fnPtr = ctx.builder.CreateLoad(i8p, slotPtr, "vtbl_fn");

            // spec/61 §3.1 caller-move on interface (vtable) dispatch: an argument passed
            // to a `consumed` interface-method parameter is moved into the callee — drop the
            // caller's ownership. m.argLifetimes is parallel to call->args (no implicit this).
            if (ctx.lifetimeDropsEnabled && !m.argLifetimes.empty()) {
                for (size_t ai = 0; ai < call->args.size() && ai < m.argLifetimes.size(); ++ai)
                    if (m.argLifetimes[ai] == "consumed")
                        consumeLocalDrop(call->args[ai].get());
            }

            // Eagerly eval call args.
            std::vector<llvm::Value*> callArgs;
            callArgs.push_back(objPtr);
            for (size_t ai = 0; ai < call->args.size(); ++ai) {
                IRValue* av = visitExpression(call->args[ai].get());
                if (!av) continue;
                llvm::Value* raw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(av));
                // spec/61 §3.1: a directly-passed interpolation temp moved into a consumed
                // interface-method parameter must leave the caller's statement-temp decref list.
                if (ctx.lifetimeDropsEnabled && ai < m.argLifetimes.size()
                    && m.argLifetimes[ai] == "consumed")
                    consumeStringTemp(raw);
                // "expected ptr, got struct" coercion for the vtable-dispatch
                // path — mirrors coerceArgs()/the direct-method path.  A struct
                // lvalue (plain variable OR member-access) passed where the
                // interface method declares ptr<T> must be passed by ADDRESS,
                // not loaded by value.  paramTys[0] = implicit this.
                size_t paramIdx = ai + 1;
                if (raw && raw->getType()->isStructTy() &&
                    paramIdx < paramTys.size() && paramTys[paramIdx]->isPointerTy()) {
                    std::string lvTy;
                    if (IRValue* addr = emitLValueAddress(call->args[ai].get(), lvTy)) {
                        llvm::Value* pv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(addr));
                        if (pv->getType() != paramTys[paramIdx])
                            pv = ctx.builder.CreateBitCast(pv, paramTys[paramIdx]);
                        raw = pv;
                    }
                }
                callArgs.push_back(raw);
            }

            // Struct return: vtable points to a sret-stub (see getOrCreateSretStub
            // in ast_compiler.cpp).  Match the stub's signature here — allocate
            // a temp at function entry, pass as sret out-param, load the result.
            // Direct (non-vtable) calls go straight to the real fn; LLVM auto-
            // ABI keeps caller + callee in sync for that path.
            if (retTy->isStructTy()) {
                std::vector<llvm::Type*> sretParamTys;
                sretParamTys.push_back(i8p);                       // sret slot
                for (auto* t : paramTys) sretParamTys.push_back(t);
                auto* sretFnTy = llvm::FunctionType::get(
                    llvm::Type::getVoidTy(ctx.context), sretParamTys, false);

                llvm::Function* curF = ctx.builder.GetInsertBlock()->getParent();
                auto savedIP = ctx.builder.saveIP();
                ctx.builder.SetInsertPoint(&curF->getEntryBlock(),
                                           curF->getEntryBlock().getFirstInsertionPt());
                llvm::Value* sretSlot = ctx.builder.CreateAlloca(retTy, nullptr, "iface_sret");
                ctx.builder.restoreIP(savedIP);

                std::vector<llvm::Value*> sretCallArgs;
                sretCallArgs.push_back(sretSlot);
                for (auto* v : callArgs) sretCallArgs.push_back(v);

                llvm::CallInst* ci = ctx.builder.CreateCall(sretFnTy, fnPtr, sretCallArgs);
                ci->addParamAttr(0, llvm::Attribute::getWithStructRetType(ctx.context, retTy));

                llvm::Value* loaded = ctx.builder.CreateLoad(retTy, sretSlot, "iface_call");
                return ctx.ir_gen->wrap(loaded);
            }

            // LLVM verbietet Namens-Suffix auf void-Calls, daher Name nur bei non-void.
            llvm::Value* res = ctx.builder.CreateCall(fnTy, fnPtr, callArgs,
                retTy->isVoidTy() ? "" : "iface_call");
            if (retTy->isVoidTy()) return nullptr;
            return ctx.ir_gen->wrap(res);
        }
    }

    // mirror_session.register/register_id/unregister: capture object type for builtin handler
    if (className == "mirror_session" &&
        (methodName == "register" || methodName == "register_id" || methodName == "unregister")) {
        if (!call->args.empty()) {
            if (auto* varEx = dynamic_cast<VariableExpr*>(call->args[0].get())) {
                Symbol* objSym = resolveVariable(varEx->name);
                if (objSym) ctx.deserializeTargetType = objSym->typeName;
            }
        }
    }

    // mirror_event.type_enum() → MirrorEventType (i32).  mirror_event is a
    // real struct (isBuiltin=false) so the builtinMethods path below is not
    // consulted — handle the typed-getter here by GEP'ing the type-string
    // field (0) and mapping it via the runtime.
    if (className == "mirror_event" && methodName == "type_enum") {
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* rawBase = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
        auto* baseElemRaw = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(baseElemTy));
        if (baseElemRaw->isPointerTy())
            rawBase = ctx.builder.CreateLoad(baseElemRaw, rawBase, "deref_ev");
        auto* evTy = static_cast<llvm::StructType*>(ctx.getLLVMType("mirror_event"));
        llvm::Value* typePtr = ctx.builder.CreateStructGEP(evTy, rawBase, 0, "ev_type_ptr");
        llvm::Value* typeStr = ctx.builder.CreateLoad(i8p, typePtr, "ev_type_str");
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(
            ctx.getFunction("aiml_mirror_type_enum", ctx.builder.getInt32Ty(), {i8p}),
            {typeStr}, "ev_type_enum"));
    }

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

            LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "Unknown builtin method: " + methodName + " on " + className);
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
            llvm::Value* dstPtr    = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), dataPtr, ctx.builder.CreateZExtOrTrunc(wIdx, ctx.getSizeType()), "dst_ptr");
            llvm::Function* mcf    = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
            ctx.builder.CreateCall(mcf, {dstPtr, srcData, ctx.builder.CreateZExtOrTrunc(srcSz, ctx.getSizeType())});
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
        LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "Unknown method: " + methodName + " on " + className);
        return ctx.ir_gen->getInt32(0);
    }

    // String.to_int() — no args, like len
    if (className == "string" && methodName == "to_int") {
        llvm::Function* atoiFn = ctx.module.getFunction("atoi");
        if (!atoiFn) atoiFn = llvm::Function::Create(
            llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false),
            llvm::Function::ExternalLinkage, "atoi", ctx.module);
        llvm::Value* strPtr = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(),
                                                     static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)));
        llvm::Value* zero = ctx.builder.getInt32(0);
        llvm::Value* isNull = ctx.builder.CreateIsNull(strPtr, "to_int_null");
        llvm::Function* parent = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* nullBB  = llvm::BasicBlock::Create(ctx.context, "to_int_null_bb", parent);
        llvm::BasicBlock* okBB    = llvm::BasicBlock::Create(ctx.context, "to_int_ok",      parent);
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "to_int_merge",   parent);
        ctx.builder.CreateCondBr(isNull, nullBB, okBB);
        ctx.builder.SetInsertPoint(nullBB);
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(okBB);
        llvm::Value* val = ctx.builder.CreateCall(atoiFn, {strPtr});
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(mergeBB);
        llvm::PHINode* phi = ctx.builder.CreatePHI(ctx.builder.getInt32Ty(), 2, "to_int");
        phi->addIncoming(zero, nullBB);
        phi->addIncoming(val, okBB);
        return ctx.ir_gen->wrap(phi);
    }

    // String.parse_i32() — lazy, null-safe, uses strtol (handles negative, hex-prefix etc.)
    if (className == "string" && methodName == "parse_i32") {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Function* fn = ctx.module.getFunction("aiml_str_parse_i32");
        if (!fn) fn = llvm::Function::Create(
            llvm::FunctionType::get(ctx.builder.getInt32Ty(), {i8p}, false),
            llvm::Function::ExternalLinkage, "aiml_str_parse_i32", ctx.module);
        llvm::Value* strPtr = ctx.builder.CreateLoad(i8p,
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)));
        llvm::Value* isNull = ctx.builder.CreateIsNull(strPtr, "pi32_null");
        llvm::Function* parent = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* nullBB  = llvm::BasicBlock::Create(ctx.context, "pi32_null_bb", parent);
        llvm::BasicBlock* okBB    = llvm::BasicBlock::Create(ctx.context, "pi32_ok",      parent);
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "pi32_merge",   parent);
        ctx.builder.CreateCondBr(isNull, nullBB, okBB);
        ctx.builder.SetInsertPoint(nullBB);
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(okBB);
        llvm::Value* val = ctx.builder.CreateCall(fn, {strPtr});
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(mergeBB);
        llvm::PHINode* phi = ctx.builder.CreatePHI(ctx.builder.getInt32Ty(), 2, "pi32");
        phi->addIncoming(ctx.builder.getInt32(0), nullBB);
        phi->addIncoming(val, okBB);
        return ctx.ir_gen->wrap(phi);
    }

    // String.parse_f32() — lazy, null-safe, uses strtof
    if (className == "string" && methodName == "parse_f32") {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Function* fn = ctx.module.getFunction("aiml_str_parse_f32");
        if (!fn) fn = llvm::Function::Create(
            llvm::FunctionType::get(ctx.builder.getFloatTy(), {i8p}, false),
            llvm::Function::ExternalLinkage, "aiml_str_parse_f32", ctx.module);
        llvm::Value* strPtr = ctx.builder.CreateLoad(i8p,
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)));
        llvm::Value* isNull = ctx.builder.CreateIsNull(strPtr, "pf32_null");
        llvm::Function* parent = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* nullBB  = llvm::BasicBlock::Create(ctx.context, "pf32_null_bb", parent);
        llvm::BasicBlock* okBB    = llvm::BasicBlock::Create(ctx.context, "pf32_ok",      parent);
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "pf32_merge",   parent);
        ctx.builder.CreateCondBr(isNull, nullBB, okBB);
        ctx.builder.SetInsertPoint(nullBB);
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(okBB);
        llvm::Value* val = ctx.builder.CreateCall(fn, {strPtr});
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(mergeBB);
        llvm::PHINode* phi = ctx.builder.CreatePHI(ctx.builder.getFloatTy(), 2, "pf32");
        phi->addIncoming(llvm::ConstantFP::get(ctx.builder.getFloatTy(), 0.0), nullBB);
        phi->addIncoming(val, okBB);
        return ctx.ir_gen->wrap(phi);
    }

    // String.len (lazy — needs only the alloca, no args)
    if (className == "string" && methodName == "len") {
        // size_t = target pointer-width int (i32 on WASM, i64 on x86_64).
        llvm::Type* sizeTy = ctx.module.getDataLayout().getIntPtrType(ctx.context);
        llvm::Function* strlenFunc = ctx.module.getFunction("strlen");
        if (!strlenFunc) strlenFunc = llvm::Function::Create(llvm::FunctionType::get(sizeTy, {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "strlen", ctx.module);
        llvm::Value* strPtr = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)));
        // Null-safe: if strPtr is null, return 0
        llvm::Value* isNull = ctx.builder.CreateIsNull(strPtr, "str_is_null");
        llvm::Function* parent = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* nullBB  = llvm::BasicBlock::Create(ctx.context, "str_len_null",  parent);
        llvm::BasicBlock* okBB    = llvm::BasicBlock::Create(ctx.context, "str_len_ok",    parent);
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "str_len_merge", parent);
        ctx.builder.CreateCondBr(isNull, nullBB, okBB);
        ctx.builder.SetInsertPoint(nullBB);
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(okBB);
        llvm::Value* lenN  = ctx.builder.CreateCall(strlenFunc, {strPtr});
        llvm::Value* len32 = ctx.builder.CreateSExtOrTrunc(lenN, ctx.builder.getInt32Ty());
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(mergeBB);
        llvm::PHINode* phi = ctx.builder.CreatePHI(ctx.builder.getInt32Ty(), 2, "str_len");
        phi->addIncoming(ctx.builder.getInt32(0), nullBB);
        phi->addIncoming(len32, okBB);
        return ctx.ir_gen->wrap(phi);
    }

    // Reset Kv-dispatch tracking flags. They're set inside the get/get_copy
    // branch below so visitDeclStmt can validate the decl-type pairing.
    ctx.kvGetCopy = false;
    ctx.kvGetCalled = false;
    ctx.kvGetValueType.clear();

    // Eagerly evaluate arguments for collection and class methods.
    std::vector<IRValue*> argVals;
    for (auto& arg : call->args) {
        IRValue* val = visitExpression(arg.get());
        if (val) argVals.push_back(val);
        else return ctx.ir_gen->getInt32(0);
    }

    // String methods (substr, starts_with, index_of, lower, trim, replace, split)
    if (className == "string") {
        llvm::Value* strPtr = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(),
                                                     static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)));
        if (methodName == "substr") {
            // size_t = platform pointer-width int (i32 on WASM, i64 on x86_64).
            // Linking against libc strndup with hardcoded i64 trips wasm-ld's
            // signature-mismatch check on the WASM target.
            llvm::Type* sizeTy = ctx.module.getDataLayout().getIntPtrType(ctx.context);
            llvm::Value* start = ctx.builder.CreateSExtOrTrunc(
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])), sizeTy);
            llvm::Value* offsetPtr = ctx.builder.CreateGEP(ctx.builder.getInt8Ty(), strPtr, start);
            // spec/61 §7: substr must return a REFCOUNTED string (aiml_str_alloc header at ptr-8).
            // Raw strndup/strdup gave a header-less string; binding it increfs/decrefs → reads/frees
            // (ptr-8) of an unowned block → heap corruption (test_ability_text `cost.substr`, rc139).
            // aiml_str_ndup/aiml_str_dup allocate the STRN header (size_t len matches the ABI).
            if (argVals.size() >= 2) {
                llvm::Function* ndupFn = ctx.getFunction("aiml_str_ndup",
                    ctx.builder.getInt8Ty()->getPointerTo(),
                    {ctx.builder.getInt8Ty()->getPointerTo(), sizeTy});
                llvm::Value* lenN = ctx.builder.CreateSExtOrTrunc(
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1])), sizeTy);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(ndupFn, {offsetPtr, lenN}));
            } else {
                llvm::Function* dupFn = ctx.getFunction("aiml_str_dup",
                    ctx.builder.getInt8Ty()->getPointerTo(),
                    {ctx.builder.getInt8Ty()->getPointerTo()});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(dupFn, {offsetPtr}));
            }
        } else if (methodName == "starts_with") {
            // starts_with(prefix) → i32 (1 or 0)
            // size_t must match target ABI: i32 on WASM, i64 on x86_64.
            llvm::Type* sizeTy = ctx.module.getDataLayout().getIntPtrType(ctx.context);
            llvm::Function* strncmpFn = ctx.module.getFunction("strncmp");
            if (!strncmpFn) strncmpFn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getInt32Ty(),
                    {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt8Ty()->getPointerTo(), sizeTy}, false),
                llvm::Function::ExternalLinkage, "strncmp", ctx.module);
            llvm::Function* strlenFn = ctx.module.getFunction("strlen");
            if (!strlenFn) strlenFn = llvm::Function::Create(
                llvm::FunctionType::get(sizeTy, {ctx.builder.getInt8Ty()->getPointerTo()}, false),
                llvm::Function::ExternalLinkage, "strlen", ctx.module);
            llvm::Value* prefix = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            llvm::Value* prefixLen = ctx.builder.CreateCall(strlenFn, {prefix});
            llvm::Value* cmp = ctx.builder.CreateCall(strncmpFn, {strPtr, prefix, prefixLen});
            llvm::Value* eq = ctx.builder.CreateICmpEQ(cmp, ctx.builder.getInt32(0));
            return ctx.ir_gen->wrap(ctx.builder.CreateZExt(eq, ctx.builder.getInt32Ty()));
        } else if (methodName == "ends_with") {
            // ends_with(suffix) → i32 (1 or 0)
            llvm::Type* sizeTy = ctx.module.getDataLayout().getIntPtrType(ctx.context);
            llvm::Function* strncmpFn = ctx.module.getFunction("strncmp");
            if (!strncmpFn) strncmpFn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getInt32Ty(),
                    {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt8Ty()->getPointerTo(), sizeTy}, false),
                llvm::Function::ExternalLinkage, "strncmp", ctx.module);
            llvm::Function* strlenFn = ctx.module.getFunction("strlen");
            if (!strlenFn) strlenFn = llvm::Function::Create(
                llvm::FunctionType::get(sizeTy, {ctx.builder.getInt8Ty()->getPointerTo()}, false),
                llvm::Function::ExternalLinkage, "strlen", ctx.module);
            llvm::Value* suffix = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            llvm::Value* strLen = ctx.builder.CreateCall(strlenFn, {strPtr});
            llvm::Value* suffixLen = ctx.builder.CreateCall(strlenFn, {suffix});
            // if suffixLen > strLen → 0
            llvm::Value* tooLong = ctx.builder.CreateICmpUGT(suffixLen, strLen);
            llvm::Value* offset = ctx.builder.CreateSub(strLen, suffixLen);
            llvm::Value* tailPtr = ctx.builder.CreateGEP(ctx.builder.getInt8Ty(), strPtr, {offset});
            llvm::Value* cmp = ctx.builder.CreateCall(strncmpFn, {tailPtr, suffix, suffixLen});
            llvm::Value* eq = ctx.builder.CreateICmpEQ(cmp, ctx.builder.getInt32(0));
            llvm::Value* match = ctx.builder.CreateZExt(eq, ctx.builder.getInt32Ty());
            llvm::Value* result = ctx.builder.CreateSelect(tooLong, ctx.builder.getInt32(0), match);
            return ctx.ir_gen->wrap(result);
        } else if (methodName == "index_of") {
            // index_of(needle) → i32 (-1 if not found)
            llvm::Function* strstrFn = ctx.module.getFunction("strstr");
            if (!strstrFn) strstrFn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(),
                    {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt8Ty()->getPointerTo()}, false),
                llvm::Function::ExternalLinkage, "strstr", ctx.module);
            llvm::Value* needle = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            llvm::Value* found = ctx.builder.CreateCall(strstrFn, {strPtr, needle});
            llvm::Value* isNull = ctx.builder.CreateICmpEQ(found, llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()));
            llvm::Value* offset = ctx.builder.CreatePtrDiff(ctx.builder.getInt8Ty(), found, strPtr);
            // ptrdiff_t is platform-width — SExtOrTrunc handles both i32 (WASM) and i64 (x86_64).
            llvm::Value* offset32 = ctx.builder.CreateSExtOrTrunc(offset, ctx.builder.getInt32Ty());
            llvm::Value* result = ctx.builder.CreateSelect(isNull, ctx.builder.getInt32(-1), offset32);
            return ctx.ir_gen->wrap(result);
        } else if (methodName == "lower") {
            // lower() -> string — aiml_str_lower(str)
            llvm::Function* fn = ctx.getFunction("aiml_str_lower",
                ctx.builder.getInt8Ty()->getPointerTo(),
                {ctx.builder.getInt8Ty()->getPointerTo()});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {strPtr}));
        } else if (methodName == "upper") {
            // upper() -> string — aiml_str_upper(str)
            llvm::Function* fn = ctx.getFunction("aiml_str_upper",
                ctx.builder.getInt8Ty()->getPointerTo(),
                {ctx.builder.getInt8Ty()->getPointerTo()});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {strPtr}));
        } else if (methodName == "title") {
            // title() -> string — aiml_str_title(str)
            // Capitalize first char of every word (separators: whitespace,
            // `-`, `_`); lowercase the rest.  Useful for snake_case →
            // display-label conversion.
            llvm::Function* fn = ctx.getFunction("aiml_str_title",
                ctx.builder.getInt8Ty()->getPointerTo(),
                {ctx.builder.getInt8Ty()->getPointerTo()});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {strPtr}));
        } else if (methodName == "trim") {
            // trim() -> string — aiml_str_trim(str)
            llvm::Function* fn = ctx.getFunction("aiml_str_trim",
                ctx.builder.getInt8Ty()->getPointerTo(),
                {ctx.builder.getInt8Ty()->getPointerTo()});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {strPtr}));
        } else if (methodName == "replace") {
            // replace(old, new) -> string — aiml_str_replace(str, old, new)
            if (argVals.size() < 2) return ctx.ir_gen->getInt32(0);
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* fn = ctx.getFunction("aiml_str_replace", i8p, {i8p, i8p, i8p});
            llvm::Value* oldV = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            llvm::Value* newV = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {strPtr, oldV, newV}));
        } else if (methodName == "split") {
            // split(delimiter) -> list<string>
            if (argVals.empty()) return ctx.ir_gen->getInt32(0);
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::Function* fillFn = ctx.getFunction("aiml_str_split_fill", i32ty, {i8p, i8p, ptrTy});
            llvm::Value* delimV = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            // spec/61 §5 (memV3): a `list<string>` local is a refcounted HANDLE freed via
            // aiml_list_handle_release. `split` must return a real HEAP handle — returning a pointer
            // to a STACK alloca made the binding local "own" stack memory, so its scope-drop released
            // a stack pointer (garbage rc header → free of the stack) → non-deterministic heap
            // corruption (test_auth_helpers / validate_email `.split` rc134/rc139). The handle IS the
            // {size,cap,data} struct pointer, so aiml_str_split_fill writes into it unchanged.
            if (ctx.memV3) {
                llvm::Function* nw = ctx.getFunction("aiml_list_handle_new", i8p, {});
                llvm::Value* handle = ctx.builder.CreateCall(nw, {}, "split_handle");
                ListImpl::initList(ctx, handle, i8p, 8);
                ctx.builder.CreateCall(fillFn, {strPtr, delimV, handle});
                return ctx.ir_gen->wrap(handle);
            }
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* listAlloca  = ctx.createEntryAlloca(listTy, "split_alloca");
            ListImpl::initList(ctx, listAlloca, i8p, 8);
            ctx.builder.CreateCall(fillFn, {strPtr, delimV, listAlloca});

            return ctx.ir_gen->wrap(listAlloca);
        }
    }

    // cascade<X> shares the list<i32> layout — normalize before dispatching
    // list methods (push / pop / get / len / …).
    if (className.rfind("cascade<", 0) == 0 && className.back() == '>') {
        className = "list<i32>";
    }

    // List Methods
    if (className.find("list<") == 0) {
         std::string listElemName = className.substr(5, className.length() - 6);
         llvm::Type* llvmListElemType = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(listElemName)));
         if (methodName == "push" && argVals.size() > 0) {
             llvm::Value* pushVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
             // spec/61 §5.7 (memV3): a NESTED CONTAINER element (list<list<T>>, list<map<…>>,
             // list<buffer>) stores the inner container's HANDLE (a pointer). push CO-OWNS it →
             // incref the inner handle and store the pointer; the source keeps its own handle
             // (share, aliasing — §5.7). The source is a container local, so read its slot directly
             // (the by-ref deref would give the {size,cap,data} struct, not the handle pointer).
             if (ctx.memV3 && ctx.isMemV3ContainerHandle(listElemName)) {
                 auto* i8p = llvm::PointerType::get(ctx.context, 0);
                 llvm::Value* hp = nullptr;
                 if (auto* ve = dynamic_cast<VariableExpr*>(call->args[0].get())) {
                     if (Symbol* s = resolveVariable(ve->name)) {
                         llvm::Value* slot = s->ir_alloca_inst
                             ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(s->ir_alloca_inst)) : s->alloca_inst;
                         if (slot) hp = ctx.builder.CreateLoad(i8p, slot, "push_innerh");
                     }
                 }
                 if (!hp) hp = (pushVal && pushVal->getType() == i8p) ? pushVal
                              : (pushVal ? ctx.builder.CreateBitCast(pushVal, i8p, "push_innerh_c") : nullptr);
                 if (hp) {
                     const char* incF = (listElemName.rfind("map<", 0) == 0) ? "aiml_map_handle_incref"
                         : (listElemName == "buffer" || listElemName.rfind("buffer<", 0) == 0) ? "aiml_buffer_handle_incref"
                         : "aiml_list_handle_incref";
                     ctx.builder.CreateCall(ctx.getFunction(incF, ctx.builder.getVoidTy(), {i8p}), { hp });
                     ListImpl::push(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), hp);
                 }
                 return nullptr;
             }
             // AIML v2 P3: a list<Model> stores %M* REFERENCES (stride 8). push increfs the object
             // and stores the pointer; the source keeps its own reference (share, not move) — the
             // element's decref (list-free / remove / idx-overwrite) balances this incref. No
             // deep-copy: the object is shared, in-place mutation is visible through every holder.
             if (ctx.isRefcountModel(listElemName) && pushVal) {
                 auto* i8p = llvm::PointerType::get(ctx.context, 0);
                 llvm::Value* objp = (pushVal->getType() == i8p) ? pushVal
                                     : ctx.builder.CreateBitCast(pushVal, i8p, "push_modelptr");
                 llvm::Function* inc = ctx.getFunction("aiml_model_incref", ctx.builder.getVoidTy(), {i8p});
                 ctx.builder.CreateCall(inc, { objp });
                 ListImpl::push(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), objp);
                 return nullptr;
             }
             // spec/61 §7.0 — VALUE-MODEL element (struct with heap fields): a plain shallow store
             // shares the source model's inner heap. If the source is a MOVABLE owned local (a fresh
             // get_copy/no-init model whose scope-drop is still live), MOVE it (shallow + consume the
             // drop — cheap, current fast path). Otherwise (an `at`-expr, member/param field, or a
             // local already moved into another container) DEEP-COPY the model into an independent
             // slot, so the owner drops don't double-free a shared inner heap (the list<Model>
             // double-free / 9rooms WASM client crash). Only value models — ptr<>/scalar/string
             // elements keep their existing paths.
             if (ctx.lifetimeDropsEnabled && llvmListElemType && llvmListElemType->isStructTy() && pushVal) {
                 auto eci = ctx.classes.find(listElemName);
                 bool modelElem = eci != ctx.classes.end() && !eci->second.isBuiltin
                     && eci->second.hasHeapFields && eci->second.serNodesVar && eci->second.type
                     && !(llvm::cast<llvm::StructType>(llvmListElemType)->hasName()
                          && llvm::cast<llvm::StructType>(llvmListElemType)->getName().startswith("AimlFatPtr_"));
                 if (modelElem) {
                     // MOVABLE owned local: a plain-local model whose kind-0 scope-drop is still live
                     // (a fresh get_copy/no-init not yet moved). Those MOVE (cheap shallow store +
                     // consume the drop) via the fallthrough below. Everything else (an at/get element
                     // pointer, a member/param field, or a local already moved) DEEP-COPIES here so the
                     // list element owns an INDEPENDENT heap (no shared-inner-heap double-free).
                     bool moveOwned = false;
                     if (auto* ve = dynamic_cast<VariableExpr*>(call->args[0].get())) {
                         if (ve->name.find('.') == std::string::npos && ve->name.find('<') == std::string::npos) {
                             if (Symbol* s = resolveVariable(ve->name)) {
                                 llvm::Value* a = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
                                     s->ir_alloca_inst ? s->ir_alloca_inst : ctx.ir_gen->wrap(s->alloca_inst)));
                                 // spec/61: the shallow-MOVE fast path (store the struct into the element
                                 // and consumeLocalDrop the source) is sound only when the push DOMINATES
                                 // the source's scope exit — i.e. the push and the decl sit in the SAME
                                 // innermost block, so the source's still-live kind-0 drop is in the
                                 // TOPMOST cleanup frame. If the drop lives in an OUTER frame the push is
                                 // inside a nested/conditional block (e.g. an `if` in a loop body):
                                 // consumeLocalDrop erases the drop from every frame, so on the paths that
                                 // skip the push the source leaks (its whole heap — observed as ~1 model +
                                 // its card_ids/strings per iteration in DeckService.list). Fall back to
                                 // DEEP-COPY there: the source keeps its drop and is freed at its own scope
                                 // exit, while the list element owns an INDEPENDENT heap (no double-free).
                                 if (a && !ctx.cleanupStack.empty())
                                     for (auto& d : ctx.cleanupStack.back())
                                         if (d.addr == a && d.kind == 0) { moveOwned = true; break; }
                             }
                         }
                     }
                     if (!moveOwned) {
                         // Source address for the deep-copy: pushVal is either already an element
                         // pointer (get/at on a struct list) or a loaded struct value — spill the
                         // latter to a temp alloca so aiml_ser_deep_copy can read its inner pointers.
                         llvm::Value* srcAddr;
                         if (pushVal->getType()->isPointerTy()) {
                             srcAddr = pushVal;
                         } else {
                             llvm::AllocaInst* tmp = ctx.createEntryAlloca(llvmListElemType, "push_src");
                             ctx.builder.CreateStore(pushVal, tmp);
                             srcAddr = tmp;
                         }
                         ListImpl::pushDeepCopyModel(ctx,
                             static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)),
                             srcAddr, llvmListElemType,
                             eci->second.serNodesVar, eci->second.serNodesCount);
                         return nullptr;
                     }
                 }
             }
             // spec/61 §8: a list<string> now OWNS its elements via a dup at the push site (below),
             // so a STRING element is a pure BORROW — the caller keeps its scope-drop / stmt-temp and
             // frees its own copy, no matter how many frames away it was built (the event_log
             // log_event(msg)→lines.push(msg) helper-indirection: the real owner is the caller's
             // interpolation temp, out of reach of consumeLocalDrop here). Non-string elements keep
             // MOVE semantics: the list stores them raw, so consume the source's still-live drop.
             if (listElemName != "string") {
                 // spec/61 (A3): pushing a local moves it into the container → do not drop it.
                 if (!call->args.empty()) consumeLocalDrop(call->args[0].get());
                 consumeStringTemp(pushVal);
             }
             // Reject pointer representations in aggregate VALUE lists, regardless
             // of the argument AST shape (variable, member, nested get/call).
             // get() returns an element pointer for structs; passing it through
             // would make ListImpl::push allocate/store pointer-sized elements.
             // Interface fat pointers retain their existing upcast path below.
             const bool aggregateValue = llvmListElemType && llvmListElemType->isStructTy();
             const bool interfaceValue = aggregateValue &&
                 llvm::cast<llvm::StructType>(llvmListElemType)->hasName() &&
                 llvm::cast<llvm::StructType>(llvmListElemType)->getName().startswith("AimlFatPtr_");
             if (aggregateValue && !interfaceValue && pushVal && pushVal->getType()->isPointerTy()) {
                 LOG_ERROR(ctx.fmtLoc(call->sourceNode) +
                     "list<" + listElemName + ">.push() got a pointer instead of a value: "
                     "storing pointer-sized elements would corrupt the list layout. "
                     "Copy/dereference the element into a local " + listElemName +
                     " value before push(), or use list<ptr<" + listElemName + ">>.");
                 return nullptr;
             }
             // Interface-Upcast für push(ptr<Concrete>) wenn list-elem = ptr<Interface>.
             // Ohne diesen Schritt landet ein 8-byte class-ptr in einem 16-byte
             // fat-pointer slot — die folgenden 8 bytes (vtable) sind garbage,
             // beim späteren `at` + dispatch korrupt der Heap.
             // Spiegelt das identische Pattern in map.put unten.
             if (llvmListElemType && llvmListElemType->isStructTy() &&
                 llvm::cast<llvm::StructType>(llvmListElemType)->hasName() &&
                 llvm::cast<llvm::StructType>(llvmListElemType)->getName().startswith("AimlFatPtr_")) {
                 if (auto* varExpr = dynamic_cast<VariableExpr*>(call->args[0].get())) {
                     if (Symbol* srcSym = resolveVariable(varExpr->name)) {
                         std::string srcTy = srcSym->typeName;
                         if (pushVal && !pushVal->getType()->isPointerTy()) {
                             pushVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
                                 srcSym->ir_alloca_inst ? srcSym->ir_alloca_inst
                                                        : ctx.ir_gen->wrap(srcSym->alloca_inst)));
                             srcTy = "ptr<" + srcTy + ">";
                         }
                         llvm::Value* fat = buildInterfaceUpcast(pushVal, srcTy,
                                                                  llvm::cast<llvm::StructType>(llvmListElemType));
                         if (fat) pushVal = fat;
                     }
                 }
             }
             // spec/61 §8 — the list OWNS an INDEPENDENT copy of every string element (dup at the
             // push site). This unifies (and subsumes) the older split paths — MOVE for owned
             // locals/interpolation temps and incref for borrowed member strings — into one robust
             // rule: pushing a plain local, a param, an interpolation temp, or a member/field access
             // (ent.player_id / <.name) all leave the source untouched, and the list's element
             // decref (kind-2 elemKind-1 drop) frees exactly this dup. Fixes uniformly both the
             // take_gone_players shared-buffer UAF (`malloc_consolidate: invalid chunk size`) and the
             // event_log dangling-temp corruption (a string pushed through a helper param, whose real
             // owner — the caller's interpolation temp — is freed at the caller's statement end).
             if (listElemName == "string" && pushVal && pushVal->getType()->isPointerTy()) {
                 llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
                 llvm::Value* asP = pushVal->getType() == i8p ? pushVal : ctx.builder.CreateBitCast(pushVal, i8p);
                 llvm::Function* dup = ctx.getFunction("aiml_str_dup", i8p, {i8p});
                 pushVal = ctx.builder.CreateCall(dup, { asP }, "list_str_own");
             }
             ListImpl::push(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), pushVal);
             return nullptr;
         } else if (methodName == "pop") {
             return ctx.ir_gen->wrap(ListImpl::pop(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), llvmListElemType));
         } else if (methodName == "slice" && !argVals.empty()) {
             // spec/01/§5.4: list.slice(start, end) — shallow COPY of [start, end) into a fresh heap
             // handle (start inclusive, end exclusive; end optional → to len). Dash-free form of
             // `lst-a-b`. ListImpl::slice returns a real handle under memV3 (adopted by the caller).
             // spec/61 §5.1: a shallow copy of a MOVE-ONLY element (ptr<T>, a unique owner) would put
             // one owner into two containers → double free at drop (valgrind: Invalid free). Reject it;
             // use `{clone}` for a deep, independent copy.
             if (listElemName.rfind("ptr<", 0) == 0) {
                 LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "list.slice of list<" + listElemName +
                     ">: the element type is move-only (a unique owner) — a shallow slice would "
                     "duplicate one owner into two containers (double free). Use {clone} (deep) "
                     "of the range instead (§5.1).");
                 return ctx.ir_gen->getInt32(0);
             }
             llvm::Type* i32ty = ctx.builder.getInt32Ty();
             llvm::Value* listP = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
             llvm::Value* startV = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
             if (startV->getType()->isIntegerTy() && startV->getType()->getIntegerBitWidth() != 32)
                 startV = ctx.builder.CreateIntCast(startV, i32ty, true, "slice_start");
             llvm::Value* endV = (argVals.size() > 1)
                 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]))
                 : ListImpl::getSize(ctx, listP);
             if (endV->getType()->isIntegerTy() && endV->getType()->getIntegerBitWidth() != 32)
                 endV = ctx.builder.CreateIntCast(endV, i32ty, true, "slice_end");
             // Element stride: nested-container / refcount-model elements are pointer-sized handles.
             llvm::Type* sliceElemTy = (ctx.memV3 && (ctx.isMemV3ContainerHandle(listElemName)
                                                      || ctx.isRefcountModel(listElemName)))
                 ? llvm::PointerType::get(ctx.context, 0)
                 : llvmListElemType;
             // §5.4/§5.1: co-own each rc-bearing element (incref) so slice+source share with a
             // balanced refcount — scalars need none. ptr<T> was rejected above (move-only).
             const char* retainFn = nullptr;
             if (listElemName == "string")                       retainFn = "aiml_str_incref";
             else if (ctx.isRefcountModel(listElemName))         retainFn = "aiml_model_incref";
             else if (ctx.memV3 && (listElemName.rfind("list<", 0) == 0
                                    || listElemName.rfind("cascade<", 0) == 0)) retainFn = "aiml_list_handle_incref";
             else if (ctx.memV3 && listElemName.rfind("map<", 0) == 0)          retainFn = "aiml_map_handle_incref";
             else if (ctx.memV3 && (listElemName == "buffer"
                                    || listElemName.rfind("buffer<", 0) == 0))  retainFn = "aiml_buffer_handle_incref";
             return ctx.ir_gen->wrap(ListImpl::slice(ctx, listP, startV, endV, sliceElemTy, retainFn));
         } else if (methodName == "get" && argVals.size() > 0) {
             // AIML v2 P3: a list<Model> element is a %M* — LOAD it (stride 8) and return the
             // reference. It is a pointer-to-struct, the same shape v1's getElementPtr yields for a
             // struct list, so downstream field access is identical. A borrow — the caller increfs
             // only if it BINDS the result into a local (visitDeclStmt's at/get share path).
             if (ctx.isRefcountModel(listElemName)) {
                 auto* i8p = llvm::PointerType::get(ctx.context, 0);
                 return ctx.ir_gen->wrap(ListImpl::get(ctx,
                     static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)),
                     static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])), i8p));
             }
             // spec/61 §5.7 (memV3): a NESTED CONTAINER element is a HANDLE pointer stored at pointer
             // stride — load and return it (a borrow, matching visitAtExpr).  Without this the element
             // type is the inline %GenericList struct → the struct-stride getElementPtr branch below
             // GEPs by sizeof(struct) and returns garbage (test_mkp_math ability_score `.effects.get`).
             // Binding into a `list<T>` local increfs (emitListHandleDecl's at/get share path).
             if (ctx.memV3 && ctx.isMemV3ContainerHandle(listElemName)) {
                 auto* i8p = llvm::PointerType::get(ctx.context, 0);
                 llvm::Value* slot = ListImpl::getElementPtr(ctx,
                     static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)),
                     static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])), i8p);
                 return ctx.ir_gen->wrap(ctx.builder.CreateLoad(i8p, slot, "list_get_innerh"));
             }
             // For struct element types, return a pointer (ptr<T>) instead of by-value copy
             if (llvmListElemType->isStructTy()) {
                 return ctx.ir_gen->wrap(ListImpl::getElementPtr(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])), llvmListElemType));
             }
             return ctx.ir_gen->wrap(ListImpl::get(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])), llvmListElemType));
         } else if (methodName == "insert" && argVals.size() > 1) {
             ListImpl::insert(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1])));
             return nullptr;
         } else if (methodName == "remove" && argVals.size() > 0) {
             // AIML v2 P3: a list<Model> stores %M* REFERENCES (stride 8, NOT the struct size). The
             // removed reference must be DECREF'd (balances push's incref) and the memmove must shift
             // by pointer stride — using the struct type here shifts by sizeof(Model) and corrupts the
             // pointer array (garbage element → aiml_model_incref SIGSEGV in the 9rooms buff remove).
             if (ctx.isRefcountModel(listElemName)) {
                 auto* i8p = llvm::PointerType::get(ctx.context, 0);
                 llvm::Value* listP = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
                 llvm::Value* idxV  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
                 // decref the element about to be removed (bounds already enforced by callers/at).
                 if (ctx.lifetimeDropsEnabled) {
                     // ctx.classes is keyed by the BARE class; a memV3 list<shared<T>> element name
                     // is "shared<T>" → strip the wrapper, else serNodes is not found and the removed
                     // element is never decref'd (probe 06: the shared model + its heap leaked).
                     std::string bareElem = listElemName;
                     if (bareElem.rfind("shared<", 0) == 0 && bareElem.back() == '>')
                         bareElem = bareElem.substr(7, bareElem.size() - 8);
                     auto ci = ctx.classes.find(bareElem);
                     if (ci != ctx.classes.end() && ci->second.serNodesVar) {
                         llvm::StructType* lstTy = ListImpl::getListStructType(ctx);
                         llvm::Value* dataP = ctx.builder.CreateLoad(i8p,
                             ctx.builder.CreateStructGEP(lstTy, listP, 2, "rm_dataPtr"), "rm_dataBase");
                         llvm::Value* slot = ctx.builder.CreateInBoundsGEP(i8p, dataP, idxV, "rm_slot");
                         llvm::Value* gone = ctx.builder.CreateLoad(i8p, slot, "rm_modelptr");
                         llvm::Function* dref = ctx.getFunction("aiml_model_decref", ctx.builder.getVoidTy(),
                             {i8p, i8p, ctx.builder.getInt32Ty()});
                         ctx.builder.CreateCall(dref, { gone, ci->second.serNodesVar,
                                                        ctx.builder.getInt32(ci->second.serNodesCount) });
                     }
                 }
                 // Shift with POINTER stride (i8p), and do NOT str-decref.
                 ListImpl::remove(ctx, listP, idxV, i8p, /*elemIsString=*/false);
                 return nullptr;
             }
             // spec/61 §5.5 (memV3): list<ptr<T>> element = an OWNING box → remove RELEASES it
             // (deep-free fields + free shell) before the memmove shifts the pointer slots. Without
             // this the removed box + its heap leaked (probe 05). Pointer stride (i8p) for the shift.
             if (ctx.memV3 && listElemName.rfind("ptr<", 0) == 0 && listElemName.back() == '>') {
                 auto* i8p = llvm::PointerType::get(ctx.context, 0);
                 llvm::Value* listP = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
                 llvm::Value* idxV  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
                 if (ctx.lifetimeDropsEnabled) {
                     std::string boxed = listElemName.substr(4, listElemName.size() - 5);
                     auto bi = ctx.classes.find(boxed);
                     llvm::StructType* lstTy = ListImpl::getListStructType(ctx);
                     llvm::Value* dataP = ctx.builder.CreateLoad(i8p,
                         ctx.builder.CreateStructGEP(lstTy, listP, 2, "rm_dataPtr"), "rm_dataBase");
                     llvm::Value* slot = ctx.builder.CreateInBoundsGEP(i8p, dataP, idxV, "rm_slot");
                     llvm::Value* gone = ctx.builder.CreateLoad(i8p, slot, "rm_ptrbox");
                     if (bi != ctx.classes.end() && bi->second.hasHeapFields && bi->second.serNodesVar) {
                         llvm::Function* dfree = ctx.getFunction("aiml_ser_free_decoded",
                             ctx.builder.getVoidTy(), {i8p, i8p, ctx.builder.getInt32Ty()});
                         ctx.builder.CreateCall(dfree, { gone, bi->second.serNodesVar,
                                                         ctx.builder.getInt32(bi->second.serNodesCount) });
                     }
                     llvm::Function* fr = ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p});
                     ctx.builder.CreateCall(fr, { gone });
                 }
                 ListImpl::remove(ctx, listP, idxV, i8p, /*elemIsString=*/false);
                 return nullptr;
             }
             // spec/61 §5.7 (memV3): a NESTED CONTAINER element is a HANDLE → remove DECREFs it (via
             // its handle-rooted descriptor → recursive release at rc 0), then shifts pointer slots.
             if (ctx.memV3 && ctx.isMemV3ContainerHandle(listElemName)) {
                 auto* i8p = llvm::PointerType::get(ctx.context, 0);
                 llvm::Value* listP = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
                 llvm::Value* idxV  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
                 if (ctx.lifetimeDropsEnabled) {
                     int nc = 0; llvm::GlobalVariable* ed = elemHandleSerNodes(listElemName, nc);
                     if (ed) {
                         llvm::StructType* lstTy = ListImpl::getListStructType(ctx);
                         llvm::Value* dataP = ctx.builder.CreateLoad(i8p,
                             ctx.builder.CreateStructGEP(lstTy, listP, 2, "rm_dataPtr"), "rm_dataBase");
                         llvm::Value* slot = ctx.builder.CreateInBoundsGEP(i8p, dataP, idxV, "rm_slot");
                         llvm::Function* dfree = ctx.getFunction("aiml_ser_free_decoded",
                             ctx.builder.getVoidTy(), {i8p, i8p, ctx.builder.getInt32Ty()});
                         ctx.builder.CreateCall(dfree, { slot, ed, ctx.builder.getInt32(nc) });  // decref inner handle
                     }
                 }
                 ListImpl::remove(ctx, listP, idxV, i8p, /*elemIsString=*/false);
                 return nullptr;
             }
             // NOTE: a VALUE-MODEL element (`list<Model>` inline at struct stride) with heap fields is
             // NOT deep-freed here on remove — the removed element's container/string fields leak
             // (spec/61 §5.4 says release-members, a known deferral). A deep-free was tried and REVERTED:
             // effect code holds a live view/copy of the element across the remove (extract → mutate →
             // remove), so freeing the inline fields here double-freed them (regressed effect_processor
             // / effect_death_on_destroy). Correct fix needs escape-time deep-copy of value-model views,
             // not a blind remove-time free. See memory memv3-nested-containers.
             ListImpl::remove(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])), llvmListElemType, listElemName == "string");
             return nullptr;
         } else if (methodName == "clear") {
             ListImpl::clear(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)));
             return nullptr;
         } else if (methodName == "free") {
             // Free the data array and reset the list to empty/null state
             llvm::Value* listPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
             llvm::StructType* lstTy = ListImpl::getListStructType(ctx);
             llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
             llvm::Value* dataPtr = ctx.builder.CreateStructGEP(lstTy, listPtr, 2, "dataPtr");
             llvm::Value* dataVal = ctx.builder.CreateLoad(i8p, dataPtr, "dataVal");
             llvm::Function* freeFn = ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p});
             ctx.builder.CreateCall(freeFn, {dataVal});
             ctx.builder.CreateStore(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)), dataPtr);
             ctx.builder.CreateStore(ctx.builder.getInt32(0), ctx.builder.CreateStructGEP(lstTy, listPtr, 0));
             ctx.builder.CreateStore(ctx.builder.getInt32(0), ctx.builder.CreateStructGEP(lstTy, listPtr, 1));
             return nullptr;
         } else if (methodName == "len") {
             return ctx.ir_gen->wrap(ListImpl::getSize(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca))));
         }
         // ── List-Builtins (doc/ideas/list_builtins_plan.md) ────────────────
         else if (methodName == "contains" || methodName == "index_of" ||
                  methodName == "count"    || methodName == "remove_value") {
             // Value-lists only: struct/model elements have no canonical equality in M1.
             if (llvmListElemType && llvmListElemType->isStructTy()) {
                 LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "list<" + listElemName + ">." + methodName +
                     " is not supported for struct/model element types (no canonical equality in M1).");
                 return ctx.ir_gen->getInt32(0);
             }
             if (argVals.size() != 1) {
                 LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "list<" + listElemName + ">." + methodName +
                     " expects 1 argument, got " + std::to_string(argVals.size()));
                 return ctx.ir_gen->getInt32(0);
             }
             llvm::Value* listPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
             llvm::Value* needle  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
             if (methodName == "index_of")
                 return ctx.ir_gen->wrap(ListImpl::indexOf(ctx, listPtr, needle, llvmListElemType, listElemName));
             if (methodName == "count")
                 return ctx.ir_gen->wrap(ListImpl::count(ctx, listPtr, needle, llvmListElemType, listElemName));
             if (methodName == "contains") {
                 llvm::Value* idx = ListImpl::indexOf(ctx, listPtr, needle, llvmListElemType, listElemName);
                 return ctx.ir_gen->wrap(ctx.builder.CreateZExt(
                     ctx.builder.CreateICmpSGE(idx, ctx.builder.getInt32(0)), ctx.builder.getInt32Ty()));
             }
             // remove_value: indexOf + conditional remove; returns i32 1 (removed) / 0.
             llvm::Value* idx = ListImpl::indexOf(ctx, listPtr, needle, llvmListElemType, listElemName);
             llvm::Function* pf = ctx.builder.GetInsertBlock()->getParent();
             llvm::Value* outVar = ctx.createEntryAlloca(ctx.builder.getInt32Ty(), "lrmv_out");
             ctx.builder.CreateStore(ctx.builder.getInt32(0), outVar);
             auto* rmBB  = llvm::BasicBlock::Create(ctx.context, "lrmv_do", pf);
             auto* endBB = llvm::BasicBlock::Create(ctx.context, "lrmv_end", pf);
             ctx.builder.CreateCondBr(ctx.builder.CreateICmpSGE(idx, ctx.builder.getInt32(0)), rmBB, endBB);
             ctx.builder.SetInsertPoint(rmBB);
             ListImpl::remove(ctx, listPtr, idx, llvmListElemType, listElemName == "string");
             ctx.builder.CreateStore(ctx.builder.getInt32(1), outVar);
             ctx.builder.CreateBr(endBB);
             ctx.builder.SetInsertPoint(endBB);
             return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), outVar, "lrmv_res"));
         }
         else if (methodName == "extend") {
             if (argVals.size() != 1) {
                 LOG_ERROR(ctx.fmtLoc(call->sourceNode) + className + ".extend expects 1 argument, got " +
                     std::to_string(argVals.size()));
                 return nullptr;
             }
             // The argument must be a list VARIABLE: we need its struct pointer
             // (alloca), not the by-value load that argVals[0] produced.  Also
             // static-type-check it against the receiver (no implicit conversion).
             llvm::Value* srcPtr = nullptr;
             std::string argTypeName;
             if (auto* v = dynamic_cast<VariableExpr*>(call->args[0].get())) {
                 if (Symbol* s = resolveVariable(v->name)) {
                     argTypeName = s->typeName;
                     srcPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
                         s->ir_alloca_inst ? s->ir_alloca_inst
                                           : ctx.ir_gen->wrap(s->alloca_inst)));
                     // spec/61 §5 (memV3): the arg local's slot holds a POINTER to the handle; load it
                     // so extend reads the real {size,cap,data} struct, not the handle-pointer bytes
                     // (otherwise src looks empty → dst.len unchanged, test_list_builtins ext_len).
                     if (ctx.memV3 && srcPtr && ctx.isMemV3ContainerHandle(s->typeName)) {
                         auto* i8pH = llvm::PointerType::get(ctx.context, 0);
                         srcPtr = ctx.builder.CreateLoad(i8pH, srcPtr, "extend_src_h");
                     }
                 }
             }
             if (argTypeName.rfind("cascade<", 0) == 0 && !argTypeName.empty() && argTypeName.back() == '>')
                 argTypeName = "list<i32>";
             if (!srcPtr) {
                 LOG_ERROR(ctx.fmtLoc(call->sourceNode) + className +
                     ".extend expects a list variable as argument.");
                 return nullptr;
             }
             if (!argTypeName.empty() && argTypeName != className) {
                 LOG_ERROR(ctx.fmtLoc(call->sourceNode) + className + ".extend expects " + className +
                     ", got " + argTypeName);
                 return nullptr;
             }
             ListImpl::extend(ctx,
                 static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)),
                 srcPtr, llvmListElemType);
             return nullptr;
         }
         else if (methodName == "reverse" || methodName == "shuffle") {
             if (!argVals.empty()) {
                 LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "list<" + listElemName + ">." + methodName +
                     " expects 0 arguments, got " + std::to_string(argVals.size()));
                 return nullptr;
             }
             llvm::Value* listPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
             if (methodName == "reverse") ListImpl::reverse(ctx, listPtr, llvmListElemType);
             else                          ListImpl::shuffle(ctx, listPtr, llvmListElemType);
             return nullptr;
         }
         else if (methodName == "sort" || methodName == "sort_desc") {
             if (!argVals.empty()) {
                 LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "list<" + listElemName + ">." + methodName +
                     " expects 0 arguments, got " + std::to_string(argVals.size()));
                 return nullptr;
             }
             bool elemIsString = (listElemName == "string");
             bool sortable = elemIsString || (llvmListElemType &&
                 (llvmListElemType->isIntegerTy() || llvmListElemType->isFloatingPointTy()));
             if (!sortable) {
                 LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "list<" + listElemName + ">." + methodName +
                     " supports only int, enum, float and string element types.");
                 return nullptr;
             }
             ListImpl::sortList(ctx,
                 static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)),
                 llvmListElemType, listElemName, methodName == "sort_desc");
             return nullptr;
         }
         LOG_ERROR("Unknown list method: " + methodName + " on " + className);
         return ctx.ir_gen->getInt32(0);
    }
    // Map Methods
    else if (className.find("map<") == 0) {
         llvm::StructType* mapStructTy = static_cast<llvm::StructType*>(ctx.ir_gen->unwrapType(baseElemTy));
         llvm::Type* vTy = nullptr;
         std::string vName;
         std::string inner = className.substr(4, className.length() - 5);
         size_t colon = inner.find(':');
         if (colon != std::string::npos) {
             vName = inner.substr(colon + 1);
             vTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(vName)));
         }
         if (!vTy) vTy = ctx.builder.getInt8Ty()->getPointerTo();
         // AIML v2 P3: a map<K:Model> stores %M* references (pointer values).
         bool refVal = ctx.isRefcountModel(vName);
         if (refVal) vTy = ctx.builder.getInt8Ty()->getPointerTo();
         if (methodName == "put" && argVals.size() > 1) {
             llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
             llvm::Value* valVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]));
             bool valIsString = (vName == "string");
             // AIML v2 P3: refcount-model value → incref + store the %M* (share). The map OWNS a
             // reference (balanced by the map-free valKind-3 decref); the source keeps its own.
             if (refVal) {
                 auto* i8p = llvm::PointerType::get(ctx.context, 0);
                 llvm::Value* objp = (valVal->getType() == i8p) ? valVal
                                     : ctx.builder.CreateBitCast(valVal, i8p, "put_modelptr");
                 llvm::Function* inc = ctx.getFunction("aiml_model_incref", ctx.builder.getVoidTy(), {i8p});
                 ctx.builder.CreateCall(inc, { objp });
                 if (keyVal->getType()->isPointerTy() && keyVal->getType() != i8p)
                     keyVal = ctx.builder.CreateBitCast(keyVal, i8p);
                 MapImpl::put(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), keyVal, objp, mapStructTy, false);
                 return nullptr;
             }
             // spec/61 §8: MapImpl::put now DUPS string keys and string values (map owns its own
             // copy), so the caller is a pure BORROWER — its key/value temp or declared local
             // keeps its normal scope-drop and frees itself, no matter how many call frames away
             // it was declared (fixes the ManaPool.add_color→colors.put helper-indirection where
             // caller-side drop-suppression could not reach the real owner one frame up → the key
             // was freed under the map → all mana colors collapsed into one slot).
             // NON-string values (model/ptr) still MOVE into the map (stored raw): suppress the
             // caller's drop for those, exactly as before.
             if (!valIsString) {
                 consumeStringTemp(valVal);
                 if (call->args.size() > 1) consumeLocalDrop(call->args[1].get());
             }
             // Interface-Upcast für put(key, ptr<Concrete>) wenn V = ptr<Interface>.
             // Zentralisiert in Compiler::buildInterfaceUpcast.
             if (vTy->isStructTy() && llvm::cast<llvm::StructType>(vTy)->hasName() &&
                 llvm::cast<llvm::StructType>(vTy)->getName().startswith("AimlFatPtr_")) {
                 if (auto* varExpr = dynamic_cast<VariableExpr*>(call->args[1].get())) {
                     if (Symbol* srcSym = resolveVariable(varExpr->name)) {
                         llvm::Value* fat = buildInterfaceUpcast(valVal, srcSym->typeName,
                                                                  llvm::cast<llvm::StructType>(vTy));
                         if (fat) valVal = fat;
                     }
                 }
             }
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
             MapImpl::put(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), keyVal, valVal, mapStructTy, valIsString);
             return nullptr;
         } else if ((methodName == "get" || methodName == "get_copy") && argVals.size() > 0) {
             // spec/61 §5.4: get_copy returns an independent copy of the value. For a value/scalar
             // value type it is identical to get (get already yields a value copy); for a handle
             // value type a deep-copy refinement is future work — accepted alias for now.
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
             MapImpl::remove(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), keyVal, vTy, mapStructTy, vName == "string");
             return nullptr;
         } else if (methodName == "contains" && argVals.size() > 0) {
             llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
             if (keyVal->getType()->isPointerTy() && keyVal->getType() != ctx.builder.getInt8Ty()->getPointerTo()) {
                 keyVal = ctx.builder.CreateBitCast(keyVal, ctx.builder.getInt8Ty()->getPointerTo());
             }
             llvm::Value* res1 = MapImpl::contains(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), keyVal, mapStructTy);
             llvm::Value* res32 = ctx.builder.CreateZExt(res1, ctx.builder.getInt32Ty(), "contains_i32");
             return ctx.ir_gen->wrap(res32);
         } else if (methodName == "len") {
             return ctx.ir_gen->wrap(MapImpl::getSize(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), mapStructTy));
         }
         LOG_ERROR("Unknown map method: " + methodName + " on " + className);
         return ctx.ir_gen->getInt32(0);
    }
    // Channel Methods: chan<T>.send / chan<T>.recv / chan<T>.init
    else if (className.rfind("chan<", 0) == 0 && className.back() == '>') {
        std::string elemName = className.substr(5, className.length() - 6); // skip "chan<", drop ">"
        llvm::Type* elemTy = static_cast<llvm::Type*>(
            ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
        if (!elemTy) elemTy = ctx.builder.getInt64Ty();

        llvm::Value* rawAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));

        if ((methodName == "send" || methodName == "send_unique") && !argVals.empty()) {
            // spec/61 §5.3: send moves an exclusive value across the thread boundary.
            // send_unique is the runtime-checked escape (rc==1 assertion) — accepted here as the
            // same move; the exclusivity assertion is a future runtime-check refinement.
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
        llvm::Type* sz  = ctx.getSizeType();
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
                    llvm::AllocaInst* tmp = ctx.createEntryAlloca(v->getType(), "ins_tmp");
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
                    llvm::AllocaInst* tmp = ctx.createEntryAlloca(v->getType(), "upd_tmp");
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
                llvm::AllocaInst* tmp = ctx.createEntryAlloca(
                    keyVal->getType(), "pk_tmp");
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
        llvm::Type* sz  = ctx.getSizeType();
        llvm::Value* kvPtr = ctx.builder.CreateLoad(i8p,
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), "kv_ptr");

        if (methodName == "set" && argVals.size() >= 2) {
            llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            llvm::Value* valVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]));
            llvm::Value* valPtr = nullptr;
            if (valSize == 0) {
                valPtr = valVal; // string: pass char* directly
            } else if (valVal->getType()->isPointerTy()) {
                // ptr<T> arg (e.g. p = kv.get(k) declared as ptr<Player>): valVal
                // is already a pointer to the struct. Pass it through — no temp
                // alloca needed. This enables the "view/sync" pattern where the
                // user mutates a kv entry in place and writes it back under the
                // same key (cache stays canonical because mutations went directly
                // through the pointer; encode reads up-to-date state).
                valPtr = ctx.builder.CreateBitCast(valVal, i8p);
            } else {
                // Value-type arg (loaded struct): spill into a temp alloca so
                // we can pass a pointer.
                llvm::AllocaInst* tmp = ctx.createEntryAlloca(valVal->getType(), "kv_val_tmp");
                ctx.builder.CreateStore(valVal, tmp);
                valPtr = ctx.builder.CreateBitCast(tmp, i8p);
            }

            // ── db_indexed set-hook ─────────────────────────────────────────
            // Before the main set:
            //   1) load old struct via aiml_db_kv_get (may be null if new key)
            //   2) for each indexed field: remove old value from its index
            // After the main set:
            //   3) for each indexed field: add new value to its index
            std::vector<std::pair<std::string, CompilerContext::StructMember>> indexedFields;
            llvm::StructType* valStructTy = nullptr;
            if (ctx.classes.count(vName) && ctx.classes[vName].type) {
                valStructTy = ctx.classes[vName].type;
                for (auto& [fname, mem] : ctx.classes[vName].members) {
                    if (mem.db_indexed) indexedFields.push_back({fname, mem});
                }
            }

            auto emitIndexOp = [&](const std::string& opSym,
                                    const std::string& fname,
                                    const CompilerContext::StructMember& mem,
                                    llvm::Value* basePtr) {
                // basePtr is i8*; bitcast to struct type ptr and GEP to field.
                llvm::Value* typedBase = ctx.builder.CreateBitCast(
                    basePtr, llvm::PointerType::get(ctx.context, 0));
                llvm::Value* fldGep = ctx.builder.CreateStructGEP(
                    valStructTy, typedBase, mem.index);

                llvm::Value* fNameVal = ctx.builder.CreateGlobalStringPtr(fname);
                llvm::Function* idxLookup = ctx.getFunction(
                    "aiml_db_index_lookup", i8p, {i8p, i8p});
                llvm::Value* idxHandle = ctx.builder.CreateCall(
                    idxLookup, {kvPtr, fNameVal});
                llvm::Function* opFn = ctx.getFunction(
                    "aiml_db_index_" + opSym,
                    ctx.builder.getVoidTy(), {i8p, i8p, i8p});

                const std::string& fTy = mem.typeName;
                if (fTy == "string") {
                    llvm::Value* fVal = ctx.builder.CreateLoad(i8p, fldGep);
                    ctx.builder.CreateCall(opFn, {idxHandle, fVal, keyVal});
                } else if (fTy == "i32") {
                    llvm::Value* fVal = ctx.builder.CreateLoad(i32, fldGep);
                    llvm::Value* fStr = StringUtils::intToString(ctx, fVal);
                    ctx.builder.CreateCall(opFn, {idxHandle, fStr, keyVal});
                } else if (fTy == "i64") {
                    llvm::Value* fVal = ctx.builder.CreateLoad(i64, fldGep);
                    llvm::Value* fStr = StringUtils::int64ToString(ctx, fVal);
                    ctx.builder.CreateCall(opFn, {idxHandle, fStr, keyVal});
                } else if (fTy.rfind("list<", 0) == 0) {
                    // list<T> indexed: walk each element, call op once per elem.
                    // For list<string> the element is i8* (the indexed string).
                    // For list<i32> we'd need intToString per elem — TODO.
                    std::string inner = fTy.substr(5, fTy.size() - 6);
                    if (inner != "string") return;  // V1: only list<string>
                    llvm::StructType* lstTy = (llvm::StructType*)ctx.getLLVMType(fTy);
                    llvm::Value* sizePtr = ctx.builder.CreateStructGEP(lstTy, fldGep, 0);
                    llvm::Value* dataPtr = ctx.builder.CreateStructGEP(lstTy, fldGep, 2);
                    llvm::Value* size    = ctx.builder.CreateLoad(i32, sizePtr);
                    llvm::Value* data    = ctx.builder.CreateLoad(i8p, dataPtr);
                    // Loop: for (i=0; i<size; ++i) op(idx, data[i], key)
                    llvm::Function* parent = ctx.builder.GetInsertBlock()->getParent();
                    llvm::BasicBlock* loopH = llvm::BasicBlock::Create(ctx.context, "lstidx_h", parent);
                    llvm::BasicBlock* loopB = llvm::BasicBlock::Create(ctx.context, "lstidx_b", parent);
                    llvm::BasicBlock* loopE = llvm::BasicBlock::Create(ctx.context, "lstidx_e", parent);
                    llvm::AllocaInst* iAlloca = ctx.createEntryAlloca(i32, "lstidx_i");
                    ctx.builder.CreateStore(ctx.builder.getInt32(0), iAlloca);
                    ctx.builder.CreateBr(loopH);
                    ctx.builder.SetInsertPoint(loopH);
                    llvm::Value* iCur = ctx.builder.CreateLoad(i32, iAlloca);
                    llvm::Value* cond = ctx.builder.CreateICmpSLT(iCur, size);
                    ctx.builder.CreateCondBr(cond, loopB, loopE);
                    ctx.builder.SetInsertPoint(loopB);
                    llvm::Value* elemGep = ctx.builder.CreateGEP(i8p, data, iCur);
                    llvm::Value* elem    = ctx.builder.CreateLoad(i8p, elemGep);
                    ctx.builder.CreateCall(opFn, {idxHandle, elem, keyVal});
                    llvm::Value* iNext = ctx.builder.CreateAdd(iCur, ctx.builder.getInt32(1));
                    ctx.builder.CreateStore(iNext, iAlloca);
                    ctx.builder.CreateBr(loopH);
                    ctx.builder.SetInsertPoint(loopE);
                }
                // Other types: silently skipped (validated at findings_by site).
            };

            if (!indexedFields.empty() && valStructTy) {
                // Step 1: load old, if not null emit removes for each indexed field.
                llvm::Function* getF = ctx.getFunction("aiml_db_kv_get", i8p, {i8p, i8p});
                llvm::Value* oldPtr = ctx.builder.CreateCall(getF, {kvPtr, keyVal});
                llvm::Value* oldNull = ctx.builder.CreateICmpEQ(
                    oldPtr, llvm::ConstantPointerNull::get(
                        llvm::PointerType::get(ctx.context, 0)));
                llvm::Function* parent = ctx.builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* rmBB   = llvm::BasicBlock::Create(ctx.context, "kvset_idxrm", parent);
                llvm::BasicBlock* contBB = llvm::BasicBlock::Create(ctx.context, "kvset_idxcont", parent);
                ctx.builder.CreateCondBr(oldNull, contBB, rmBB);
                ctx.builder.SetInsertPoint(rmBB);
                for (auto& [fname, mem] : indexedFields) {
                    emitIndexOp("remove", fname, mem, oldPtr);
                }
                ctx.builder.CreateBr(contBB);
                ctx.builder.SetInsertPoint(contBB);
            }

            llvm::Function* f = ctx.getFunction("aiml_db_kv_set",
                ctx.builder.getVoidTy(), {i8p, i8p, i8p, i64});
            ctx.builder.CreateCall(f, {kvPtr, keyVal, valPtr, ctx.builder.getInt64(valSize)});

            // Step 3: add new field values to indexes.
            if (!indexedFields.empty() && valStructTy) {
                for (auto& [fname, mem] : indexedFields) {
                    emitIndexOp("add", fname, mem, valPtr);
                }
            }
            return nullptr;

        } else if ((methodName == "get" || methodName == "get_copy") && !argVals.empty()) {
            llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            // Both methods compile to the same runtime call (aiml_db_kv_get returns
            // the cache pointer). The distinction is enforced at the consumer
            // (visitDeclStmt): get_copy must pair with value-type T, plain get
            // must pair with ptr<T>.
            ctx.kvGetCalled = true;
            if (methodName == "get_copy") ctx.kvGetCopy = true;
            ctx.kvGetValueType = vName;
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

        } else if (methodName == "get_copy_at" && argVals.size() >= 2) {
            // get_copy_at(key, dst) — deep-copy into a CALLER-owned buffer `dst`
            // (spec/31 §6.7.2).  dst is a ptr<T> (pass {'&':[buf]} for a value-type
            // local/member, or a ptr<T> directly).  The runtime frees dst's prior
            // heap, then decodes a fresh independent copy into it (the struct shell is
            // reused; nested heap fields are re-decoded each call → no accumulation,
            // not zero allocation).  Returns i32: 1=copied, 0=missing key, -1=value
            // present but undecodable (schema mismatch/corrupt; dst left zero-inited).
            // Callers MUST only use dst when the result is 1.
            llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            llvm::Value* dstVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]));
            llvm::Function* f = ctx.getFunction("aiml_db_kv_get_copy_at", i32, {i8p, i8p, i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {kvPtr, keyVal, dstVal}));

        } else if (methodName == "has" && !argVals.empty()) {
            llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            llvm::Function* f = ctx.getFunction("aiml_db_kv_has", i32, {i8p, i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {kvPtr, keyVal}));

        } else if (methodName == "len") {
            llvm::Function* f = ctx.getFunction("aiml_db_kv_len", i32, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {kvPtr}));

        } else if (methodName == "delete" && !argVals.empty()) {
            llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));

            // db_indexed delete-hook: load the row, remove from each index.
            // Note: must happen BEFORE the main delete because aiml_db_kv_get
            // returns an mmap-backed pointer that the delete invalidates.
            if (ctx.classes.count(vName) && ctx.classes[vName].type) {
                llvm::StructType* valStructTy = ctx.classes[vName].type;
                std::vector<std::pair<std::string, CompilerContext::StructMember>> indexedFields;
                for (auto& [fname, mem] : ctx.classes[vName].members) {
                    if (mem.db_indexed) indexedFields.push_back({fname, mem});
                }
                if (!indexedFields.empty()) {
                    llvm::Function* getF = ctx.getFunction("aiml_db_kv_get", i8p, {i8p, i8p});
                    llvm::Value* oldPtr = ctx.builder.CreateCall(getF, {kvPtr, keyVal});
                    llvm::Value* oldNull = ctx.builder.CreateICmpEQ(
                        oldPtr, llvm::ConstantPointerNull::get(
                            llvm::PointerType::get(ctx.context, 0)));
                    llvm::Function* parent = ctx.builder.GetInsertBlock()->getParent();
                    llvm::BasicBlock* rmBB   = llvm::BasicBlock::Create(ctx.context, "kvdel_idxrm", parent);
                    llvm::BasicBlock* contBB = llvm::BasicBlock::Create(ctx.context, "kvdel_idxcont", parent);
                    ctx.builder.CreateCondBr(oldNull, contBB, rmBB);
                    ctx.builder.SetInsertPoint(rmBB);
                    for (auto& [fname, mem] : indexedFields) {
                        // Same emitIndexOp pattern as set-hook, but we're in a
                        // separate scope so we duplicate the per-field walk here.
                        llvm::Value* typedBase = ctx.builder.CreateBitCast(
                            oldPtr, llvm::PointerType::get(ctx.context, 0));
                        llvm::Value* fldGep = ctx.builder.CreateStructGEP(
                            valStructTy, typedBase, mem.index);
                        llvm::Value* fNameVal = ctx.builder.CreateGlobalStringPtr(fname);
                        llvm::Function* idxLookup = ctx.getFunction(
                            "aiml_db_index_lookup", i8p, {i8p, i8p});
                        llvm::Value* idxHandle = ctx.builder.CreateCall(
                            idxLookup, {kvPtr, fNameVal});
                        llvm::Function* idxRm = ctx.getFunction(
                            "aiml_db_index_remove",
                            ctx.builder.getVoidTy(), {i8p, i8p, i8p});

                        const std::string& fTy = mem.typeName;
                        if (fTy == "string") {
                            llvm::Value* fVal = ctx.builder.CreateLoad(i8p, fldGep);
                            ctx.builder.CreateCall(idxRm, {idxHandle, fVal, keyVal});
                        } else if (fTy == "i32") {
                            llvm::Value* fVal = ctx.builder.CreateLoad(i32, fldGep);
                            llvm::Value* fStr = StringUtils::intToString(ctx, fVal);
                            ctx.builder.CreateCall(idxRm, {idxHandle, fStr, keyVal});
                        } else if (fTy == "i64") {
                            llvm::Value* fVal = ctx.builder.CreateLoad(i64, fldGep);
                            llvm::Value* fStr = StringUtils::int64ToString(ctx, fVal);
                            ctx.builder.CreateCall(idxRm, {idxHandle, fStr, keyVal});
                        } else if (fTy.rfind("list<", 0) == 0 &&
                                   fTy.substr(5, fTy.size() - 6) == "string") {
                            llvm::StructType* lstTy = (llvm::StructType*)ctx.getLLVMType(fTy);
                            llvm::Value* sizePtr = ctx.builder.CreateStructGEP(lstTy, fldGep, 0);
                            llvm::Value* dataPtr = ctx.builder.CreateStructGEP(lstTy, fldGep, 2);
                            llvm::Value* size    = ctx.builder.CreateLoad(i32, sizePtr);
                            llvm::Value* data    = ctx.builder.CreateLoad(i8p, dataPtr);
                            llvm::BasicBlock* lh = llvm::BasicBlock::Create(ctx.context, "delidx_lh", parent);
                            llvm::BasicBlock* lb = llvm::BasicBlock::Create(ctx.context, "delidx_lb", parent);
                            llvm::BasicBlock* le = llvm::BasicBlock::Create(ctx.context, "delidx_le", parent);
                            llvm::AllocaInst* iAlloca = ctx.createEntryAlloca(i32, "delidx_i");
                            ctx.builder.CreateStore(ctx.builder.getInt32(0), iAlloca);
                            ctx.builder.CreateBr(lh);
                            ctx.builder.SetInsertPoint(lh);
                            llvm::Value* iCur = ctx.builder.CreateLoad(i32, iAlloca);
                            llvm::Value* cond = ctx.builder.CreateICmpSLT(iCur, size);
                            ctx.builder.CreateCondBr(cond, lb, le);
                            ctx.builder.SetInsertPoint(lb);
                            llvm::Value* elemGep = ctx.builder.CreateGEP(i8p, data, iCur);
                            llvm::Value* elem    = ctx.builder.CreateLoad(i8p, elemGep);
                            ctx.builder.CreateCall(idxRm, {idxHandle, elem, keyVal});
                            llvm::Value* iNext = ctx.builder.CreateAdd(iCur, ctx.builder.getInt32(1));
                            ctx.builder.CreateStore(iNext, iAlloca);
                            ctx.builder.CreateBr(lh);
                            ctx.builder.SetInsertPoint(le);
                        }
                    }
                    ctx.builder.CreateBr(contBB);
                    ctx.builder.SetInsertPoint(contBB);
                }
            }

            llvm::Function* f = ctx.getFunction("aiml_db_kv_delete",
                ctx.builder.getVoidTy(), {i8p, i8p});
            ctx.builder.CreateCall(f, {kvPtr, keyVal});
            return nullptr;

        } else if (methodName == "clear") {
            llvm::Function* f = ctx.getFunction("aiml_db_kv_clear",
                ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(f, {kvPtr});
            return nullptr;

        } else if (methodName == "set_blob" && call->args.size() >= 2) {
            // kv.set_blob(key, buffer) — writes buffer data as single binary blob
            llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            // Resolve buffer alloca from arg[1] (local var or <.member)
            llvm::Value* bufAlloca = nullptr;
            if (auto* varE = dynamic_cast<AIML::AST::VariableExpr*>(call->args[1].get())) {
                std::string vn = varE->name;
                if (vn.find("<.") == 0) vn = "this." + vn.substr(2);
                size_t dot = vn.find('.');
                std::string base = (dot != std::string::npos) ? vn.substr(0, dot) : vn;
                std::string prop = (dot != std::string::npos) ? vn.substr(dot + 1) : "";
                Symbol* sym = resolveVariable(base);
                if (sym && prop.empty()) {
                    bufAlloca = sym->alloca_inst
                        ? static_cast<llvm::Value*>(sym->alloca_inst)
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst));
                    // spec/61 §5: a memV3 buffer (LOCAL or GLOBAL) slot holds the handle POINTER, not
                    // the inline struct — load it so the StructGEPs below hit the {…,data} struct.
                    if (sym->is_byref_arg)
                        bufAlloca = ctx.builder.CreateLoad(i8p, bufAlloca, "blob_conh");
                } else if (sym && !prop.empty()) {
                    // struct member access — resolve class name (strip ref<> wrapper)
                    std::string clsName = sym->typeName;
                    if (clsName.find("ref<") == 0 && clsName.back() == '>')
                        clsName = clsName.substr(4, clsName.size() - 5);
                    if (!clsName.empty() && ctx.classes.count(clsName)) {
                        auto& ci = ctx.classes[clsName];
                        if (ci.members.count(prop)) {
                            auto& mem = ci.members[prop];
                            llvm::Value* parentAlloca = sym->alloca_inst
                                ? static_cast<llvm::Value*>(sym->alloca_inst)
                                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst));
                            // For ref<T>, load the pointer first, then GEP
                            if (sym->typeName.find("ref<") == 0) {
                                llvm::Value* objPtr = ctx.builder.CreateLoad(i8p, parentAlloca, "this_ptr");
                                llvm::Value* castPtr = ctx.builder.CreateBitCast(objPtr, ci.type->getPointerTo());
                                bufAlloca = ctx.builder.CreateStructGEP(ci.type, castPtr, mem.index, prop + "_ptr");
                            } else {
                                bufAlloca = ctx.builder.CreateStructGEP(ci.type, parentAlloca, mem.index, prop + "_ptr");
                            }
                        }
                    }
                }
            }
            if (!bufAlloca) {
                LOG_ERROR("set_blob: second argument must be a buffer variable");
                return ctx.ir_gen->getInt32(0);
            }
            llvm::StructType* bufType = llvm::cast<llvm::StructType>(ctx.getLLVMType("buffer"));
            // Extract data pointer and size from buffer struct
            llvm::Value* sizePtr = ctx.builder.CreateStructGEP(bufType, bufAlloca, 0);
            llvm::Value* bufSize = ctx.builder.CreateLoad(i32, sizePtr, "buf_size");
            llvm::Value* dataPtrPtr = ctx.builder.CreateStructGEP(bufType, bufAlloca, 4);
            llvm::Value* dataPtr = ctx.builder.CreateLoad(i8p, dataPtrPtr, "buf_data");
            llvm::Function* f = ctx.getFunction("aiml_db_kv_set_blob",
                ctx.builder.getVoidTy(), {i8p, i8p, i8p, i32});
            ctx.builder.CreateCall(f, {kvPtr, keyVal, dataPtr, bufSize});
            return nullptr;

        } else if (methodName == "get_blob" && call->args.size() >= 2) {
            // kv.get_blob(key, buffer) — reads binary blob into buffer
            llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
            // Resolve buffer alloca from arg[1] (local var or <.member)
            llvm::Value* bufAlloca = nullptr;
            if (auto* varE = dynamic_cast<AIML::AST::VariableExpr*>(call->args[1].get())) {
                std::string vn = varE->name;
                if (vn.find("<.") == 0) vn = "this." + vn.substr(2);
                size_t dot = vn.find('.');
                std::string base = (dot != std::string::npos) ? vn.substr(0, dot) : vn;
                std::string prop = (dot != std::string::npos) ? vn.substr(dot + 1) : "";
                Symbol* sym = resolveVariable(base);
                if (sym && prop.empty()) {
                    bufAlloca = sym->alloca_inst
                        ? static_cast<llvm::Value*>(sym->alloca_inst)
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst));
                    // spec/61 §5: a memV3 buffer (LOCAL or GLOBAL) slot holds the handle POINTER, not
                    // the inline struct — load it so the StructGEPs below hit the {…,data} struct.
                    if (sym->is_byref_arg)
                        bufAlloca = ctx.builder.CreateLoad(i8p, bufAlloca, "blob_conh");
                } else if (sym && !prop.empty()) {
                    std::string clsName = sym->typeName;
                    if (clsName.find("ref<") == 0 && clsName.back() == '>')
                        clsName = clsName.substr(4, clsName.size() - 5);
                    if (!clsName.empty() && ctx.classes.count(clsName)) {
                        auto& ci = ctx.classes[clsName];
                        if (ci.members.count(prop)) {
                            auto& mem = ci.members[prop];
                            llvm::Value* parentAlloca = sym->alloca_inst
                                ? static_cast<llvm::Value*>(sym->alloca_inst)
                                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst));
                            if (sym->typeName.find("ref<") == 0) {
                                llvm::Value* objPtr = ctx.builder.CreateLoad(i8p, parentAlloca, "this_ptr");
                                llvm::Value* castPtr = ctx.builder.CreateBitCast(objPtr, ci.type->getPointerTo());
                                bufAlloca = ctx.builder.CreateStructGEP(ci.type, castPtr, mem.index, prop + "_ptr");
                            } else {
                                bufAlloca = ctx.builder.CreateStructGEP(ci.type, parentAlloca, mem.index, prop + "_ptr");
                            }
                        }
                    }
                }
            }
            if (!bufAlloca) {
                LOG_ERROR("get_blob: second argument must be a buffer variable");
                return ctx.ir_gen->getInt32(0);
            }
            // Call C function: get_blob(kv, key, &out_size) -> data ptr
            llvm::AllocaInst* outSizeAlloca = ctx.createEntryAlloca(i32, "blob_size");
            ctx.builder.CreateStore(ctx.builder.getInt32(0), outSizeAlloca);
            llvm::Function* f = ctx.getFunction("aiml_db_kv_get_blob",
                i8p, {i8p, i8p, i8p});
            llvm::Value* outSizePtr = ctx.builder.CreateBitCast(outSizeAlloca, i8p);
            llvm::Value* blobPtr = ctx.builder.CreateCall(f, {kvPtr, keyVal, outSizePtr});
            llvm::Value* blobSize = ctx.builder.CreateLoad(i32, outSizeAlloca, "blob_sz");
            // memcpy blob data into buffer's data region
            llvm::StructType* bufType = llvm::cast<llvm::StructType>(ctx.getLLVMType("buffer"));
            llvm::Value* dataPtrPtr = ctx.builder.CreateStructGEP(bufType, bufAlloca, 4);
            llvm::Value* dataPtr = ctx.builder.CreateLoad(i8p, dataPtrPtr, "buf_data");
            llvm::Function* memcpyFn = ctx.getFunction("memcpy", i8p, {i8p, i8p, ctx.getSizeType()});
            llvm::Value* sz64 = ctx.builder.CreateZExtOrTrunc(blobSize, ctx.getSizeType());
            ctx.builder.CreateCall(memcpyFn, {dataPtr, blobPtr, sz64});
            // Update buffer size + writeIndex to blob size
            llvm::Value* sizePtr = ctx.builder.CreateStructGEP(bufType, bufAlloca, 0);
            ctx.builder.CreateStore(blobSize, sizePtr);
            llvm::Value* writePtr = ctx.builder.CreateStructGEP(bufType, bufAlloca, 3);
            ctx.builder.CreateStore(blobSize, writePtr);
            // Reset readIndex to 0
            llvm::Value* readPtr = ctx.builder.CreateStructGEP(bufType, bufAlloca, 2);
            ctx.builder.CreateStore(ctx.builder.getInt32(0), readPtr);
            // Return blob size (0 = not found)
            return ctx.ir_gen->wrap(blobSize);

        } else if (methodName == "iter") {
            // kv.iter() → iterator<string> (opaque i8* handle)
            llvm::Function* f = ctx.getFunction("aiml_iter_create_kv", i8p, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {kvPtr}));

        } else if (methodName == "findings_by" || methodName == "finding_by") {
            // db_indexed lookup methods.  Compile-time checks:
            //   1. Arg-Count >= 2 (field_literal, search_value)
            //   2. First arg is a string LiteralExpr (no runtime field names in V1)
            //   3. Value-type has that field
            //   4. Field has db_indexed:true
            //   5. finding_by requires unique:true
            if (call->args.size() < 2) {
                LOG_ERROR(methodName + ": expected (field_name, value), got " +
                          std::to_string(call->args.size()) + " args");
                return ctx.ir_gen->getInt32(0);
            }
            auto* lit = dynamic_cast<AIML::AST::LiteralExpr*>(call->args[0].get());
            if (!lit || lit->type != "string") {
                LOG_ERROR(methodName + ": first arg must be a string literal "
                          "(the field name).  Dynamic field names are not "
                          "supported in V1.");
                return ctx.ir_gen->getInt32(0);
            }
            std::string fieldName = lit->value;
            if (!ctx.classes.count(vName)) {
                LOG_ERROR(methodName + ": Kv value type '" + vName +
                          "' is unknown (no model/class registered)");
                return ctx.ir_gen->getInt32(0);
            }
            auto& vci = ctx.classes[vName];
            auto memIt = vci.members.find(fieldName);
            if (memIt == vci.members.end()) {
                LOG_ERROR(methodName + ": field '" + fieldName + "' does not "
                          "exist on model '" + vName + "'");
                return ctx.ir_gen->getInt32(0);
            }
            auto& mem = memIt->second;
            if (!mem.db_indexed) {
                LOG_ERROR(methodName + ": field '" + fieldName + "' on model '" +
                          vName + "' is not db_indexed.  Add "
                          "`{ db_indexed: true }` to the field declaration.");
                return ctx.ir_gen->getInt32(0);
            }
            if (methodName == "finding_by" && !mem.db_indexed_unique) {
                LOG_ERROR("finding_by: field '" + fieldName + "' on model '" +
                          vName + "' is not unique.  Use findings_by, or add "
                          "`unique: true` to the modifier.");
                return ctx.ir_gen->getInt32(0);
            }

            // Resolve index handle at runtime: aiml_db_index_lookup(kv, field_name).
            llvm::Value* fNameVal = ctx.builder.CreateGlobalStringPtr(fieldName);
            llvm::Function* idxLookup = ctx.getFunction(
                "aiml_db_index_lookup", i8p, {i8p, i8p});
            llvm::Value* idxHandle = ctx.builder.CreateCall(idxLookup, {kvPtr, fNameVal});

            // Convert search value to char* depending on field type.
            llvm::Value* searchVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]));
            const std::string& fTy = mem.typeName;
            if (fTy == "i32") {
                searchVal = StringUtils::intToString(ctx, searchVal);
            } else if (fTy == "i64") {
                searchVal = StringUtils::int64ToString(ctx, searchVal);
            }
            // string and list<string> already pass string args; other types
            // (f32/f64/bool) are caught here as we don't support them yet.
            else if (fTy != "string" && fTy.rfind("list<", 0) != 0) {
                LOG_ERROR(methodName + ": field '" + fieldName + "' has type '" +
                          fTy + "' which is not supported by db_indexed in V1");
                return ctx.ir_gen->getInt32(0);
            }

            if (methodName == "finding_by") {
                // Returns const char* (or "" if no match).
                llvm::Function* f = ctx.getFunction(
                    "aiml_db_index_finding", i8p, {i8p, i8p});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {idxHandle, searchVal}));
            }
            // findings_by: returns list<string> via out-param.
            // spec/61 §5 (memV3): list<string> is a refcounted HANDLE (aiml_list_handle_release).
            // Allocate a real heap handle — its {size,cap,data} struct pointer IS the out-param the
            // fill writes into (mirrors str.split). A STACK alloca would make the caller's binding
            // "own" stack memory → its scope-drop frees a stack pointer, and a direct
            // `return: [{ call: [<.arts.findings_by, …] }]` (MediaStore.art_owner_cards) would return
            // a by-value %list_string struct against a handle-ptr return type → invalid IR
            // ("return type does not match operand type of return inst").
            if (ctx.memV3) {
                llvm::Function* nw = ctx.getFunction("aiml_list_handle_new", i8p, {});
                llvm::Value* handle = ctx.builder.CreateCall(nw, {}, "findings_handle");
                ListImpl::initList(ctx, handle, i8p, 8);
                llvm::Function* f = ctx.getFunction(
                    "aiml_db_index_findings", ctx.builder.getVoidTy(), {i8p, i8p, i8p});
                ctx.builder.CreateCall(f, {idxHandle, searchVal, handle});
                return ctx.ir_gen->wrap(handle);
            }
            llvm::Type* listTy = ctx.getLLVMType("list<string>");
            if (!listTy) {
                LOG_ERROR(methodName + ": list<string> type unavailable");
                return ctx.ir_gen->getInt32(0);
            }
            llvm::AllocaInst* outList = ctx.createEntryAlloca(listTy, "findings_buf");
            // Initialize the list (size=0, capacity=0, data=null).
            ctx.builder.CreateStore(llvm::Constant::getNullValue(listTy), outList);
            llvm::Value* outPtr = ctx.builder.CreateBitCast(outList, i8p);
            llvm::Function* f = ctx.getFunction(
                "aiml_db_index_findings", ctx.builder.getVoidTy(), {i8p, i8p, i8p});
            ctx.builder.CreateCall(f, {idxHandle, searchVal, outPtr});
            // Return the list struct by value (loaded from alloca).
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(listTy, outList, "findings_val"));
        }
        LOG_ERROR("Unknown Kv method: " + methodName);
        return ctx.ir_gen->getInt32(0);
    }
    // ── iterator<T> methods ──────────────────────────────────────────────────
    else if (className.rfind("iterator<", 0) == 0 && className.back() == '>') {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Value* iterPtr = ctx.builder.CreateLoad(i8p,
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), "iter_ptr");

        if (methodName == "next") {
            llvm::Function* f = ctx.getFunction("aiml_iter_next", i8p, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {iterPtr}));

        } else if (methodName == "has_next") {
            llvm::Function* f = ctx.getFunction("aiml_iter_has_next", i32, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {iterPtr}));

        } else if (methodName == "reset") {
            llvm::Function* f = ctx.getFunction("aiml_iter_reset",
                ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(f, {iterPtr});
            return nullptr;

        }
        LOG_ERROR("Unknown iterator method: " + methodName);
        return ctx.ir_gen->getInt32(0);
    }
    // ── VecStore methods ──────────────────────────────────────────────────────
    else if (className == "VecStore") {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
        llvm::Value* vsPtr = ctx.builder.CreateLoad(i8p,
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), "vs_ptr");

        // Helper: get alloca for a VariableExpr argument (supports "var.field" property access)
        auto getArgAlloca = [&](size_t argIdx) -> llvm::Value* {
            if (argIdx >= call->args.size()) return nullptr;
            if (auto* varE = dynamic_cast<AIML::AST::VariableExpr*>(call->args[argIdx].get())) {
                std::string vname = varE->name;
                // Try direct variable first
                Symbol* sym = resolveVariable(vname);
                if (sym) {
                    return sym->alloca_inst
                        ? static_cast<llvm::Value*>(sym->alloca_inst)
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst));
                }
                // Try "base.field" → resolve base, then GEP to field
                size_t dot = vname.find('.');
                if (dot != std::string::npos) {
                    std::string base = vname.substr(0, dot);
                    std::string field = vname.substr(dot + 1);
                    Symbol* baseSym = resolveVariable(base);
                    if (baseSym) {
                        std::string clsName = baseSym->typeName;
                        if (clsName.find("ref<") == 0 && clsName.back() == '>')
                            clsName = clsName.substr(4, clsName.size() - 5);
                        if (!clsName.empty() && ctx.classes.count(clsName)) {
                            auto& ci = ctx.classes[clsName];
                            if (ci.type && ci.members.count(field)) {
                                auto& mem = ci.members[field];
                                llvm::Value* parentAlloca = baseSym->alloca_inst
                                    ? static_cast<llvm::Value*>(baseSym->alloca_inst)
                                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseSym->ir_alloca_inst));
                                if (baseSym->typeName.find("ref<") == 0) {
                                    llvm::Value* objPtr = ctx.builder.CreateLoad(i8p, parentAlloca, "obj_ptr");
                                    llvm::Value* castPtr = ctx.builder.CreateBitCast(objPtr, ci.type->getPointerTo());
                                    return ctx.builder.CreateStructGEP(ci.type, castPtr, mem.index, field + "_ptr");
                                }
                                return ctx.builder.CreateStructGEP(ci.type, parentAlloca, mem.index, field + "_ptr");
                            }
                        }
                    }
                }
            }
            return nullptr;
        };

        // Helper: get data ptr and size from list<f32> alloca
        auto getListDataAndSize = [&](llvm::Value* listAlloca)
                -> std::pair<llvm::Value*, llvm::Value*> {
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            // spec/61 §5: a memV3 list local's slot holds the handle POINTER — load it so the GEPs hit
            // the {size,cap,data} struct, not the handle-pointer bytes (else vec insert/search read a
            // garbage size + data ptr → crash in aiml_db_vec_insert, test_db Phase C).
            if (ctx.memV3) {
                auto* i8pH = ctx.builder.getInt8Ty()->getPointerTo();
                listAlloca = ctx.builder.CreateLoad(i8pH, listAlloca, "vec_list_conh");
            }
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
            llvm::Value* listAlloca  = ctx.createEntryAlloca(listTy, "vecres_alloca");
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
                    llvm::AllocaInst* tmp = ctx.createEntryAlloca(v->getType(), "ts_tmp");
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
            llvm::Value* listAlloca  = ctx.createEntryAlloca(listTy, "tsrange_alloca");
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
         std::string resolvedMangled = mangledName;  // the name whose FunctionInfo we actually call
         llvm::Function* func = ctx.module.getFunction(mangledName);
         if (!func) {
             std::string currClass = className;
             while (!currClass.empty() && ctx.classes.count(currClass)) {
                 std::string pName = ctx.classes[currClass].parentName;
                 if (!pName.empty()) {
                     currClass = pName;
                     func = ctx.module.getFunction(currClass + "_" + methodName);
                     if (func) { resolvedMangled = currClass + "_" + methodName; break; }
                 } else {
                     break;
                 }
             }
         }
         if (!func) {
             // Chained field-method dispatch: e.g. c.mana_cost.put → fieldName="mana_cost", subMethod="put"
             size_t chainDot = methodName.find('.');
             if (chainDot != std::string::npos) {
                 std::string fieldName = methodName.substr(0, chainDot);
                 std::string subMethod = methodName.substr(chainDot + 1);
                 auto& classInfo = ctx.classes[className];
                 auto fieldIt = classInfo.members.find(fieldName);
                 if (fieldIt != classInfo.members.end()) {
                     // Get GEP to the field within the struct
                     llvm::Value* rawBase = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
                     // If base is a pointer (ref/ptr), load it first
                     auto* baseElemRaw = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(baseElemTy));
                     if (baseElemRaw->isPointerTy())
                         rawBase = ctx.builder.CreateLoad(baseElemRaw, rawBase, "deref_this");
                     llvm::StructType* structTy = classInfo.type;
                     llvm::Value* fieldPtr = ctx.builder.CreateStructGEP(structTy, rawBase, fieldIt->second.index, fieldName + "_ptr");

                     // Create a temporary Symbol for the field, then recursively dispatch
                     Symbol fieldSym;
                     fieldSym.ir_alloca_inst = ctx.ir_gen->wrap(static_cast<llvm::Value*>(fieldPtr));
                     fieldSym.typeName = fieldIt->second.typeName;
                     fieldSym.ir_type = ctx.ir_gen->getPointerTy();
                     // spec/61 §5 Option B (memV3): a list<T>/cascade<X> field is a POINTER to a
                     // handle. Mark the temp as is_byref_arg with the list struct as element type so
                     // method dispatch derefs the slot once → the handle (the {size,cap,data} ptr).
                     bool memV3ListFld = ctx.memV3 &&
                         (fieldSym.typeName.rfind("list<", 0) == 0 || fieldSym.typeName.rfind("cascade<", 0) == 0)
                         && fieldSym.typeName.back() == '>';
                     bool memV3MapFld = ctx.memV3 &&
                         fieldSym.typeName.rfind("map<", 0) == 0 && fieldSym.typeName.back() == '>';
                     bool memV3BufFld = ctx.memV3 && (fieldSym.typeName == "buffer" ||
                         (fieldSym.typeName.rfind("buffer<", 0) == 0 && fieldSym.typeName.back() == '>'));
                     if (memV3BufFld) {
                         fieldSym.is_byref_arg = true;
                         fieldSym.ir_element_type = ctx.ir_gen->wrapType(ctx.getLLVMType("buffer"));
                     } else if (memV3ListFld) {
                         fieldSym.is_byref_arg = true;
                         fieldSym.ir_element_type = ctx.ir_gen->wrapType(ListImpl::getListStructType(ctx));
                     } else if (memV3MapFld) {
                         // Map struct type: parse map<K:V> for the k/v LLVM types.
                         std::string minner = fieldSym.typeName.substr(4, fieldSym.typeName.length() - 5);
                         size_t mcolon = minner.find(':');
                         std::string mkName = (mcolon == std::string::npos) ? minner : minner.substr(0, mcolon);
                         std::string mvName = (mcolon == std::string::npos) ? std::string("i32") : minner.substr(mcolon + 1);
                         auto* i8pT = ctx.builder.getInt8Ty()->getPointerTo();
                         llvm::Type* mkTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(mkName)));
                         llvm::Type* mvTy = ctx.isRefcountModel(mvName) ? i8pT
                             : static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(mvName)));
                         if (!mkTy) mkTy = ctx.builder.getInt32Ty();
                         if (!mvTy) mvTy = ctx.builder.getInt32Ty();
                         fieldSym.is_byref_arg = true;
                         fieldSym.ir_element_type = ctx.ir_gen->wrapType(MapImpl::getMapStructType(ctx, mkTy, mvTy));
                     } else {
                         fieldSym.ir_element_type = ctx.ir_gen->wrapType(fieldIt->second.type);
                     }
                     return visitObjectMethodCall(baseName + "." + fieldName, subMethod, &fieldSym, call);
                 }
             }
             LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "Unknown method: " + className + "_" + methodName);
             return ctx.ir_gen->getInt32(0);
         }
         // spec/61 §3.1 caller-move for a DIRECT method call: an argument passed to a
         // `consumed` parameter is moved into the callee — remove the caller's scope-drop
         // (safe: at worst a leak, never a double-free). argLifetimes includes the implicit
         // `this` at index 0, so explicit call->args[j] lines up with argLifetimes[j+1].
         if (ctx.lifetimeDropsEnabled) {
             auto mfit = ctx.functions.find(resolvedMangled);
             if (mfit != ctx.functions.end() && !mfit->second.argLifetimes.empty()) {
                 const auto& lifos = mfit->second.argLifetimes;
                 for (size_t j = 0; j < call->args.size() && (j + 1) < lifos.size(); ++j) {
                     if (lifos[j + 1] != "consumed") continue;
                     consumeLocalDrop(call->args[j].get());
                     // directly-passed interpolation temp moved into the callee (see the
                     // free-function path) — remove it from the statement-temp decref list.
                     if (j < argVals.size())
                         consumeStringTemp(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[j])));
                 }
             }
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
         // Implicit ptr<Concrete> → ptr<Interface> upcast per arg, by checking
         // expected LLVM param type vs source AIML type.
         llvm::FunctionType* fnTy = func->getFunctionType();
         for (size_t i = 0; i < argVals.size(); ++i) {
             IRValue* a = argVals[i];
             // Param index in fnTy: +1 for `this`.
             size_t paramIdx = i + 1;
             llvm::Type* expected = (paramIdx < fnTy->getNumParams())
                 ? fnTy->getParamType(paramIdx) : nullptr;
             if (expected && expected->isStructTy()) {
                 auto* expectedSt = llvm::cast<llvm::StructType>(expected);
                 if (expectedSt->hasName() &&
                     expectedSt->getName().startswith("AimlFatPtr_") &&
                     i < call->args.size()) {
                     if (auto* ve = dynamic_cast<VariableExpr*>(call->args[i].get())) {
                         Symbol* srcSym = resolveVariable(ve->name);
                         if (srcSym) {
                             llvm::Value* rawArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                             llvm::Value* fat = buildInterfaceUpcast(rawArg, srcSym->typeName, expectedSt);
                             if (fat) {
                                 a = ctx.ir_gen->wrap(fat);
                             }
                         }
                     }
                 }
             }
             // "expected ptr, got struct" coercion — mirrors coerceArgs() for
             // free-function calls.  Common case: caller passes a local
             // variable typed list<T>/map<K:V>; visitExpression returned the
             // loaded struct value, but callee parameter is `ptr` (struct
             // arg passed by reference).  Re-evaluate as lvalue (alloca-ptr).
             //
             // SUBTLETY: when the source symbol is itself a by-ref param
             // (sym->is_byref_arg), its alloca holds a POINTER, not the
             // struct.  Address-of-alloca would be ptr-to-ptr-to-struct
             // (one indirection too many).  In that case we must LOAD the
             // pointer from the alloca and pass *that* as ptr-to-struct.
             // Repro: tests/test_class_method_chain_list_arg.aiml.
             if (expected && expected->isPointerTy() && i < call->args.size()) {
                 llvm::Value* rawArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                 if (rawArg && rawArg->getType()->isStructTy()) {
                     // plain variable OR dotted member-access (out.card) → address.
                     std::string lvTy;
                     if (IRValue* addr = emitLValueAddress(call->args[i].get(), lvTy)) {
                         llvm::Value* passVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(addr));
                         if (passVal->getType() != expected)
                             passVal = ctx.builder.CreateBitCast(passVal, expected);
                         a = ctx.ir_gen->wrap(passVal);
                     }
                 }
             }
             // Hard type-mismatch guard (mirrors coerceArgs() for free functions):
             // a numeric parameter (i32/enum/bool/float) receiving a pointer value
             // (e.g. a string literal) is never valid — report it HERE with a
             // source location instead of failing later in the LLVM IR verifier.
             if (expected && (expected->isIntegerTy() || expected->isFloatingPointTy())) {
                 llvm::Value* rawA = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                 if (rawA && rawA->getType()->isPointerTy()) {
                     std::string hint =
                         expected->isIntegerTy()
                             ? " — passing a string/pointer where an integer- or "
                               "enum-typed argument is expected; use EnumType.<value> "
                               "or an int literal"
                             : " — passing a string/pointer where a float-typed "
                               "argument is expected";
                     LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "Call to '" +
                               func->getName().str() + "': argument " +
                               std::to_string(i + 1) + " type mismatch" + hint);
                 }
             }
             methodArgs.push_back(a);
         }
         // Inside a try block a method call must be an `invoke` unwinding to the
         // catch landingpad — otherwise an exception thrown by the callee (e.g.
         // persist_from_req → InvalidCascadeException) sails past the handler
         // (uncaught → AOT std::terminate).  Same fix as the free-function path.
         IRValue* callResult;
         if (ctx.catchBlock) {
             llvm::BasicBlock* contBB = llvm::BasicBlock::Create(
                 ctx.context, "invoke_cont",
                 ctx.builder.GetInsertBlock()->getParent());
             IRBlock* contWrapped  = ctx.ir_gen->wrapBlock(contBB);
             IRBlock* catchWrapped = ctx.ir_gen->wrapBlock(ctx.catchBlock);
             const std::string invName =
                 func->getReturnType()->isVoidTy() ? "" : "call_method";
             callResult = ctx.ir_gen->createInvoke(
                 func->getName().str(), contWrapped, catchWrapped, methodArgs, invName);
             ctx.builder.SetInsertPoint(contBB);
         } else {
             callResult = ctx.ir_gen->createCall(func->getName().str(), methodArgs, "call_method");
         }
         // If the method returns a struct (list<T>, model, etc.), store the value
         // into a temporary alloca and return the pointer.  This matches what
         // builtin handlers do and what visitSetStmt/visitDeclStmt expect for
         // memcpy-based struct assignment.
         if (callResult) {
             llvm::Value* raw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(callResult));
             if (raw && raw->getType()->isStructTy()) {
                 llvm::AllocaInst* tmp = ctx.createEntryAlloca(raw->getType(), "method_struct_tmp");
                 ctx.builder.CreateStore(raw, tmp);
                 return ctx.ir_gen->wrap(static_cast<llvm::Value*>(tmp));
             }
             // spec/61 §3.2 Part 2: a `producer` METHOD returning a fresh STRING used as a transient
             // sub-expression must be decref'd at statement end (see the free-function path). A
             // consuming decl/set/push/return removes it via consumeStringTemp first.
             if (ctx.lifetimeDropsEnabled && ctx.stringProducerFns.count(methodName) > 0
                 && !ctx.stmtTempStack.empty())
                 ctx.stmtTempStack.back().push_back(raw);
         }
         return callResult;
    }

    LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "Unknown method: " + methodName + " on variable " + baseName);
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

        llvm::Value* stylePtr = nullptr;
        if (call->args.size() >= 3) {
            if (auto* styleVar = dynamic_cast<VariableExpr*>(call->args.back().get())) {
                Symbol* styleSym = resolveVariable(styleVar->name);
                if (styleSym && styleSym->typeName == "JsonStyle") {
                    IRValue* baseAlloca = styleSym->ir_alloca_inst
                        ? styleSym->ir_alloca_inst : ctx.ir_gen->wrap(styleSym->alloca_inst);
                    stylePtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
                } else {
                    LOG_ERROR("sys.serialize: optional style argument must be a JsonStyle variable");
                    return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr("(error)"));
                }
            } else {
                LOG_ERROR("sys.serialize: optional style argument must be a JsonStyle variable");
                return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr("(error)"));
            }
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
                return ctx.ir_gen->wrap(SerializeImpl::serialize(ctx, ptrVal, innerType, stylePtr));
            }
        }

        // AIML v2: a refcount-MODEL local's alloca holds a %M* (pointer to the object), NOT an
        // inline struct — so serialize the LOADED pointer, exactly like the ptr<T> case above.
        // Passing the alloca (a pointer-to-%M*) makes SerializeImpl read the %M* bytes as the first
        // struct field → empty/garbage output (the ability-text cache produced identical keys →
        // false cache hits + a read-past-buffer SIGSEGV). See probe_ser.aiml.
        if (argSym && ctx.isRefcountModel(argType)) {
            if (!SerializeImpl::isSerializable(ctx, argType)) {
                LOG_ERROR("sys.serialize: type '" + argType + "' is not serialisable");
                return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr("(error)"));
            }
            IRValue* baseAlloca = argSym->ir_alloca_inst
                ? argSym->ir_alloca_inst : ctx.ir_gen->wrap(argSym->alloca_inst);
            llvm::Value* objVal = ctx.builder.CreateLoad(
                llvm::PointerType::get(ctx.context, 0),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), "ser_modelptr");
            return ctx.ir_gen->wrap(SerializeImpl::serialize(ctx, objVal, argType, stylePtr));
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
            return ctx.ir_gen->wrap(SerializeImpl::serialize(ctx, rawAlloca, argType, stylePtr));
        }

        // Scalars and enums: evaluate expression to get the loaded value
        IRValue* argVal = visitExpression(call->args[0].get());
        if (!argVal) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
        llvm::Value* val = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVal));

        return ctx.ir_gen->wrap(SerializeImpl::serialize(ctx, val, argType, stylePtr));

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
        llvm::Type* sz  = ctx.getSizeType();

    // Helper: malloc a new empty GenericList, init it, return raw i8*
    llvm::StructType* listTy  = ListImpl::getListStructType(ctx);
    llvm::DataLayout  dl(&ctx.module);
    uint64_t          listBytes = dl.getTypeAllocSize(listTy);
    llvm::Function*   mallocFn  = ctx.getFunction("malloc", i8p, {sz});

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
// sys.arg_dump(struct_var) — parse argv into struct fields
//
// Syntax:  - call: [sys.arg_dump, cfg]
//   where cfg is a variable of a user-defined model/class type.
//
// Flag-to-field mapping:
//   --field-name  or  --field_name  →  field "field_name"
//   -flag         →  field "flag"
//   i32 fields without a value (or next arg is another flag) → set to 1 (boolean)
//   Unknown flags → print error + exit(1)
// ─────────────────────────────────────────────────────────────────────────────
IRValue* Compiler::visitSysArgDumpCall(CallExpr* call) {
    using namespace llvm;

    if (call->args.empty()) {
        LOG_ERROR("sys.arg_dump: requires exactly 1 argument (struct variable)");
        return ctx.ir_gen->getInt32(0);
    }

    // arg[0] must be a variable of a struct type
    auto* varExpr = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get());
    if (!varExpr) {
        LOG_ERROR("sys.arg_dump: argument must be a struct variable");
        return ctx.ir_gen->getInt32(0);
    }

    Symbol* sym = resolveVariable(varExpr->name);
    if (!sym) {
        LOG_ERROR("sys.arg_dump: variable '" + varExpr->name + "' not found");
        return ctx.ir_gen->getInt32(0);
    }

    const std::string& typeName = sym->typeName;
    if (!ctx.classes.count(typeName) || !ctx.classes[typeName].type) {
        LOG_ERROR("sys.arg_dump: '" + typeName + "' is not a known struct type");
        return ctx.ir_gen->getInt32(0);
    }

    auto& ci = ctx.classes[typeName];
    // Collect fields sorted by index
    std::vector<std::pair<std::string, CompilerContext::StructMember>> sortedFields;
    for (auto& [fname, fmem] : ci.members) {
        sortedFields.push_back({fname, fmem});
    }
    std::sort(sortedFields.begin(), sortedFields.end(),
              [](auto& a, auto& b){ return a.second.index < b.second.index; });

    int numFields = (int)sortedFields.size();
    if (numFields == 0) return ctx.ir_gen->getInt32(0);

    // Compute byte offsets via DataLayout
    const auto& dl = ctx.module.getDataLayout();
    const auto* sl = dl.getStructLayout(ci.type);

    // Determine type codes: 0=i32, 1=f32, 2=string(i8*), 3=i64
    auto typeCode = [](const std::string& tn) -> int {
        if (tn == "i32" || tn == "u32" || tn == "i8" || tn == "u8" || tn == "i16" || tn == "u16") return 0;
        if (tn == "f32") return 1;
        if (tn == "string") return 2;
        if (tn == "i64" || tn == "u64") return 3;
        if (tn == "f64") return 1;  // downcast to f32 parse
        return 0;  // default: treat as i32
    };

    Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
    Type* i8pp = i8p->getPointerTo();
    Type* i32Ty = ctx.builder.getInt32Ty();
    Type* i64Ty = ctx.builder.getInt64Ty();

    // Build parallel arrays on the stack:
    //   names[N]   = i8** (array of string pointers)
    //   types[N]   = i32* (array of type codes)
    //   offsets[N] = i32* (array of byte offsets)
    ArrayType* ptrArrTy = ArrayType::get(i8p, numFields);
    ArrayType* i32ArrTy = ArrayType::get(i32Ty, numFields);

    AllocaInst* namesArr   = ctx.createEntryAlloca(ptrArrTy, "argdump_names");
    AllocaInst* typesArr   = ctx.createEntryAlloca(i32ArrTy, "argdump_types");
    AllocaInst* offsetsArr = ctx.createEntryAlloca(i32ArrTy, "argdump_offsets");

    for (int fi = 0; fi < numFields; fi++) {
        auto& [fname, fmem] = sortedFields[fi];
        uint64_t byteOffset = sl->getElementOffset(fmem.index);
        int tc = typeCode(fmem.typeName);

        Value* namePtr   = ctx.builder.CreateConstGEP2_32(ptrArrTy, namesArr,   0, fi);
        Value* typePtr   = ctx.builder.CreateConstGEP2_32(i32ArrTy, typesArr,   0, fi);
        Value* offsetPtr = ctx.builder.CreateConstGEP2_32(i32ArrTy, offsetsArr, 0, fi);

        ctx.builder.CreateStore(ctx.builder.CreateGlobalStringPtr(fname), namePtr);
        ctx.builder.CreateStore(ctx.builder.getInt32(tc), typePtr);
        ctx.builder.CreateStore(ctx.builder.getInt32((int)byteOffset), offsetPtr);
    }

    // Get struct alloca (the variable's storage)
    Value* structAlloca = static_cast<Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst));

    // Load __argc and __argv globals
    GlobalVariable* argcVar = ctx.module.getGlobalVariable("__argc", true);
    GlobalVariable* argvVar = ctx.module.getGlobalVariable("__argv", true);
    if (!argcVar || !argvVar) {
        LOG_ERROR("sys.arg_dump: __argc/__argv globals not found (is this a JIT context?)");
        return ctx.ir_gen->getInt32(0);
    }
    Value* argcVal = ctx.builder.CreateLoad(i32Ty, argcVar, "argc");
    Value* argvVal = ctx.builder.CreateLoad(i8pp,  argvVar, "argv");

    // Get element pointers (decay arrays to pointers)
    Value* namesPtr   = ctx.builder.CreateConstGEP2_32(ptrArrTy, namesArr,   0, 0);
    Value* typesPtr   = ctx.builder.CreateConstGEP2_32(i32ArrTy, typesArr,   0, 0);
    Value* offsetsPtr = ctx.builder.CreateConstGEP2_32(i32ArrTy, offsetsArr, 0, 0);
    Value* structName = ctx.builder.CreateGlobalStringPtr(typeName);

    // Declare aiml_arg_dump_impl if not yet in module
    Function* implFn = ctx.module.getFunction("aiml_arg_dump_impl");
    if (!implFn) {
        FunctionType* ft = FunctionType::get(
            ctx.builder.getVoidTy(),
            {i8p, i8pp, i32Ty->getPointerTo(), i32Ty->getPointerTo(),
             i32Ty, i8p, i32Ty, i8pp},
            false);
        implFn = Function::Create(ft, Function::ExternalLinkage, "aiml_arg_dump_impl", ctx.module);
    }

    // Cast struct alloca to i8*
    Value* structPtr = ctx.builder.CreateBitCast(structAlloca, i8p);

    ctx.builder.CreateCall(implFn, {
        structPtr, namesPtr, typesPtr, offsetsPtr,
        ctx.builder.getInt32(numFields),
        structName,
        argcVal, argvVal
    });

    return ctx.ir_gen->getInt32(0);
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
    llvm::AllocaInst* eidAlloca = ctx.createEntryAlloca(i32t, "rs_eid");
    ctx.builder.CreateStore(ctx.builder.getInt32(1), eidAlloca);
    llvm::AllocaInst* maxAlloca = ctx.createEntryAlloca(i32t, "rs_max");
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
        // Only pass as many component ptrs as the function actually expects.
        // Extra stores may be used only for intersection (filtering), not passed as args.
        size_t numCompParams = fnTy->getNumParams() - (hasEid ? 1 : 0);
        for (size_t j = 0; j < storePtrs.size() && j < numCompParams; j++) {
            llvm::Value* ptr = ctx.builder.CreateCall(getPtrFn, {storePtrs[j], eid}, "comp_ptr");
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
