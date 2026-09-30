#define DEBUG_MODE
#include "ast_compiler.h"
#include "list_impl.h"
#include "map_impl.h"
#include "buffer_impl.h"
#include "chan_impl.h"
#include "string_utils.h"
#include <algorithm>
#include <iostream>
#include <cstdlib>   // std::getenv (AIML_SIZETRACE)
#include <vector>

namespace AIML {
namespace AST {

namespace {

// ── AIML_SIZETRACE (compile-time gated) ──────────────────────────────────────
// The compiler reads AIML_SIZETRACE once (comma-separated function-name tokens,
// or "*"/"all") and instruments only matching functions.  Matching is
// substring-based, so a token like "collect_owned" also matches a mangled
// "CardService_collect_owned".  Emits an aiml_sizetrace() call per traced symbol
// (variable decl / assignment / call result) printing size + address + type.
static bool sizetrace_active(const std::string& fname) {
    static bool inited = false, all = false;
    static std::vector<std::string> tokens;
    if (!inited) {
        inited = true;
        const char* env = std::getenv("AIML_SIZETRACE");
        if (env && *env) {
            std::string v(env);
            if (v == "*" || v == "all") { all = true; }
            else {
                size_t pos = 0;
                while (pos <= v.size()) {
                    size_t c = v.find(',', pos); if (c == std::string::npos) c = v.size();
                    std::string t = v.substr(pos, c - pos);
                    size_t s = t.find_first_not_of(" \t"), e = t.find_last_not_of(" \t");
                    if (s != std::string::npos) tokens.push_back(t.substr(s, e - s + 1));
                    pos = c + 1;
                }
            }
        }
    }
    if (all) return true;
    if (fname.empty()) return false;
    for (auto& t : tokens) if (!t.empty() && fname.find(t) != std::string::npos) return true;
    return false;
}

// Emit aiml_sizetrace(fname, sym, type, size, addr) for symName's current
// symbol-table entry.  No-op if it has no alloca/element type (e.g. a global or
// a decl that errored out).  size = compile-time DataLayout alloc-size;
// addr = runtime storage pointer.
static void emitSizeTrace(CompilerContext& ctx, const std::string& symName) {
    auto it = ctx.symbol_table.find(symName);
    if (it == ctx.symbol_table.end()) return;
    auto& s = it->second;
    if (!s.ir_alloca_inst || !s.ir_element_type) return;
    llvm::Value* allocaV = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(s.ir_alloca_inst));
    llvm::Type*  elemTy  = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(s.ir_element_type));
    if (!allocaV || !elemTy) return;
    uint64_t size = ctx.module.getDataLayout().getTypeAllocSize(elemTy);
    std::string typeStr = s.typeName.empty() ? "?" : s.typeName;

    llvm::PointerType* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
    llvm::Type*        i64t = ctx.builder.getInt64Ty();
    llvm::FunctionCallee fn = ctx.module.getOrInsertFunction(
        "aiml_sizetrace",
        llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p, i8p, i64t, i8p}, false));
    llvm::Value* fnameV = ctx.builder.CreateGlobalStringPtr(ctx.currentFunctionName, "sz_fn");
    llvm::Value* symV   = ctx.builder.CreateGlobalStringPtr(symName, "sz_sym");
    llvm::Value* typeV  = ctx.builder.CreateGlobalStringPtr(typeStr, "sz_ty");
    llvm::Value* sizeV  = llvm::ConstantInt::get(i64t, size);
    llvm::Value* addrV  = ctx.builder.CreatePointerCast(allocaV, i8p, "sz_addr");
    ctx.builder.CreateCall(fn, {fnameV, symV, typeV, sizeV, addrV});
}

// Detects `decl: [name, T, { call: [fn, ...] }]` or `set: [target, { call: [fn, ...] }]`
// where `fn` has a void return type.  Storing a void value into a typed location
// produces malformed IR (LLVM's getPrefTypeAlignment recurses to stack overflow
// on the resulting type ring — see crash on tests/test_import_toplevel_order
// during the libraryMode source-order debugging session 2026-06-04).
//
// Returns true (and logs an error) when the call's target resolves to a void
// function in this module.  Unresolved (non-LLVM) names and method/namespace
// calls fall through silently — they have their own typing rules.
bool rejectVoidCallInit(CompilerContext& ctx, Expr* initExpr,
                        const std::string& targetDesc,
                        const YAML::Node& srcNode) {
    auto* call = dynamic_cast<CallExpr*>(initExpr);
    if (!call) return false;
    // Method/namespace calls are routed through different visit handlers;
    // their typing is checked elsewhere.  Only catch the direct-call form.
    if (call->functionName.find('.') != std::string::npos) return false;
    llvm::Function* fn = ctx.module.getFunction(call->functionName);
    if (!fn) {
        // Try qualified name (current namespace).
        fn = ctx.module.getFunction(ctx.qualify(call->functionName));
    }
    if (!fn) return false;
    if (!fn->getReturnType()->isVoidTy()) return false;
    LOG_ERROR(ctx.fmtLoc(srcNode) + targetDesc +
        " from a void-returning call '" + call->functionName +
        "()' — void values cannot be stored into a typed location. " +
        "Add `return: <T>` + a `- return: ...` to '" + call->functionName +
        "', or use an explicit `- call: [" + call->functionName +
        ", ...]` statement (no init).");
    return true;
}
}  // namespace

namespace {
void coerceOrStoreFieldDefault(CompilerContext& ctx,
                               llvm::Value* targetPtr,
                               const CompilerContext::StructMember& member,
                               llvm::Value* initVal) {
    if (!targetPtr || !initVal) return;

    llvm::Type* targetTy = member.type;
    llvm::Value* storeVal = initVal;

    if (member.typeName == "path" && storeVal->getType()->isPointerTy()) {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Function* internFn = ctx.getFunction("aiml_path_intern", i8p, {i8p});
        storeVal = ctx.builder.CreateCall(internFn, {storeVal}, "path_default_init");
    }

    // spec/61: a STRING member with a literal default stores a raw .rodata pointer, which drops-on's
    // aiml_ser_free_decoded then decref's → segfault (reading the literal's `-8` header). Dup it into
    // a refcounted, decref-safe string. Mirrors the initClassContainerMembers m.initValue path.
    if (member.typeName == "string" && storeVal->getType()->isPointerTy()) {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Function* dupFn = ctx.getFunction("aiml_str_dup", i8p, {i8p});
        storeVal = ctx.builder.CreateCall(dupFn, { storeVal }, "str_default_dup");
    }

    bool copyByMemcpy =
        (member.typeName.find("buffer<") == 0) ||
        (member.typeName.find("list<") == 0) ||
        (member.typeName.find("map<") == 0) ||
        (targetTy && targetTy->isStructTy() && storeVal->getType()->isPointerTy());

    if (copyByMemcpy) {
        uint64_t structSize = ctx.module.getDataLayout().getTypeAllocSize(targetTy);
        llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
        llvm::Function* memcpyFunc = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
        ctx.builder.CreateCall(memcpyFunc, {
            targetPtr,
            storeVal,
            ctx.getSizeConst(structSize)
        });
        return;
    }

    if (targetTy && targetTy != storeVal->getType()) {
        if (targetTy->isIntegerTy() && storeVal->getType()->isIntegerTy()) {
            unsigned dstBits = targetTy->getIntegerBitWidth();
            unsigned srcBits = storeVal->getType()->getIntegerBitWidth();
            if (dstBits < srcBits)
                storeVal = ctx.builder.CreateTrunc(storeVal, targetTy, "default_trunc");
            else if (srcBits == 1)
                storeVal = ctx.builder.CreateZExt(storeVal, targetTy, "default_zext_bool");
            else
                storeVal = ctx.builder.CreateSExt(storeVal, targetTy, "default_sext");
        } else if (targetTy->isFloatingPointTy() && storeVal->getType()->isIntegerTy()) {
            storeVal = ctx.builder.CreateSIToFP(storeVal, targetTy, "default_int_to_fp");
        } else if (targetTy->isIntegerTy() && storeVal->getType()->isFloatingPointTy()) {
            storeVal = ctx.builder.CreateFPToSI(storeVal, targetTy, "default_fp_to_int");
        } else if (targetTy->isFloatingPointTy() && storeVal->getType()->isFloatingPointTy()) {
            storeVal = ctx.builder.CreateFPCast(storeVal, targetTy, "default_fp_cast");
        }
    }

    ctx.builder.CreateStore(storeVal, targetPtr);
}
}

void Compiler::consumeStringTemp(llvm::Value* v) {
    if (!v) return;
    for (auto& frame : ctx.stmtTempStack) {
        for (size_t i = 0; i < frame.size(); ) {
            if (frame[i] == v) frame.erase(frame.begin() + (long)i);
            else ++i;
        }
    }
}

// spec/61 §7: is `v` an OWNED string temp (a fresh interpolation/producer result in the current
// statement's temp list)? Such a value is MOVED into a managed string slot (stored as-is); anything
// else assigned into a managed slot is a BORROW that is DUPLICATED. Query BEFORE consumeStringTemp.
static bool inStringTempStack(CompilerContext& ctx, llvm::Value* v) {
    if (!v) return false;
    for (auto& frame : ctx.stmtTempStack)
        for (llvm::Value* t : frame)
            if (t == v) return true;
    return false;
}

// spec/61 (T1c): decref + clear the current statement's owned-string temps NOW (at the current
// insert point). Used after an if/while CONDITION is evaluated: a condition interpolation temp
// (e.g. `if has_texture("@art:${hsh}")`) must be freed on every path, including a branch that
// terminates (return/break) before the per-statement decref loop in visitStatement would run.
static void decrefCurrentStmtTemps(CompilerContext& ctx) {
    if (!ctx.lifetimeDropsEnabled || ctx.stmtTempStack.empty()) return;
    auto* bb = static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
    if (!bb || bb->getTerminator()) return;
    llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
    for (llvm::Value* t : ctx.stmtTempStack.back()) {
        llvm::Function* dc = ctx.getFunction("aiml_str_decref", ctx.builder.getVoidTy(), {i8p});
        ctx.builder.CreateCall(dc, { t });
    }
    ctx.stmtTempStack.back().clear();
}
static void traceSymbolTarget(CompilerContext& ctx, const char* phase, const std::string& target);
// spec/61 §7 KILL-SWITCH: the managed string-local model (dup-on-copy + assign_copy/move +
// null-slot-on-move + all-strings-owned) is DISABLED pending a fix for an open-match double-free
// (OpenLobbyHandler → nick_for path) the sanitizer probes don't cover. false → the compiler falls
// back to the T1/T1b/T1c behaviour. MUST match kManagedStringLocals in ownership_analyzer.cpp.
static constexpr bool kManagedStringLocals = true;
static bool isManagedStringLocalSym(const Symbol& s);  // spec/61 §7 (defined below)

void Compiler::visitStatement(Stmt* stmt) {
    if (stmt->sourceLine > 0) ctx.currentLine = stmt->sourceLine;
    ctx.stmtTempStack.emplace_back();   // spec/61: per-statement owned-string-temp frame
    if (auto decl = dynamic_cast<DeclStmt*>(stmt)) {
        visitDeclStmt(decl);
        if (sizetrace_active(ctx.currentFunctionName)) emitSizeTrace(ctx, decl->varName);
    }
    else if (auto setStmt = dynamic_cast<SetStmt*>(stmt)) {
        traceSymbolTarget(ctx, "BEFORE_SET", setStmt->target);
        visitSetStmt(setStmt);
        traceSymbolTarget(ctx, "SET", setStmt->target);
        if (sizetrace_active(ctx.currentFunctionName)) emitSizeTrace(ctx, setStmt->target);
    }
    else if (auto exprStmt = dynamic_cast<ExprStmt*>(stmt)) visitExprStmt(exprStmt);
    else if (auto retStmt = dynamic_cast<RetStmt*>(stmt)) visitRetStmt(retStmt);
    else if (auto blockStmt = dynamic_cast<BlockStmt*>(stmt)) visitBlockStmt(blockStmt);
    else if (auto ifStmt = dynamic_cast<IfStmt*>(stmt)) visitIfStmt(ifStmt);
    else if (auto whileStmt = dynamic_cast<WhileStmt*>(stmt)) visitWhileStmt(whileStmt);
    else if (auto tryStmt = dynamic_cast<TryStmt*>(stmt)) visitTry(tryStmt);
    else if (auto throwStmt = dynamic_cast<ThrowStmt*>(stmt)) visitThrow(throwStmt);
    else if (auto idxStmt = dynamic_cast<IdxStmt*>(stmt)) visitIdxStmt(idxStmt);
    else if (auto storeStmt = dynamic_cast<StoreStmt*>(stmt)) visitStoreStmt(storeStmt);
    // spec/61: decref owned string temporaries not consumed by this statement (e.g.
    // interpolation results passed as borrowed call args that the callee copied).
    {
        auto* bb = static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
        if (bb && !bb->getTerminator() && !ctx.stmtTempStack.empty() && ctx.lifetimeDropsEnabled) {
            llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
            for (llvm::Value* t : ctx.stmtTempStack.back()) {
                llvm::Function* dc = ctx.getFunction("aiml_str_decref", ctx.builder.getVoidTy(), {i8p});
                ctx.builder.CreateCall(dc, { t });
            }
        }
    }
    if (!ctx.stmtTempStack.empty()) ctx.stmtTempStack.pop_back();
}
// AIML_TRACE_SYMBOL lifetime-value tracing (defined below, used by decl/set/drop).
static bool aimlTraceSymbol(const std::string& name);
static void emitSymbolValueTrace(CompilerContext& ctx, const char* phase, const std::string& name,
                                 int kind, const std::string& typeName, llvm::Value* addr);
// Map a declared type name to the trace "kind": 1=string, 2=list, 3=map, 0=model, -1=skip(scalar).
static int aimlTraceKind(CompilerContext& ctx, const std::string& tn) {
    if (tn == "string") return 1;
    if (tn.rfind("list<", 0) == 0) return 2;
    if (tn.rfind("map<", 0) == 0) return 3;
    if (ctx.classes.count(tn) && !ctx.classes[tn].isBuiltin) return 0;
    // owned new-box: `ptr<T>` where T is a class/model — the alloca holds a heap pointer
    // (kind 4 in emitSymbolValueTrace loads + prints it). Lets AIML_TRACE_SYMBOL follow a
    // { new: T } local's lifetime (DECL + DROP), which was previously invisible.
    if (tn.rfind("ptr<", 0) == 0 && !tn.empty() && tn.back() == '>') {
        std::string boxed = tn.substr(4, tn.size() - 5);
        if (ctx.classes.count(boxed) && !ctx.classes[boxed].isBuiltin) return 4;
        // ptr<Interface> fat-pointer — kind 6 loads the obj slot (field 0) + prints it, so
        // AIML_TRACE_SYMBOL can follow an owned interface box (e.g. get_base → ptr<CardBase>).
        if (ctx.interfaces.count(boxed)) return 6;
    }
    return -1;
}

// spec/61 §5 Option B (memV3): a list<T>/cascade<X> value-local becomes a refcounted HANDLE.
// The slot holds a pointer to a heap block [rc][pad][{size,cap,data}] (pointer at the struct
// part). Fresh init → aiml_list_handle_new + initList; init from another list var → SHARE
// (incref the source's handle); { copy: L } / { clone: L } → an independent-spine handle.
// A scope-exit decref (kind 2, isListHandle) frees data + block at rc 0.
// spec/61 §5.4: classify a list element type into its runtime free kind. Mirrors the branches in
// emitListHandleDecl below (kept in sync); used by clear()/discarded-pop() so mid-lifetime element
// release agrees with the scope-drop. Does NOT compute the LLVM elem type (callers that need it
// resolve it separately).
void Compiler::classifyListElem(const std::string& elemName, int& elemKind, int64_t& elemSize,
                                llvm::GlobalVariable*& serNodes, int32_t& serNodesCount) {
    elemKind = 0; elemSize = 8; serNodes = nullptr; serNodesCount = 0;
    if (elemName == "string") {
        elemKind = 1; elemSize = 8;
    } else if (ctx.isRefcountModel(elemName)) {
        elemKind = 3; elemSize = 8;
        std::string bareElem = elemName;
        if (bareElem.rfind("shared<", 0) == 0 && bareElem.back() == '>')
            bareElem = bareElem.substr(7, bareElem.size() - 8);
        auto ci = ctx.classes.find(bareElem);
        if (ci != ctx.classes.end()) { serNodes = ci->second.serNodesVar; serNodesCount = ci->second.serNodesCount; }
    } else if (elemName.rfind("ptr<", 0) == 0 && elemName.back() == '>') {
        elemKind = 4; elemSize = 8;
        std::string boxed = elemName.substr(4, elemName.size() - 5);
        if (ctx.interfaces.count(boxed)) {
            elemKind = 6;
            llvm::Type* et = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
            elemSize = et ? (int64_t)ctx.module.getDataLayout().getTypeAllocSize(et) : 16;
        } else {
            auto ci = ctx.classes.find(boxed);
            if (ci != ctx.classes.end()) { serNodes = ci->second.serNodesVar; serNodesCount = ci->second.serNodesCount; }
        }
    } else if (ctx.memV3 && ctx.isMemV3ContainerHandle(elemName)) {
        elemKind = 5;
        elemSize = (int64_t)ctx.module.getDataLayout().getPointerSize();
        int nc = 0; serNodes = elemHandleSerNodes(elemName, nc); serNodesCount = (int32_t)nc;
    } else {
        auto ci = ctx.classes.find(elemName);
        if (ci != ctx.classes.end() && ci->second.hasHeapFields && ci->second.serNodesVar && ci->second.type) {
            elemKind = 2;
            serNodes = ci->second.serNodesVar; serNodesCount = ci->second.serNodesCount;
            elemSize = (int64_t)ctx.module.getDataLayout().getTypeAllocSize(ci->second.type);
        } else {
            elemKind = 0;
            llvm::Type* et = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
            elemSize = et ? (int64_t)ctx.module.getDataLayout().getTypeAllocSize(et) : 8;
        }
    }
}

// spec/29_3 §2 / spec/61 §5.1: the copy/clone operand may be any expression, evaluated exactly once.
// A plain local yields its own slot; anything else is evaluated into a temp slot. A fresh owned call
// result (not a container accessor) is owned by the temp and released after the copy.
llvm::Value* Compiler::containerCopySource(Expr* arg, DeclStmt* decl, llvm::Value*& tempSlot, bool& tempOwned) {
    llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
    tempSlot = nullptr; tempOwned = false;
    if (auto* av = dynamic_cast<VariableExpr*>(arg)) {
        if (av->name.find('.') == std::string::npos && av->name.find('<') == std::string::npos)
            if (Symbol* src = resolveVariable(av->name))
                return src->ir_alloca_inst
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(src->ir_alloca_inst)) : src->alloca_inst;
    }
    IRValue* v = visitExpression(arg);
    llvm::Value* h = v ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v)) : nullptr;
    if (!h || !h->getType()->isPointerTy()) {
        LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + "copy/clone operand of '" + decl->varName +
            "' does not evaluate to a " + decl->typeName + " (spec/29_3 §2).");
        return nullptr;
    }
    tempSlot = ctx.createEntryAlloca(i8p, "copy_src");
    ctx.builder.CreateStore(h, tempSlot);
    if (auto* oc = dynamic_cast<CallExpr*>(arg)) {
        const std::string& fn = oc->functionName;
        size_t dp = fn.rfind('.');
        std::string sn = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
        tempOwned = !(dp != std::string::npos && (sn == "get" || sn == "at" || sn == "first" || sn == "last"));
    }
    return tempSlot;
}

// spec/61 §8: container type of a user call whose result is an owning reference ("" otherwise).
static bool isContainerAccessorCall(const CompilerContext& ctx, const Expr* e, const std::string& type);
std::string Compiler::ownedContainerCallType(Expr* e) {
    auto* ce = dynamic_cast<CallExpr*>(e);
    if (!ce || !ctx.memV3) return "";
    CompilerContext::FunctionInfo* fi = resolveCallTarget(ce->functionName);
    if (!fi || !ctx.isMemV3ContainerHandle(fi->returnTypeName)) return "";
    if (isContainerAccessorCall(ctx, ce, fi->returnTypeName)) return "";
    return fi->returnTypeName;
}

// Free function, or `recv.method` / `<.method` resolved through the receiver class chain.
CompilerContext::FunctionInfo* Compiler::resolveCallTarget(const std::string& functionName) {
    if (auto* fi = resolveFunction(functionName)) return fi;
    size_t dp = functionName.rfind('.');
    if (dp == std::string::npos) return nullptr;
    std::string base = functionName.substr(0, dp), meth = functionName.substr(dp + 1), cls;
    if (base == "<" || base == "this") cls = ctx.currentClassName;
    else if (base.find('.') == std::string::npos) {
        if (Symbol* s = resolveVariable(base)) {
            cls = s->typeName;
            size_t lt = cls.find('<');
            if (lt != std::string::npos && cls.back() == '>'
                && (cls.rfind("ptr<", 0) == 0 || cls.rfind("ref<", 0) == 0 || cls.rfind("shared<", 0) == 0))
                cls = cls.substr(lt + 1, cls.size() - lt - 2);
        }
    }
    while (!cls.empty() && ctx.classes.count(cls)) {
        auto it = ctx.functions.find(cls + "_" + meth);
        if (it != ctx.functions.end()) return &it->second;
        cls = ctx.classes[cls].parentName;
    }
    return nullptr;
}

// spec/61 §5.7: a non-accessor call yields a reference the consumer takes over (no incref).
bool Compiler::isOwningContainerCall(Expr* e, const std::string& type) {
    return dynamic_cast<CallExpr*>(e) && !isContainerAccessorCall(ctx, e, type);
}

// spec/61 §8: heap value-model type of a user call whose by-value result the caller owns ("" otherwise).
std::string Compiler::ownedModelCallType(Expr* e) {
    auto* ce = dynamic_cast<CallExpr*>(e);
    if (!ce) return "";
    CompilerContext::FunctionInfo* fi = resolveCallTarget(ce->functionName);
    if (!fi || !fi->ownsReturn || !ctx.isOwnedValueReturn(fi->returnTypeName)) return "";
    auto ci = ctx.classes.find(fi->returnTypeName);
    return (ci != ctx.classes.end() && ci->second.serNodesVar && ci->second.type) ? fi->returnTypeName : "";
}

// Free the heap of a value-model VALUE (struct) or of the struct a pointer addresses.
void Compiler::releaseModelValue(llvm::Value* v, const std::string& typeName) {
    auto ci = ctx.classes.find(typeName);
    if (!v || !ctx.lifetimeDropsEnabled || ci == ctx.classes.end() || !ci->second.serNodesVar) return;
    llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
    llvm::Value* addr = v;
    if (v->getType()->isStructTy()) {
        addr = ctx.createEntryAlloca(v->getType(), "model_tmp");
        ctx.builder.CreateStore(v, addr);
    } else if (!v->getType()->isPointerTy()) return;
    ctx.builder.CreateCall(ctx.getFunction("aiml_ser_free_decoded", ctx.builder.getVoidTy(),
        {i8p, i8p, ctx.builder.getInt32Ty()}),
        { addr, ci->second.serNodesVar, ctx.builder.getInt32(ci->second.serNodesCount) });
}

// spec/61 class 4: an iterator<T> local passed to a user call is moved (the callee releases it).
void Compiler::moveIteratorArg(Expr* arg) {
    auto* ve = dynamic_cast<VariableExpr*>(arg);
    if (!ve || ve->name.find('.') != std::string::npos || ve->name.find('<') != std::string::npos) return;
    Symbol* s = resolveVariable(ve->name);
    if (s && s->typeName.rfind("iterator<", 0) == 0) consumeLocalDrop(arg);
}

void Compiler::releaseContainerHandle(llvm::Value* handle, const std::string& typeName) {
    if (!handle || !ctx.lifetimeDropsEnabled) return;
    llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
    llvm::Value* slot = ctx.createEntryAlloca(i8p, "ctr_tmp");
    ctx.builder.CreateStore(handle, slot);
    releaseContainerCopyTemp(slot, true, typeName);
}

void Compiler::releaseContainerCopyTemp(llvm::Value* tempSlot, bool tempOwned, const std::string& typeName) {
    if (!tempSlot || !tempOwned) return;
    llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
    int nc = 0; llvm::GlobalVariable* d = elemHandleSerNodes(typeName, nc);
    if (d) ctx.builder.CreateCall(ctx.getFunction("aiml_ser_free_decoded", ctx.builder.getVoidTy(),
               {i8p, i8p, ctx.builder.getInt32Ty()}), { tempSlot, d, ctx.builder.getInt32(nc) });
}

// spec/61 §5.7/§8: `.get/.at/.first/.last` on a container yields a BORROWED element handle; every
// other container-returning call hands the caller an owning reference (producer or not).
static bool isContainerAccessorCall(const CompilerContext& ctx, const Expr* e, const std::string& type) {
    auto* ce = dynamic_cast<const CallExpr*>(e);
    if (!ce) return false;
    size_t dp = ce->functionName.rfind('.');
    if (dp == std::string::npos) return false;
    std::string sn = ce->functionName.substr(dp + 1);
    return (sn == "get" || sn == "at" || sn == "first" || sn == "last") && !ctx.ownsReturnMatches(sn, type);
}

void Compiler::emitListHandleDecl(DeclStmt* decl, bool isCascade) {
    auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
    llvm::Type* i32Ty = ctx.builder.getInt32Ty();
    llvm::Type* i64Ty = ctx.builder.getInt64Ty();

    // Element classification (mirrors the list scope-drop logic below).
    std::string elemName = isCascade ? std::string("i32")
        : decl->typeName.substr(5, decl->typeName.length() - 6);
    int elemKind; int64_t elemSize; llvm::GlobalVariable* serNodes = nullptr; int32_t serNodesCount = 0;
    llvm::Type* elemTy;
    if (elemName == "string") {
        elemKind = 1; elemSize = 8;
        elemTy = i8p;
    } else if (ctx.isRefcountModel(elemName)) {
        elemKind = 3; elemSize = 8;
        // ctx.classes is keyed by the BARE class; a memV3 `list<shared<T>>` has elemName
        // "shared<T>". Strip the wrapper so the element's serNodes are found — otherwise the
        // scope-drop's per-element decref runs with a null descriptor and never frees the
        // co-owned element at rc 0 (leak). v2 elemName is already bare → strip is a no-op.
        std::string bareElem = elemName;
        if (bareElem.rfind("shared<", 0) == 0 && bareElem.back() == '>')
            bareElem = bareElem.substr(7, bareElem.size() - 8);
        auto ci = ctx.classes.find(bareElem);
        if (ci != ctx.classes.end()) { serNodes = ci->second.serNodesVar; serNodesCount = ci->second.serNodesCount; }
        elemTy = i8p;
    } else if (elemName.rfind("ptr<", 0) == 0 && elemName.back() == '>') {
        // spec/61 §5.5 (memV3): list<ptr<T>> elements are OWNING boxes (unique owner). Scope-drop
        // deep-frees each box + fields (elemKind 4) via T's serNodes — else the boxes leaked.
        elemKind = 4; elemSize = 8;
        std::string boxed = elemName.substr(4, elemName.size() - 5);
        if (ctx.interfaces.count(boxed)) {
            // interface polymorphism: `list<ptr<Interface>>` elements are 16-byte fat pointers
            // {obj, vtable} (AimlFatPtr_<Iface>), NOT plain 8-byte pointers. push stores the full fat
            // pointer (16B stride), so the data buffer MUST be sized with the fat-pointer stride —
            // else the initial capacity buffer (elemTy i8p → 8B stride) overflowed and corrupted the
            // heap (realloc: invalid next size) at server boot in build_card_behaviors
            // (list<ptr<CardBehavior>>, 7 pushes). resolveType("ptr<Iface>") is the fat-ptr struct.
            // elemKind 6 (own interface box): the scope-drop strides by elemSize and frees only the
            // obj pointer (field 0) — a plain char** (8B) walk would free the vtable global at odd
            // indices (free(): invalid pointer, test_card_base_engine_api). The concrete type is
            // dynamic so there is no per-element serNodes; the obj shell is freed shallow.
            elemKind = 6;
            elemTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
            if (!elemTy) elemTy = i8p;
            elemSize = (int64_t)ctx.module.getDataLayout().getTypeAllocSize(elemTy);
        } else {
            auto ci = ctx.classes.find(boxed);
            if (ci != ctx.classes.end()) { serNodes = ci->second.serNodesVar; serNodesCount = ci->second.serNodesCount; }
            elemTy = i8p;
        }
    } else if (ctx.memV3 && ctx.isMemV3ContainerHandle(elemName)) {
        // spec/61 §5.7 (memV3): a NESTED CONTAINER element (list<list<T>>/list<map<…>>/list<buffer>)
        // is a refcounted HANDLE (pointer stride). Scope-drop (elemKind 5) releases each element via
        // its handle-rooted descriptor (aiml_ser_free_decoded → LIST_PTR/MAP_PTR/BUFFER_PTR fre →
        // decref the inner handle, recursively freeing at its rc 0).
        elemKind = 5;
        elemSize = (int64_t)ctx.module.getDataLayout().getPointerSize();
        int nc = 0; serNodes = elemHandleSerNodes(elemName, nc); serNodesCount = (int32_t)nc;
        elemTy = i8p;
    } else {
        auto ci = ctx.classes.find(elemName);
        if (ci != ctx.classes.end() && ci->second.hasHeapFields && ci->second.serNodesVar && ci->second.type) {
            elemKind = 2;
            serNodes = ci->second.serNodesVar; serNodesCount = ci->second.serNodesCount;
            elemSize = (int64_t)ctx.module.getDataLayout().getTypeAllocSize(ci->second.type);
            elemTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
        } else {
            elemKind = 0;
            elemTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
            elemSize = elemTy ? (int64_t)ctx.module.getDataLayout().getTypeAllocSize(elemTy) : 8;
        }
    }
    if (!elemTy) elemTy = i8p;

    // Pointer slot for the handle. A memV3 container GLOBAL (§5 storage-location invariance) already
    // owns a pointer-typed global variable (declared + is_byref_arg in the global-registration pass);
    // reuse it as the slot so the initializer's share/adopt/copy writes the global, not a fresh local
    // alloca. A local decl allocates its own slot.
    IRType* ptrTy = ctx.ir_gen->getPointerTy();
    auto gsymIt = ctx.symbol_table.find(decl->varName);
    bool isGlobalDecl = (gsymIt != ctx.symbol_table.end() && gsymIt->second.is_global);
    IRValue* alloca;
    llvm::Value* slot;
    if (isGlobalDecl) {
        alloca = gsymIt->second.ir_alloca_inst;
        slot = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca));
        // gv is program-zero-inited (null); Phase-2 skipped its handle_new for an initialized global.
    } else {
        alloca = ctx.ir_gen->createAlloca(ptrTy, decl->varName);
        slot = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca));
        ctx.builder.CreateStore(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)), slot);
    }

    // Classify the initializer.
    Expr* iv = decl->initialValue.get();
    bool isShareVar = false, isCopy = false, isClone = false, isShareAt = false, isAdoptExpr = false;
    if (iv) {
        if (auto* ve = dynamic_cast<VariableExpr*>(iv)) {
            // A plain local (`a`) → resolveVariable + incref (isShareVar). A MEMBER access
            // (`obj.field`, `<.field`) is a container FIELD handle → share it via visitExpression +
            // incref (isShareAt path); resolveVariable can't resolve a dotted name → would give null
            // (decl:[loc, list<T>, mmm.tagsm] produced an empty/null list → crash on use).
            if (ve->name.find('.') != std::string::npos || ve->name.find('<') != std::string::npos)
                isShareAt = true;
            else if (ve->name.find('-') != std::string::npos)
                // a SLICE expression (`lst-1-3`, `lst-2-`, `lst--2`) parses as a VariableExpr whose
                // name carries dashes; it returns a FRESH owned handle → adopt (no incref). Without
                // this it was misclassified isShareVar, resolveVariable("lst-1-3") failed → the local
                // stayed null → crash on first use (test_list_slice).
                isAdoptExpr = true;
            else
                isShareVar = true;                    // decl:[b, list<T>, a] → SHARE (incref)
        } else if (dynamic_cast<AtExpr*>(iv)) {
            isShareAt = true;                         // spec/61 §5.7: decl:[v, list<T>, {at:[outer,i]}]
                                                      // → the inner handle is SHARED (incref)
        } else if (auto* ce = dynamic_cast<CallExpr*>(iv)) {
            if (ce->functionName == "sys.copy")  isCopy  = true;
            else if (ce->functionName == "sys.clone") isClone = true;
            else {
                // spec/61 §5.7: a container `.get` of a NESTED-CONTAINER element returns a BORROW
                // (visitCall's memV3 container-handle branch, matching `at`) — bind it as an owning
                // alias → incref (isShareAt). Without this the scope-drop decref frees an element
                // still owned by the outer container (test_mkp_math ability_score `.effects.get`
                // double-free). `.pop`/`.take` stay MOVE (transfer) → the generic call path below.
                auto dot = ce->functionName.rfind('.');
                std::string meth = (dot == std::string::npos) ? ce->functionName
                                                              : ce->functionName.substr(dot + 1);
                (void)meth;
                if (ctx.memV3 && ctx.isMemV3ContainerHandle(decl->typeName)
                    && isContainerAccessorCall(ctx, ce, decl->typeName))
                    isShareAt = true;
            }
        }
    }

    llvm::Value* copyTempSlot = nullptr; bool copyTempOwned = false;

    llvm::Value* handle = nullptr;
    if (isShareVar) {
        // Load the source list's handle pointer and incref (aliasing).
        auto* ve = static_cast<VariableExpr*>(iv);
        Symbol* src = resolveVariable(ve->name);
        if (src) {
            llvm::Value* srcSlot = src->ir_alloca_inst
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(src->ir_alloca_inst))
                : src->alloca_inst;
            handle = ctx.builder.CreateLoad(i8p, srcSlot, "listh_src");
            llvm::Function* inc = ctx.getFunction("aiml_list_handle_incref", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(inc, { handle });
        }
    } else if (isShareAt) {
        // spec/61 §5.7: `{at:[outer,i]}` returns the inner container's HANDLE pointer (visitAtExpr,
        // memV3 nested-container branch). Bind it as an owning alias → incref (matching the outer
        // container's decref-per-element at drop). Scope-drop below balances this incref.
        IRValue* v = visitExpression(iv);
        handle = v ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v)) : nullptr;
        if (handle) {
            llvm::Function* inc = ctx.getFunction("aiml_list_handle_incref", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(inc, { handle });
        }
    } else if (isClone) {
        // spec/61 §5.1 (F1): `{ clone: L }` is a DEEP clone via the memoized descriptor walker.
        // The walker reads the source handle from srcSlot, allocates a fresh handle and deep-clones
        // the element DAG (shared/model/recursive elements cloned exactly once), then writes the
        // new handle into `slot`. This is the correct path for move-only (ptr<T>) elements: each
        // element is an independent owner, so the two containers never double-free.
        auto* ce = static_cast<CallExpr*>(iv);
        llvm::Value* srcSlot = ce->args.empty() ? nullptr : containerCopySource(ce->args[0].get(), decl, copyTempSlot, copyTempOwned);
        if (srcSlot) emitContainerCloneDeep(slot, srcSlot, decl->typeName);
        releaseContainerCopyTemp(copyTempSlot, copyTempOwned, decl->typeName);
        handle = nullptr;   // emitContainerCloneDeep wrote `slot` directly
    } else if (isCopy) {
        // spec/61 §10: shallow `{ copy: L }` of a container whose element is MOVE-ONLY (ptr<T>,
        // a unique owner) is a compile error — pointer-copying a unique owner would give two
        // containers that each free the same object (double free). Use `{ clone: L }` (deep).
        if (elemName.rfind("ptr<", 0) == 0) {
            LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + "shallow copy of list<" + elemName +
                ">: the element type is move-only (a unique owner) — {copy} would duplicate one "
                "owner into two containers (double free). Use {clone} (deep) instead (§10/§5.1).");
        }
        // { copy: L } → independent-spine handle with a SHALLOW element copy. For scalar/string
        // elements copy and clone coincide (fresh data buffer; strings shared).
        auto* ce = static_cast<CallExpr*>(iv);
        llvm::Value* srcSlot = ce->args.empty() ? nullptr : containerCopySource(ce->args[0].get(), decl, copyTempSlot, copyTempOwned);
        llvm::Value* srcHandle = srcSlot ? ctx.builder.CreateLoad(i8p, srcSlot, "listh_copysrc") : nullptr;
        llvm::Function* cp = ctx.getFunction("aiml_list_handle_copy", i8p, {i8p, i64Ty, i32Ty});
        handle = ctx.builder.CreateCall(cp, { srcHandle ? srcHandle
                                              : llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)),
                                              ctx.builder.getInt64(elemSize),
                                              ctx.builder.getInt32(elemKind) }, "listh_copy");
        if (elemKind == 2 && serNodes)   // inline value models: member-wise share (§5.1)
            ctx.builder.CreateCall(ctx.getFunction("aiml_list_retain_elems", ctx.builder.getVoidTy(),
                {i8p, i64Ty, i8p, i32Ty}), { handle, ctx.builder.getInt64(elemSize), serNodes, ctx.builder.getInt32(serNodesCount) });
        releaseContainerCopyTemp(copyTempSlot, copyTempOwned, decl->typeName);
    } else if (isAdoptExpr) {
        // spec/61 §5: a slice expression returns a FRESH owned handle (aiml_list_handle_new inside
        // ListImpl::slice) → adopt the pointer directly, no incref.
        IRValue* v = visitExpression(iv);
        handle = v ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v)) : nullptr;
    } else if (iv && dynamic_cast<CallExpr*>(iv)) {
        // spec/61 §5 (memV3): init from a call that RETURNS a list → the callee handed us a handle
        // (moved). Use it directly; no fresh alloc, no incref (we own the returned rc).
        IRValue* cv = visitExpression(iv);
        handle = cv ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(cv)) : nullptr;
    } else {
        // Fresh empty list: new handle + initList on the heap struct.
        llvm::Function* nw = ctx.getFunction("aiml_list_handle_new", i8p, {});
        handle = ctx.builder.CreateCall(nw, {}, "listh_new");
        ListImpl::initList(ctx, handle, elemTy, 4);
    }
    if (handle) ctx.builder.CreateStore(handle, slot);

    // Register the symbol as a by-ref handle: reads/methods deref once (is_byref_arg path),
    // then load/GEP the {size,cap,data} struct.
    Symbol sym;
    sym.ir_alloca_inst  = alloca;
    sym.ir_type         = ptrTy;
    sym.ir_element_type = ctx.ir_gen->wrapType(ListImpl::getListStructType(ctx));
    sym.typeName        = decl->typeName;
    sym.is_const        = decl->is_const;
    sym.is_volatile     = decl->is_volatile;
    sym.lifetime        = decl->lifetime;
    sym.is_byref_arg    = true;
    sym.is_global       = isGlobalDecl;   // preserve global storage class (slot IS the global var)
    OwnershipId declOid = (ctx.ownershipPlan && ctx.ownershipPlan->declarations.count(decl))
        ? ctx.ownershipPlan->declarations.at(decl) : NoOwnership;
    sym.ownershipId     = declOid;
    ctx.symbol_table[decl->varName] = sym;

    // Scope-exit decref (unless persistent). Every list local owns exactly one handle ref
    // (fresh alloc, incref'd share, or fresh copy) → one decref balances it. A GLOBAL is not
    // function-scoped — it lives for the whole program (like the OFF-path inline global, which was
    // never scope-dropped either), so no scope-drop is registered for it.
    if (!isGlobalDecl && decl->lifetime != "persistent" && ctx.lifetimeDropsEnabled && !ctx.cleanupStack.empty()) {
        CompilerContext::ScopeDrop sd;
        sd.addr = slot;
        sd.kind = 2;
        sd.isListHandle = true;
        sd.elemKind = elemKind;
        sd.elemSize = elemSize;
        sd.serNodes = serNodes;
        sd.serNodesCount = serNodesCount;
        sd.ownershipId = declOid;
        sd.debugName = decl->varName; sd.debugType = decl->typeName;
        ctx.cleanupStack.back().push_back(sd);
    }
}

// spec/61 §5 Option B (memV3): a map<K:V> value-local becomes a refcounted HANDLE, mirroring the
// list-handle path. Fresh init → aiml_map_handle_new + initMap; init from another map var → SHARE
// (incref); { copy: M } → independent-spine map; scope-exit decref (kind 3, isListHandle→map).
void Compiler::emitMapHandleDecl(DeclStmt* decl) {
    auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
    llvm::Type* i32Ty = ctx.builder.getInt32Ty();
    llvm::Type* i64Ty = ctx.builder.getInt64Ty();

    // Parse map<K:V>.
    std::string inner = decl->typeName.substr(4, decl->typeName.length() - 5);
    size_t colon = inner.find(':');
    std::string kName = (colon == std::string::npos) ? inner : inner.substr(0, colon);
    std::string vName = (colon == std::string::npos) ? std::string("i32") : inner.substr(colon + 1);
    llvm::Type* kTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(kName)));
    // spec/61 §5.7: a nested container value is a HANDLE slot (pointer stride), like a refcount model.
    llvm::Type* vTyRaw = (ctx.isRefcountModel(vName) || ctx.isMemV3ContainerHandle(vName))
        ? i8p
        : static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(vName)));
    if (!kTy) kTy = i32Ty;
    if (!vTyRaw) vTyRaw = i32Ty;
    llvm::DataLayout dl(&ctx.module);

    // Key/value classification for copy + release.
    int keyKind = (kName == "string") ? 1 : 0;
    int64_t keyStride = (kName == "string") ? 8 : (int64_t)dl.getTypeAllocSize(kTy);
    int valKind; int64_t valStride; llvm::GlobalVariable* valNodes = nullptr; int32_t valNodesCount = 0;
    if (vName == "string") { valKind = 1; valStride = 8; }
    else if (ctx.isMemV3ContainerHandle(vName)) { valKind = 4; valStride = (int64_t)dl.getPointerSize(); }
    else if (vName.rfind("ptr<", 0) == 0 && vName.back() == '>' && ctx.classes.count(vName.substr(4, vName.size() - 5))
             && !ctx.classGroupContainers.count(vName.substr(4, vName.size() - 5))
             && !ctx.cascadeClasses.count(vName.substr(4, vName.size() - 5))) {
        // owning box value: drop frees fields + shell (valKind 5)
        valKind = 5; valStride = (int64_t)dl.getPointerSize();
        auto bci = ctx.classes.find(vName.substr(4, vName.size() - 5));
        if (bci->second.hasHeapFields && bci->second.serNodesVar) { valNodes = bci->second.serNodesVar; valNodesCount = bci->second.serNodesCount; }
    }
    else if (ctx.isRefcountModel(vName)) {
        valKind = 3; valStride = 8;
        // Bare-class key: a memV3 map<K:shared<V>> has vName "shared<V>" (see emitListHandleDecl).
        std::string bareVal = vName;
        if (bareVal.rfind("shared<", 0) == 0 && bareVal.back() == '>')
            bareVal = bareVal.substr(7, bareVal.size() - 8);
        auto ci = ctx.classes.find(bareVal);
        if (ci != ctx.classes.end()) { valNodes = ci->second.serNodesVar; valNodesCount = ci->second.serNodesCount; }
    } else {
        auto ci = ctx.classes.find(vName);
        if (ci != ctx.classes.end() && ci->second.hasHeapFields && ci->second.serNodesVar && ci->second.type) {
            valKind = 2; valNodes = ci->second.serNodesVar; valNodesCount = ci->second.serNodesCount;
            valStride = (int64_t)dl.getTypeAllocSize(ci->second.type);
        } else {
            valKind = 0; valStride = (int64_t)dl.getTypeAllocSize(vTyRaw);
        }
    }

    // Pointer slot. A memV3 container GLOBAL reuses its existing pointer-typed global var (§5).
    IRType* ptrTy = ctx.ir_gen->getPointerTy();
    auto gsymIt = ctx.symbol_table.find(decl->varName);
    bool isGlobalDecl = (gsymIt != ctx.symbol_table.end() && gsymIt->second.is_global);
    IRValue* alloca;
    llvm::Value* slot;
    if (isGlobalDecl) {
        alloca = gsymIt->second.ir_alloca_inst;
        slot = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca));
    } else {
        alloca = ctx.ir_gen->createAlloca(ptrTy, decl->varName);
        slot = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca));
        ctx.builder.CreateStore(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)), slot);
    }

    Expr* iv = decl->initialValue.get();
    // A plain local (`a`) → resolveVariable + incref. A MEMBER access (`obj.field`) or an at-element
    // (`{at:[outer,i]}`, spec/61 §5.7 nested map) is a map FIELD/element handle → share via
    // visitExpression + incref (isShareMember). resolveVariable fails on a dotted name → null map →
    // crash on use (df8).
    bool isShareVar = false, isShareMember = false;
    if (iv) {
        if (auto* ve = dynamic_cast<VariableExpr*>(iv)) {
            if (ve->name.find('.') != std::string::npos || ve->name.find('<') != std::string::npos)
                isShareMember = true;
            else isShareVar = true;
        } else if (dynamic_cast<AtExpr*>(iv)) {
            isShareMember = true;
        }
    }
    bool isCopy = false, isClone = false;
    if (iv) if (auto* ce = dynamic_cast<CallExpr*>(iv)) {
        if (ce->functionName == "sys.copy")  isCopy  = true;
        if (ce->functionName == "sys.clone") isClone = true;
    }
    if (!isCopy && !isClone && isContainerAccessorCall(ctx, iv, decl->typeName)) isShareMember = true;

    llvm::Value* handle = nullptr;
    if (isShareVar) {
        auto* ve = static_cast<VariableExpr*>(iv);
        Symbol* src = resolveVariable(ve->name);
        if (src) {
            llvm::Value* srcSlot = src->ir_alloca_inst
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(src->ir_alloca_inst)) : src->alloca_inst;
            handle = ctx.builder.CreateLoad(i8p, srcSlot, "maph_src");
            llvm::Function* inc = ctx.getFunction("aiml_map_handle_incref", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(inc, { handle });
        }
    } else if (isShareMember) {
        // spec/61 §5.7: member/at map handle → share (incref). visitExpression yields the field/element
        // handle pointer (resolveProperty / at nested-map branch).
        IRValue* v = visitExpression(iv);
        handle = v ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v)) : nullptr;
        if (handle) {
            llvm::Function* inc = ctx.getFunction("aiml_map_handle_incref", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(inc, { handle });
        }
    } else if (isClone) {
        // spec/61 §5.1 (F1): deep clone via the memoized descriptor walker (model/ptr values cloned
        // once each). The walker reads the source handle from srcSlot and writes a fresh handle into
        // `slot`. Required for move-only value types so the two maps never double-free a value.
        auto* ce = static_cast<CallExpr*>(iv);
        llvm::Value* tmpSlot = nullptr; bool tmpOwned = false;
        llvm::Value* srcSlot = ce->args.empty() ? nullptr : containerCopySource(ce->args[0].get(), decl, tmpSlot, tmpOwned);
        if (srcSlot) emitContainerCloneDeep(slot, srcSlot, decl->typeName);
        releaseContainerCopyTemp(tmpSlot, tmpOwned, decl->typeName);
        handle = nullptr;   // emitContainerCloneDeep wrote `slot` directly
    } else if (isCopy) {
        if (vName.rfind("ptr<", 0) == 0) {
            LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + "shallow copy of map<...:" + vName +
                ">: the value type is move-only (a unique owner) — {copy} would duplicate one "
                "owner into two maps (double free). Use {clone} (deep) instead (§10/§5.1).");
        }
        auto* ce = static_cast<CallExpr*>(iv);
        llvm::Value* tmpSlot = nullptr; bool tmpOwned = false;
        llvm::Value* srcSlot = ce->args.empty() ? nullptr : containerCopySource(ce->args[0].get(), decl, tmpSlot, tmpOwned);
        llvm::Value* srcHandle = srcSlot ? ctx.builder.CreateLoad(i8p, srcSlot, "maph_copysrc") : nullptr;
        llvm::Function* cp = ctx.getFunction("aiml_map_handle_copy", i8p, {i8p, i64Ty, i64Ty, i32Ty, i32Ty});
        handle = ctx.builder.CreateCall(cp, { srcHandle ? srcHandle
                                              : llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)),
                                              ctx.builder.getInt64(keyStride), ctx.builder.getInt64(valStride),
                                              ctx.builder.getInt32(keyKind), ctx.builder.getInt32(valKind) }, "maph_copy");
        releaseContainerCopyTemp(tmpSlot, tmpOwned, decl->typeName);
    } else if (iv && dynamic_cast<CallExpr*>(iv)) {
        // spec/61 §5 (memV3): init from a call that RETURNS a map → the callee handed us a handle
        // (moved). Use it directly; no fresh alloc, no incref (we own the returned rc).
        IRValue* cv = visitExpression(iv);
        handle = cv ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(cv)) : nullptr;
    } else {
        llvm::Function* nw = ctx.getFunction("aiml_map_handle_new", i8p, {});
        handle = ctx.builder.CreateCall(nw, {}, "maph_new");
        // spec/61 §5 (memV3): a map LITERAL initializer `{ k: v, ... }` is a DictLiteralExpr. The
        // handle path previously fell through to a fresh EMPTY map and DROPPED the entries → the map
        // was empty at runtime (deck_limit_tbl.contains → 0; legacy/OFF populated it, memV3 did not).
        // Populate the fresh handle with one put per entry, mirroring the inline path (line ~1594).
        auto* dictLit = dynamic_cast<DictLiteralExpr*>(iv);
        int initCap = 4;
        if (dictLit && (int)dictLit->entriesExpr.size() > initCap) initCap = (int)dictLit->entriesExpr.size();
        MapImpl::initMap(ctx, handle, kTy, vTyRaw, initCap);
        if (dictLit && !dictLit->entriesExpr.empty()) {
            auto* mapStructTy = MapImpl::getMapStructType(ctx, kTy, vTyRaw);
            for (auto& [keyStr, valExpr] : dictLit->entriesExpr) {
                llvm::Value* kVal = nullptr;
                if (kName == "string") {
                    kVal = ctx.builder.CreateGlobalStringPtr(keyStr, "mlit_k");
                } else if (kName == "i32") {
                    kVal = ctx.builder.getInt32((int32_t)std::stoll(keyStr));
                } else if (kName == "i64") {
                    kVal = ctx.builder.getInt64(std::stoll(keyStr));
                } else if (ctx.enums.count(kName) || ctx.enums.count(ctx.qualify(kName))) {
                    const auto& enumVals = ctx.enums.count(kName)
                        ? ctx.enums[kName] : ctx.enums[ctx.qualify(kName)];
                    auto it = enumVals.find(keyStr);
                    if (it == enumVals.end()) {
                        LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + "map literal: enum '" + kName +
                                  "' has no value named '" + keyStr + "'");
                        continue;
                    }
                    kVal = ctx.builder.getInt32((int32_t)it->second);
                } else {
                    LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + "map literal: key type '" + kName +
                              "' not supported yet");
                    continue;
                }
                IRValue* vv = visitExpression(valExpr.get());
                if (!vv) continue;
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(vv));
                if (vTyRaw->isIntegerTy() && v->getType()->isIntegerTy() && v->getType() != vTyRaw) {
                    v = ctx.builder.CreateZExtOrTrunc(v, vTyRaw, "mlit_v");
                } else if (vTyRaw->isFloatingPointTy() && v->getType()->isFloatingPointTy() && v->getType() != vTyRaw) {
                    v = ctx.builder.CreateFPCast(v, vTyRaw, "mlit_v");
                }
                MapImpl::put(ctx, handle, kVal, v, mapStructTy, vName == "string");
            }
        }
    }
    if (handle) ctx.builder.CreateStore(handle, slot);

    Symbol sym;
    sym.ir_alloca_inst  = alloca;
    sym.ir_type         = ptrTy;
    sym.ir_element_type = ctx.ir_gen->wrapType(MapImpl::getMapStructType(ctx, kTy, vTyRaw));
    sym.typeName        = decl->typeName;
    sym.is_const        = decl->is_const;
    sym.is_volatile     = decl->is_volatile;
    sym.lifetime        = decl->lifetime;
    sym.is_byref_arg    = true;
    sym.is_global       = isGlobalDecl;   // preserve global storage class (slot IS the global var)
    OwnershipId declOid = (ctx.ownershipPlan && ctx.ownershipPlan->declarations.count(decl))
        ? ctx.ownershipPlan->declarations.at(decl) : NoOwnership;
    sym.ownershipId     = declOid;
    ctx.symbol_table[decl->varName] = sym;

    if (!isGlobalDecl && decl->lifetime != "persistent" && ctx.lifetimeDropsEnabled && !ctx.cleanupStack.empty()) {
        CompilerContext::ScopeDrop sd;
        sd.addr = slot;
        sd.kind = 3;
        sd.isListHandle = true;   // reused flag: kind 3 + isListHandle → aiml_map_handle_release
        sd.keyKind = keyKind;
        sd.keyStride = keyStride;
        sd.elemKind = valKind;    // kind 3 uses elemKind as the VALUE kind
        sd.elemSize = valStride;  // kind 3 uses elemSize as the VALUE stride
        sd.serNodes = valNodes;
        sd.serNodesCount = valNodesCount;
        if (ctx.memV3 && ctx.isMemV3ContainerHandle(vName)) {
            // spec/61 §5.7 outer drop: decref each nested container value via its handle-rooted descriptor.
            int nc = 0;
            if (llvm::GlobalVariable* hd = elemHandleSerNodes(vName, nc)) {
                sd.elemKind = 2; sd.elemSize = valStride; sd.serNodes = hd; sd.serNodesCount = nc;
            }
        }
        sd.ownershipId = declOid;
        sd.debugName = decl->varName; sd.debugType = decl->typeName;
        ctx.cleanupStack.back().push_back(sd);
    }
}

// spec/61 §5 Option B (memV3): a buffer value-local becomes a refcounted HANDLE, mirroring the
// list/map handle path. Fresh init → aiml_buffer_handle_new (data alloc'd on demand by .alloc);
// init from another buffer var → SHARE (incref); { copy } → independent buffer; scope-exit
// decref (kind 8) frees data + block at rc 0. No element ownership (raw bytes).
void Compiler::emitBufferHandleDecl(DeclStmt* decl) {
    auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();

    IRType* ptrTy = ctx.ir_gen->getPointerTy();
    auto gsymIt = ctx.symbol_table.find(decl->varName);
    bool isGlobalDecl = (gsymIt != ctx.symbol_table.end() && gsymIt->second.is_global);
    IRValue* alloca;
    llvm::Value* slot;
    if (isGlobalDecl) {
        alloca = gsymIt->second.ir_alloca_inst;   // memV3 container global reuses its global var (§5)
        slot = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca));
    } else {
        alloca = ctx.ir_gen->createAlloca(ptrTy, decl->varName);
        slot = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca));
        ctx.builder.CreateStore(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)), slot);
    }

    Expr* iv = decl->initialValue.get();
    // Plain local → resolveVariable + incref. Member (`obj.field`) / at-element → share via
    // visitExpression + incref (dotted name would give null via resolveVariable → crash; §5.7).
    bool isShareVar = false, isShareMember = false;
    if (iv) {
        if (auto* ve = dynamic_cast<VariableExpr*>(iv)) {
            if (ve->name.find('.') != std::string::npos || ve->name.find('<') != std::string::npos)
                isShareMember = true;
            else isShareVar = true;
        } else if (dynamic_cast<AtExpr*>(iv)) {
            isShareMember = true;
        }
    }
    bool isCopyClone = false;
    if (iv) if (auto* ce = dynamic_cast<CallExpr*>(iv))
        isCopyClone = (ce->functionName == "sys.copy" || ce->functionName == "sys.clone");
    if (!isCopyClone && isContainerAccessorCall(ctx, iv, decl->typeName)) isShareMember = true;

    llvm::Value* handle = nullptr;
    if (isShareVar) {
        auto* ve = static_cast<VariableExpr*>(iv);
        Symbol* src = resolveVariable(ve->name);
        if (src) {
            llvm::Value* srcSlot = src->ir_alloca_inst
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(src->ir_alloca_inst)) : src->alloca_inst;
            handle = ctx.builder.CreateLoad(i8p, srcSlot, "bufh_src");
            llvm::Function* inc = ctx.getFunction("aiml_buffer_handle_incref", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(inc, { handle });
        }
    } else if (isShareMember) {
        IRValue* v = visitExpression(iv);
        handle = v ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v)) : nullptr;
        if (handle) {
            llvm::Function* inc = ctx.getFunction("aiml_buffer_handle_incref", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(inc, { handle });
        }
    } else if (isCopyClone) {
        auto* ce = static_cast<CallExpr*>(iv);
        llvm::Value* tmpSlot = nullptr; bool tmpOwned = false;
        llvm::Value* srcSlot = ce->args.empty() ? nullptr : containerCopySource(ce->args[0].get(), decl, tmpSlot, tmpOwned);
        llvm::Value* srcHandle = srcSlot ? ctx.builder.CreateLoad(i8p, srcSlot, "bufh_copysrc") : nullptr;
        llvm::Function* cp = ctx.getFunction("aiml_buffer_handle_copy", i8p, {i8p});
        handle = ctx.builder.CreateCall(cp, { srcHandle ? srcHandle
                                              : llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)) }, "bufh_copy");
        releaseContainerCopyTemp(tmpSlot, tmpOwned, decl->typeName);
    } else if (iv && dynamic_cast<CallExpr*>(iv)) {
        // spec/61 §5 (memV3): init from a call that RETURNS a buffer → use the returned handle.
        IRValue* cv = visitExpression(iv);
        handle = cv ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(cv)) : nullptr;
    } else {
        llvm::Function* nw = ctx.getFunction("aiml_buffer_handle_new", i8p, {});
        handle = ctx.builder.CreateCall(nw, {}, "bufh_new");
    }
    if (handle) ctx.builder.CreateStore(handle, slot);

    Symbol sym;
    sym.ir_alloca_inst  = alloca;
    sym.ir_type         = ptrTy;
    sym.ir_element_type = ctx.ir_gen->wrapType(ctx.getLLVMType("buffer"));
    sym.typeName        = decl->typeName;
    sym.is_const        = decl->is_const;
    sym.is_volatile     = decl->is_volatile;
    sym.lifetime        = decl->lifetime;
    sym.is_byref_arg    = true;
    sym.is_global       = isGlobalDecl;   // preserve global storage class (slot IS the global var)
    OwnershipId declOid = (ctx.ownershipPlan && ctx.ownershipPlan->declarations.count(decl))
        ? ctx.ownershipPlan->declarations.at(decl) : NoOwnership;
    sym.ownershipId     = declOid;
    ctx.symbol_table[decl->varName] = sym;

    if (!isGlobalDecl && decl->lifetime != "persistent" && ctx.lifetimeDropsEnabled && !ctx.cleanupStack.empty()) {
        CompilerContext::ScopeDrop sd;
        sd.addr = slot;
        sd.kind = 8;   // buffer handle → aiml_buffer_handle_release
        sd.isListHandle = true;   // memV3 refcounted handle → always-emit on the return path (§5)
        sd.ownershipId = declOid;
        sd.debugName = decl->varName; sd.debugType = decl->typeName;
        ctx.cleanupStack.back().push_back(sd);
    }
}

// spec/61 §5.4/§7: a value Model is MOVE-ONLY when it (transitively) owns a unique resource that a
// member-wise value-copy would duplicate → double-free / dangling. Such a model cannot be value-copied
// (bind ref<Model> to borrow, or { clone } for an independent copy). The move-only-inducing fields are
// an owning ptr<T> (a unique box) and a `tensor` (Class-4 resource, single-owner via owns_data). A
// shared<T> field is co-ownable (incref, NOT move-only); a ref<T> field is a non-owning borrow (NOT
// move-only). Nested value-model fields are checked transitively (cycle-guarded).
static bool modelIsMoveOnly(CompilerContext& ctx, const std::string& className,
                            std::set<std::string>& seen) {
    if (!seen.insert(className).second) return false;   // cycle guard (value nesting is acyclic anyway)
    auto it = ctx.classes.find(className);
    if (it == ctx.classes.end() || it->second.isBuiltin || !it->second.type) return false;
    for (auto& mkv : it->second.members) {
        const std::string& t = mkv.second.typeName;
        if (t.rfind("ptr<", 0) == 0) return true;       // unique owning box
        if (t == "tensor") return true;                 // Class-4 unique resource
        // A bare nested value-model field: recurse (a ptr<>/ref<>/shared<> wrapper is not a value
        // nesting — ptr is caught above, ref/shared are borrow/co-own, neither makes the parent
        // move-only through a value copy of this slot).
        if (ctx.classes.count(t) && !ctx.classes[t].isBuiltin && modelIsMoveOnly(ctx, t, seen))
            return true;
    }
    return false;
}
static bool modelIsMoveOnly(CompilerContext& ctx, const std::string& className) {
    std::set<std::string> seen;
    return modelIsMoveOnly(ctx, className, seen);
}

void Compiler::visitDeclStmt(DeclStmt* decl) {
    if (ctx.isRefContainerType(decl->typeName))
        LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + ctx.refContainerError("variable '" + decl->varName + "'", decl->typeName));
    ctx.validateIdentifier(decl->varName, "variable", decl->sourceNode);

    IRType* ty = ctx.ir_gen->resolveType(decl->typeName);
    if (!ty) return;

    // spec/61 §5.4/§7: binding a VALUE `Model` to an `at`/`get` read MATERIALISES a value-copy
    // (member-wise, incref fields). That is forbidden when the model is MOVE-ONLY — it has a
    // `ptr<T>` field (a unique owner): copying would duplicate the one box into two owners
    // (double-free). `at`/`get` on such an element yields a `ref<T>` borrow, so the only legal
    // bindings are `ref<Model>` (borrow) or `{ clone: … }` (a fresh box). Reject the value-copy
    // form here (memV3; the accessor read never destructively moves out of a container).
    if (ctx.memV3 && decl->initialValue) {
        const std::string& dtn = decl->typeName;
        if (modelIsMoveOnly(ctx, dtn)) {
            Expr* iv0 = decl->initialValue.get();
            std::string acc;
            if (dynamic_cast<AtExpr*>(iv0)) acc = "at";
            else if (auto* ce = dynamic_cast<CallExpr*>(iv0)) {
                const std::string& fn = ce->functionName; size_t dp = fn.rfind('.');
                std::string sn = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                if (sn == "at" || sn == "get" || sn == "first" || sn == "last" || sn == "get_copy") acc = sn;
            }
            if (!acc.empty()) {
                LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + "decl: '" + decl->varName + "' binds a VALUE '"
                    + dtn + "' to a '" + acc + "' read, which value-copies a MOVE-ONLY model (it owns a "
                    "ptr<T>/tensor resource, §7) — duplicating a unique owner (double-free / dangling). "
                    "Bind 'ref<" + dtn + ">' to borrow it, or use '{ clone: … }' for an independent copy "
                    "(§5.4/§7).");
                return;
            }
        }
    }

    // spec/61 §5.4: `.get`/`.at`/`.first`/`.last` on a container (list/map/cascade) or a Class-6 store
    // (Kv/Store/TimeSeries/VecStore) yields a `ref<T>` BORROW — it never hands out a second owner. So
    // binding the read to a `ptr<T>` (a UNIQUE OWNER) is illegal: the borrow would be treated as owned
    // and freed on drop/reassign (the test_model / db-reopen double-free class). Bind `ref<T>` to
    // borrow, or use `get_copy` for an independent owned copy (which already exists — §5.4). Scoped to
    // KNOWN borrow-returning receivers so a user method happening to be named `get` is not caught.
    if (ctx.memV3 && decl->initialValue && decl->typeName.rfind("ptr<", 0) == 0) {
        // Determine (accessor-name, receiver-name, receiver-type) for either the method form
        // `{ call: [recv.get, i] }` or the shorthand `{ at: [recv, i] }` (both are §5.4 borrow reads;
        // ref<Iface> from a ptr-list element now shares the fat-pointer representation, so the pervasive
        // `list<ptr<Iface>>` dispatch idiom migrates to ref<Iface> cleanly).
        std::string meth, recv, rt;
        if (auto* ce = dynamic_cast<CallExpr*>(decl->initialValue.get())) {
            const std::string& fn = ce->functionName;
            size_t dp = fn.rfind('.');
            if (dp != std::string::npos) {
                std::string m = fn.substr(dp + 1);
                if (m == "get" || m == "at" || m == "first" || m == "last") { meth = m; recv = fn.substr(0, dp); }
            }
        } else if (auto* ax = dynamic_cast<AtExpr*>(decl->initialValue.get())) {
            // `{ at: [c, i] }` with NO trailing member path is exactly `c.at(i)` — a borrow read.
            if (ax->memberPath.empty())
                if (auto* cv = dynamic_cast<VariableExpr*>(ax->container.get()))
                    if (cv->name.find('.') == std::string::npos) { meth = "at"; recv = cv->name; }
        }
        if (!meth.empty()) {
            if (recv == "yaml") rt = "yaml";   // builtin namespace: yaml.get → node view (borrow)
            else if (Symbol* rs = resolveVariable(recv)) rt = rs->typeName;
            bool borrowSource = rt == "yaml"
                || rt.rfind("list<", 0) == 0 || rt.rfind("map<", 0) == 0
                || rt.rfind("cascade<", 0) == 0 || rt.rfind("array<", 0) == 0
                || rt.rfind("Kv<", 0) == 0 || rt.rfind("Store<", 0) == 0
                || rt.rfind("TimeSeries<", 0) == 0 || rt == "VecStore";
            if (borrowSource) {
                std::string inner = decl->typeName.substr(4, decl->typeName.size() - 5);
                LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + "decl: '" + decl->varName + "' binds a "
                    "'" + meth + "' read to ptr<" + inner + ">, but '" + meth + "' yields a "
                    "ref<" + inner + "> BORROW (the container/store keeps ownership; it never "
                    "hands out a second owner). A ptr<T> is a UNIQUE OWNER, so this would free a "
                    "box it does not own (double-free on drop/reassign). Bind 'ref<" + inner +
                    ">' to borrow it, or use '" + recv + ".get_copy(…)' for an independent owned "
                    "copy (§5.4).");
                return;
            }
        }
    }

    // ── spec/61 §5 Option B (memV3): list<T>/cascade<X> value-LOCAL = refcounted HANDLE ──────────
    // The slot holds a POINTER to a heap block [i32 rc][i32 pad][{size,cap,data}]; the stored
    // pointer targets the {size,cap,data} part so every ListImpl op is unchanged, reached via the
    // is_byref_arg deref. Assignment shares (incref → aliasing); scope-exit decrefs (frees data +
    // block at rc 0). LOCALS only here (globals/model fields stay inline — Stage B). The whole
    // block is gated on ctx.memV3, so the OFF/deployed path is byte-identical.
    {
        bool isListLocal = (decl->typeName.rfind("list<", 0) == 0 && decl->typeName.back() == '>');
        bool isCascadeLocal = (decl->typeName.rfind("cascade<", 0) == 0 && decl->typeName.back() == '>');
        // Two entry conditions into the handle-decl path:
        //  • LOCAL: not yet in symbol_table + inside a cleanup scope (the normal case).
        //  • GLOBAL WITH INITIALIZER (§5 storage-location invariance): a container global is a handle
        //    now, so its share/adopt/copy initializer must run the handle path (writing the existing
        //    global var), not the OFF inline fall-through. A global WITHOUT an initializer is prepared
        //    by the Phase-2 prologue instead (handle_new), so it is NOT taken here.
        auto gsIt = ctx.symbol_table.find(decl->varName);
        bool declGlobalInit = ctx.memV3 && !decl->is_extern && decl->initialValue
            && gsIt != ctx.symbol_table.end() && gsIt->second.is_global
            && ctx.isMemV3ContainerHandle(decl->typeName);
        bool declLocal = !ctx.symbol_table.count(decl->varName) && !ctx.cleanupStack.empty();
        if (ctx.memV3 && (isListLocal || isCascadeLocal) && !decl->is_extern
            && (declLocal || declGlobalInit)) {
            emitListHandleDecl(decl, isCascadeLocal);
            return;
        }
        bool isMapLocal = (decl->typeName.rfind("map<", 0) == 0 && decl->typeName.back() == '>');
        if (ctx.memV3 && isMapLocal && !decl->is_extern
            && (declLocal || declGlobalInit)) {
            emitMapHandleDecl(decl);
            return;
        }
        bool isBufLocal = (decl->typeName == "buffer"
            || (decl->typeName.rfind("buffer<", 0) == 0 && decl->typeName.back() == '>'));
        if (ctx.memV3 && isBufLocal && !decl->is_extern
            && (declLocal || declGlobalInit)) {
            emitBufferHandleDecl(decl);
            return;
        }
    }

    // spec/61 §6.2/§10: illegal cross-kind assignments (localized, no flow analysis; memV3-gated →
    // OFF path unchanged). The §6.2 matrix forbids:
    //   • ptr<T>    ← shared<T>  (cannot extract unique ownership from a co-owned object), and
    //   • ptr<T>    ← ref<T>     (a borrow does not own), and
    //   • shared<T> ← ref<T>     (a borrow cannot become a co-owner).
    if (ctx.memV3 && decl->initialValue) {
        bool tgtPtr    = decl->typeName.rfind("ptr<", 0) == 0;
        bool tgtShared = decl->typeName.rfind("shared<", 0) == 0;
        if (tgtPtr || tgtShared) {
            if (auto* sv = dynamic_cast<VariableExpr*>(decl->initialValue.get())) {
                Symbol* ss = resolveVariable(sv->name);
                const std::string sty = ss ? ss->typeName : std::string();
                if (tgtPtr && sty.rfind("shared<", 0) == 0)
                    LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + "illegal shared<T> → ptr<T> assignment ('"
                        + decl->varName + "' <- '" + sv->name + "'): a unique owner cannot be extracted "
                        "from a shared (co-owned) object (§6.2/§10). Borrow it as ref<T>, or co-own as shared<T>.");
                else if (tgtPtr && sty.rfind("ref<", 0) == 0)
                    LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + "illegal ref<T> → ptr<T> assignment ('"
                        + decl->varName + "' <- '" + sv->name + "'): a borrow does not own, so it cannot "
                        "become a unique owner (§6.2/§10).");
                else if (tgtShared && sty.rfind("ref<", 0) == 0)
                    LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + "illegal ref<T> → shared<T> assignment ('"
                        + decl->varName + "' <- '" + sv->name + "'): a borrow does not own, so it cannot "
                        "become a co-owner (§6.2/§10).");
            }
        }
    }

    // ── AIML v2 (doc/aiml_v2_impl_plan.md P1/P5): refcounted MODEL local ─────────────────────────
    // A heap-field model is a shared refcounted heap object. The local's alloca holds a %M*
    // (pointer), NOT an inline struct — exactly like a ptr<M> box, so the existing pointer-element
    // auto-deref (resolveProperty / resolvePropertyWritePtr: `baseElemTy->isPointer()`) gives field
    // read/write for free. Entirely isolated from the v1 path below; only reachable when
    // AIML_MODEL_REFCOUNT is ON (isRefcountModel gates it). OFF stays byte-for-byte v1.
    if (ctx.isRefcountModel(decl->typeName)) {
        if (ctx.symbol_table.count(decl->varName)) {
            LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + "Variable shadowing is not allowed: '"
                      + decl->varName + "' is already declared in an outer scope");
            return;
        }
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        IRType* ptrTy = ctx.ir_gen->getPointerTy();
        IRValue* palloca = ctx.ir_gen->createAlloca(ptrTy, decl->varName);
        llvm::Value* pallocaRaw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(palloca));
        ctx.builder.CreateStore(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)), pallocaRaw);

        // ctx.classes is keyed by the BARE class name. Under memV3 the decl type is `shared<T>`
        // (wrapped), so look up T — otherwise `ci` is end() and the size/field-init/scope-drop all
        // silently fall away (leak, or a deref of end() for a no-init decl). Under v2 the refcount
        // decl type is already the bare name, so the strip is a no-op → byte-identical.
        std::string bareModelClass = decl->typeName;
        if (ctx.memV3 && bareModelClass.rfind("shared<", 0) == 0 && bareModelClass.back() == '>')
            bareModelClass = bareModelClass.substr(7, bareModelClass.size() - 8);
        auto ci = ctx.classes.find(bareModelClass);

        // Produce the object pointer to store into the slot.
        llvm::Value* objPtr = nullptr;
        bool shareIncref = false;   // true → the init is a borrow/share of an existing object
        if (!decl->initialValue) {
            // No initializer: allocate a fresh, default-initialised model (refcount 1).
            uint64_t allocSize = (ci != ctx.classes.end() && ci->second.padded_size > 0)
                ? (uint64_t)ci->second.padded_size
                : ctx.module.getDataLayout().getTypeAllocSize(ci->second.type);
            llvm::Function* allocFn = ctx.getFunction("aiml_model_alloc", i8p, { ctx.getSizeType() });
            objPtr = ctx.builder.CreateCall(allocFn, { ctx.getSizeConst(allocSize) }, "refmodel_alloc");
            initClassContainerMembers(objPtr, bareModelClass);
        } else {
            // Evaluate the initializer → a %M* (NewExpr / producer call → owned move; a
            // VariableExpr / AtExpr / member-access → a shared borrow → incref).
            Expr* iv = decl->initialValue.get();
            // spec/61 §6.2 adopt (`{ share: p }`, lowered to the bare source before here): a
            // ptr<T> SOURCE is turned into a co-owned shared<T> block (realloc [rc=1|fields], move
            // the fields, free the old block, source moved-out). A shared<T> source co-owns (incref);
            // a plain value source is not an adopt. Detect it from the source variable's type.
            // memV3-only: under the legacy v2 `modelRefcount` flag a `ptr<T>` is itself a MODL, so
            // the realloc-and-raw-free adopt would be wrong there — keep v2 byte-identical.
            bool adoptFromPtr = false;
            if (ctx.memV3) if (auto* ve = dynamic_cast<VariableExpr*>(iv)) {
                Symbol* ssym = resolveVariable(ve->name);
                if (ssym && ssym->typeName.rfind("ptr<", 0) == 0) adoptFromPtr = true;
            }
            if (dynamic_cast<AtExpr*>(iv)) shareIncref = true;
            else if (auto* ve = dynamic_cast<VariableExpr*>(iv)) { (void)ve; shareIncref = !adoptFromPtr; }
            else if (auto* ce = dynamic_cast<CallExpr*>(iv)) {
                const std::string& fn = ce->functionName;
                size_t dp = fn.rfind('.');
                std::string sn = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                // A producer (owns_return) call MOVES its fresh ref; a plain accessor
                // (at/get/first/last that is NOT owns_return) SHARES → incref.
                if ((sn == "at" || sn == "get" || sn == "first" || sn == "last")
                    && !ctx.ownsReturnMatches(sn, decl->typeName))
                    shareIncref = true;
            }
            // spec/61 §6.2: a `{ new: T }` initialising a shared<T> local must allocate a REFCOUNTED
            // [rc=1|fields] block. visitNewExpr keys that off deserializeTargetType (the decl's
            // shared<> type); this branch previously never set it, so `new` fell to the header-less
            // calloc path and a later incref/decref read the rc header out of bounds (valgrind OOB,
            // rc silently a no-op → leak). Set it around the init eval, restore after.
            std::string savedDTT = ctx.deserializeTargetType;
            if (ctx.memV3) ctx.deserializeTargetType = decl->typeName;
            IRValue* initV = visitExpression(iv);
            if (ctx.memV3) ctx.deserializeTargetType = savedDTT;
            objPtr = initV ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initV)) : nullptr;
            if (objPtr && adoptFromPtr) {
                // ptr<T> → shared<T>: realloc into [rc=1|fields], move fields, free old block raw.
                // Size from the BARE class T (`ci` above already resolved it).
                uint64_t allocSize = (ci != ctx.classes.end() && ci->second.padded_size > 0)
                    ? (uint64_t)ci->second.padded_size
                    : (ci != ctx.classes.end() && ci->second.type
                        ? ctx.module.getDataLayout().getTypeAllocSize(ci->second.type) : 0);
                llvm::Function* adoptFn = ctx.getFunction("aiml_model_adopt", i8p, { i8p, ctx.getSizeType() });
                objPtr = ctx.builder.CreateCall(adoptFn, { objPtr, ctx.getSizeConst(allocSize) }, "model_adopt");
                // The source ptr<T> is moved-out: null its slot + drop its scope-cleanup so the
                // old (now-freed) block is not freed again.
                consumeLocalDrop(iv);
            }
            if (objPtr && ctx.kvGetCalled && ci != ctx.classes.end() && ci->second.serNodesVar) {
                // AIML v2 P6b: kv.get/get_copy returns a pointer into the runtime's DECODE CACHE
                // (a plain struct buffer the runtime owns — NOT a MODL). Deep-copy it into a fresh
                // refcounted MODL shell so this local owns a decref-safe, independent reference.
                uint64_t allocSize = (ci->second.padded_size > 0)
                    ? (uint64_t)ci->second.padded_size
                    : ctx.module.getDataLayout().getTypeAllocSize(ci->second.type);
                llvm::Function* allocFn = ctx.getFunction("aiml_model_alloc", i8p, { ctx.getSizeType() });
                llvm::Value* newObj = ctx.builder.CreateCall(allocFn, { ctx.getSizeConst(allocSize) }, "kv_model_alloc");
                llvm::Type* ptrT = llvm::PointerType::get(ctx.context, 0);
                llvm::Function* dcp = ctx.getFunction("aiml_ser_deep_copy",
                    ctx.builder.getVoidTy(), {ptrT, ptrT, ptrT, ctx.builder.getInt32Ty()});
                ctx.builder.CreateCall(dcp, { newObj, objPtr,
                    static_cast<llvm::Value*>(ci->second.serNodesVar),
                    ctx.builder.getInt32(ci->second.serNodesCount) });
                objPtr = newObj;
                shareIncref = false;   // owned fresh MODL — no incref (move)
                ctx.kvGetCalled = false; ctx.kvGetCopy = false;
            } else if (objPtr && shareIncref) {
                llvm::Function* inc = ctx.getFunction("aiml_model_incref", ctx.builder.getVoidTy(), {i8p});
                ctx.builder.CreateCall(inc, { objPtr });
            }
        }
        if (objPtr) ctx.builder.CreateStore(objPtr, pallocaRaw);

        OwnershipId declOid = (ctx.ownershipPlan && ctx.ownershipPlan->declarations.count(decl))
            ? ctx.ownershipPlan->declarations.at(decl) : NoOwnership;
        Symbol sym;
        sym.ir_alloca_inst  = palloca;
        sym.ir_type         = ptrTy;
        sym.ir_element_type  = ptrTy;   // pointer element → field access auto-derefs (kept as-is per design)
        sym.typeName        = decl->typeName;
        sym.is_const        = decl->is_const;
        sym.lifetime        = decl->lifetime;
        sym.ownershipId     = declOid;
        ctx.symbol_table[decl->varName] = sym;

        // spec/61 §7 / v2 P5: scope-exit decref (kind 7). Frees the shell + inner heap at refcount 0.
        if (ctx.lifetimeDropsEnabled && decl->lifetime != "persistent"
            && !ctx.cleanupStack.empty() && ci != ctx.classes.end() && ci->second.serNodesVar) {
            CompilerContext::ScopeDrop sd;
            sd.addr = pallocaRaw;
            sd.kind = 7;
            sd.serNodes = ci->second.serNodesVar;
            sd.serNodesCount = ci->second.serNodesCount;
            sd.ownershipId = declOid;
            sd.debugName = decl->varName; sd.debugType = decl->typeName;
            ctx.cleanupStack.back().push_back(sd);
        }
        return;
    }

    // Reset Kv.get/get_copy tracking flags at statement entry — they leak from
    // earlier statements (e.g. a `set: [x, kv.get(k)]` that doesn't consume them).
    // If this decl's initial value evaluation contains a kv.get/get_copy call,
    // the flags will be set fresh by visitObjectMethodCall before we check them below.
    ctx.kvGetCalled = false;
    ctx.kvGetCopy = false;
    ctx.kvGetValueType.clear();

    // Check if this variable was pre-promoted to a GlobalVariable (non-library
    // top-level path). If so, skip alloca creation and only emit the initializer.
    bool isPrePromotedGlobal = false;
    IRValue* alloca = nullptr;
    llvm::Value* unwrappedAlloca = nullptr;

    if (ctx.symbol_table.count(decl->varName)) {
        auto& existing = ctx.symbol_table[decl->varName];
        if (existing.is_global && ctx.inTopLevelInit) {
            // Pre-promoted GlobalVariable — reuse storage, emit initializer in-order.
            isPrePromotedGlobal = true;
            alloca = existing.ir_alloca_inst;
            unwrappedAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca));
        } else {
            LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + "Variable shadowing is not allowed: '" + decl->varName
                      + "' is already declared in an outer scope");
            return;
        }
    }

    if (!isPrePromotedGlobal) {
        alloca = ctx.ir_gen->createAlloca(ty, decl->varName);
        // Zero-initialize the allocated memory
        unwrappedAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca));
    }

    llvm::Type* unwrappedTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ty));
    if (!isPrePromotedGlobal)
        ctx.builder.CreateStore(llvm::Constant::getNullValue(unwrappedTy), unwrappedAlloca);
    
    std::string tName = decl->typeName;
    // Pre-promoted globals: container init was already emitted in the lib_init
    // prologue (Phase 2 of libraryMode in ast_compiler.cpp::compile).  Skip
    // here to avoid double-init (which would leak the first data buffer).
    if (isPrePromotedGlobal) {
        // fall through — initializer eval still happens below
    } else if (tName.find("list<") == 0 && tName.back() == '>') {
        // Auto-alloc the empty data buffer ONLY when there is no initializer (later populated
        // by push) OR the initializer is a buffer-literal (expands to element writes onto this
        // list). A call/variable initializer produces a FULL list struct that is memcpy'd over
        // this alloca below (the list<> init path) — pre-allocating a buffer here would orphan
        // it (a 32-byte leak per call; the DeckService/active_match_ids producer-list leak).
        bool listLiteralInit = decl->initialValue &&
                               dynamic_cast<BufferLiteralExpr*>(decl->initialValue.get());
        if (!decl->initialValue || listLiteralInit) {
            std::string elemName = tName.substr(5, tName.length() - 6);
            // AIML v2 P3: a list<Model> stores %M* references (stride 8), so init with a pointer
            // element type. Otherwise the struct element type (v1 inline-value stride).
            llvm::Type* elemTy = ctx.isRefcountModel(elemName)
                ? llvm::PointerType::get(ctx.context, 0)
                : static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
            if (elemTy) ListImpl::initList(ctx, unwrappedAlloca, elemTy, 4);
        }
    } else if (tName.find("cascade<") == 0 && tName.back() == '>') {
        // cascade<X> shares the list<i32> layout; init the same way.
        llvm::Type* elemTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType("i32")));
        if (elemTy) ListImpl::initList(ctx, unwrappedAlloca, elemTy, 4);
    } else if (tName.find("map<") == 0 && tName.back() == '>') {
        std::string inner = tName.substr(4, tName.length() - 5);
        size_t colon = inner.find(':');
        if (colon != std::string::npos) {
            std::string kName = inner.substr(0, colon);
            std::string vName = inner.substr(colon + 1);
            llvm::Type* kTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(kName)));
            llvm::Type* vTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(vName)));
            if (kTy && vTy) MapImpl::initMap(ctx, unwrappedAlloca, kTy, vTy, 4);
        }
    } else if (tName.find("chan<") == 0 && tName.back() == '>') {
        // Auto-init with capacity=0 (true rendezvous). Use chan.init(N) for buffered.
        ChanImpl::init(ctx, unwrappedAlloca, ctx.builder.getInt64(0));
    } else if (tName.rfind("buffer<", 0) == 0 && tName.back() == '>') {
        // Auto-alloc when there is no initializer OR when the initializer is a
        // buffer literal [v1, v2, ...] (which expands to push calls, not a struct copy).
        bool isLiteral = decl->initialValue &&
                         dynamic_cast<BufferLiteralExpr*>(decl->initialValue.get());
        if (!decl->initialValue || isLiteral)
            BufferImpl::alloc(ctx, unwrappedAlloca, ctx.builder.getInt32(4096));
    } else if (tName == "ui_style" && !decl->initialValue) {
        // Initialize ui_style with current top-of-style-stack defaults
        auto* p    = llvm::PointerType::get(ctx.context, 0);
        auto* voidTy = ctx.builder.getVoidTy();
        ctx.builder.CreateCall(
            ctx.getFunction("aiml_ui_copy_current_style", voidTy, {p}),
            {unwrappedAlloca});
    } else if (!decl->initialValue && ctx.classes.count(tName) &&
               !ctx.classes[tName].isBuiltin && ctx.classes[tName].type) {
        // Local class-instance decl without initializer (no { new: ... }):
        // recursively initialise container-typed members so e.g. `.push` works
        // immediately on a freshly-decl'd list<T>/map<K:V> field.
        initClassContainerMembers(unwrappedAlloca, tName);
    }
    
    if (!isPrePromotedGlobal) {
        Symbol sym;
        sym.ir_alloca_inst = alloca;
        sym.ir_type = ctx.ir_gen->getPointerTy();
        sym.ir_element_type = ty;
        sym.typeName = decl->typeName;
        sym.is_const = decl->is_const;
        sym.is_volatile = decl->is_volatile;
        sym.lifetime = decl->lifetime;   // spec/61 ownership metadata (drop emission: later step)
        // spec/61 §4: the ownership-plan id for this declaration (gates its drop in visitBlockStmt).
        OwnershipId declOid = (ctx.ownershipPlan && ctx.ownershipPlan->declarations.count(decl))
            ? ctx.ownershipPlan->declarations.at(decl) : NoOwnership;
        sym.ownershipId = declOid;
        // spec/61 §6.3 (systemic value-semantics): a value-model local declared from a sharing VIEW
        // (`decl [x, Model, {at:..}]` / `{obj.field}` / a container accessor) is a VALUE — it must
        // become an INDEPENDENT deep copy, not a shallow alias of the container's inline list/map
        // heap. Otherwise, when x is later dropped, returned, or stored, its shared buffers are freed
        // twice with the container's own drop (the pervasive drops-on ZoneEntry/PlayerState double-
        // free class). We deep-copy at the DECL (see the value-model init store below) and mark it
        // owned (modelOwnedInit), so it is dropped exactly once at scope exit. Read-only views pay a
        // copy; escapes (return/store) then just move an already-independent owned value.
        // NOTE: full value-semantics deep-copy-at-decl was tried and reverted — 9rooms code relies
        // on view-in-place mutation (`myc = at(..); apply_buff(myc)` mutates the source), which a
        // deep copy breaks (enchantment_picker), plus a per-read perf cost. We keep the view SHARED
        // and only deep-copy at genuine ESCAPE points (return/set/idx-store), tainting via is_view_alias.
        // DEFERRED / to be REPLACED — doc/spec61_inline_value_model_release_plan_2026-09-24.md (§9.2/§9.4):
        // the target contract is CopyRetain at THIS decl (own inline copy; container fields retained =
        // incref → still shared, so add_buff visibility is preserved; scalar fields independent), which
        // then makes element release (remove/clear/pop) safe. is_view_alias is to be removed as an
        // ownership source (replaced by OwnershipPlan provenance). Red contract tests:
        // tests/_inline_model_red/ (50_share_semantics already green; 49 leaks until Paket 5).
        if (ctx.lifetimeDropsEnabled && decl->initialValue) {
            auto tciDecl = ctx.classes.find(decl->typeName);
            if (tciDecl != ctx.classes.end() && tciDecl->second.hasHeapFields && tciDecl->second.serNodesVar) {
                Expr* iv = decl->initialValue.get();
                if (dynamic_cast<AtExpr*>(iv)) {
                    sym.is_view_alias = true;
                } else if (auto* ve = dynamic_cast<VariableExpr*>(iv)) {
                    if (ve->name.find('.') != std::string::npos || ve->name.find('<') != std::string::npos)
                        sym.is_view_alias = true;
                } else if (auto* ce = dynamic_cast<CallExpr*>(iv)) {
                    const std::string& fn = ce->functionName;
                    size_t dp = fn.rfind('.');
                    std::string sn = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                    if ((sn == "at" || sn == "get" || sn == "first" || sn == "last")
                        && !ctx.ownsReturnMatches(sn, decl->typeName))
                        sym.is_view_alias = true;
                }
            }
        }
        ctx.symbol_table[decl->varName] = sym;

        // spec/61 (A2+A3): a FRESH value-model local with heap fields owns an independent heap
        // → register a scope-exit drop. Safe now that A3 move-detection removes the drop if the
        // local is later consumed (push/return/store). Excludes ptr<T>/ref<T> (box/borrow) and
        // `persistent`. Two owned-model INIT forms (kv.get_copy is registered separately below):
        //   • no initializer (populated later), and
        //   • a call to an `owns_return: true` function — return-ownership for VALUE models: the
        //     callee returns a freshly-built INDEPENDENT model (e.g. every RPC handler's
        //     `decl resp = svc.list(...)`); the caller owns it. Matched by the call's trailing
        //     identifier against ctx.ownsReturnFns (mirrors freshOwned). The address-based drop
        //     is valid even though the call stores into the alloca later — it fires at scope exit.
        bool modelOwnedInit = (decl->initialValue == nullptr);
        if (!modelOwnedInit && decl->initialValue) {
            if (auto* mc = dynamic_cast<CallExpr*>(decl->initialValue.get())) {
                const std::string& fn = mc->functionName;
                // spec/61 §5.1: `{ clone: X }` / `{ copy: X }` yield a FRESH, INDEPENDENT owned
                // value → the local owns it and must free it at scope exit (else the cloned heap —
                // strings, containers, cloned tensors, shared sub-objects — leaks; probes 15/34).
                if (fn == "sys.clone" || fn == "sys.copy") modelOwnedInit = true;
                // spec/61 §4 (round5 Finding B): `sys.deserialize(json, "T")` decodes a fresh owned
                // value model whose ser_decode heap must be released at scope exit (else 71 B/call
                // leak, probe class2_deser). Lockstep with freshOwned in ownership_analyzer.cpp.
                if (fn == "sys.deserialize") modelOwnedInit = true;
                size_t dp = fn.rfind('.');
                std::string shortName = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                if (ctx.ownsReturnMatches(shortName, decl->typeName)) modelOwnedInit = true;
            }
        }
        // spec/61 §5.4/§7 CopyRetain: a value-model materialised from a sharing VIEW (is_view_alias)
        // retains its shareable field handles below → it OWNS its own refs and must drop them at scope
        // exit (else release-old on a member-store frees a still-shared handle: mana_choice double-free,
        // endphase-crash). Lockstep with freshOwned (ownership_analyzer) + the retain below.
        if ((modelOwnedInit || sym.is_view_alias) && decl->lifetime != "persistent"
            && decl->typeName.rfind("ptr<", 0) != 0
            && decl->typeName.rfind("ref<", 0) != 0
            && !ctx.cleanupStack.empty()) {
            auto ci = ctx.classes.find(decl->typeName);
            if (ci != ctx.classes.end() && ci->second.hasHeapFields && ci->second.serNodesVar) {
                CompilerContext::ScopeDrop sd;
                sd.addr = unwrappedAlloca;
                sd.serNodes = ci->second.serNodesVar;
                sd.serNodesCount = ci->second.serNodesCount;
                sd.ownershipId = declOid;
                sd.debugName = decl->varName; sd.debugType = decl->typeName;
                ctx.cleanupStack.back().push_back(sd);
            }
        }

        // spec/61: OWNED box — `decl [box, ptr<T>, INIT]` where `box` owns the pointee → register
        // a kind-4 drop: deep-free the pointee (aiml_ser_free_decoded) then free the heap shell.
        // Removed by consumeLocalDrop when the box is MOVED (return/member-store/push/put — e.g.
        // get_base's own `return obj` keeps the transfer intact), and mayDrop-gated via the
        // ownership plan (freshOwned marks it Owned+Live) — the SAME safety the value-model drop
        // above relies on. Two INIT forms (MUST match freshOwned in ownership_analyzer.cpp):
        //   (1) `{ new: T }`               — locally calloc'd (visitNewExpr).
        //   (2) `{ call: fn(...) }` where fn is `owns_return: true` — the callee freshly allocated
        //       the box and transfers ownership to us (return-ownership; e.g. `cbase = get_base(...)`
        //       in compute_mkp). Matched by the call's trailing identifier against ctx.ownsReturnFns.
        // Restricted to a plain model/class with a ser-subtree; classgroup containers excluded.
        if (decl->initialValue && decl->lifetime != "persistent"
            && decl->typeName.rfind("ptr<", 0) == 0 && decl->typeName.back() == '>'
            && !ctx.cleanupStack.empty()) {
            bool ownedInit = dynamic_cast<NewExpr*>(decl->initialValue.get()) != nullptr;
            if (!ownedInit) {
                if (auto* c = dynamic_cast<CallExpr*>(decl->initialValue.get())) {
                    const std::string& fn = c->functionName;
                    size_t dp = fn.rfind('.');
                    std::string shortName = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                    ownedInit = ctx.ownsReturnMatches(shortName, decl->typeName);   // collision guard (handlers.get)
                }
            }
            if (ownedInit) {
                std::string boxed = decl->typeName.substr(4, decl->typeName.size() - 5);
                auto bi = ctx.classes.find(boxed);
                auto ii = ctx.interfaces.find(boxed);
                // spec/57/58: a cascade_class instance owns only its shell (the cascade rows are a
                // shared borrow) → release the shell alone at owner drop (kind 11, every path).
                if (ctx.cascadeClasses.count(boxed) && dynamic_cast<NewExpr*>(decl->initialValue.get())
                    && ctx.lifetimeDropsEnabled && !ctx.cleanupStack.empty()) {
                    CompilerContext::ScopeDrop sd;
                    sd.addr = unwrappedAlloca;
                    sd.kind = 11;
                    if (bi != ctx.classes.end() && bi->second.hasHeapFields && bi->second.serNodesVar) {
                        sd.serNodes = bi->second.serNodesVar;          // its own fields (blk share)
                        sd.serNodesCount = bi->second.serNodesCount;
                    }
                    sd.debugName = decl->varName; sd.debugType = decl->typeName;
                    ctx.cleanupStack.back().push_back(sd);
                }
                if (bi != ctx.classes.end() && bi->second.hasHeapFields && bi->second.serNodesVar
                    && bi->second.type && !ctx.classGroupContainers.count(boxed)
                    // spec/58: cascade_class instances borrow the shared, loaded-once cascade buffer
                    // (every `new` points at the same global rows) → a box-drop double-frees it.
                    // Must stay in lockstep with freshOwned in ownership_analyzer.cpp.
                    && !ctx.cascadeClasses.count(boxed)) {
                    // Concrete class box → kind-4 (static serNodes deep-free + shell free).
                    CompilerContext::ScopeDrop sd;
                    sd.addr = unwrappedAlloca;
                    sd.kind = 4;
                    sd.serNodes = bi->second.serNodesVar;
                    sd.serNodesCount = bi->second.serNodesCount;
                    sd.ownershipId = declOid;
                    sd.debugName = decl->varName; sd.debugType = decl->typeName;
                    ctx.cleanupStack.back().push_back(sd);
                } else if (ii != ctx.interfaces.end() && ii->second.fatPtrType) {
                    // Interface fat-pointer box (e.g. get_base → ptr<CardBase>) → kind-5:
                    // dispatch the concrete type's __free_<Class> via the vtable's trailing slot.
                    // (Only the owns_return CALL form reaches here — you cannot `{ new: Iface }`.)
                    CompilerContext::ScopeDrop sd;
                    sd.addr = unwrappedAlloca;
                    sd.kind = 5;
                    sd.fatPtrType = static_cast<llvm::Type*>(ii->second.fatPtrType);
                    sd.ifaceMethodCount = (int)ii->second.methods.size();
                    sd.ownershipId = declOid;
                    sd.debugName = decl->varName; sd.debugType = decl->typeName;
                    ctx.cleanupStack.back().push_back(sd);
                } else if (bi != ctx.classes.end() && bi->second.type
                           && !ctx.classGroupContainers.count(boxed)
                           && !ctx.cascadeClasses.count(boxed)) {
                    // spec/61 §6.2 (round8 §4): a POD box — a plain class with NO heap fields
                    // (serNodesVar == null) — still OWNS its outer heap allocation. Register a
                    // SHELL-ONLY kind-4 drop (serNodes null → emitScopeDrop skips the field
                    // deep-free, just frees the box). Without this the `new` allocation leaked
                    // (pod_box: 4 B/instance). Safe now that borrow-escape into an owning ptr
                    // member is a hard error (no aliased second owner). classgroup/cascade excluded.
                    CompilerContext::ScopeDrop sd;
                    sd.addr = unwrappedAlloca;
                    sd.kind = 4;
                    sd.serNodes = nullptr;
                    sd.serNodesCount = 0;
                    sd.ownershipId = declOid;
                    sd.debugName = decl->varName; sd.debugType = decl->typeName;
                    ctx.cleanupStack.back().push_back(sd);
                }
            }
        }

        // spec/61 §7: EVERY managed string local owns its value → register a null-safe kind-1
        // decref-drop (managedStringSlot). The slot invariant "null | one owned RC ref" is enforced
        // by the init-store normalisation below (dup a borrow / move an owned temp) and by
        // visitSetStmt (assign_copy/move) + committed-move nulling (consumeLocalDrop). A no-init slot
        // is null → decref(null) no-op. Excludes persistent/extern/pre-promoted-global (not managed).
        bool managedStringDecl = kManagedStringLocals && decl->typeName == "string"
            && decl->lifetime != "persistent" && !decl->is_extern && !isPrePromotedGlobal;
        if (managedStringDecl && !ctx.cleanupStack.empty()) {
            CompilerContext::ScopeDrop sd;
            sd.addr = unwrappedAlloca;
            sd.kind = 1;
            sd.ownershipId = declOid;
            sd.managedStringSlot = true;
            sd.debugName = decl->varName; sd.debugType = decl->typeName;
            ctx.cleanupStack.back().push_back(sd);
        } else if (!kManagedStringLocals && decl->typeName == "string" && decl->lifetime != "persistent"
                   && !ctx.cleanupStack.empty()) {
            // Managed model OFF → T1-era gate: only INTERPOLATION or producer/text_value-call string
            // inits own a fresh RC string that must be decref'd (a literal/borrow must NOT be dropped).
            bool ownedStringInit =
                decl->initialValue && dynamic_cast<StringInterpolationExpr*>(decl->initialValue.get());
            if (!ownedStringInit && decl->initialValue) {
                if (auto* sc = dynamic_cast<CallExpr*>(decl->initialValue.get())) {
                    const std::string& fn = sc->functionName;
                    size_t dp = fn.rfind('.');
                    std::string shortName = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                    if (shortName == "text_value" || shortName == "dropdown_value"
                        || ctx.ownsReturnMatches(shortName, decl->typeName))
                        ownedStringInit = true;
                }
            }
            if (ownedStringInit) {
                CompilerContext::ScopeDrop sd;
                sd.addr = unwrappedAlloca;
                sd.kind = 1;
                sd.ownershipId = declOid;
                sd.debugName = decl->varName; sd.debugType = decl->typeName;
                ctx.cleanupStack.back().push_back(sd);
            }
        }

        // spec/61 (A2/A3): a FRESH list<T> value-local owns its malloc'd data buffer → register
        // a list-drop (aiml_list_free frees the buffer; for list<string> it decref's each
        // element, which push MOVED in — see list_impl.cpp).  Two owned-init forms (MUST match
        // freshOwned in ownership_analyzer.cpp):
        //   • no initializer (empty list initList'd above, then populated by push), and
        //   • a call to an `owns_return: true` (producer) function — return-ownership for LISTS:
        //     the callee returns a freshly-built INDEPENDENT list (e.g. active_match_ids /
        //     take_gone_players → list<string>); the caller owns it. Matched by the call's
        //     trailing identifier against ctx.ownsReturnFns (mirrors the model/box cases).
        //     (assignment / at / get inits may alias a view and are left to leak = safe).
        // A3 move-detection removes this drop if the list is later pushed/returned/stored.
        bool listOwnedInit = (decl->initialValue == nullptr);
        if (!listOwnedInit && decl->initialValue) {
            if (auto* lc = dynamic_cast<CallExpr*>(decl->initialValue.get())) {
                const std::string& fn = lc->functionName;
                size_t dp = fn.rfind('.');
                std::string shortName = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                if (ctx.ownsReturnMatches(shortName, decl->typeName)) listOwnedInit = true;
            }
        }
        if (decl->typeName.rfind("list<", 0) == 0 && decl->typeName.back() == '>'
            && listOwnedInit && decl->lifetime != "persistent"
            && !decl->is_extern && !ctx.cleanupStack.empty()) {
            std::string elemName = decl->typeName.substr(5, decl->typeName.length() - 6);
            CompilerContext::ScopeDrop sd;
            sd.addr = unwrappedAlloca;
            sd.kind = 2;
            if (elemName == "string") {
                sd.elemKind = 1;                       // decref each element char*
            } else if (ctx.isRefcountModel(elemName)) {
                // AIML v2 P3: list<Model> stores %M* references → decref each element (elemKind 3).
                // Bare-class key (a memV3 shared<T> elem name is wrapped; v2 is already bare).
                std::string bareElem = elemName;
                if (bareElem.rfind("shared<", 0) == 0 && bareElem.back() == '>')
                    bareElem = bareElem.substr(7, bareElem.size() - 8);
                auto ci = ctx.classes.find(bareElem);
                sd.elemKind = 3;
                if (ci != ctx.classes.end()) { sd.serNodes = ci->second.serNodesVar; sd.serNodesCount = ci->second.serNodesCount; }
                sd.elemSize = 8;                       // pointer stride
            } else {
                // list<Model> whose element type carries heap fields → free each element's
                // heap (elemKind 2) using the class's serNodes + struct stride. Otherwise a
                // plain scalar/POD element list → just free the data buffer (elemKind 0).
                auto ci = ctx.classes.find(elemName);
                if (ci != ctx.classes.end() && ci->second.hasHeapFields
                    && ci->second.serNodesVar && ci->second.type) {
                    sd.elemKind = 2;
                    sd.serNodes = ci->second.serNodesVar;
                    sd.serNodesCount = ci->second.serNodesCount;
                    sd.elemSize = (int64_t)ctx.module.getDataLayout().getTypeAllocSize(ci->second.type);
                } else {
                    sd.elemKind = 0;                   // scalar/POD: free the buffer only
                }
            }
            sd.ownershipId = declOid;
            sd.debugName = decl->varName; sd.debugType = decl->typeName;
            ctx.cleanupStack.back().push_back(sd);
        }

        // spec/61 §8 (map-drop DEFERRED — MapImpl audit required): aiml_map_free + kind-3
        // emission exist, and map.put now moves temps (consumeStringTemp, so a map<string:*>
        // build is UAF-free = a safe leak). But REGISTERING the drop here still trips an
        // "Invalid read of size 1" inside aiml_map_free's occ walk once the map has GROWN
        // (initial cap 4 → 8 → 16 within a build loop) — the occ/keys/vals layout after
        // grow/rehash does not match the simple [0,capacity) occupancy assumption (§8.2).
        // No measured map leak → left OFF until the grow/rehash occ semantics are audited and
        // a valgrind map probe (empty / one / many / collisions / rehash) is green.

        // spec/61 §5.6 (memV3): a dyn_array<T> local is a Class-4 unique buffer that OWNS its single
        // malloc → register a scope-default free (kind 9 = free(ptr)). `.free` nulls the slot so this
        // drop is a null no-op (no double free). Owned when init is a `new_array` (the alloc site).
        if (ctx.memV3 && decl->typeName.rfind("dyn_array<", 0) == 0 && decl->typeName.back() == '>'
            && decl->initialValue && dynamic_cast<NewArrayExpr*>(decl->initialValue.get())
            && decl->lifetime != "persistent" && !decl->is_extern && !ctx.cleanupStack.empty()) {
            CompilerContext::ScopeDrop sd;
            sd.addr = unwrappedAlloca;
            sd.kind = 9;                 // raw heap pointer → free(load(slot))
            sd.ownershipId = declOid;
            sd.debugName = decl->varName; sd.debugType = decl->typeName;
            ctx.cleanupStack.back().push_back(sd);
        }
        // spec/61 class 4: an iterator<T> local owns its iterator → released at owner drop (kind 10);
        // `free` is an early release that nulls the slot, a call argument moves it into the callee.
        if (decl->typeName.rfind("iterator<", 0) == 0 && decl->initialValue
            && decl->lifetime != "persistent" && !decl->is_extern && !ctx.cleanupStack.empty()) {
            CompilerContext::ScopeDrop sd;
            sd.addr = unwrappedAlloca;
            sd.kind = 10;
            sd.debugName = decl->varName; sd.debugType = decl->typeName;
            ctx.cleanupStack.back().push_back(sd);
        }
    }

    LOG_DEBUG("Compiled AST Decl: " + decl->varName + " of " + decl->typeName);

    if (!decl->initialValue) {
        auto classIt = ctx.classes.find(decl->typeName);
        if (classIt != ctx.classes.end() && !classIt->second.isBuiltin && classIt->second.type) {
            std::vector<std::pair<std::string, CompilerContext::StructMember>> orderedMembers;
            orderedMembers.reserve(classIt->second.members.size());
            for (const auto& [memberName, member] : classIt->second.members)
                orderedMembers.push_back({memberName, member});
            std::sort(orderedMembers.begin(), orderedMembers.end(),
                      [](const auto& a, const auto& b) { return a.second.index < b.second.index; });

            for (const auto& [memberName, member] : orderedMembers) {
                if (!member.initValue) continue;
                // spec/61 §5 (memV3): a CONTAINER field (list/map/buffer) was already given a valid
                // empty HANDLE by initClassContainerMembers. A scalar default (e.g. the `""` placeholder
                // used because `[]` at field position 2 breaks the field parser) must NOT be applied —
                // storing a string pointer over the handle slot clobbers the handle (leak) and turns the
                // field into a bogus handle → the next set/access frees a string as a handle → SIGSEGV
                // (test_db_indexed_list_field). Leave the empty handle in place.
                {
                    const std::string& mtn = member.typeName;
                    bool isContainerField = (mtn.rfind("list<", 0) == 0 && mtn.back() == '>')
                        || (mtn.rfind("map<", 0) == 0 && mtn.back() == '>')
                        || mtn == "buffer" || (mtn.rfind("buffer<", 0) == 0 && mtn.back() == '>');
                    if (ctx.memV3 && isContainerField) continue;
                }
                IRValue* fieldInitV = visitExpression(member.initValue.get());
                if (!fieldInitV) {
                    LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + "Failed to evaluate default value for member '" +
                              memberName + "' of type '" + decl->typeName + "'");
                    continue;
                }
                llvm::Value* initVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(fieldInitV));
                if (!initVal) {
                    LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + "Null LLVM value for default member '" +
                              memberName + "' of type '" + decl->typeName + "'");
                    continue;
                }
                llvm::Value* fieldPtr = ctx.builder.CreateStructGEP(classIt->second.type,
                                                                    unwrappedAlloca,
                                                                    member.index,
                                                                    memberName + "_default_gep");
                // spec/61: for a no-init value model, initClassContainerMembers already default-init'd
                // this field (dup'ing a string default into the slot). This loop re-applies the same
                // default → for a STRING member it would overwrite the prior dup and LEAK it (probe 15
                // Graph.title). Release the old managed string before the overwrite (memV3; the slot
                // holds initClassContainerMembers' dup or null — decref is null-safe).
                if (ctx.memV3 && member.typeName == "string") {
                    llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
                    llvm::Value* oldv = ctx.builder.CreateLoad(i8p, fieldPtr, "str_default_old");
                    coerceOrStoreFieldDefault(ctx, fieldPtr, member, initVal);
                    llvm::Function* dc = ctx.getFunction("aiml_str_decref", ctx.builder.getVoidTy(), {i8p});
                    ctx.builder.CreateCall(dc, { oldv });
                    continue;
                }
                coerceOrStoreFieldDefault(ctx, fieldPtr, member, initVal);
            }
        }
    }
    
    if (decl->initialValue) {
        if (rejectVoidCallInit(ctx, decl->initialValue.get(),
                "cannot initialize '" + decl->varName + "' of type '" + decl->typeName + "'",
                decl->sourceNode)) {
            return;
        }
        // Map literal: decl: [name, map<K:V>, {"k1": v1, "k2": v2, ...}]
        // Map is already alloc'd above (initMap); emit one put per entry.
        if (tName.rfind("map<", 0) == 0 && tName.back() == '>') {
            if (auto* dictLit = dynamic_cast<DictLiteralExpr*>(decl->initialValue.get())) {
                if (!dictLit->entriesExpr.empty()) {
                    std::string inner = tName.substr(4, tName.length() - 5);
                    size_t colon = inner.find(':');
                    if (colon != std::string::npos) {
                        std::string kName = inner.substr(0, colon);
                        std::string vName = inner.substr(colon + 1);
                        llvm::Type* kTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(kName)));
                        llvm::Type* vTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(vName)));
                        if (kTy && vTy) {
                            auto* mapStructTy = MapImpl::getMapStructType(ctx, kTy, vTy);
                            for (auto& [keyStr, valExpr] : dictLit->entriesExpr) {
                                llvm::Value* kVal = nullptr;
                                if (kName == "string") {
                                    kVal = ctx.builder.CreateGlobalStringPtr(keyStr, "mlit_k");
                                } else if (kName == "i32") {
                                    kVal = ctx.builder.getInt32((int32_t)std::stoll(keyStr));
                                } else if (kName == "i64") {
                                    kVal = ctx.builder.getInt64(std::stoll(keyStr));
                                } else if (ctx.enums.count(kName) || ctx.enums.count(ctx.qualify(kName))) {
                                    /* Enum-typed key — resolve the string
                                     * (e.g. "flying") to its integer value
                                     * via the enum's value map. */
                                    const auto& enumVals = ctx.enums.count(kName)
                                        ? ctx.enums[kName]
                                        : ctx.enums[ctx.qualify(kName)];
                                    auto it = enumVals.find(keyStr);
                                    if (it == enumVals.end()) {
                                        LOG_ERROR(ctx.fmtLoc(decl->sourceNode)
                                            + "map literal: enum '" + kName
                                            + "' has no value named '" + keyStr + "'");
                                        continue;
                                    }
                                    kVal = ctx.builder.getInt32((int32_t)it->second);
                                } else {
                                    LOG_ERROR(ctx.fmtLoc(decl->sourceNode) + "map literal: key type '" + kName + "' not supported yet");
                                    continue;
                                }
                                IRValue* vv = visitExpression(valExpr.get());
                                if (!vv) continue;
                                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(vv));
                                if (vTy->isIntegerTy() && v->getType()->isIntegerTy() && v->getType() != vTy) {
                                    v = ctx.builder.CreateZExtOrTrunc(v, vTy, "mlit_v");
                                } else if (vTy->isFloatingPointTy() && v->getType()->isFloatingPointTy() && v->getType() != vTy) {
                                    v = ctx.builder.CreateFPCast(v, vTy, "mlit_v");
                                }
                                MapImpl::put(ctx, unwrappedAlloca, kVal, v, mapStructTy, vName == "string");
                            }
                            return; // done — skip normal expression init
                        }
                    }
                }
            }
        }
        // Buffer literal: decl: [name, buffer<T>, [v1, v2, ...]]
        // Buffer is already alloc'd above; emit one write per element.
        if (auto* bufLit = dynamic_cast<BufferLiteralExpr*>(decl->initialValue.get())) {
            std::string elemName = tName.substr(7, tName.size() - 8); // strip "buffer<" + ">"
            llvm::Type* elemTy = ctx.getLLVMType(elemName);
            for (auto& elem : bufLit->elements) {
                IRValue* val = visitExpression(elem.get());
                if (!val) continue;
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
                if (elemTy && v->getType() != elemTy) {
                    if (elemTy->isFloatingPointTy() && v->getType()->isFloatingPointTy())
                        v = ctx.builder.CreateFPCast(v, elemTy, "fp_cast");
                    else if (elemTy->isIntegerTy() && v->getType()->isIntegerTy())
                        v = ctx.builder.CreateZExtOrTrunc(v, elemTy, "int_cast");
                }
                BufferImpl::write(ctx, unwrappedAlloca, v, elemTy ? elemTy : v->getType());
            }
            return; // done — skip normal expression init
        }

        ctx.deserializeTargetType = decl->typeName;
        IRValue* initVal = visitExpression(decl->initialValue.get());
        ctx.deserializeTargetType.clear();
        // spec/61 §7: normalise a managed string local's init to "one owned RC ref". An owned temp
        // (interpolation/producer, in the stmt-temp list) is MOVED (stored as-is, then removed from
        // the temp list below). A BORROW (variable/member/at/non-producer call/literal) is DUPLICATED
        // so the slot owns an INDEPENDENT ref — dup reads only content, never a header before a
        // foreign address, and never aliases the source (whose own lifetime is unchanged).
        {
            bool managedStringDeclInit = kManagedStringLocals && decl->typeName == "string"
                && decl->lifetime != "persistent" && !decl->is_extern && !isPrePromotedGlobal;
            if (ctx.lifetimeDropsEnabled && managedStringDeclInit && initVal) {
                llvm::Value* iv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal));
                bool ownedTemp = iv && inStringTempStack(ctx, iv);
                if (iv && iv->getType()->isPointerTy() && !ownedTemp) {
                    llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
                    if (ctx.memV3) {
                        // spec/61 §4: SHARE the borrow (incref) — the slot owns a shared ref, freed
                        // by its scope-drop decref. Immutable → same content, no dup.
                        llvm::Function* inc = ctx.getFunction("aiml_str_incref",
                            ctx.builder.getVoidTy(), {i8p});
                        ctx.builder.CreateCall(inc, { iv });
                        // initVal stays `iv` (the shared buffer)
                    } else {
                        llvm::Function* dup = ctx.getFunction("aiml_str_dup", i8p, {i8p});
                        initVal = ctx.ir_gen->wrap(ctx.builder.CreateCall(dup, { iv }, "managed_dup"));
                    }
                }
            }
        }
        // spec/61: a string temp used as a decl initializer is owned by the new local
        // (its scope-drop handles it) → remove it from the statement-temp decref list.
        if (initVal) consumeStringTemp(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal)));
        if (initVal) {
            // path: auto-intern so pointer identity == path equality
            if (decl->typeName == "path") {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* internFn = ctx.getFunction("aiml_path_intern", i8p, {i8p});
                llvm::Value* raw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal));
                initVal = ctx.ir_gen->wrap(ctx.builder.CreateCall(internFn, {raw}, "path_init"));
            }
            // String → int coercion: decl: [x, i32/i64, "${str_var}"]
            // Null-safe: if pointer is null (missing KV key etc.) → returns 0 instead of crashing atoi(NULL)
            if ((decl->typeName == "i32" || decl->typeName == "i64") && !decl->typeName.empty()) {
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal));
                if (v->getType()->isPointerTy()) {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    bool is32 = (decl->typeName == "i32");
                    llvm::Type* intTy = is32 ? ctx.builder.getInt32Ty() : ctx.builder.getInt64Ty();
                    llvm::Value* zero = is32 ? ctx.builder.getInt32(0)
                                             : (llvm::Value*)ctx.builder.getInt64(0);
                    llvm::Value* isNull = ctx.builder.CreateIsNull(v, "str2i_null");
                    llvm::Function* parent = ctx.builder.GetInsertBlock()->getParent();
                    llvm::BasicBlock* nullBB  = llvm::BasicBlock::Create(ctx.context, "str2i_null_bb", parent);
                    llvm::BasicBlock* okBB    = llvm::BasicBlock::Create(ctx.context, "str2i_ok",      parent);
                    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "str2i_merge",   parent);
                    ctx.builder.CreateCondBr(isNull, nullBB, okBB);
                    ctx.builder.SetInsertPoint(nullBB);
                    ctx.builder.CreateBr(mergeBB);
                    ctx.builder.SetInsertPoint(okBB);
                    llvm::Function* fn = is32 ? ctx.getFunction("atoi",  ctx.builder.getInt32Ty(), {i8p})
                                              : ctx.getFunction("atoll", ctx.builder.getInt64Ty(), {i8p});
                    llvm::Value* callResult = ctx.builder.CreateCall(fn, {v}, is32 ? "str2i32_v" : "str2i64_v");
                    ctx.builder.CreateBr(mergeBB);
                    ctx.builder.SetInsertPoint(mergeBB);
                    llvm::PHINode* phi = ctx.builder.CreatePHI(intTy, 2, is32 ? "str2i32" : "str2i64");
                    phi->addIncoming(zero, nullBB);
                    phi->addIncoming(callResult, okBB);
                    initVal = ctx.ir_gen->wrap(phi);
                }
            }
            // buffer<u8>: handler returns a fresh alloca → memcpy struct content into target alloca
            if (decl->typeName.find("buffer<") == 0) {
                llvm::Type* bufTy = ctx.getLLVMType("buffer");
                uint64_t sz = ctx.module.getDataLayout().getTypeAllocSize(bufTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                llvm::Function* mcf = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
                ctx.builder.CreateCall(mcf, {
                    unwrappedAlloca,
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal)),
                    ctx.getSizeConst(sz)
                });
            } else if (decl->typeName.find("list<") == 0) {
                llvm::Value* rawInitVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal));
                // `null` literal on a container slot would memcpy 16 bytes from
                // a null source — segfault at startup.  Global storage already
                // starts zeroed; just skip the no-op.  Common pattern:
                //   - decl: [xs, list<T>, null, { extern: true }]
                if (llvm::isa<llvm::ConstantPointerNull>(rawInitVal)) {
                    // skip — global is zero-initialised by linker
                } else if (rawInitVal->getType()->isPointerTy()) {
                    // Builtin handler returns alloca ptr → memcpy struct contents
                    llvm::StructType* listStructTy = ListImpl::getListStructType(ctx);
                    uint64_t sz = ctx.module.getDataLayout().getTypeAllocSize(listStructTy);
                    llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                    llvm::Function* mcf = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
                    ctx.builder.CreateCall(mcf, {
                        unwrappedAlloca,
                        rawInitVal,
                        ctx.getSizeConst(sz)
                    });
                } else {
                    // User function returns list struct value → store directly
                    ctx.builder.CreateStore(rawInitVal, unwrappedAlloca);
                }
            } else {
                // For user-defined struct types (isBuiltin=false, real LLVM struct),
                // builtin handlers like next_event/next_rpc return an alloca pointer
                // to a temporary struct → memcpy into target alloca instead of store.
                // Only do this when: (1) target LLVM type is a struct, AND
                // (2) the initVal is a pointer type (i.e. an alloca ptr, not a struct value).
                llvm::Type* declTy = ctx.getLLVMType(decl->typeName);
                llvm::Value* initRaw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal));

                // Enforce kv.get / kv.get_copy ↔ decl-type pairing for class
                // value types with heap fields.  See doc/issue/kv_get_mutate_set_same_key_crash.md.
                //
                //   decl: [p, T,      kv.get_copy(k)]  → OK (deep-copy below)
                //   decl: [p, ptr<T>, kv.get(k)]       → OK (zero-copy view)
                //   decl: [p, T,      kv.get(k)]       → ERROR
                //   decl: [p, ptr<T>, kv.get_copy(k)]  → ERROR
                //
                // ptr<T> / primitive-only structs are excluded from the check
                // because their lifetime model is unaffected.
                if (ctx.kvGetCalled) {
                    bool isPtrTarget = (decl->typeName.rfind("ptr<", 0) == 0 ||
                                        decl->typeName.rfind("ref<", 0) == 0)
                                       && decl->typeName.back() == '>';
                    std::string innerName = isPtrTarget
                        ? decl->typeName.substr(4, decl->typeName.size() - 5)
                        : decl->typeName;
                    auto ciIt2 = ctx.classes.find(innerName);
                    bool isHeapClass = ciIt2 != ctx.classes.end() && ciIt2->second.hasHeapFields;
                    if (isHeapClass) {
                        if (ctx.kvGetCopy && isPtrTarget) {
                            LOG_ERROR(ctx.fmtLoc(decl->sourceNode) +
                                "kv.get_copy must be assigned to a value-type, not ptr<" + innerName + ">. "
                                "Deep-copy is wasted on a pointer view. "
                                "Use 'decl: [" + decl->varName + ", " + innerName + ", kv.get_copy(k)]' "
                                "or use 'kv.get' (without _copy) for a zero-copy view.");
                        } else if (!ctx.kvGetCopy && !isPtrTarget) {
                            LOG_ERROR(ctx.fmtLoc(decl->sourceNode) +
                                "kv.get of struct '" + innerName + "' must be assigned to ptr<" + innerName + "> (zero-copy view), "
                                "not to a value-type. A shallow memcpy into a value-type shares heap "
                                "pointers with the kv decoded_cache and crashes on mutate-and-set-same-key. "
                                "Use 'decl: [" + decl->varName + ", ptr<" + innerName + ">, kv.get(k)]' "
                                "for a view, or 'decl: [" + decl->varName + ", " + innerName + ", kv.get_copy(k)]' "
                                "for an independent deep-copy.");
                        }
                    }
                    ctx.kvGetCalled = false;
                    ctx.kvGetValueType.clear();
                }

                // Interface upcast: decl: [s, ptr<Iface>, c] where c is a class
                // instance / ref<Class>.  The target is a fat pointer struct
                // {i8* obj, i8* vtbl}.  We need the SOURCE class name to look up
                // the __vtable_<Class>_<Iface> global. spec/61 §5.4: a `ref<Iface>`
                // target is the borrow form of the same upcast (upcasting a borrowed
                // concrete instance to an interface view) — handle it the same way;
                // resolveType gives both the same AimlFatPtr_<Iface> layout.
                if ((decl->typeName.rfind("ptr<", 0) == 0 || decl->typeName.rfind("ref<", 0) == 0)
                    && decl->typeName.back() == '>') {
                    std::string ifaceName = decl->typeName.substr(4, decl->typeName.size() - 5);
                    if (ctx.interfaces.count(ifaceName)) {
                        // Resolve source class and raw object pointer.
                        std::string srcType;
                        llvm::Value* objPtr = initRaw;
                        if (auto* varExpr = dynamic_cast<VariableExpr*>(decl->initialValue.get())) {
                            if (Symbol* srcSym = resolveVariable(varExpr->name)) {
                                srcType = srcSym->typeName;
                                if (objPtr && !objPtr->getType()->isPointerTy()) {
                                    // By-value class instance: take its alloca address.
                                    objPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
                                        srcSym->ir_alloca_inst ? srcSym->ir_alloca_inst
                                                               : ctx.ir_gen->wrap(srcSym->alloca_inst)));
                                    srcType = "ptr<" + srcType + ">";  // promote to ptr-form
                                }
                            }
                        } else if (auto* ne = dynamic_cast<NewExpr*>(decl->initialValue.get())) {
                            // `decl: [s, ptr<Iface>, {new: Concrete}]` — upcast the fresh box directly.
                            if (ctx.classes.count(ne->typeName)) srcType = "ptr<" + ne->typeName + ">";
                        }
                        if (!srcType.empty() && objPtr) {
                            auto* fpTy = llvm::cast<llvm::StructType>(declTy);
                            llvm::Value* fat = buildInterfaceUpcast(objPtr, srcType, fpTy);
                            if (fat) {
                                ctx.builder.CreateStore(fat, unwrappedAlloca);
                                LOG_DEBUG("interface upcast: " + decl->varName + " as ptr<" + ifaceName + ">");
                                return;
                            }
                            LOG_ERROR(ctx.fmtLoc(decl->sourceNode) +
                                "class source '" + srcType + "' does not implement interface '" + ifaceName + "'");
                        }
                    }
                }

                if (declTy && declTy->isStructTy() && initRaw && initRaw->getType()->isPointerTy()) {
                    // `null` literal on a struct slot would memcpy `sizeof(T)`
                    // bytes from a null source — segfault at startup.  Global
                    // storage already starts zeroed; just skip the no-op.
                    // Common pattern: `- decl: [x, T, null, { extern: true }]`.
                    if (llvm::isa<llvm::ConstantPointerNull>(initRaw)) {
                        return;
                    }
                    // ctx.kvGetCopy is set when the initialValue was `kv.get(k, {copy: true})`.
                    // In that case, do a deep-copy (encode-then-decode roundtrip) instead of
                    // a flat memcpy so the destination's heap allocations (strings, list
                    // buffers, etc.) are INDEPENDENT of the kv decoded_cache.  Otherwise
                    // mutate-and-set-same-key crashes — see doc/issue/kv_get_mutate_set_same_key_crash.md.
                    auto ciIt = ctx.classes.find(decl->typeName);
                    bool needsDeepCopy = ctx.kvGetCopy
                                         && ciIt != ctx.classes.end()
                                         && ciIt->second.hasHeapFields
                                         && ciIt->second.serNodesVar;
                    llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                    if (needsDeepCopy) {
                        // Destination alloca is already zero-initialised (line 116).
                        // aiml_ser_deep_copy encodes src to a temp blob then decodes into dst,
                        // which malloc's fresh heap for every string/list/map/buffer field.
                        llvm::Function* fn = ctx.getFunction("aiml_ser_deep_copy",
                            ctx.builder.getVoidTy(),
                            {ptrTy, ptrTy, ptrTy, ctx.builder.getInt32Ty()});
                        ctx.builder.CreateCall(fn, {
                            unwrappedAlloca,
                            initRaw,
                            ciIt->second.serNodesVar,
                            ctx.builder.getInt32(ciIt->second.serNodesCount)
                        });
                        // spec/61 (A2): this local now owns an INDEPENDENT heap (deep-copied)
                        // → register it for a scope-exit drop, unless it is persistent
                        // (owned elsewhere) — persistent skips auto-drop.
                        if (decl->lifetime != "persistent" && !ctx.cleanupStack.empty()) {
                            CompilerContext::ScopeDrop sd;
                            sd.addr = unwrappedAlloca;
                            sd.serNodes = ciIt->second.serNodesVar;
                            sd.serNodesCount = ciIt->second.serNodesCount;
                            sd.ownershipId =
                                (ctx.ownershipPlan && ctx.ownershipPlan->declarations.count(decl))
                                ? ctx.ownershipPlan->declarations.at(decl) : NoOwnership;
                            sd.debugName = decl->varName; sd.debugType = decl->typeName;
                            ctx.cleanupStack.back().push_back(sd);
                        }
                    } else {
                        uint64_t sz = ctx.module.getDataLayout().getTypeAllocSize(declTy);
                        llvm::Function* mcf = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
                        ctx.builder.CreateCall(mcf, {
                            unwrappedAlloca,
                            initRaw,
                            ctx.getSizeConst(sz)
                        });
                        // sys.deserialize returns a fresh malloc'd carrier (__aiml_deser_T); the memcpy
                        // moved its bytes (inner heap pointers included) into the inline value-model slot,
                        // which now owns them and frees them at its scope-drop. Free the abandoned carrier
                        // shell here, else it leaks sizeof(T) per call (spec/61 §3/§6.1).
                        if (auto* dc = llvm::dyn_cast<llvm::CallInst>(initRaw))
                            if (auto* cf = dc->getCalledFunction())
                                if (cf->getName().startswith("__aiml_deser_"))
                                    ctx.builder.CreateCall(
                                        ctx.getFunction("free", ctx.builder.getVoidTy(), {ptrTy}), {initRaw});
                        // spec/61 §5.4/§7 CopyRetain (endphase-crash fix): value-model materialised from
                        // a sharing VIEW (is_view_alias) via a pointer/memcpy — retain its shareable field
                        // handles so this local holds its OWN refs; its scope-drop (registered above) then
                        // balances a later member-store's release-old (mana_choice double-free). Not a
                        // deserialize carrier (freed above; mutually exclusive with a view init).
                        Symbol* _vsym_mc = resolveVariable(decl->varName);
                        if (_vsym_mc && _vsym_mc->is_view_alias && ciIt != ctx.classes.end()
                            && ciIt->second.hasHeapFields && ciIt->second.serNodesVar) {
                            llvm::Function* rf = ctx.getFunction("aiml_ser_retain", ctx.builder.getVoidTy(),
                                {ptrTy, ptrTy, ctx.builder.getInt32Ty()});
                            ctx.builder.CreateCall(rf, { unwrappedAlloca, ciIt->second.serNodesVar,
                                ctx.builder.getInt32(ciIt->second.serNodesCount) });
                        }
                    }
                    ctx.kvGetCopy = false;  // reset; only applies to this decl
                } else {
                    // ptr<T>/ref<T> = struct_var: store the address of the struct, not the
                    // struct value itself (which would overflow the 8-byte pointer alloca).
                    llvm::Value* storeVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal));
                    bool isPtrTarget = (decl->typeName.rfind("ptr<", 0) == 0 ||
                                        decl->typeName.rfind("ref<", 0) == 0)
                                       && decl->typeName.back() == '>';
                    if (isPtrTarget && storeVal && storeVal->getType()->isStructTy()) {
                        // initVal is a loaded struct — we need the ADDRESS instead (a ptr/ref binds
                        // the lvalue). A plain variable → its alloca; an `at`/member view → the
                        // element/field address via emitLValueAddress (else the loaded struct value
                        // was written into the 8-byte ref slot → garbage pointer, crash on deref).
                        llvm::Value* addrVal = nullptr;
                        if (auto* varExpr = dynamic_cast<VariableExpr*>(decl->initialValue.get())) {
                            Symbol* srcSym = resolveVariable(varExpr->name);
                            if (srcSym) addrVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
                                srcSym->ir_alloca_inst ? srcSym->ir_alloca_inst
                                                       : ctx.ir_gen->wrap(srcSym->alloca_inst)));
                        }
                        if (!addrVal) {
                            std::string lvtn;
                            IRValue* av = emitLValueAddress(decl->initialValue.get(), lvtn);
                            if (av) addrVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(av));
                        }
                        if (addrVal) initVal = ctx.ir_gen->wrap(addrVal);
                    }
                    // Numeric coercion (int↔int, int→float, float→int)
                    llvm::Value* sv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(initVal));
                    if (sv) {
                        llvm::Type* targetTy = ctx.getLLVMType(decl->typeName);
                        if (targetTy && targetTy != sv->getType()) {
                            if (targetTy->isIntegerTy() && sv->getType()->isIntegerTy()) {
                                unsigned dstBits = targetTy->getIntegerBitWidth();
                                unsigned srcBits = sv->getType()->getIntegerBitWidth();
                                if (dstBits < srcBits)
                                    initVal = ctx.ir_gen->wrap(ctx.builder.CreateTrunc(sv, targetTy, "trunc_narrow"));
                                else if (srcBits == 1)
                                    initVal = ctx.ir_gen->wrap(ctx.builder.CreateZExt(sv, targetTy, "zext_bool"));
                                else
                                    initVal = ctx.ir_gen->wrap(ctx.builder.CreateSExt(sv, targetTy, "sext_widen"));
                            } else if (targetTy->isFloatingPointTy() && sv->getType()->isIntegerTy()) {
                                initVal = ctx.ir_gen->wrap(ctx.builder.CreateSIToFP(sv, targetTy, "int_to_fp"));
                            } else if (targetTy->isIntegerTy() && sv->getType()->isFloatingPointTy()) {
                                initVal = ctx.ir_gen->wrap(ctx.builder.CreateFPToSI(sv, targetTy, "fp_to_int"));
                            } else if (targetTy->isFloatingPointTy() && sv->getType()->isFloatingPointTy()) {
                                initVal = ctx.ir_gen->wrap(ctx.builder.CreateFPCast(sv, targetTy, "fp_cast"));
                            }
                        }
                    }
                    ctx.ir_gen->createStore(initVal, alloca);
                    // spec/61 §5.4/§7 CopyRetain (endphase-crash fix): value-model materialised from a
                    // sharing VIEW (is_view_alias) via a LOADED struct value (at on an inline value-model
                    // list element returns the struct, not a pointer — the memcpy branch is skipped).
                    // Retain its shareable field handles so this local holds its OWN refs; its scope-drop
                    // (registered above) balances a later member-store's release-old (mana_choice
                    // double-free). Lockstep with the memcpy-path retain + freshOwned.
                    Symbol* _vsym_st = resolveVariable(decl->varName);
                    if (_vsym_st && _vsym_st->is_view_alias) {
                        auto vmi = ctx.classes.find(decl->typeName);
                        if (vmi != ctx.classes.end() && vmi->second.hasHeapFields && vmi->second.serNodesVar) {
                            llvm::Type* ptrTy2 = llvm::PointerType::get(ctx.context, 0);
                            llvm::Value* aRaw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca));
                            llvm::Function* rf = ctx.getFunction("aiml_ser_retain", ctx.builder.getVoidTy(),
                                {ptrTy2, ptrTy2, ctx.builder.getInt32Ty()});
                            ctx.builder.CreateCall(rf, { aRaw, vmi->second.serNodesVar,
                                ctx.builder.getInt32(vmi->second.serNodesCount) });
                        }
                    }
                }
            }
        }
    }
    // AIML_TRACE_SYMBOL: log the symbol's value at declaration (birth of its lifetime).
    if (aimlTraceSymbol(decl->varName)) {
        auto sit = ctx.symbol_table.find(decl->varName);
        int kind = aimlTraceKind(ctx, decl->typeName);
        if (kind >= 0 && sit != ctx.symbol_table.end() && sit->second.ir_alloca_inst) {
            llvm::Value* a = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sit->second.ir_alloca_inst));
            ctx.traceDeclarationSites[a] = ctx.fmtLoc();
            emitSymbolValueTrace(ctx, "DECL", decl->varName, kind, decl->typeName, a);
        }
    }
}
// Conservative assignment-compatibility guard.  Runs at the scalar
// createStore fallback — AFTER all legitimate coercions (int↔int, int↔float,
// string→int, enum←int, path-intern) and AFTER the container/model memcpy/
// deep-copy branches.  Catches silent garbage: a pointer/struct stored into a
// numeric/enum slot, or a number stored into a string.  ptr<>/ref<>/any/path/
// container targets are handled by other branches and skipped here.  Returns a
// human message on mismatch, "" when compatible.
static std::string scalarStoreMismatch(CompilerContext& ctx,
                                       const std::string& typeName,
                                       llvm::Value* sv) {
    if (!sv || typeName.empty() || typeName == "any" || typeName == "path") return "";
    if (typeName.rfind("ptr<", 0) == 0 || typeName.rfind("ref<", 0) == 0) return "";
    if (typeName.rfind("list<", 0) == 0 || typeName.rfind("map<", 0) == 0 ||
        typeName.rfind("buffer<", 0) == 0) return "";
    llvm::Type* tgt = ctx.getLLVMType(typeName);
    if (!tgt) return "";
    llvm::Type* vt = sv->getType();
    bool valPtr = vt->isPointerTy();
    bool valAgg = vt->isStructTy() || vt->isArrayTy();
    bool valNum = vt->isIntegerTy() || vt->isFloatingPointTy();
    if ((tgt->isIntegerTy() || tgt->isFloatingPointTy()) && (valPtr || valAgg))
        return "target is numeric/enum (" + typeName + ") but the value is a pointer/string/struct";
    if (typeName == "string" && (valNum || valAgg))
        return "target is string but the value is numeric/struct";
    return "";
}

// True if `derived` is `base` or transitively inherits from it (parentName chain).
static bool modelIsAncestor(CompilerContext& ctx, std::string derived, const std::string& base) {
    int guard = 0;
    while (!derived.empty() && guard++ < 64) {
        if (derived == base) return true;
        auto it = ctx.classes.find(derived);
        if (it == ctx.classes.end()) break;
        derived = it->second.parentName;
    }
    return false;
}
// Two models are assignment-related if one inherits from the other (either
// direction) — a derived value into a base slot copies the shared prefix.
static bool modelsInheritanceRelated(CompilerContext& ctx, const std::string& a, const std::string& b) {
    return modelIsAncestor(ctx, a, b) || modelIsAncestor(ctx, b, a);
}

void Compiler::visitSetStmt(SetStmt* setStmt) {
    // spec/61 §5.5/§7: the ownership plan decides whether a local rebind releases the old value.
    const char* rebindCopyFn = "aiml_ser_deep_copy";
    if (ctx.ownershipPlan) {
        auto pit = ctx.ownershipPlan->statements.find(setStmt);
        if (pit != ctx.ownershipPlan->statements.end() && pit->second.rebindReleaseOld)
            rebindCopyFn = "aiml_ser_deep_copy_replace";
    }
    // spec/61 §5.5: a GLOBAL heap value model always owns its value (not tracked by the local
    // ownership plan) → a rebind releases the old heap too.
    if (setStmt->target.find('.') == std::string::npos && setStmt->target.find('<') == std::string::npos) {
        if (Symbol* gs = resolveVariable(setStmt->target))
            if (gs->is_global && gs->lifetime != "persistent" && ctx.isOwnedValueReturn(gs->typeName))
                rebindCopyFn = "aiml_ser_deep_copy_replace";
    }
    // Reset any leaked kv-dispatch flags from prior statements before evaluating
    // the rhs. visitCall on a `kv.get_copy(...)` inside the rhs will re-set
    // ctx.kvGetCopy = true, which the struct-assignment branch below honors
    // to emit aiml_ser_deep_copy instead of a shallow memcpy
    // (see doc/issue/kv_get_mutate_set_same_key_crash.md — same logic as
    // visitDeclStmt). Without this, `set: [exp, { call: [kv.get_copy, key] }]`
    // would leave exp and the kv decoded_cache sharing heap pointers, and the
    // first push+set-same-key after that crashes.
    ctx.kvGetCalled = false;
    ctx.kvGetCopy = false;
    ctx.kvGetValueType.clear();

    // spec/61 §5.5/§6.1: rebinding an OWNED ptr<T> box local to a fresh box (`{new: T}` or an owning
    // call) secures the new box, then releases the old one (fields + shell), then stores.
    if (ctx.memV3 && ctx.lifetimeDropsEnabled && setStmt->target.find('.') == std::string::npos
        && setStmt->target.find('<') == std::string::npos) {
        Symbol* bs = resolveVariable(setStmt->target);
        llvm::Value* bslot = (bs && bs->ir_alloca_inst) ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(bs->ir_alloca_inst)) : nullptr;
        const CompilerContext::ScopeDrop* boxDrop = nullptr;
        if (bslot && bs->typeName.rfind("ptr<", 0) == 0)
            for (auto& fr : ctx.cleanupStack) for (auto& d : fr) if (d.addr == bslot && d.kind == 4) boxDrop = &d;
        bool freshBox = dynamic_cast<NewExpr*>(setStmt->value.get()) != nullptr;
        if (auto* fc = dynamic_cast<CallExpr*>(setStmt->value.get())) {
            size_t dp = fc->functionName.rfind('.');
            std::string sn = (dp == std::string::npos) ? fc->functionName : fc->functionName.substr(dp + 1);
            freshBox = ctx.ownsReturnMatches(sn, bs ? bs->typeName : "");
        }
        if (boxDrop && freshBox) {
            llvm::Value* bnodes = boxDrop->serNodes; int bnc = boxDrop->serNodesCount;
            IRValue* nv = visitExpression(setStmt->value.get());
            llvm::Value* newP = nv ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(nv)) : nullptr;
            if (newP && newP->getType()->isPointerTy()) {
                llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
                llvm::Value* oldP = ctx.builder.CreateLoad(i8p, bslot, "box_old");
                llvm::Function* fn = ctx.builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* relBB = llvm::BasicBlock::Create(ctx.context, "box_rel_old", fn);
                llvm::BasicBlock* doneBB = llvm::BasicBlock::Create(ctx.context, "box_rel_done", fn);
                llvm::Value* doRel = ctx.builder.CreateAnd(ctx.builder.CreateIsNotNull(oldP),
                                                           ctx.builder.CreateICmpNE(oldP, newP));
                ctx.builder.CreateCondBr(doRel, relBB, doneBB);
                ctx.builder.SetInsertPoint(relBB);
                if (bnodes) ctx.builder.CreateCall(ctx.getFunction("aiml_ser_free_decoded", ctx.builder.getVoidTy(),
                                {i8p, i8p, ctx.builder.getInt32Ty()}), { oldP, bnodes, ctx.builder.getInt32(bnc) });
                ctx.builder.CreateCall(ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p}), { oldP });
                ctx.builder.CreateBr(doneBB);
                ctx.builder.SetInsertPoint(doneBB);
                ctx.builder.CreateStore(newP, bslot);
                return;
            }
        }
    }

    // spec/61 (A3): move-detection (consumeLocalDrop) is applied LATER — only after the
    // target and RHS are classified, so a local→local copy does not wrongly drop the source
    // (see §4.2/§4.4 below).  It must not run unconditionally here.

    std::string targetStr = setStmt->target;
    if (targetStr.find("<.") == 0) {
        targetStr = "this." + targetStr.substr(2);
    }

    // Support dot-notation (obj.prop) and dash-notation (arr-index)
    size_t dotPos = targetStr.find('.');
    size_t dashPos = targetStr.find('-');
    size_t sepPos = (dotPos != std::string::npos) ? dotPos : dashPos;
    bool isDash = (dashPos != std::string::npos && dotPos == std::string::npos);
    // spec/01: the deprecated hyphen index write (`set: [x-0, v]`) is removed. Direct to idx:.
    if (isDash) {
        LOG_ERROR(ctx.fmtLoc() + "deprecated hyphen index write '" + targetStr +
            "' is removed — use idx: [" + targetStr.substr(0, dashPos) +
            ", <index>, <value>] instead (spec/01).");
        return;
    }

    std::string baseName = (sepPos != std::string::npos) ? targetStr.substr(0, sepPos) : targetStr;
    std::string propName = (sepPos != std::string::npos) ? targetStr.substr(sepPos + 1) : "";

    // spec/61 §7.1 borrow-escape (ERROR — Migration A): storing a borrowed ptr/ref PARAMETER into
    // one of THIS object's OWNING members lets the pointee (owned by the caller) be reached after the
    // call returns, when it may already be gone — spec/61 §5.1/§6.2: "a borrow must not … store it".
    // Under the scope-default `new`-box auto-release this is a genuine double-free/UAF hazard, so it
    // is rejected. Fixes: declare the member ref<T> (weak back-edge, §6.2), copy the pointee, or take
    // ownership with a { lifetime: consumed } parameter. A `consumed` param is a MOVE-IN (the caller
    // gives up ownership), NOT a borrow → exempt it.
    if (!isDash && baseName == "this" && !propName.empty() && setStmt->value) {
        if (auto* ve = dynamic_cast<VariableExpr*>(setStmt->value.get())) {
            auto vit = ctx.symbol_table.find(ve->name);
            if (vit != ctx.symbol_table.end() && vit->second.is_param
                && vit->second.lifetime != "consumed"
                && (vit->second.typeName.rfind("ptr<", 0) == 0
                    || vit->second.typeName.rfind("ref<", 0) == 0)) {
                // spec/61 §5.2/§6.2: a `ref<T>` MEMBER is a declared WEAK back-reference — it does not
                // own, does not incref, and is not freed through, so storing a borrow into it is the
                // sanctioned back-edge pattern (`Card { owner: ref<Zone> }`), NOT an escape. Only an
                // OWNING target member (bare model / ptr<T> / shared<T>) can dangle a borrowed pointee.
                // Look up the direct member's declared type; skip the warning for a ref<T> member.
                bool targetIsWeakRef = false;
                if (propName.find('.') == std::string::npos) {
                    auto cit = ctx.classes.find(ctx.currentClassName);
                    if (cit != ctx.classes.end()) {
                        auto mit = cit->second.members.find(propName);
                        if (mit != cit->second.members.end()
                            && mit->second.typeName.rfind("ref<", 0) == 0)
                            targetIsWeakRef = true;
                    }
                }
                if (!targetIsWeakRef) {
                    LOG_ERROR(ctx.fmtLoc() + "borrow-escape: storing borrowed parameter '" + ve->name +
                        "' (" + vit->second.typeName + ") into OWNING member '<." + propName + "'. Its "
                        "pointee is owned by the caller and may not outlive this object (spec/61 §6.2). "
                        "Declare the member ref<T> (weak back-edge), copy it, or take ownership with a "
                        "{ lifetime: consumed } parameter.");
                    return;
                }
            }
        }
    }

    Symbol* sym = resolveVariable(baseName);
    if (!sym) {
        LOG_ERROR(ctx.fmtLoc() + "Undefined variable (set target): " + targetStr);
        return;
    }

    // spec/49 §21: `set [X, F(..., X, ...)]` — a heap value model passed by value and reassigned
    // from the call's result is a hard error (use ref<T> + in-place mutation).
    if (propName.empty() && !isDash) {
        if (auto* rc = dynamic_cast<CallExpr*>(setStmt->value.get())) {
            auto tci = ctx.classes.find(sym->typeName);
            bool heapValueModel = tci != ctx.classes.end() && tci->second.hasHeapFields
                && tci->second.serNodesVar && !tci->second.isBuiltin && !ctx.isRefcountModel(sym->typeName);
            if (heapValueModel) {
                for (const auto& a : rc->args) {
                    auto* av = dynamic_cast<VariableExpr*>(a.get());
                    if (av && av->name == baseName) {
                        LOG_ERROR(ctx.fmtLoc(setStmt->sourceNode) + "'" + baseName + "' (" + sym->typeName +
                            ") is passed by value to '" + rc->functionName + "' and reassigned from its result"
                            " — pass ref<" + sym->typeName + "> and mutate in place (spec/49 §21)");
                        return;
                    }
                }
            }
        }
    }

    // spec/61 §5.4/§7 (memV3): a VALUE-COPY into a whole MOVE-ONLY value Model duplicates a unique
    // owner (ptr<T>/tensor) → double-free/dangling. The concrete crash was `set: [<.inner, an_inner]`
    // copying a tensor-bearing model per-field (dropped the tensor / would double-free). Reject the
    // value-copy form (plain value-model variable → bare value-model target). Legal alternatives: a
    // ref<Model> target (borrow/alias) or `{ clone: … }` (independent copy). Precise + low-risk: only
    // a bare value-model source variable into a bare value-model move-only target is caught here.
    if (ctx.memV3 && setStmt->value) {
        std::string tgtModelTy;
        if (propName.empty()) tgtModelTy = sym->typeName;
        else if (propName.find('.') == std::string::npos) {
            std::string bcls = sym->typeName;
            if (bcls.rfind("ptr<", 0) == 0 || bcls.rfind("ref<", 0) == 0 || bcls.rfind("shared<", 0) == 0) {
                size_t lt = bcls.find('<');
                if (lt != std::string::npos && bcls.back() == '>') bcls = bcls.substr(lt + 1, bcls.size() - lt - 2);
            }
            auto bci = ctx.classes.find(bcls);
            if (bci != ctx.classes.end()) {
                auto mit = bci->second.members.find(propName);
                if (mit != bci->second.members.end()) tgtModelTy = mit->second.typeName;
            }
        }
        if (!tgtModelTy.empty()
            && tgtModelTy.rfind("ptr<", 0) != 0 && tgtModelTy.rfind("ref<", 0) != 0
            && tgtModelTy.rfind("shared<", 0) != 0
            && ctx.classes.count(tgtModelTy) && !ctx.classes[tgtModelTy].isBuiltin
            && modelIsMoveOnly(ctx, tgtModelTy)) {
            if (auto* ve = dynamic_cast<VariableExpr*>(setStmt->value.get())) {
                if (ve->name.find('.') == std::string::npos) {
                    Symbol* ssym = resolveVariable(ve->name);
                    if (ssym) {
                        const std::string& st = ssym->typeName;
                        bool srcIsValueModel = st.rfind("ptr<", 0) != 0 && st.rfind("ref<", 0) != 0
                            && st.rfind("shared<", 0) != 0 && ctx.classes.count(st);
                        if (srcIsValueModel) {
                            LOG_ERROR(ctx.fmtLoc() + "set: value-copies MOVE-ONLY model '" + tgtModelTy +
                                "' from '" + ve->name + "' (it owns a ptr<T>/tensor, §7) — duplicating a "
                                "unique owner (double-free/dangling). Declare the target 'ref<" + tgtModelTy +
                                ">' to alias/borrow, or assign '{ clone: " + ve->name + " }' for an "
                                "independent copy (§5.4/§7).");
                            return;
                        }
                    }
                }
            }
        }
    }

    IRValue* baseAlloca = sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst);
    IRType* baseElemTy = sym->ir_element_type ? sym->ir_element_type : ctx.ir_gen->wrapType(sym->element_type);
    
    if (!baseAlloca) return;

    // ── spec/61 §4.2/§4.4/§4.6: ownership-correct assignment for a PLAIN-LOCAL target ──
    // The move-detection (consumeLocalDrop) that used to run unconditionally at the top is
    // decided here, after target + RHS are known:
    //   • self-assignment (set:[x,x]) is a no-op — never lose the single owner's drop, never
    //     free-then-read the same heap (§4.6);
    //   • a local→local copy of a heap-bearing model is a DEEP copy so source and target stay
    //     independently owned (both keep their scope-drop) — a shallow struct copy plus
    //     consuming the source would dangle the source when the target is dropped (§4.2);
    //   • every other case falls through to consumeLocalDrop (the move into a member/container/
    //     return sink, unchanged).
    if (propName.empty() && !isDash) {
        if (auto* sv = dynamic_cast<VariableExpr*>(setStmt->value.get())) {
            if (sv->name == baseName) return;   // §4.6 self-assignment: no-op
            if (ctx.lifetimeDropsEnabled     // AIML_LIFETIME_DROPS=0 → full pre-feature revert
                && sv->name.find('.') == std::string::npos && sv->name.find('<') == std::string::npos
                && !sym->is_global) {
                auto tci = ctx.classes.find(sym->typeName);
                Symbol* ssym = resolveVariable(sv->name);
                // AIML v2: a refcount-model local holds a %M* (pointer), NOT an inline struct — this
                // local→local DEEP-COPY (which passes both ALLOCAS to aiml_ser_deep_copy) would read
                // the %M* slot as a struct base → crash (lifetime lab 08). Skip it so the v2 rebind
                // branch below (incref-new / decref-old / store the %M*) handles it by reference.
                if (tci != ctx.classes.end() && tci->second.hasHeapFields && tci->second.serNodesVar
                    && !ctx.isRefcountModel(sym->typeName)
                    && ssym && ssym->typeName == sym->typeName && !ssym->is_global
                    && ssym->ir_alloca_inst) {
                    // Finding #7: this early deep-copy path returns before the generic const
                    // check below, so enforce it here — a const target must not be written.
                    if (sym->is_const) {
                        LOG_ERROR(ctx.fmtLoc() + "Cannot assign to const variable " + targetStr);
                        return;
                    }
                    // §4.4.5: an owned target releases its old heap (rebindCopyFn = _replace).
                    llvm::Value* dst = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
                    llvm::Value* src = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(ssym->ir_alloca_inst));
                    llvm::Type* p = llvm::PointerType::get(ctx.context, 0);
                    llvm::Function* dc = ctx.getFunction(rebindCopyFn,
                        ctx.builder.getVoidTy(), {p, p, p, ctx.builder.getInt32Ty()});
                    ctx.builder.CreateCall(dc, { dst, src, tci->second.serNodesVar,
                                                 ctx.builder.getInt32(tci->second.serNodesCount) });
                    return;   // source keeps its drop — do NOT consumeLocalDrop
                }
            }
        }
        // spec/61 §4.2/§5.4: `set: [out, {at:[list<Model>, i]}]` — a value-model VIEW assigned to an
        // owned value-model local must DEEP-COPY. `at` yields a shallow struct whose container/string
        // field pointers ALIAS the container element's buffers; a plain struct store makes `out` share
        // them, and when `out` is later dropped or returned (producer) it frees buffers the container
        // still owns → multi-free (load_entry_at's `set out=at(lez,pos)` then `return out`, freed once
        // per damage_zone loop iteration → test_creature_damage). The VariableExpr case above covers
        // `set:[out, othervar]`; this covers a view-shaped RHS. Spill the loaded view struct and
        // deep-copy through it, so `out` gets an INDEPENDENT heap; the container keeps its ownership.
        if (ctx.lifetimeDropsEnabled && !sym->is_global
            && dynamic_cast<AtExpr*>(setStmt->value.get())) {
            auto tci = ctx.classes.find(sym->typeName);
            if (tci != ctx.classes.end() && tci->second.hasHeapFields && tci->second.serNodesVar
                && tci->second.type && !ctx.isRefcountModel(sym->typeName)) {
                if (sym->is_const) {
                    LOG_ERROR(ctx.fmtLoc() + "Cannot assign to const variable " + targetStr);
                    return;
                }
                IRValue* rv = visitExpression(setStmt->value.get());
                llvm::Value* rval = rv ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rv)) : nullptr;
                if (rval) {
                    llvm::Type* p = llvm::PointerType::get(ctx.context, 0);
                    llvm::Value* srcAddr;
                    if (rval->getType()->isPointerTy()) {
                        srcAddr = rval;                       // `at` already gave the element pointer
                    } else {
                        llvm::Type* mt = tci->second.type;
                        llvm::AllocaInst* tmp = ctx.createEntryAlloca(mt, "setview_src");
                        ctx.builder.CreateStore(rval, tmp); // spill the shallow view struct
                        srcAddr = tmp;
                    }
                    llvm::Value* dst = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
                    llvm::Function* dc = ctx.getFunction(rebindCopyFn,
                        ctx.builder.getVoidTy(), {p, p, p, ctx.builder.getInt32Ty()});
                    ctx.builder.CreateCall(dc, { dst, srcAddr, tci->second.serNodesVar,
                                                 ctx.builder.getInt32(tci->second.serNodesCount) });
                    return;   // source (container) keeps its ownership — do NOT consumeLocalDrop
                }
            }
        }
    }
    // spec/61 §7.0 (Increment C): does the ownership plan classify this MEMBER store as a
    // DeepCopy (source is a param/view/rebound local, not an owned local)? If so the member
    // must get an INDEPENDENT heap and the source is NOT consumed (it keeps its ownership).
    bool memberDeepCopy = false;
    if (ctx.ownershipPlan && ctx.lifetimeDropsEnabled) {   // AIML_LIFETIME_DROPS=0 → full pre-feature revert
        auto sp = ctx.ownershipPlan->statements.find(setStmt);
        if (sp != ctx.ownershipPlan->statements.end()
            && sp->second.transfer == AIML::AST::Transfer::DeepCopy)
            memberDeepCopy = true;
    }
    // Move (default): a plain owned local moved into a longer-lived sink → drop its cleanup.
    // Skipped for a planned DeepCopy (the source is copied, so it retains its own drop).
    // spec/61 §7: a MANAGED string local SOURCE in a `set` is COPIED into the target (plain-local
    // target → assign_copy dup; member target → incref below), never moved via this path — and this
    // consume runs BEFORE the value is loaded (nulling here would feed null to the store). So skip it
    // for a managed string source; the source keeps its owned ref + null-safe drop.
    bool srcIsManagedString = false;
    Symbol* memberHandleSource = nullptr;
    if (auto* sv = dynamic_cast<VariableExpr*>(setStmt->value.get())) {
        if (sv->name.find('.') == std::string::npos && sv->name.find('<') == std::string::npos) {
            auto sit = ctx.symbol_table.find(sv->name);
            if (sit != ctx.symbol_table.end() && isManagedStringLocalSym(sit->second))
                srcIsManagedString = true;
            if (ctx.memV3 && !propName.empty() && sit != ctx.symbol_table.end()
                && ctx.isMemV3ContainerHandle(sit->second.typeName))
                memberHandleSource = &sit->second;
        }
    }
    // spec/61 §8/§5.4 (F3): re-binding a `ref<T>` borrow (`set: [view, owner]`) must NOT transfer
    // ownership — the source stays a valid owner and keeps its drop. So the plain-local ref target
    // does not consume the source. (The store below aliases the owner's pointer into the ref slot.)
    bool targetIsRefBorrow = ctx.memV3 && propName.empty() && !isDash
        && sym->typeName.rfind("ref<", 0) == 0;
    // spec/61 §6.2: the same for a `ref<T>` MEMBER field (`set: [card.owner, {ref: zone}]`, lowered to
    // bare `zone`): a weak back-edge is a borrow, so the source owner keeps its drop (probe 08). The
    // field is a raw pointer store below; do NOT consume the source.
    if (ctx.memV3 && !targetIsRefBorrow && !propName.empty() && !isDash
        && propName.find('.') == std::string::npos) {
        std::string bcls = sym->typeName;
        if (bcls.rfind("ptr<", 0) == 0 || bcls.rfind("shared<", 0) == 0 || bcls.rfind("ref<", 0) == 0) {
            size_t lt = bcls.find('<');
            if (lt != std::string::npos && bcls.back() == '>') bcls = bcls.substr(lt + 1, bcls.size() - lt - 2);
        }
        auto bci = ctx.classes.find(bcls);
        if (bci != ctx.classes.end()) {
            auto mit = bci->second.members.find(propName);
            if (mit != bci->second.members.end() && mit->second.typeName.rfind("ref<", 0) == 0)
                targetIsRefBorrow = true;
        }
    }
    // spec/61 §6.2/§5.5: a shared<T> (refcount) target SHARES a variable source (incref below) — the
    // source keeps its own reference and drop, so it must not be consumed as a move.
    bool targetIsRcShare = false;
    if (dynamic_cast<VariableExpr*>(setStmt->value.get())) {
        if (propName.empty()) targetIsRcShare = ctx.isRefcountModel(sym->typeName);
        else if (propName.find('.') == std::string::npos) {
            std::string bcls = sym->typeName;
            if (bcls.rfind("ptr<", 0) == 0 || bcls.rfind("shared<", 0) == 0 || bcls.rfind("ref<", 0) == 0) {
                size_t lt = bcls.find('<');
                if (lt != std::string::npos && bcls.back() == '>') bcls = bcls.substr(lt + 1, bcls.size() - lt - 2);
            }
            auto bci = ctx.classes.find(bcls);
            if (bci != ctx.classes.end()) {
                auto mit = bci->second.members.find(propName);
                if (mit != bci->second.members.end()) targetIsRcShare = ctx.isRefcountModel(mit->second.typeName);
            }
        }
    }
    if (setStmt->value && !memberDeepCopy && !memberHandleSource && !srcIsManagedString && !targetIsRefBorrow
        && !targetIsRcShare)
        consumeLocalDrop(setStmt->value.get());

    // Expose the target variable's type so db.open_kv / sys.deserialize can infer
    // the value type.  For plain variables (propName empty) use the variable's own
    // type.  For `this.field` assignments (class member writes), look up the
    // field's declared type so that e.g. set:[<.kv, {call:[db.open_kv,...]}] with
    // member type "Kv<string:i32>" produces the correct val_size.
    if (propName.empty()) {
        ctx.deserializeTargetType = sym->typeName;
    } else if (baseName == "this" && !propName.empty()) {
        // Resolve field type from enclosing class
        if (!ctx.currentClassName.empty()) {
            auto cit = ctx.classes.find(ctx.currentClassName);
            if (cit != ctx.classes.end()) {
                auto mit = cit->second.members.find(propName);
                if (mit != cit->second.members.end()) {
                    ctx.deserializeTargetType = mit->second.typeName;
                }
            }
        }
    }
    if (rejectVoidCallInit(ctx, setStmt->value.get(),
            "cannot assign to '" + targetStr + "'",
            setStmt->sourceNode)) {
        ctx.deserializeTargetType.clear();
        return;
    }
    // The ordinary variable reader materializes the legacy container struct. A member
    // assignment needs the owning handle instead (also for by-reference parameters).
    IRValue* val = nullptr;
    if (memberHandleSource) {
        llvm::Value* slot = memberHandleSource->ir_alloca_inst
            ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(memberHandleSource->ir_alloca_inst))
            : memberHandleSource->alloca_inst;
        val = ctx.ir_gen->wrap(ctx.builder.CreateLoad(
            llvm::PointerType::get(ctx.context, 0), slot, "setf_source_handle"));
    } else {
        val = visitExpression(setStmt->value.get());
    }
    ctx.deserializeTargetType.clear();
    // spec/61 §7: was the RHS an OWNED string temp (interpolation/producer)? Captured BEFORE
    // consumeStringTemp clears it — a managed set MOVES an owned temp (assign_move) and COPIES a
    // borrow (assign_copy = dup).
    bool rhsWasOwnedStringTemp = val
        && inStringTempStack(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)));
    // spec/61: a string temp assigned into a target is moved (target owns the char*) →
    // remove it from the statement-temp decref list (idempotent for non-temp values).
    if (val) consumeStringTemp(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)));
    if (!val) {
        LOG_ERROR(ctx.fmtLoc() + "Failed to evaluate value expression for assignment to: " + targetStr);
        return;
    }
    
    if (propName.empty()) {
        if (sym->is_const) {
             LOG_ERROR(ctx.fmtLoc() + "Cannot assign to const variable " + targetStr);
             return;
        }
        // spec/61 §5.5/§8: a fresh owned call result moved into an OWNED value-model local — release
        // the old value, then store (tests/lifetime/75, 9rooms `set [mat, match_get(...)]`).
        if (std::string(rebindCopyFn) == "aiml_ser_deep_copy_replace" && !sym->is_byref_arg
            && dynamic_cast<CallExpr*>(setStmt->value.get())) {
            auto tci = ctx.classes.find(sym->typeName);
            llvm::Value* nv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            // A method call may hand back its result through a pointer instead of a struct value.
            if (tci != ctx.classes.end() && tci->second.serNodesVar && tci->second.type && nv
                && (nv->getType() == tci->second.type || nv->getType()->isPointerTy())) {
                llvm::Type* p = llvm::PointerType::get(ctx.context, 0);
                llvm::Value* dst = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
                if (nv->getType()->isPointerTy())   // capture before the release (alias-safe)
                    nv = ctx.builder.CreateLoad(tci->second.type, nv, "rebind_fresh");
                ctx.builder.CreateCall(ctx.getFunction("aiml_ser_free_decoded", ctx.builder.getVoidTy(),
                    {p, p, ctx.builder.getInt32Ty()}),
                    { dst, tci->second.serNodesVar, ctx.builder.getInt32(tci->second.serNodesCount) });
                ctx.builder.CreateStore(nv, dst);
                return;
            }
        }
        // Enum-typed target must not receive a string (enum is i32 under the
        // hood — a string would silently coerce to garbage).  Require an
        // enum-literal (EnumName.value) or an int.
        if (ctx.enums.count(sym->typeName)) {
            llvm::Value* sv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            if (sv && sv->getType()->isPointerTy()) {
                LOG_ERROR(ctx.fmtLoc() + "Cannot assign a string to enum-typed '" + targetStr +
                          "' (" + sym->typeName + ") — use " + sym->typeName + ".<value> or an int");
                return;
            }
        }
        // ── spec/61 §4.2: value-model set from a SHARING VIEW → DEEP COPY (not shallow) ──
        // `set [x, at(list,i)]` / `set [x, obj.field]` must give x an INDEPENDENT copy (value
        // semantics). A shallow store shares the container element's inline list/map heap; if x is
        // then RETURNED from a producer (load_entry_at → att_entry → aent) the caller owns that
        // shallow copy and its scope-drop double-frees the buffer with the container's own drop
        // (the 9rooms drive_ai_combat ZoneEntry double-free). Gated to sharing-view sources only:
        // a producer/owns-return call returns an already-independent owned temp (MOVE — deep-copying
        // it would leak the temp), and a same-type owned local is handled by the §4.2 block above.
        // AIML v2: a refcount-model local is a %M* reference — the v1 set-view DEEP-COPY (into an
        // inline struct slot) is wrong here (it would write a full struct into the 8-byte pointer
        // slot). The v2 rebind branch below (incref-new/decref-old, store the %M*) handles it.
        if (propName.empty() && !isDash && ctx.lifetimeDropsEnabled
            && !sym->is_global && !sym->is_byref_arg && !sym->is_const && !ctx.kvGetCopy
            && !ctx.isRefcountModel(sym->typeName)) {
            auto tciView = ctx.classes.find(sym->typeName);
            if (tciView != ctx.classes.end() && tciView->second.hasHeapFields
                && tciView->second.serNodesVar && tciView->second.type) {
                bool rhsIsView = false;
                Expr* rv = setStmt->value.get();
                if (dynamic_cast<AtExpr*>(rv)) {
                    rhsIsView = true;                          // container[index] → shared element view
                } else if (auto* ce = dynamic_cast<CallExpr*>(rv)) {
                    const std::string& fn = ce->functionName;
                    size_t dp = fn.rfind('.');
                    std::string sn = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                    if ((sn == "at" || sn == "get" || sn == "first" || sn == "last")
                        && !ctx.ownsReturnMatches(sn, sym->typeName))
                        rhsIsView = true;
                } else if (auto* ve = dynamic_cast<VariableExpr*>(rv)) {
                    if (ve->name.find('.') != std::string::npos || ve->name.find('<') != std::string::npos)
                        rhsIsView = true;                      // member/field access
                }
                if (rhsIsView) {
                    llvm::Value* rawVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
                    llvm::Type* p = llvm::PointerType::get(ctx.context, 0);
                    llvm::Value* srcAddr;
                    if (rawVal->getType()->isPointerTy()) {
                        srcAddr = rawVal;
                    } else {
                        llvm::AllocaInst* tmp = ctx.createEntryAlloca(tciView->second.type, "set_view_src");
                        ctx.builder.CreateStore(rawVal, tmp);
                        srcAddr = tmp;
                    }
                    llvm::Value* dst = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
                    llvm::Function* dc = ctx.getFunction(rebindCopyFn,
                        ctx.builder.getVoidTy(), {p, p, p, ctx.builder.getInt32Ty()});
                    ctx.builder.CreateCall(dc, { dst, srcAddr, tciView->second.serNodesVar,
                                                 ctx.builder.getInt32(tciView->second.serNodesCount) });
                    sym->is_view_alias = false;   // now an INDEPENDENT copy → not a view
                    return;
                }
            }
        }
        // For a BY-REF container param (list/map/buffer passed by reference), the alloca
        // holds a POINTER to the struct — a struct-sized write must go THROUGH it, not
        // into the 8-byte pointer slot (which would clobber the pointer and overflow into
        // adjacent stack → corruption/crash).  Dereference once for such targets.  For a
        // plain local (own struct alloca) this is a no-op.
        llvm::Value* writeDest = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
        if (sym->is_byref_arg) {
            llvm::Type* wptrTy = llvm::PointerType::get(ctx.context, 0);
            writeDest = ctx.builder.CreateLoad(wptrTy, writeDest, "byref_set_deref");
        }
        // spec/61 §5 (memV3): a container-HANDLE local reassigned (`set: [loc, src]`) must share/adopt
        // the HANDLE POINTER (incref/adopt src, release loc's OLD handle, store the pointer) — NOT
        // memcpy the {size,cap,data} struct, which shares src's data buffer so both loc and src free
        // it (effect_targetable `set etp = ent.passive_abilities` → double-free, test_creature_damage).
        // A borrow src (member/at) → incref; a plain-local src (consumeLocalDrop moved it above) or an
        // owned call-result → adopt (no incref). The OFF path keeps the struct memcpy below.
        if (ctx.memV3 && ctx.lifetimeDropsEnabled && ctx.isMemV3ContainerHandle(sym->typeName)) {
            auto* i8p = llvm::PointerType::get(ctx.context, 0);
            llvm::Type* i32Ty = ctx.builder.getInt32Ty();
            llvm::Type* i64Ty = ctx.builder.getInt64Ty();
            llvm::Value* slot = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));  // holds the handle ptr
            llvm::Value* newH = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            if (newH && newH->getType()->isPointerTy()) {
                std::string kind = sym->typeName.rfind("map<",0)==0 ? "map"
                    : ((sym->typeName=="buffer"||sym->typeName.rfind("buffer<",0)==0) ? "buffer" : "list");
                Expr* rhs = setStmt->value.get();
                bool borrowSrc = false;
                if (auto* rv = dynamic_cast<VariableExpr*>(rhs))
                    borrowSrc = (rv->name.find('.') != std::string::npos || rv->name.find('<') != std::string::npos);
                else if (dynamic_cast<AtExpr*>(rhs)) borrowSrc = true;
                else if (auto* ce = dynamic_cast<CallExpr*>(rhs)) {
                    // spec/61 §5.7: `.get`/`.at` of a nested container element is identical to `at` —
                    // the element keeps its handle, the binding increfs (test_list_list_inner_values).
                    const std::string& fn = ce->functionName;
                    size_t dp = fn.rfind('.');
                    std::string sn = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                    borrowSrc = dp != std::string::npos
                        && (sn == "get" || sn == "at" || sn == "first" || sn == "last")
                        && !ctx.ownsReturnMatches(sn, sym->typeName);
                }
                if (borrowSrc)   // incref-new BEFORE release-old (self-assign safe)
                    ctx.builder.CreateCall(ctx.getFunction("aiml_"+kind+"_handle_incref", ctx.builder.getVoidTy(), {i8p}), { newH });
                llvm::Value* oldH = ctx.builder.CreateLoad(i8p, slot, "seth_old");
                llvm::Value* nullp = llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p));
                if (kind == "buffer") {
                    ctx.builder.CreateCall(ctx.getFunction("aiml_buffer_handle_release", ctx.builder.getVoidTy(), {i8p}), { oldH });
                } else if (kind == "list") {
                    std::string en = sym->typeName.rfind("cascade<",0)==0 ? "i32" : sym->typeName.substr(5, sym->typeName.size()-6);
                    int ek = (en=="string")?1 : (ctx.isRefcountModel(en)?3 : (ctx.isMemV3ContainerHandle(en)?5:0));
                    ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_release", ctx.builder.getVoidTy(), {i8p, i32Ty, i64Ty, i8p, i32Ty}),
                        { oldH, ctx.builder.getInt32(ek), ctx.builder.getInt64(8), nullp, ctx.builder.getInt32(0) });
                } else {
                    std::string inr = sym->typeName.substr(4, sym->typeName.size()-5);
                    size_t col = inr.find(':');
                    std::string kn = col==std::string::npos?inr:inr.substr(0,col);
                    std::string vn = col==std::string::npos?std::string("i32"):inr.substr(col+1);
                    int kk = (kn=="string")?1:0; int vk = (vn=="string")?1:(ctx.isRefcountModel(vn)?3:0);
                    ctx.builder.CreateCall(ctx.getFunction("aiml_map_handle_release", ctx.builder.getVoidTy(), {i8p, i32Ty, i64Ty, i32Ty, i64Ty, i8p, i32Ty}),
                        { oldH, ctx.builder.getInt32(kk), ctx.builder.getInt64(8), ctx.builder.getInt32(vk), ctx.builder.getInt64(8), nullp, ctx.builder.getInt32(0) });
                }
                ctx.builder.CreateStore(newH, slot);
                return;
            }
        }
        if (sym->typeName.find("list<") == 0) {
            llvm::Value* rawVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            if (rawVal->getType()->isPointerTy()) {
                // Builtin handler / alloca ptr → memcpy struct contents
                llvm::StructType* listStructTy = ListImpl::getListStructType(ctx);
                uint64_t structSize = ctx.module.getDataLayout().getTypeAllocSize(listStructTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                llvm::Function* memcpyFunc = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
                ctx.builder.CreateCall(memcpyFunc, {
                    writeDest,
                    rawVal,
                    ctx.getSizeConst(structSize)
                });
            } else {
                // User function returns list struct value → store directly
                ctx.builder.CreateStore(rawVal, writeDest);
            }
            return;
        }
        if (sym->typeName.find("buffer<") == 0) {
            llvm::Type* bufTy = ctx.getLLVMType("buffer");
            if (bufTy) {
                uint64_t structSize = ctx.module.getDataLayout().getTypeAllocSize(bufTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                llvm::Function* memcpyFunc = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
                ctx.builder.CreateCall(memcpyFunc, {
                    writeDest,
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)),
                    ctx.getSizeConst(structSize)
                });
                return;
            }
        }
        if (sym->typeName.find("map<") == 0) {
            llvm::Type* mapTy = ctx.getLLVMType(sym->typeName);
            if (mapTy) {
                uint64_t structSize = ctx.module.getDataLayout().getTypeAllocSize(mapTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                llvm::Function* memcpyFunc = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
                ctx.builder.CreateCall(memcpyFunc, {
                    writeDest,
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)),
                    ctx.getSizeConst(structSize)
                });
                return;
            }
        }
        // User struct/class assigned from deserialized i8* heap pointer → memcpy
        // OR deep-copy when ctx.kvGetCopy is set (kv.get_copy pattern, see
        // doc/issue/kv_get_mutate_set_same_key_crash.md). visitDeclStmt has the
        // same logic for decl-with-init; this branch handles `set: [exp, kv.get_copy(...)]`
        // where exp was declared earlier without an initializer.
        // AIML v2: a refcount-model target is a %M* slot, NOT an inline struct — a struct memcpy
        // here would write the struct's bytes into the 8-byte pointer slot (storing the first field
        // as the "pointer"). The v2 rebind branch below stores the %M* reference instead.
        auto classIt = ctx.classes.find(sym->typeName);
        if (classIt != ctx.classes.end() && !classIt->second.isBuiltin && classIt->second.type
            && !ctx.isRefcountModel(sym->typeName)) {
            llvm::StructType* structTy = classIt->second.type;
            uint64_t structSize = ctx.module.getDataLayout().getTypeAllocSize(structTy);
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* rawVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            // Only act when the value is a pointer (i8* from deserializer), not a struct value
            if (rawVal->getType()->isPointerTy()) {
                bool needsDeepCopy = ctx.kvGetCopy
                                     && classIt->second.hasHeapFields
                                     && classIt->second.serNodesVar;
                if (needsDeepCopy) {
                    // Encode + decode round-trip → fresh heap allocations independent of cache.
                    llvm::Function* fn = ctx.getFunction("aiml_ser_deep_copy",
                        ctx.builder.getVoidTy(),
                        {ptrTy, ptrTy, ptrTy, ctx.builder.getInt32Ty()});
                    ctx.builder.CreateCall(fn, {
                        static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)),
                        rawVal,
                        classIt->second.serNodesVar,
                        ctx.builder.getInt32(classIt->second.serNodesCount)
                    });
                    ctx.kvGetCopy = false;
                    return;
                }
                llvm::Function* memcpyFunc = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
                ctx.builder.CreateCall(memcpyFunc, {
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)),
                    rawVal,
                    ctx.getSizeConst(structSize)
                });
                // Free the abandoned sys.deserialize carrier shell (see visitDeclStmt, spec/61 §3/§6.1).
                if (auto* dc = llvm::dyn_cast<llvm::CallInst>(rawVal))
                    if (auto* cf = dc->getCalledFunction())
                        if (cf->getName().startswith("__aiml_deser_"))
                            ctx.builder.CreateCall(
                                ctx.getFunction("free", ctx.builder.getVoidTy(), {ptrTy}), {rawVal});
                return;
            }
        }
        // path type: auto-intern the value so pointer equality == path equality
        if (sym->typeName == "path") {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* internFn = ctx.getFunction("aiml_path_intern", i8p, {i8p});
            llvm::Value* rawVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            llvm::Value* interned = ctx.builder.CreateCall(internFn, {rawVal}, "path_interned");
            val = ctx.ir_gen->wrap(interned);
        }
        // String → int coercion: set: [x, "${str_var}"] where x is i32/i64
        // Null-safe: if pointer is null → 0 instead of crashing atoi(NULL)
        if (sym->typeName == "i32" || sym->typeName == "i64") {
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            if (v->getType()->isPointerTy()) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                bool is32 = (sym->typeName == "i32");
                llvm::Type* intTy = is32 ? ctx.builder.getInt32Ty() : ctx.builder.getInt64Ty();
                llvm::Value* zero = is32 ? ctx.builder.getInt32(0)
                                         : (llvm::Value*)ctx.builder.getInt64(0);
                llvm::Value* isNull = ctx.builder.CreateIsNull(v, "str2i_null");
                llvm::Function* parent = ctx.builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* nullBB  = llvm::BasicBlock::Create(ctx.context, "str2i_null_bb", parent);
                llvm::BasicBlock* okBB    = llvm::BasicBlock::Create(ctx.context, "str2i_ok",      parent);
                llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "str2i_merge",   parent);
                ctx.builder.CreateCondBr(isNull, nullBB, okBB);
                ctx.builder.SetInsertPoint(nullBB);
                ctx.builder.CreateBr(mergeBB);
                ctx.builder.SetInsertPoint(okBB);
                llvm::Function* fn = is32 ? ctx.getFunction("atoi",  ctx.builder.getInt32Ty(), {i8p})
                                          : ctx.getFunction("atoll", ctx.builder.getInt64Ty(), {i8p});
                llvm::Value* callResult = ctx.builder.CreateCall(fn, {v}, is32 ? "str2i32_v" : "str2i64_v");
                ctx.builder.CreateBr(mergeBB);
                ctx.builder.SetInsertPoint(mergeBB);
                llvm::PHINode* phi = ctx.builder.CreatePHI(intTy, 2, is32 ? "str2i32" : "str2i64");
                phi->addIncoming(zero, nullBB);
                phi->addIncoming(callResult, okBB);
                val = ctx.ir_gen->wrap(phi);
            }
        }
        // Numeric coercion (int↔int, int→float, float→int)
        {
            llvm::Value* sv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            if (sv) {
                llvm::Type* targetTy = ctx.getLLVMType(sym->typeName);
                if (targetTy && targetTy != sv->getType()) {
                    if (targetTy->isIntegerTy() && sv->getType()->isIntegerTy()) {
                        unsigned dstBits = targetTy->getIntegerBitWidth();
                        unsigned srcBits = sv->getType()->getIntegerBitWidth();
                        if (dstBits < srcBits)
                            val = ctx.ir_gen->wrap(ctx.builder.CreateTrunc(sv, targetTy, "trunc_narrow"));
                        else if (srcBits == 1)
                            val = ctx.ir_gen->wrap(ctx.builder.CreateZExt(sv, targetTy, "zext_bool"));
                        else
                            val = ctx.ir_gen->wrap(ctx.builder.CreateSExt(sv, targetTy, "sext_widen"));
                    } else if (targetTy->isFloatingPointTy() && sv->getType()->isIntegerTy()) {
                        val = ctx.ir_gen->wrap(ctx.builder.CreateSIToFP(sv, targetTy, "int_to_fp"));
                    } else if (targetTy->isIntegerTy() && sv->getType()->isFloatingPointTy()) {
                        val = ctx.ir_gen->wrap(ctx.builder.CreateFPToSI(sv, targetTy, "fp_to_int"));
                    } else if (targetTy->isFloatingPointTy() && sv->getType()->isFloatingPointTy()) {
                        val = ctx.ir_gen->wrap(ctx.builder.CreateFPCast(sv, targetTy, "fp_cast"));
                    }
                }
            }
        }
        {
            std::string mm = scalarStoreMismatch(ctx, sym->typeName,
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)));
            if (!mm.empty()) {
                LOG_ERROR(ctx.fmtLoc() + "Incompatible assignment to '" + targetStr + "': " + mm);
                return;
            }
        }
        // spec/61 §7: managed string-local reassignment maintains "null | one owned RC ref" and frees
        // the OLD value (fixes the deckset-editor self-accumulation leak: build_card_text /
        // ability_block_cached / CardArtCache_fetch). An owned temp (interpolation/producer) is MOVED
        // (assign_move: store + decref old); a borrow is COPIED (assign_copy: dup + decref old). The
        // exact self-assignment `set:[x,x]` was already returned as a no-op above.
        // spec/61 §4 rule 3: a GLOBAL string slot also owns one ref (decl init is normalised) → a
        // rebind releases the old value exactly like a managed local.
        bool globalStringSlot = ctx.memV3 && sym->is_global && sym->typeName == "string"
            && sym->lifetime != "persistent" && !sym->is_byref_arg;
        if (ctx.lifetimeDropsEnabled && (isManagedStringLocalSym(*sym) || globalStringSlot)) {
            if (sym->is_const) {
                LOG_ERROR(ctx.fmtLoc() + "Cannot assign to const variable " + targetStr);
                return;
            }
            llvm::Value* newRaw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            llvm::Value* slot   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
            if (newRaw && newRaw->getType()->isPointerTy()) {
                llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
                if (rhsWasOwnedStringTemp) {
                    llvm::Function* mv = ctx.getFunction("aiml_str_assign_move",
                        ctx.builder.getVoidTy(), {i8p->getPointerTo(), i8p});
                    ctx.builder.CreateCall(mv, { slot, newRaw });
                } else if (ctx.memV3) {
                    // spec/61 §4: a borrow is SHARED (incref), not dup'd. Secure-before-release in
                    // aiml_str_assign_share makes self-assignment a no-op. Immutable → same content.
                    llvm::Function* sh = ctx.getFunction("aiml_str_assign_share",
                        ctx.builder.getInt32Ty(), {i8p->getPointerTo(), i8p});
                    ctx.builder.CreateCall(sh, { slot, newRaw });
                } else {
                    llvm::Function* cp = ctx.getFunction("aiml_str_assign_copy",
                        ctx.builder.getInt32Ty(), {i8p->getPointerTo(), i8p});
                    ctx.builder.CreateCall(cp, { slot, newRaw });
                }
                return;
            }
        }
        // AIML v2 P5b: rebind a refcount-MODEL local (`set:[local, m]`). Store the new %M* with
        // incref-new-BEFORE-decref-old (self-alias `set:[x,x]` safe). A borrow source
        // (variable/at/member) is incref'd (share); an owned temp (new/producer call) MOVES (no
        // incref). The old reference is decref'd (freed at 0).
        if (ctx.isRefcountModel(sym->typeName)) {
            if (sym->is_const) { LOG_ERROR(ctx.fmtLoc() + "Cannot assign to const variable " + targetStr); return; }
            auto* i8p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* newObj = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            llvm::Value* slot   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
            if (newObj && newObj->getType()->isPointerTy()) {
                if (newObj->getType() != i8p) newObj = ctx.builder.CreateBitCast(newObj, i8p, "set_modelptr");
                bool shareIncref = false;
                Expr* rv = setStmt->value.get();
                if (dynamic_cast<AtExpr*>(rv)) shareIncref = true;
                else if (dynamic_cast<VariableExpr*>(rv)) shareIncref = true;
                else if (auto* ce = dynamic_cast<CallExpr*>(rv)) {
                    const std::string& fn = ce->functionName; size_t dp = fn.rfind('.');
                    std::string sn = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                    if ((sn == "at" || sn == "get" || sn == "first" || sn == "last")
                        && !ctx.ownsReturnMatches(sn, sym->typeName)) shareIncref = true;
                }
                if (shareIncref) {
                    llvm::Function* inc = ctx.getFunction("aiml_model_incref", ctx.builder.getVoidTy(), {i8p});
                    ctx.builder.CreateCall(inc, { newObj });
                }
                if (ctx.lifetimeDropsEnabled) {
                    std::string bareRc = sym->typeName;   // spec/61 §5.5: shared<T> → class T
                    if (bareRc.rfind("shared<", 0) == 0 && bareRc.back() == '>') bareRc = bareRc.substr(7, bareRc.size() - 8);
                    auto ci = ctx.classes.find(bareRc);
                    if (ci != ctx.classes.end() && ci->second.serNodesVar) {
                        llvm::Value* oldObj = ctx.builder.CreateLoad(i8p, slot, "set_old_modelptr");
                        llvm::Function* dref = ctx.getFunction("aiml_model_decref", ctx.builder.getVoidTy(),
                            {i8p, i8p, ctx.builder.getInt32Ty()});
                        ctx.builder.CreateCall(dref, { oldObj, ci->second.serNodesVar,
                                                       ctx.builder.getInt32(ci->second.serNodesCount) });
                    }
                }
                ctx.builder.CreateStore(newObj, slot);
                return;
            }
        }
        ctx.ir_gen->createStore(val, baseAlloca);
    } else {
        std::string className = sym->typeName;
        if ((className.rfind("ptr<", 0) == 0 || className.rfind("ref<", 0) == 0) && className.back() == '>') {
             className = className.substr(4, className.length() - 5);
        }
        
        llvm::Type* llvmBaseElemTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(baseElemTy));
        if (isDash && (className.find("list<") == 0 || llvmBaseElemTy->isArrayTy())) {
            size_t dashInProp = propName.find('-');
            if (dashInProp != std::string::npos) {
                LOG_ERROR(ctx.fmtLoc() + "Slice assignment is not supported yet: " + targetStr);
                return;
            }
            
            llvm::Value* idxVal = nullptr;
            try {
                idxVal = ctx.builder.getInt32(std::stoi(propName));
            } catch(...) {
                Symbol* idxSym = resolveVariable(propName);
                if (idxSym) {
                    IRValue* idxAlloca = idxSym->ir_alloca_inst ? idxSym->ir_alloca_inst : ctx.ir_gen->wrap(idxSym->alloca_inst);
                    IRType* idxElemTy = idxSym->ir_element_type ? idxSym->ir_element_type : ctx.ir_gen->wrapType(idxSym->element_type);
                    idxVal = ctx.builder.CreateLoad(static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(idxElemTy)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(idxAlloca)));
                    if (idxVal->getType()->isIntegerTy(64)) idxVal = ctx.builder.CreateTrunc(idxVal, ctx.builder.getInt32Ty());
                } else {
                    LOG_ERROR(ctx.fmtLoc() + "Invalid list index for set: " + propName);
                    return;
                }
            }
            
            llvm::Value* elemPtr = nullptr;
            if (llvmBaseElemTy->isArrayTy()) {
                elemPtr = ctx.builder.CreateInBoundsGEP(llvmBaseElemTy, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), {ctx.builder.getInt32(0), idxVal});
            } else {
                // Resolve the inner element type (e.g. "list<i32>" → i32).
                // sym->element_type is the list *container* struct, not the element type.
                llvm::Type* innerElemTy = llvmBaseElemTy; // fallback
                if (className.find("list<") == 0 && className.back() == '>') {
                    std::string innerName = className.substr(5, className.length() - 6);
                    llvm::Type* resolved = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(innerName)));
                    if (resolved) innerElemTy = resolved;
                }
                // spec/61 §5: a memV3 list (LOCAL or GLOBAL, both is_byref_arg) slot holds the handle
                // POINTER, not the {size,cap,data} struct — load it before GEP, else the pointer bytes
                // are strided as the struct → element write corrupts memory (test_list `set:[lst-0,v]`).
                llvm::Value* listBase = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
                if (sym->is_byref_arg) {
                    auto* i8pH = ctx.builder.getInt8Ty()->getPointerTo();
                    listBase = ctx.builder.CreateLoad(i8pH, listBase, "conh_dashset");
                }
                elemPtr = ListImpl::getElementPtr(ctx, listBase, idxVal, innerElemTy);
            }
            ctx.builder.CreateStore(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)), elemPtr);
            return;
        }
        
        // Try resolvePropertyWritePtr first — handles both shallow (a.b) and deep (a.b.c) chains.
        {
            Symbol tmpBaseSym;
            tmpBaseSym.typeName        = sym->typeName;
            tmpBaseSym.ir_alloca_inst  = baseAlloca;
            tmpBaseSym.ir_element_type = baseElemTy;
            std::string finalTypeName;
            IRValue* targetGep = resolvePropertyWritePtr(&tmpBaseSym, propName, finalTypeName);
            if (targetGep) {
                // Enum-typed member must not receive a string (see top-level path).
                if (!finalTypeName.empty() && ctx.enums.count(finalTypeName)) {
                    llvm::Value* sv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
                    if (sv && sv->getType()->isPointerTy()) {
                        LOG_ERROR(ctx.fmtLoc() + "Cannot assign a string to enum-typed '" + targetStr +
                                  "' (" + finalTypeName + ") — use " + finalTypeName + ".<value> or an int");
                        return;
                    }
                }
                // AIML v2 P5b: store into a refcount-MODEL field (`set:[parent.model_field, m]`). The
                // slot holds a %M*; store the new pointer with incref-new-BEFORE-decref-old (self-alias
                // safe). A borrow source (variable/at/member) is incref'd (share); an owned temp
                // (new/producer) MOVES. Bypasses the container/deep-copy paths below.
                if (ctx.isRefcountModel(finalTypeName)) {
                    auto* i8p = llvm::PointerType::get(ctx.context, 0);
                    llvm::Value* dstSlot = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(targetGep));
                    llvm::Value* newObj  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
                    if (newObj && newObj->getType()->isPointerTy()) {
                        if (newObj->getType() != i8p) newObj = ctx.builder.CreateBitCast(newObj, i8p, "setf_modelptr");
                        bool shareIncref = false;
                        Expr* rv = setStmt->value.get();
                        if (dynamic_cast<AtExpr*>(rv)) shareIncref = true;
                        else if (dynamic_cast<VariableExpr*>(rv)) shareIncref = true;
                        else if (auto* ce = dynamic_cast<CallExpr*>(rv)) {
                            const std::string& fn = ce->functionName; size_t dp = fn.rfind('.');
                            std::string sn = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                            if ((sn == "at" || sn == "get" || sn == "first" || sn == "last")
                                && !ctx.ownsReturnMatches(sn, finalTypeName)) shareIncref = true;
                        }
                        if (shareIncref) {
                            llvm::Function* inc = ctx.getFunction("aiml_model_incref", ctx.builder.getVoidTy(), {i8p});
                            ctx.builder.CreateCall(inc, { newObj });
                        }
                        if (ctx.lifetimeDropsEnabled) {
                            std::string bareRcF = finalTypeName;   // spec/61 §5.5: shared<T> → class T
                            if (bareRcF.rfind("shared<", 0) == 0 && bareRcF.back() == '>') bareRcF = bareRcF.substr(7, bareRcF.size() - 8);
                            auto ci = ctx.classes.find(bareRcF);
                            if (ci != ctx.classes.end() && ci->second.serNodesVar) {
                                llvm::Value* oldObj = ctx.builder.CreateLoad(i8p, dstSlot, "setf_old_modelptr");
                                llvm::Function* dref = ctx.getFunction("aiml_model_decref", ctx.builder.getVoidTy(),
                                    {i8p, i8p, ctx.builder.getInt32Ty()});
                                ctx.builder.CreateCall(dref, { oldObj, ci->second.serNodesVar,
                                                               ctx.builder.getInt32(ci->second.serNodesCount) });
                            }
                        }
                        ctx.builder.CreateStore(newObj, dstSlot);
                        return;
                    }
                }
                // spec/61 §5.5/§6.1: an owning ptr<T> FIELD receiving a fresh box (`{new: T}`, an owning
                // call) or a moved ptr local: secure the new box, release the old one, store.
                if (ctx.memV3 && ctx.lifetimeDropsEnabled && finalTypeName.rfind("ptr<", 0) == 0
                    && finalTypeName.back() == '>') {
                    std::string boxed = finalTypeName.substr(4, finalTypeName.size() - 5);
                    auto bci = ctx.classes.find(boxed);
                    Expr* rv = setStmt->value.get();
                    bool fresh = dynamic_cast<NewExpr*>(rv) != nullptr;
                    if (auto* fc = dynamic_cast<CallExpr*>(rv)) {
                        size_t dp = fc->functionName.rfind('.');
                        std::string sn = (dp == std::string::npos) ? fc->functionName : fc->functionName.substr(dp + 1);
                        fresh = ctx.ownsReturnMatches(sn, finalTypeName);
                    }
                    if (auto* ve = dynamic_cast<VariableExpr*>(rv))
                        if (ve->name.find('.') == std::string::npos)
                            if (Symbol* rs = resolveVariable(ve->name)) fresh = rs->typeName == finalTypeName && !rs->is_param;
                    llvm::Value* dstSlot = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(targetGep));
                    llvm::Value* newP = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
                    if (fresh && bci != ctx.classes.end() && bci->second.type && !ctx.classGroupContainers.count(boxed)
                        && !ctx.cascadeClasses.count(boxed) && dstSlot && newP && newP->getType()->isPointerTy()) {
                        auto* i8p = llvm::PointerType::get(ctx.context, 0);
                        llvm::Value* oldP = ctx.builder.CreateLoad(i8p, dstSlot, "fbox_old");
                        llvm::Function* fn = ctx.builder.GetInsertBlock()->getParent();
                        llvm::BasicBlock* relBB = llvm::BasicBlock::Create(ctx.context, "fbox_rel", fn);
                        llvm::BasicBlock* doneBB = llvm::BasicBlock::Create(ctx.context, "fbox_done", fn);
                        ctx.builder.CreateCondBr(ctx.builder.CreateAnd(ctx.builder.CreateIsNotNull(oldP),
                                                 ctx.builder.CreateICmpNE(oldP, newP)), relBB, doneBB);
                        ctx.builder.SetInsertPoint(relBB);
                        if (bci->second.hasHeapFields && bci->second.serNodesVar)
                            ctx.builder.CreateCall(ctx.getFunction("aiml_ser_free_decoded", ctx.builder.getVoidTy(),
                                {i8p, i8p, ctx.builder.getInt32Ty()}),
                                { oldP, bci->second.serNodesVar, ctx.builder.getInt32(bci->second.serNodesCount) });
                        ctx.builder.CreateCall(ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p}), { oldP });
                        ctx.builder.CreateBr(doneBB);
                        ctx.builder.SetInsertPoint(doneBB);
                        ctx.builder.CreateStore(newP, dstSlot);
                        return;
                    }
                }
                // spec/61 §5 rule 1 (memV3): `set:[obj.field, src]` where field is a CONTAINER
                // SHARES the handle (incref new, decref old, store) — a mutation through either
                // field is then visible through the other (probe 24). The RHS was evaluated to the
                // source handle (resolveProperty loads a container field/local's handle).
                if (ctx.memV3 && ctx.isMemV3ContainerHandle(finalTypeName)) {
                    auto* i8p = llvm::PointerType::get(ctx.context, 0);
                    // Resolve the storage slot, not the loaded receiver handle.
                    std::string slotTy;
                    IRValue* slotIR = resolvePropertyWritePtr(&tmpBaseSym, propName, slotTy, true);
                    llvm::Value* dstSlot = slotIR ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(slotIR)) : nullptr;
                    llvm::Value* newH = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
                    // Some legacy builtins (ai.embedding_lookup) still return inline structs.
                    // Keep their existing lowering; named container sources above are handles.
                    if (dstSlot && newH && newH->getType()->isPointerTy()) {
                        std::string kind = finalTypeName.rfind("map<", 0) == 0 ? "map"
                            : ((finalTypeName == "buffer" || finalTypeName.rfind("buffer<", 0) == 0) ? "buffer" : "list");
                        // Secure the new reference before dropping the old one, including self-aliases.
                        // A FRESH owned call result has no other owner → adopt it (tests/lifetime/77).
                        // spec/61 §8: any non-accessor call result is an owning reference.
                        bool adoptFresh = dynamic_cast<CallExpr*>(setStmt->value.get())
                            && !isContainerAccessorCall(ctx, setStmt->value.get(), finalTypeName);
                        if (!adoptFresh)
                            ctx.builder.CreateCall(ctx.getFunction("aiml_" + kind + "_handle_incref",
                                ctx.builder.getVoidTy(), {i8p}), {newH});
                        if (ctx.lifetimeDropsEnabled) {
                            int nc = 0;
                            llvm::GlobalVariable* descriptor = elemHandleSerNodes(finalTypeName, nc);
                            if (!descriptor) {
                                LOG_ERROR(ctx.fmtLoc() + "Missing container release descriptor: " + finalTypeName);
                                return;
                            }
                            ctx.builder.CreateCall(ctx.getFunction("aiml_ser_free_decoded",
                                ctx.builder.getVoidTy(), {i8p, i8p, ctx.builder.getInt32Ty()}),
                                {dstSlot, descriptor, ctx.builder.getInt32(nc)});
                        }
                        ctx.builder.CreateStore(newH, dstSlot);
                        return;
                    }
                }
                // spec/61 §7.0 — RELEASE the member's PREVIOUS value before overwriting it, so a
                // repeated assignment (poll_board_state's `<.board_state = …`, take_gone_players's
                // `<.entries = kept`) does not leak the prior heap. SCOPE (important): only a
                // `this`-rooted member store of a CONTAINER (list/map/buffer) or a heap-owning MODEL.
                //   • `this`-member only: a class member exclusively owns its heap (assignment already
                //     Moves/DeepCopies into it). A `local.field` store (e.g. `ent.player_id` where
                //     `ent` is a shallow `at(<.list, i)` element view) may ALIAS a container element,
                //     so freeing "the old value" there would double-free the shared element.
                //   • container/model only: string/scalar/enum/ptr fields either own no heap or are
                //     handled by their own store path (the string-field incref); releasing them here
                //     would double-decref a shared string (the reconnect double-free).
                if (ctx.lifetimeDropsEnabled && baseName == "this" && !finalTypeName.empty()) {
                    bool relHeap = finalTypeName.rfind("list<", 0) == 0
                                || finalTypeName.rfind("map<", 0) == 0
                                || finalTypeName.rfind("buffer<", 0) == 0;
                    if (!relHeap) {
                        auto rci = ctx.classes.find(finalTypeName);
                        relHeap = (rci != ctx.classes.end() && !rci->second.isBuiltin
                                   && rci->second.hasHeapFields);
                    }
                    if (relHeap) {
                        llvm::Value* dstRel = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(targetGep));
                        llvm::Value* rawV   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
                        llvm::Value* srcRel = (rawV && rawV->getType()->isPointerTy()) ? rawV : nullptr;
                        emitContainerFieldFree(dstRel, srcRel, finalTypeName);
                    }
                }
                // Apply integer narrowing/widening for final member type
                if (!finalTypeName.empty()) {
                    llvm::Value* sv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
                    if (sv && sv->getType()->isIntegerTy()) {
                        llvm::Type* targetTy = ctx.getLLVMType(finalTypeName);
                        if (targetTy && targetTy->isIntegerTy() && targetTy != sv->getType()) {
                            unsigned dstBits = targetTy->getIntegerBitWidth();
                            unsigned srcBits = sv->getType()->getIntegerBitWidth();
                            if (dstBits < srcBits)
                                val = ctx.ir_gen->wrap(ctx.builder.CreateTrunc(sv, targetTy, "trunc_member"));
                            else if (srcBits == 1)
                                val = ctx.ir_gen->wrap(ctx.builder.CreateZExt(sv, targetTy, "zext_member"));
                            else
                                val = ctx.ir_gen->wrap(ctx.builder.CreateSExt(sv, targetTy, "sext_member"));
                        }
                    }
                }
                // ── Struct-content copy for list<T>/buffer<T>/map<K:V> fields ────
                // For these types, resolveProperty (used to evaluate the RHS) and
                // the RHS-as-VariableExpr path return a POINTER to the inline
                // struct, not a loaded value.  A plain `store ptr, targetGep`
                // would overwrite only the first 8 bytes of the destination's
                // inline struct — leaving size/capacity correct but data garbage
                // (or vice versa).  Use memcpy for the full struct width.
                //
                // Note: this is a shallow copy.  The destination shares heap
                // data with the source — same semantics as `decl: [s, T, src]`.
                // For deep copy use kv.get_copy / aiml_ser_deep_copy paths.
                bool isInlineHeapStruct =
                    finalTypeName.rfind("list<",   0) == 0 ||
                    finalTypeName.rfind("buffer<", 0) == 0 ||
                    finalTypeName.rfind("map<",    0) == 0;
                if (isInlineHeapStruct) {
                    llvm::Type* fieldTy = ctx.getLLVMType(finalTypeName);
                    llvm::Value* srcPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
                    llvm::Value* dstPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(targetGep));
                    if (fieldTy && srcPtr && srcPtr->getType()->isPointerTy()) {
                        // spec/61 §7.0 — a plain struct memcpy is a SHALLOW copy: the destination
                        // field would share the source's heap buffer (data/keys/vals).  When the
                        // source is NOT a fresh owned local (plan → DeepCopy: a borrow/param/view,
                        // e.g. `dat.produces = req.produces`), DEEP-COPY the container so the
                        // destination owns an INDEPENDENT buffer — then the destination model can
                        // keep its scope-drop with no double-free (was the 9rooms create_bundle
                        // crash) AND no leak.  For an owned-local source (Move) the shallow memcpy
                        // is correct: consumeLocalDrop(value) has already transferred ownership.
                        if (memberDeepCopy && emitContainerFieldDeepCopy(dstPtr, srcPtr, finalTypeName))
                            return;
                        // MOVE case (owned-local source, shallow struct memcpy). The member's prior
                        // value was already released by the centralized member-field free above, so
                        // this memcpy writes into a zeroed field; the moved-in source's own scope-drop
                        // was consumed by consumeLocalDrop → no double-free, no leak.
                        uint64_t sz = ctx.module.getDataLayout().getTypeAllocSize(fieldTy);
                        llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                        llvm::Function* mcf = ctx.getFunction("memcpy", ptrTy,
                            {ptrTy, ptrTy, ctx.getSizeType()});
                        ctx.builder.CreateCall(mcf, {dstPtr, srcPtr, ctx.getSizeConst(sz)});
                        return;
                    }
                }
                // ── User model/class field assigned from a HEAP POINTER
                // (kv.get_copy / deserializer i8*).  A plain `store ptr` writes
                // only the first 8 bytes (the pointer) into the destination
                // struct → garbage.  Deep-copy when kv.get_copy was the source
                // (independent heap, see kv_getcopy_nested_slot backlog),
                // otherwise memcpy the full struct width.  When `val` is a
                // struct VALUE (nested-assign of a loaded struct) we fall
                // through to createStore, which is correct.
                {
                    auto cIt = ctx.classes.find(finalTypeName);
                    if (cIt != ctx.classes.end() && !cIt->second.isBuiltin && cIt->second.type) {
                        // Model target ← plain variable of a DIFFERENT model type is almost always
                        // a bug (silent wrong-size memcpy). Run this check FIRST so the Increment C
                        // fast-path below cannot bypass it (finding #3).
                        bool srcIsPtrLike = false;   // finding #2: a ptr<T>/ref<T> source variable
                        if (auto* rve = dynamic_cast<VariableExpr*>(setStmt->value.get())) {
                            if (rve->name.find('.') == std::string::npos) {
                                if (Symbol* rs = resolveVariable(rve->name)) {
                                    if (rs->typeName.rfind("ptr<", 0) == 0 || rs->typeName.rfind("ref<", 0) == 0)
                                        srcIsPtrLike = true;
                                    if (!srcIsPtrLike && rs->typeName != finalTypeName &&
                                        ctx.classes.count(rs->typeName) &&
                                        !ctx.classes[rs->typeName].isBuiltin &&
                                        !modelsInheritanceRelated(ctx, rs->typeName, finalTypeName)) {
                                        LOG_ERROR(ctx.fmtLoc() + "Incompatible assignment to '" + targetStr +
                                            "': target is model '" + finalTypeName + "' but the value is unrelated model '" +
                                            rs->typeName + "'");
                                        return;
                                    }
                                }
                            }
                        }
                        // spec/61 §7.0 (Increment C) fast-path: the plan says this member store
                        // copies a borrowed/view/param source → deep-copy from the source's ADDRESS
                        // so the member owns an INDEPENDENT heap. Skipped for a ptr<T>/ref<T> source
                        // (finding #2: emitLValueAddress would give the pointer SLOT, not the pointee
                        // model — those fall through to the pointer-aware logic below).
                        if (memberDeepCopy && !srcIsPtrLike
                            && cIt->second.hasHeapFields && cIt->second.serNodesVar) {
                            std::string lvTy;
                            IRValue* srcAddr = emitLValueAddress(setStmt->value.get(), lvTy);
                            if (srcAddr) {
                                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                                // spec/61 §4.4.5: RELEASE the member's previous heap before overwriting —
                                // else a re-assigned / default-init value-model FIELD leaks its old
                                // container handles (the match-view per-poll leak: `set out.match =
                                // view_buf`). *_replace encodes src FIRST, then frees dst, so a self/
                                // aliasing copy stays safe; a zero-init field frees to a no-op.
                                llvm::Function* fn = ctx.getFunction("aiml_ser_deep_copy_replace",
                                    ctx.builder.getVoidTy(), {ptrTy, ptrTy, ptrTy, ctx.builder.getInt32Ty()});
                                ctx.builder.CreateCall(fn, {
                                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(targetGep)),
                                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(srcAddr)),
                                    cIt->second.serNodesVar,
                                    ctx.builder.getInt32(cIt->second.serNodesCount) });
                                return;
                            }
                        }
                        llvm::Value* srcPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
                        if (srcPtr && srcPtr->getType()->isPointerTy()) {
                            llvm::StructType* structTy = cIt->second.type;
                            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                            llvm::Value* dstPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(targetGep));
                            // spec/61 §7.0 (Increment C): deep-copy when kv.get_copy sourced it
                            // OR the ownership plan says this store copies a borrowed/view/param
                            // source (memberDeepCopy) → the member owns an INDEPENDENT heap, so a
                            // later recursive free of the aggregate cannot double-free a shared
                            // sub-heap (fixes the shared value-model member-store double-free).
                            bool needsDeepCopy = (ctx.kvGetCopy || memberDeepCopy)
                                                 && cIt->second.hasHeapFields
                                                 && cIt->second.serNodesVar;
                            if (needsDeepCopy) {
                                // spec/61 §4.4.5: release the member's previous heap first (see the
                                // fast-path above) — a re-assigned / default-init value-model field
                                // otherwise leaks its old container handles per store (match-view leak).
                                llvm::Function* fn = ctx.getFunction("aiml_ser_deep_copy_replace",
                                    ctx.builder.getVoidTy(),
                                    {ptrTy, ptrTy, ptrTy, ctx.builder.getInt32Ty()});
                                ctx.builder.CreateCall(fn, {
                                    dstPtr, srcPtr,
                                    cIt->second.serNodesVar,
                                    ctx.builder.getInt32(cIt->second.serNodesCount)
                                });
                                ctx.kvGetCopy = false;
                                return;
                            }
                            uint64_t sz = ctx.module.getDataLayout().getTypeAllocSize(structTy);
                            // spec/61 §5.5/§4.4.5: this is the MOVE path (an owned/producer struct value,
                            // not a borrowed/view/param source — those took deep_copy_replace above). Before
                            // overwriting the field, RELEASE its previous heap, else a re-assigned value-model
                            // FIELD leaks its old container/string handles per store (the per-poll render-tree
                            // leak: `set holder.cur = mock_state_from_match(...)`, fill_hand→MockState). src is
                            // an INDEPENDENT producer/owned temp (never a view of dst here), so free-before-
                            // copy is safe; the distinct-IR-value guard skips a trivial self-store.
                            if (cIt->second.hasHeapFields && cIt->second.serNodesVar && dstPtr != srcPtr) {
                                llvm::Function* frf = ctx.getFunction("aiml_ser_free_decoded",
                                    ctx.builder.getVoidTy(), {ptrTy, ptrTy, ctx.builder.getInt32Ty()});
                                ctx.builder.CreateCall(frf, { dstPtr, cIt->second.serNodesVar,
                                    ctx.builder.getInt32(cIt->second.serNodesCount) });
                            }
                            llvm::Function* mcf = ctx.getFunction("memcpy", ptrTy,
                                {ptrTy, ptrTy, ctx.getSizeType()});
                            ctx.builder.CreateCall(mcf, {dstPtr, srcPtr, ctx.getSizeConst(sz)});
                            return;
                        }
                    }
                }
                {
                    std::string mm = scalarStoreMismatch(ctx, finalTypeName,
                        static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)));
                    if (!mm.empty()) {
                        LOG_ERROR(ctx.fmtLoc() + "Incompatible assignment to '" + targetStr + "': " + mm);
                        return;
                    }
                }
                // spec/61 §7.0 — a STRING field stored from a BORROWED source (memberDeepCopy: a
                // member-access / param field, e.g. `mock.name = cdat.name`) shares the source's
                // refcounted buffer. The destination model must own an INDEPENDENT reference, else
                // when the source model is dropped (aiml_ser_free_decoded decref's its strings) the
                // stored copy dangles (the 9rooms deck-editor card-title/artwork UAF). incref here;
                // the destination model's own drop decref's it, keeping the count balanced. (Move
                // cases — memberDeepCopy=false — consume the source local instead, no incref.)
                //
                // BUT only a NAMED source (a VariableExpr: a plain local, a member-access like
                // `cdat.name`, or a borrowed param) genuinely SHARES an existing refcounted buffer.
                // A fresh OWNED temp — string interpolation `"..${x}.."`, a call result, a string
                // literal — is classified DeepCopy here too (its origin is Unknown, not Owned), but
                // it TRANSFERS its single reference into the member: increfing it leaks one ref (the
                // temp is never decref'd), and increfing a .rodata literal corrupts the byte before
                // it. So gate the incref on a VariableExpr source. (Owned-local sources take the
                // Move path — memberDeepCopy=false — and never reach here.)
                // spec/61 §7: a managed string LOCAL source is copied into the member (its consume was
                // skipped, so it stays owned) → the member needs its own incref'd ref, regardless of
                // memberDeepCopy. The existing memberDeepCopy case covers borrowed member/param sources.
                // AIML v2: a refcount-model PARENT always deep-frees its string fields at its kind-7
                // decref, so a borrowed (VariableExpr) string stored into one MUST be incref'd —
                // regardless of the v1 memberDeepCopy analysis (which does not always flag it). Else
                // the parent's decref frees a string still owned elsewhere (the do_deal_damage
                // `mst.source_iid = ctx.source_iid` premature-free: mst's decref freed ctx's iid, so
                // the next effect's read saw garbage). OFF: isRefcountModel=false → v1 condition.
                if (finalTypeName == "string"
                    && dynamic_cast<VariableExpr*>(setStmt->value.get())
                    && (memberDeepCopy || srcIsManagedString || ctx.isRefcountModel(sym->typeName))) {
                    llvm::Value* sv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
                    if (sv && sv->getType()->isPointerTy()) {
                        llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
                        llvm::Function* inc = ctx.getFunction("aiml_str_incref", ctx.builder.getVoidTy(), {i8p});
                        ctx.builder.CreateCall(inc, { sv });
                    }
                }
                // spec/61 §7.0: a bare string LITERAL source is a raw .rodata pointer (no refcount
                // header). Stored into a model string field, aiml_ser_free_decoded later decref's it →
                // reads the byte at `-8` → segfault (test_any_deck_draw / on_death_zone_fallback /
                // listptr_inplace_kv). Dup it into a refcounted, decref-safe buffer so the member owns
                // an independent reference (mirrors coerceOrStoreFieldDefault's default-init dup). Only
                // the bare literal needs this: interpolation / call-result temps are already owned
                // refcounted strings that TRANSFER their single ref, and a VariableExpr source is
                // incref'd above. Drops-on only — drops-off never frees the field, so no dup is needed.
                if (ctx.lifetimeDropsEnabled && finalTypeName == "string") {
                    if (auto* lit = dynamic_cast<LiteralExpr*>(setStmt->value.get())) {
                        if (lit->type == "string") {
                            llvm::Value* sv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
                            if (sv && sv->getType()->isPointerTy()) {
                                llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
                                llvm::Function* dupFn = ctx.getFunction("aiml_str_dup", i8p, {i8p});
                                val = ctx.ir_gen->wrap(ctx.builder.CreateCall(dupFn, { sv }, "str_lit_member_dup"));
                            }
                        }
                    }
                }
                // spec/61 §7 (memV3): overwriting an OWNED string field must release the previous
                // value, else the old string leaks on every re-set (probe 09: each iteration
                // overwrote the default "big"/"idx" without freeing it → 2 leaks/iteration). `val`
                // was already dup'd (literal) / incref'd (VariableExpr) / is an owned transfer
                // (interp/call) above, so loading the old value, storing the new, then decref-ing the
                // old is self-assign safe. memV3-gated → OFF/v2 codegen byte-identical.
                // spec/61 §5.4: this includes a VIEW-alias base (`ecur = at(list,i)`): CopyRetain
                // incref'd its string fields at the decl, so the old value is ecur's OWN reference —
                // skipping the decref leaked it once the write-back replaced the slot
                // (9rooms ConnRegistry.touch, tests/lifetime/68).
                if (ctx.memV3 && ctx.lifetimeDropsEnabled && finalTypeName == "string") {
                    llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
                    llvm::Value* tg  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(targetGep));
                    llvm::Value* oldv = ctx.builder.CreateLoad(i8p, tg, "str_field_old");
                    ctx.ir_gen->createStore(val, targetGep);
                    llvm::Function* dc = ctx.getFunction("aiml_str_decref", ctx.builder.getVoidTy(), {i8p});
                    ctx.builder.CreateCall(dc, { oldv });
                    return;
                }
                // spec/61 §5.5/§4.4.5: a value-model struct-VALUE store into a heap-owning field (a
                // producer/owned-move source reaches this loaded-struct path — views/params were
                // deep_copy_replace'd above). RELEASE the field's previous heap before overwriting, else
                // the old value's container/string handles leak per re-assign (the per-poll render-tree
                // leak: `set holder.cur = mock_state_from_match(...)`, fill_hand→MockState). Includes a
                // view-alias base: CopyRetain owns its field refs (like the string case above;
                // tests/lifetime/70). src is an independent producer/move value here.
                {
                    llvm::Value* sv2 = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
                    auto fci = ctx.classes.find(finalTypeName);
                    if (ctx.memV3 && ctx.lifetimeDropsEnabled && sv2 && sv2->getType()->isStructTy()
                        && fci != ctx.classes.end() && fci->second.hasHeapFields && fci->second.serNodesVar) {
                        llvm::Type* p8 = llvm::PointerType::get(ctx.context, 0);
                        llvm::Value* tg2 = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(targetGep));
                        llvm::Function* frf = ctx.getFunction("aiml_ser_free_decoded",
                            ctx.builder.getVoidTy(), {p8, p8, ctx.builder.getInt32Ty()});
                        ctx.builder.CreateCall(frf, { tg2, fci->second.serNodesVar,
                            ctx.builder.getInt32(fci->second.serNodesCount) });
                    }
                }
                ctx.ir_gen->createStore(val, targetGep);
                return;
            }
        }

        // Builtin property setter for handle types (e.g. resp.status, resp.body)
        if (!className.empty() && ctx.classes.count(className)) {
            auto& classInfo = ctx.classes[className];
            if (classInfo.isBuiltin && classInfo.builtinSetters.count(propName)) {
                llvm::Value* baseVal = ctx.builder.CreateLoad(
                    ctx.builder.getInt8Ty()->getPointerTo(),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), "bs_load");
                classInfo.builtinSetters.at(propName)(ctx, baseVal, val);
                return;
            }
        }
        LOG_ERROR(ctx.fmtLoc() + "Undefined property for set: " + targetStr);
    }
}
void Compiler::visitExprStmt(ExprStmt* exprStmt) {
    // spec/61 §5.4: a bare `list.pop` statement discards the popped element — flag it so the pop
    // handler releases the element's owned heap (aiml_list_pop_free) instead of leaking it. Only the
    // DIRECT statement expression sets this (a pop nested as a call argument is consumed, not
    // discarded); the pop handler clears the flag when it consumes it.
    bool popDiscard = false;
    if (auto* call = dynamic_cast<CallExpr*>(exprStmt->expr.get())) {
        const std::string& fn = call->functionName;
        size_t dot = fn.rfind('.');
        if (dot != std::string::npos && fn.substr(dot + 1) == "pop" && call->args.empty())
            popDiscard = true;
    }
    bool savedPop = ctx.popResultDiscarded;
    ctx.popResultDiscarded = popDiscard;
    IRValue* dv = visitExpression(exprStmt->expr.get());
    ctx.popResultDiscarded = savedPop;
    std::string ct = ownedContainerCallType(exprStmt->expr.get());   // discarded owning container ref
    if (dv && !ct.empty()) releaseContainerHandle(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(dv)), ct);
    // spec/61 §9.2: a discarded producer value-model result is owned by the caller → free its heap.
    auto* dc = dynamic_cast<CallExpr*>(exprStmt->expr.get());
    if (dv && dc && ctx.lifetimeDropsEnabled) {
        std::string mt;
        if (auto* fi = resolveCallTarget(dc->functionName)) { if (fi->ownsReturn) mt = fi->returnTypeName; }
        else {
            size_t dp = dc->functionName.rfind('.');
            std::string sn = (dp == std::string::npos) ? dc->functionName : dc->functionName.substr(dp + 1);
            auto it = ctx.ownsReturnFnTypes.find(sn);
            if (ctx.ownsReturnFns.count(sn) && it != ctx.ownsReturnFnTypes.end() && it->second.size() == 1)
                mt = *it->second.begin();
        }
        auto ci = mt.empty() ? ctx.classes.end() : ctx.classes.find(mt);
        llvm::Value* rv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(dv));
        if (ci != ctx.classes.end() && ci->second.hasHeapFields && ci->second.serNodesVar && ci->second.type
            && !ctx.isRefcountModel(mt) && !ctx.classGroupContainers.count(mt) && rv) {
            llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* addr = nullptr;
            if (rv->getType()->isStructTy()) {
                addr = ctx.createEntryAlloca(rv->getType(), "discard_model");
                ctx.builder.CreateStore(rv, addr);
            } else if (rv->getType()->isPointerTy()) {
                addr = rv;   // method path: struct already spilled to a temp
            }
            if (addr) ctx.builder.CreateCall(ctx.getFunction("aiml_ser_free_decoded", ctx.builder.getVoidTy(),
                          {i8p, i8p, ctx.builder.getInt32Ty()}),
                          { addr, ci->second.serNodesVar, ctx.builder.getInt32(ci->second.serNodesCount) });
        }
    }
}
static void emitScopeDrop(CompilerContext& ctx, const CompilerContext::ScopeDrop& it);  // defined below
void Compiler::visitRetStmt(RetStmt* retStmt) {
    // The move below only applies to THIS return path: paths compiled after it (e.g. the rest of a
    // loop body around a conditional `return x`) must keep x's drop → restore the frames afterwards.
    struct CleanupRestore { CompilerContext& c; std::vector<std::vector<CompilerContext::ScopeDrop>> saved;
                            ~CleanupRestore() { c.cleanupStack = std::move(saved); } } cleanupRestore{ctx, ctx.cleanupStack};
    // spec/61 (A3): returning a local moves it to the caller → do not drop it here. This runs BEFORE
    // the value is loaded, so a managed string return uses ERASE mode (nullManagedStringSlot=false):
    // the value is captured afterwards and the returned value's owner is exempted from cleanup.
    if (retStmt->value) consumeLocalDrop(retStmt->value.get(), /*nullManagedStringSlot=*/false);
    llvm::Function* currentFunc = ctx.builder.GetInsertBlock()->getParent();
    llvm::Type* retTy = currentFunc->getReturnType();

    // spec/61 §6 (P2a return-cleanup) — ENABLED, gated by the §4 dataflow. Frees in-scope owned
    // locals before the return terminator (so consumed params / list / map / model locals that
    // never reach normal block exit are not leaked on the return path). Two safety layers:
    //   (1) mayDrop() against the return's exit facts suppresses shared-VIEW slots (fixes the
    //       intra-function case, test_list_list_inner_values);
    //   (2) Increment C deep-copies value-model member-stores of borrowed sources, so an owned
    //       aggregate has no internal shallow sharing to double-free (test_class_member_shared_tensor);
    //   plus the returned value's own owner is exempted (§6.2) so `return x.field` is safe.
    const OwnershipPlan* retPlan = ctx.ownershipPlan;
    const FlowState* retExit = nullptr;
    if (retPlan) {
        auto ee = retPlan->earlyExits.find(retStmt);
        if (ee != retPlan->earlyExits.end()) retExit = &ee->second;
    }
    OwnershipId retBaseId = NoOwnership;
    // AIML v2 (P4): `return <model_local>` is a MOVE — the callee hands its reference to the caller,
    // so the returned model's kind-7 decref must NOT fire on the return path. Record its alloca.
    llvm::Value* retModelAddr = nullptr;
    // spec/61 §5 (memV3): `return <container-local>` returns the HANDLE pointer (share/move to the
    // caller). Capture the local's slot so (a) we return the loaded handle ptr and (b) its handle
    // drop is exempted on the return path (a move — the caller takes the ref).
    llvm::Value* retContainerSlot = nullptr;
    bool retContainerIsParam = false;   // spec/61 §8: a returned container-handle PARAM is a borrow → share (incref)
    if (auto* rv = dynamic_cast<VariableExpr*>(retStmt->value.get())) {
        std::string base = rv->name;
        size_t dot = base.find('.');
        if (dot != std::string::npos) base = base.substr(0, dot);   // `x.field` → exempt owner x
        if (Symbol* bs = resolveVariable(base)) {
            retBaseId = bs->ownershipId;
            if (dot == std::string::npos && ctx.isRefcountModel(bs->typeName) && bs->ir_alloca_inst)
                retModelAddr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(bs->ir_alloca_inst));
            if (dot == std::string::npos && bs->is_byref_arg
                && ctx.isMemV3ContainerHandle(bs->typeName) && bs->ir_alloca_inst) {
                retContainerSlot = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(bs->ir_alloca_inst));
                // borrowed arg / global / persistent local keeps its own ref → share
                retContainerIsParam = bs->is_param || bs->is_global || bs->lifetime == "persistent";
            }
        }
    }
    // Only alias-safe return shapes may run the cleanup; otherwise the returned value's
    // provenance is unclear, so the return path is left leaking (under-drop, safe):
    //   • void / scalar-typed return (cannot alias heap);
    //   • a VariableExpr return whose OWNER is provably Owned at the return (a genuine move-out,
    //     exempted below). Finding #1: a VIEW/borrow variable is NOT Owned here → cleanup is
    //     skipped, so we never free the owner whose heap the returned view aliases.
    bool retScalar = retTy && !retTy->isVoidTy() && !retTy->isPointerTy() && !retTy->isStructTy();
    bool retBaseOwned = false;
    if (retExit && retBaseId != AIML::AST::NoOwnership) {
        auto it = retExit->slots.find(retBaseId);
        if (it != retExit->slots.end() && it->second.origin == AIML::AST::Origin::Owned)
            retBaseOwned = true;
    }
    // spec/61 §6.2: a `return: { call: <producer> }` hands back a FRESH owned value — a str.*/list
    // builtin alloc'd with an rc-1 header, or a user fn in ownsReturnFns returning an owned box. The
    // callee produced it, so it CANNOT alias any of THIS function's locals → dropping them on the
    // return path is safe (and required: else a `list<string>` split-local declared before
    // `return: head.trim` leaks — grai run_replace/match_key_of, the container never freed on any
    // exit). Restricted to KNOWN producers so `return: obj.borrow_view()` (a view into a local) still
    // conservatively skips cleanup.
    bool retFreshOwned = false;
    if (auto* rcx = dynamic_cast<CallExpr*>(retStmt->value.get())) {
        const std::string& fn = rcx->functionName;
        size_t dp = fn.rfind('.');
        std::string shortName = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
        bool builtinFresh = shortName == "substr" || shortName == "lower" || shortName == "upper"
            || shortName == "title" || shortName == "trim" || shortName == "replace"
            || shortName == "split" || shortName == "slice";
        if (builtinFresh || ctx.stringProducerFns.count(shortName)
            || ctx.ownsReturnMatches(shortName, ctx.currentFunctionReturnType))
            retFreshOwned = true;
    }
    // spec/61 §6.2: `return "..${x}.."` — a string INTERPOLATION allocates a FRESH str_concat buffer
    // that cannot alias any of this function's locals (it copies their content), exactly like a
    // producer-call return. Without this, safeToCleanup stays false for an interpolation return and
    // the function's owned-string locals read by the interpolation are never dropped on the return
    // path → leak (hash_password_new salt+derived, leak_probes/round3 §2; any producer returning a
    // composed string). Marking it fresh-owned lets emitReturnCleanup drop those siblings (mayDrop-
    // gated, so a still-shared view is not freed); the returned concat temp is moved out, not dropped.
    if (dynamic_cast<StringInterpolationExpr*>(retStmt->value.get()))
        retFreshOwned = true;
    // spec/61 §4/§8 diagnostic (finding-1 class): a function that returns a FRESH string (an
    // interpolation, a string builtin like substr/trim/…, or a producer-call result) but is NOT
    // declared `producer` hands the caller a newly-owned string that the caller treats as a BORROW
    // (keeps its own copy) → the new string is never freed and leaks per call (cap_first, pluralize,
    // the ability-text chain owner_object_phrase/eff_owner_str/…). Warn so it gets `{ type: string,
    // lifetime: producer }` — the compiler then moves/materializes the result correctly (§4).
    // A BORROW return (retFreshOwned == false, e.g. `return param`) is balanced and is NOT flagged.
    // Suppress a deliberate case with `static_off: ["fresh_string_return"]`.
    if (retFreshOwned && ctx.currentFunctionReturnType == "string" && !ctx.currentFunctionOwnsReturn
        && std::find(ctx.currentFunctionStaticOff.begin(), ctx.currentFunctionStaticOff.end(),
                     "fresh_string_return") == ctx.currentFunctionStaticOff.end()) {
        // Show the source name with a dot: method names are stored as `Class_method`
        // (ast_compiler.cpp: `cls->name + "_" + method`), so split on the KNOWN class name
        // (unambiguous — class names may themselves contain '_'). Free functions stay as-is.
        std::string dispName = ctx.currentFunctionName;
        if (!ctx.currentClassName.empty()) {
            const std::string pfx = ctx.currentClassName + "_";
            if (dispName.rfind(pfx, 0) == 0)
                dispName = ctx.currentClassName + "." + dispName.substr(pfx.size());
        }
        LOG_WARNING(ctx.fmtLoc() + "fresh string return without `lifetime: producer`: '" +
            dispName + "' creates a new string and returns it. Without `producer` the "
            "caller assumes the string belongs to someone else and keeps its own copy, so the new "
            "string is never freed and leaks on every call (spec/61 §4/§8). Declare the return "
            "`{ type: string, lifetime: producer }` so the caller takes ownership. If the string is "
            "deliberately borrowed (returned from somewhere else, not newly created), suppress this "
            "with static_off [\"fresh_string_return\"].");
    }
    // spec/61 §6.2 (T1): `return <owned value-model LOCAL>.<string field>` hands back a string that
    // ALIASES the local's heap. The generic owner-exemption in emitReturnCleanup (below) would skip
    // the WHOLE local's drop to protect that returned string → leaking its SIBLING heap fields on
    // every call (auth_service pid_for leaked User.email/password_hash/display_name/salt/roles on
    // EVERY authenticated request — ~1KB/request). Detect this shape so we can instead INCREF the
    // returned string (it then survives the owner's deep-free) and drop the owner FULLY (freeing the
    // siblings). Net refcount of the returned string is unchanged (incref +1, deep-free decref −1).
    // String-only (incref = aiml_str_incref, which is null/rodata/immortal-safe); value models only
    // (a refcount ptr<>/shared<> local is a kind-7 decref, order-independent, never exempted here).
    // Gate on `lifetime: producer` (currentFunctionOwnsReturn): only then does the caller ADOPT
    // the returned string (no incref), so incref(+1)+owner-deep-free(−1)+caller-drop(−1) balances
    // to 0. A non-producer string return is a BORROW (caller increfs) — returning a field of a
    // to-be-freed local is an inherent dangling borrow there; the user must mark it producer.
    bool retStrFieldOfOwnedModel = false;
    if (ctx.currentFunctionOwnsReturn && retBaseOwned && retModelAddr == nullptr
        && ctx.currentFunctionReturnType == "string") {
        if (auto* rv = dynamic_cast<VariableExpr*>(retStmt->value.get())) {
            size_t dot = rv->name.find('.');
            if (dot != std::string::npos && rv->name.find('<') == std::string::npos) {
                std::string mbase = rv->name.substr(0, dot);
                if (Symbol* mbs = resolveVariable(mbase)) {
                    // an owned ptr<T> box local (kind-4 box drop) is handled like an owned value model
                    std::string mtn = mbs->typeName;
                    bool boxBase = mtn.rfind("ptr<", 0) == 0 && mtn.back() == '>';
                    if (boxBase) mtn = mtn.substr(4, mtn.size() - 5);
                    auto ci = ctx.classes.find(mtn);
                    if (!mbs->is_byref_arg && ci != ctx.classes.end()
                        && (ci->second.isModel || boxBase) && ci->second.hasHeapFields && !ci->second.isBuiltin
                        && !ctx.classGroupContainers.count(mtn) && !ctx.cascadeClasses.count(mtn)
                        && !ctx.isRefcountModel(mtn))
                        retStrFieldOfOwnedModel = true;
                }
            }
        }
    }
    // spec/61 §4/§8 (producer materialization): a `lifetime: producer` STRING function that returns a
    // BORROWED value — a string param, or a string field of a borrowed base (retBaseOwned == false),
    // and not already fresh (retFreshOwned) nor the owned-model-field incref case
    // (retStrFieldOfOwnedModel) — must MATERIALIZE the producer's freshness guarantee: dup the
    // returned string so the caller adopts an INDEPENDENT owned buffer. Without it the result shares
    // the borrowed buffer and the caller's adopt-then-drop double-frees / reads freed memory (the
    // `return: eff` / `return: cnt` UAF: assemble_ability, pluralize — leak_probes round8 §1/§2).
    // Only under producer (the caller ADOPTS the result); a non-producer string return stays a borrow
    // the caller increfs. This honors an EXPLICIT annotation — NOT a general return auto-classifier.
    // A container accessor (`at`, `.get/.at/.first/.last`) yields the container's own element — a
    // borrow even though it is a call (spec/61 §8: a producer never returns the callee's live handle).
    bool retAccessorBorrow = dynamic_cast<AtExpr*>(retStmt->value.get()) != nullptr;
    if (auto* rc = dynamic_cast<CallExpr*>(retStmt->value.get())) {
        const std::string& fn = rc->functionName;
        size_t dp = fn.rfind('.');
        std::string sn = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
        retAccessorBorrow = dp != std::string::npos
            && (sn == "get" || sn == "at" || sn == "first" || sn == "last")
            && !ctx.ownsReturnMatches(sn, "string");
    }
    bool retProducerBorrowedString =
        ctx.currentFunctionOwnsReturn && ctx.currentFunctionReturnType == "string"
        && !retStrFieldOfOwnedModel
        && ((!retFreshOwned && !retBaseOwned && dynamic_cast<VariableExpr*>(retStmt->value.get()) != nullptr)
            || retAccessorBorrow);
    // AIML v2 (P4): returning a refcount model is alias-safe to clean up — the returned object is
    // exempted by retModelAddr (a move), and every other local's kind-7 decref is order-independent.
    bool safeToCleanup = (retTy && retTy->isVoidTy()) || retScalar || retBaseOwned
                         || (retModelAddr != nullptr) || (retContainerSlot != nullptr)
                         || retFreshOwned || retStrFieldOfOwnedModel || retProducerBorrowedString
                         // a literal (static string / number) cannot alias any local heap
                         || dynamic_cast<LiteralExpr*>(retStmt->value.get()) != nullptr;
    // spec/61 §6.2 (T1): the returned value only aliases the owner's heap when the return TYPE is a
    // pointer/struct (`return x.field` handing out x's sub-heap). For a SCALAR/void return the value
    // is a copy that cannot alias — e.g. `return str_local.len` yields an i32 — so the owner must NOT
    // be exempted from cleanup, else it leaks (the deckset-editor `build_a`/`iso3` case: str_local
    // never dropped). Narrowing the exemption to alias-capable returns only ADDS drops of
    // already-owned locals (mayDrop-gated) → cannot double-free.
    const bool returnMayAliasHeap = retTy && (retTy->isPointerTy() || retTy->isStructTy());
    auto emitReturnCleanup = [&]() {
        if (!ctx.lifetimeDropsEnabled) return;
        auto* bb = static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
        if (!bb || bb->getTerminator()) return;
        for (auto fit = ctx.cleanupStack.rbegin(); fit != ctx.cleanupStack.rend(); ++fit)
            for (auto it = fit->rbegin(); it != fit->rend(); ++it) {
                if (returnMayAliasHeap && it->ownershipId != NoOwnership && it->ownershipId == retBaseId
                    && !retStrFieldOfOwnedModel)
                    continue;   // owner of an alias-capable returned value → exempt (would double-free).
                    // Exception (§6.2 T1): for `return owned-model-local.string-field` the field was
                    // increfed before this cleanup, so the owner is dropped FULLY here (frees siblings;
                    // the incref keeps the returned string alive across the deep-free).
                // AIML v2: a refcount MODEL decref (kind 7) is order-independent and always safe
                // (freed only at 0), so it is NOT mayDrop-gated — emit on every return path unless
                // this model is the value being returned (the retBaseId exemption above = a move).
                if (it->kind == 7) {
                    if (retModelAddr && it->addr == retModelAddr) continue;  // moved-out: caller takes the ref
                    emitScopeDrop(ctx, *it);
                    continue;
                }
                if (it->kind == 10 || it->kind == 11) { emitScopeDrop(ctx, *it); continue; }   // null-safe single release
                // spec/61 §5 (memV3): the returned container handle is MOVED to the caller → skip its
                // decref/free on the return path (kind 2 list / 3 map / 8 buffer, isListHandle).
                if (retContainerSlot && it->addr == retContainerSlot
                    && (it->kind == 2 || it->kind == 3 || it->kind == 8))
                    continue;
                // spec/61 §5 (memV3): a refcounted container HANDLE decref (kind 2 list / 3 map /
                // 8 buffer, isListHandle) is order-independent and safe at any refcount (frees only
                // at rc 0), exactly like the model decref above — so emit it on EVERY return path,
                // not mayDrop-gated. The v1 ownership plan doesn't always track a handle local (it
                // predates Option B), which wrongly suppressed the drop and leaked the container
                // (probe 17: map local released on block-exit but NOT on the return path). The
                // returned handle was already exempted just above. isListHandle is memV3-only, so
                // OFF/v2 (no handle drops) are byte-identical.
                if (it->isListHandle && (it->kind == 2 || it->kind == 3 || it->kind == 8)) {
                    emitScopeDrop(ctx, *it);
                    continue;
                }
                if (!retPlan || !retExit) continue;   // v1 drops need the ownership plan
                if (mayDrop(*retPlan, *retExit, it->ownershipId))
                    emitScopeDrop(ctx, *it);
            }
    };

    // Decorator wrapping — right-to-left, one hook per built-in.  Currently
    // only `vocab` is recognised (wraps string returns with
    // aiml_vocab_lookup).  Unknown decorator names are silently ignored so
    // adding new ones later doesn't break older functions.
    auto apply_decorators = [&](llvm::Value* raw) -> llvm::Value* {
        if (!raw) return raw;
        if (ctx.currentFunctionDecorators.empty()) return raw;
        for (auto it  = ctx.currentFunctionDecorators.rbegin();
             it != ctx.currentFunctionDecorators.rend(); ++it) {
            const std::string& dname = *it;
            if (dname == "vocab") {
                // Only wrap when the value is a string (i8*) — non-string
                // returns pass through unchanged so mixed-return functions
                // don't hit a signature mismatch.
                if (!raw->getType()->isPointerTy() ||
                    ctx.currentFunctionReturnType != "string") continue;
                auto* p8 = llvm::PointerType::get(ctx.context, 0);
                llvm::Function* fn = ctx.module.getFunction("aiml_vocab_lookup");
                if (!fn) fn = llvm::Function::Create(
                    llvm::FunctionType::get(p8, {p8}, false),
                    llvm::Function::ExternalLinkage, "aiml_vocab_lookup",
                    ctx.module);
                raw = ctx.builder.CreateCall(fn, {raw}, "vocab_wrapped");
            }
        }
        return raw;
    };

    // Void function: always emit ret void, evaluate expression only for side effects
    if (retTy->isVoidTy()) {
        if (retStmt->value) visitExpression(retStmt->value.get());
        emitReturnCleanup();   // §6/P2a: void → return value cannot alias a dropped owner
        ctx.ir_gen->createRet(nullptr);
    } else if (retContainerSlot) {
        // spec/61 §5 (memV3): return the container HANDLE pointer. Load it from the slot, then ret.
        // An OWNED local is MOVED (its drop is exempted by cleanup). A borrowed PARAM is SHARED — the
        // caller still owns the argument, so incref before returning, else the returned handle and the
        // caller's own reference both free the rc-1 handle → double-free (the cascade `carrier:
        // list<T>` render `return carry`, test_ability_text/trig_text).
        auto* i8p = llvm::PointerType::get(ctx.context, 0);
        llvm::Value* handle = ctx.builder.CreateLoad(i8p, retContainerSlot, "ret_conh");
        if (retContainerIsParam) {
            const std::string& rt = ctx.currentFunctionReturnType;
            const char* incF = (rt.rfind("map<", 0) == 0) ? "aiml_map_handle_incref"
                : (rt == "buffer" || rt.rfind("buffer<", 0) == 0) ? "aiml_buffer_handle_incref"
                : "aiml_list_handle_incref";
            ctx.builder.CreateCall(ctx.getFunction(incF, ctx.builder.getVoidTy(), {i8p}), { handle });
        }
        emitReturnCleanup();
        ctx.ir_gen->createRet(ctx.ir_gen->wrap(handle));
    } else if (retStmt->value) {
        IRValue* val = visitExpression(retStmt->value.get());
        // An owned statement temp (e.g. a string `at` that already retained the element) is moved
        // out as-is — dup'ing it again would orphan that reference.
        bool retWasOwnedTemp = val && inStringTempStack(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)));
        // spec/61: a returned string temp is moved to the caller → not a statement temp.
        if (val) consumeStringTemp(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)));
        // spec/61 §4/§8 (producer materialization): dup a BORROWED string returned by a `producer`
        // function so the caller adopts an INDEPENDENT owned buffer (see retProducerBorrowedString).
        // Runs BEFORE emitReturnCleanup so the fresh dup cannot alias any dropped local; the borrowed
        // source (a param) stays owned by the caller. aiml_str_dup is null/immortal-safe.
        if (retProducerBorrowedString && val && !retWasOwnedTemp) {
            llvm::Value* rs = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            if (rs && rs->getType()->isPointerTy()) {
                auto* i8pD = llvm::PointerType::get(ctx.context, 0);
                llvm::Value* dup = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_str_dup", i8pD, {i8pD}), { rs }, "producer_ret_dup");
                val = ctx.ir_gen->wrap(dup);
            }
        }
        // spec/61 §5.7/§8: returning a container FIELD/member (`return obj.field`, `return xref.field`)
        // or an at-element is a SHARE — the caller receives an owning reference (with or without
        // producer) while the source still owns it → incref the returned handle, else the caller's
        // binding and the source both free the rc-1 handle (double-free; df7). A returned owned-LOCAL
        // (retContainerSlot, move — drop consumed) and a call-result (already an owned rc) are NOT incref'd.
        if (ctx.memV3 && val && !retContainerSlot
            && ctx.isMemV3ContainerHandle(ctx.currentFunctionReturnType)) {
            bool shareRet = false;
            if (auto* rv = dynamic_cast<VariableExpr*>(retStmt->value.get()))
                shareRet = (rv->name.find('.') != std::string::npos || rv->name.find('<') != std::string::npos);
            else if (dynamic_cast<AtExpr*>(retStmt->value.get())) shareRet = true;
            if (shareRet) {
                llvm::Value* h = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
                if (h) {
                    const std::string& rt = ctx.currentFunctionReturnType;
                    const char* incF = (rt.rfind("map<", 0) == 0) ? "aiml_map_handle_incref"
                        : (rt == "buffer" || rt.rfind("buffer<", 0) == 0) ? "aiml_buffer_handle_incref"
                        : "aiml_list_handle_incref";
                    auto* i8p = llvm::PointerType::get(ctx.context, 0);
                    ctx.builder.CreateCall(ctx.getFunction(incF, ctx.builder.getVoidTy(), {i8p}), { h });
                }
            }
        }
        // spec/61 (T1b): a `return "${…}"` builds interpolation INTERMEDIATE temps (int→string
        // conversions, sub-concat accumulators) into this statement's temp frame. The returned final
        // value was just removed via consumeStringTemp; the remaining intermediates would normally be
        // decref'd by the per-statement temp loop in visitStatement — but the `createRet` below
        // TERMINATES the block, so that loop is skipped (it guards on !getTerminator()). Decref them
        // here, before the terminator, so they are not leaked (the `return <local>` form avoids this
        // because its interpolation ran in the non-terminating decl statement).
        if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty()) {
            llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
            for (llvm::Value* t : ctx.stmtTempStack.back()) {
                llvm::Function* dc = ctx.getFunction("aiml_str_decref", ctx.builder.getVoidTy(), {i8p});
                ctx.builder.CreateCall(dc, { t });
            }
            ctx.stmtTempStack.back().clear();
        }
        // spec/61 §6.2 (T1): incref the returned STRING FIELD of an owned value-model local BEFORE
        // the cleanup so the owner's deep-free (now un-exempted) does not release it — net refcount
        // unchanged, sibling heap fields freed. aiml_str_incref is null/rodata/immortal-safe.
        if (retStrFieldOfOwnedModel && val) {
            llvm::Value* rs = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            if (rs) {
                auto* i8pS = llvm::PointerType::get(ctx.context, 0);
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_str_incref", ctx.builder.getVoidTy(), {i8pS}), { rs });
            }
        }
        // §6/P2a: cleanup after the value is in an SSA register / its owner is exempted, only
        // for alias-safe return shapes; the value the branches below build then stays valid.
        if (safeToCleanup) emitReturnCleanup();
        if (val) {
            // Struct-by-value return (list/buffer/map): visitExpression often
            // yields an alloca-pointer to the struct (e.g. from `str.split` or
            // any builtin that fills a stack-alloc'd list).  ret needs the
            // loaded struct, not the pointer.
            llvm::Value* raw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
            if (retTy->isStructTy() && raw && raw->getType()->isPointerTy()) {
                // `return: null` for a struct / fat-pointer return type: do NOT
                // load the struct from the null address (→ SIGSEGV).  A null
                // struct return is a zero-initialized struct (e.g. a null
                // interface fat pointer {obj=null, vtable=null}).
                if (llvm::isa<llvm::ConstantPointerNull>(raw)) {
                    ctx.ir_gen->createRet(ctx.ir_gen->wrap(
                        llvm::ConstantAggregateZero::get(retTy)));
                } else {
                    // `return: kv.get_copy(k)` — the get_copy result is a pointer
                    // into the kv decoded_cache (shallow view).  A plain struct
                    // load would return a struct whose string/list/map heap
                    // pointers ALIAS the cache slot; a later set/get_copy/evict on
                    // the same key frees them → use-after-free in the CALLER
                    // (e.g. get_raw → amat → nested play_card.set → dangling).
                    // Deep-copy into an independent temp first, exactly like the
                    // decl-site path.  See doc/issue/kv_get_mutate_set_same_key_crash.md.
                    auto ciRet = ctx.classes.find(ctx.currentFunctionReturnType);
                    bool retDeepCopy = ctx.kvGetCopy
                                       && ciRet != ctx.classes.end()
                                       && ciRet->second.hasHeapFields
                                       && ciRet->second.serNodesVar;
                    if (retDeepCopy) {
                        auto* ptrTy = llvm::PointerType::get(ctx.context, 0);
                        llvm::Value* tmp = ctx.builder.CreateAlloca(retTy, nullptr, "retcopy");
                        ctx.builder.CreateStore(llvm::ConstantAggregateZero::get(retTy), tmp);
                        llvm::Function* fn = ctx.getFunction("aiml_ser_deep_copy",
                            ctx.builder.getVoidTy(),
                            {ptrTy, ptrTy, ptrTy, ctx.builder.getInt32Ty()});
                        ctx.builder.CreateCall(fn, {
                            tmp, raw, ciRet->second.serNodesVar,
                            ctx.builder.getInt32(ciRet->second.serNodesCount)
                        });
                        llvm::Value* loaded = ctx.builder.CreateLoad(retTy, tmp, "ret_struct_load");
                        ctx.ir_gen->createRet(ctx.ir_gen->wrap(loaded));
                        ctx.kvGetCopy = false;
                    } else {
                    // Interface upcast on return: `return: obj` where obj is a
                    // ptr<ConcreteClass> but the function returns ptr<Interface>
                    // (fat pointer AimlFatPtr_<Iface>).  Loading a fat-pointer
                    // struct from the object's own memory yields garbage — build
                    // {obj, vtable} instead (same as the decl-site upcast).
                    llvm::Value* fat = nullptr;
                    auto* st = llvm::cast<llvm::StructType>(retTy);
                    if (st->hasName() && st->getName().startswith("AimlFatPtr_")) {
                        std::string srcType;
                        if (auto* varExpr =
                                dynamic_cast<VariableExpr*>(retStmt->value.get())) {
                            if (Symbol* srcSym = resolveVariable(varExpr->name))
                                srcType = srcSym->typeName;
                        }
                        // Upcast only from a concrete-class ptr; a source that is
                        // already the interface fat pointer falls through to load.
                        if (srcType.rfind("ptr<", 0) == 0 && srcType.back() == '>') {
                            std::string inner =
                                srcType.substr(4, srcType.size() - 5);
                            if (ctx.classes.count(inner))
                                fat = buildInterfaceUpcast(raw, srcType, st);
                        }
                    }
                    if (fat) {
                        ctx.ir_gen->createRet(ctx.ir_gen->wrap(fat));
                    } else {
                        // ── spec/61 §6.3: producer returning a value-model VIEW → DEEP COPY ──
                        // A value-model is a value type. When the returned value is a VIEW that shares
                        // a container element's inline list/map heap — `return at(zone,i)`,
                        // `return e.field`, an accessor `return zone.get(i)`, or a plain local that
                        // was filled from such a view (freshOwned=false → not Owned here) — a plain
                        // struct load hands the caller a SHALLOW copy. The caller then owns that copy
                        // and its scope-drop double-frees the element's buffer with the container's own
                        // drop (9rooms load_entry_at → att_entry → aent, freed in drive_ai_combat).
                        // Deep-copy into an independent temp so the caller owns its own heap. An OWNED
                        // producer temp / owned local (retBaseOwned, or an owns-return call) is a real
                        // move and is NOT deep-copied (that would leak the temp).
                        bool retIsView = false;
                        auto ciRetV = ctx.classes.find(ctx.currentFunctionReturnType);
                        if (ctx.lifetimeDropsEnabled && ciRetV != ctx.classes.end()
                            && ciRetV->second.hasHeapFields && ciRetV->second.serNodesVar) {
                            Expr* rv = retStmt->value.get();
                            if (dynamic_cast<AtExpr*>(rv)) {
                                retIsView = true;                       // container[index]
                            } else if (auto* ve = dynamic_cast<VariableExpr*>(rv)) {
                                // member/field access (obj.field / <.field) shares the aggregate sub-heap.
                                // A plain local is a view only if it was flagged is_view_alias at its decl
                                // (decl-from-view, kept cheap = uncopied until it escapes HERE). This is
                                // precise — no reliance on flow facts — so owned built locals (MatchResp
                                // `out` etc.) are never falsely deep-copied+leaked.
                                if (ve->name.find('.') != std::string::npos || ve->name.find('<') != std::string::npos) {
                                    retIsView = true;
                                } else if (Symbol* rsym = resolveVariable(ve->name)) {
                                    // a parameter, global or persistent local stays owned elsewhere → copy out
                                    if (rsym->is_view_alias || rsym->is_param || rsym->is_global
                                        || rsym->lifetime == "persistent")
                                        retIsView = true;
                                }
                            } else if (auto* ce = dynamic_cast<CallExpr*>(rv)) {
                                const std::string& fn = ce->functionName;
                                size_t dp = fn.rfind('.');
                                std::string sn = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                                if ((sn == "at" || sn == "get" || sn == "first" || sn == "last")
                                    && !ctx.ownsReturnMatches(sn, ctx.currentFunctionReturnType))
                                    retIsView = true;                   // container accessor (not a producer)
                            }
                        }
                        if (retIsView) {
                            auto* ptrTy = llvm::PointerType::get(ctx.context, 0);
                            llvm::Value* tmp = ctx.builder.CreateAlloca(retTy, nullptr, "retviewcopy");
                            ctx.builder.CreateStore(llvm::ConstantAggregateZero::get(retTy), tmp);
                            llvm::Function* fn = ctx.getFunction("aiml_ser_deep_copy",
                                ctx.builder.getVoidTy(), {ptrTy, ptrTy, ptrTy, ctx.builder.getInt32Ty()});
                            ctx.builder.CreateCall(fn, { tmp, raw, ciRetV->second.serNodesVar,
                                                         ctx.builder.getInt32(ciRetV->second.serNodesCount) });
                            llvm::Value* loaded = ctx.builder.CreateLoad(retTy, tmp, "ret_view_load");
                            ctx.ir_gen->createRet(ctx.ir_gen->wrap(loaded));
                        } else {
                            llvm::Value* loaded = ctx.builder.CreateLoad(retTy, raw, "ret_struct_load");
                            ctx.ir_gen->createRet(ctx.ir_gen->wrap(loaded));
                        }
                    }
                    }
                }
            } else {
                // AIML v2: a refcount-MODEL pointer return must hand the caller an OWNED reference.
                // A returned OWNED/shared LOCAL is a MOVE — its scope-drop is exempted (retModelAddr)
                // and its existing ref transfers, so no incref. But a returned BORROW — a parameter
                // (is_param: no owned ref, no scope-drop to move) or a view (`obj.field` / `at(...)` /
                // a non-owns-return accessor) — aliases someone else's object, so the caller would get
                // a ref it doesn't own and its decref-old would free the still-live object (the
                // CostDraft cascade `return carry`: builder returns its borrowed param, the caller's
                // `set:[carry, b_x(carry)]` decref'd the aliased old value → free → UAF, caught by
                // AIML_MODEL_TRACE). Incref the borrow so the returned reference is genuinely owned.
                if (ctx.lifetimeDropsEnabled && retTy->isPointerTy()
                    && ctx.isRefcountModel(ctx.currentFunctionReturnType) && raw) {
                    bool retIsBorrow = false;
                    Expr* rv = retStmt->value.get();
                    if (dynamic_cast<AtExpr*>(rv)) {
                        retIsBorrow = true;
                    } else if (auto* ve = dynamic_cast<VariableExpr*>(rv)) {
                        if (ve->name.find('.') != std::string::npos || ve->name.find('<') != std::string::npos)
                            retIsBorrow = true;                       // member/field view
                        else if (Symbol* rsym = resolveVariable(ve->name))
                            retIsBorrow = rsym->is_param;             // borrowed parameter (no owned ref)
                    } else if (auto* ce = dynamic_cast<CallExpr*>(rv)) {
                        const std::string& fn = ce->functionName; size_t dp = fn.rfind('.');
                        std::string sn = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                        if ((sn == "at" || sn == "get" || sn == "first" || sn == "last")
                            && !ctx.ownsReturnMatches(sn, ctx.currentFunctionReturnType))
                            retIsBorrow = true;                       // container accessor (shared view)
                    }
                    if (retIsBorrow) {
                        auto* i8p = llvm::PointerType::get(ctx.context, 0);
                        llvm::Value* incObj = (raw->getType() == i8p) ? raw
                                              : ctx.builder.CreateBitCast(raw, i8p, "ret_incref_cast");
                        llvm::Function* inc = ctx.getFunction("aiml_model_incref", ctx.builder.getVoidTy(), {i8p});
                        ctx.builder.CreateCall(inc, { incObj });
                    }
                }
                // spec/61 §8: a by-value parameter / global / persistent value model is still owned
                // elsewhere → hand the caller an independent deep copy (it drops what it receives).
                if (ctx.lifetimeDropsEnabled && raw && raw->getType()->isStructTy()
                    && ctx.isOwnedValueReturn(ctx.currentFunctionReturnType)) {
                    auto ciV = ctx.classes.find(ctx.currentFunctionReturnType);
                    bool borrowedSrc = false;
                    if (auto* ve = dynamic_cast<VariableExpr*>(retStmt->value.get()))
                        if (ve->name.find('.') == std::string::npos && ve->name.find('<') == std::string::npos)
                            if (Symbol* rsym = resolveVariable(ve->name))
                                borrowedSrc = rsym->is_param || rsym->is_global || rsym->lifetime == "persistent";
                    if (borrowedSrc && ciV->second.serNodesVar) {
                        auto* ptrTy = llvm::PointerType::get(ctx.context, 0);
                        llvm::Value* src = ctx.createEntryAlloca(raw->getType(), "ret_param_src");
                        ctx.builder.CreateStore(raw, src);
                        llvm::Value* tmp = ctx.createEntryAlloca(raw->getType(), "ret_param_copy");
                        ctx.builder.CreateStore(llvm::ConstantAggregateZero::get(raw->getType()), tmp);
                        ctx.builder.CreateCall(ctx.getFunction("aiml_ser_deep_copy", ctx.builder.getVoidTy(),
                            {ptrTy, ptrTy, ptrTy, ctx.builder.getInt32Ty()}),
                            { tmp, src, ciV->second.serNodesVar, ctx.builder.getInt32(ciV->second.serNodesCount) });
                        raw = ctx.builder.CreateLoad(raw->getType(), tmp, "ret_param_val");
                    }
                }
                raw = apply_decorators(raw);
                ctx.ir_gen->createRet(ctx.ir_gen->wrap(raw));
            }
        } else {
             LOG_ERROR(ctx.fmtLoc() + "Failed to evaluate return expression");
             ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::Constant::getNullValue(retTy)));
        }
    } else {
        LOG_ERROR(ctx.fmtLoc() + "Empty return statement in non-void function!");
        ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::Constant::getNullValue(retTy)));
    }
    // Move to a dead block so code emitted after return doesn't corrupt the ret block.
    llvm::BasicBlock* deadBB = llvm::BasicBlock::Create(ctx.context, "ret_dead", currentFunc);
    ctx.builder.SetInsertPoint(deadBB);
}
// spec/61 §7: a plain local string whose slot is codegen-managed ("null | one owned RC ref").
static bool isManagedStringLocalSym(const Symbol& s) {
    return kManagedStringLocals
        && s.typeName == "string" && !s.is_byref_arg && !s.is_global && !s.is_param
        && s.lifetime != "persistent";
}

void Compiler::consumeLocalDrop(Expr* argExpr, bool nullManagedStringSlot) {
    // spec/61 (A3): a plain local passed to push/return/store is MOVED into the target.
    // Remove its pending scope-drop so it is not freed at scope exit (the container /
    // caller / member now owns its heap via the shallow transfer).  Only plain locals —
    // members (`<.x`) and non-variable expressions are not owned-locals here.
    auto* var = dynamic_cast<VariableExpr*>(argExpr);
    if (!var) return;
    const std::string& nm = var->name;
    if (nm.find('.') != std::string::npos || nm.find('<') != std::string::npos) return;
    auto it = ctx.symbol_table.find(nm);
    if (it == ctx.symbol_table.end() || !it->second.ir_alloca_inst) return;
    llvm::Value* addr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(it->second.ir_alloca_inst));
    if (!addr) return;
    // spec/61 §7: a committed move of a MANAGED string local hands its owned reference to the sink.
    // Instead of ERASING the drop (which cannot express a branch-divergent move), NULL the slot at
    // runtime and KEEP the drop (null-safe): the moved-out slot's later scope-drop / decref-old is a
    // null no-op, and paths that did not move still own + free their value. Callers must invoke this
    // AFTER the moved value is captured for the sink (true for return/push/put/consuming-arg; the
    // pre-value-load member/container set-store skips this for managed strings and copies instead).
    if (ctx.lifetimeDropsEnabled && nullManagedStringSlot && isManagedStringLocalSym(it->second)) {
        if (aimlTraceSymbol(nm))
            emitSymbolValueTrace(ctx, "MOVE", nm, 1, it->second.typeName, addr);
        // spec/61 §7: the sink (push/put/consuming-arg) captured the ORIGINAL char* just above. AIML
        // code frequently READS a string local again after handing it to a container (event-log lines,
        // mana-color keys — the old shallow-share semantics), so we must NOT null the slot. Give this
        // local its OWN independent copy (dup): the sink owns the original, the local owns the copy →
        // read-after-move stays valid, and the local's scope-drop / a later decref-old frees a DISTINCT
        // buffer (no double-free, incl. reassign-after-move). The copy is freed at scope exit.
        llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
        llvm::Value* cur  = ctx.builder.CreateLoad(i8p, addr, "managed_move_cur");
        llvm::Function* dup = ctx.getFunction("aiml_str_dup", i8p, {i8p});
        llvm::Value* copy = ctx.builder.CreateCall(dup, { cur }, "managed_move_copy");
        ctx.builder.CreateStore(copy, addr);
        return;
    }
    // (nullManagedStringSlot == false: fall through to the legacy compile-time drop-erase — used at
    // the pre-value-load return site, where erase is safe because the returned value is captured
    // afterwards and its owner is exempted from the return-cleanup.)
    for (auto& frame : ctx.cleanupStack) {
        for (size_t i = 0; i < frame.size(); ) {
            if (frame[i].addr == addr) {
                if (aimlTraceSymbol(nm))
                    emitSymbolValueTrace(ctx, "MOVE", nm, frame[i].kind, frame[i].debugType, addr);
                frame.erase(frame.begin() + (long)i);
            }
            else ++i;
        }
    }
}
// spec/61: emit ONE scope-drop (model / string / list / map) at the current insert point.
// Shared by normal block exit (visitBlockStmt) and early-exit cleanup (visitRetStmt).
// AIML_TRACE_SYMBOL — is this source symbol name selected for lifetime-value tracing?
// Unset → off (zero overhead). "1"/"all" → every symbol. Else a comma-list of names.
static bool aimlTraceSymbol(const std::string& name) {
    const char* e = getenv("AIML_TRACE_SYMBOL");
    if (!e || !*e || name.empty()) return false;
    std::string s = e;
    if (s == "1" || s == "all" || s == "ALL") return true;
    size_t p = 0;
    while (p <= s.size()) {
        size_t c = s.find(',', p);
        std::string tok = s.substr(p, c == std::string::npos ? std::string::npos : c - p);
        size_t a = tok.find_first_not_of(" \t"), b = tok.find_last_not_of(" \t");
        if (a != std::string::npos) tok = tok.substr(a, b - a + 1); else tok.clear();
        if (tok == name) return true;
        if (c == std::string::npos) break;
        p = c + 1;
    }
    return false;
}

// AIML_TRACE_SYMBOL — emit a runtime printf of a symbol's VALUE at a lifetime phase (DECL/SET/
// DROP). string → its content; list/map → element count + address; model → address. Lets you
// watch a symbol's value from birth until it is dropped/cleaned up (find poison / premature free).
static void emitSymbolValueTrace(CompilerContext& ctx, const char* phase, const std::string& name,
                                 int kind, const std::string& typeName, llvm::Value* addr) {
    if (!addr) return;
    llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
    llvm::FunctionType* pfTy = llvm::FunctionType::get(ctx.builder.getInt32Ty(), {i8p}, /*varArg=*/true);
    llvm::FunctionCallee pf = ctx.module.getOrInsertFunction("printf", pfTy);
    std::string site = ctx.currentFunctionName + " | " + ctx.fmtLoc();
    auto declaration = ctx.traceDeclarationSites.find(addr);
    if (declaration != ctx.traceDeclarationSites.end()) site += "decl=" + declaration->second;
    if (std::string(phase) == "DECL") {
        bool registered = false;
        for (const auto& frame : ctx.cleanupStack)
            for (const auto& drop : frame) if (drop.addr == addr) registered = true;
        site += registered ? " cleanup=registered" : " cleanup=unregistered";
    }
    // Keep the original phase/name prefix parseable; append context to the type label.
    std::string tracedType = (typeName.empty() ? "?" : typeName) + " | " + site;
    llvm::Value* phaseS = ctx.builder.CreateGlobalStringPtr(phase);
    llvm::Value* nameS  = ctx.builder.CreateGlobalStringPtr(name);
    if (kind == 1) {  // string local: print the char* content (shows poison/garbage directly)
        llvm::Value* sptr = ctx.builder.CreateLoad(i8p, addr, "trace_str");
        llvm::Value* safe = ctx.builder.CreateSelect(ctx.builder.CreateICmpEQ(sptr,
            llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p))),
            ctx.builder.CreateGlobalStringPtr("<null>"), sptr);
        llvm::Value* fmt = ctx.builder.CreateGlobalStringPtr("[aiml-trace] %s %s (%s) slot@%p data@%p = \"%.256s\"\n");
        ctx.builder.CreateCall(pf, { fmt, phaseS, nameS,
            ctx.builder.CreateGlobalStringPtr(tracedType), addr, sptr, safe });
    } else if (kind == 2) {  // list: element count (struct field 0) + address
        llvm::Value* sz = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), addr, "trace_len");
        llvm::Value* tyS = ctx.builder.CreateGlobalStringPtr(tracedType);
        auto* listTy = ListImpl::getListStructType(ctx);
        llvm::Value* dataSlot = ctx.builder.CreateStructGEP(listTy, addr, 2);
        llvm::Value* data = ctx.builder.CreateLoad(i8p, dataSlot);
        llvm::Value* fmt = ctx.builder.CreateGlobalStringPtr("[aiml-trace] %s %s (%s) len=%d slot@%p data@%p\n");
        ctx.builder.CreateCall(pf, { fmt, phaseS, nameS, tyS, sz, addr, data });
    } else if (kind == 4) {  // owned new-box: the alloca holds a heap pointer → load + print it
        llvm::Value* bptr = ctx.builder.CreateLoad(i8p, addr, "trace_box");
        llvm::Value* tyS = ctx.builder.CreateGlobalStringPtr(tracedType);
        llvm::Value* fmt = ctx.builder.CreateGlobalStringPtr("[aiml-trace] %s %s (%s) box@%p\n");
        ctx.builder.CreateCall(pf, { fmt, phaseS, nameS, tyS, bptr });
    } else if (kind == 5 || kind == 6) {  // interface fat-pointer: load obj slot (field 0) + print
        // kind 6 = DECL (from aimlTraceKind), kind 5 = DROP (from ScopeDrop.kind). Extract the
        // interface name from typeName ("ptr<Iface>") to GEP the fat-pointer's obj slot.
        std::string ifn = (typeName.rfind("ptr<", 0) == 0 && typeName.back() == '>')
            ? typeName.substr(4, typeName.size() - 5) : std::string();
        auto ii = ctx.interfaces.find(ifn);
        llvm::Value* objp = addr;
        if (ii != ctx.interfaces.end() && ii->second.fatPtrType) {
            auto* fpTy = static_cast<llvm::StructType*>(ii->second.fatPtrType);
            llvm::Value* objField = ctx.builder.CreateStructGEP(fpTy, addr, 0, "trace_fp_obj");
            objp = ctx.builder.CreateLoad(i8p, objField, "trace_iface_obj");
        }
        llvm::Value* tyS = ctx.builder.CreateGlobalStringPtr(tracedType);
        llvm::Value* fmt = ctx.builder.CreateGlobalStringPtr("[aiml-trace] %s %s (%s) iface_obj@%p\n");
        ctx.builder.CreateCall(pf, { fmt, phaseS, nameS, tyS, objp });
    } else {  // model / map / other: address (+ type)
        llvm::Value* tyS = ctx.builder.CreateGlobalStringPtr(tracedType);
        llvm::Value* fmt = ctx.builder.CreateGlobalStringPtr("[aiml-trace] %s %s (%s) @%p\n");
        ctx.builder.CreateCall(pf, { fmt, phaseS, nameS, tyS, addr });
    }
}

static void traceSymbolTarget(CompilerContext& ctx, const char* phase, const std::string& target) {
    std::string path = target;
    if (path.rfind("<.", 0) == 0) path = "this." + path.substr(2);
    size_t dot = path.find('.');
    std::string leaf = path.substr(path.find_last_of('.') == std::string::npos ? 0 : path.find_last_of('.') + 1);
    if (!aimlTraceSymbol(target) && !aimlTraceSymbol(leaf)) return;
    auto it = ctx.symbol_table.find(path.substr(0, dot));
    if (it == ctx.symbol_table.end() || !it->second.ir_alloca_inst) return;
    llvm::Value* addr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(it->second.ir_alloca_inst));
    std::string tn = it->second.typeName;
    if (it->second.is_byref_arg) addr = ctx.builder.CreateLoad(ctx.builder.getPtrTy(), addr);
    while (dot != std::string::npos) {
        if (tn.rfind("ptr<", 0) == 0 || tn.rfind("ref<", 0) == 0) {
            tn = tn.substr(4, tn.size() - 5);
            addr = ctx.builder.CreateLoad(ctx.builder.getPtrTy(), addr);
        }
        auto cls = ctx.classes.find(tn);
        if (cls == ctx.classes.end() || !cls->second.type) return;
        size_t next = path.find('.', dot + 1);
        auto member = cls->second.members.find(path.substr(dot + 1, next == std::string::npos ? next : next - dot - 1));
        if (member == cls->second.members.end()) return;
        addr = ctx.builder.CreateStructGEP(cls->second.type, addr, member->second.index);
        tn = member->second.typeName;
        dot = next;
    }
    int kind = aimlTraceKind(ctx, tn);
    if (kind >= 0) emitSymbolValueTrace(ctx, phase, target, kind, tn, addr);
}

static void emitScopeDrop(CompilerContext& ctx, const CompilerContext::ScopeDrop& it) {
    llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
    if (aimlTraceSymbol(it.debugName))       // AIML_TRACE_SYMBOL: value right before the free
        emitSymbolValueTrace(ctx, "DROP", it.debugName, it.kind, it.debugType, it.addr);
    if (it.kind == 1) {
        llvm::Value* sptr = ctx.builder.CreateLoad(i8p, it.addr, "drop_str");
        llvm::Function* dc = ctx.getFunction("aiml_str_decref", ctx.builder.getVoidTy(), {i8p});
        ctx.builder.CreateCall(dc, { sptr });
        // spec/61 §7: a managed string slot is nulled after the drop so that a second drop on a
        // merged Live|Moved path (or a later decref-old) is a null no-op — the slot invariant holds.
        if (it.managedStringSlot)
            ctx.builder.CreateStore(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)), it.addr);
    } else if (it.kind == 2) {
        llvm::Value* nodesArg = it.serNodes
            ? static_cast<llvm::Value*>(it.serNodes)
            : llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p));
        if (it.isListHandle) {
            // spec/61 §5 Option B (memV3): the slot holds a POINTER to a refcounted handle —
            // load it, then decref (frees data + block at rc 0). Null-safe in the runtime.
            llvm::Value* hptr = ctx.builder.CreateLoad(i8p, it.addr, "drop_listh");
            llvm::Function* rel = ctx.getFunction("aiml_list_handle_release", ctx.builder.getVoidTy(),
                {i8p, ctx.builder.getInt32Ty(), ctx.builder.getInt64Ty(), i8p, ctx.builder.getInt32Ty()});
            ctx.builder.CreateCall(rel, { hptr, ctx.builder.getInt32(it.elemKind),
                                          ctx.builder.getInt64(it.elemSize), nodesArg,
                                          ctx.builder.getInt32(it.serNodesCount) });
        } else {
            llvm::Function* lf = ctx.getFunction("aiml_list_free", ctx.builder.getVoidTy(),
                {i8p, ctx.builder.getInt32Ty(), ctx.builder.getInt64Ty(), i8p, ctx.builder.getInt32Ty()});
            ctx.builder.CreateCall(lf, { it.addr, ctx.builder.getInt32(it.elemKind),
                                         ctx.builder.getInt64(it.elemSize), nodesArg,
                                         ctx.builder.getInt32(it.serNodesCount) });
        }
    } else if (it.kind == 3) {
        llvm::Value* vNodes = it.serNodes
            ? static_cast<llvm::Value*>(it.serNodes)
            : llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p));
        if (it.isListHandle) {
            // spec/61 §5 Option B (memV3): map handle — load the pointer, decref (frees at rc 0).
            llvm::Value* hptr = ctx.builder.CreateLoad(i8p, it.addr, "drop_maph");
            llvm::Function* rel = ctx.getFunction("aiml_map_handle_release", ctx.builder.getVoidTy(),
                {i8p, ctx.builder.getInt32Ty(), ctx.builder.getInt64Ty(),
                 ctx.builder.getInt32Ty(), ctx.builder.getInt64Ty(), i8p, ctx.builder.getInt32Ty()});
            ctx.builder.CreateCall(rel, { hptr,
                ctx.builder.getInt32(it.keyKind), ctx.builder.getInt64(it.keyStride),
                ctx.builder.getInt32(it.elemKind), ctx.builder.getInt64(it.elemSize),
                vNodes, ctx.builder.getInt32(it.serNodesCount) });
        } else {
            llvm::Function* mf = ctx.getFunction("aiml_map_free", ctx.builder.getVoidTy(),
                {i8p, ctx.builder.getInt32Ty(), ctx.builder.getInt64Ty(),
                 ctx.builder.getInt32Ty(), ctx.builder.getInt64Ty(), i8p, ctx.builder.getInt32Ty()});
            ctx.builder.CreateCall(mf, { it.addr,
                ctx.builder.getInt32(it.keyKind), ctx.builder.getInt64(it.keyStride),
                ctx.builder.getInt32(it.elemKind), ctx.builder.getInt64(it.elemSize),
                vNodes, ctx.builder.getInt32(it.serNodesCount) });
        }
    } else if (it.kind == 4) {
        // owned new-box: the alloca holds a heap pointer (calloc'd in visitNewExpr).
        // Load it, deep-free the pointee's heap fields (if any), then free the shell.
        llvm::Value* boxptr = ctx.builder.CreateLoad(i8p, it.addr, "drop_box");
        // spec/61 §6.2: a POD box (T has NO heap fields → serNodes == null) needs only the SHELL
        // free — skip the recursive field deep-free (round8 §4: `new` of a heap-field-less class
        // leaked its outer allocation because no drop was registered at all).
        if (it.serNodes) {
            llvm::Function* fn = ctx.getFunction("aiml_ser_free_decoded",
                ctx.builder.getVoidTy(), {i8p, i8p, ctx.builder.getInt32Ty()});
            ctx.builder.CreateCall(fn, { boxptr, it.serNodes, ctx.builder.getInt32(it.serNodesCount) });
        }
        llvm::Function* fr = ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p});
        ctx.builder.CreateCall(fr, { boxptr });
    } else if (it.kind == 5) {
        // owned interface fat-pointer box: it.addr is the AimlFatPtr_<Iface> alloca {obj, vtable}.
        // Load obj (field 0) + vtable (field 1); the vtable's TRAILING slot (index = method count)
        // is __free_<Class> (void(i8*)) for the concrete type. Dispatch it on obj → deep-free +
        // shell free of the concrete object. Mirrors interface method dispatch (ast_compiler_call).
        auto* fpTy = static_cast<llvm::StructType*>(it.fatPtrType);
        llvm::Value* objField  = ctx.builder.CreateStructGEP(fpTy, it.addr, 0, "fp_obj");
        llvm::Value* vtblField = ctx.builder.CreateStructGEP(fpTy, it.addr, 1, "fp_vtbl");
        llvm::Value* objPtr  = ctx.builder.CreateLoad(i8p, objField,  "drop_iface_obj");
        llvm::Value* vtblPtr = ctx.builder.CreateLoad(i8p, vtblField, "drop_iface_vtbl");
        // NULL-guard: a null interface box carries {obj=null, vtable=null} (e.g. get_base's
        // `return null` for not-found / not-owner). Dispatching the vtable free slot on a null
        // vtable would deref address 0x8 → SIGSEGV. Only free when the vtable is non-null.
        auto* nullp = llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p));
        llvm::Value* isNull = ctx.builder.CreateICmpEQ(vtblPtr, nullp, "fp_vtbl_null");
        llvm::Function* curFn = ctx.builder.GetInsertBlock()->getParent();
        auto* doBB   = llvm::BasicBlock::Create(ctx.context, "ifacedrop.do", curFn);
        auto* contBB = llvm::BasicBlock::Create(ctx.context, "ifacedrop.cont", curFn);
        ctx.builder.CreateCondBr(isNull, contBB, doBB);
        ctx.builder.SetInsertPoint(doBB);
        auto* slotsTy = llvm::ArrayType::get(i8p, (uint64_t)it.ifaceMethodCount + 1);
        llvm::Value* slotsBase = ctx.builder.CreateBitCast(vtblPtr, slotsTy->getPointerTo());
        llvm::Value* slotPtr = ctx.builder.CreateInBoundsGEP(slotsTy, slotsBase,
            { ctx.builder.getInt32(0), ctx.builder.getInt32(it.ifaceMethodCount) }, "free_slot");
        llvm::Value* freeFnPtr = ctx.builder.CreateLoad(i8p, slotPtr, "free_fn");
        auto* freeTy = llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p}, false);
        ctx.builder.CreateCall(freeTy, freeFnPtr, { objPtr });
        ctx.builder.CreateBr(contBB);
        ctx.builder.SetInsertPoint(contBB);
    } else if (it.kind == 7) {
        // AIML v2 (doc/aiml_v2_impl_plan.md P5): refcounted MODEL local — the alloca holds a %M*.
        // Load it and aiml_model_decref: at refcount 0 it deep-frees the inner heap (via the model's
        // serNodes) and frees the shell; otherwise just decrements. Order-independent, no double-free.
        llvm::Value* objptr = ctx.builder.CreateLoad(i8p, it.addr, "drop_refmodel");
        llvm::Function* dref = ctx.getFunction("aiml_model_decref", ctx.builder.getVoidTy(),
            {i8p, i8p, ctx.builder.getInt32Ty()});
        ctx.builder.CreateCall(dref, { objptr, it.serNodes, ctx.builder.getInt32(it.serNodesCount) });
    } else if (it.kind == 8) {
        // spec/61 §5 Option B (memV3): buffer handle — load the pointer, decref (frees data + block
        // at rc 0). Null-safe in the runtime.
        llvm::Value* hptr = ctx.builder.CreateLoad(i8p, it.addr, "drop_bufh");
        llvm::Function* rel = ctx.getFunction("aiml_buffer_handle_release", ctx.builder.getVoidTy(), {i8p});
        ctx.builder.CreateCall(rel, { hptr });
    } else if (it.kind == 9) {
        // spec/61 §5.6 (memV3): dyn_array<T> Class-4 unique buffer — load the raw i8* and free it.
        // free(NULL) is a no-op, so an earlier `.free` (which nulls the slot) makes this safe.
        llvm::Value* rawptr = ctx.builder.CreateLoad(i8p, it.addr, "drop_dynarr");
        llvm::Function* fr = ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p});
        ctx.builder.CreateCall(fr, { rawptr });
    } else if (it.kind == 11) {
        // cascade_class instance: release its own fields (the block share), then the shell; the
        // cascade rows themselves are global. Null-safe; null the slot (single release).
        llvm::Value* shell = ctx.builder.CreateLoad(i8p, it.addr, "drop_ccshell");
        if (it.serNodes) {
            llvm::BasicBlock* cur = ctx.builder.GetInsertBlock();
            llvm::Function* fn = cur->getParent();
            llvm::BasicBlock* relBB = llvm::BasicBlock::Create(ctx.context, "ccshell_rel", fn);
            llvm::BasicBlock* doneBB = llvm::BasicBlock::Create(ctx.context, "ccshell_done", fn);
            ctx.builder.CreateCondBr(ctx.builder.CreateIsNotNull(shell), relBB, doneBB);
            ctx.builder.SetInsertPoint(relBB);
            ctx.builder.CreateCall(ctx.getFunction("aiml_ser_free_decoded", ctx.builder.getVoidTy(),
                {i8p, i8p, ctx.builder.getInt32Ty()}), { shell, it.serNodes, ctx.builder.getInt32(it.serNodesCount) });
            ctx.builder.CreateBr(doneBB);
            ctx.builder.SetInsertPoint(doneBB);
        }
        ctx.builder.CreateCall(ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p}), { shell });
        ctx.builder.CreateStore(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)), it.addr);
    } else if (it.kind == 10) {
        // spec/61 class 4: iterator<T> — aiml_iter_free is null-safe; null the slot (single release).
        llvm::Value* itp = ctx.builder.CreateLoad(i8p, it.addr, "drop_iter");
        ctx.builder.CreateCall(ctx.getFunction("aiml_iter_free", ctx.builder.getVoidTy(), {i8p}), { itp });
        ctx.builder.CreateStore(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)), it.addr);
    } else {
        llvm::Function* fn = ctx.getFunction("aiml_ser_free_decoded",
            ctx.builder.getVoidTy(), {i8p, i8p, ctx.builder.getInt32Ty()});
        ctx.builder.CreateCall(fn, { it.addr, it.serNodes, ctx.builder.getInt32(it.serNodesCount) });
    }
}
void Compiler::visitBlockStmt(BlockStmt* block) {
    auto savedSymbols = ctx.symbol_table;
    ctx.cleanupStack.emplace_back();            // spec/61: per-block cleanup frame
    // spec/61 §3.1: seed staged consumed-parameter drops into THIS frame (the function
    // body — visitFunction stages them immediately before this call, so only the body
    // block receives them; nested blocks see the now-empty staging vector).
    if (!ctx.pendingParamDrops.empty()) {
        auto& frame = ctx.cleanupStack.back();
        frame.insert(frame.end(), ctx.pendingParamDrops.begin(), ctx.pendingParamDrops.end());
        ctx.pendingParamDrops.clear();
    }
    for (const auto& stmt : block->statements) {
        visitStatement(stmt.get());
    }
    // spec/61 (A2): drop owned value-locals declared in this block, in reverse decl
    // order, at NORMAL block exit — only if this basic block is not already terminated
    // (a return/break left on this path; those exits get their own cleanup in a later
    // step, so skipping here is a residual leak on that path, never a double-free).
    {
        auto* bb = static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
        if (bb && !bb->getTerminator() && ctx.lifetimeDropsEnabled) {
            auto& frame = ctx.cleanupStack.back();
            // spec/61 §4/§9.2: gate each registered drop through the ownership plan's exit facts.
            // A drop fires only for a provably-owned, un-rebound slot (mayDrop). If there is no
            // plan (e.g. module init) or the analyzer did not record this block, fall back to the
            // pre-plan behaviour (emit as registered) — never worse than before for that context.
            const OwnershipPlan* plan = ctx.ownershipPlan;
            const FlowState* exit = nullptr;
            if (plan) {
                auto pe = plan->normalExits.find(block);
                if (pe != plan->normalExits.end()) exit = &pe->second;
            }
            for (auto it = frame.rbegin(); it != frame.rend(); ++it) {
                // AIML v2: refcount MODEL decref (kind 7) is always safe → never suppressed.
                if (it->kind != 7 && plan && exit && it->ownershipId != NoOwnership
                    && !mayDrop(*plan, *exit, it->ownershipId))
                    continue;   // not provably owned at this exit → suppress (leak, never UAF)
                emitScopeDrop(ctx, *it);
            }
        }
    }
    ctx.cleanupStack.pop_back();
    ctx.symbol_table = savedSymbols;
}
void Compiler::visitIfStmt(IfStmt* ifStmt) {
    if (!ifStmt->condition) return;
    
    IRValue* condVal = visitExpression(ifStmt->condition.get());
    if (!condVal) return;
    
    // Ensure the condition is boolean (i1). In AIML, any non-zero is true.
    // For now assume strictly typed, or cast:
    if (ctx.ir_gen->unwrapType(condVal->getType()) != ctx.builder.getInt1Ty()) {
        IRValue* zero = ctx.ir_gen->getInt32(0); // Assuming 32-bit compare for now
        condVal = ctx.ir_gen->createICmpNE(condVal, zero, "to_bool");
    }
    // spec/61 (T1c): free the condition's owned-string temps before branching (the boolean is already
    // computed), so a terminating then/else branch cannot leak them (CardArtCache `if has_texture(
    // "@art:${hsh}") return 1` leaked the concat every cache-hit frame).
    decrefCurrentStmtTemps(ctx);

    IRBlock* thenBB = ctx.ir_gen->createBasicBlock("if_then");
    IRBlock* mergeBB = nullptr;
    IRBlock* elseBB = nullptr;
    
    if (ifStmt->elseBranch) {
        elseBB = ctx.ir_gen->createBasicBlock("if_else");
    }
    mergeBB = ctx.ir_gen->createBasicBlock("if_merge");
    
    ctx.ir_gen->createCondBr(condVal, thenBB, ifStmt->elseBranch ? elseBB : mergeBB);
    
    // THEN branch
    ctx.ir_gen->setInsertPoint(thenBB);
    if (ifStmt->thenBranch) {
        visitBlockStmt(ifStmt->thenBranch.get());
    }
    {
        auto* bb = static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
        if (!bb->getTerminator()) ctx.ir_gen->createBr(mergeBB);
    }

    // ELSE branch
    if (ifStmt->elseBranch) {
        ctx.ir_gen->setInsertPoint(elseBB);
        visitBlockStmt(ifStmt->elseBranch.get());
        auto* bb = static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
        if (!bb->getTerminator()) ctx.ir_gen->createBr(mergeBB);
    }

    // MERGE block
    ctx.ir_gen->setInsertPoint(mergeBB);
}
void Compiler::visitWhileStmt(WhileStmt* whileStmt) {
    if (!whileStmt->condition) return;
    
    IRBlock* loopBB = ctx.ir_gen->createBasicBlock("while_loop");
    IRBlock* bodyBB = ctx.ir_gen->createBasicBlock("while_body");
    IRBlock* endBB = ctx.ir_gen->createBasicBlock("while_end");
    
    // Jump to loop check
    ctx.ir_gen->createBr(loopBB);
    
    // Loop Check
    ctx.ir_gen->setInsertPoint(loopBB);
    IRValue* condVal = visitExpression(whileStmt->condition.get());
    if (!condVal) return;
    
    if (ctx.ir_gen->unwrapType(condVal->getType()) != ctx.builder.getInt1Ty()) {
        IRValue* zero = ctx.ir_gen->getInt32(0);
        condVal = ctx.ir_gen->createICmpNE(condVal, zero, "while_cond_bool");
    }
    // spec/61 (T1c): free the while-condition's owned-string temps each iteration (evaluated here in
    // loopBB), before entering the body/exit — a condition interpolation temp would otherwise leak
    // once per iteration, and the per-statement decref (after the loop) would only see the last value.
    decrefCurrentStmtTemps(ctx);

    ctx.ir_gen->createCondBr(condVal, bodyBB, endBB);
    
    // Body Block
    ctx.ir_gen->setInsertPoint(bodyBB);
    if (whileStmt->body) {
        visitBlockStmt(whileStmt->body.get());
    }
    {
        auto* bb = static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
        if (!bb->getTerminator()) ctx.ir_gen->createBr(loopBB);
    }
    
    // End Block
    ctx.ir_gen->setInsertPoint(endBB);
}
void Compiler::visitTry(TryStmt* tryStmt) {
    LOG_DEBUG("Compiling TryStmt. Has TryBlock: " + std::to_string(tryStmt->tryBlock != nullptr));
    if (!tryStmt->tryBlock) {
        LOG_ERROR(ctx.fmtLoc() + "AST Error: Try statement is missing a body. The try block will not execute.");
        return; 
    }

    llvm::Function* func = ctx.builder.GetInsertBlock()->getParent();

    llvm::BasicBlock* tryBlock = llvm::BasicBlock::Create(ctx.context, "try_block", func);
    llvm::BasicBlock* catchBlock = llvm::BasicBlock::Create(ctx.context, "catch_block", func);
    llvm::BasicBlock* contBlock = llvm::BasicBlock::Create(ctx.context, "try_cont", func);

    ctx.builder.CreateBr(tryBlock);
    ctx.builder.SetInsertPoint(tryBlock);

    // Save previous catch block logically if we implement nested tries.
    // For now, assume top-level single try/catch handling via IRGen's context or just emit blocks.
    // To properly support invoke, any call inside tryBlock must be an invoke.
    // Since we don't know which call throws, we need to instruct ir_gen to use invoke instead of call!
    // We'll set a state in ctx to emit invokes to our catch block.
    
    llvm::BasicBlock* oldCatch = ctx.catchBlock;
    ctx.catchBlock = catchBlock;

    if (tryStmt->tryBlock) {
        visitBlockStmt(tryStmt->tryBlock.get());
    }
    
    // Jump to contBlock if we haven't returned/thrown
    if (!ctx.builder.GetInsertBlock()->getTerminator()) {
        ctx.builder.CreateBr(contBlock);
    }

    // Restore catch block
    ctx.catchBlock = oldCatch;

    ctx.builder.SetInsertPoint(catchBlock);
    
    // Set Personality Function
    llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
    if (!parentFunc->hasPersonalityFn()) {
        llvm::Function* persFunc = ctx.module.getFunction("__gxx_personality_v0");
        if (!persFunc) {
            llvm::FunctionType* fty = llvm::FunctionType::get(ctx.builder.getInt32Ty(), true);
            persFunc = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "__gxx_personality_v0", ctx.module);
        }
        parentFunc->setPersonalityFn(persFunc);
    }

    // Emit LandingPad
    llvm::PointerType* ptrTy = llvm::PointerType::getUnqual(ctx.context);
    llvm::StructType* landingPadTy = llvm::StructType::get(ctx.context, {ptrTy, ctx.builder.getInt32Ty()});
    llvm::LandingPadInst* lpad = ctx.builder.CreateLandingPad(landingPadTy, 1, "lpad");
    
    // We only catch C++ exceptions (or our own type) roughly.
    // We need typeinfo of __cxa_exceptio n or equivalent.
    // Null catches all in C++ LLVM IR usually: `catch i8* null`
    lpad->addClause(llvm::ConstantPointerNull::get(ptrTy));
    lpad->setCleanup(false); // We are catching it.
    
    // Extract exception pointer
    llvm::Value* excPtr = ctx.builder.CreateExtractValue(lpad, 0, "exc_ptr");

    // Begin Catch
    llvm::Function* beginCatchFunc = ctx.module.getFunction("__cxa_begin_catch");
    if (!beginCatchFunc) {
        llvm::FunctionType* fty = llvm::FunctionType::get(ptrTy, {ptrTy}, false);
        beginCatchFunc = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "__cxa_begin_catch", ctx.module);
    }
    
    llvm::Value* rawExc = ctx.builder.CreateCall(beginCatchFunc, {excPtr}, "raw_exc");
    
    // If catch type specified, cast to it. 
    // Wait, AIML dynamic typing or we cast to an Object base?
    // For MVP we just give them the pointer and let them cast, or assume it is an Exception object.
    
    if (tryStmt->catchBlock && !tryStmt->catchVar.empty()) {
        // Assume exception is a ptr to `catchType` struct
        llvm::Type* excType = ctx.getLLVMType(tryStmt->catchType);
        if (!excType) excType = ctx.builder.getInt8Ty(); // fallback
        
        // Since we allocated 8 bytes in __cxa_allocate_exception and stored the actual object ptr inside it,
        // we must load the object ptr from the returned envelope.
        llvm::Value* userExc = ctx.builder.CreateLoad(ptrTy, rawExc, "actual_exc_ptr");
        
        // Register variable in scope
        IRType* targetTy = ctx.ir_gen->wrapType(ptrTy); // Opaque pointer
        IRValue* alloca = ctx.ir_gen->createAlloca(targetTy, tryStmt->catchVar);
        ctx.builder.CreateStore(userExc, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca)));
        
        Symbol sym;
        sym.ir_alloca_inst = alloca;
        sym.ir_type = ctx.ir_gen->getPointerTy();
        sym.ir_element_type = targetTy;
        sym.typeName = tryStmt->catchType;
        ctx.symbol_table[tryStmt->catchVar] = sym;
        
        visitBlockStmt(tryStmt->catchBlock.get());
    }

    // End Catch
    llvm::Function* endCatchFunc = ctx.module.getFunction("__cxa_end_catch");
    if (!endCatchFunc) {
        llvm::FunctionType* fty = llvm::FunctionType::get(ctx.builder.getVoidTy(), false);
        endCatchFunc = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "__cxa_end_catch", ctx.module);
    }
    
    if (!ctx.builder.GetInsertBlock()->getTerminator()) {
        ctx.builder.CreateCall(endCatchFunc);
        ctx.builder.CreateBr(contBlock);
    } else {
        // If it terminates, we still need to end catch before returning/throwing?
        // Actually C++ requires invoking end_catch before returning from catch!
        // This is complex for MVP, we'll assume people don't branch directly out of catch without it manually, 
        // or we just put it right before.
    }

    ctx.builder.SetInsertPoint(contBlock);
}
void Compiler::visitThrow(ThrowStmt* throwStmt) {
    if (!throwStmt->value) return;
    
    IRValue* excVal = visitExpression(throwStmt->value.get());
    if (!excVal) return;

    llvm::Value* rawObj = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(excVal));
    llvm::PointerType* ptrTy = llvm::PointerType::getUnqual(ctx.context);

    llvm::Function* allocExcFunc = ctx.module.getFunction("__cxa_allocate_exception");
    if (!allocExcFunc) {
        llvm::FunctionType* fty = llvm::FunctionType::get(ptrTy, {ctx.builder.getInt64Ty()}, false);
        allocExcFunc = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "__cxa_allocate_exception", ctx.module);
    }

    llvm::Function* throwFunc = ctx.module.getFunction("__cxa_throw");
    if (!throwFunc) {
        llvm::FunctionType* fty = llvm::FunctionType::get(ctx.builder.getVoidTy(), 
            {ptrTy, ptrTy, ptrTy}, false);
        throwFunc = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "__cxa_throw", ctx.module);
    }

    const llvm::DataLayout& dl = ctx.module.getDataLayout();
    uint64_t excSize = 8; // Pointer size for thrown object reference
    
    llvm::Value* excAlloc = ctx.builder.CreateCall(allocExcFunc, {ctx.builder.getInt64(excSize)}, "exc_alloc");
    
    // cast rawObj to i8* and store it in excAlloc? No, AIML uses objects. We can just store the pointer.
    // With Opaque Pointers, excAlloc is `ptr`. `rawObj` is `ptr`.
    ctx.builder.CreateStore(rawObj, excAlloc);

    // Record the exception's `message` (offset 0, an i8* — every exception
    // extends Exception whose sole member `message` sits first) so the
    // top-level uncaught handler in jitRun can print it (spec/22).  Harmless
    // when the throw IS caught; the value is simply overwritten by the next throw.
    {
        llvm::Function* noteFunc = ctx.module.getFunction("aiml_note_thrown_message");
        if (!noteFunc) {
            llvm::FunctionType* nfty = llvm::FunctionType::get(ctx.builder.getVoidTy(), {ptrTy}, false);
            noteFunc = llvm::Function::Create(nfty, llvm::Function::ExternalLinkage, "aiml_note_thrown_message", ctx.module);
        }
        llvm::Value* msgCharPtr = ctx.builder.CreateLoad(ptrTy, rawObj, "exc_msg_ptr");
        ctx.builder.CreateCall(noteFunc, {msgCharPtr});
    }

    llvm::Value* nullTypeInfo = llvm::ConstantPointerNull::get(ptrTy);
    llvm::Value* nullDtor = llvm::ConstantPointerNull::get(ptrTy);

    if (ctx.catchBlock) {
        llvm::Function* currentFunc = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* unreachableBB = llvm::BasicBlock::Create(ctx.context, "throw_unreachable", currentFunc);
        ctx.builder.CreateInvoke(throwFunc, unreachableBB, ctx.catchBlock, {excAlloc, nullTypeInfo, nullDtor});
        ctx.builder.SetInsertPoint(unreachableBB);
        ctx.builder.CreateUnreachable();
        // Move insert point to a dead block so any subsequent statements in the same
        // scope don't append instructions after the unreachable terminator.
        llvm::BasicBlock* deadBB = llvm::BasicBlock::Create(ctx.context, "throw_dead", currentFunc);
        ctx.builder.SetInsertPoint(deadBB);
    } else {
        llvm::Function* currentFunc = ctx.builder.GetInsertBlock()->getParent();
        ctx.builder.CreateCall(throwFunc, {excAlloc, nullTypeInfo, nullDtor});
        ctx.builder.CreateUnreachable();
        llvm::BasicBlock* deadBB = llvm::BasicBlock::Create(ctx.context, "throw_dead", currentFunc);
        ctx.builder.SetInsertPoint(deadBB);
    }
}

void Compiler::visitIdxStmt(IdxStmt* idx) {
    // Resolve the container.  Supports plain variables ("g_foo"), the `<.`
    // shortcut for `this.<field>` inside class methods, and full `this.<field>`
    // / `obj.<field>` property paths via resolvePropertyWritePtr.
    std::string container = idx->container;
    if (container.rfind("<.", 0) == 0) container = "this." + container.substr(2);

    Symbol* sym = nullptr;
    Symbol propSym;  // backing storage when container is a property path
    size_t dotPos = container.find('.');
    if (dotPos != std::string::npos) {
        std::string baseName = container.substr(0, dotPos);
        std::string propPath = container.substr(dotPos + 1);
        Symbol* baseSym = resolveVariable(baseName);
        if (baseSym) {
            std::string fieldTy;
            IRValue* fieldPtr = resolvePropertyWritePtr(baseSym, propPath, fieldTy);
            if (fieldPtr) {
                propSym.typeName       = fieldTy;
                propSym.ir_alloca_inst = fieldPtr;
                propSym.ir_type        = ctx.ir_gen->getPointerTy();
                if (llvm::Type* lt = ctx.getLLVMType(fieldTy))
                    propSym.ir_element_type = ctx.ir_gen->wrapType(lt);
                sym = &propSym;
            }
        }
    }
    if (!sym) sym = resolveVariable(container);
    if (!sym) {
        LOG_ERROR(ctx.fmtLoc() + "idx: unknown variable '" + idx->container + "'");
        return;
    }

    // Evaluate indices
    std::vector<llvm::Value*> indexVals;
    for (auto& idxExpr : idx->indices) {
        IRValue* v = visitExpression(idxExpr.get());
        if (!v) return;
        llvm::Value* raw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v));
        if (raw->getType()->isIntegerTy(64))
            raw = ctx.builder.CreateTrunc(raw, ctx.builder.getInt32Ty());
        indexVals.push_back(raw);
    }

    // Evaluate the value to store
    IRValue* valIR = visitExpression(idx->value.get());
    bool idxValOwnedTemp = valIR && inStringTempStack(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(valIR)));
    if (valIR) consumeStringTemp(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(valIR))); // spec/61: moved into element
    if (!valIR) return;
    llvm::Value* storeVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(valIR));

    const std::string& tName = sym->typeName;

    // ── array<T:N[:M[:...]]> ──
    if (tName.rfind("array<", 0) == 0 && tName.back() == '>') {
        std::string inner = tName.substr(6, tName.size() - 7);
        std::vector<std::string> parts;
        size_t pos = 0;
        while (pos < inner.size()) {
            size_t sep = inner.find(':', pos);
            if (sep == std::string::npos) { parts.push_back(inner.substr(pos)); break; }
            parts.push_back(inner.substr(pos, sep - pos));
            pos = sep + 1;
        }
        llvm::Type* elemTy = ctx.getLLVMType(parts[0]);
        std::vector<uint64_t> dims;
        for (size_t i = 1; i < parts.size(); ++i)
            dims.push_back(std::stoull(parts[i]));

        // Compute flat index (row-major)
        llvm::Value* flatIdx = nullptr;
        if (indexVals.size() == 1) {
            flatIdx = indexVals[0];
        } else {
            flatIdx = indexVals[0];
            for (size_t i = 1; i < indexVals.size(); ++i) {
                uint64_t stride = 1;
                for (size_t j = i; j < dims.size(); ++j) stride *= dims[j];
                flatIdx = ctx.builder.CreateMul(flatIdx, ctx.builder.getInt32((uint32_t)stride));
                flatIdx = ctx.builder.CreateAdd(flatIdx, indexVals[i]);
            }
        }

        uint64_t totalCount = 1;
        for (auto d : dims) totalCount *= d;
        llvm::Type* arrTy = llvm::ArrayType::get(elemTy, totalCount);
        llvm::Value* arrPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
            sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
        llvm::Value* elemPtr = ctx.builder.CreateGEP(arrTy, arrPtr,
            {ctx.builder.getInt32(0), flatIdx}, "arr_idx_gep");

        // Type coercion for store value.
        // Arrays now support C-like pointer/value semantics:
        // - ptr<T> elements store raw addresses
        // - struct elements store the full struct value inline
        if (storeVal->getType() != elemTy) {
            if (elemTy->isPointerTy() && storeVal->getType()->isPointerTy()) {
                storeVal = ctx.builder.CreatePointerCast(storeVal, elemTy, "idx_pcast");
            } else if (elemTy->isStructTy() && storeVal->getType()->isPointerTy()) {
                uint64_t sz = ctx.module.getDataLayout().getTypeAllocSize(elemTy);
                llvm::Value* tmp = ctx.createEntryAlloca(elemTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                llvm::Function* memcpyFunc = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
                ctx.builder.CreateCall(memcpyFunc, {
                    tmp,
                    storeVal,
                    ctx.getSizeConst(sz)
                });
                storeVal = ctx.builder.CreateLoad(elemTy, tmp, "idx_struct_from_ptr");
            } else if (elemTy->isFloatingPointTy() && storeVal->getType()->isIntegerTy()) {
                storeVal = ctx.builder.CreateSIToFP(storeVal, elemTy, "idx_i2f");
            } else if (elemTy->isFloatingPointTy() && storeVal->getType()->isFloatingPointTy()) {
                storeVal = ctx.builder.CreateFPCast(storeVal, elemTy, "idx_fcast");
            } else if (elemTy->isIntegerTy() && storeVal->getType()->isIntegerTy()) {
                storeVal = ctx.builder.CreateZExtOrTrunc(storeVal, elemTy, "idx_icast");
            } else if (elemTy->isIntegerTy() && storeVal->getType()->isFloatingPointTy()) {
                storeVal = ctx.builder.CreateFPToSI(storeVal, elemTy, "idx_f2i");
            } else {
                LOG_ERROR(ctx.fmtLoc() + "idx: incompatible value for array element store");
                return;
            }
        }

        ctx.builder.CreateStore(storeVal, elemPtr);
        return;
    }

    // ── dyn_array<T> ──
    if (tName.rfind("dyn_array<", 0) == 0 && tName.back() == '>') {
        std::string elemName = tName.substr(10, tName.size() - 11);
        llvm::Type* elemTy   = ctx.getLLVMType(elemName);
        llvm::Type* i8p      = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* ptrAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
            sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
        llvm::Value* rawPtr  = ctx.builder.CreateLoad(i8p, ptrAlloca, "darr_base");
        llvm::Value* typedPtr = ctx.builder.CreatePointerCast(rawPtr, elemTy->getPointerTo(), "darr_typed");
        if (indexVals.empty()) { LOG_ERROR("idx: dyn_array requires index"); return; }
        llvm::Value* elemPtr = ctx.builder.CreateGEP(elemTy, typedPtr, indexVals[0], "darr_idx_gep");
        if (storeVal->getType() != elemTy) {
            if (elemTy->isFloatingPointTy() && storeVal->getType()->isIntegerTy())
                storeVal = ctx.builder.CreateSIToFP(storeVal, elemTy, "darr_i2f");
            else if (elemTy->isFloatingPointTy() && storeVal->getType()->isFloatingPointTy())
                storeVal = ctx.builder.CreateFPCast(storeVal, elemTy, "darr_fcast");
            else if (elemTy->isIntegerTy() && storeVal->getType()->isIntegerTy())
                storeVal = ctx.builder.CreateZExtOrTrunc(storeVal, elemTy, "darr_icast");
        }
        ctx.builder.CreateStore(storeVal, elemPtr);
        return;
    }

    // ── list<T> ──
    if (tName.rfind("list<", 0) == 0 && tName.back() == '>') {
        std::string elemName = tName.substr(5, tName.size() - 6);
        llvm::Type* elemTy = ctx.getLLVMType(elemName);
        llvm::Value* listPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
            sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
        // Byref-arg: alloca holds a pointer to the list-struct; dereference
        // before GEP into element slots.  Without this, ListImpl::getElementPtr
        // reads `dataPtr` at the wrong offset (treating the ptr-slot itself
        // as a list-struct), corrupting nearby heap memory (often the next
        // Kv handle, manifesting as "std::mutex: Invalid argument").
        // visitVariable's read path handles this via implicit deref; the
        // write path needs the same logic.
        if (sym->is_byref_arg) {
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            listPtr = ctx.builder.CreateLoad(ptrTy, listPtr, "byref_list_deref");
        }
        if (indexVals.empty()) { LOG_ERROR("idx: list requires at least one index"); return; }

        // Trailing member path: idx: [list, i, .field, value] writes element[i].field = value. Get a
        // pointer to the element (a %M* for a refcount model, else the inline struct slot), resolve
        // the member path on it, then coerce+store the value into the final field.
        if (!idx->memberPath.empty()) {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* elemBase;
            if (ctx.isRefcountModel(elemName)) {
                llvm::Value* slot = ListImpl::getElementPtr(ctx, listPtr, indexVals[0], i8p);
                elemBase = ctx.builder.CreateLoad(i8p, slot, "idx_mp_modelptr");
            } else {
                elemBase = ListImpl::getElementPtr(ctx, listPtr, indexVals[0], elemTy);
            }
            std::string joined;
            for (size_t i = 0; i < idx->memberPath.size(); ++i)
                joined += (i ? "." : "") + idx->memberPath[i];
            Symbol elemSym;
            elemSym.typeName       = elemName;
            elemSym.ir_alloca_inst = ctx.ir_gen->wrap(elemBase);
            if (ctx.classes.count(elemName) && ctx.classes[elemName].type)
                elemSym.ir_element_type = ctx.ir_gen->wrapType(ctx.classes[elemName].type);
            std::string fldTypeName;
            IRValue* fldPtr = resolvePropertyWritePtr(&elemSym, joined, fldTypeName);
            if (!fldPtr || fldTypeName.empty()) {
                LOG_ERROR(ctx.fmtLoc() + "idx: member path '" + joined + "' unresolved on element of " + tName);
                return;
            }
            llvm::Type* fldTy = ctx.getLLVMType(fldTypeName);
            llvm::Value* fp = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(fldPtr));
            if (storeVal->getType() != fldTy) {
                if (fldTy->isIntegerTy() && storeVal->getType()->isIntegerTy())
                    storeVal = ctx.builder.CreateZExtOrTrunc(storeVal, fldTy, "idx_mp_icast");
                else if (fldTy->isFloatingPointTy() && storeVal->getType()->isIntegerTy())
                    storeVal = ctx.builder.CreateSIToFP(storeVal, fldTy, "idx_mp_i2f");
                else if (fldTy->isFloatingPointTy() && storeVal->getType()->isFloatingPointTy())
                    storeVal = ctx.builder.CreateFPCast(storeVal, fldTy, "idx_mp_fcast");
                else if (fldTy->isPointerTy() && storeVal->getType()->isPointerTy())
                    storeVal = ctx.builder.CreatePointerCast(storeVal, fldTy, "idx_mp_pcast");
            }
            ctx.builder.CreateStore(storeVal, fp);
            return;
        }

        // AIML v2 P3: list<Model> slot holds a %M* reference. Store the new pointer with
        // incref-new-BEFORE-decref-old so the self-alias write-back `idx:[c,i,at(c,i)]` is safe
        // (decref-first could free the object at refcount 1 before we re-store it).
        if (ctx.isRefcountModel(elemName)) {
            auto* i8p = llvm::PointerType::get(ctx.context, 0);
            auto ciElem = ctx.classes.find(elemName);
            llvm::Value* slot = ListImpl::getElementPtr(ctx, listPtr, indexVals[0], i8p);
            llvm::Value* newObj = (storeVal->getType() == i8p) ? storeVal
                                  : ctx.builder.CreateBitCast(storeVal, i8p, "idx_modelptr");
            llvm::Function* inc = ctx.getFunction("aiml_model_incref", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(inc, { newObj });
            if (ctx.lifetimeDropsEnabled && ciElem != ctx.classes.end() && ciElem->second.serNodesVar) {
                llvm::Value* oldObj = ctx.builder.CreateLoad(i8p, slot, "idx_old_modelptr");
                llvm::Function* dref = ctx.getFunction("aiml_model_decref", ctx.builder.getVoidTy(),
                    {i8p, i8p, ctx.builder.getInt32Ty()});
                ctx.builder.CreateCall(dref, { oldObj, ciElem->second.serNodesVar,
                                               ctx.builder.getInt32(ciElem->second.serNodesCount) });
            }
            ctx.builder.CreateStore(newObj, slot);
            return;
        }
        // spec/61 §5.7 (memV3): a NESTED CONTAINER element is a HANDLE. Replace = secure-before-
        // release: incref the new inner handle, decref the OLD slot handle (via its handle-rooted
        // descriptor → recursive release at rc 0), then store the new handle. Self-alias safe
        // (incref-before-decref). No-op only when the slot already holds the same handle.
        if (ctx.memV3 && ctx.isMemV3ContainerHandle(elemName)) {
            auto* i8p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* slot = ListImpl::getElementPtr(ctx, listPtr, indexVals[0], i8p);
            // source handle pointer: read the value local's slot (deref would give the struct).
            llvm::Value* newHp = nullptr;
            if (auto* vv = dynamic_cast<VariableExpr*>(idx->value.get())) {
                if (Symbol* s = resolveVariable(vv->name)) {
                    llvm::Value* vs = s->ir_alloca_inst
                        ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(s->ir_alloca_inst)) : s->alloca_inst;
                    if (vs) newHp = ctx.builder.CreateLoad(i8p, vs, "idx_newinnerh");
                }
            }
            if (!newHp) newHp = (storeVal->getType() == i8p) ? storeVal
                               : ctx.builder.CreateBitCast(storeVal, i8p, "idx_newinnerh_c");
            const char* incF = (elemName.rfind("map<", 0) == 0) ? "aiml_map_handle_incref"
                : (elemName == "buffer" || elemName.rfind("buffer<", 0) == 0) ? "aiml_buffer_handle_incref"
                : "aiml_list_handle_incref";
            if (!isOwningContainerCall(idx->value.get(), elemName))
                ctx.builder.CreateCall(ctx.getFunction(incF, ctx.builder.getVoidTy(), {i8p}), { newHp });
            if (ctx.lifetimeDropsEnabled) {
                int nc = 0; llvm::GlobalVariable* ed = elemHandleSerNodes(elemName, nc);
                if (ed) {
                    llvm::Function* dfree = ctx.getFunction("aiml_ser_free_decoded",
                        ctx.builder.getVoidTy(), {i8p, i8p, ctx.builder.getInt32Ty()});
                    ctx.builder.CreateCall(dfree, { slot, ed, ctx.builder.getInt32(nc) });  // decref old, nulls slot
                }
            }
            ctx.builder.CreateStore(newHp, slot);
            consumeStringTemp(newHp);
            return;
        }
        // spec/61 §5.5 (memV3): list<ptr<T>> element = an OWNING box (unique owner). set(i) replaces
        // it → the OLD box is released (deep-free its fields + free the shell) BEFORE the new box's
        // pointer is stored (the new box is moved in). Without this the replaced box + its heap
        // leaked (probe 05: the "bolt" Card). Plain-pointer elemTy → not the struct/model path below.
        if (ctx.memV3 && elemName.rfind("ptr<", 0) == 0 && elemName.back() == '>') {
            auto* i8p = llvm::PointerType::get(ctx.context, 0);
            std::string boxed = elemName.substr(4, elemName.size() - 5);
            auto bi = ctx.classes.find(boxed);
            llvm::Value* slot = ListImpl::getElementPtr(ctx, listPtr, indexVals[0], i8p);
            llvm::Value* newObj = (storeVal->getType() == i8p) ? storeVal
                                  : ctx.builder.CreateBitCast(storeVal, i8p, "idx_ptrbox");
            if (ctx.lifetimeDropsEnabled) {
                llvm::Value* oldObj = ctx.builder.CreateLoad(i8p, slot, "idx_old_ptrbox");
                if (bi != ctx.classes.end() && bi->second.hasHeapFields && bi->second.serNodesVar) {
                    llvm::Function* dfree = ctx.getFunction("aiml_ser_free_decoded",
                        ctx.builder.getVoidTy(), {i8p, i8p, ctx.builder.getInt32Ty()});
                    ctx.builder.CreateCall(dfree, { oldObj, bi->second.serNodesVar,
                                                    ctx.builder.getInt32(bi->second.serNodesCount) });
                }
                llvm::Function* fr = ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p});
                ctx.builder.CreateCall(fr, { oldObj });
            }
            ctx.builder.CreateStore(newObj, slot);
            // The new box is MOVED into the list (the list now owns it) → drop the source local's
            // scope-cleanup so it is not freed twice (mirrors push at ast_compiler_call.cpp).
            consumeLocalDrop(idx->value.get());
            return;
        }
        llvm::Value* elemPtr = ListImpl::getElementPtr(ctx, listPtr, indexVals[0], elemTy);

        // spec/61 §5.5: list<string> set(i) — secure the new string (adopt an owned temp, else dup
        // like push), release the old element, store.
        if (elemName == "string" && ctx.lifetimeDropsEnabled && storeVal->getType()->isPointerTy()) {
            auto* i8p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* newS = idxValOwnedTemp ? storeVal
                : ctx.builder.CreateCall(ctx.getFunction("aiml_str_dup", i8p, {i8p}), { storeVal }, "idx_str_own");
            llvm::Value* oldS = ctx.builder.CreateLoad(i8p, elemPtr, "idx_old_str");
            ctx.builder.CreateCall(ctx.getFunction("aiml_str_decref", ctx.builder.getVoidTy(), {i8p}), { oldS });
            ctx.builder.CreateStore(newS, elemPtr);
            return;
        }

        // struct elements + pointer-typed source: memcpy the whole struct.
        // Without this, CreateStore would only write pointer-size bytes
        // (typically 8) into a slot meant for the full struct (e.g. 24+
        // bytes for a model with multiple fields), corrupting both the
        // list element and adjacent heap memory.
        // spec/61 §4.2: a VALUE-MODEL element store (`idx: [list, i, ent]`, e.g. store_entry_at's
        // sez[pos]=ent) must OWN an independent heap. A shallow store/memcpy shares the source's
        // inline list/map buffers with the list slot → double-free when both the source and the
        // container drop (the 9rooms combat store_entry_at / creature-cost path). It also overwrites
        // the old element WITHOUT freeing it → leak. So (drops-on) free the old element, then
        // DEEP-COPY the source into the slot. Handles the source as either an element POINTER or a
        // loaded struct VALUE (spilled to a temp). The member-store deep-copy analyzer skips index
        // targets (dashIdx), so index stores are handled here.
        if (elemTy->isStructTy()) {
            auto ciElem = ctx.classes.find(elemName);
            bool modelElem = ctx.lifetimeDropsEnabled && ciElem != ctx.classes.end()
                && ciElem->second.hasHeapFields && ciElem->second.serNodesVar && ciElem->second.type
                && !(llvm::cast<llvm::StructType>(elemTy)->hasName()
                     && llvm::cast<llvm::StructType>(elemTy)->getName().startswith("AimlFatPtr_"));
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            // spec/61 §5.5: a FRESH owned source (producer / copy / clone / deserialize call) cannot alias
            // the slot → free the old element and MOVE the temp in (deep-copying it would orphan it).
            bool freshModelSrc = false;
            if (modelElem) if (auto* fc = dynamic_cast<CallExpr*>(idx->value.get())) {
                const std::string& fn = fc->functionName;
                freshModelSrc = fn == "sys.clone" || fn == "sys.copy" || fn == "sys.deserialize"
                    || !ownedModelCallType(fc).empty();
            }
            if (modelElem && freshModelSrc) {
                llvm::Value* srcAddr = storeVal;
                if (!storeVal->getType()->isPointerTy()) {
                    llvm::AllocaInst* tmp = ctx.createEntryAlloca(elemTy, "idx_fresh");
                    ctx.builder.CreateStore(storeVal, tmp);
                    srcAddr = tmp;
                }
                ctx.builder.CreateCall(ctx.getFunction("aiml_ser_free_decoded", ctx.builder.getVoidTy(),
                    {ptrTy, ptrTy, ctx.builder.getInt32Ty()}),
                    { elemPtr, ciElem->second.serNodesVar, ctx.builder.getInt32(ciElem->second.serNodesCount) });
                uint64_t fsz = ctx.module.getDataLayout().getTypeAllocSize(elemTy);
                ctx.builder.CreateCall(ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()}),
                    { elemPtr, srcAddr, ctx.getSizeConst(fsz) });
                return;
            }
            if (modelElem) {
                // source address: an element pointer directly, or spill the loaded struct to a temp
                llvm::Value* srcAddr;
                if (storeVal->getType()->isPointerTy()) {
                    srcAddr = storeVal;
                } else {
                    llvm::AllocaInst* tmp = ctx.createEntryAlloca(elemTy, "idx_src");
                    ctx.builder.CreateStore(storeVal, tmp);
                    srcAddr = tmp;
                }
                // CRITICAL ordering: the source may ALIAS the destination slot — the write-back
                // idiom `idx: [c, i, at(c, i)]` (store_entry_at/remove_entry_at) passes a view of the
                // very slot being written. So deep-copy the source into an INDEPENDENT temp FIRST
                // (while the source is still valid), THEN free the old slot, THEN move the temp in.
                // Freeing the slot before the copy would read freed memory in enc() → SIGSEGV.
                uint64_t esz = ctx.module.getDataLayout().getTypeAllocSize(elemTy);
                llvm::AllocaInst* indep = ctx.createEntryAlloca(elemTy, "idx_indep");
                ctx.builder.CreateStore(llvm::ConstantAggregateZero::get(elemTy), indep);
                llvm::Function* dc = ctx.getFunction("aiml_ser_deep_copy",
                    ctx.builder.getVoidTy(), {ptrTy, ptrTy, ptrTy, ctx.builder.getInt32Ty()});
                ctx.builder.CreateCall(dc, { indep, srcAddr, ciElem->second.serNodesVar,
                                             ctx.builder.getInt32(ciElem->second.serNodesCount) });
                llvm::Function* freeFn = ctx.getFunction("aiml_ser_free_decoded",
                    ctx.builder.getVoidTy(), {ptrTy, ptrTy, ctx.builder.getInt32Ty()});
                ctx.builder.CreateCall(freeFn, { elemPtr, ciElem->second.serNodesVar,
                                                 ctx.builder.getInt32(ciElem->second.serNodesCount) });
                llvm::Function* mcf = ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
                ctx.builder.CreateCall(mcf, { elemPtr, indep, ctx.getSizeConst(esz) });
                return;
            }
            if (storeVal->getType()->isPointerTy()) {
                uint64_t sz = ctx.module.getDataLayout().getTypeAllocSize(elemTy);
                llvm::Function* memcpyFunc = ctx.getFunction("memcpy",
                    ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
                ctx.builder.CreateCall(memcpyFunc,
                    {elemPtr, storeVal, ctx.getSizeConst(sz)});
                return;
            }
        }

        if (storeVal->getType() != elemTy) {
            if (elemTy->isFloatingPointTy() && storeVal->getType()->isIntegerTy())
                storeVal = ctx.builder.CreateSIToFP(storeVal, elemTy);
            else if (elemTy->isFloatingPointTy() && storeVal->getType()->isFloatingPointTy())
                storeVal = ctx.builder.CreateFPCast(storeVal, elemTy);
            else if (elemTy->isIntegerTy() && storeVal->getType()->isIntegerTy())
                storeVal = ctx.builder.CreateZExtOrTrunc(storeVal, elemTy);
        }

        ctx.builder.CreateStore(storeVal, elemPtr);
        return;
    }

    // ── tensor ──
    if (tName == "tensor") {
        llvm::Value* tPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
            sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
        llvm::Value* tensorVal = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), tPtr);
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* f32 = ctx.builder.getFloatTy();
        // Coerce value to f32
        if (storeVal->getType()->isIntegerTy())
            storeVal = ctx.builder.CreateSIToFP(storeVal, f32);
        else if (storeVal->getType()->isDoubleTy())
            storeVal = ctx.builder.CreateFPTrunc(storeVal, f32);
        llvm::Function* setFn = ctx.getFunction("aiml_tensor_set", ctx.builder.getVoidTy(), {i8p, i32, f32});
        ctx.builder.CreateCall(setFn, {tensorVal, indexVals[0], storeVal});
        return;
    }

    LOG_ERROR(ctx.fmtLoc() + "idx: unsupported container type '" + tName + "'");
}

// store: [ptr_var, value, (optional)depth]  → deref-write through a pointer.
// Loadet depth-mal vom ptr-Slot, schreibt dann value an die finale Adresse.
//   depth=1:  v = load slot; store value -> v
//   depth=2:  v1 = load slot; v2 = load v1; store value -> v2
//   depth=N:  N loads then store.
void Compiler::visitStoreStmt(StoreStmt* st) {
    Symbol* sym = resolveVariable(st->target);
    if (!sym) {
        LOG_ERROR(ctx.fmtLoc() + "store: undefined pointer variable '" + st->target + "'");
        return;
    }
    // A `store` target is a pointer we deref-write through: an owning `ptr<T>` OR a borrow `ref<T>`
    // (both are target-pointer slots). A `ref<T>` out-parameter (§8: a borrow that must NOT take
    // ownership — unlike `ptr<T>` which moves) is the correct spelling for a copy-out fill, so store
    // must accept it. The pointed-to type strips one `ptr<`/`ref<` layer either way (4-char prefix).
    auto isPtrLike = [](const std::string& t){
        return (t.rfind("ptr<", 0) == 0 || t.rfind("ref<", 0) == 0) && !t.empty() && t.back() == '>';
    };
    if (!isPtrLike(sym->typeName)) {
        LOG_ERROR(ctx.fmtLoc() + "store: target '" + st->target
                  + "' is not a pointer or borrow (type=" + sym->typeName + ")");
        return;
    }
    // Determine innermost pointed-to type by stripping `depth` `ptr<...>`/`ref<...>`-layers.
    std::string remaining = sym->typeName;
    for (int i = 0; i < st->depth; ++i) {
        if (!isPtrLike(remaining)) {
            LOG_ERROR(ctx.fmtLoc() + "store: depth=" + std::to_string(st->depth)
                      + " exceeds pointer nesting of '" + sym->typeName + "'");
            return;
        }
        remaining = remaining.substr(4, remaining.size() - 5);
    }
    llvm::Type* innerTy = ctx.getLLVMType(remaining);
    if (!innerTy) {
        LOG_ERROR(ctx.fmtLoc() + "store: cannot resolve inner type '" + remaining + "'");
        return;
    }

    IRValue* val = visitExpression(st->value.get());
    if (val) consumeStringTemp(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val))); // spec/61: moved via store
    if (!val) {
        LOG_ERROR(ctx.fmtLoc() + "store: failed to evaluate value expression");
        return;
    }
    llvm::Value* storeVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));

    // Coerce primitive width if needed.
    if (storeVal->getType() != innerTy) {
        if (innerTy->isIntegerTy() && storeVal->getType()->isIntegerTy()) {
            storeVal = ctx.builder.CreateZExtOrTrunc(storeVal, innerTy, "store_coerce");
        } else if (innerTy->isFloatingPointTy() && storeVal->getType()->isIntegerTy()) {
            storeVal = ctx.builder.CreateSIToFP(storeVal, innerTy, "store_coerce_fp");
        } else if (innerTy->isFloatingPointTy() && storeVal->getType()->isFloatingPointTy()) {
            storeVal = ctx.builder.CreateFPCast(storeVal, innerTy, "store_coerce_fp");
        }
    }

    // Walk pointer-chain: start at sym->alloca (slot), then `depth` loads of ptr-type
    // give us the final write-address.
    llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
    llvm::Value* cur = sym->ir_alloca_inst
        ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst))
        : sym->alloca_inst;
    for (int i = 0; i < st->depth; ++i) {
        cur = ctx.builder.CreateLoad(ptrTy, cur, "store_chain_" + std::to_string(i));
    }

    // spec/61 §7: `store: [ref<Model>, view]` where Model is a VALUE model with heap fields (the
    // find_*_entry / copy-out out-parameter fill, out-param spelled `ref<Model>` = borrow, §8) is a
    // VALUE assignment = member-wise **SHARE**: bit-copy the struct, then **retain** (incref each
    // string/container/shared field) so dst and src co-own with a balanced refcount — each side's
    // later drop decrefs once → freed exactly once. A plain CreateStore would share WITHOUT the
    // incref → both free the same heap (the ZoneEntry/PlayerState out-param double-free,
    // test_mana_pick / find_noncombat_entry); a deep-copy would violate §7 (deep independence is
    // `clone`, not a plain store). `at`/`get` on a value-model element is a `ref` borrow (§5.4), so
    // its fields alias the container element — retain balances that aliasing. isRefcountModel (%M*)
    // is handled below.
    if (ctx.memV3 && !ctx.isRefcountModel(remaining)) {
        auto vci = ctx.classes.find(remaining);
        if (vci != ctx.classes.end() && vci->second.hasHeapFields && vci->second.serNodesVar) {
            std::string srcTn;
            IRValue* srcAddrV = emitLValueAddress(st->value.get(), srcTn);
            if (srcAddrV) {
                llvm::Value* srcAddr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(srcAddrV));
                llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
                // Self-store guard (`store: [p, *p-ish]`): freeing dst would free src.
                llvm::Value* same = ctx.builder.CreateICmpEQ(srcAddr, cur, "store_self_vm");
                llvm::Function* fn = ctx.builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* doBB   = llvm::BasicBlock::Create(ctx.context, "store_vm_share", fn);
                llvm::BasicBlock* postBB = llvm::BasicBlock::Create(ctx.context, "store_vm_post", fn);
                ctx.builder.CreateCondBr(same, postBB, doBB);
                ctx.builder.SetInsertPoint(doBB);
                // release dst's (freshly-decl'd, empty) heap first, then bit-copy + retain (share)
                llvm::Function* freeFn = ctx.getFunction("aiml_ser_free_decoded",
                    ctx.builder.getVoidTy(), {i8p, i8p, ctx.builder.getInt32Ty()});
                ctx.builder.CreateCall(freeFn, { cur, vci->second.serNodesVar,
                                                 ctx.builder.getInt32(vci->second.serNodesCount) });
                uint64_t vsz = ctx.module.getDataLayout().getTypeAllocSize(vci->second.type);
                ctx.builder.CreateMemCpy(cur, llvm::MaybeAlign(8), srcAddr, llvm::MaybeAlign(8),
                                         ctx.builder.getInt64(vsz));
                llvm::Function* rtn = ctx.getFunction("aiml_ser_retain",
                    ctx.builder.getVoidTy(), {i8p, i8p, ctx.builder.getInt32Ty()});
                ctx.builder.CreateCall(rtn, { cur, vci->second.serNodesVar,
                                              ctx.builder.getInt32(vci->second.serNodesCount) });
                ctx.builder.CreateBr(postBB);
                ctx.builder.SetInsertPoint(postBB);
                return;
            }
        }
    }

    // AIML v2: `store: [ptr<Model>, model_value]` — `*dst = src` is a VALUE assignment through the
    // pointer, so dst must end up with an INDEPENDENT copy of src's heap (the fill_header_widgets
    // deep-copy: the copy must survive the source being freed). Under v2 `src` is a %M* and `dst`
    // (`cur`) is the destination object address; a plain CreateStore would write the source POINTER
    // into dst's first field (nested lists lost). Free dst's current heap, then deep-copy src into it.
    if (ctx.isRefcountModel(remaining) && storeVal->getType()->isPointerTy()) {
        auto ci = ctx.classes.find(remaining);
        if (ci != ctx.classes.end() && ci->second.serNodesVar) {
            llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
            // Skip a self-store (`store: [p, *p]`): freeing dst would free src too.
            llvm::Value* same = ctx.builder.CreateICmpEQ(storeVal, cur, "store_self");
            llvm::Function* fn = ctx.builder.GetInsertBlock()->getParent();
            llvm::BasicBlock* doBB   = llvm::BasicBlock::Create(ctx.context, "store_deepcopy", fn);
            llvm::BasicBlock* postBB = llvm::BasicBlock::Create(ctx.context, "store_post", fn);
            ctx.builder.CreateCondBr(same, postBB, doBB);
            ctx.builder.SetInsertPoint(doBB);
            llvm::Function* freeFn = ctx.getFunction("aiml_ser_free_decoded",
                ctx.builder.getVoidTy(), {i8p, i8p, ctx.builder.getInt32Ty()});
            ctx.builder.CreateCall(freeFn, { cur, ci->second.serNodesVar,
                                             ctx.builder.getInt32(ci->second.serNodesCount) });
            llvm::Function* dcp = ctx.getFunction("aiml_ser_deep_copy",
                ctx.builder.getVoidTy(), {i8p, i8p, i8p, ctx.builder.getInt32Ty()});
            ctx.builder.CreateCall(dcp, { cur, storeVal, ci->second.serNodesVar,
                                          ctx.builder.getInt32(ci->second.serNodesCount) });
            ctx.builder.CreateBr(postBB);
            ctx.builder.SetInsertPoint(postBB);
            return;
        }
    }

    ctx.builder.CreateStore(storeVal, cur);
}

} // namespace AST
} // namespace AIML
