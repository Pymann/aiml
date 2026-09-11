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
void Compiler::visitStatement(Stmt* stmt) {
    if (stmt->sourceLine > 0) ctx.currentLine = stmt->sourceLine;
    ctx.stmtTempStack.emplace_back();   // spec/61: per-statement owned-string-temp frame
    if (auto decl = dynamic_cast<DeclStmt*>(stmt)) {
        visitDeclStmt(decl);
        if (sizetrace_active(ctx.currentFunctionName)) emitSizeTrace(ctx, decl->varName);
    }
    else if (auto setStmt = dynamic_cast<SetStmt*>(stmt)) {
        visitSetStmt(setStmt);
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

void Compiler::visitDeclStmt(DeclStmt* decl) {
    ctx.validateIdentifier(decl->varName, "variable", decl->sourceNode);

    IRType* ty = ctx.ir_gen->resolveType(decl->typeName);
    if (!ty) return;

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
        std::string elemName = tName.substr(5, tName.length() - 6);
        llvm::Type* elemTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
        if (elemTy) ListImpl::initList(ctx, unwrappedAlloca, elemTy, 4);
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
                size_t dp = fn.rfind('.');
                std::string shortName = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                if (ctx.ownsReturnFns.count(shortName) > 0) modelOwnedInit = true;
            }
        }
        if (modelOwnedInit && decl->lifetime != "persistent"
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
                    ownedInit = ctx.ownsReturnFns.count(shortName) > 0;
                }
            }
            if (ownedInit) {
                std::string boxed = decl->typeName.substr(4, decl->typeName.size() - 5);
                auto bi = ctx.classes.find(boxed);
                auto ii = ctx.interfaces.find(boxed);
                if (bi != ctx.classes.end() && bi->second.hasHeapFields && bi->second.serNodesVar
                    && bi->second.type && !ctx.classGroupContainers.count(boxed)) {
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
                }
            }
        }

        // spec/61 (A2/A3): a string local initialised from an INTERPOLATION owns a fresh
        // heap string (aiml_str_alloc) → register a decref-drop.  ONLY interpolation
        // origins (never string literals — decref reads a header before the pointer,
        // which is UB for .rodata literals; see string_runtime).  A3 move-detection
        // removes this drop if the string is later consumed (push/return/store).
        if (decl->typeName == "string" && decl->lifetime != "persistent"
            && decl->initialValue
            && dynamic_cast<StringInterpolationExpr*>(decl->initialValue.get())
            && !ctx.cleanupStack.empty()) {
            CompilerContext::ScopeDrop sd;
            sd.addr = unwrappedAlloca;
            sd.kind = 1;
            sd.ownershipId = declOid;
            sd.debugName = decl->varName; sd.debugType = decl->typeName;
            ctx.cleanupStack.back().push_back(sd);
        }

        // spec/61 (A2/A3): a FRESH list<T> value-local (no initializer → an empty list
        // initList'd above, then populated by push) owns its malloc'd data buffer → register
        // a list-drop (aiml_list_free frees the buffer; for list<string> it decref's each
        // element, which push MOVED in — see list_impl.cpp).  Conservative: only the no-init
        // case (assignment/return-initialised lists may alias and are left to leak = safe).
        // A3 move-detection removes this drop if the list is later pushed/returned/stored.
        if (decl->typeName.rfind("list<", 0) == 0 && decl->typeName.back() == '>'
            && !decl->initialValue && decl->lifetime != "persistent"
            && !decl->is_extern && !ctx.cleanupStack.empty()) {
            std::string elemName = decl->typeName.substr(5, decl->typeName.length() - 6);
            CompilerContext::ScopeDrop sd;
            sd.addr = unwrappedAlloca;
            sd.kind = 2;
            if (elemName == "string") {
                sd.elemKind = 1;                       // decref each element char*
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
                                MapImpl::put(ctx, unwrappedAlloca, kVal, v, mapStructTy);
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
                // the __vtable_<Class>_<Iface> global.
                if (decl->typeName.rfind("ptr<", 0) == 0 && decl->typeName.back() == '>') {
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
                        // initVal is a loaded struct — we need the alloca (address) instead
                        if (auto* varExpr = dynamic_cast<VariableExpr*>(decl->initialValue.get())) {
                            Symbol* srcSym = resolveVariable(varExpr->name);
                            if (srcSym) {
                                llvm::Value* addrVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
                                    srcSym->ir_alloca_inst ? srcSym->ir_alloca_inst
                                                           : ctx.ir_gen->wrap(srcSym->alloca_inst)));
                                initVal = ctx.ir_gen->wrap(addrVal);
                            }
                        }
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
    
    std::string baseName = (sepPos != std::string::npos) ? targetStr.substr(0, sepPos) : targetStr;
    std::string propName = (sepPos != std::string::npos) ? targetStr.substr(sepPos + 1) : "";

    // spec/61 §7.1 borrow-escape (WARNING — non-breaking; becomes an error under Migration A):
    // storing a borrowed ptr/ref PARAMETER into one of THIS object's members lets the pointee
    // (owned by the caller) be reached after the call returns, when it may already be gone.
    // Copy the pointee, or take ownership via a { lifetime: consumed } parameter instead.
    if (!isDash && baseName == "this" && !propName.empty() && setStmt->value) {
        if (auto* ve = dynamic_cast<VariableExpr*>(setStmt->value.get())) {
            auto vit = ctx.symbol_table.find(ve->name);
            if (vit != ctx.symbol_table.end() && vit->second.is_param
                && (vit->second.typeName.rfind("ptr<", 0) == 0
                    || vit->second.typeName.rfind("ref<", 0) == 0)) {
                LOG_WARNING(ctx.fmtLoc() + "borrow-escape: storing borrowed parameter '" + ve->name +
                    "' (" + vit->second.typeName + ") into member '<." + propName + "'. Its pointee is "
                    "owned by the caller and may not outlive this object (spec/61 §7.1). Copy it, or "
                    "take ownership with a { lifetime: consumed } parameter.");
            }
        }
    }

    Symbol* sym = resolveVariable(baseName);
    if (!sym) {
        LOG_ERROR(ctx.fmtLoc() + "Undefined variable (set target): " + targetStr);
        return;
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
                if (tci != ctx.classes.end() && tci->second.hasHeapFields && tci->second.serNodesVar
                    && ssym && ssym->typeName == sym->typeName && !ssym->is_global
                    && ssym->ir_alloca_inst) {
                    // Finding #7: this early deep-copy path returns before the generic const
                    // check below, so enforce it here — a const target must not be written.
                    if (sym->is_const) {
                        LOG_ERROR(ctx.fmtLoc() + "Cannot assign to const variable " + targetStr);
                        return;
                    }
                    // NOTE (§4.4.5, deferred): deep_copy overwrites the target without freeing
                    // its previous heap → a reassigned target leaks the old heap (under-drop,
                    // safe). A freshly-decl'd local (the §4.2 case) has none. Free-old-target
                    // + a fallible-copy contract (§4.4.6) come with the assignment-dataflow step.
                    llvm::Value* dst = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
                    llvm::Value* src = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(ssym->ir_alloca_inst));
                    llvm::Type* p = llvm::PointerType::get(ctx.context, 0);
                    llvm::Function* dc = ctx.getFunction("aiml_ser_deep_copy",
                        ctx.builder.getVoidTy(), {p, p, p, ctx.builder.getInt32Ty()});
                    ctx.builder.CreateCall(dc, { dst, src, tci->second.serNodesVar,
                                                 ctx.builder.getInt32(tci->second.serNodesCount) });
                    return;   // source keeps its drop — do NOT consumeLocalDrop
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
    if (setStmt->value && !memberDeepCopy) consumeLocalDrop(setStmt->value.get());

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
    IRValue* val = visitExpression(setStmt->value.get());
    ctx.deserializeTargetType.clear();
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
        auto classIt = ctx.classes.find(sym->typeName);
        if (classIt != ctx.classes.end() && !classIt->second.isBuiltin && classIt->second.type) {
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
                elemPtr = ListImpl::getElementPtr(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), idxVal, innerElemTy);
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
                                llvm::Function* fn = ctx.getFunction("aiml_ser_deep_copy",
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
                                llvm::Function* fn = ctx.getFunction("aiml_ser_deep_copy",
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
                if (finalTypeName == "string" && memberDeepCopy
                    && dynamic_cast<VariableExpr*>(setStmt->value.get())) {
                    llvm::Value* sv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
                    if (sv && sv->getType()->isPointerTy()) {
                        llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
                        llvm::Function* inc = ctx.getFunction("aiml_str_incref", ctx.builder.getVoidTy(), {i8p});
                        ctx.builder.CreateCall(inc, { sv });
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
    visitExpression(exprStmt->expr.get());
}
static void emitScopeDrop(CompilerContext& ctx, const CompilerContext::ScopeDrop& it);  // defined below
void Compiler::visitRetStmt(RetStmt* retStmt) {
    // spec/61 (A3): returning a local moves it to the caller → do not drop it here.
    if (retStmt->value) consumeLocalDrop(retStmt->value.get());
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
    if (auto* rv = dynamic_cast<VariableExpr*>(retStmt->value.get())) {
        std::string base = rv->name;
        size_t dot = base.find('.');
        if (dot != std::string::npos) base = base.substr(0, dot);   // `x.field` → exempt owner x
        if (Symbol* bs = resolveVariable(base)) retBaseId = bs->ownershipId;
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
    bool safeToCleanup = (retTy && retTy->isVoidTy()) || retScalar || retBaseOwned;
    auto emitReturnCleanup = [&]() {
        if (!ctx.lifetimeDropsEnabled || !retPlan || !retExit) return;
        auto* bb = static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
        if (!bb || bb->getTerminator()) return;
        for (auto fit = ctx.cleanupStack.rbegin(); fit != ctx.cleanupStack.rend(); ++fit)
            for (auto it = fit->rbegin(); it != fit->rend(); ++it) {
                if (it->ownershipId != NoOwnership && it->ownershipId == retBaseId) continue;
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
    } else if (retStmt->value) {
        IRValue* val = visitExpression(retStmt->value.get());
        // spec/61: a returned string temp is moved to the caller → not a statement temp.
        if (val) consumeStringTemp(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)));
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
                        llvm::Value* loaded = ctx.builder.CreateLoad(retTy, raw, "ret_struct_load");
                        ctx.ir_gen->createRet(ctx.ir_gen->wrap(loaded));
                    }
                    }
                }
            } else {
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
void Compiler::consumeLocalDrop(Expr* argExpr) {
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
    for (auto& frame : ctx.cleanupStack) {
        for (size_t i = 0; i < frame.size(); ) {
            if (frame[i].addr == addr) frame.erase(frame.begin() + (long)i);
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
    llvm::Value* phaseS = ctx.builder.CreateGlobalStringPtr(phase);
    llvm::Value* nameS  = ctx.builder.CreateGlobalStringPtr(name);
    if (kind == 1) {  // string local: print the char* content (shows poison/garbage directly)
        llvm::Value* sptr = ctx.builder.CreateLoad(i8p, addr, "trace_str");
        llvm::Value* fmt = ctx.builder.CreateGlobalStringPtr("[aiml-trace] %s %s (string) @%p = \"%s\"\n");
        ctx.builder.CreateCall(pf, { fmt, phaseS, nameS, addr, sptr });
    } else if (kind == 2) {  // list: element count (struct field 0) + address
        llvm::Value* sz = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), addr, "trace_len");
        llvm::Value* tyS = ctx.builder.CreateGlobalStringPtr(typeName);
        llvm::Value* fmt = ctx.builder.CreateGlobalStringPtr("[aiml-trace] %s %s (%s) len=%d @%p\n");
        ctx.builder.CreateCall(pf, { fmt, phaseS, nameS, tyS, sz, addr });
    } else if (kind == 4) {  // owned new-box: the alloca holds a heap pointer → load + print it
        llvm::Value* bptr = ctx.builder.CreateLoad(i8p, addr, "trace_box");
        llvm::Value* tyS = ctx.builder.CreateGlobalStringPtr(typeName.empty() ? "?" : typeName);
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
        llvm::Value* tyS = ctx.builder.CreateGlobalStringPtr(typeName.empty() ? "?" : typeName);
        llvm::Value* fmt = ctx.builder.CreateGlobalStringPtr("[aiml-trace] %s %s (%s) iface_obj@%p\n");
        ctx.builder.CreateCall(pf, { fmt, phaseS, nameS, tyS, objp });
    } else {  // model / map / other: address (+ type)
        llvm::Value* tyS = ctx.builder.CreateGlobalStringPtr(typeName.empty() ? "?" : typeName);
        llvm::Value* fmt = ctx.builder.CreateGlobalStringPtr("[aiml-trace] %s %s (%s) @%p\n");
        ctx.builder.CreateCall(pf, { fmt, phaseS, nameS, tyS, addr });
    }
}

static void emitScopeDrop(CompilerContext& ctx, const CompilerContext::ScopeDrop& it) {
    llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
    if (aimlTraceSymbol(it.debugName))       // AIML_TRACE_SYMBOL: value right before the free
        emitSymbolValueTrace(ctx, "DROP", it.debugName, it.kind, it.debugType, it.addr);
    if (it.kind == 1) {
        llvm::Value* sptr = ctx.builder.CreateLoad(i8p, it.addr, "drop_str");
        llvm::Function* dc = ctx.getFunction("aiml_str_decref", ctx.builder.getVoidTy(), {i8p});
        ctx.builder.CreateCall(dc, { sptr });
    } else if (it.kind == 2) {
        llvm::Value* nodesArg = it.serNodes
            ? static_cast<llvm::Value*>(it.serNodes)
            : llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p));
        llvm::Function* lf = ctx.getFunction("aiml_list_free", ctx.builder.getVoidTy(),
            {i8p, ctx.builder.getInt32Ty(), ctx.builder.getInt64Ty(), i8p, ctx.builder.getInt32Ty()});
        ctx.builder.CreateCall(lf, { it.addr, ctx.builder.getInt32(it.elemKind),
                                     ctx.builder.getInt64(it.elemSize), nodesArg,
                                     ctx.builder.getInt32(it.serNodesCount) });
    } else if (it.kind == 3) {
        llvm::Value* vNodes = it.serNodes
            ? static_cast<llvm::Value*>(it.serNodes)
            : llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p));
        llvm::Function* mf = ctx.getFunction("aiml_map_free", ctx.builder.getVoidTy(),
            {i8p, ctx.builder.getInt32Ty(), ctx.builder.getInt64Ty(),
             ctx.builder.getInt32Ty(), ctx.builder.getInt64Ty(), i8p, ctx.builder.getInt32Ty()});
        ctx.builder.CreateCall(mf, { it.addr,
            ctx.builder.getInt32(it.keyKind), ctx.builder.getInt64(it.keyStride),
            ctx.builder.getInt32(it.elemKind), ctx.builder.getInt64(it.elemSize),
            vNodes, ctx.builder.getInt32(it.serNodesCount) });
    } else if (it.kind == 4) {
        // owned new-box: the alloca holds a heap pointer (calloc'd in visitNewExpr).
        // Load it, deep-free the pointee's heap fields, then free the shell.
        llvm::Value* boxptr = ctx.builder.CreateLoad(i8p, it.addr, "drop_box");
        llvm::Function* fn = ctx.getFunction("aiml_ser_free_decoded",
            ctx.builder.getVoidTy(), {i8p, i8p, ctx.builder.getInt32Ty()});
        ctx.builder.CreateCall(fn, { boxptr, it.serNodes, ctx.builder.getInt32(it.serNodesCount) });
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
                if (plan && exit && it->ownershipId != NoOwnership
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
        llvm::Value* elemPtr = ListImpl::getElementPtr(ctx, listPtr, indexVals[0], elemTy);

        // struct elements + pointer-typed source: memcpy the whole struct.
        // Without this, CreateStore would only write pointer-size bytes
        // (typically 8) into a slot meant for the full struct (e.g. 24+
        // bytes for a model with multiple fields), corrupting both the
        // list element and adjacent heap memory.
        if (elemTy->isStructTy() && storeVal->getType()->isPointerTy()) {
            uint64_t sz = ctx.module.getDataLayout().getTypeAllocSize(elemTy);
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* memcpyFunc = ctx.getFunction("memcpy",
                ptrTy, {ptrTy, ptrTy, ctx.getSizeType()});
            ctx.builder.CreateCall(memcpyFunc,
                {elemPtr, storeVal, ctx.getSizeConst(sz)});
            return;
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
    if (sym->typeName.rfind("ptr<", 0) != 0 || sym->typeName.back() != '>') {
        LOG_ERROR(ctx.fmtLoc() + "store: target '" + st->target
                  + "' is not a pointer (type=" + sym->typeName + ")");
        return;
    }
    // Determine innermost pointed-to type by stripping `depth` `ptr<...>`-layers.
    std::string remaining = sym->typeName;
    for (int i = 0; i < st->depth; ++i) {
        if (remaining.rfind("ptr<", 0) != 0 || remaining.back() != '>') {
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
    ctx.builder.CreateStore(storeVal, cur);
}

} // namespace AST
} // namespace AIML
