#define DEBUG_MODE
#include "ast_compiler.h"
#include "ast_builder.h"
#include "ownership_analyzer.h"   // spec/61 §4: per-function ownership plan
#include "borrow_check.h"         // spec/61 §10: memV3-only ownership diagnostics
#include <limits>
#include "asset_scanner.h"
#include "builtin_registry.h"
#include "fptable.h"
#include "schema_registry.h"
#include "list_impl.h"
#include "map_impl.h"
#include "buffer_impl.h"
#include "chan_impl.h"
#include "string_utils.h"
#include <algorithm>
#include <functional>
#include <set>
#include <iostream>
#include "../sys/ser_runtime.h"  // AimlTypeNode, AimlTDKind (spec 45)
#include "container_abi.h"       // target-ABI SerList/SerMap/SerBuffer header sizes
#include "../sys/trace_runtime.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>

namespace AIML {
namespace AST {

// ── WASM struct-return sret stub (2026-06-05) ─────────────────────────────────
// Indirect calls through interface vtables suffer an ABI mismatch on WASM when
// the method returns a struct >8 bytes: LLVM auto-lowers the direct-call form
// to sret-out-pointer at codegen, but the IR-level indirect-call signature has
// no auto-lowering — call site uses i64-packed return, callee uses sret-out.
// The struct's data pointer ends up uninitialised → first access traps.
// Native x86-64 happens to pack 12-byte structs into rax:rdx on both sides, so
// the bug only manifests on WASM.  Repro: tests/test_iface_list_return.aiml.
//
// Fix: for every struct-returning method placed in a vtable, emit a SRET STUB
// wrapper.  The stub takes (ptr sret(retTy), originalParams...) and forwards
// to the real method directly (LLVM handles the direct-call ABI consistently
// for both sides).  Vtable stores the stub; indirect callers see a clean
// sret signature.  Direct calls (`c.method()`) bypass the stub.
static llvm::Function* getOrCreateSretStub(CompilerContext& ctx,
                                           llvm::Function* realFn) {
    std::string stubName = realFn->getName().str() + "__sret_stub";
    if (auto* existing = ctx.module.getFunction(stubName)) return existing;

    llvm::FunctionType* realTy = realFn->getFunctionType();
    llvm::Type* retTy = realTy->getReturnType();

    // Build stub signature: void(ptr sret(retTy), realParams...).
    std::vector<llvm::Type*> stubParams;
    auto* i8p = llvm::PointerType::get(ctx.context, 0);
    stubParams.push_back(i8p);                              // sret slot
    for (auto* t : realTy->params()) stubParams.push_back(t);
    auto* stubTy = llvm::FunctionType::get(
        llvm::Type::getVoidTy(ctx.context), stubParams, false);

    auto* stub = llvm::Function::Create(
        stubTy, llvm::GlobalValue::PrivateLinkage, stubName, ctx.module);
    stub->addParamAttr(0, llvm::Attribute::getWithStructRetType(ctx.context, retTy));

    // Body: forward args to real fn, copy result through sret via memcpy.
    // memcpy avoids any ABI-lowering ambiguity around `store %struct, ptr` on
    // WASM — the load+store sequence on a 12+ byte struct can get partially
    // lost when the direct call's return gets demoted to sret while the store
    // expects a value.  Allocating an explicit tmp slot + memcpy-ing the bytes
    // forces a deterministic byte-for-byte copy regardless of how LLVM lowers
    // the direct call.
    auto savedIP = ctx.builder.saveIP();
    auto* entry = llvm::BasicBlock::Create(ctx.context, "entry", stub);
    ctx.builder.SetInsertPoint(entry);

    std::vector<llvm::Value*> realArgs;
    auto argIt = stub->arg_begin();
    llvm::Value* sretSlot = &*argIt++;
    for (; argIt != stub->arg_end(); ++argIt) realArgs.push_back(&*argIt);

    llvm::Value* tmpSlot = ctx.builder.CreateAlloca(retTy, nullptr, "stub_tmp");
    llvm::Value* res = ctx.builder.CreateCall(realFn, realArgs);
    ctx.builder.CreateStore(res, tmpSlot);

    auto& DL = ctx.module.getDataLayout();
    uint64_t structSize = DL.getTypeAllocSize(retTy);
    ctx.builder.CreateMemCpy(sretSlot, llvm::MaybeAlign(4),
                             tmpSlot,  llvm::MaybeAlign(4),
                             structSize);
    ctx.builder.CreateRetVoid();

    ctx.builder.restoreIP(savedIP);
    return stub;
}

// spec/61 return-ownership (interface boxes): synthesize once `void __free_<Class>(i8* obj)`
// for a concrete class C — deep-free C's heap fields (aiml_ser_free_decoded with C's
// __aiml_sernodes_C, only when C has heap fields + a ser subtree and is not a classgroup
// container), then free(obj) the shell. Placed in the trailing slot of every interface vtable
// C implements so an owned `ptr<Iface>` fat-pointer box can be deep-freed by virtual dispatch
// on its concrete type (kind-5 ScopeDrop). Idempotent (one per class, reused across interfaces).
static llvm::Function* getOrCreateClassFree(CompilerContext& ctx, const std::string& className) {
    std::string name = "__free_" + className;
    if (auto* existing = ctx.module.getFunction(name)) return existing;
    auto* i8p = llvm::PointerType::get(ctx.context, 0);
    auto* fnTy = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx.context), {i8p}, false);
    auto* fn = llvm::Function::Create(
        fnTy, llvm::GlobalValue::LinkOnceODRLinkage, name, ctx.module);
    auto savedIP = ctx.builder.saveIP();
    auto* entry = llvm::BasicBlock::Create(ctx.context, "entry", fn);
    ctx.builder.SetInsertPoint(entry);
    llvm::Value* obj = &*fn->arg_begin();
    auto ci = ctx.classes.find(className);
    bool deepFree = (ci != ctx.classes.end() && ci->second.hasHeapFields
                     && ci->second.serNodesVar && !ctx.classGroupContainers.count(className));
    if (deepFree) {
        auto* sf = ctx.getFunction("aiml_ser_free_decoded", ctx.builder.getVoidTy(),
                                   {i8p, i8p, ctx.builder.getInt32Ty()});
        ctx.builder.CreateCall(sf, { obj,
            static_cast<llvm::Value*>(ci->second.serNodesVar),
            ctx.builder.getInt32(ci->second.serNodesCount) });
    }
    auto* freeFn = ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p});
    ctx.builder.CreateCall(freeFn, { obj });
    ctx.builder.CreateRetVoid();
    ctx.builder.restoreIP(savedIP);
    return fn;
}

// Recursively initialises container-typed members (list/map/chan/buffer) of
// a class instance at `basePtr`.  See ast_compiler.h.
void Compiler::initClassContainerMembers(llvm::Value* basePtr,
                                         const std::string& className) {
    auto it = ctx.classes.find(className);
    if (it == ctx.classes.end()) return;
    auto& info = it->second;
    if (!info.type) return;  // builtin / opaque — no struct layout to walk
    for (const auto& [memberName, m] : info.members) {
        const std::string& mt = m.typeName;
        llvm::Value* fieldPtr = ctx.builder.CreateStructGEP(
            info.type, basePtr, m.index, memberName + "_init_gep");
        if (mt.rfind("list<", 0) == 0 && mt.back() == '>') {
            std::string elemName = mt.substr(5, mt.length() - 6);
            // AIML v2 P3/P6: a list<Model> field stores %M* references → pointer element stride.
            llvm::Type* elemTy = ctx.isRefcountModel(elemName)
                ? llvm::PointerType::get(ctx.context, 0)
                : static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
            if (ctx.memV3) {
                // spec/61 §5 Option B: the field is a POINTER to a refcounted handle — allocate the
                // handle, store the pointer in the field slot, then initList on the handle's struct.
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* nw = ctx.getFunction("aiml_list_handle_new", i8p, {});
                llvm::Value* handle = ctx.builder.CreateCall(nw, {}, "fld_listh_new");
                ctx.builder.CreateStore(handle, fieldPtr);
                if (elemTy) ListImpl::initList(ctx, handle, elemTy, 4);
            } else if (elemTy) {
                ListImpl::initList(ctx, fieldPtr, elemTy, 4);
            }
        } else if (mt.rfind("map<", 0) == 0 && mt.back() == '>') {
            std::string inner = mt.substr(4, mt.length() - 5);
            size_t colon = inner.find(':');
            if (colon != std::string::npos) {
                std::string kName = inner.substr(0, colon);
                std::string vName = inner.substr(colon + 1);
                llvm::Type* kTy = static_cast<llvm::Type*>(
                    ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(kName)));
                llvm::Type* vTy = ctx.isRefcountModel(vName)
                    ? ctx.builder.getInt8Ty()->getPointerTo()
                    : static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(vName)));
                if (ctx.memV3 && kTy && vTy) {
                    // spec/61 §5 Option B: map field is a POINTER to a handle — alloc + store ptr.
                    auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Function* nw = ctx.getFunction("aiml_map_handle_new", i8p, {});
                    llvm::Value* handle = ctx.builder.CreateCall(nw, {}, "fld_maph_new");
                    ctx.builder.CreateStore(handle, fieldPtr);
                    MapImpl::initMap(ctx, handle, kTy, vTy, 4);
                } else if (kTy && vTy) {
                    MapImpl::initMap(ctx, fieldPtr, kTy, vTy, 4);
                }
            }
        } else if (mt.rfind("chan<", 0) == 0 && mt.back() == '>') {
            ChanImpl::init(ctx, fieldPtr, ctx.builder.getInt64(0));
        } else if (mt == "buffer" || (mt.rfind("buffer<", 0) == 0 && mt.back() == '>')) {
            // Bare `buffer` (untyped) as well as `buffer<T>` — both share one
            // struct layout and need alloc.  (visitNewExpr's heap path matched
            // both; keep this in sync so `{new:}` via initClassContainerMembers
            // inits bare-buffer members too.  Repro: test_buffer_class_member_heap_init_repro.)
            if (ctx.memV3) {
                // spec/61 §5 Option B: buffer field is a POINTER to a handle — alloc + store ptr.
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* nw = ctx.getFunction("aiml_buffer_handle_new", i8p, {});
                llvm::Value* handle = ctx.builder.CreateCall(nw, {}, "fld_bufh_new");
                ctx.builder.CreateStore(handle, fieldPtr);
                BufferImpl::alloc(ctx, handle, ctx.builder.getInt32(4096));
            } else {
                BufferImpl::alloc(ctx, fieldPtr, ctx.builder.getInt32(4096));
            }
        }
        // AIML v2 P6: a direct refcount-MODEL field is a %M* pointer slot (not an inline struct).
        else if (ctx.isRefcountModel(mt)) {
            // A `ptr<Model>` / `ref<Model>` field is a BORROW/optional reference → leave it null
            // (like v1); it is populated by an explicit set/decode, and this also breaks the
            // recursion of self/mutually-referential models (cycles MUST go through ptr<>).
            bool isPtrField = (mt.rfind("ptr<", 0) == 0 || mt.rfind("ref<", 0) == 0);
            if (!isPtrField) {
                // A BARE value-model field must EXIST after construction (v1 had it inline /
                // default-constructed). Under v2 the slot holds a %M*, so allocate a fresh
                // refcounted instance (refcount 1, owned by this parent — freed by the parent's
                // AIML_TD_MODEL decref) and recurse to init ITS containers. Value-model nesting is
                // finite + acyclic (the struct layout forbids an inline cycle; cycles use ptr<>),
                // so this terminates. Without it, reading `parent.field.x` derefs NULL → SIGSEGV
                // (probe: a model with a direct value-model field).
                auto ci = ctx.classes.find(mt);
                if (ci != ctx.classes.end() && ci->second.type) {
                    auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    uint64_t allocSize = (ci->second.padded_size > 0)
                        ? (uint64_t)ci->second.padded_size
                        : ctx.module.getDataLayout().getTypeAllocSize(ci->second.type);
                    llvm::Function* allocFn = ctx.getFunction("aiml_model_alloc", i8p, { ctx.getSizeType() });
                    llvm::Value* obj = ctx.builder.CreateCall(allocFn, { ctx.getSizeConst(allocSize) }, "field_model_alloc");
                    ctx.builder.CreateStore(obj, fieldPtr);
                    initClassContainerMembers(obj, mt);
                }
            }
            // else: ptr<Model>/ref<Model> — null reference by default.
        }
        // Nested class instances: recurse so deeply-embedded containers init too.
        else if (ctx.classes.count(mt) && !ctx.classes[mt].isBuiltin) {
            initClassContainerMembers(fieldPtr, mt);
        }
        // String member without an explicit default: initialise to the empty
        // string, NOT a null pointer.  A string field is an i8* and `.len`
        // lowers to strlen(ptr) — an unset (zero/null) field segfaults on any
        // access.  Mirrors list/map fields defaulting to empty containers so a
        // freshly-decl'd model is immediately safe to read.
        // Repro: reading `.len` of an unset model string field crashed the
        // DeckEditor (build_card_text on a card whose ability_text was never set).
        else if (mt == "string" && !m.initValue) {
            // spec/61: default an unset model string field to the IMMORTAL refcounted "" (not a
            // raw .rodata literal). `.len`/strlen still see "" (safe to read), and drops-on's
            // aiml_ser_free_decoded can decref it safely (no-op) — a raw literal here segfaults at
            // free (decref reading its `-8` header). Repro: test_add_life_clamp under AIML_LIFETIME_DROPS=1.
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* emptyFn = ctx.getFunction("aiml_str_empty", i8p, {});
            llvm::Value* emptyStr = ctx.builder.CreateCall(emptyFn, {}, "str_empty_default");
            ctx.builder.CreateStore(emptyStr, fieldPtr);
        }
        // Scalar / pointer member with an explicit per-member default: apply
        // it.  Without this, `decl: [g_app, MyClass, null, {extern: true}]`
        // wins over the class declaration's `[counter, i32, 42]` because the
        // `null` triggers a zero-init that wipes the implicit defaults.
        // Repro: tests/test_class_global_extern_defaults.aiml
        else if (m.initValue) {
            IRValue* v = visitExpression(m.initValue.get());
            if (v) {
                llvm::Value* raw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v));
                if (raw) {
                    // spec/61: a STRING member with a literal default (e.g. `[artwork_id, string, ""]`)
                    // would store a raw .rodata pointer, which drops-on's aiml_ser_free_decoded then
                    // decref's → segfault (reading the literal's `-8` header). Dup the literal into a
                    // refcounted, decref-safe string (aiml_str_dup("") → an owned ""), like assigned
                    // string values. Non-string defaults store as-is.
                    if (mt == "string" && raw->getType()->isPointerTy()) {
                        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                        llvm::Function* dupFn = ctx.getFunction("aiml_str_dup", i8p, {i8p});
                        raw = ctx.builder.CreateCall(dupFn, { raw }, "str_default_dup");
                    }
                    ctx.builder.CreateStore(raw, fieldPtr);
                }
            }
        }
    }
}

// Static check: string-literal comparison (enum/regex hints).
// Comparing the SAME variable against string literals (eq/neq) >=2 times is a
// hand-rolled string switch → hard error (hint: enum / polymorphic dispatch).
// A SINGLE string comparison is a warning (hint: regular expression).  Only
// quoted string literals count — unquoted identifiers (incl. enum members like
// GamePhase.x) are VariableExpr and never trigger.  Per-function opt-out via
// `static_off: ["str_cmp"]`.  Runs for entry AND imported (libraryMode) files.
void Compiler::checkStringComparisons(Program* prog) {
    if (!prog) return;
    auto isStrLit = [](Expr* e) -> bool {
        auto* lit = dynamic_cast<LiteralExpr*>(e);
        return lit && lit->type == "string";
    };
    std::function<void(Expr*, std::map<std::string, int>&)> scanExpr;
    std::function<void(Stmt*, std::map<std::string, int>&)> scanStmt;
    scanExpr = [&](Expr* e, std::map<std::string, int>& counts) {
        if (!e) return;
        if (auto* bin = dynamic_cast<BinaryExpr*>(e)) {
            if (bin->op == "eq" || bin->op == "neq") {
                Expr* l = bin->lhs.get();
                Expr* r = bin->rhs.get();
                VariableExpr* var = nullptr;
                if (dynamic_cast<VariableExpr*>(l) && isStrLit(r))
                    var = static_cast<VariableExpr*>(l);
                else if (dynamic_cast<VariableExpr*>(r) && isStrLit(l))
                    var = static_cast<VariableExpr*>(r);
                if (var) counts[var->name]++;
            }
            scanExpr(bin->lhs.get(), counts);
            scanExpr(bin->rhs.get(), counts);
        } else if (auto* un = dynamic_cast<UnaryExpr*>(e)) {
            scanExpr(un->operand.get(), counts);
        } else if (auto* call = dynamic_cast<CallExpr*>(e)) {
            for (auto& a : call->args) scanExpr(a.get(), counts);
        } else if (auto* interp = dynamic_cast<StringInterpolationExpr*>(e)) {
            for (auto& p : interp->parts) scanExpr(p.get(), counts);
        } else if (auto* ne = dynamic_cast<NewExpr*>(e)) {
            for (auto& kv : ne->initValues) scanExpr(kv.second.get(), counts);
        } else if (auto* na = dynamic_cast<NewArrayExpr*>(e)) {
            scanExpr(na->countExpr.get(), counts);
        } else if (auto* sp = dynamic_cast<SpawnExpr*>(e)) {
            for (auto& a : sp->args) scanExpr(a.get(), counts);
        } else if (auto* at = dynamic_cast<AtExpr*>(e)) {
            scanExpr(at->container.get(), counts);
            for (auto& i : at->indices) scanExpr(i.get(), counts);
        } else if (auto* bl = dynamic_cast<BufferLiteralExpr*>(e)) {
            for (auto& el : bl->elements) scanExpr(el.get(), counts);
        } else if (auto* dl = dynamic_cast<DictLiteralExpr*>(e)) {
            for (auto& kv : dl->entriesExpr) scanExpr(kv.second.get(), counts);
        }
    };
    scanStmt = [&](Stmt* s, std::map<std::string, int>& counts) {
        if (!s) return;
        if (auto* b = dynamic_cast<BlockStmt*>(s)) {
            for (auto& st : b->statements) scanStmt(st.get(), counts);
        } else if (auto* d = dynamic_cast<DeclStmt*>(s)) {
            scanExpr(d->initialValue.get(), counts);
        } else if (auto* st = dynamic_cast<SetStmt*>(s)) {
            scanExpr(st->value.get(), counts);
        } else if (auto* so = dynamic_cast<StoreStmt*>(s)) {
            scanExpr(so->value.get(), counts);
        } else if (auto* ix = dynamic_cast<IdxStmt*>(s)) {
            for (auto& i : ix->indices) scanExpr(i.get(), counts);
            scanExpr(ix->value.get(), counts);
        } else if (auto* es = dynamic_cast<ExprStmt*>(s)) {
            scanExpr(es->expr.get(), counts);
        } else if (auto* iff = dynamic_cast<IfStmt*>(s)) {
            scanExpr(iff->condition.get(), counts);
            scanStmt(iff->thenBranch.get(), counts);
            scanStmt(iff->elseBranch.get(), counts);
        } else if (auto* wh = dynamic_cast<WhileStmt*>(s)) {
            scanExpr(wh->condition.get(), counts);
            scanStmt(wh->body.get(), counts);
        } else if (auto* rs = dynamic_cast<RetStmt*>(s)) {
            scanExpr(rs->value.get(), counts);
        } else if (auto* th = dynamic_cast<ThrowStmt*>(s)) {
            scanExpr(th->value.get(), counts);
        } else if (auto* tr = dynamic_cast<TryStmt*>(s)) {
            scanStmt(tr->tryBlock.get(), counts);
            scanStmt(tr->catchBlock.get(), counts);
        }
    };
    auto checkFn = [&](FunctionDecl* fn) {
        if (!fn || !fn->body) return;
        for (const auto& s : fn->staticOff)
            if (s == "str_cmp") return;
        std::map<std::string, int> counts;
        scanStmt(fn->body.get(), counts);
        for (const auto& [var, n] : counts) {
            if (n >= 2) {
                LOG_ERROR(ctx.fmtLoc(fn->sourceNode) + "variable '" + var +
                    "' is compared against string literals " + std::to_string(n) +
                    " times in '" + fn->name + "' — this hand-rolled string switch "
                    "should be an enum or polymorphic dispatch.  Add "
                    "`static_off: [\"str_cmp\"]` to this def to bypass.");
            } else {
                LOG_WARNING(ctx.fmtLoc(fn->sourceNode) + "variable '" + var +
                    "' is compared against a string literal in '" + fn->name +
                    "' — consider a regular expression for string matching.");
            }
        }
    };
    for (const auto& d : prog->declarations) {
        if (auto* fn = dynamic_cast<FunctionDecl*>(d.get())) {
            checkFn(fn);
        } else if (auto* cd = dynamic_cast<ClassDecl*>(d.get())) {
            for (const auto& m : cd->methods) checkFn(m.get());
        }
    }
}

// Static check (warning): pointer-indirection mismatch on call arguments.  Every
// variable-based argument (plain var, `&`/`&&` addr-of, `?`/`??` deref) has a
// SOURCE pointer-depth (count of `ptr<` wrappers, adjusted by &/?).  For free
// functions we know the parameter's declared depth and warn on OVER-indirection
// (arg deeper than param → the callee dereferences too few levels and reads/writes
// through a bogus address).  UNDER-indirection is left alone — passing a value
// where a `ptr` is expected is the legitimate implicit by-ref pass.  Method /
// cross-file / builtin calls can't be resolved per-file, so they fall back to the
// callee-independent `&(already-pointer)` heuristic (same over-indirection, minus
// the exact param).  LLVM opaque pointers hide all this, hence a source-level pass.
// Per-function opt-out via `static_off: ["ptr_indirection"]`.
// Real-world repro: record_death({'&':[mat]}, ...) where mat is ptr<MatchSession>.
void Compiler::checkPtrIndirection(Program* prog) {
    if (!prog) return;
    // ptr-depth of a declared type = number of leading `ptr<` wrappers.
    auto ptrDepth = [](const std::string& t) {
        int d = 0; size_t p = 0;
        while (t.compare(p, 4, "ptr<") == 0) { d++; p += 4; }
        return d;
    };
    // Signatures of free functions (no receiver) → param type list, for the exact
    // depth compare.  Methods are keyed by receiver at the call site (dotted
    // functionName) and can't be resolved here, so they use the heuristic path.
    std::map<std::string, std::vector<std::string>> freeSigs;
    for (const auto& d : prog->declarations)
        if (auto* fn = dynamic_cast<FunctionDecl*>(d.get())) {
            std::vector<std::string> ps;
            for (const auto& a : fn->args) ps.push_back(a.second);
            freeSigs[fn->name] = std::move(ps);
        }
    // Pass 1: collect declared variable types (params + all decls in the body).
    std::function<void(Stmt*, std::map<std::string, std::string>&)> collectDecls;
    collectDecls = [&](Stmt* s, std::map<std::string, std::string>& vt) {
        if (!s) return;
        if (auto* b = dynamic_cast<BlockStmt*>(s)) {
            for (auto& st : b->statements) collectDecls(st.get(), vt);
        } else if (auto* d = dynamic_cast<DeclStmt*>(s)) {
            if (!d->typeName.empty()) vt[d->varName] = d->typeName;
        } else if (auto* iff = dynamic_cast<IfStmt*>(s)) {
            collectDecls(iff->thenBranch.get(), vt);
            collectDecls(iff->elseBranch.get(), vt);
        } else if (auto* wh = dynamic_cast<WhileStmt*>(s)) {
            collectDecls(wh->body.get(), vt);
        } else if (auto* tr = dynamic_cast<TryStmt*>(s)) {
            collectDecls(tr->tryBlock.get(), vt);
            collectDecls(tr->catchBlock.get(), vt);
        }
    };
    // Source pointer-depth of an argument expression, or INT_MIN when it can't be
    // determined statically (complex expr, unknown variable) → caller skips it.
    const int UNKNOWN = std::numeric_limits<int>::min();
    std::function<int(Expr*, std::map<std::string, std::string>&, std::string&)> argDepth;
    argDepth = [&](Expr* e, std::map<std::string, std::string>& vt,
                   std::string& baseVar) -> int {
        if (auto* var = dynamic_cast<VariableExpr*>(e)) {
            auto it = vt.find(var->name);
            if (it == vt.end()) return UNKNOWN;
            baseVar = var->name;
            return ptrDepth(it->second);
        }
        if (auto* un = dynamic_cast<UnaryExpr*>(e)) {
            bool allAmp = un->op.find_first_not_of('&') == std::string::npos;
            bool allDeref = un->op.find_first_not_of('?') == std::string::npos;
            if (allAmp || allDeref) {
                int inner = argDepth(un->operand.get(), vt, baseVar);
                if (inner == UNKNOWN) return UNKNOWN;
                return inner + (allAmp ? (int)un->op.size() : -(int)un->op.size());
            }
        }
        return UNKNOWN;
    };
    // Pass 2: scan every CallExpr argument for an indirection mismatch.
    std::function<void(Expr*, std::map<std::string, std::string>&)> scanExpr;
    std::function<void(Stmt*, std::map<std::string, std::string>&)> scanStmt;
    auto checkArgs = [&](const std::string& fname,
                         std::vector<std::unique_ptr<Expr>>& args,
                         const YAML::Node& loc,
                         std::map<std::string, std::string>& vt) {
        bool isMethod = (fname.find('.') != std::string::npos);
        auto sig = freeSigs.find(fname);
        bool haveSig = (!isMethod && sig != freeSigs.end());
        for (size_t i = 0; i < args.size(); ++i) {
            std::string baseVar;
            int ad = argDepth(args[i].get(), vt, baseVar);
            if (ad == UNKNOWN) continue;
            if (haveSig && i < sig->second.size()) {
                int pd = ptrDepth(sig->second[i]);
                if (ad > pd) {
                    LOG_WARNING(ctx.fmtLoc(loc) + "call to '" + fname +
                        "': argument " + std::to_string(i + 1) + " ('" + baseVar +
                        "') is over-indirected — it has pointer-depth " +
                        std::to_string(ad) + " but the parameter expects depth " +
                        std::to_string(pd) + " (" + sig->second[i] + ").  The callee "
                        "will dereference too few levels; drop a '&' or add a '?'.  "
                        "Suppress with `static_off: [\"ptr_indirection\"]`.");
                }
            } else {
                // Unresolvable callee (method/builtin/cross-file): the only safe
                // over-indirection signal is `&x` where x is already a pointer.
                auto* un = dynamic_cast<UnaryExpr*>(args[i].get());
                if (un && un->op.find_first_not_of('&') == std::string::npos &&
                    !baseVar.empty() && ad > (int)un->op.size()) {
                    LOG_WARNING(ctx.fmtLoc(loc) + "call to '" + fname +
                        "': `&" + baseVar + "` takes the address of '" + baseVar +
                        "' which is already a pointer (depth " +
                        std::to_string(ad - (int)un->op.size()) + ") — this passes an "
                        "over-indirected pointer (depth " + std::to_string(ad) +
                        ").  Pass '" + baseVar + "' directly, or suppress with "
                        "`static_off: [\"ptr_indirection\"]`.");
                }
            }
        }
    };
    scanExpr = [&](Expr* e, std::map<std::string, std::string>& vt) {
        if (!e) return;
        if (auto* call = dynamic_cast<CallExpr*>(e)) {
            checkArgs(call->functionName, call->args, call->sourceNode, vt);
            for (auto& a : call->args) scanExpr(a.get(), vt);
        } else if (auto* sp = dynamic_cast<SpawnExpr*>(e)) {
            checkArgs(sp->functionName, sp->args, sp->sourceNode, vt);
            for (auto& a : sp->args) scanExpr(a.get(), vt);
        } else if (auto* bin = dynamic_cast<BinaryExpr*>(e)) {
            scanExpr(bin->lhs.get(), vt); scanExpr(bin->rhs.get(), vt);
        } else if (auto* un = dynamic_cast<UnaryExpr*>(e)) {
            scanExpr(un->operand.get(), vt);
        } else if (auto* interp = dynamic_cast<StringInterpolationExpr*>(e)) {
            for (auto& p : interp->parts) scanExpr(p.get(), vt);
        } else if (auto* ne = dynamic_cast<NewExpr*>(e)) {
            for (auto& kv : ne->initValues) scanExpr(kv.second.get(), vt);
        } else if (auto* at = dynamic_cast<AtExpr*>(e)) {
            scanExpr(at->container.get(), vt);
            for (auto& i : at->indices) scanExpr(i.get(), vt);
        } else if (auto* bl = dynamic_cast<BufferLiteralExpr*>(e)) {
            for (auto& el : bl->elements) scanExpr(el.get(), vt);
        } else if (auto* dl = dynamic_cast<DictLiteralExpr*>(e)) {
            for (auto& kv : dl->entriesExpr) scanExpr(kv.second.get(), vt);
        }
    };
    scanStmt = [&](Stmt* s, std::map<std::string, std::string>& vt) {
        if (!s) return;
        if (auto* b = dynamic_cast<BlockStmt*>(s)) {
            for (auto& st : b->statements) scanStmt(st.get(), vt);
        } else if (auto* d = dynamic_cast<DeclStmt*>(s)) {
            scanExpr(d->initialValue.get(), vt);
        } else if (auto* st = dynamic_cast<SetStmt*>(s)) {
            scanExpr(st->value.get(), vt);
        } else if (auto* so = dynamic_cast<StoreStmt*>(s)) {
            scanExpr(so->value.get(), vt);
        } else if (auto* ix = dynamic_cast<IdxStmt*>(s)) {
            for (auto& i : ix->indices) scanExpr(i.get(), vt);
            scanExpr(ix->value.get(), vt);
        } else if (auto* es = dynamic_cast<ExprStmt*>(s)) {
            scanExpr(es->expr.get(), vt);
        } else if (auto* iff = dynamic_cast<IfStmt*>(s)) {
            scanExpr(iff->condition.get(), vt);
            scanStmt(iff->thenBranch.get(), vt);
            scanStmt(iff->elseBranch.get(), vt);
        } else if (auto* wh = dynamic_cast<WhileStmt*>(s)) {
            scanExpr(wh->condition.get(), vt);
            scanStmt(wh->body.get(), vt);
        } else if (auto* rs = dynamic_cast<RetStmt*>(s)) {
            scanExpr(rs->value.get(), vt);
        } else if (auto* th = dynamic_cast<ThrowStmt*>(s)) {
            scanExpr(th->value.get(), vt);
        } else if (auto* tr = dynamic_cast<TryStmt*>(s)) {
            scanStmt(tr->tryBlock.get(), vt);
            scanStmt(tr->catchBlock.get(), vt);
        }
    };
    auto checkFn = [&](FunctionDecl* fn) {
        if (!fn || !fn->body) return;
        for (const auto& s : fn->staticOff) if (s == "ptr_indirection") return;
        std::map<std::string, std::string> vt;
        for (const auto& [n, t] : fn->args) vt[n] = t;
        collectDecls(fn->body.get(), vt);
        scanStmt(fn->body.get(), vt);
    };
    for (const auto& d : prog->declarations) {
        if (auto* fn = dynamic_cast<FunctionDecl*>(d.get())) {
            checkFn(fn);
        } else if (auto* cd = dynamic_cast<ClassDecl*>(d.get())) {
            for (const auto& m : cd->methods) checkFn(m.get());
        }
    }
}

// Static check: a *blocking* primitive (a thread-suspending builtin —
// session.send_and_await / time.sleep_ms / time.sleep) whose enclosing def is
// statically reachable, over the direct-call graph, from a per-frame `engine.run`
// update/render callback stalls the render loop — under WASM asyncify it can
// freeze the page.  Emits a WARNING (build proceeds); per-def opt-out via
// `static_off: ["frame_blocking"]`.  Sound-by-under-approximation: only precisely
// resolvable calls (free fns, `<.method`, `<.field.method`, typed receivers) are
// followed; unresolvable indirect dispatch is neither flagged nor propagated.
// See spec/49 §20.
void Compiler::checkFrameBlocking(Program* prog) {
    if (!prog) return;

    // A call is a blocking primitive iff its final name segment matches.
    auto isBlockingPrim = [](const std::string& fn) {
        std::string leaf = fn;
        auto dot = fn.rfind('.');
        if (dot != std::string::npos) leaf = fn.substr(dot + 1);
        return leaf == "send_and_await" || leaf == "sleep_ms" || leaf == "sleep";
    };
    // Base class of a declared type: strip leading ptr<>/ref<> wrappers.
    auto baseType = [](std::string t) -> std::string {
        while (t.rfind("ptr<", 0) == 0 || t.rfind("ref<", 0) == 0) {
            if (t.size() < 5) return "";
            t = t.substr(4, t.size() - 5);
        }
        return t;
    };

    // ── Registries ──
    std::map<std::string, FunctionDecl*> freeFns;                 // name -> fn
    std::map<std::string, FunctionDecl*> methods;                 // "Class::name" -> fn
    std::map<std::string, std::string>   nodeClass;               // id -> enclosing class ("" free)
    std::map<std::string, std::map<std::string, std::string>> classFields; // Class -> field -> type
    std::map<std::string, std::string>   globalTypes;             // globalVar -> type
    auto fnId = [](const std::string& n) { return "fn:" + n; };

    for (const auto& d : prog->declarations) {
        if (auto* fn = dynamic_cast<FunctionDecl*>(d.get())) {
            freeFns[fn->name] = fn;
            nodeClass[fnId(fn->name)] = "";
        } else if (auto* cd = dynamic_cast<ClassDecl*>(d.get())) {
            for (const auto& m : cd->members) classFields[cd->name][m.name] = m.type;
            for (const auto& mth : cd->methods) {
                std::string id = cd->name + "::" + mth->name;
                methods[id] = mth.get();
                nodeClass[id] = cd->name;
            }
        } else if (auto* ds = dynamic_cast<DeclStmt*>(d.get())) {
            if (!ds->typeName.empty()) globalTypes[ds->varName] = ds->typeName;
        }
    }

    // Resolve a call's functionName to a callee node-id (precise cases only; ""
    // when it can't be resolved → intentionally not followed).
    auto resolveCallee = [&](const std::string& fname, const std::string& encClass,
                             std::map<std::string, std::string>& localTypes) -> std::string {
        auto dot = fname.find('.');
        if (dot == std::string::npos)
            return freeFns.count(fname) ? fnId(fname) : std::string();
        std::string recv = fname.substr(0, dot);
        std::string rest = fname.substr(dot + 1);
        std::string method = rest, midField;
        auto d2 = rest.rfind('.');
        if (d2 != std::string::npos) { midField = rest.substr(0, d2); method = rest.substr(d2 + 1); }
        std::string recvType;
        if (recv == "<") {                                  // self: <.method / <.field.method
            if (!midField.empty()) {
                auto it = classFields.find(encClass);
                if (it != classFields.end() && it->second.count(midField))
                    recvType = baseType(it->second.at(midField));
            } else {
                recvType = encClass;
            }
        } else {                                            // recv var: local/param/field/global
            std::string vt;
            if (localTypes.count(recv)) vt = localTypes[recv];
            else if (!encClass.empty() && classFields.count(encClass)
                     && classFields[encClass].count(recv)) vt = classFields[encClass][recv];
            else if (globalTypes.count(recv)) vt = globalTypes[recv];
            if (!vt.empty()) recvType = baseType(vt);
        }
        if (!recvType.empty()) {
            std::string id = recvType + "::" + method;
            if (methods.count(id)) return id;
        }
        return std::string();
    };

    // Collect declared var types (params + body decls) for one function body.
    std::function<void(Stmt*, std::map<std::string, std::string>&)> collectDecls;
    collectDecls = [&](Stmt* s, std::map<std::string, std::string>& vt) {
        if (!s) return;
        if (auto* b = dynamic_cast<BlockStmt*>(s)) { for (auto& st : b->statements) collectDecls(st.get(), vt); }
        else if (auto* d = dynamic_cast<DeclStmt*>(s)) { if (!d->typeName.empty()) vt[d->varName] = d->typeName; }
        else if (auto* iff = dynamic_cast<IfStmt*>(s)) { collectDecls(iff->thenBranch.get(), vt); collectDecls(iff->elseBranch.get(), vt); }
        else if (auto* wh = dynamic_cast<WhileStmt*>(s)) { collectDecls(wh->body.get(), vt); }
        else if (auto* tr = dynamic_cast<TryStmt*>(s)) { collectDecls(tr->tryBlock.get(), vt); collectDecls(tr->catchBlock.get(), vt); }
    };

    // Walk a function body: gather direct-call edges, blocking-primitive call
    // sites, and any engine.run(win, update, render) frame-root seeds.
    struct BlockSite { std::string fname; YAML::Node loc; };
    std::map<std::string, std::vector<std::string>> edges;         // id -> callee ids
    std::map<std::string, std::vector<BlockSite>>   blockSites;    // id -> primitive calls
    std::set<std::string> frameRoots;
    // Statement-level `- call:` CallExprs carry no own source mark; fall back to
    // the enclosing statement's node so the diagnostic points at the real line.
    YAML::Node curLoc;
    auto pickLoc = [&](const YAML::Node& n) -> const YAML::Node& {
        return (n.IsDefined() && n.Mark().line >= 0) ? n : curLoc;
    };

    std::function<void(Expr*, const std::string&, const std::string&, std::map<std::string, std::string>&)> scanExpr;
    std::function<void(Stmt*, const std::string&, const std::string&, std::map<std::string, std::string>&)> scanStmt;
    auto onCall = [&](const std::string& fname, std::vector<std::unique_ptr<Expr>>& args,
                      const YAML::Node& loc, const std::string& selfId,
                      const std::string& encClass, std::map<std::string, std::string>& vt) {
        if (fname == "engine.run") {                        // seed frame roots (arg1, arg2 = callbacks)
            for (size_t i = 1; i + 1 <= args.size() && i <= 2; ++i)
                if (auto* v = dynamic_cast<VariableExpr*>(args[i].get()))
                    if (freeFns.count(v->name)) frameRoots.insert(fnId(v->name));
            return;
        }
        if (isBlockingPrim(fname)) blockSites[selfId].push_back({fname, pickLoc(loc)});
        std::string callee = resolveCallee(fname, encClass, vt);
        if (!callee.empty()) edges[selfId].push_back(callee);
    };
    scanExpr = [&](Expr* e, const std::string& selfId, const std::string& encClass,
                   std::map<std::string, std::string>& vt) {
        if (!e) return;
        if (auto* call = dynamic_cast<CallExpr*>(e)) {
            onCall(call->functionName, call->args, call->sourceNode, selfId, encClass, vt);
            for (auto& a : call->args) scanExpr(a.get(), selfId, encClass, vt);
        } else if (auto* sp = dynamic_cast<SpawnExpr*>(e)) {
            onCall(sp->functionName, sp->args, sp->sourceNode, selfId, encClass, vt);
            for (auto& a : sp->args) scanExpr(a.get(), selfId, encClass, vt);
        } else if (auto* bin = dynamic_cast<BinaryExpr*>(e)) { scanExpr(bin->lhs.get(), selfId, encClass, vt); scanExpr(bin->rhs.get(), selfId, encClass, vt); }
        else if (auto* un = dynamic_cast<UnaryExpr*>(e)) { scanExpr(un->operand.get(), selfId, encClass, vt); }
        else if (auto* interp = dynamic_cast<StringInterpolationExpr*>(e)) { for (auto& p : interp->parts) scanExpr(p.get(), selfId, encClass, vt); }
        else if (auto* ne = dynamic_cast<NewExpr*>(e)) { for (auto& kv : ne->initValues) scanExpr(kv.second.get(), selfId, encClass, vt); }
        else if (auto* at = dynamic_cast<AtExpr*>(e)) { scanExpr(at->container.get(), selfId, encClass, vt); for (auto& i : at->indices) scanExpr(i.get(), selfId, encClass, vt); }
        else if (auto* dl = dynamic_cast<DictLiteralExpr*>(e)) { for (auto& kv : dl->entriesExpr) scanExpr(kv.second.get(), selfId, encClass, vt); }
    };
    scanStmt = [&](Stmt* s, const std::string& selfId, const std::string& encClass,
                   std::map<std::string, std::string>& vt) {
        if (!s) return;
        if (s->sourceNode.IsDefined() && s->sourceNode.Mark().line >= 0) curLoc = s->sourceNode;
        if (auto* b = dynamic_cast<BlockStmt*>(s)) { for (auto& st : b->statements) scanStmt(st.get(), selfId, encClass, vt); }
        else if (auto* d = dynamic_cast<DeclStmt*>(s)) { scanExpr(d->initialValue.get(), selfId, encClass, vt); }
        else if (auto* st = dynamic_cast<SetStmt*>(s)) { scanExpr(st->value.get(), selfId, encClass, vt); }
        else if (auto* so = dynamic_cast<StoreStmt*>(s)) { scanExpr(so->value.get(), selfId, encClass, vt); }
        else if (auto* ix = dynamic_cast<IdxStmt*>(s)) { for (auto& i : ix->indices) scanExpr(i.get(), selfId, encClass, vt); scanExpr(ix->value.get(), selfId, encClass, vt); }
        else if (auto* es = dynamic_cast<ExprStmt*>(s)) { scanExpr(es->expr.get(), selfId, encClass, vt); }
        else if (auto* iff = dynamic_cast<IfStmt*>(s)) { scanExpr(iff->condition.get(), selfId, encClass, vt); scanStmt(iff->thenBranch.get(), selfId, encClass, vt); scanStmt(iff->elseBranch.get(), selfId, encClass, vt); }
        else if (auto* wh = dynamic_cast<WhileStmt*>(s)) { scanExpr(wh->condition.get(), selfId, encClass, vt); scanStmt(wh->body.get(), selfId, encClass, vt); }
        else if (auto* rs = dynamic_cast<RetStmt*>(s)) { scanExpr(rs->value.get(), selfId, encClass, vt); }
        else if (auto* th = dynamic_cast<ThrowStmt*>(s)) { scanExpr(th->value.get(), selfId, encClass, vt); }
        else if (auto* tr = dynamic_cast<TryStmt*>(s)) { scanStmt(tr->tryBlock.get(), selfId, encClass, vt); scanStmt(tr->catchBlock.get(), selfId, encClass, vt); }
    };

    auto walkFn = [&](FunctionDecl* fn, const std::string& id, const std::string& encClass) {
        if (!fn || !fn->body) return;
        std::map<std::string, std::string> vt;
        for (const auto& [n, t] : fn->args) vt[n] = t;
        collectDecls(fn->body.get(), vt);
        scanStmt(fn->body.get(), id, encClass, vt);
    };
    for (const auto& d : prog->declarations) {
        if (auto* fn = dynamic_cast<FunctionDecl*>(d.get())) walkFn(fn, fnId(fn->name), "");
        else if (auto* cd = dynamic_cast<ClassDecl*>(d.get()))
            for (const auto& m : cd->methods) walkFn(m.get(), cd->name + "::" + m->name, cd->name);
    }

    if (frameRoots.empty()) return;   // no engine.run in this unit → nothing to check

    // Frame-context = roots + transitive direct callees (BFS).
    std::set<std::string> frameSet;
    std::vector<std::string> work(frameRoots.begin(), frameRoots.end());
    while (!work.empty()) {
        std::string id = work.back(); work.pop_back();
        if (!frameSet.insert(id).second) continue;
        auto it = edges.find(id);
        if (it != edges.end()) for (const auto& c : it->second) if (!frameSet.count(c)) work.push_back(c);
    }

    // Report: every blocking-primitive call site inside a frame-context def
    // (unless that def opted out via static_off: ["frame_blocking"]).
    auto declFor = [&](const std::string& id) -> FunctionDecl* {
        if (id.rfind("fn:", 0) == 0) { auto it = freeFns.find(id.substr(3)); return it == freeFns.end() ? nullptr : it->second; }
        auto it = methods.find(id); return it == methods.end() ? nullptr : it->second;
    };
    for (const auto& [id, sites] : blockSites) {
        if (!frameSet.count(id)) continue;
        FunctionDecl* fn = declFor(id);
        if (fn) { bool off = false; for (const auto& s : fn->staticOff) if (s == "frame_blocking") { off = true; break; } if (off) continue; }
        std::string where = (id.rfind("fn:", 0) == 0) ? id.substr(3) : id;
        for (const auto& bs : sites)
            LOG_WARNING(ctx.fmtLoc(bs.loc) + "blocking call `" + bs.fname +
                "` runs inside `" + where + "`, which is reachable from a per-frame "
                "`engine.run` render/update callback.  A blocking RPC/sleep in the "
                "frame loop stalls rendering — under WASM asyncify it can freeze the "
                "page.  Make it async (`send_rpc` + `poll_rpc` across frames) or "
                "suppress with `static_off: [\"frame_blocking\"]`.");
    }
}

// Static check: engine.instantiate("<Name>") must name a declared `- prefab:`.
// An unknown name silently returns a null instance at runtime (a whole UI
// subtree just never appears), so we turn it into a hard compile error at the
// exact call site.  Only STRING-LITERAL names are checked; runtime strings are
// left untouched (no false positives).  Import-aware via ctx.prefabDecls, which
// spans the transitive import closure.  See spec/44_8_prefab_static.md.
void Compiler::checkPrefabUsage(Program* prog) {
    if (!prog) return;
    // Strip the surrounding quotes a string LiteralExpr carries in `value`.
    auto unquote = [](const std::string& v) -> std::string {
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
            return v.substr(1, v.size() - 2);
        return v;
    };
    std::function<void(Expr*)> scanExpr;
    std::function<void(Stmt*)> scanStmt;
    scanExpr = [&](Expr* e) {
        if (!e) return;
        if (auto* call = dynamic_cast<CallExpr*>(e)) {
            if (call->functionName == "engine.instantiate" && !call->args.empty()) {
                auto* lit = dynamic_cast<LiteralExpr*>(call->args[0].get());
                if (lit && lit->type == "string") {
                    std::string pname = unquote(lit->value);
                    // Record the instantiation for Tier-3 dead-prefab analysis
                    // (merged up the sub-compiler hierarchy at build time).
                    ctx.prefabUses.insert(pname);
                    if (ctx.reportPrefabs)
                        ctx.prefabReportLines.push_back(
                            "I\t" + pname + "\t" + ctx.fmtLoc(call->sourceNode));
                    if (!ctx.prefabDecls.count(pname)) {
                        LOG_ERROR(ctx.fmtLoc(call->sourceNode) +
                            "engine.instantiate(\"" + pname + "\") refers to an unknown "
                            "prefab — no `- prefab:` with that name is declared or imported "
                            "in this module. Declare it, fix the name, or add the missing "
                            "import.");
                    }
                }
            }
            // Button-handler query: `<inst>.child_clicked("<button>")` — records
            // which button names are polled anywhere (used to flag buttons that
            // are never wired to a handler).  Matched by method suffix so it
            // works for field-stored scenes (`<.scene.child_clicked`) too, which
            // the local-var Tier-2 tracking cannot see.
            if (ctx.reportPrefabs) {
                const std::string& fn = call->functionName;
                const std::string suf = ".child_clicked";
                if (fn.size() > suf.size() &&
                    fn.compare(fn.size() - suf.size(), suf.size(), suf) == 0 &&
                    !call->args.empty()) {
                    auto* clit = dynamic_cast<LiteralExpr*>(call->args[0].get());
                    if (clit && clit->type == "string")
                        ctx.prefabReportLines.push_back("K\t" + unquote(clit->value));
                }
            }
            for (auto& a : call->args) scanExpr(a.get());
        } else if (auto* bin = dynamic_cast<BinaryExpr*>(e)) {
            scanExpr(bin->lhs.get());
            scanExpr(bin->rhs.get());
        } else if (auto* un = dynamic_cast<UnaryExpr*>(e)) {
            scanExpr(un->operand.get());
        } else if (auto* interp = dynamic_cast<StringInterpolationExpr*>(e)) {
            for (auto& p : interp->parts) scanExpr(p.get());
        } else if (auto* ne = dynamic_cast<NewExpr*>(e)) {
            for (auto& kv : ne->initValues) scanExpr(kv.second.get());
        } else if (auto* na = dynamic_cast<NewArrayExpr*>(e)) {
            scanExpr(na->countExpr.get());
        } else if (auto* sp = dynamic_cast<SpawnExpr*>(e)) {
            for (auto& a : sp->args) scanExpr(a.get());
        } else if (auto* at = dynamic_cast<AtExpr*>(e)) {
            scanExpr(at->container.get());
            for (auto& i : at->indices) scanExpr(i.get());
        } else if (auto* bl = dynamic_cast<BufferLiteralExpr*>(e)) {
            for (auto& el : bl->elements) scanExpr(el.get());
        } else if (auto* dl = dynamic_cast<DictLiteralExpr*>(e)) {
            for (auto& kv : dl->entriesExpr) scanExpr(kv.second.get());
        }
    };
    scanStmt = [&](Stmt* s) {
        if (!s) return;
        if (auto* b = dynamic_cast<BlockStmt*>(s)) {
            for (auto& st : b->statements) scanStmt(st.get());
        } else if (auto* d = dynamic_cast<DeclStmt*>(s)) {
            scanExpr(d->initialValue.get());
        } else if (auto* st = dynamic_cast<SetStmt*>(s)) {
            scanExpr(st->value.get());
        } else if (auto* so = dynamic_cast<StoreStmt*>(s)) {
            scanExpr(so->value.get());
        } else if (auto* ix = dynamic_cast<IdxStmt*>(s)) {
            for (auto& i : ix->indices) scanExpr(i.get());
            scanExpr(ix->value.get());
        } else if (auto* es = dynamic_cast<ExprStmt*>(s)) {
            scanExpr(es->expr.get());
        } else if (auto* iff = dynamic_cast<IfStmt*>(s)) {
            scanExpr(iff->condition.get());
            scanStmt(iff->thenBranch.get());
            scanStmt(iff->elseBranch.get());
        } else if (auto* wh = dynamic_cast<WhileStmt*>(s)) {
            scanExpr(wh->condition.get());
            scanStmt(wh->body.get());
        } else if (auto* rs = dynamic_cast<RetStmt*>(s)) {
            scanExpr(rs->value.get());
        } else if (auto* th = dynamic_cast<ThrowStmt*>(s)) {
            scanExpr(th->value.get());
        } else if (auto* tr = dynamic_cast<TryStmt*>(s)) {
            scanStmt(tr->tryBlock.get());
            scanStmt(tr->catchBlock.get());
        }
    };
    auto checkFn = [&](FunctionDecl* fn) {
        if (!fn || !fn->body) return;
        scanStmt(fn->body.get());
    };
    for (const auto& d : prog->declarations) {
        if (auto* fn = dynamic_cast<FunctionDecl*>(d.get())) {
            checkFn(fn);
        } else if (auto* cd = dynamic_cast<ClassDecl*>(d.get())) {
            for (const auto& m : cd->methods) checkFn(m.get());
        }
    }

    // ── Tier 2: child-known ──────────────────────────────────────────────
    // A prefab addresses its children by NAME.  Methods like set_text /
    // set_texture / set_child_visible resolve the name through find_child,
    // which silently returns NULL (no-op) on a miss — the exact silent-failure
    // class Tier 1 catches for prefab names.  Here we catch it for CHILD names:
    // within a function body we track which prefab each `prefab_instance`
    // variable was instantiated from (direct `decl: [v, prefab_instance,
    // {call:[engine.instantiate,"X"]}]` bindings only — conservative, so a
    // runtime-computed name is never falsely flagged), then verify every
    // `v.<child-method>("<child>", …)` against prefab X's declared children.
    const std::set<std::string> childMethods = {
        "set_text", "set_texture", "set_child_visible",
        "set_child_font_size", "set_dropdown_options", "set_compact"
    };
    // Is `e` exactly `engine.instantiate("<literal>")`?  Yields the prefab name.
    auto asInstantiate = [&](Expr* e, std::string& outName) -> bool {
        auto* c = dynamic_cast<CallExpr*>(e);
        if (!c || c->functionName != "engine.instantiate" || c->args.empty())
            return false;
        auto* lit = dynamic_cast<LiteralExpr*>(c->args[0].get());
        if (!lit || lit->type != "string") return false;
        outName = unquote(lit->value);
        return true;
    };
    using BoundMap = std::map<std::string, std::string>; // var → prefab name
    std::function<void(Expr*, BoundMap&)> t2Expr;
    std::function<void(Stmt*, BoundMap&)> t2Stmt;
    t2Expr = [&](Expr* e, BoundMap& bound) {
        if (!e) return;
        if (auto* call = dynamic_cast<CallExpr*>(e)) {
            const std::string& fn = call->functionName;
            size_t dot = fn.find('.');
            if (dot != std::string::npos) {
                std::string base = fn.substr(0, dot);
                std::string method = fn.substr(dot + 1);
                // Only simple `var.method` (no chained receiver) on a tracked
                // prefab-instance variable, addressing a child by literal name.
                if (method.find('.') == std::string::npos &&
                    childMethods.count(method) && bound.count(base) &&
                    !call->args.empty()) {
                    auto* lit = dynamic_cast<LiteralExpr*>(call->args[0].get());
                    if (lit && lit->type == "string") {
                        std::string child = unquote(lit->value);
                        if (ctx.reportPrefabs)
                            ctx.prefabReportLines.push_back(
                                "C\t" + bound[base] + "\t" + child + "\t" +
                                ctx.fmtLoc(call->sourceNode));
                        const auto& kids = ctx.prefabDecls[bound[base]];
                        if (!kids.count(child)) {
                            LOG_ERROR(ctx.fmtLoc(call->sourceNode) +
                                "'" + base + "'." + method + "(\"" + child + "\") "
                                "addresses an unknown child of prefab '" + bound[base] +
                                "' — no child with that name is declared in its "
                                "`children:` list. Fix the child name or add the child "
                                "to the prefab declaration.");
                        }
                    }
                }
            }
            for (auto& a : call->args) t2Expr(a.get(), bound);
        } else if (auto* bin = dynamic_cast<BinaryExpr*>(e)) {
            t2Expr(bin->lhs.get(), bound); t2Expr(bin->rhs.get(), bound);
        } else if (auto* un = dynamic_cast<UnaryExpr*>(e)) {
            t2Expr(un->operand.get(), bound);
        } else if (auto* interp = dynamic_cast<StringInterpolationExpr*>(e)) {
            for (auto& p : interp->parts) t2Expr(p.get(), bound);
        } else if (auto* ne = dynamic_cast<NewExpr*>(e)) {
            for (auto& kv : ne->initValues) t2Expr(kv.second.get(), bound);
        } else if (auto* at = dynamic_cast<AtExpr*>(e)) {
            t2Expr(at->container.get(), bound);
            for (auto& i : at->indices) t2Expr(i.get(), bound);
        } else if (auto* dl = dynamic_cast<DictLiteralExpr*>(e)) {
            for (auto& kv : dl->entriesExpr) t2Expr(kv.second.get(), bound);
        }
    };
    t2Stmt = [&](Stmt* s, BoundMap& bound) {
        if (!s) return;
        if (auto* b = dynamic_cast<BlockStmt*>(s)) {
            for (auto& st : b->statements) t2Stmt(st.get(), bound);
        } else if (auto* d = dynamic_cast<DeclStmt*>(s)) {
            t2Expr(d->initialValue.get(), bound);
            std::string pn;
            if (asInstantiate(d->initialValue.get(), pn)) bound[d->varName] = pn;
            else bound.erase(d->varName); // rebind/shadow to something untracked
        } else if (auto* st = dynamic_cast<SetStmt*>(s)) {
            t2Expr(st->value.get(), bound);
            std::string pn;
            if (asInstantiate(st->value.get(), pn)) bound[st->target] = pn;
            else bound.erase(st->target); // reassigned away — stop trusting
        } else if (auto* so = dynamic_cast<StoreStmt*>(s)) {
            t2Expr(so->value.get(), bound);
        } else if (auto* ix = dynamic_cast<IdxStmt*>(s)) {
            for (auto& i : ix->indices) t2Expr(i.get(), bound);
            t2Expr(ix->value.get(), bound);
        } else if (auto* es = dynamic_cast<ExprStmt*>(s)) {
            t2Expr(es->expr.get(), bound);
        } else if (auto* iff = dynamic_cast<IfStmt*>(s)) {
            t2Expr(iff->condition.get(), bound);
            // Branch-local bindings must not leak past the branch → recurse on
            // copies.  Bindings made before the `if` stay visible inside.
            BoundMap tb = bound; t2Stmt(iff->thenBranch.get(), tb);
            BoundMap eb = bound; t2Stmt(iff->elseBranch.get(), eb);
        } else if (auto* wh = dynamic_cast<WhileStmt*>(s)) {
            t2Expr(wh->condition.get(), bound);
            BoundMap wb = bound; t2Stmt(wh->body.get(), wb);
        } else if (auto* rs = dynamic_cast<RetStmt*>(s)) {
            t2Expr(rs->value.get(), bound);
        } else if (auto* th = dynamic_cast<ThrowStmt*>(s)) {
            t2Expr(th->value.get(), bound);
        } else if (auto* tr = dynamic_cast<TryStmt*>(s)) {
            BoundMap trb = bound; t2Stmt(tr->tryBlock.get(), trb);
            BoundMap cb = bound;  t2Stmt(tr->catchBlock.get(), cb);
        }
    };
    auto checkFnT2 = [&](FunctionDecl* fn) {
        if (!fn || !fn->body) return;
        BoundMap bound;
        t2Stmt(fn->body.get(), bound);
    };
    for (const auto& d : prog->declarations) {
        if (auto* fn = dynamic_cast<FunctionDecl*>(d.get())) {
            checkFnT2(fn);
        } else if (auto* cd = dynamic_cast<ClassDecl*>(d.get())) {
            for (const auto& m : cd->methods) checkFnT2(m.get());
        }
    }

    // ── Declarative instantiation channel: spawn: / payload_spawn: ───────
    // A container child with `bind_list:` + `spawn: <Prefab>` instantiates the
    // named prefab once per list element; a cascade/dropdown child with
    // `payload_spawn: <Prefab>` instantiates it per cascade field.  These are
    // real instantiation sites — just declarative, not `engine.instantiate` —
    // so their targets count as USES for the Tier-3 dead-prefab analysis (a
    // list-row / chip prefab spawned only this way must not be reported dead).
    //
    // We deliberately do NOT hard-error on an unresolved spawn target the way
    // Tier 1 does for engine.instantiate.  A `- prefab:` is data that gets
    // registered whenever its file is imported, but its spawn target may live
    // in a file that only *some* build targets import (e.g. prefabs.aiml
    // declares CardEditorScene, which spawns AA*Chip from cascade_chips.aiml;
    // the board-only client build pulls in prefabs.aiml but not the card
    // editor).  Such a prefab is simply dead in that target — not a typo — so
    // flagging it would break legitimate partial builds.
    for (const auto& d : prog->declarations) {
        auto* pf = dynamic_cast<PrefabDecl*>(d.get());
        if (!pf) continue;
        // Naming discipline: prefab names and child names are the UI's cross-file
        // wiring vocabulary (spawn:/set_text/child_clicked address them by name),
        // so they must be >=5 chars — stricter than the 3-char identifier floor.
        ctx.validatePrefabName(pf->name, "prefab", pf->sourceNode);
        for (const auto& ch : pf->children)
            ctx.validatePrefabName(ch.name, "prefab child", pf->sourceNode);
        if (ctx.reportPrefabs) {
            std::string kids;
            for (const auto& ch : pf->children) {
                if (!kids.empty()) kids += ",";
                kids += ch.name;
                // Interaction inventory: button children (drag is whole-prefab).
                if (ch.type == "button")
                    ctx.prefabReportLines.push_back("B\t" + pf->name + "\t" + ch.name);
                // Per-child detail record for the recursive tree:
                //   H \t <prefab> \t <child> \t <type> \t <spawnTarget> \t <channel> \t <attrs>
                // spawnTarget/channel drive the recursion (a container that
                // spawns another prefab nests that prefab's subtree); attrs
                // carry the remaining wiring-relevant fields for display.
                std::string spawnTgt = !ch.spawn.empty() ? ch.spawn : ch.payload_spawn;
                std::string channel  = !ch.spawn.empty() ? "spawn"
                                     : !ch.payload_spawn.empty() ? "payload_spawn" : "";
                std::string a;
                auto add = [&](const char* k, const std::string& v) {
                    if (v.empty()) return;
                    std::string vv = v;
                    for (char& c : vv) if (c == '\t' || c == '\n') c = ' ';
                    if (vv.size() > 40) vv = vv.substr(0, 37) + "...";
                    if (!a.empty()) a += " ";
                    a += std::string(k) + "=" + vv;
                };
                add("bind",     ch.bind);
                add("cascade",  ch.cascade_type);
                add("enum",     ch.enum_type);
                add("on_click", ch.on_click);
                add("on_change",ch.on_change);
                add("group",    ch.group);
                add("tex",      ch.texture);
                add("label",    ch.label);
                ctx.prefabReportLines.push_back(
                    "H\t" + pf->name + "\t" + ch.name + "\t" + ch.type + "\t" +
                    spawnTgt + "\t" + channel + "\t" + a);
            }
            ctx.prefabReportLines.push_back(
                "D\t" + pf->name + "\t" + ctx.fmtLoc(pf->sourceNode) + "\t" + kids);
            if (pf->draggable)  // whole-prefab drag (e.g. CardToken, CardFull)
                ctx.prefabReportLines.push_back("G\t" + pf->name + "\t*");
        }
        for (const auto& ch : pf->children) {
            if (!ch.spawn.empty()) {
                ctx.prefabUses.insert(ch.spawn);
                if (ctx.reportPrefabs)
                    ctx.prefabReportLines.push_back(
                        "S\t" + ch.spawn + "\t" + pf->name + "\t" + ch.name + "\tspawn");
            }
            if (!ch.payload_spawn.empty()) {
                ctx.prefabUses.insert(ch.payload_spawn);
                if (ctx.reportPrefabs)
                    ctx.prefabReportLines.push_back(
                        "S\t" + ch.payload_spawn + "\t" + pf->name + "\t" + ch.name +
                        "\tpayload_spawn");
            }
            // `enum: T` dropdown/radio binding = reflective WHOLE-enum use: the
            // widget's options are every member and the selection is read back via
            // dropdown_enum, so no member appears as `T.Member` in code.  Mark all
            // members referenced for the unused-enumerator analysis (spec/49 §18.2).
            if (!ch.enum_type.empty()) {
                auto ei = ctx.enums.find(ch.enum_type);
                if (ei == ctx.enums.end()) ei = ctx.enums.find(ctx.qualify(ch.enum_type));
                if (ei != ctx.enums.end())
                    for (const auto& mv : ei->second)
                        ctx.enumUsedMembers[ei->first].insert(mv.first);
            }
        }
    }

    // ── Behavioral wiring collection (report mode only) ──────────────────
    // Prefab instances live in class fields (`<.spell_stack`) and are driven by
    // `<.field.<op>(...)` calls across methods.  Records:
    //   F  field->prefab binding:  F \t <class> \t <field> \t <prefab>
    //   O  field operation:        O \t <class> \t <field> \t <op> \t <bucket> \t <def> \t <arg0>
    // so the report can show, per prefab, its owner field, the methods that drive
    // it, and — via the buckets — whether it is populated / animated / handled.
    // See doc/prefab_report_v2_plan.md.
    if (ctx.reportPrefabs) {
        // Pre-pass A: spawn-helper registry — a function returning prefab_instance
        // whose body instantiates exactly ONE distinct prefab is a helper for it
        // (e.g. spawn_mana_sphere -> ManaSphere).  Lets us resolve field/list
        // bindings made through a helper, not just a literal engine.instantiate.
        std::function<void(Expr*, std::set<std::string>&)> collInstE;
        std::function<void(Stmt*, std::set<std::string>&)> collInstS;
        collInstE = [&](Expr* e, std::set<std::string>& out) {
            if (!e) return;
            if (auto* c = dynamic_cast<CallExpr*>(e)) {
                if (c->functionName == "engine.instantiate" && !c->args.empty())
                    if (auto* l = dynamic_cast<LiteralExpr*>(c->args[0].get()))
                        if (l->type == "string") out.insert(unquote(l->value));
                for (auto& a : c->args) collInstE(a.get(), out);
            } else if (auto* bin = dynamic_cast<BinaryExpr*>(e)) { collInstE(bin->lhs.get(),out); collInstE(bin->rhs.get(),out);
            } else if (auto* un = dynamic_cast<UnaryExpr*>(e)) { collInstE(un->operand.get(),out);
            } else if (auto* at = dynamic_cast<AtExpr*>(e)) { collInstE(at->container.get(),out); for(auto&i:at->indices) collInstE(i.get(),out);
            } else if (auto* ne = dynamic_cast<NewExpr*>(e)) { for(auto&kv:ne->initValues) collInstE(kv.second.get(),out);
            } else if (auto* dl = dynamic_cast<DictLiteralExpr*>(e)) { for(auto&kv:dl->entriesExpr) collInstE(kv.second.get(),out);
            } else if (auto* ip = dynamic_cast<StringInterpolationExpr*>(e)) { for(auto&p:ip->parts) collInstE(p.get(),out); }
        };
        collInstS = [&](Stmt* s, std::set<std::string>& out) {
            if (!s) return;
            if (auto* b = dynamic_cast<BlockStmt*>(s)) { for(auto&st:b->statements) collInstS(st.get(),out);
            } else if (auto* d = dynamic_cast<DeclStmt*>(s)) { collInstE(d->initialValue.get(),out);
            } else if (auto* st = dynamic_cast<SetStmt*>(s)) { collInstE(st->value.get(),out);
            } else if (auto* so = dynamic_cast<StoreStmt*>(s)) { collInstE(so->value.get(),out);
            } else if (auto* ix = dynamic_cast<IdxStmt*>(s)) { for(auto&i:ix->indices) collInstE(i.get(),out); collInstE(ix->value.get(),out);
            } else if (auto* es = dynamic_cast<ExprStmt*>(s)) { collInstE(es->expr.get(),out);
            } else if (auto* iff = dynamic_cast<IfStmt*>(s)) { collInstE(iff->condition.get(),out); collInstS(iff->thenBranch.get(),out); collInstS(iff->elseBranch.get(),out);
            } else if (auto* wh = dynamic_cast<WhileStmt*>(s)) { collInstE(wh->condition.get(),out); collInstS(wh->body.get(),out);
            } else if (auto* rs = dynamic_cast<RetStmt*>(s)) { collInstE(rs->value.get(),out);
            } else if (auto* tr = dynamic_cast<TryStmt*>(s)) { collInstS(tr->tryBlock.get(),out); collInstS(tr->catchBlock.get(),out); }
        };
        std::map<std::string, std::string> spawnHelpers;   // fn name -> prefab
        std::set<std::string> listFields;                  // "class\tfield" of type list<prefab_instance>
        auto regHelper = [&](FunctionDecl* fn) {
            if (!fn || fn->returnType != "prefab_instance" || !fn->body) return;
            std::set<std::string> insts; collInstS(fn->body.get(), insts);
            if (insts.size() == 1) spawnHelpers[fn->name] = *insts.begin();
        };
        // Pre-pass B: prefab-instance class members (single + list) -> M records
        // (so the Collections section can enumerate them even without a push).
        for (const auto& d : prog->declarations) {
            if (auto* fn = dynamic_cast<FunctionDecl*>(d.get())) regHelper(fn);
            else if (auto* cd = dynamic_cast<ClassDecl*>(d.get())) {
                for (const auto& m : cd->methods) regHelper(m.get());
                for (const auto& mem : cd->members) {
                    bool isList = mem.type == "list<prefab_instance>";
                    if (mem.type == "prefab_instance" || isList) {
                        ctx.prefabReportLines.push_back(
                            "M\t" + cd->name + "\t" + mem.name + "\t" + (isList ? "1" : "0"));
                        if (isList) listFields.insert(cd->name + "\t" + mem.name);
                    }
                }
            }
        }
        auto classifyOp = [](const std::string& op) -> const char* {
            if (op=="set_text"||op=="set_texture"||op=="set_dropdown_options"||
                op=="set_dropdown_index"||op=="set_dropdown_selected"||op=="bind_list"||
                op=="set_bg_tex"||op=="set_bg"||op=="set_compact") return "content";
            if (op=="set_visible"||op=="set_child_visible"||op=="set_alpha") return "visibility";
            if (op=="set_pos"||op=="set_pos_rel"||op=="set_z"||op=="set_size_w"||
                op=="set_size_h"||op=="set_scale") return "motion";
            if (op=="set_bar_value"||op=="set_bar_max"||op=="set_bar_fill") return "gauge";
            if (op=="child_clicked"||op=="was_clicked"||op=="set_draggable"||
                op=="child_contains"||op=="set_child_font_size") return "interaction";
            return nullptr;
        };
        std::string curClass, curDef;
        std::map<std::string, std::string> bwLocals;   // local var -> prefab (per method)
        // resolvePrefab: the prefab a value expression yields, if statically known
        // (literal instantiate, spawn-helper call, or a tracked local).
        auto resolvePrefab = [&](Expr* e) -> std::string {
            if (auto* c = dynamic_cast<CallExpr*>(e)) {
                if (c->functionName == "engine.instantiate" && !c->args.empty())
                    if (auto* l = dynamic_cast<LiteralExpr*>(c->args[0].get()))
                        if (l->type == "string") return unquote(l->value);
                std::string h = c->functionName;
                if (h.size() > 2 && h[0] == '<' && h[1] == '.') h = h.substr(2);
                auto it = spawnHelpers.find(h);
                if (it != spawnHelpers.end()) return it->second;
            } else if (auto* v = dynamic_cast<VariableExpr*>(e)) {
                auto it = bwLocals.find(v->name);
                if (it != bwLocals.end()) return it->second;
            }
            return "";
        };
        std::function<void(Expr*)> bwExpr;
        std::function<void(Stmt*)> bwStmt;
        bwExpr = [&](Expr* e) {
            if (!e) return;
            if (auto* call = dynamic_cast<CallExpr*>(e)) {
                const std::string& fn = call->functionName;
                // N: server action — a `net_*` call names the RPC this method fires.
                if (fn.rfind("net_", 0) == 0)
                    ctx.prefabReportLines.push_back("N\t" + curClass + "\t" + curDef + "\t" + fn);
                // Y: key binding — `input.key_pressed("<key>")` names a keyboard key
                // this method reacts to.
                if (fn.rfind("input.key", 0) == 0 && !call->args.empty())
                    if (auto* l = dynamic_cast<LiteralExpr*>(call->args[0].get()))
                        if (l->type == "string")
                            ctx.prefabReportLines.push_back(
                                "Y\t" + curClass + "\t" + curDef + "\t" + unquote(l->value));
                // `<.field[.sub].op(...)` — a call on a `this` field.
                if (fn.size() > 2 && fn[0] == '<' && fn[1] == '.') {
                    size_t ld = fn.rfind('.');
                    if (ld != std::string::npos && ld > 2) {
                        std::string field = fn.substr(2, ld - 2);
                        std::string op    = fn.substr(ld + 1);
                        // L: `<.list_field.push(x)` where x resolves to a prefab.
                        if ((op == "push" || op == "append" || op == "add") && !call->args.empty()) {
                            std::string pn = resolvePrefab(call->args[0].get());
                            if (!pn.empty())
                                ctx.prefabReportLines.push_back(
                                    "L\t" + curClass + "\t" + field + "\t" + pn + "\t" + curDef);
                        }
                        if (const char* bucket = classifyOp(op)) {
                            std::string arg0;
                            if (!call->args.empty())
                                if (auto* l = dynamic_cast<LiteralExpr*>(call->args[0].get()))
                                    if (l->type == "string") arg0 = unquote(l->value);
                            ctx.prefabReportLines.push_back(
                                "O\t" + curClass + "\t" + field + "\t" + op + "\t" +
                                bucket + "\t" + curDef + "\t" + arg0);
                        }
                    }
                }
                for (auto& a : call->args) bwExpr(a.get());
            } else if (auto* v = dynamic_cast<VariableExpr*>(e)) {
                // T: a reference to a list<prefab_instance> field -> "touched by def".
                if (v->name.size() > 2 && v->name[0] == '<' && v->name[1] == '.') {
                    std::string field = v->name.substr(2);
                    if (listFields.count(curClass + "\t" + field))
                        ctx.prefabReportLines.push_back("T\t" + curClass + "\t" + field + "\t" + curDef);
                }
            } else if (auto* bin = dynamic_cast<BinaryExpr*>(e)) {
                bwExpr(bin->lhs.get()); bwExpr(bin->rhs.get());
            } else if (auto* un = dynamic_cast<UnaryExpr*>(e)) {
                bwExpr(un->operand.get());
            } else if (auto* interp = dynamic_cast<StringInterpolationExpr*>(e)) {
                for (auto& p : interp->parts) bwExpr(p.get());
            } else if (auto* ne = dynamic_cast<NewExpr*>(e)) {
                for (auto& kv : ne->initValues) bwExpr(kv.second.get());
            } else if (auto* at = dynamic_cast<AtExpr*>(e)) {
                bwExpr(at->container.get());
                for (auto& i : at->indices) bwExpr(i.get());
            } else if (auto* dl = dynamic_cast<DictLiteralExpr*>(e)) {
                for (auto& kv : dl->entriesExpr) bwExpr(kv.second.get());
            }
        };
        bwStmt = [&](Stmt* s) {
            if (!s) return;
            if (auto* b = dynamic_cast<BlockStmt*>(s)) {
                for (auto& st : b->statements) bwStmt(st.get());
            } else if (auto* d = dynamic_cast<DeclStmt*>(s)) {
                std::string pn = resolvePrefab(d->initialValue.get());
                if (!pn.empty()) bwLocals[d->varName] = pn;
                bwExpr(d->initialValue.get());
            } else if (auto* st = dynamic_cast<SetStmt*>(s)) {
                // F: `<.field = engine.instantiate("X")` OR a spawn-helper call.
                if (st->target.size() > 2 && st->target[0] == '<' && st->target[1] == '.') {
                    std::string pn = resolvePrefab(st->value.get());
                    if (!pn.empty())
                        ctx.prefabReportLines.push_back(
                            "F\t" + curClass + "\t" + st->target.substr(2) + "\t" + pn);
                }
                bwExpr(st->value.get());
            } else if (auto* so = dynamic_cast<StoreStmt*>(s)) {
                bwExpr(so->value.get());
            } else if (auto* ix = dynamic_cast<IdxStmt*>(s)) {
                for (auto& i : ix->indices) bwExpr(i.get());
                bwExpr(ix->value.get());
            } else if (auto* es = dynamic_cast<ExprStmt*>(s)) {
                bwExpr(es->expr.get());
            } else if (auto* iff = dynamic_cast<IfStmt*>(s)) {
                bwExpr(iff->condition.get());
                bwStmt(iff->thenBranch.get());
                bwStmt(iff->elseBranch.get());
            } else if (auto* wh = dynamic_cast<WhileStmt*>(s)) {
                bwExpr(wh->condition.get());
                bwStmt(wh->body.get());
            } else if (auto* rs = dynamic_cast<RetStmt*>(s)) {
                bwExpr(rs->value.get());
            } else if (auto* th = dynamic_cast<ThrowStmt*>(s)) {
                bwExpr(th->value.get());
            } else if (auto* tr = dynamic_cast<TryStmt*>(s)) {
                bwStmt(tr->tryBlock.get());
                bwStmt(tr->catchBlock.get());
            }
        };
        for (const auto& d : prog->declarations) {
            if (auto* fn = dynamic_cast<FunctionDecl*>(d.get())) {
                curClass = ""; curDef = fn->name; bwLocals.clear();
                if (fn->body) bwStmt(fn->body.get());
            } else if (auto* cd = dynamic_cast<ClassDecl*>(d.get())) {
                for (const auto& m : cd->methods) {
                    curClass = cd->name; curDef = m->name; bwLocals.clear();
                    if (m->body) bwStmt(m->body.get());
                }
            }
        }
    }
}

void Compiler::compile(Program* prog) {
    if (!prog) return;

    // spec/61 — the heap-lifetime feature (scope-drops + Increment C member deep-copy + P2a
    // return-cleanup + producer return-ownership + the §7.0.1 conditional-push dominance rule) is
    // DEFAULT OFF while it hardens against real-world aliasing.  A trial default-ON flip on
    // 2026-09-11 ran the full 9rooms suite drops-on and still hit REAL failures (NOT flakiness,
    // reproduced standalone): teardown double-frees (test_ceremony_cost, test_mkp_math,
    // test_conditional_durations "free(): double free"), a SEGV (test_activate_ability), and a
    // behavioral regression (test_creature_costs "sac1 nicht im Friedhof" — a card dropped before
    // reaching the graveyard).  So the acceptance gate is NOT green yet; kept OFF.  Opt IN with
    // AIML_LIFETIME_DROPS=1 (memory-sanitizer harness + 9rooms server/media/static json set it).
    // Re-flip the default to true only once the FULL 9rooms suite passes drops-on.
    ctx.lifetimeDropsEnabled = false;
    if (const char* de = getenv("AIML_LIFETIME_DROPS")) {
        std::string s = de;
        ctx.lifetimeDropsEnabled = !(s.empty() || s == "0" || s == "off" || s == "OFF"
                                     || s == "false" || s == "FALSE");
    }

    // AIML v2 reference-semantics for heap-field models (doc/aiml_v2_impl_plan.md). DEFAULT OFF
    // (v1 inline-value-model path). Opt IN with AIML_MODEL_REFCOUNT=1 once P1–P6 land and the full
    // 9rooms suite is green with it. INERT today — no codegen reads this flag yet.
    ctx.modelRefcount = false;
    if (const char* mr = getenv("AIML_MODEL_REFCOUNT")) {
        std::string s = mr;
        ctx.modelRefcount = !(s.empty() || s == "0" || s == "off" || s == "OFF"
                              || s == "false" || s == "FALSE");
    }

    registerBuiltins(ctx);

    // Function cascades (spec/57): validate pools/rules (M1/M2), then synthesize the
    // pool `render` class (M3).  Runs BEFORE the visitTopLevel loop so `{new: Pool}` /
    // `pool.render(...)` in user code resolve the registered type + forward-declared
    // methods.  Validation must precede synthesis (synthesis moves the handler defs).
    checkCascadeClasses(prog);
    synthesizeCascadeClasses(prog);

    // classgroup (spec/58): validate wiring, then desugar to subclasses + a
    // container class.  Same ordering rationale as cascade_class — validate
    // first (synthesis moves the function defs), synthesize before visitTopLevel
    // so {new: <group>} resolves the container + its __ctor.
    mergeGroupMethods(prog);
    checkClassGroups(prog);
    synthesizeClassGroups(prog);

    if (ctx.libraryMode) {
        // Library mode: top-level DeclStmts become LLVM GlobalVariables (not stack
        // allocas) so that functions in this compilation unit can reference them
        // without cross-function dominance issues.  A small internal init function
        // is lazily created for any top-level code that needs an insert point
        // (initializers, set:/call: statements).

        // Deterministic init function name from source file path.
        std::string sanitized;
        for (char c : ctx.currentSourceFile)
            sanitized += (std::isalnum(c) ? c : '_');
        std::string initName = "__aiml_lib_init_" + sanitized;

        // Always create the lib init function (even if empty) so the linker
        // can resolve the call from the main module's __aiml_init.
        auto* voidTy_ = llvm::Type::getVoidTy(ctx.context);
        auto* ft_ = llvm::FunctionType::get(voidTy_, false);
        llvm::Function* libInitFunc = llvm::Function::Create(
            ft_, llvm::Function::ExternalLinkage, initName, ctx.module);
        auto* libInitEntryBB = llvm::BasicBlock::Create(ctx.context, "entry", libInitFunc);
        ctx.ir_gen->setInsertPoint(ctx.ir_gen->wrapBlock(libInitEntryBB));

        // Restore insert point to the lib init function only when the current
        // insert point is NOT already inside it (FunctionDecls leave it inside
        // the freshly-compiled function).  Doing back() unconditionally would
        // abandon List/Map control-flow merge blocks without terminator.
        auto ensureLibInit = [&]() {
            llvm::BasicBlock* cur = static_cast<llvm::BasicBlock*>(
                ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
            if (!cur || cur->getParent() != libInitFunc) {
                auto* lastBB = &libInitFunc->back();
                ctx.ir_gen->setInsertPoint(ctx.ir_gen->wrapBlock(lastBB));
            }
        };

        // Phase 1 — storage allocation only (no init code in lib_init yet).
        // - {extern: true} → __lib_<varName>            (WeakAnyLinkage, cross-file)
        // - otherwise      → __lib_<sanitized_file>_<varName> (ExternalLinkage, unique per file)
        std::string filePrefix;
        if (!ctx.currentSourceFile.empty()) {
            for (char c : ctx.currentSourceFile)
                filePrefix += (std::isalnum(c) ? c : '_');
            filePrefix += "_";
        }
        std::vector<std::tuple<DeclStmt*, llvm::GlobalVariable*, IRType*>> globalContainers;
        for (const auto& d : prog->declarations) {
            auto* decl = dynamic_cast<DeclStmt*>(d.get());
            if (!decl) continue;
            IRType* ty = ctx.ir_gen->resolveType(decl->typeName);
            if (!ty) continue;
            llvm::Type* llvmTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ty));
            std::string symName = decl->is_extern
                ? "__lib_" + decl->varName
                : "__lib_" + filePrefix + decl->varName;
            auto linkage = decl->is_extern
                ? llvm::GlobalValue::WeakAnyLinkage
                : llvm::GlobalValue::ExternalLinkage;
            llvm::GlobalVariable* gv = ctx.module.getGlobalVariable(symName);
            if (gv) {
                if (!gv->hasInitializer())
                    gv->setInitializer(llvm::Constant::getNullValue(llvmTy));
                gv->setLinkage(linkage);
            } else {
                gv = new llvm::GlobalVariable(
                    ctx.module, llvmTy, /*isConstant=*/false,
                    linkage,
                    llvm::Constant::getNullValue(llvmTy),
                    symName);
            }

            Symbol sym;
            sym.ir_alloca_inst  = ctx.ir_gen->wrap(static_cast<llvm::Value*>(gv));
            sym.ir_type         = ctx.ir_gen->getPointerTy();
            sym.ir_element_type = ty;
            sym.typeName        = decl->typeName;
            sym.is_const        = decl->is_const;
            sym.is_volatile     = decl->is_volatile;
            sym.is_global       = true;
            ctx.symbol_table[decl->varName] = sym;

            // Collect container globals (list/map/chan/buffer) AND class-
            // instance globals (their members may be containers and need
            // recursive init) so Phase 2 can prep them before user-code runs.
            // SKIP extern decls: the owning file initialises them.
            const std::string& tn = decl->typeName;
            bool isContainer = (tn.rfind("list<",   0) == 0 ||
                                tn.rfind("map<",    0) == 0 ||
                                tn.rfind("chan<",   0) == 0 ||
                                tn.rfind("buffer<", 0) == 0);
            bool isClassInstance = ctx.classes.count(tn) &&
                                   !ctx.classes[tn].isBuiltin &&
                                   ctx.classes[tn].type != nullptr;
            if ((isContainer || isClassInstance) && !decl->is_extern) {
                globalContainers.emplace_back(decl, gv, ty);
            }
        }

        // Phase 2 — container init prologue.  Initialises list/map/chan/buffer
        // globals FIRST so any user-code (in initializers or imperative
        // statements) can safely read/mutate them — regardless of where they
        // appear in source order.  Same approach as C++ static-storage zero-
        // init, conceptually equivalent to global_ctors with high priority.
        ensureLibInit();
        for (auto& [decl, gv, ty] : globalContainers) {
            const std::string& tn = decl->typeName;
            if (tn.rfind("list<", 0) == 0) {
                std::string elemName = tn.substr(5, tn.length() - 6);
                llvm::Type* elemTy = static_cast<llvm::Type*>(
                    ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
                if (elemTy) ListImpl::initList(ctx, gv, elemTy, 4);
            } else if (tn.rfind("map<", 0) == 0) {
                std::string inner = tn.substr(4, tn.length() - 5);
                size_t colon = inner.find(':');
                if (colon != std::string::npos) {
                    std::string kName = inner.substr(0, colon);
                    std::string vName = inner.substr(colon + 1);
                    llvm::Type* kTy = static_cast<llvm::Type*>(
                        ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(kName)));
                    llvm::Type* vTy = static_cast<llvm::Type*>(
                        ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(vName)));
                    if (kTy && vTy) MapImpl::initMap(ctx, gv, kTy, vTy, 4);
                }
            } else if (tn.rfind("chan<", 0) == 0) {
                ChanImpl::init(ctx, gv, ctx.builder.getInt64(0));
            } else if (tn.rfind("buffer<", 0) == 0 && !decl->initialValue) {
                BufferImpl::alloc(ctx, gv, ctx.builder.getInt32(4096));
            } else if (ctx.classes.count(tn) && !ctx.classes[tn].isBuiltin &&
                       ctx.classes[tn].type != nullptr) {
                initClassContainerMembers(gv, tn);
            }
        }

        // Phase 3 — visit ALL top-level statements in SOURCE ORDER.  Before
        // each statement we restore the insert point to lib_init so the
        // previous FunctionDecl (which left the insert point inside the
        // function body) doesn't bleed into the next imperative emission.
        // visitDeclStmt sees is_global=true + inTopLevelInit and emits only
        // the initializer (no alloca, no second container-init).
        ctx.inTopLevelInit = true;
        for (const auto& d : prog->declarations) {
            ensureLibInit();
            visitTopLevel(d.get());
        }
        ctx.inTopLevelInit = false;

        // Terminate the library init function.
        {
            auto* curBB = static_cast<llvm::BasicBlock*>(
                ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
            if (curBB && !curBB->getTerminator())
                ctx.builder.CreateRetVoid();
            ctx.libInitFuncName = initName;
        }

        // Static check also applies to imported/library modules (not just entry).
        checkStringComparisons(prog);
        checkPtrIndirection(prog);
        checkFrameBlocking(prog);
        checkPrefabUsage(prog);
    } else {
        // Create __aiml_init function to hold top-level statements
        IRType* voidTy = ctx.ir_gen->resolveType("void");
        std::vector<IRType*> noArgs;
        IRValue* initFunc = ctx.ir_gen->createFunction("__aiml_init", voidTy, noArgs);

        // Create entry block for __aiml_init
        llvm::Function* llvmInitFunc = static_cast<llvm::Function*>(ctx.ir_gen->unwrap(initFunc));
        llvm::BasicBlock* initEntryBB = llvm::BasicBlock::Create(ctx.context, "entry", llvmInitFunc);
        ctx.ir_gen->setInsertPoint(ctx.ir_gen->wrapBlock(initEntryBB));

        // Call all imported lib init functions BEFORE user top-level code.
        for (const auto& libInitName : ctx.libInitFuncNames) {
            auto* vd = llvm::Type::getVoidTy(ctx.context);
            auto* ft = llvm::FunctionType::get(vd, false);
            llvm::FunctionCallee fc = ctx.module.getOrInsertFunction(libInitName, ft);
            ctx.builder.CreateCall(fc);
        }

        // Phase 1 — storage allocation only.  Pre-promote top-level DeclStmts
        // to GlobalVariables so functions can reference them without cross-
        // function alloca issues.
        // - {extern: true} → __lib_<varName>   (WeakAnyLinkage, shared with
        //                    extern decls in imported libraryMode files)
        // - otherwise      → __global_<varName> (ExternalLinkage, private)
        // Main-mode files can own globals that referenced extern from library
        // files (e.g. client_main.aiml owns g_session, login_panel.aiml refs it).
        std::vector<std::tuple<DeclStmt*, llvm::GlobalVariable*, IRType*>> globalContainers;
        for (const auto& d : prog->declarations) {
            auto* decl = dynamic_cast<DeclStmt*>(d.get());
            if (!decl) continue;
            IRType* ty = ctx.ir_gen->resolveType(decl->typeName);
            if (!ty) continue;
            llvm::Type* llvmTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ty));
            std::string symName = decl->is_extern
                ? "__lib_" + decl->varName
                : "__global_" + decl->varName;
            auto linkage = decl->is_extern
                ? llvm::GlobalValue::WeakAnyLinkage
                : llvm::GlobalValue::ExternalLinkage;
            llvm::GlobalVariable* gv = ctx.module.getGlobalVariable(symName);
            if (gv) {
                if (!gv->hasInitializer())
                    gv->setInitializer(llvm::Constant::getNullValue(llvmTy));
                gv->setLinkage(linkage);
            } else {
                gv = new llvm::GlobalVariable(
                    ctx.module, llvmTy, /*isConstant=*/false,
                    linkage,
                    llvm::Constant::getNullValue(llvmTy),
                    symName);
            }

            Symbol sym;
            sym.ir_alloca_inst  = ctx.ir_gen->wrap(static_cast<llvm::Value*>(gv));
            sym.ir_type         = ctx.ir_gen->getPointerTy();
            sym.ir_element_type = ty;
            sym.typeName        = decl->typeName;
            sym.is_const        = decl->is_const;
            sym.is_volatile     = decl->is_volatile;
            sym.is_global       = true;
            ctx.symbol_table[decl->varName] = sym;

            const std::string& tn = decl->typeName;
            bool isContainer = (tn.rfind("list<",   0) == 0 ||
                                tn.rfind("map<",    0) == 0 ||
                                tn.rfind("chan<",   0) == 0 ||
                                tn.rfind("buffer<", 0) == 0);
            bool isClassInstance = ctx.classes.count(tn) &&
                                   !ctx.classes[tn].isBuiltin &&
                                   ctx.classes[tn].type != nullptr;
            // Main-mode runs exactly once per binary, so it's safe to init
            // EXTERN class-instance globals here: when every file uses
            // `{extern: true}` (the DataCreatures all-extern pattern), no
            // library file owns Phase-2 init, and the class member defaults
            // would otherwise be wiped.  Extern containers (list/map/etc.)
            // still skipped because their init allocates a fresh heap buffer
            // and would leak if a library owner already inited.
            if ((isContainer && !decl->is_extern) || isClassInstance) {
                globalContainers.emplace_back(decl, gv, ty);
            }
        }

        // Phase 2 — container init prologue.  Initialises list/map/chan/buffer
        // globals BEFORE any user-code runs.  Matches the libraryMode layout
        // so containers are always ready when user-code (initializers,
        // imperative statements) might access them.
        for (auto& [decl, gv, ty] : globalContainers) {
            const std::string& tn = decl->typeName;
            if (tn.rfind("list<", 0) == 0) {
                std::string elemName = tn.substr(5, tn.length() - 6);
                llvm::Type* elemTy = static_cast<llvm::Type*>(
                    ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
                if (elemTy) ListImpl::initList(ctx, gv, elemTy, 4);
            } else if (tn.rfind("map<", 0) == 0) {
                std::string inner = tn.substr(4, tn.length() - 5);
                size_t colon = inner.find(':');
                if (colon != std::string::npos) {
                    std::string kName = inner.substr(0, colon);
                    std::string vName = inner.substr(colon + 1);
                    llvm::Type* kTy = static_cast<llvm::Type*>(
                        ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(kName)));
                    llvm::Type* vTy = static_cast<llvm::Type*>(
                        ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(vName)));
                    if (kTy && vTy) MapImpl::initMap(ctx, gv, kTy, vTy, 4);
                }
            } else if (tn.rfind("chan<", 0) == 0) {
                ChanImpl::init(ctx, gv, ctx.builder.getInt64(0));
            } else if (tn.rfind("buffer<", 0) == 0 && !decl->initialValue) {
                BufferImpl::alloc(ctx, gv, ctx.builder.getInt32(4096));
            } else if (ctx.classes.count(tn) && !ctx.classes[tn].isBuiltin &&
                       ctx.classes[tn].type != nullptr) {
                initClassContainerMembers(gv, tn);
            }
        }

        // Visit all declarations in file order (preserving execution order).
        // visitDeclStmt will detect is_global=true and skip alloca creation,
        // only emitting the initializer at the correct point in the sequence.
        // Top-level-Style-Check: OPT-IN via env var `AIML_STRICT_TOPLEVEL`.
        // Gesetzt (irgendein truthy Wert) → top-level imperative Statements
        // sind Compile-Errors.  Default (env unset oder "0") → kein Check.
        //
        // Hintergrund: top-level Imperative laufen in __aiml_init und
        // unterliegen dem decl-Hoisting-Problem (decl mit call-initializer
        // feuert vor imperativen Statements).  In `main()` ist die Ordnung
        // deterministisch.  `main` wird automatisch vom C-entrypoint gerufen.
        const char* strictEnv = getenv("AIML_STRICT_TOPLEVEL");
        bool strict = false;
        if (strictEnv && *strictEnv) {
            std::string s = strictEnv;
            strict = !(s == "0" || s == "off" || s == "OFF" || s == "false" || s == "FALSE");
        }

        ctx.inTopLevelInit = true;
        // Restore the insert point to __aiml_init when a previous FunctionDecl
        // left it inside a freshly-compiled function body.  Only restore when
        // we're not already inside __aiml_init (otherwise MapImpl/ListImpl
        // mid-flow blocks like `put_end` would be abandoned without terminator).
        auto ensureInitPoint = [&]() {
            llvm::BasicBlock* cur = static_cast<llvm::BasicBlock*>(
                ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
            if (!cur || cur->getParent() != llvmInitFunc) {
                auto* lastBB = &llvmInitFunc->back();
                ctx.ir_gen->setInsertPoint(ctx.ir_gen->wrapBlock(lastBB));
            }
        };
        bool hasExplicitMainCall = false;
        for (const auto& decl : prog->declarations) {
            ensureInitPoint();
            visitTopLevel(decl.get());
            if (!strict) continue;
            // Erlaubt sind import/class/def/decl/extern_block/model.
            if (auto* stmt = dynamic_cast<Stmt*>(decl.get())) {
                if (auto* dst = dynamic_cast<DeclStmt*>(stmt)) {
                    if (dst->initialValue && dynamic_cast<CallExpr*>(dst->initialValue.get())) {
                        LOG_ERROR(ctx.fmtLoc(dst->sourceNode) + "top-level decl '" + dst->varName +
                            "' has a call() initializer — wird gehoist und läuft VOR " +
                            "imperative top-level Statements.  In main() verschieben.");
                    }
                } else if (auto* ex = dynamic_cast<ExprStmt*>(stmt)) {
                    if (auto* ce = dynamic_cast<CallExpr*>(ex->expr.get())) {
                        if (ce->functionName == "main") hasExplicitMainCall = true;
                    }
                    LOG_ERROR(ctx.fmtLoc(ex->sourceNode) + "top-level imperative statement (" +
                        ex->expr->toString() + ") — move into a `main` function (auto-called by compiler).");
                } else if (dynamic_cast<SetStmt*>(stmt) || dynamic_cast<IfStmt*>(stmt) ||
                           dynamic_cast<WhileStmt*>(stmt) || dynamic_cast<IdxStmt*>(stmt)) {
                    LOG_ERROR(ctx.fmtLoc(stmt->sourceNode) + "top-level imperative statement (" +
                        stmt->toString() + ") — move into a `main` function (auto-called by compiler).");
                }
            }
        }
        ctx.inTopLevelInit = false;

        // `main` wird vom generateEntryPoint() in compiler.cpp automatisch
        // gerufen (renamed → __aiml_user_main, dann aus dem C-entrypoint).
        // Ein explizites `- call: [main]` am top-level würde main DOPPELT rufen.
        if (strict && hasExplicitMainCall) {
            LOG_ERROR("explicit `- call: [main]` at top-level causes a double-call: "
                      "`main` is already invoked by the C entry-point.  Remove the explicit call.");
        }

        // Finish __aiml_init
        IRBlock* currentBlock = ctx.ir_gen->getInsertBlock();
        if (!static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(currentBlock))->getTerminator()) {
            ctx.ir_gen->createRet(nullptr);
        }

        // ── Static check: same_arg ─────────────────────────────────────────
        // 3+ freie Funktionen, die irgendwo in ihrer Arg-Liste dasselbe Pärchen
        // (Name, Typ) tragen, sind ein starkes Signal dafür, dass sie Methoden
        // auf einer Klasse sein sollten.  Pro Funktion via
        // `static_off: ["same_arg"]` abschaltbar.  Class-Methoden werden NICHT
        // geprüft — die sind schon klassen-organisiert.
        {
            std::map<std::string, std::vector<FunctionDecl*>> byArg;
            for (const auto& d : prog->declarations) {
                auto* fn = dynamic_cast<FunctionDecl*>(d.get());
                if (!fn) continue;
                // Pro Arg-Pärchen in dieser Funktion einen Eintrag.  Dedup über
                // Funktion: wenn dieselbe Funktion das Pärchen mehrfach hat
                // (was eigentlich nicht vorkommt), nur einmal zählen.
                std::set<std::string> seen;
                for (const auto& a : fn->args) {
                    std::string key = a.first + ":" + a.second;
                    if (seen.insert(key).second) byArg[key].push_back(fn);
                }
            }
            for (const auto& [key, fns] : byArg) {
                if (fns.size() < 3) continue;
                std::vector<FunctionDecl*> hits;
                for (auto* fn : fns) {
                    bool off = false;
                    for (const auto& s : fn->staticOff) {
                        if (s == "same_arg") { off = true; break; }
                    }
                    if (!off) hits.push_back(fn);
                }
                if (hits.empty()) continue;  // alle opt-out → OK
                std::string names;
                for (size_t i = 0; i < fns.size(); ++i) {
                    if (i > 0) names += ", ";
                    names += fns[i]->name;
                }
                for (auto* fn : hits) {
                    LOG_ERROR(ctx.fmtLoc(fn->sourceNode) + "function '" + fn->name +
                        "' shares arg '" + key + "' with " +
                        std::to_string(fns.size() - 1) + " other function(s) (" + names +
                        ").  These should be methods on a class.  Add `static_off: [\"same_arg\"]` to this def to bypass.");
                }
            }
        }

        // Static check: string-literal comparison (enum/regex hints).  Runs for
        // BOTH entry and libraryMode (see checkStringComparisons) so imported
        // modules are checked too — unlike same_arg which is entry-only.
        checkStringComparisons(prog);
        checkPtrIndirection(prog);
        checkFrameBlocking(prog);
        checkPrefabUsage(prog);

        // Emit hot-reload support tables (after all functions and types are registered)
        emitFptable(ctx);
        emitSchemaRegistry(ctx);
    }
}

// Post-registration validation of function-cascade method pools (spec/57).  Runs
// after all types are registered, so a pool may reference member classes / a bound
// cascade|enum declared anywhere in the module (forward references are fine).
void Compiler::checkCascadeClasses(Program* prog) {
    auto boundTargetExists = [&](const std::string& n, bool wantEnum) {
        return wantEnum
            ? (ctx.enums.count(n)    || ctx.enums.count(ctx.qualify(n)))
            : (ctx.cascades.count(n) || ctx.cascades.count(ctx.qualify(n)));
    };
    // A member class's `renders:` must name a declared cascade or enum.
    if (prog) {
        for (const auto& d : prog->declarations) {
            auto* cls = dynamic_cast<ClassDecl*>(d.get());
            if (!cls) continue;
            if (!cls->rules.empty() && cls->renders.empty())
                LOG_ERROR("class '" + cls->name + "': has `rules:` but no `renders:` "
                          "(a rule-bundle must name the cascade|enum it renders).");
            for (const auto& r : cls->rules)
                if (!r->def)
                    LOG_ERROR("class '" + cls->name + "': a rule has no `def:` handler.");
            if (cls->renders.empty()) continue;
            if (!boundTargetExists(cls->renders, /*wantEnum=*/false) &&
                !boundTargetExists(cls->renders, /*wantEnum=*/true))
                LOG_ERROR("class '" + cls->name + "': renders '" + cls->renders +
                          "' is not a declared cascade or enum.");
        }
    }
    for (auto& kv : ctx.cascadeClasses) {
        auto& info = kv.second;
        if (info.bindsEnum) {
            if (!ctx.enums.count(info.boundName) &&
                !ctx.enums.count(ctx.qualify(info.boundName)))
                LOG_ERROR("cascade_class '" + info.name + "': binds_enum '" +
                          info.boundName + "' is not a declared enum.");
        } else {
            if (!ctx.cascades.count(info.boundName) &&
                !ctx.cascades.count(ctx.qualify(info.boundName)))
                LOG_ERROR("cascade_class '" + info.name + "': binds_cascade '" +
                          info.boundName + "' is not a declared cascade.");
        }
        if (info.members.empty())
            LOG_ERROR("cascade_class '" + info.name +
                      "': `members:` must list at least one class.");
        for (const auto& m : info.members) {
            if (!ctx.classes.count(m) && !ctx.classes.count(ctx.qualify(m)))
                LOG_ERROR("cascade_class '" + info.name + "': member '" + m +
                          "' is not a declared class.");
        }
    }
    checkCascadeClassSemantics(prog);
}

// M2 (slice 1) — the static α A β pass for function-cascade pools.  For each
// binds_cascade pool: enumerate the top-level path per opcode (opcode member +
// direct field types; nested cascades are ONE opaque stage in this slice), resolve
// one rule per stage by context + specificity, then verify the carrier chain is
// type-continuous (carrier → … → taker).  Nested-cascade recursion is a later slice.
void Compiler::checkCascadeClassSemantics(Program* prog) {
    if (!prog) return;

    // className → ClassDecl* (rules live on the AST, not in ctx).
    std::map<std::string, ClassDecl*> classByName;
    for (const auto& d : prog->declarations)
        if (auto* c = dynamic_cast<ClassDecl*>(d.get()))
            classByName[c->name] = c;

    // A Mask matches an element token iff wildcard, or "any value of the stage"
    // (empty options), or one of its options matches.  An option matches by exact
    // token, OR type-level: a cascade name matches any of its sub-opcode (node)
    // names, an enum name matches any of its members — so `on: OwnerRef` /
    // `before: [revive, OwnerRef]` still match the flattened member tokens.
    auto optMatches = [&](const std::string& o, const std::string& token) -> bool {
        if (o == token) return true;
        std::string ck = ctx.cascades.count(o) ? o
                       : (ctx.cascades.count(ctx.qualify(o)) ? ctx.qualify(o) : "");
        if (!ck.empty() && ctx.cascades[ck].byName.count(token)) return true;
        // Type-scoped (spec/57): an enum option matches a cascade-NODE token ONLY
        // when the enum is the OPCODE-enum of the cascade that owns that node.  A
        // flat enum field sharing member NAMES with a cascade's nodes (the value-
        // identical subset-enum pattern, e.g. PlayerRelation vs OwnerRef) must NOT
        // claim those node tokens.  (o == token and cascade-name matches above.)
        std::string ek = ctx.enums.count(o) ? o
                       : (ctx.enums.count(ctx.qualify(o)) ? ctx.qualify(o) : "");
        if (!ek.empty()) {
            for (const auto& cp : ctx.cascades) {
                const auto& ci = cp.second;
                if (ci.opcode.empty() || !ci.byName.count(token)) continue;
                std::string oek = ctx.enums.count(ci.opcode) ? ci.opcode
                                : (ctx.enums.count(ctx.qualify(ci.opcode))
                                       ? ctx.qualify(ci.opcode) : ci.opcode);
                if (oek == ek) return true;
            }
        }
        return false;
    };
    auto maskMatches = [&](const Mask& m, const std::string& token) -> bool {
        if (m.is_wildcard || m.options.empty()) return true;
        for (const auto& o : m.options) if (optMatches(o, token)) return true;
        return false;
    };
    // `before` is the prefix TAIL: its masks align to the last |before| prefix
    // stages.  Empty before → matches any prefix.
    auto beforeMatches = [&](const std::vector<Mask>& before,
                             const std::vector<std::string>& prefix) -> bool {
        if (before.size() > prefix.size()) return false;
        size_t off = prefix.size() - before.size();
        for (size_t j = 0; j < before.size(); ++j)
            if (!maskMatches(before[j], prefix[off + j])) return false;
        return true;
    };
    // `after` is the suffix HEAD.  `after: []` (hasAfter && empty) = terminal →
    // suffix must be empty.  Absent after → matches any suffix.
    auto afterMatches = [&](const RuleDecl* r,
                            const std::vector<std::string>& suffix) -> bool {
        if (r->hasAfter && r->after.empty()) return suffix.empty();
        if (r->after.size() > suffix.size()) return false;
        for (size_t j = 0; j < r->after.size(); ++j)
            if (!maskMatches(r->after[j], suffix[j])) return false;
        return true;
    };
    auto slotScore = [](const Mask& m) -> int {
        if (m.is_wildcard || m.options.empty()) return 0;
        return m.options.size() == 1 ? 2 : 1;
    };
    auto specificity = [&](const RuleDecl* r) -> int {
        int s = slotScore(r->on);
        for (const auto& m : r->before) s += slotScore(m);
        for (const auto& m : r->after)  s += slotScore(m);
        return s;
    };

    for (auto& kv : ctx.cascadeClasses) {
        auto& pool = kv.second;
        if (pool.bindsEnum) continue;   // enum-bound pools: later slice

        std::string cn = ctx.cascades.count(pool.boundName) ? pool.boundName
                       : ctx.cascades.count(ctx.qualify(pool.boundName))
                             ? ctx.qualify(pool.boundName) : "";
        if (cn.empty()) continue;       // already reported by checkCascadeClasses
        const auto& casc = ctx.cascades[cn];

        // Gather rules from the pool's member classes (found in this module).
        std::vector<RuleDecl*> rules;
        for (const auto& m : pool.members) {
            auto it = classByName.find(m);
            if (it == classByName.end()) continue;
            for (const auto& r : it->second->rules) rules.push_back(r.get());
        }
        if (rules.empty()) continue;

        // Carrier accessors (nominal string types).
        auto carryIn  = [](RuleDecl* r) -> std::string {
            return (r && r->def && !r->def->args.empty()) ? r->def->args[0].second : "";
        };
        auto carryOut = [](RuleDecl* r) -> std::string {
            return (r && r->def) ? r->def->returnType : "";
        };
        // Dedup identical diagnostics across the many enumerated paths.
        std::set<std::string> emitted;
        auto emit = [&](const std::string& msg) {
            if (emitted.insert(msg).second) LOG_ERROR(msg);
        };
        // Field type → cascade registry key ("" if not a cascade).
        auto cascKey = [&](const std::string& tn) -> std::string {
            if (ctx.cascades.count(tn)) return tn;
            std::string q = ctx.qualify(tn);
            return ctx.cascades.count(q) ? q : "";
        };
        // Enumerate every flat token-path for one node, recursing (and branching)
        // into nested-cascade fields.  `stack` guards cyclic schemas — a cascade
        // already being expanded is treated as one opaque stage.
        std::function<std::vector<std::vector<std::string>>(
            const CompilerContext::CascadeInfo&, const CompilerContext::CascadeNodeInfo&,
            std::vector<std::string>&)> flatten =
            [&](const CompilerContext::CascadeInfo& c,
                const CompilerContext::CascadeNodeInfo& n,
                std::vector<std::string>& stack) -> std::vector<std::vector<std::string>> {
            (void)c;
            std::vector<std::vector<std::string>> paths{ { n.name } };   // opcode token
            for (const auto& f : n.fields) {
                std::string ck = cascKey(f.typeName);
                bool cyclic = false;
                for (const auto& s : stack) if (s == ck) { cyclic = true; break; }
                if (!ck.empty() && !cyclic) {
                    const auto& sub = ctx.cascades[ck];
                    stack.push_back(ck);
                    std::vector<std::vector<std::string>> subPaths;
                    for (const auto& sn : sub.nodes)
                        for (auto& sp : flatten(sub, sn, stack)) subPaths.push_back(std::move(sp));
                    stack.pop_back();
                    std::vector<std::vector<std::string>> next;
                    for (auto& p : paths)
                        for (auto& sp : subPaths) {
                            std::vector<std::string> merged = p;
                            merged.insert(merged.end(), sp.begin(), sp.end());
                            next.push_back(std::move(merged));
                        }
                    paths = std::move(next);
                } else {
                    for (auto& p : paths) p.push_back(f.typeName);   // enum/alias/opaque
                }
            }
            return paths;
        };
        // Match + carrier-chain check for one concrete flat path.
        auto processPath = [&](const std::string& opcodeName,
                               const std::vector<std::string>& path) {
            std::vector<RuleDecl*> resolved(path.size(), nullptr);
            for (size_t i = 0; i < path.size(); ++i) {
                std::vector<std::string> prefix(path.begin(), path.begin() + i);
                std::vector<std::string> suffix(path.begin() + i + 1, path.end());
                // Most-specific wins; `weight:` breaks a tie; unresolved tie = error.
                RuleDecl* best = nullptr; int bestScore = -1, bestWeight = 0; bool tie = false;
                for (auto* r : rules) {
                    if (!maskMatches(r->on, path[i])) continue;
                    if (!beforeMatches(r->before, prefix)) continue;
                    if (!afterMatches(r, suffix)) continue;
                    int sc = specificity(r), w = r->weight;
                    if (!best || sc > bestScore || (sc == bestScore && w > bestWeight)) {
                        best = r; bestScore = sc; bestWeight = w; tie = false;
                    } else if (sc == bestScore && w == bestWeight) tie = true;
                }
                if (!best)
                    emit("cascade_class '" + pool.name + "': no handler for opcode '" +
                         opcodeName + "' element '" + path[i] + "'.");
                else if (tie)
                    emit("cascade_class '" + pool.name + "': ambiguous handlers for opcode '" +
                         opcodeName + "' element '" + path[i] + "' — add `weight:` or make one "
                         "context more specific.");
                resolved[i] = best;
            }
            for (size_t i = 0; i < resolved.size(); ++i) {
                if (!resolved[i]) continue;
                if (i > 0 && !resolved[i - 1]) continue;
                std::string in   = carryIn(resolved[i]);
                std::string want = (i == 0) ? pool.carrier : carryOut(resolved[i - 1]);
                if (in != want)
                    emit("cascade_class '" + pool.name + "': carrier break at opcode '" +
                         opcodeName + "' element '" + path[i] + "' — handler takes '" + in +
                         "' but the chain carries '" + want + "'.");
            }
            if (!resolved.empty() && resolved.back()) {
                std::string out = carryOut(resolved.back());
                if (out != pool.taker)
                    emit("cascade_class '" + pool.name + "': terminal carrier at opcode '" +
                         opcodeName + "' is '" + out + "' but taker is '" + pool.taker + "'.");
            }
        };

        // Per opcode: enumerate all flat paths (nested cascades expand + branch).
        for (const auto& node : casc.nodes) {
            std::vector<std::string> stack{ cn };   // guard the top cascade too
            for (auto& path : flatten(casc, node, stack))
                processPath(node.name, path);
        }
    }
}

// Parse a single `- def:` YAML snippet into a FunctionDecl (reuses the real parser,
// so synthesized `render` bodies use correct AST — no hand-built nodes).
static std::unique_ptr<FunctionDecl> parseDefSnippet(const std::string& yamlText) {
    AIML::AST::Builder b;
    std::istringstream ss(yamlText);
    YAML::Parser parser(ss);
    if (!parser.HandleNextDocument(b)) return nullptr;
    auto prog = b.takeProgram();
    if (!prog) return nullptr;
    for (auto& d : prog->declarations)
        if (dynamic_cast<FunctionDecl*>(d.get()))
            return std::unique_ptr<FunctionDecl>(static_cast<FunctionDecl*>(d.release()));
    return nullptr;
}

// M3a — synthesize each function-cascade pool's `render(carrier, block) -> taker` as a
// real class, injected + registered so user code can `{new: Pool}` / `pool.render(...)`.
// Slice scope: binds_cascade pools whose cascade has NO nested-cascade field (straight
// per-opcode handler chain).  Nested runtime dispatch is a later slice; those pools are
// skipped here (their `render` is not emitted yet).
void Compiler::synthesizeCascadeClasses(Program* prog) {
    if (!prog) return;
    std::map<std::string, ClassDecl*> classByName;
    for (const auto& d : prog->declarations)
        if (auto* c = dynamic_cast<ClassDecl*>(d.get())) classByName[c->name] = c;

    auto optMatches = [&](const std::string& o, const std::string& token) -> bool {
        if (o == token) return true;
        std::string ck = ctx.cascades.count(o) ? o
                       : (ctx.cascades.count(ctx.qualify(o)) ? ctx.qualify(o) : "");
        if (!ck.empty() && ctx.cascades[ck].byName.count(token)) return true;
        // Type-scoped (spec/57): an enum option matches a cascade-NODE token ONLY
        // when the enum is the OPCODE-enum of the cascade that owns that node.  A
        // flat enum field sharing member NAMES with a cascade's nodes (the value-
        // identical subset-enum pattern, e.g. PlayerRelation vs OwnerRef) must NOT
        // claim those node tokens.  (o == token and cascade-name matches above.)
        std::string ek = ctx.enums.count(o) ? o
                       : (ctx.enums.count(ctx.qualify(o)) ? ctx.qualify(o) : "");
        if (!ek.empty()) {
            for (const auto& cp : ctx.cascades) {
                const auto& ci = cp.second;
                if (ci.opcode.empty() || !ci.byName.count(token)) continue;
                std::string oek = ctx.enums.count(ci.opcode) ? ci.opcode
                                : (ctx.enums.count(ctx.qualify(ci.opcode))
                                       ? ctx.qualify(ci.opcode) : ci.opcode);
                if (oek == ek) return true;
            }
        }
        return false;
    };
    auto maskMatches = [&](const Mask& m, const std::string& token) -> bool {
        if (m.is_wildcard || m.options.empty()) return true;
        for (const auto& o : m.options) if (optMatches(o, token)) return true;
        return false;
    };
    auto beforeMatches = [&](const std::vector<Mask>& before,
                             const std::vector<std::string>& prefix) -> bool {
        if (before.size() > prefix.size()) return false;
        size_t off = prefix.size() - before.size();
        for (size_t j = 0; j < before.size(); ++j)
            if (!maskMatches(before[j], prefix[off + j])) return false;
        return true;
    };
    auto afterMatches = [&](const RuleDecl* r, const std::vector<std::string>& suffix) -> bool {
        if (r->hasAfter && r->after.empty()) return suffix.empty();
        if (r->after.size() > suffix.size()) return false;
        for (size_t j = 0; j < r->after.size(); ++j)
            if (!maskMatches(r->after[j], suffix[j])) return false;
        return true;
    };
    auto slotScore = [](const Mask& m) -> int {
        if (m.is_wildcard || m.options.empty()) return 0;
        return m.options.size() == 1 ? 2 : 1;
    };
    auto specificity = [&](const RuleDecl* r) -> int {
        int s = slotScore(r->on);
        for (const auto& m : r->before) s += slotScore(m);
        for (const auto& m : r->after)  s += slotScore(m);
        return s;
    };

    std::vector<std::unique_ptr<Node>> toAppend;

    for (auto& kv : ctx.cascadeClasses) {
        auto& pool = kv.second;
        if (pool.bindsEnum) continue;
        std::string cn = ctx.cascades.count(pool.boundName) ? pool.boundName
                       : (ctx.cascades.count(ctx.qualify(pool.boundName))
                              ? ctx.qualify(pool.boundName) : "");
        if (cn.empty()) continue;
        const auto& casc = ctx.cascades[cn];

        std::vector<RuleDecl*> rules;
        for (const auto& m : pool.members) {
            auto it = classByName.find(m);
            if (it == classByName.end()) continue;
            for (const auto& r : it->second->rules) rules.push_back(r.get());
        }
        if (rules.empty()) continue;

        // M5 — recursive per-cascade dispatch (replaces the M3c flat-path emit that was
        // O(∏ nested-branch-factors) and exploded on deep nesting).  Emit ONE shared
        // `dispatch_<Cascade>(carry)` method per (cascade, context): switch on block[<.idx]
        // — one branch per NODE — advancing idx and threading the carrier through the
        // resolved per-node/per-field handler; a nested-cascade field CALLS its own
        // dispatch method (recursion, not inlining) → O(Σ nodes).  Context uses type-token
        // prefixes so `before:[opcode, OwnerRef]` matches regardless of a nested owner's
        // structural length (removes the all_in_zone field-shift bug class by construction).
        auto cascKey2 = [&](const std::string& tn) -> std::string {
            if (ctx.cascades.count(tn)) return tn;
            std::string q = ctx.qualify(tn);
            return ctx.cascades.count(q) ? q : "";
        };
        size_t maxBeforeLen = 1;
        for (auto* r : rules) if (r->before.size() + 1 > maxBeforeLen) maxBeforeLen = r->before.size() + 1;

        std::map<RuleDecl*, std::string> methodName;   // handler rule → method name
        int mctr = 0;
        auto handlerMethod = [&](RuleDecl* r) -> std::string {
            auto it = methodName.find(r);
            if (it != methodName.end()) return it->second;
            std::string m = "handler_" + std::to_string(mctr++);
            methodName[r] = m; return m;
        };
        // Best rule for (onTok, prefix, suffix); nullptr on gap/tie (already reported by M2).
        auto resolve = [&](const std::string& onTok, const std::vector<std::string>& prefix,
                           const std::vector<std::string>& suffix) -> RuleDecl* {
            RuleDecl* best = nullptr; int bs = -1, bw = 0; bool tie = false;
            for (auto* r : rules) {
                if (!maskMatches(r->on, onTok) || !beforeMatches(r->before, prefix) ||
                    !afterMatches(r, suffix)) continue;
                int sc = specificity(r), w = r->weight;
                if (!best || sc > bs || (sc == bs && w > bw)) { best = r; bs = sc; bw = w; tie = false; }
                else if (sc == bs && w == bw) tie = true;
            }
            return (!best || tie) ? nullptr : best;
        };

        std::vector<std::string> dispatchDefs;              // AIML text per dispatch method
        std::map<std::string, std::string> dispatchMemo;    // ctx-key → dispatch method name
        int dctr = 0; bool ok = true;
        // Recursively generate (and memoise) a dispatch method for cascade `C` reached with
        // `prefix` (type-token context).  Returns the method name.
        std::function<std::string(const CompilerContext::CascadeInfo&, std::vector<std::string>)>
        genDispatch = [&](const CompilerContext::CascadeInfo& C,
                          std::vector<std::string> prefix) -> std::string {
            // Key by cascade + the resolution-relevant prefix tail (bounds contexts → also
            // terminates on cyclic cascades).
            std::vector<std::string> tail = prefix;
            if (tail.size() > maxBeforeLen) tail.erase(tail.begin(), tail.end() - maxBeforeLen);
            std::string key = C.name + "|";
            for (auto& t : tail) key += t + ",";
            auto mit = dispatchMemo.find(key);
            if (mit != dispatchMemo.end()) return mit->second;
            std::string dm = "dispatch_" + std::to_string(dctr++);
            dispatchMemo[key] = dm;                         // reserve before recursing (cycle guard)

            std::string body = "      - decl: [dspv, i32, { at: [<.blk, <.idx] }]\n";
            for (const auto& N : C.nodes) {
                std::vector<std::string> opAfter;
                for (auto& f : N.fields) opAfter.push_back(f.typeName);
                RuleDecl* opRule = resolve(N.name, prefix, opAfter);
                if (!opRule) { ok = false; return dm; }
                body += "      - if:\n          condition: { eq: [dspv, " + std::to_string(N.id) +
                        "] }\n          then:\n";
                body += "            - set: [carry, { call: [<." + handlerMethod(opRule) + ", carry] }]\n";
                body += "            - set: [<.idx, { +: [<.idx, 1] }]\n";
                std::vector<std::string> curPrefix = prefix; curPrefix.push_back(N.name);
                for (size_t fi = 0; fi < N.fields.size(); ++fi) {
                    const auto& f = N.fields[fi];
                    std::vector<std::string> after;
                    for (size_t j = fi + 1; j < N.fields.size(); ++j) after.push_back(N.fields[j].typeName);
                    std::string ck = cascKey2(f.typeName);
                    if (!ck.empty()) {                      // nested cascade → recurse
                        std::string subM = genDispatch(ctx.cascades[ck], curPrefix);
                        if (!ok) return dm;
                        body += "            - set: [carry, { call: [<." + subM + ", carry] }]\n";
                    } else {                                // flat enum/alias field
                        RuleDecl* fr = resolve(f.typeName, curPrefix, after);
                        if (!fr) { ok = false; return dm; }
                        body += "            - set: [carry, { call: [<." + handlerMethod(fr) + ", carry] }]\n";
                        body += "            - set: [<.idx, { +: [<.idx, 1] }]\n";
                    }
                    curPrefix.push_back(f.typeName);
                }
                body += "            - return: carry\n";
            }
            body += "      - return: carry\n";
            dispatchDefs.push_back(
                "- def:\n    name: " + dm + "\n    args: [[carry, " + pool.carrier +
                "]]\n    return: " + pool.taker + "\n    body:\n" + body);
            return dm;
        };

        std::string topDispatch = genDispatch(casc, {});
        if (!ok) continue;   // gaps/ties already reported by the semantic pass

        std::string renderText =
            "- def:\n    name: render\n    args: [[carry, " + pool.carrier +
            "], [block, list<i32>]]\n    return: " + pool.taker +
            "\n    body:\n      - set: [<.blk, block]\n      - set: [<.idx, 0]\n"
            "      - return: { call: [<." + topDispatch + ", carry] }\n";
        auto renderFn = parseDefSnippet(renderText);
        if (!renderFn) {
            LOG_ERROR("cascade_class '" + pool.name + "': internal — failed to synthesize render.");
            continue;
        }

        auto poolClass = std::make_unique<ClassDecl>();
        poolClass->name = pool.name;
        // Implicit state: the block being walked + the current index; `field()` reads
        // the current element so handlers stay position-agnostic (spec/57 self.field).
        StructMember blkM; blkM.name = "blk"; blkM.type = "list<i32>";
        StructMember idxM; idxM.name = "idx"; idxM.type = "i32";
        idxM.initialValue = std::make_unique<LiteralExpr>("0", "i32");
        poolClass->members.push_back(std::move(blkM));
        poolClass->members.push_back(std::move(idxM));
        if (auto fieldFn = parseDefSnippet(
                "- def:\n    name: field\n    args: []\n    return: i32\n"
                "    body:\n      - return: { at: [<.blk, <.idx] }\n"))
            poolClass->methods.push_back(std::move(fieldFn));
        for (auto& mn : methodName) {          // move handler defs in (renamed)
            mn.first->def->name = mn.second;
            poolClass->methods.push_back(std::move(mn.first->def));
        }
        for (auto& dd : dispatchDefs)          // synthesized dispatch methods
            if (auto df = parseDefSnippet(dd)) poolClass->methods.push_back(std::move(df));
        poolClass->methods.push_back(std::move(renderFn));

        registerTypeDecl(poolClass.get());     // register type + forward-declare methods
        toAppend.push_back(std::move(poolClass));
        LOG_INFO("synthesized render for cascade_class '" + pool.name + "' (" +
                 std::to_string(dispatchDefs.size()) + " dispatch methods, " +
                 std::to_string(methodName.size()) + " handlers)");
    }
    for (auto& n : toAppend) prog->declarations.push_back(std::move(n));
}

// ── classgroup (spec/58) ────────────────────────────────────────────────────
// Fold every external `groupmethods:` block into its target group's instances
// BEFORE validation/synthesis: the collected `methods:` are moved onto the named
// ClassGroupInstance.functions, so from here on an externally-defined method is
// indistinguishable from one written inline in the classgroup body.  This lets a
// coordinator's methods live in its own file while the group body stays lean.
// Header-only classgroup synthesis for imported files (spec/58 §11).  Folds any
// `groupmethods:` in `prog`, then synthesizes the container + per-instance
// subclass ClassDecls (appended to prog->declarations, types registered).  The
// caller (resolveImportCallback) then visitTopLevel's the appended ClassDecls in
// header-only mode to forward-declare their methods; the bodies are emitted only
// in the unit that defines the classgroup.  Validation (checkClassGroups) is
// intentionally skipped here — the defining unit reports any wiring errors.
void Compiler::synthesizeImportedGroups(Program* prog) {
    if (!prog) return;
    mergeGroupMethods(prog);
    synthesizeClassGroups(prog);
}

void Compiler::mergeGroupMethods(Program* prog) {
    if (!prog) return;
    // Only classgroups DEFINED in this prog can receive methods now; a group not
    // yet in scope keeps its pending `groupmethods:` for the unit that has both.
    std::map<std::string, ClassGroupDecl*> groupByName;
    for (const auto& d : prog->declarations)
        if (auto* gg = dynamic_cast<ClassGroupDecl*>(d.get())) groupByName[gg->name] = gg;
    if (groupByName.empty()) return;

    // Consume matching entries from the cross-file pending list (spec/58 §11):
    // a `groupmethods:` block and its target group may be in different units, so
    // the block was recorded in ctx during type registration.  Fold every pending
    // block whose group is present here; leave the rest for another prog.
    auto& pending = ctx.pendingGroupMethods;
    for (auto it = pending.begin(); it != pending.end(); ) {
        GroupMethodsDecl* gm = *it;
        auto git = groupByName.find(gm->group);
        if (git == groupByName.end()) { ++it; continue; }   // group not here — keep
        ClassGroupDecl* g = git->second;
        for (auto& tgt : gm->targets) {
            ClassGroupInstance* inst = nullptr;
            for (auto& ci : g->instances)
                if (ci.instanceName == tgt.instanceName) { inst = &ci; break; }
            if (!inst) {
                LOG_ERROR(ctx.fmtLoc() + "groupmethods for group '" + gm->group +
                          "': unknown instance '" + tgt.instanceName + "'.");
                continue;
            }
            for (auto& fn : tgt.methods)
                if (fn) inst->functions.push_back(std::move(fn));
            tgt.methods.clear();
        }
        it = pending.erase(it);   // consumed
    }
}

// Validate one architecture cluster's wiring: instance-name uniqueness, base
// class existence, sees targets resolve to same-group instances (no self, no
// dup), and no function overrides a base method.  Runs before synthesis so an
// invalid group is rejected rather than desugared.
void Compiler::checkClassGroups(Program* prog) {
    if (!prog) return;
    std::map<std::string, ClassDecl*> classByName;
    std::map<std::string, ClassGroupDecl*> groupByName;
    for (const auto& d : prog->declarations) {
        if (auto* c = dynamic_cast<ClassDecl*>(d.get()))       classByName[c->name] = c;
        if (auto* gg = dynamic_cast<ClassGroupDecl*>(d.get())) groupByName[gg->name] = gg;
    }

    // Collect a class's method names, walking `extends`.  Uses ctx.classes (the
    // registered type info) so IMPORTED classes are seen too — the AST classByName
    // only holds same-compilation-unit ClassDecls (library mode compiles per file).
    std::function<void(const std::string&, std::set<std::string>&, std::set<std::string>&)>
    collectMethods = [&](const std::string& cn0, std::set<std::string>& out,
                         std::set<std::string>& seen) {
        std::string cn = ctx.classes.count(cn0) ? cn0 : ctx.qualify(cn0);
        if (!ctx.classes.count(cn) || !seen.insert(cn).second) return;
        for (const auto& mn : ctx.classes[cn].methodNames) {
            out.insert(mn);
            auto dot = mn.rfind('.');            // also the unqualified form
            if (dot != std::string::npos) out.insert(mn.substr(dot + 1));
        }
        if (!ctx.classes[cn].parentName.empty()) collectMethods(ctx.classes[cn].parentName, out, seen);
    };

    for (const auto& d : prog->declarations) {
        auto* g = dynamic_cast<ClassGroupDecl*>(d.get());
        if (!g) continue;

        // pass 1: instance identity + base class existence.
        std::set<std::string> instNames;
        for (auto& inst : g->instances) {
            if (inst.instanceName.empty())
                LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name +
                          "': an instance is missing `instance:`.");
            else if (!instNames.insert(inst.instanceName).second)
                LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name +
                          "': duplicate instance name '" + inst.instanceName + "'.");
            if (inst.className.empty())
                LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': instance '" +
                          inst.instanceName + "' is missing `class:`.");
            else if (!ctx.classes.count(inst.className) &&
                     !ctx.classes.count(ctx.qualify(inst.className)))
                LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': instance '" +
                          inst.instanceName + "' names unknown class '" + inst.className + "'.");
        }

        // subgroups: member name unique (shares the container namespace with
        // instances) + the referenced group type exists.
        for (auto& sg : g->subgroups) {
            if (!instNames.insert(sg.first).second)
                LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name +
                          "': subgroup name '" + sg.first +
                          "' clashes with another instance/subgroup.");
            if (!groupByName.count(sg.second))
                LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': subgroup '" +
                          sg.first + "' names unknown group '" + sg.second + "'.");
        }
        // exports must name real instances of THIS group.
        for (auto& e : g->exports)
            if (!instNames.count(e))
                LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name +
                          "': export '" + e + "' is not an instance of this group.");

        // requires: PORT names (spec/58 §6) — unique across the instance/subgroup
        // namespace + the port's declared base class exists.  A `sees: [port]` edge
        // (pass 2) resolves against these.
        std::set<std::string> portNames;
        for (auto& p : g->ports) {
            if (p.first.empty())
                LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name +
                          "': a `requires` port is missing its name.");
            else if (instNames.count(p.first) || !portNames.insert(p.first).second)
                LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': port '" + p.first +
                          "' clashes with an instance/subgroup/port of the same name.");
            if (!p.second.empty() && !ctx.classes.count(p.second) &&
                !ctx.classes.count(ctx.qualify(p.second)))
                LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': port '" + p.first +
                          "' names unknown class '" + p.second + "'.");
        }

        // Subgroup PORT BINDINGS: `subgroups: [[fx, EffectRuntime, {cards: cards}]]`.
        // Every binding names a real port of the child group and a real instance of
        // THIS group whose class satisfies the port; and every child port must be
        // bound (no unmet dependency).
        auto isSubclassOf = [&](const std::string& sub0, const std::string& base) {
            std::string sub = ctx.classes.count(sub0) ? sub0 : ctx.qualify(sub0);
            std::string b   = ctx.classes.count(base)  ? base  : ctx.qualify(base);
            std::set<std::string> seen;
            while (!sub.empty() && seen.insert(sub).second) {
                if (sub == b) return true;
                if (!ctx.classes.count(sub)) break;
                sub = ctx.classes[sub].parentName;
                if (!sub.empty() && !ctx.classes.count(sub)) sub = ctx.qualify(sub);
            }
            return sub0 == base;
        };
        auto instClass = [&](const std::string& n) -> std::string {
            for (auto& gi : g->instances) if (gi.instanceName == n) return gi.className;
            return "";
        };
        for (auto& sg : g->subgroups) {
            auto git = groupByName.find(sg.second);
            if (git == groupByName.end()) continue;   // unknown group already reported
            ClassGroupDecl* cg = git->second;
            auto bit = g->subgroupPorts.find(sg.first);
            std::set<std::string> bound;
            if (bit != g->subgroupPorts.end())
                for (auto& b : bit->second) {
                    // port exists on the child group?
                    bool portExists = false;
                    std::string portClass;
                    for (auto& cp : cg->ports) if (cp.first == b.first) { portExists = true; portClass = cp.second; }
                    if (!portExists)
                        LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': subgroup '" + sg.first +
                                  "' binds unknown port '" + b.first + "' of group '" + sg.second + "'.");
                    // bound target is a real instance of THIS group?
                    std::string bc = instClass(b.second);
                    if (bc.empty())
                        LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': subgroup '" + sg.first +
                                  "' binds port '" + b.first + "' to '" + b.second +
                                  "' which is not an instance of this group.");
                    else if (!portClass.empty() && !isSubclassOf(bc, portClass))
                        LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': subgroup '" + sg.first +
                                  "' binds port '" + b.first + "' (needs '" + portClass + "') to '" +
                                  b.second + "' of incompatible class '" + bc + "'.");
                    if (portExists && !bound.insert(b.first).second)
                        LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': subgroup '" + sg.first +
                                  "' binds port '" + b.first + "' more than once.");
                }
            for (auto& cp : cg->ports)
                if (!bound.count(cp.first))
                    LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': subgroup '" + sg.first +
                              "' (group '" + sg.second + "') leaves port '" + cp.first + "' unbound.");
        }

        // pass 2: sees targets (exist / not self / not dup) + no override.
        for (auto& inst : g->instances) {
            std::set<std::string> seenSees;
            for (auto& s : inst.sees) {
                if (!seenSees.insert(s).second)
                    LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': instance '" +
                              inst.instanceName + "' lists sees '" + s + "' twice.");
                // Strip an optional `:Iface` suffix (interface-typed edge, §5.2).
                std::string tpart = s, iface;
                auto colon = tpart.find(':');
                if (colon != std::string::npos) { iface = tpart.substr(colon + 1); tpart = tpart.substr(0, colon); }
                if (!iface.empty() && !ctx.interfaces.count(iface) &&
                    !ctx.interfaces.count(ctx.qualify(iface)))
                    LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': instance '" +
                              inst.instanceName + "' sees '" + s +
                              "' names unknown interface '" + iface + "'.");
                auto dot = tpart.find('.');
                if (dot == std::string::npos) {
                    // Same-group sibling, or a declared PORT (child→parent injection).
                    if (tpart == inst.instanceName)
                        LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': instance '" +
                                  inst.instanceName + "' sees itself.");
                    else if (!instNames.count(tpart) && !portNames.count(tpart))
                        LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': instance '" +
                                  inst.instanceName + "' sees unknown instance '" + tpart + "'.");
                    continue;
                }
                // Cross-group `subgroup.instance` — must resolve to an EXPORTED
                // instance of an embedded subgroup (spec/58 §6 encapsulation).
                std::string sgName = tpart.substr(0, dot), tgt = tpart.substr(dot + 1);
                std::string sgType;
                for (auto& sg : g->subgroups) if (sg.first == sgName) sgType = sg.second;
                if (sgType.empty()) {
                    LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': instance '" +
                              inst.instanceName + "' sees '" + s + "' but there is no subgroup '" +
                              sgName + "'.");
                    continue;
                }
                auto git = groupByName.find(sgType);
                if (git == groupByName.end()) continue;   // unknown group already reported
                ClassGroupDecl* cg = git->second;
                bool found = false, exported = cg->exports.empty();
                for (auto& gi : cg->instances) if (gi.instanceName == tgt) found = true;
                for (auto& e : cg->exports)     if (e == tgt) exported = true;
                if (!found)
                    LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': sees '" + s +
                              "' — group '" + sgType + "' has no instance '" + tgt + "'.");
                else if (!exported)
                    LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': sees '" + s +
                              "' — '" + tgt + "' is not exported by group '" + sgType +
                              "' (spec/58 §6).");
            }
            std::set<std::string> baseMethods, visited;
            collectMethods(inst.className, baseMethods, visited);
            for (auto& fn : inst.functions)
                if (baseMethods.count(fn->name))
                    LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': function '" +
                              fn->name + "' on instance '" + inst.instanceName +
                              "' overrides a base method of '" + inst.className +
                              "' — overriding is not allowed (spec/58 §7).");
            // A constructor names a base method run in phase 0 (sees-blind).
            if (!inst.constructor.empty() && !baseMethods.count(inst.constructor[0]))
                LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': instance '" +
                          inst.instanceName + "' constructor '" + inst.constructor[0] +
                          "' is not a method of base '" + inst.className + "'.");
            // Declared interfaces must exist (subclass conformance — that a base
            // method or group-function satisfies each — is enforced downstream).
            for (auto& itf : inst.implements)
                if (!ctx.interfaces.count(itf) && !ctx.interfaces.count(ctx.qualify(itf)))
                    LOG_ERROR(ctx.fmtLoc() + "classgroup '" + g->name + "': instance '" +
                              inst.instanceName + "' implements unknown interface '" + itf + "'.");
        }
    }
}

// Desugar each classgroup: one synthesized subclass per instance (extends its
// base; `sees` → base-typed ptr members; `functions` → methods) plus a container
// class holding one subclass-typed ptr member per instance and a synthesized
// `__ctor` that allocates every instance (phase 0) then back-patches the sees
// pointers (phase 1).  visitNewExpr calls `__ctor` on {new: <group>}.
void Compiler::synthesizeClassGroups(Program* prog) {
    if (!prog) return;
    std::vector<std::unique_ptr<Node>> toAppend;
    std::map<std::string, ClassGroupDecl*> groupByName;
    for (auto& d : prog->declarations)
        if (auto* gg = dynamic_cast<ClassGroupDecl*>(d.get())) groupByName[gg->name] = gg;

    for (auto& d : prog->declarations) {
        auto* g = dynamic_cast<ClassGroupDecl*>(d.get());
        if (!g) continue;

        auto subName = [&](const std::string& inst) { return g->name + "_" + inst; };
        // instance → its base class (for base-typed sees members).
        std::map<std::string, std::string> baseOf;
        std::map<std::string, std::set<std::string>> implementsOf;   // instance → its interfaces
        std::set<std::string> postHookInsts;   // instances with an `on_wired` hook
        for (auto& inst : g->instances) {
            baseOf[inst.instanceName] = inst.className;
            implementsOf[inst.instanceName].insert(inst.implements.begin(), inst.implements.end());
        }
        // PORTS (spec/58 §6, `requires:`): port name → declared base class.  A
        // `sees: [port]` edge synthesizes a base-typed collaborator, wired to a
        // parent instance in a deferred `__wire_ports` pass (child→parent).
        std::map<std::string, std::string> portClassOf;
        for (auto& p : g->ports) portClassOf[p.first] = p.second;
        bool hasPorts = !g->ports.empty();
        auto isPortName = [&](const std::string& n) { return portClassOf.count(n) > 0; };
        auto subgTypeOf = [&](const std::string& sgName) -> std::string {
            for (auto& sg : g->subgroups) if (sg.first == sgName) return sg.second;
            return "";
        };
        // Resolve a sees target to the member name it gets on the seeing subclass,
        // the concrete base class it points at, and (if the edge is written
        // `inst:Iface`) the interface it is typed as.  Handles same-group names and
        // cross-group `subgroup.instance`.  False = dangling (already reported).
        auto resolveSees = [&](const std::string& s0, std::string& memberName,
                               std::string& baseType, std::string& iface) -> bool {
            iface.clear();
            std::string s = s0;
            auto colon = s.find(':');
            if (colon != std::string::npos) { iface = s.substr(colon + 1); s = s.substr(0, colon); }
            auto dot = s.find('.');
            if (dot == std::string::npos) {
                auto it = baseOf.find(s);
                if (it != baseOf.end() && !it->second.empty()) {
                    memberName = s; baseType = it->second; return true;
                }
                // A port name resolves to its declared base class (base-typed edge).
                auto pit = portClassOf.find(s);
                if (pit != portClassOf.end()) { memberName = s; baseType = pit->second; return true; }
                return false;
            }
            std::string sgName = s.substr(0, dot), tgt = s.substr(dot + 1);
            std::string sgType = subgTypeOf(sgName);
            if (sgType.empty()) return false;
            auto git = groupByName.find(sgType);
            if (git == groupByName.end()) return false;
            for (auto& gi : git->second->instances)
                if (gi.instanceName == tgt) { memberName = tgt; baseType = gi.className; return true; }
            return false;
        };

        // 1) one subclass per instance.
        for (auto& inst : g->instances) {
            auto sub = std::make_unique<ClassDecl>();
            sub->name = subName(inst.instanceName);
            sub->parentName = inst.className;
            // Instance-declared interfaces (spec/58 §5.2): satisfied by the base
            // methods AND the group `functions` (which live on this subclass), so a
            // group-function can implement an interface method and be called across
            // an interface-typed sees edge.  A vtable is emitted for the subclass.
            sub->interfaces = inst.implements;
            for (auto& s : inst.sees) {
                // Base-typed (spec/58 §5 option A): the base class is already
                // registered, so this resolves even for a sees cycle; cross-edge
                // calls hit the base API.  Cross-group `subgroup.instance` targets
                // resolve to the child's exported instance base.  Skip a dangling
                // target (already reported) so synthesis never builds a `ptr<>`.
                std::string mName, baseType, iface;
                if (!resolveSees(s, mName, baseType, iface)) continue;
                // If the base class already declares a member of this name, the
                // classgroup WIRES that member (phase 1) instead of synthesizing a
                // duplicate — so ordinary base methods can use the collaborator via
                // `<.name` (spec/58 §5.1).  Only for concrete edges.
                if (iface.empty() && ctx.classes.count(inst.className) &&
                    ctx.classes[inst.className].members.count(mName))
                    continue;
                StructMember m;
                m.name = mName;
                if (!iface.empty()) {
                    m.type = "ptr<" + iface + ">";
                } else if (isPortName(mName)) {
                    // Port edge (spec/58 §6): base-typed collaborator, wired to a
                    // parent instance in `__wire_ports`.  Deliberately the port's
                    // declared base (not a child subclass) — a service API.
                    m.type = "ptr<" + baseType + ">";
                } else {
                    // Concrete edge: type the member as the sibling's SUBCLASS
                    // (inheritor, `Group_target`), not the base.  The subclass is
                    // exactly the instance the group constructs, so the edge exposes
                    // its FULL API — base methods AND its group `functions` — via
                    // ordinary member resolution + inheritance (spec/58 §5.1).
                    std::string sStrip = s;
                    auto sc = sStrip.find(':'); if (sc != std::string::npos) sStrip = sStrip.substr(0, sc);
                    auto sd = sStrip.find('.');
                    std::string subT = (sd == std::string::npos)
                        ? subName(mName)                                   // same-group instance
                        : (subgTypeOf(sStrip.substr(0, sd)) + "_" + mName); // subgroup.instance
                    m.type = "ptr<" + subT + ">";
                }
                m.is_collaborator = true;   // wired collaborator — not serialized (spec/58 §9)
                sub->members.push_back(std::move(m));
            }
            for (auto& fn : inst.functions) {
                // Record the post-wire hook BEFORE moving the functions out (after
                // the move, inst.functions holds null unique_ptrs).
                if (fn->name == "on_wired") postHookInsts.insert(inst.instanceName);
                sub->methods.push_back(std::move(fn));
            }
            registerTypeDecl(sub.get());
            toAppend.push_back(std::move(sub));
        }

        // 2) container class: group params → members (settable at {new: G, p: v});
        //    one subclass-typed ptr per instance; a synthesized __ctor.
        auto cont = std::make_unique<ClassDecl>();
        cont->name = g->name;
        std::set<std::string> paramNames;
        for (auto& p : g->params) {
            StructMember m;
            m.name = p.first;
            m.type = p.second;
            cont->members.push_back(std::move(m));
            paramNames.insert(p.first);
        }
        for (auto& sg : g->subgroups) {   // embedded child groups
            StructMember m;
            m.name = sg.first;
            m.type = "ptr<" + sg.second + ">";
            cont->members.push_back(std::move(m));
        }
        // Port members (spec/58 §6): one `ptr<BaseClass>` per required port.  The
        // parent sets these to its own instances before calling `__wire_ports`;
        // they are wired collaborators (not serialized).
        for (auto& p : g->ports) {
            StructMember m;
            m.name = p.first;
            m.type = "ptr<" + p.second + ">";
            m.is_collaborator = true;
            cont->members.push_back(std::move(m));
        }
        for (auto& inst : g->instances) {
            StructMember m;
            m.name = inst.instanceName;
            m.type = "ptr<" + subName(inst.instanceName) + ">";
            cont->members.push_back(std::move(m));
        }

        // __ctor body.  Phase 0: allocate each instance, grab a local ptr, and run
        // its constructor (a base method; sees-blind) with mapped args — a group
        // param → <.param, anything else a literal.  Phase 1: back-patch sees
        // through the same one-level locals (`local.member = <.target>`).
        std::string body;
        // Construct embedded child groups first (their own __ctor runs via the
        // {new:} hook) so cross-group sees can wire to them in phase 1.
        for (auto& sg : g->subgroups)
            body += "      - set: [<." + sg.first + ", { new: " + sg.second + " }]\n";
        for (auto& inst : g->instances) {
            body += "      - set: [<." + inst.instanceName + ", { new: " +
                    subName(inst.instanceName) + " }]\n";
            // A __ctor local is needed only if this instance is wired here (a
            // sibling / cross-group sees), constructed, or runs on_wired here.
            // Pure-port sees are wired in __wire_ports, so they don't count.
            bool hasWiredSees = false;
            for (auto& s : inst.sees) {
                std::string t = s; auto c = t.find(':'); if (c != std::string::npos) t = t.substr(0, c);
                if (t.find('.') != std::string::npos) { hasWiredSees = true; break; } // subgroup.instance
                if (!isPortName(t))                   { hasWiredSees = true; break; } // sibling
            }
            bool needLocal = hasWiredSees || !inst.constructor.empty() ||
                             (postHookInsts.count(inst.instanceName) && !hasPorts);
            if (needLocal)
                body += "      - decl: [w_" + inst.instanceName + ", ptr<" +
                        subName(inst.instanceName) + ">, <." + inst.instanceName + "]\n";
            if (!inst.constructor.empty()) {
                std::string line = "      - call: [w_" + inst.instanceName + "." +
                                   inst.constructor[0];
                for (size_t ai = 1; ai < inst.constructor.size(); ++ai) {
                    const std::string& arg = inst.constructor[ai];
                    line += ", " + (paramNames.count(arg) ? ("<." + arg) : arg);
                }
                line += "]\n";
                body += line;
            }
        }
        // Port injection into embedded subgroups (spec/58 §6).  Now that both the
        // subgroup containers (phase 0) and this group's own instances exist, bind
        // each configured port to a parent instance and run the child's deferred
        // `__wire_ports` (which wires the child instances + runs their on_wired).
        for (auto& sg : g->subgroups) {
            auto bit = g->subgroupPorts.find(sg.first);
            if (bit == g->subgroupPorts.end() || bit->second.empty()) continue;
            for (auto& b : bit->second)
                body += "      - set: [<." + sg.first + "." + b.first + ", <." + b.second + "]\n";
            body += "      - call: [<." + sg.first + ".__wire_ports]\n";
        }

        std::set<std::string> declaredSubgLocals;
        for (auto& inst : g->instances) {
            if (inst.sees.empty()) continue;
            for (auto& s : inst.sees) {
                std::string mName, baseType, iface;
                if (!resolveSees(s, mName, baseType, iface)) continue;   // dangling — reported
                // Access path to the concrete target instance (strip any :Iface).
                std::string tpart = s;
                auto colon = tpart.find(':');
                if (colon != std::string::npos) tpart = tpart.substr(0, colon);
                auto dot = tpart.find('.');
                // Port edges (spec/58 §6) are wired later, in `__wire_ports`, after
                // the parent has bound the port to one of its instances.
                if (dot == std::string::npos && isPortName(tpart)) continue;
                std::string srcExpr;
                if (dot == std::string::npos) {
                    srcExpr = "<." + tpart;
                } else {
                    // Cross-group: read the child's instance off the subgroup member
                    // through a one-level local.
                    std::string sgName = tpart.substr(0, dot), tgt = tpart.substr(dot + 1);
                    std::string local = "sc_" + sgName;
                    if (declaredSubgLocals.insert(local).second)
                        body += "      - decl: [" + local + ", ptr<" + subgTypeOf(sgName) +
                                ">, <." + sgName + "]\n";
                    srcExpr = local + "." + tgt;
                }
                if (iface.empty()) {
                    body += "      - set: [w_" + inst.instanceName + "." + mName +
                            ", " + srcExpr + "]\n";
                } else {
                    std::string up = "up_" + inst.instanceName + "_" + mName;
                    // Same-group target that declares `implements: [Iface]` carries the
                    // vtable on its SUBCLASS (an instance/group-function may satisfy the
                    // interface) → upcast the subclass directly.  Otherwise the concrete
                    // BASE carries the vtable → view-as-base then upcast.
                    bool directOnSub = (dot == std::string::npos) &&
                                       implementsOf.count(tpart) && implementsOf[tpart].count(iface);
                    if (directOnSub) {
                        // Route through a plain local: the decl-upcast only fires for
                        // a VariableExpr source, not a `<.member` access.
                        std::string sv = "s_" + inst.instanceName + "_" + mName;
                        body += "      - decl: [" + sv + ", ptr<" + subName(tpart) + ">, " + srcExpr + "]\n";
                        body += "      - decl: [" + up + ", ptr<" + iface + ">, " + sv + "]\n";
                    } else {
                        std::string bl = "b_" + inst.instanceName + "_" + mName;
                        body += "      - decl: [" + bl + ", ptr<" + baseType + ">, " + srcExpr + "]\n";
                        body += "      - decl: [" + up + ", ptr<" + iface + ">, " + bl + "]\n";
                    }
                    body += "      - set: [w_" + inst.instanceName + "." + mName + ", " + up + "]\n";
                }
            }
        }
        // Phase 2: post-wire hooks.  After every sees is back-patched, call each
        // instance's `on_wired` group-function (if present).  It may use sees —
        // the home for init that intertwines intrinsic setup with a collaborator
        // (spec/58 §3, the automated "start()").  For a group with PORTS the hooks
        // are DEFERRED to `__wire_ports` (spec/58 §6): a ported child's on_wired
        // must never observe an unbound port, so it runs only after the parent has
        // injected the ports.
        if (!hasPorts)
            for (auto& inst : g->instances)
                if (postHookInsts.count(inst.instanceName))
                    body += "      - call: [w_" + inst.instanceName + ".on_wired]\n";
        std::string ctorText = "- def:\n    name: __ctor\n    args: []\n    body:\n" + body;
        if (auto ctorFn = parseDefSnippet(ctorText))
            cont->methods.push_back(std::move(ctorFn));
        else
            LOG_ERROR("classgroup '" + g->name + "': internal — failed to synthesize __ctor.");

        // `__wire_ports` (spec/58 §6): the parent calls this after it has set the
        // container's port members to its own instances.  It back-patches each
        // ported child instance's collaborator member from the container port, then
        // runs the deferred `on_wired` hooks (now every port is bound).
        if (hasPorts) {
            auto seesPort = [&](const ClassGroupInstance& inst) {
                for (auto& s : inst.sees) {
                    std::string t = s; auto c = t.find(':'); if (c != std::string::npos) t = t.substr(0, c);
                    if (t.find('.') == std::string::npos && isPortName(t)) return true;
                }
                return false;
            };
            std::string wire;
            for (auto& inst : g->instances)
                if (seesPort(inst) || postHookInsts.count(inst.instanceName))
                    wire += "      - decl: [w_" + inst.instanceName + ", ptr<" +
                            subName(inst.instanceName) + ">, <." + inst.instanceName + "]\n";
            for (auto& inst : g->instances)
                for (auto& s : inst.sees) {
                    std::string t = s; auto c = t.find(':'); if (c != std::string::npos) t = t.substr(0, c);
                    if (t.find('.') == std::string::npos && isPortName(t))
                        wire += "      - set: [w_" + inst.instanceName + "." + t + ", <." + t + "]\n";
                }
            for (auto& inst : g->instances)
                if (postHookInsts.count(inst.instanceName))
                    wire += "      - call: [w_" + inst.instanceName + ".on_wired]\n";
            std::string wpText = "- def:\n    name: __wire_ports\n    args: []\n    body:\n" + wire;
            if (auto wpFn = parseDefSnippet(wpText))
                cont->methods.push_back(std::move(wpFn));
            else
                LOG_ERROR("classgroup '" + g->name + "': internal — failed to synthesize __wire_ports.");
        }

        registerTypeDecl(cont.get());
        ctx.classGroupContainers.insert(g->name);
        toAppend.push_back(std::move(cont));
        LOG_INFO("synthesized classgroup '" + g->name + "' (" +
                 std::to_string(g->instances.size()) + " instances)");
    }
    for (auto& n : toAppend) prog->declarations.push_back(std::move(n));
}

void Compiler::registerTypes(Program* prog) {
    if (!prog) return;

    // AIML v2: registerTypes forward-declares functions (model arg/return signatures depend on
    // isRefcountModel → ctx.modelRefcount), and it runs BEFORE Compiler::compile() reads the env.
    // Read the flag here too (idempotent) so forward-decl signatures match the later definitions.
    if (const char* mr = getenv("AIML_MODEL_REFCOUNT")) {
        std::string s = mr;
        ctx.modelRefcount = !(s.empty() || s == "0" || s == "off" || s == "OFF"
                              || s == "false" || s == "FALSE");
    }

    // Inject built-in enum: MirrorEventType — typed companion to the string
    // `mirror_event.type` field.  `evt.type_enum()` returns this enum so code
    // can `eq: [evt.type_enum(), MirrorEventType.peer_connected]` instead of a
    // string switch.  Values MUST match aiml_mirror_event_type_enum().
    if (!ctx.enums.count("MirrorEventType")) {
        ctx.enums["MirrorEventType"] = {
            {"none", 0}, {"peer_connected", 1}, {"peer_disconnected", 2},
            {"object_registered", 3}, {"object_unregistered", 4}
        };
    }

    // Inject BuiltIn Exception Classes
    std::vector<std::string> builtinExceptions = {
        "Exception", "NullPointerException", "TypeMismatchException", "IndexOutOfBoundsException",
        "DivisionByZeroException", "StackOverflowException", "OutOfMemoryException", "TimeoutException",
        "SerializationException", "DeserializationException", "BufferOverflowException", "BufferUnderflowException",
        "InvalidThreadStateException"
    };

    for (const auto& excName : builtinExceptions) {
        if (!ctx.classes.count(excName)) {
             CompilerContext::ClassInfo classInfo;
             classInfo.name = excName;
             classInfo.version = 0;
             classInfo.parentName = excName == "Exception" ? "" : "Exception";
             
             CompilerContext::StructMember msgMember;
             msgMember.typeName = "string";
             msgMember.type = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType("string")));
             msgMember.index = 0;
             msgMember.access = AccessLevel::Public;
             classInfo.members["message"] = msgMember;
             
             std::vector<llvm::Type*> llvmMemberTypes;
             llvmMemberTypes.push_back(msgMember.type);
             
             llvm::StructType* structType = llvm::StructType::create(ctx.context, excName);
             structType->setBody(llvmMemberTypes);
             classInfo.type = structType;
             
             ctx.classes[excName] = classInfo;
        }
    }

    // Fixed-point loop: ModelDecl with `inherits:` and ClassDecl with
    // `parentName:` / `models:` may defer when their dependencies aren't
    // registered yet.  In cross-file imports the declarations can arrive in
    // an order where a dependent type is visited before its dependency, so
    // a single pass is not enough.  Each pass re-tries all declarations;
    // already-registered ones early-return.  Stop when no new types appear.
    //
    // Genuine "missing dependency" errors are NOT emitted here — they
    // surface as "Unknown type" during visitTopLevel.  registerTypes is
    // invoked from several phases (imports, siblings, main, compileFromString),
    // and a final-pass error here would fire prematurely in earlier phases
    // before later phases have a chance to register the missing pieces.
    constexpr int MAX_REGISTRATION_PASSES = 16;
    size_t prevTotal = 0;
    for (int pass = 0; pass < MAX_REGISTRATION_PASSES; ++pass) {
        for (const auto& decl : prog->declarations) {
            registerTypeDecl(decl.get());
        }
        size_t total = ctx.classes.size() + ctx.enums.size();
        if (total == prevTotal) break;
        prevTotal = total;
    }
}

// ── Spec-45: Heap-type descriptor helpers ────────────────────────────────────

// Shift the absolute child-index fields of a descriptor node by delta.
static AimlTypeNode snShift(AimlTypeNode n, int32_t delta) {
    switch (n.kind) {
        case AIML_TD_LIST:
        case AIML_TD_BUFFER:
        case AIML_TD_PTR_STRUCT:
        case AIML_TD_MODEL:
        case AIML_TD_LIST_PTR:
        case AIML_TD_MAP_PTR:
        case AIML_TD_BUFFER_PTR: n.param1 += delta; break;  // param1 = child index (subtree)
        case AIML_TD_MAP:    n.param1 += delta; n.param2 += delta; break;
        case AIML_TD_STRUCT: n.param2 += delta; break; // param1 = n_fields (not an index)
        default: break;
    }
    return n;
}

// Returns true if typeName contains at least one heap-allocated component
// (string, list<T>, map<K:V>, buffer<T>, or user struct with heap members).
static bool serHasHeap(const std::string& typeName, CompilerContext& ctx) {
    if (typeName == "string") return true;
    if (typeName.size() > 5 && typeName.substr(0,5) == "list<")   return true;
    if (typeName.size() > 4 && typeName.substr(0,4) == "map<")    return true;
    if (typeName.size() > 7 && typeName.substr(0,7) == "buffer<") return true;
    // spec/61 §5 Option B (memV3): bare `buffer` is a Class-3 rc handle → heap-bearing, so a model
    // with a buffer field gets serNodes + a drop. OFF keeps the old value-header classification.
    if (ctx.memV3 && typeName == "buffer") return true;
    auto it = ctx.classes.find(typeName);
    if (it != ctx.classes.end() && !it->second.isBuiltin)
        for (auto& [mn, mem] : it->second.members)
            if (serHasHeap(mem.typeName, ctx)) return true;
    return false;
}

// Assemble the flat STRUCT node array from per-field subtrees.
// Each fsub[j] has been built by serBuildSubtree with the field's byte offset.
static std::vector<AimlTypeNode> serBuildStructNodes(
    const std::vector<std::vector<AimlTypeNode>>& fsubs,
    int32_t offset, int32_t struct_sz)
{
    int32_t nf = (int32_t)fsubs.size();
    // children_start[j] = index in result where fsubs[j][1..] begin
    std::vector<int32_t> cs;
    int32_t acc = 1 + nf;
    for (int j = 0; j < nf; ++j) { cs.push_back(acc); acc += (int32_t)fsubs[j].size() - 1; }

    std::vector<AimlTypeNode> out;
    out.push_back({AIML_TD_STRUCT, nf, 1, offset, struct_sz, 0}); // root
    for (int j = 0; j < nf; ++j)                                  // field roots
        out.push_back(snShift(fsubs[j][0], cs[j] - 1));
    for (int j = 0; j < nf; ++j) {                                // field children
        int32_t d = cs[j] - 1;
        for (size_t k = 1; k < fsubs[j].size(); ++k)
            out.push_back(snShift(fsubs[j][k], d));
    }
    return out;
}

// Forward declaration.
static std::vector<AimlTypeNode> serBuildSubtree(
    const std::string& typeName, int32_t offset,
    CompilerContext& ctx, const llvm::DataLayout& dl);

// spec/61 §5 Option B (memV3): build a STRUCT FIELD's subtree. A list<T>/cascade<X> field is a
// POINTER to a refcounted handle → wrap the inner LIST subtree in an AIML_TD_LIST_PTR node at the
// pointer slot. Everything else (and the OFF path) builds the field subtree inline as before. Only
// the FIELD level is wrapped — nested list ELEMENTS (list<list<T>>) stay inline via serBuildSubtree.
static std::vector<AimlTypeNode> serFieldSubtree(
    const std::string& typeName, int32_t offset,
    CompilerContext& ctx, const llvm::DataLayout& dl)
{
    bool memV3ListField = ctx.memV3 &&
        ((typeName.rfind("list<", 0) == 0 && typeName.back() == '>') ||
         (typeName.rfind("cascade<", 0) == 0 && typeName.back() == '>'));
    bool memV3MapField = ctx.memV3 && typeName.rfind("map<", 0) == 0 && typeName.back() == '>';
    bool memV3BufField = ctx.memV3 && (typeName == "buffer" ||
                                       (typeName.rfind("buffer<", 0) == 0 && typeName.back() == '>'));
    if (!memV3ListField && !memV3MapField && !memV3BufField)
        return serBuildSubtree(typeName, offset, ctx, dl);
    std::vector<AimlTypeNode> res;
    if (memV3BufField) {
        res.push_back({AIML_TD_BUFFER_PTR, 1, 0, offset, 8, 0});   // idx 0: pointer slot → BUFFER at 1
        if (typeName == "buffer") {
            // Bare byte buffer: BUFFER(param1=2 → i8 element, esz 1). fre recurses fre(param1) per
            // element, so param1 MUST point at a scalar (i8, a fre no-op), not node 0.
            res.push_back({AIML_TD_BUFFER, 2, 0, 0, 1, 0});        // idx 1
            res.push_back({AIML_TD_I8,     0, 0, 0, 0, 0});        // idx 2: element type
        } else {
            for (auto& n : serBuildSubtree(typeName, 0, ctx, dl)) res.push_back(snShift(n, 1));
        }
        return res;
    }
    std::string inner = (typeName.rfind("cascade<", 0) == 0) ? std::string("list<i32>") : typeName;
    res.push_back({memV3MapField ? AIML_TD_MAP_PTR : AIML_TD_LIST_PTR, 1, 0, offset, 8, 0}); // pointer slot
    for (auto& n : serBuildSubtree(inner, 0, ctx, dl)) res.push_back(snShift(n, 1));
    return res;
}

// DFS active-path guard against recursive types.  A struct that (transitively,
// via ptr<>) refers back to itself — e.g. a back-reference component holding a
// `ptr<Owner>` while the owner holds a `ptr<Component>` — would otherwise make
// serBuildSubtree recurse forever (Owner→ptr<Comp>→Comp→ptr<Owner>→…) and blow
// the stack.  We record each struct type currently being expanded; when a ptr<T>
// points at a type already on the path we stop following it (emit an opaque
// pointer leaf below) instead of descending.  A *path* set (erased on unwind),
// not a global visited-set, so the same type may still appear in sibling
// subtrees — only genuine ancestor cycles are cut.  This mirrors the
// classgroup-collaborator skip a few lines down.
static thread_local std::set<std::string> g_serActivePath;

// Build a flat DFS descriptor node array for typeName.  Root is at index 0.
// offset = byte offset of this value within its parent struct (0 = top-level).
static std::vector<AimlTypeNode> serBuildSubtree(
    const std::string& typeName, int32_t offset,
    CompilerContext& ctx, const llvm::DataLayout& dl)
{
    std::vector<AimlTypeNode> res;

    // ── Primitive scalars ─────────────────────────────────────────────────────
    if (typeName=="string")                                    { res.push_back({AIML_TD_STRING, 0,0,offset,0,0}); return res; }
    if (typeName=="i8"||typeName=="u8"||typeName=="bool")      { res.push_back({AIML_TD_I8,     0,0,offset,0,0}); return res; }
    if (typeName=="i16"||typeName=="u16")                      { res.push_back({AIML_TD_I16,    0,0,offset,0,0}); return res; }
    if (typeName=="i32"||typeName=="u32"||typeName=="f32")     { res.push_back({AIML_TD_I32,    0,0,offset,0,0}); return res; }
    if (typeName=="i64"||typeName=="u64"||typeName=="f64")     { res.push_back({AIML_TD_I64,    0,0,offset,0,0}); return res; }

    // ── list<T> ───────────────────────────────────────────────────────────────
    if (typeName.size()>5 && typeName.substr(0,5)=="list<") {
        std::string inner = typeName.substr(5, typeName.size()-6);
        int32_t esz;
        // Determine element size without calling getLLVMType on unregistered types
        // (which would log spurious errors during Phase-A sibling type registration).
        if (ctx.isRefcountModel(inner)) {
            esz = 8;  // AIML v2 P3/P6: list<Model> stores %M* references (pointer stride)
        } else if (ctx.classes.count(inner) && ctx.classes[inner].type) {
            esz = (int32_t)dl.getTypeAllocSize(ctx.classes[inner].type);
        } else if (ctx.enums.count(inner)) {
            esz = 4;  // enum = i32
        } else if (inner.size()>5 && inner.substr(0,5)=="list<") {
            // Target-ABI SerList size (64-bit 16, wasm32 12) — NOT host sizeof.
            esz = aiml::serListHeaderSize(ctx.context, dl);
        } else if (inner.size()>3 && inner.substr(0,4)=="map<") {
            // Target-ABI SerMap size (64-bit 32, wasm32 20).
            esz = aiml::serMapHeaderSize(ctx.context, dl);
        } else if (inner.size()>7 && inner.substr(0,7)=="buffer<") {
            // Target-ABI SerBuffer size (64-bit 24, wasm32 20).
            esz = aiml::serBufferHeaderSize(ctx.context, dl);
        } else if (!inner.empty() && std::isupper((unsigned char)inner[0])) {
            // Unregistered user-defined type during Phase-A — use placeholder.
            // The model depending on this list<T> should also have been deferred,
            // so this branch is a last-resort safety net.
            esz = 8;
        } else {
            llvm::Type* iT = ctx.getLLVMType(inner);
            esz = iT ? (int32_t)dl.getTypeAllocSize(iT) : 8;
        }
        res.push_back({AIML_TD_LIST, 1, 0, offset, esz, 0});
        for (auto& n : serBuildSubtree(inner, 0, ctx, dl)) res.push_back(snShift(n, 1));
        return res;
    }

    // ── buffer<T> ─────────────────────────────────────────────────────────────
    if (typeName.size()>7 && typeName.substr(0,7)=="buffer<") {
        std::string inner = typeName.substr(7, typeName.size()-8);
        llvm::Type* iT = ctx.getLLVMType(inner);
        int32_t esz = iT ? (int32_t)dl.getTypeAllocSize(iT) : 1;
        res.push_back({AIML_TD_BUFFER, 1, 0, offset, esz, 0});
        for (auto& n : serBuildSubtree(inner, 0, ctx, dl)) res.push_back(snShift(n, 1));
        return res;
    }

    // ── map<K:V> ──────────────────────────────────────────────────────────────
    if (typeName.size()>4 && typeName.substr(0,4)=="map<") {
        std::string inner = typeName.substr(4, typeName.size()-5);
        size_t col = inner.find(':');
        std::string kT = (col!=std::string::npos) ? inner.substr(0,col)   : "string";
        std::string vT = (col!=std::string::npos) ? inner.substr(col+1)   : inner;
        auto ksub = serBuildSubtree(kT, 0, ctx, dl);
        auto vsub = serBuildSubtree(vT, 0, ctx, dl);
        int32_t kc = (int32_t)ksub.size();
        // AIML v2 P3/P6: map<K:Model> stores %M* references (pointer value stride).
        int32_t vsz;
        if (ctx.isRefcountModel(vT)) vsz = 8;
        else { llvm::Type* vLT = ctx.getLLVMType(vT); vsz = vLT ? (int32_t)dl.getTypeAllocSize(vLT) : 0; }
        res.push_back({AIML_TD_MAP, 1, 1+kc, offset, vsz, 0});
        for (auto& n : ksub) res.push_back(snShift(n, 1));
        for (auto& n : vsub) res.push_back(snShift(n, 1+kc));
        return res;
    }

    // ── ptr<T> / ref<T> where T is a registered user struct ───────────────────
    // Wraps the inner struct's subtree in an AIML_TD_PTR_STRUCT node.  At
    // encode/decode time the runtime follows / allocates through the pointer
    // (otherwise the raw pointer value is meaningless on the receiver side).
    if ((typeName.rfind("ptr<", 0) == 0 || typeName.rfind("ref<", 0) == 0)
        && typeName.back() == '>') {
        std::string inner = typeName.substr(4, typeName.size() - 5);
        auto pit = ctx.classes.find(inner);
        // …but NOT if `inner` is already being expanded on the current DFS path:
        // that is a recursive back-reference (e.g. Component.owner → ptr<Owner>
        // while Owner is mid-expansion).  Following it would loop forever, so we
        // fall through to the opaque raw-pointer leaf instead — the back-ref is a
        // wired reference, not owned data, and must not be deep-serialised.
        if (pit != ctx.classes.end() && !pit->second.isBuiltin && pit->second.type
            && !g_serActivePath.count(inner)) {
            int32_t inner_sz = (int32_t)dl.getTypeAllocSize(pit->second.type);
            auto sub = serBuildSubtree(inner, 0, ctx, dl);
            res.push_back({AIML_TD_PTR_STRUCT, 1, 0, offset, inner_sz, 0});
            for (auto& n : sub) res.push_back(snShift(n, 1));
            return res;
        }
        // Inner type unknown or a recursive back-ref — fall through to raw-pointer
        // (RAWBYTES, pointer-sized) below.
    }

    // ── AIML v2: a refcount-MODEL reference field/element → AIML_TD_MODEL wrapping the inner
    // struct subtree (mirrors the PTR_STRUCT block: marker + struct bytes; decode allocs a MODL
    // shell, free decrefs). The field/element slot is a %M* pointer (8 bytes). A back-reference to
    // a type already on the DFS path is cut to a raw pointer below (not owned data).
    if (ctx.isRefcountModel(typeName) && !g_serActivePath.count(typeName)) {
        // Unwrap a shared<T>/ptr<T>/ref<T> wrapper to the bare class name — the
        // %M* slot points at a T shell, and ctx.classes is keyed by the bare name.
        std::string modelClass = typeName;
        if (modelClass.rfind("shared<",0)==0 && modelClass.back()=='>')
            modelClass = modelClass.substr(7, modelClass.size()-8);
        else if (modelClass.rfind("ptr<",0)==0 && modelClass.back()=='>')
            modelClass = modelClass.substr(4, modelClass.size()-5);
        else if (modelClass.rfind("ref<",0)==0 && modelClass.back()=='>')
            modelClass = modelClass.substr(4, modelClass.size()-5);
        auto& ci = ctx.classes[modelClass];
        const llvm::StructLayout* sl = dl.getStructLayout(ci.type);
        int32_t inner_sz = (int32_t)dl.getTypeAllocSize(ci.type);
        std::vector<std::pair<std::string, CompilerContext::StructMember>> sm;
        for (auto& [mn, mem] : ci.members) sm.push_back({mn, mem});
        std::sort(sm.begin(), sm.end(),
                  [](auto& a, auto& b){ return a.second.index < b.second.index; });
        bool onPath = g_serActivePath.insert(typeName).second;
        std::vector<std::vector<AimlTypeNode>> fsubs;
        for (auto& [mn, mem] : sm) {
            if (mem.is_collaborator) continue;
            fsubs.push_back(serFieldSubtree(mem.typeName,
                (int32_t)sl->getElementOffset(mem.index), ctx, dl));
        }
        if (onPath) g_serActivePath.erase(typeName);
        auto structNodes = serBuildStructNodes(fsubs, 0, inner_sz);  // struct root at index 0 of subvec
        res.push_back({AIML_TD_MODEL, 1, 0, offset, inner_sz, 0});
        for (auto& n : structNodes) res.push_back(snShift(n, 1));
        return res;
    }

    // ── User struct (must already be in ctx.classes) ──────────────────────────
    {
        auto it = ctx.classes.find(typeName);
        if (it != ctx.classes.end() && !it->second.isBuiltin && it->second.type) {
            auto& ci = it->second;
            const llvm::StructLayout* sl = dl.getStructLayout(ci.type);
            int32_t sz = (int32_t)dl.getTypeAllocSize(ci.type);
            std::vector<std::pair<std::string, CompilerContext::StructMember>> sm;
            for (auto& [mn, mem] : ci.members) sm.push_back({mn, mem});
            std::sort(sm.begin(), sm.end(),
                      [](auto& a, auto& b){ return a.second.index < b.second.index; });
            // Mark this struct as on the active DFS path so a recursive ptr<> back
            // to it (via any member subtree) is cut instead of looping forever.
            bool onPath = g_serActivePath.insert(typeName).second;
            std::vector<std::vector<AimlTypeNode>> fsubs;
            for (auto& [mn, mem] : sm) {
                // classgroup sees-members are wired collaborators, not owned data:
                // skip them (spec/58 §9) — and this breaks the infinite recursion a
                // bidirectional sees cycle would otherwise cause here.
                if (mem.is_collaborator) continue;
                fsubs.push_back(serFieldSubtree(mem.typeName,
                    (int32_t)sl->getElementOffset(mem.index), ctx, dl));
            }
            if (onPath) g_serActivePath.erase(typeName);
            return serBuildStructNodes(fsubs, offset, sz);
        }
    }

    // ── Fallback: opaque / value-only type → raw bytes ────────────────────────
    // If the type name looks like a user-defined struct but is not yet registered
    // (e.g. a sibling type not yet processed), use a placeholder size without
    // triggering a spurious error in getLLVMType.
    if (!typeName.empty() && std::isupper((unsigned char)typeName[0]) &&
        !ctx.classes.count(typeName) && !ctx.enums.count(typeName)) {
        // Unknown user type during type registration phase — treat as opaque.
        res.push_back({AIML_TD_RAWBYTES, 8, 0, offset, 0, 0});
        return res;
    }
    llvm::Type* t = ctx.getLLVMType(typeName);
    int32_t sz = t ? (int32_t)dl.getTypeAllocSize(t) : 8;
    res.push_back({AIML_TD_RAWBYTES, sz, 0, offset, 0, 0});
    return res;
}

// Emit @__aiml_sernodes_<name> as a private constant global; update classInfo.
static void serEmitGlobal(
    const std::string& name,
    const std::vector<AimlTypeNode>& nodes,
    CompilerContext& ctx,
    CompilerContext::ClassInfo& classInfo)
{
    if (nodes.empty()) return;
    llvm::LLVMContext& lctx = ctx.context;
    llvm::Type* i32 = llvm::Type::getInt32Ty(lctx);
    llvm::StructType* nodeTy = llvm::StructType::get(lctx, {i32,i32,i32,i32,i32,i32});
    std::vector<llvm::Constant*> elems;
    for (auto& n : nodes) {
        llvm::Constant* f[6] = {
            llvm::ConstantInt::get(i32, (uint64_t)n.kind),
            llvm::ConstantInt::get(i32, (uint64_t)n.param1),
            llvm::ConstantInt::get(i32, (uint64_t)n.param2),
            llvm::ConstantInt::get(i32, (uint64_t)n.offset),
            llvm::ConstantInt::get(i32, (uint64_t)n.elem_size),
            llvm::ConstantInt::get(i32, 0),
        };
        elems.push_back(llvm::ConstantStruct::get(nodeTy, f));
    }
    llvm::ArrayType* arrTy = llvm::ArrayType::get(nodeTy, elems.size());
    // LinkOnceODRLinkage: wenn zwei Compilation-Units denselben Typ
    // serialisieren (z.B. Modul A öffnet `Kv<string:T>`, importiertes Modul B
    // öffnet dieselbe Kv), emittieren beide identische ser_nodes-Arrays.
    // Mit PrivateLinkage hätte jedes Modul seinen eigenen Pointer; der
    // db-Singleton-Cache (aiml_db_kv_open_typed_persist) registriert beim
    // ersten Open einen der beiden, ignoriert den zweiten — danach passt der
    // Caller mit MISMATCH-Pointer encode mit anderen Offsets. Mit linkonce_odr
    // werden die Symbole vom Linker gemerged: alle Module zeigen auf
    // dieselbe ser_nodes-Definition.
    auto* gv = new llvm::GlobalVariable(ctx.module, arrTy, /*isConstant=*/true,
        llvm::GlobalValue::LinkOnceODRLinkage,
        llvm::ConstantArray::get(arrTy, elems),
        "__aiml_sernodes_" + name);
    gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    gv->setVisibility(llvm::GlobalValue::HiddenVisibility);
    // hasHeapFields is computed by the caller based on serHasHeap; do not force-set it.
    classInfo.serNodesVar   = gv;
    classInfo.serNodesCount = (int32_t)nodes.size();
}

// serBuildSubtree + a reusable LinkOnceODR sernodes global for a container field type. Shared by
// emitContainerFieldFree and emitContainerFieldDeepCopy. Returns null gv (and sets nodeCount 0)
// when the type has no descriptor.
static llvm::GlobalVariable* getFieldSerNodesGlobal(CompilerContext& ctx, const std::string& typeName,
                                                    int& nodeCount) {
    llvm::DataLayout dl(&ctx.module);
    std::vector<AimlTypeNode> nodes = serBuildSubtree(typeName, 0, ctx, dl);
    nodeCount = (int)nodes.size();
    if (nodes.empty()) return nullptr;
    llvm::LLVMContext& lctx = ctx.context;
    llvm::Type* i32Ty = llvm::Type::getInt32Ty(lctx);
    llvm::StructType* nodeTy = llvm::StructType::get(lctx, {i32Ty,i32Ty,i32Ty,i32Ty,i32Ty,i32Ty});
    std::string symName = "__aiml_sernodes_fieldcopy_" + typeName;
    for (char& c : symName) if (c=='<'||c=='>'||c==':'||c==' '||c==','||c=='/') c = '_';
    llvm::GlobalVariable* gv = ctx.module.getGlobalVariable(symName);
    if (!gv) {
        std::vector<llvm::Constant*> elems;
        for (auto& n : nodes) {
            llvm::Constant* f[6] = {
                llvm::ConstantInt::get(i32Ty, (uint64_t)n.kind),
                llvm::ConstantInt::get(i32Ty, (uint64_t)n.param1),
                llvm::ConstantInt::get(i32Ty, (uint64_t)n.param2),
                llvm::ConstantInt::get(i32Ty, (uint64_t)n.offset),
                llvm::ConstantInt::get(i32Ty, (uint64_t)n.elem_size),
                llvm::ConstantInt::get(i32Ty, 0),
            };
            elems.push_back(llvm::ConstantStruct::get(nodeTy, f));
        }
        llvm::ArrayType* arrTy = llvm::ArrayType::get(nodeTy, elems.size());
        gv = new llvm::GlobalVariable(ctx.module, arrTy, /*isConstant=*/true,
            llvm::GlobalValue::LinkOnceODRLinkage,
            llvm::ConstantArray::get(arrTy, elems), symName);
        gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
        gv->setVisibility(llvm::GlobalValue::HiddenVisibility);
    }
    return gv;
}

// spec/61 §7.0 — free the heap a container field currently owns (guarded against dst==src).
bool Compiler::emitContainerFieldFree(llvm::Value* dstPtr, llvm::Value* srcPtr,
                                      const std::string& typeName) {
    if (!dstPtr) return false;
    llvm::LLVMContext& lctx = ctx.context;
    llvm::Type* i32Ty = llvm::Type::getInt32Ty(lctx);
    llvm::Type* i64Ty = llvm::Type::getInt64Ty(lctx);
    llvm::Type* ptrTy = llvm::PointerType::get(lctx, 0);

    // Emit the type-appropriate free inside the dst!=src guard. A list<T> field is freed with
    // aiml_list_free — the SAME per-element deep-free the scope-drop uses (elemKind 0 scalar /
    // 1 string-decref / 2 model deep-free via the element class's serNodes+stride). This matters:
    // aiml_ser_free_decoded frees a list's buffer but NOT its element MODELS' heap (the
    // take_gone_players list<ConnEntry> element strings leaked). map<>/buffer<>/nested fields use
    // aiml_ser_free_decoded via the field descriptor.
    auto emitFree = [&]() -> bool {
        if (typeName.rfind("list<", 0) == 0 && typeName.back() == '>') {
            std::string elemName = typeName.substr(5, typeName.size() - 6);
            int elemKind = 0; int nodeCount = 0; int64_t elemSize = 0;
            llvm::Value* nodes = llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(ptrTy));
            if (elemName == "string") {
                elemKind = 1;
            } else {
                auto ci = ctx.classes.find(elemName);
                if (ci != ctx.classes.end() && ci->second.hasHeapFields
                    && ci->second.serNodesVar && ci->second.type) {
                    elemKind = 2;
                    nodes = static_cast<llvm::Value*>(ci->second.serNodesVar);
                    nodeCount = ci->second.serNodesCount;
                    elemSize = (int64_t)ctx.module.getDataLayout().getTypeAllocSize(ci->second.type);
                }
            }
            llvm::Function* lf = ctx.getFunction("aiml_list_free", ctx.builder.getVoidTy(),
                {ptrTy, i32Ty, i64Ty, ptrTy, i32Ty});
            ctx.builder.CreateCall(lf, { dstPtr, ctx.builder.getInt32(elemKind),
                ctx.builder.getInt64(elemSize), nodes, ctx.builder.getInt32(nodeCount) });
            return true;
        }
        // A value-MODEL field → deep-free via the class's full (recursive) serNodes.
        auto ci = ctx.classes.find(typeName);
        if (ci != ctx.classes.end() && !ci->second.isBuiltin && ci->second.hasHeapFields
            && ci->second.serNodesVar) {
            llvm::Function* sfF = ctx.getFunction("aiml_ser_free_decoded", ctx.builder.getVoidTy(),
                {ptrTy, ptrTy, i32Ty});
            ctx.builder.CreateCall(sfF, { dstPtr,
                static_cast<llvm::Value*>(ci->second.serNodesVar),
                ctx.builder.getInt32(ci->second.serNodesCount) });
            return true;
        }
        // map<>/buffer<>/other container field → generic descriptor free.
        int nodeCount = 0;
        llvm::GlobalVariable* gv = getFieldSerNodesGlobal(ctx, typeName, nodeCount);
        if (!gv) return false;
        llvm::Function* sfF = ctx.getFunction("aiml_ser_free_decoded", ctx.builder.getVoidTy(),
            {ptrTy, ptrTy, i32Ty});
        ctx.builder.CreateCall(sfF, { dstPtr, gv, ctx.builder.getInt32(nodeCount) });
        return true;
    };

    // Guard against a self-assign (dst==src): freeing dst would release the storage the caller is
    // about to read/move from src. When srcPtr is null the free is unconditional.
    llvm::Function* curFn = ctx.builder.GetInsertBlock()->getParent();
    auto* freeBB = llvm::BasicBlock::Create(lctx, "fieldfree.do", curFn);
    auto* contBB = llvm::BasicBlock::Create(lctx, "fieldfree.cont", curFn);
    if (srcPtr) {
        llvm::Value* dstI = ctx.builder.CreatePtrToInt(dstPtr, ctx.getSizeType());
        llvm::Value* srcI = ctx.builder.CreatePtrToInt(srcPtr, ctx.getSizeType());
        llvm::Value* distinct = ctx.builder.CreateICmpNE(dstI, srcI, "fieldfree_distinct");
        ctx.builder.CreateCondBr(distinct, freeBB, contBB);
    } else {
        ctx.builder.CreateBr(freeBB);
    }
    ctx.builder.SetInsertPoint(freeBB);
    bool ok = emitFree();
    if (ok) {
        // Zero the field after releasing it: the caller then writes the new value (memcpy / store /
        // aiml_ser_deep_copy) into a clean field, and any redundant downstream free (e.g.
        // emitContainerFieldDeepCopy's own guarded free) becomes a no-op on the nulled pointers.
        if (llvm::Type* fieldTy = ctx.getLLVMType(typeName)) {
            uint64_t sz = ctx.module.getDataLayout().getTypeAllocSize(fieldTy);
            llvm::Function* memsetF = ctx.getFunction("memset", ptrTy, {ptrTy, i32Ty, ctx.getSizeType()});
            ctx.builder.CreateCall(memsetF, { dstPtr, ctx.builder.getInt32(0), ctx.getSizeConst(sz) });
        }
    }
    ctx.builder.CreateBr(contBB);
    ctx.builder.SetInsertPoint(contBB);
    return ok;
}

bool Compiler::emitContainerFieldDeepCopy(llvm::Value* dstPtr, llvm::Value* srcPtr,
                                          const std::string& typeName) {
    if (!dstPtr || !srcPtr) return false;
    llvm::DataLayout dl(&ctx.module);
    llvm::LLVMContext& lctx = ctx.context;
    llvm::Type* i32Ty = llvm::Type::getInt32Ty(lctx);
    llvm::Type* ptrTy = llvm::PointerType::get(lctx, 0);
    int nodeCount = 0;
    llvm::GlobalVariable* gv = getFieldSerNodesGlobal(ctx, typeName, nodeCount);
    if (!gv) return false;
    // spec/61: the destination container field usually already owns heap — either the empty
    // default storage that `new`+initClassContainerMembers allocated (get_base's box → copy_header
    // assigns mana_cost/active_abilities/passive_abilities), or prior data being replaced. The
    // memset below zeroes the field's pointers WITHOUT freeing that storage → it leaks (the
    // get_base per-card leak). Release it first (guarded against dst==src, see emitContainerFieldFree).
    emitContainerFieldFree(dstPtr, srcPtr, typeName);
    // aiml_ser_deep_copy requires dst zero-inited (all heap pointers NULL). Zero the field first.
    if (llvm::Type* fieldTy = ctx.getLLVMType(typeName)) {
        uint64_t sz = dl.getTypeAllocSize(fieldTy);
        llvm::Function* memsetF = ctx.getFunction("memset", ptrTy, {ptrTy, i32Ty, ctx.getSizeType()});
        ctx.builder.CreateCall(memsetF, { dstPtr, ctx.builder.getInt32(0), ctx.getSizeConst(sz) });
    }
    llvm::Function* dcF = ctx.getFunction("aiml_ser_deep_copy", ctx.builder.getVoidTy(),
        {ptrTy, ptrTy, ptrTy, i32Ty});
    ctx.builder.CreateCall(dcF, { dstPtr, srcPtr, gv, ctx.builder.getInt32(nodeCount) });
    return true;
}

void Compiler::registerTypeDecl(Node* node) {
    if (auto ns = dynamic_cast<NamespaceDecl*>(node)) {
        ctx.currentNamespace = ns->name;
        LOG_DEBUG("AST Passed Namespace: " + ns->name);
    } else if (auto cfg = dynamic_cast<ConfigDecl*>(node)) {
        if (!cfg->entryPoint.empty()) ctx.config.entryPointName = cfg->entryPoint;
        if (cfg->stackSize) ctx.config.stackSize = cfg->stackSize;
        if (cfg->heapSize) ctx.config.heapSize = cfg->heapSize;
        if (!cfg->target.empty()) ctx.config.target = cfg->target;
        if (!cfg->version.empty()) ctx.config.version = cfg->version;
    } else if (auto imp = dynamic_cast<ImportDecl*>(node)) {
        LOG_DEBUG("AST Processing Import: " + imp->path);
        if (ctx.resolveImportCallback) {
            ctx.resolveImportCallback(imp->path);
        } else {
            LOG_ERROR(ctx.fmtLoc(imp->sourceNode) + "resolveImportCallback not bound! Cannot process import: " + imp->path);
        }
    } else if (auto cls = dynamic_cast<ClassDecl*>(node)) {
        std::string className = ctx.qualify(cls->name);
        ctx.validateIdentifier(cls->name, "class", cls->sourceNode);
        if (ctx.classes.count(className)) {
            // Hard-error if a model with the same name already exists.
            // Models hold data; classes hold behavior — sharing a name conflates
            // two concepts and creates ambiguity in the type system.  Use
            // `class: FooBehavior models: [Foo]` instead.
            if (ctx.classes[className].isModel) {
                LOG_ERROR(ctx.fmtLoc(cls->sourceNode) +
                    "class '" + className + "' conflicts with model '" + className + "': "
                    "same-name model and class is forbidden. "
                    "Rename the class (e.g. '" + className + "Behavior') and reference "
                    "the model via 'models: [" + className + "]'.");
            }
            // Backfill: older Phase-A passes (or older compiler revisions) may
            // have registered this class with empty `interfaces`/`usedModels`.
            // buildInterfaceUpcast needs `interfaces` to validate cross-file
            // forward-decls, so fill it in here if a richer AST node is seen.
            auto& ci = ctx.classes[className];
            if (ci.interfaces.empty() && !cls->interfaces.empty())
                ci.interfaces = cls->interfaces;
            if (ci.usedModels.empty() && !cls->models.empty())
                ci.usedModels = cls->models;
            return; // Already registered
        }

        CompilerContext::ClassInfo classInfo;
        classInfo.name = className;
        classInfo.version = cls->version;
        classInfo.compiled_version = cls->version;  // locked at compile time
        classInfo.parentName = cls->parentName;
        classInfo.interfaces = cls->interfaces;     // implements: list
        classInfo.usedModels = cls->models;         // models:     list
        classInfo.sourceYaml = YAML::Dump(cls->sourceNode);
        
        std::vector<std::pair<std::string, CompilerContext::StructMember>> orderedMembers;
        int currentIndex = 0;
        
        // 0. Defer if parent or any model isn't registered yet.  The
        // fixed-point loop in registerTypes will retry on the next pass.
        // Errors for genuinely-unresolvable deps are emitted in the
        // post-loop report (see registerTypes).
        if (!classInfo.parentName.empty() && !ctx.classes.count(classInfo.parentName)) {
            LOG_DEBUG("Deferring class '" + className + "': parent '" +
                classInfo.parentName + "' not yet registered");
            return;
        }
        for (const auto& modelName : cls->models) {
            if (!ctx.classes.count(modelName)) {
                LOG_DEBUG("Deferring class '" + className + "': model '" +
                    modelName + "' not yet registered");
                return;
            }
        }

        // 1. Parent
        if (!classInfo.parentName.empty()) {
             const auto& parentInfo = ctx.classes[classInfo.parentName];
             for (const auto& [name, member] : parentInfo.members) {
                 orderedMembers.push_back({name, member});
             }
             std::sort(orderedMembers.begin(), orderedMembers.end(), [](auto& a, auto& b){ return a.second.index < b.second.index; });
        }

        // 1.5 Models (Mixins)
        // Iterate the model's members in their original LLVM-struct-field
        // order (member.index), NOT std::map's alphabetical key order.
        // Otherwise the class's struct fields end up in a different order
        // than the model's, while member.index is preserved from the model
        // — causing GEPs to address the wrong field.
        for (const auto& modelName : cls->models) {
             const auto& modelInfo = ctx.classes[modelName];
             std::vector<std::pair<std::string, CompilerContext::StructMember>> sorted;
             for (auto& [name, member] : modelInfo.members)
                 sorted.push_back({name, member});
             std::sort(sorted.begin(), sorted.end(),
                       [](auto& a, auto& b){ return a.second.index < b.second.index; });
             for (auto& p : sorted)
                 orderedMembers.push_back(p);
        }

        currentIndex = orderedMembers.size();
        
        // 2. Class Members (Parsed from AST)
        for (auto& astMember : cls->members) {
            std::string memberName = astMember.name;
            std::string typeName = astMember.type;
            ctx.validateIdentifier(memberName, "class field", cls->sourceNode);

            bool duplicate = false;
            for(const auto& pair : orderedMembers) if(pair.first == memberName) duplicate = true;
            if(duplicate) {
                LOG_ERROR(ctx.fmtLoc(cls->sourceNode) + "Duplicate member '" + memberName + "' in class '" + className + "'");
                continue;
            }
            
            llvm::Type* memberType = ctx.getLLVMType(typeName);
            if (!memberType) {
                LOG_ERROR(ctx.fmtLoc(cls->sourceNode) + "Unknown type '" + typeName + "' for member '" + memberName + "'");
                continue; 
            }

            CompilerContext::StructMember member;
            member.index = currentIndex++;
            member.type = memberType;
            member.typeName = typeName;
            member.is_collaborator = astMember.is_collaborator;
            member.version = astMember.version;
            member.initValue = std::shared_ptr<Expr>(const_cast<Expr*>(astMember.initialValue.release()));
            
            if (astMember.access == "public") member.access = AccessLevel::Public;
            else if (astMember.access == "private") member.access = AccessLevel::Private;
            else if (astMember.access == "protected") member.access = AccessLevel::Protected;
            
            member.is_const = astMember.is_const;
            #ifdef LLVM 18
            // member.is_volatile not supported structurally in opaque pointers.
            // Requires attaching volatile flag to the underlying Load/Store instructions.
            #endif
            // Bridge AionDB db_indexed modifier (valid on class members too).
            member.db_indexed            = astMember.db_indexed;
            member.db_indexed_mode       = astMember.db_indexed_mode;
            member.db_indexed_unique     = astMember.db_indexed_unique;
            member.db_indexed_skip_empty = astMember.db_indexed_skip_empty;

            orderedMembers.push_back({memberName, member});
        }
        
        // Finalize Members.  CRITICAL: re-assign member.index based on
        // each member's POSITION in orderedMembers, because that determines
        // the LLVM struct field-index used by every GEP.  Without this,
        // a model field's old index (from its model's struct) would be
        // preserved but the class's struct layout might place that field
        // at a different position — causing GEPs to address the wrong field.
        std::vector<llvm::Type*> llvmMemberTypes;
        int finalIdx = 0;
        for (auto& [name, member] : orderedMembers) {
            CompilerContext::StructMember m = member;
            m.index = finalIdx++;
            classInfo.members[name] = m;
            llvmMemberTypes.push_back(m.type);
        }

        llvm::StructType* structType = llvm::StructType::create(ctx.context, className);
        structType->setBody(llvmMemberTypes);
        classInfo.type = structType;

        // Compute packed_size / padded_size for hot-reload calloc padding
        {
            int packed = (int)ctx.module.getDataLayout().getTypeAllocSize(structType);
            classInfo.packed_size = packed;
            int padded = 1;
            while (padded < packed) padded <<= 1;
            classInfo.padded_size = padded;
        }

        // Always emit @__aiml_sernodes_<Name> so any class can be used as a
        // typed RPC payload / sys.serialize target — matching the same
        // behaviour models have.  hasHeapFields is still tracked for the
        // kv.get/kv.get_copy diagnostic (see ast_compiler_stmt.cpp).
        {
            bool anyHeap = false;
            for (auto& [mn, mem] : classInfo.members)
                if (serHasHeap(mem.typeName, ctx)) { anyHeap = true; break; }
            classInfo.hasHeapFields = anyHeap;
            const llvm::DataLayout& dl = ctx.module.getDataLayout();
            const llvm::StructLayout* sl = dl.getStructLayout(structType);
            int32_t sz = (int32_t)dl.getTypeAllocSize(structType);
            bool onPathC = g_serActivePath.insert(className).second;  // seed cycle guard
            std::vector<std::vector<AimlTypeNode>> fsubs;
            for (auto& [mn, mem] : orderedMembers)
                fsubs.push_back(serFieldSubtree(mem.typeName,
                    (int32_t)sl->getElementOffset(mem.index), ctx, dl));
            if (onPathC) g_serActivePath.erase(className);
            auto nodes = serBuildStructNodes(fsubs, 0, sz);
            serEmitGlobal(className, nodes, ctx, classInfo);
        }

        // Track user-defined types for sys.bundle dependency closure.
        // collectUserTypes recurses into list<T> / map<K,V> to find inner types.
        auto track = [&](const std::string& typeName) {
            std::set<std::string> found;
            ctx.collectUserTypes(typeName, found);
            classInfo.referencedTypes.insert(found.begin(), found.end());
        };
        for (const auto& [mname, member] : classInfo.members) track(member.typeName);
        track(classInfo.parentName);
        for (const auto& modelName : cls->models)  track(modelName);
        for (const auto& ifaceName : cls->interfaces) track(ifaceName);
        for (const auto& method : cls->methods) {
            for (const auto& arg : method->args) track(arg.second);
            track(method->returnType);
            // Record the mangled method name so dfsCollect can recurse into body types
            classInfo.methodNames.push_back(ctx.qualify(method->name));
        }

        ctx.classes[className] = classInfo;
        LOG_DEBUG("AST Registered Class: " + className);

        /* Forward-declare each method as `ClassName_methodName` so that
         * bodies can invoke sibling methods declared later in the class.
         * Without this pass, `<.render` called from `<.init` fails when
         * `render` appears further down in the source — even though the
         * method IS in the same class, method compilation runs in source
         * order and only inserts each function on-the-fly.  Mirrors the
         * FunctionDecl forward-decl block below at the top level. */
        {
            auto* i8p = llvm::PointerType::get(ctx.context, 0);
            for (const auto& method : cls->methods) {
                std::string mangled = className + "_" + method->name;
                if (ctx.module.getFunction(mangled)) continue;
                std::vector<llvm::Type*> paramTys;
                paramTys.push_back(i8p);   /* implicit this */
                for (const auto& arg : method->args) {
                    // AIML v2 P4: refcount-MODEL arg is by-ref (%M*), like list/map. MUST match
                    // the method definition (visitFunction) or signatures mismatch at the call.
                    bool byref = (arg.second.rfind("list<", 0) == 0 ||
                                  arg.second.rfind("map<",  0) == 0);
                    if (byref || ctx.isRefcountModel(arg.second)) { paramTys.push_back(i8p); continue; }
                    IRType* irt = ctx.ir_gen->resolveType(arg.second);
                    llvm::Type* t = irt
                        ? static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(irt))
                        : i8p;
                    paramTys.push_back(t ? t : i8p);
                }
                llvm::Type* retTy = nullptr;
                if (method->returnType.empty() || method->returnType == "void") {
                    retTy = llvm::Type::getVoidTy(ctx.context);
                } else if (ctx.isRefcountModel(method->returnType)) {
                    retTy = i8p;   // AIML v2 P4: refcount-model return = %M* pointer
                } else {
                    IRType* irt = ctx.ir_gen->resolveType(method->returnType);
                    retTy = irt
                        ? static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(irt))
                        : i8p;
                }
                llvm::FunctionType* ft =
                    llvm::FunctionType::get(retTy, paramTys, false);
                llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                       mangled, ctx.module);
                LOG_DEBUG("registerTypeDecl: forward-declared class method " +
                          mangled);
            }
        }
    } else if (auto mod = dynamic_cast<ModelDecl*>(node)) {
        std::string modelName = ctx.qualify(mod->name);
        ctx.validateIdentifier(mod->name, "model", mod->sourceNode);
        if (ctx.classes.count(modelName)) {
            // Mirror check: if a class registered first under the same name,
            // hard-error.  (See ClassDecl branch above for full rationale.)
            if (!ctx.classes[modelName].isModel) {
                LOG_ERROR(ctx.fmtLoc(mod->sourceNode) +
                    "model '" + modelName + "' conflicts with class '" + modelName + "': "
                    "same-name model and class is forbidden. "
                    "Rename the class and reference this model via "
                    "'models: [" + modelName + "]'.");
            }
            return; // Already registered
        }

        CompilerContext::ClassInfo classInfo;
        classInfo.name = modelName;
        classInfo.isModel = true;
        classInfo.primaryKey = mod->primaryKey;
        classInfo.version = mod->version;

        std::vector<llvm::Type*> fieldTypes;
        int index = 0;

        // Inherited parent model fields first
        if (!mod->parentName.empty()) {
            classInfo.parentName = mod->parentName;  // record inheritance (for assignment-compat + downcast checks)
            std::string parentQual = ctx.qualify(mod->parentName);
            if (ctx.classes.count(parentQual)) {
                const auto& parentInfo = ctx.classes[parentQual];
                std::vector<std::pair<std::string, CompilerContext::StructMember>> pFields;
                for (auto& [n, m] : parentInfo.members) pFields.push_back({n, m});
                std::sort(pFields.begin(), pFields.end(),
                          [](auto& a, auto& b){ return a.second.index < b.second.index; });
                for (auto& [n, m] : pFields) {
                    fieldTypes.push_back(m.type);
                    CompilerContext::StructMember member = m;
                    member.index = index++;
                    classInfo.members[n] = member;
                }
            } else {
                // Parent not yet registered — skip for now so that a later
                // registration attempt (once siblings are processed) can succeed.
                LOG_DEBUG("Deferring model '" + modelName + "': parent '" + mod->parentName + "' not yet registered");
                return;
            }
        }

        // Pre-check: ensure all field-type dependencies are registered.
        // If a field is list<T>, map<K:V>, or a direct user type that isn't
        // registered yet, defer to the next fixed-point iteration.
        {
            auto userTypeReady = [&](const std::string& tn) -> bool {
                if (tn.empty() || !std::isupper((unsigned char)tn[0])) return true;
                return ctx.classes.count(tn) > 0 || ctx.enums.count(tn) > 0;
            };
            for (const auto& astMember : mod->fields) {
                const std::string& tn = astMember.type;
                bool ok = true;
                if (tn.size() > 5 && tn.substr(0,5) == "list<") {
                    ok = userTypeReady(tn.substr(5, tn.size()-6));
                } else if (tn.size() > 4 && tn.substr(0,4) == "map<") {
                    size_t col = tn.find(':');
                    if (col != std::string::npos)
                        ok = userTypeReady(tn.substr(col+1, tn.size()-col-2));
                } else {
                    ok = userTypeReady(tn);
                }
                if (!ok) {
                    LOG_DEBUG("Deferring model '" + modelName + "': field '" + astMember.name + "' type '" + tn + "' not ready");
                    return;
                }
            }
        }

        for (auto& astMember : mod->fields) {
            std::string fieldName = astMember.name;
            std::string typeName = astMember.type;
            ctx.validateIdentifier(fieldName, "model field", mod->sourceNode);

            // AIML v2 P6: a direct refcount-MODEL field is a %M* pointer (8 bytes), not an inline
            // struct — the parent shares/owns a reference (aiml_model_alloc'd, decref'd on free),
            // and field access auto-derefs the pointer. Otherwise the declared type.
            // spec/61 §5 Option B (memV3): a list<T>/cascade<X> field is a POINTER to a refcounted
            // handle (8 bytes) so by-value model copy shares (aliases) the container. Gated on
            // memV3 → OFF/deployed struct layout is byte-identical.
            bool memV3ContainerField = ctx.memV3 &&
                ((typeName.rfind("list<", 0) == 0 && typeName.back() == '>') ||
                 (typeName.rfind("cascade<", 0) == 0 && typeName.back() == '>') ||
                 (typeName.rfind("map<", 0) == 0 && typeName.back() == '>') ||
                 (typeName == "buffer") ||
                 (typeName.rfind("buffer<", 0) == 0 && typeName.back() == '>'));
            llvm::Type* fieldType = (ctx.isRefcountModel(typeName) || memV3ContainerField)
                ? llvm::PointerType::get(ctx.context, 0)
                : ctx.getLLVMType(typeName);
            if (!fieldType) {
                 LOG_ERROR(ctx.fmtLoc(mod->sourceNode) + "Unknown type '" + typeName + "' for field '" + fieldName + "' in model '" + modelName + "'");
                 continue;
            }

            fieldTypes.push_back(fieldType);

            CompilerContext::StructMember member;
            member.index = index++;
            member.type = fieldType;
            member.typeName = typeName;
            member.access = AccessLevel::Public; // Models are DTOs
            member.version = astMember.version;
            member.is_const = astMember.is_const;
            member.is_volatile = astMember.is_volatile;
            if (astMember.initialValue) {
                member.initValue = std::shared_ptr<Expr>(const_cast<Expr*>(astMember.initialValue.release()));
            }
            // Bridge AionDB db_indexed modifier from AST → CompilerContext member.
            member.db_indexed            = astMember.db_indexed;
            member.db_indexed_mode       = astMember.db_indexed_mode;
            member.db_indexed_unique     = astMember.db_indexed_unique;
            member.db_indexed_skip_empty = astMember.db_indexed_skip_empty;

            classInfo.members[fieldName] = member;
        }

        llvm::StructType* structType = llvm::StructType::create(ctx.context, modelName);
        structType->setBody(fieldTypes);
        classInfo.type = structType;

        // Compute packed_size / padded_size for hot-reload calloc padding
        {
            int packed = (int)ctx.module.getDataLayout().getTypeAllocSize(structType);
            classInfo.packed_size = packed;
            int padded = 1;
            while (padded < packed) padded <<= 1;
            classInfo.padded_size = padded;
        }

        // Spec 45 / RPC payload v2: always emit @__aiml_sernodes_<Name> so any
        // model can be used as a typed RPC payload, not just those with heap fields.
        {
            bool anyHeap = false;
            for (auto& [mn, mem] : classInfo.members)
                if (serHasHeap(mem.typeName, ctx)) { anyHeap = true; break; }
            classInfo.hasHeapFields = anyHeap;
            const llvm::DataLayout& dl = ctx.module.getDataLayout();
            const llvm::StructLayout* sl = dl.getStructLayout(structType);
            int32_t sz = (int32_t)dl.getTypeAllocSize(structType);
            std::vector<std::pair<std::string, CompilerContext::StructMember>> sm;
            for (auto& [mn, mem] : classInfo.members) sm.push_back({mn, mem});
            std::sort(sm.begin(), sm.end(),
                      [](auto& a, auto& b){ return a.second.index < b.second.index; });
            bool onPathM = g_serActivePath.insert(modelName).second;  // seed cycle guard
            std::vector<std::vector<AimlTypeNode>> fsubs;
            for (auto& [mn, mem] : sm)
                fsubs.push_back(serFieldSubtree(mem.typeName,
                    (int32_t)sl->getElementOffset(mem.index), ctx, dl));
            if (onPathM) g_serActivePath.erase(modelName);
            auto nodes = serBuildStructNodes(fsubs, 0, sz);
            serEmitGlobal(modelName, nodes, ctx, classInfo);
        }

        // Mirror: propagate sync/rpc annotations
        classInfo.syncFields = mod->syncFields;
        classInfo.rpcMethods = mod->rpcMethods;

        ctx.classes[modelName] = classInfo;
        LOG_DEBUG("AST Registered Model: " + modelName);
    } else if (auto enm = dynamic_cast<EnumDecl*>(node)) {
        std::string enumName = ctx.qualify(enm->name);
        ctx.validateIdentifier(enm->name, "enum", enm->sourceNode);
        std::map<std::string, int> enumValues;
        for (const auto& val : enm->values) {
            enumValues[val.first] = val.second;
        }
        ctx.enums[enumName] = enumValues;
        if (!enm->labels.empty()) {
            ctx.enumLabels[enumName] = enm->labels;
        }
        ctx.enumSources[enumName] = YAML::Dump(enm->sourceNode);
        // Unused-enumerator analysis: record every declared member + the enum's
        // source location (block-level; EnumDecl has no per-member source node).
        // Uses are collected in resolveEnum / resolvePropertyOrEnum under the same
        // qualified key; the top-level build diffs decl \ used.  (spec/49)
        {
            auto& declSet = ctx.enumDeclMembers[enumName];
            for (const auto& val : enm->values) declSet.insert(val.first);
            ctx.enumDeclLoc[enumName] = ctx.fmtLoc(enm->sourceNode);
        }
        LOG_DEBUG("AST Registered Enum: " + enumName +
                  " (" + std::to_string(enumValues.size()) + " values, " +
                  std::to_string(enm->labels.size()) + " labels)");
    } else if (auto casc = dynamic_cast<CascadeDecl*>(node)) {
        std::string casName = ctx.qualify(casc->name);
        ctx.validateIdentifier(casc->name, "cascade", casc->sourceNode);
        // Every field-type referenced by a cascade node must be a known type
        // at registration time (enum, cascade, model/class, primitive, alias).
        // Forward-references to a NOT-YET-declared type would silently succeed
        // otherwise and blow up later — surface it here instead.
        auto isKnownCascadeFieldType = [&](const std::string& tn) -> bool {
            if (tn.empty()) return false;
            // Primitives
            static const std::set<std::string> primitives = {
                "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
                "f32", "f64", "bool", "string"
            };
            if (primitives.count(tn)) return true;
            // Registered enums / cascades / classes-or-models
            if (ctx.enums.count(tn)) return true;
            if (ctx.cascades.count(tn)) return true;
            if (tn == casName) return true; // self-recursion (e.g. duration → ValueType)
            if (ctx.classes.count(tn)) return true;
            if (ctx.aliases.count(tn)) return true;
            return false;
        };
        std::vector<std::string> unknowns;
        for (const auto& n : casc->nodes) {
            for (const auto& f : n.fields) {
                if (!isKnownCascadeFieldType(f.typeName)) {
                    unknowns.push_back(n.name + "." +
                        (f.name.empty() ? f.typeName : f.name) +
                        " (type '" + f.typeName + "')");
                }
            }
        }
        if (!unknowns.empty()) {
            std::string msg = "cascade '" + casName + "': unknown field type(s):";
            for (auto& u : unknowns) msg += "\n  - " + u;
            msg += "\nDeclare the referenced type(s) BEFORE the cascade, or via an `- alias:` block.";
            LOG_ERROR(msg);
        }
        CompilerContext::CascadeInfo info;
        info.name = casName;
        info.opcode = casc->opcode;

        // When `opcode: <Enum>` is declared, element 0 (the node id) is typed by that
        // enum: resolve a missing id from the node name (list form), a missing name from
        // the id, and verify id == enum value for that member.  This turns the old
        // by-convention coupling (id/name mirror the enum) into a checked guarantee.
        const std::map<std::string, int>* opEnum = nullptr;
        std::map<int, std::string> opByValue;
        if (!casc->opcode.empty()) {
            auto eit = ctx.enums.find(casc->opcode);
            if (eit == ctx.enums.end()) {
                LOG_ERROR("cascade '" + casName + "': opcode enum '" + casc->opcode +
                          "' is not a declared enum (declare it before the cascade).");
            } else {
                opEnum = &eit->second;
                for (const auto& kv : eit->second) opByValue[kv.second] = kv.first;
            }
        }

        info.nodes.reserve(casc->nodes.size());
        for (size_t i = 0; i < casc->nodes.size(); ++i) {
            const auto& src = casc->nodes[i];
            CompilerContext::CascadeNodeInfo n;
            n.name  = src.name;
            n.id    = src.id;
            n.label = src.label;
            if (opEnum) {
                if (n.id < 0 && !n.name.empty()) {
                    auto mit = opEnum->find(n.name);
                    if (mit == opEnum->end())
                        LOG_ERROR("cascade '" + casName + "': node '" + n.name +
                                  "' is not a member of opcode enum '" + casc->opcode + "'.");
                    else n.id = mit->second;
                } else if (n.name.empty() && n.id >= 0) {
                    auto vit = opByValue.find(n.id);
                    if (vit == opByValue.end())
                        LOG_ERROR("cascade '" + casName + "': node id " + std::to_string(n.id) +
                                  " has no member in opcode enum '" + casc->opcode + "'.");
                    else n.name = vit->second;
                } else if (!n.name.empty() && n.id >= 0) {
                    auto mit = opEnum->find(n.name);
                    if (mit == opEnum->end())
                        LOG_ERROR("cascade '" + casName + "': node '" + n.name +
                                  "' is not a member of opcode enum '" + casc->opcode + "'.");
                    else if (mit->second != n.id)
                        LOG_ERROR("cascade '" + casName + "': node '" + n.name + "' id " +
                                  std::to_string(n.id) + " != " + casc->opcode + "." + n.name +
                                  " (" + std::to_string(mit->second) + ").");
                }
            } else if (n.id < 0) {
                LOG_ERROR("cascade '" + casName + "': node '" + n.name + "' has no numeric "
                          "id and the cascade has no `opcode:` to resolve it from.");
            }
            n.fields.reserve(src.fields.size());
            for (const auto& f : src.fields) {
                CompilerContext::CascadeFieldInfo fi;
                fi.name         = f.name;
                fi.typeName     = f.typeName;
                fi.defaultValue = f.defaultValue;
                n.fields.push_back(std::move(fi));
            }
            if (n.id >= 0) info.byId[n.id] = i;
            info.byName[n.name] = i;
            info.nodes.push_back(std::move(n));
        }
        ctx.cascades[casName] = std::move(info);
        ctx.cascadeSources[casName] = YAML::Dump(casc->sourceNode);
        LOG_DEBUG("AST Registered Cascade: " + casName + " with " +
                  std::to_string(ctx.cascades[casName].nodes.size()) + " nodes");

        /* Phase 6f: the runtime-side register call is emitted from
         * visitTopLevel(CascadeDecl) — that path runs with a valid insertion
         * block (inside the program-init function).  Keeping the emit out
         * of registerTypeDecl avoids the "no active insert block" crash. */
    } else if (auto al = dynamic_cast<AliasDecl*>(node)) {
        ctx.validateIdentifier(al->name, "alias", al->sourceNode);
        CompilerContext::AliasInfo info;
        info.name         = al->name;
        info.typeName     = al->typeName.empty() ? "i32" : al->typeName;
        info.label        = al->label;
        info.minValue     = al->minValue;
        info.maxValue     = al->maxValue;
        info.defaultValue = al->defaultValue;
        ctx.aliases[al->name] = info;
        LOG_DEBUG("AST Registered Alias: " + al->name + " → " + info.typeName +
                  " (label='" + info.label + "')");
    } else if (auto cc = dynamic_cast<CascadeClassDecl*>(node)) {
        // Function-cascade method pool (spec/57).  Register the pool here; member
        // classes are declared later in the file, so existence checks run in the
        // post-pass checkCascadeClasses (after all types are registered).
        ctx.validateIdentifier(cc->name, "cascade_class", cc->sourceNode);
        CompilerContext::CascadeClassInfo info;
        info.name    = cc->name;
        info.carrier = cc->carrier;
        info.taker   = cc->taker;
        info.members = cc->members;
        bool hasCasc = !cc->bindsCascade.empty();
        bool hasEnum = !cc->bindsEnum.empty();
        if (hasCasc == hasEnum)
            LOG_ERROR("cascade_class '" + cc->name + "': declare exactly one of "
                      "`binds_cascade:` or `binds_enum:`.");
        info.bindsEnum = hasEnum;
        info.boundName = hasEnum ? cc->bindsEnum : cc->bindsCascade;
        if (cc->carrier.empty() || cc->taker.empty())
            LOG_ERROR("cascade_class '" + cc->name +
                      "': `carrier:` and `taker:` are required.");
        ctx.cascadeClasses[cc->name] = std::move(info);
        LOG_DEBUG("AST Registered CascadeClass: " + cc->name);
    } else if (dynamic_cast<ClassGroupDecl*>(node)) {
        // classgroup (spec/58): no-op during the type-registration pass.  The
        // synthesized subclasses + container are built + registered later in
        // synthesizeClassGroups (from compile()), once every base class exists.
        return;
    } else if (auto gm = dynamic_cast<GroupMethodsDecl*>(node)) {
        // groupmethods (spec/58 §1.1/§11): record for later merge into its target
        // classgroup.  The block and its group may be in different units, so we
        // stash the node (its AST is kept alive by the import machinery) and let
        // mergeGroupMethods fold the methods in once the group is in scope.
        if (std::find(ctx.pendingGroupMethods.begin(), ctx.pendingGroupMethods.end(), gm)
            == ctx.pendingGroupMethods.end())
            ctx.pendingGroupMethods.push_back(gm);
        return;
    } else if (auto iface = dynamic_cast<InterfaceDecl*>(node)) {
        std::string ifaceName = ctx.qualify(iface->name);
        ctx.validateIdentifier(iface->name, "interface", iface->sourceNode);
        ctx.interfaceSources[ifaceName] = YAML::Dump(iface->sourceNode);
        // Register the interface in ctx.interfaces so ptr<Interface> can be
        // resolved as a fat pointer type and method calls can find the
        // corresponding vtable slot.
        if (!ctx.interfaces.count(ifaceName)) {
            CompilerContext::InterfaceInfo info;
            info.name = ifaceName;
            for (const auto& sig : iface->methods) {
                ctx.validateIdentifier(sig.name, "interface method", iface->sourceNode);
                CompilerContext::InterfaceMethod m;
                m.name = sig.name;
                m.returnType = sig.returnType;
                m.args = sig.args;
                m.argLifetimes = sig.argLifetimes;  // spec/61 §3.1
                info.methods.push_back(std::move(m));
            }
            ctx.interfaces[ifaceName] = std::move(info);
        }
        LOG_DEBUG("AST Registered Interface: " + ifaceName + " (" + std::to_string(ctx.interfaces[ifaceName].methods.size()) + " methods)");
    } else if (auto prefab = dynamic_cast<PrefabDecl*>(node)) {
        // Record the prefab name so checkPrefabUsage can statically verify each
        // engine.instantiate("<Name>") call site.  Prefabs are otherwise only
        // known at runtime (aiml_prefab_register in visitTopLevel) — this
        // pre-pass entry is what makes the compile-time name check possible.
        // Imports are resolved eagerly into the same ctx, so this map spans the
        // whole (transitive) import closure.  Record the child names too, for
        // the Tier-2 child-known check.  See spec/44_8_prefab_static.md.
        auto& kids = ctx.prefabDecls[prefab->name];
        for (const auto& ch : prefab->children)
            kids.insert(ch.name);
        LOG_DEBUG("AST Registered Prefab (static): " + prefab->name +
                  " (" + std::to_string(kids.size()) + " children)");
    } else if (auto func = dynamic_cast<FunctionDecl*>(node)) {
        // Forward-declare function signature so forward references resolve.
        std::string mangledName = ctx.qualify(func->name);
        if (ctx.module.getFunction(mangledName)) return; // already declared

        auto* i8p = llvm::PointerType::get(ctx.context, 0);
        std::vector<llvm::Type*> paramTys;
        for (const auto& arg : func->args) {
            // SAME byref policy as visitFunction: list/map args become opaque ptr.
            // AIML v2 (P4): a refcount MODEL arg is likewise an opaque ptr (%M* by-ref). MUST match
            // visitFunction's predicate or the definition/forward-decl signatures mismatch.
            bool byref = (arg.second.rfind("list<", 0) == 0 || arg.second.rfind("map<", 0) == 0);
            if (byref || ctx.isRefcountModel(arg.second)) {
                paramTys.push_back(i8p);
                continue;
            }
            IRType* irt = ctx.ir_gen->resolveType(arg.second);
            llvm::Type* t = irt ? static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(irt)) : i8p;
            paramTys.push_back(t ? t : i8p);
        }
        llvm::Type* retTy = nullptr;
        if (func->returnType.empty() || func->returnType == "void")
            retTy = llvm::Type::getVoidTy(ctx.context);
        else if (ctx.isRefcountModel(func->returnType))
            retTy = i8p;   // AIML v2 (P4): refcount model return = %M* pointer
        else {
            IRType* irt = ctx.ir_gen->resolveType(func->returnType);
            retTy = irt ? static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(irt)) : i8p;
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(retTy, paramTys, false);
        llvm::Function::Create(ft, llvm::Function::ExternalLinkage, mangledName, ctx.module);
        LOG_DEBUG("registerTypeDecl: forward-declared " + mangledName);
    }
}
void Compiler::visitExternBlock(ExternBlock* ext) {
    // Register each extern "C" function as an LLVM ExternalLinkage declaration.
    // No function body is generated — the linker (AOT) or dlopen/dlsym (JIT) resolve it.
    auto* i8p = llvm::PointerType::get(ctx.context, 0);

    for (const auto& fd : ext->funcs) {
        // Build LLVM arg types
        std::vector<llvm::Type*> paramTys;
        for (const auto& arg : fd.args) {
            llvm::Type* t = nullptr;
            // `fn` type — passes an AIML function pointer as i8* (void*)
            if (arg.second == "fn") {
                t = i8p;
            } else {
                IRType* irt = ctx.ir_gen->resolveType(arg.second);
                t = irt ? static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(irt)) : i8p;
            }
            paramTys.push_back(t ? t : i8p);
        }

        // Return type
        llvm::Type* retTy = nullptr;
        if (fd.returnType.empty() || fd.returnType == "void") {
            retTy = llvm::Type::getVoidTy(ctx.context);
        } else {
            IRType* irt = ctx.ir_gen->resolveType(fd.returnType);
            retTy = irt ? static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(irt)) : i8p;
        }

        // Skip if already declared (e.g. extern block appears twice)
        if (ctx.module.getFunction(fd.name)) {
            LOG_DEBUG("visitExternBlock: " + fd.name + " already declared, skipping");
            continue;
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(retTy, paramTys, false);
        llvm::Function* f = llvm::Function::Create(
            ft, llvm::Function::ExternalLinkage, fd.name, ctx.module);

        // Store in context so visitCall can find it via resolveFunction / ctx.module.getFunction
        CompilerContext::FunctionInfo fi;
        fi.func           = f;
        fi.fptable_index  = -1;   // external → no fptable slot
        fi.version        = 1;
        fi.compiled_version = 1;
        ctx.functions[fd.name] = fi;

        LOG_DEBUG("visitExternBlock: registered extern " + fd.name);
    }
}

void Compiler::visitTopLevel(Node* node) {
    if (auto ext = dynamic_cast<ExternBlock*>(node)) {
        visitExternBlock(ext);
        return;
    } else if (auto func = dynamic_cast<FunctionDecl*>(node)) {
        // Prevent duplicate IR generation if function was already compiled.
        // BUT: if a newer version is declared (higher `version:` value), update
        // sourceYaml + version in the symbol table so sys.bundle picks up the
        // new YAML for hot-reload bundles at runtime.
        // NOTE: If the function was only header-only registered (forward-decl,
        // no body), we still need to compile it.  Check func->empty() on the
        // LLVM function to distinguish.
        std::string mangledName = func->name;
        if (ctx.functions.find(mangledName) != ctx.functions.end()) {
            auto& fi = ctx.functions[mangledName];
            if (func->version > fi.version) {
                fi.version    = func->version;
                fi.sourceYaml = YAML::Dump(func->sourceNode);
                if (ctx.functions.count(func->name))
                    ctx.functions[func->name] = fi;
                LOG_INFO("visitTopLevel: updated sourceYaml for " + mangledName +
                         " to v" + std::to_string(func->version));
            }
            // Skip only if function already has a body (not just forward-declared).
            if (fi.func && !fi.func->empty())
                return;
        }
        visitFunction(func);
    } else if (auto stmt = dynamic_cast<Stmt*>(node)) {
        visitStatement(stmt);
    } else if (auto cls = dynamic_cast<ClassDecl*>(node)) {
        // If this is a higher-version re-definition, update sourceYaml + version (not compiled_version)
        std::string qualName = ctx.qualify(cls->name);
        if (ctx.classes.count(qualName) && cls->version > ctx.classes[qualName].version) {
            ctx.classes[qualName].version    = cls->version;
            ctx.classes[qualName].sourceYaml = YAML::Dump(cls->sourceNode);
            if (ctx.classes.count(cls->name)) {
                ctx.classes[cls->name].version    = cls->version;
                ctx.classes[cls->name].sourceYaml = YAML::Dump(cls->sourceNode);
            }
            LOG_INFO("visitTopLevel: updated sourceYaml for " + cls->name + " to v"
                     + std::to_string(cls->version));
        }
        for (const auto& method : cls->methods) {
            std::string oldName = method->name;
            // spec/61 return-ownership: capture the ORIGINAL method name (pre-mangle) so a
            // call site `<.recv.get_base` matches by its trailing identifier.
            if (method->ownsReturn) {
                ctx.ownsReturnFns.insert(oldName);
                ctx.ownsReturnFnTypes[oldName].insert(method->returnType);   // spec/61: name→return type (collision guard)
                if (method->returnType == "string") ctx.stringProducerFns.insert(oldName);
            }
            method->name = cls->name + "_" + method->name;
            auto it = method->args.insert(method->args.begin(), {"this", "ref<" + cls->name + ">"});
            // Keep argMods index-synced with args: `this` gets a default (borrow) modifier
            // slot at the front, so argMods[i] still lines up with args[i] (spec/61 §3.1).
            bool insertedMod = false;
            if (!method->argMods.empty() || !method->args.empty()) {
                method->argMods.insert(method->argMods.begin(), ParamMod{});
                insertedMod = true;
            }

            std::string oldClassName = ctx.currentClassName;
            ctx.currentClassName = cls->name;

            visitFunction(method.get());

            ctx.currentClassName = oldClassName;

            method->args.erase(it);
            if (insertedMod && !method->argMods.empty()) method->argMods.erase(method->argMods.begin());
            method->name = oldName;
        }

        // VTable emission for each implemented interface.  At this point all
        // class methods are LLVM-defined (visitFunction above).  For each
        // interface I in `implements:`, build __vtable_<Class>_<Iface> as a
        // constant array of function pointers in I's declaration order.  Class
        // must provide every method of I; otherwise emit an error.
        for (const auto& ifaceName : cls->interfaces) {
            auto ifaceIt = ctx.interfaces.find(ifaceName);
            if (ifaceIt == ctx.interfaces.end()) {
                LOG_ERROR(ctx.fmtLoc(cls->sourceNode) +
                    "class '" + cls->name + "' implements unknown interface '" + ifaceName + "'");
                continue;
            }
            const auto& iface = ifaceIt->second;
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            std::vector<llvm::Constant*> slots;
            bool anyError = false;   // collect, then skip vtable emission on error
            for (const auto& m : iface.methods) {
                std::string mangled = cls->name + "_" + m.name;
                llvm::Function* fn = ctx.module.getFunction(mangled);
                if (!fn) {
                    LOG_ERROR(ctx.fmtLoc(cls->sourceNode) +
                        "class '" + cls->name + "' is missing method '" +
                        m.name + "' required by interface '" + ifaceName + "'");
                    anyError = true;
                    continue;
                }
                // ── Signature check: return type + arg count + arg types ─────
                llvm::FunctionType* fnTy = fn->getFunctionType();
                // Expected: (i8* this, m.args[0].type, m.args[1].type, …) → m.returnType
                size_t expectedArgC = m.args.size() + 1;  // +1 for implicit this
                if (fnTy->getNumParams() != expectedArgC) {
                    LOG_ERROR(ctx.fmtLoc(cls->sourceNode) +
                        "class '" + cls->name + "' method '" + m.name +
                        "' has " + std::to_string(fnTy->getNumParams() - 1) +
                        " arg(s) but interface '" + ifaceName + "' declares " +
                        std::to_string(m.args.size()));
                    anyError = true;
                    continue;
                }
                // Compare return type.
                std::string ifaceRet = m.returnType.empty() ? "void" : m.returnType;
                llvm::Type* expectedRet = static_cast<llvm::Type*>(
                    ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(ifaceRet)));
                if (expectedRet && fnTy->getReturnType() != expectedRet) {
                    LOG_ERROR(ctx.fmtLoc(cls->sourceNode) +
                        "class '" + cls->name + "' method '" + m.name +
                        "' return type does not match interface '" + ifaceName + "'");
                    anyError = true;
                    continue;
                }
                // Compare arg types (skip arg 0 = this).
                bool argMismatch = false;
                for (size_t ai = 0; ai < m.args.size(); ++ai) {
                    llvm::Type* expectedArg = static_cast<llvm::Type*>(
                        ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(m.args[ai].second)));
                    if (expectedArg && fnTy->getParamType(ai + 1) != expectedArg) {
                        LOG_ERROR(ctx.fmtLoc(cls->sourceNode) +
                            "class '" + cls->name + "' method '" + m.name +
                            "' arg '" + m.args[ai].first + "' type does not match interface '" +
                            ifaceName + "' (expected " + m.args[ai].second + ")");
                        argMismatch = true;
                    }
                }
                if (argMismatch) { anyError = true; continue; }
                // Struct-return methods get a sret stub wrapper — vtable stores
                // the stub, so indirect callers see an explicit sret signature
                // that matches what LLVM does for direct calls under the hood.
                // Avoids WASM ABI mismatch (see getOrCreateSretStub).
                llvm::Function* slotFn = fn;
                if (fnTy->getReturnType()->isStructTy()) {
                    slotFn = getOrCreateSretStub(ctx, fn);
                }
                slots.push_back(llvm::ConstantExpr::getBitCast(slotFn, i8p));
            }
            if (anyError) {
                // Hard-fail policy: do not emit a partial / null-slot vtable.
                continue;
            }
            // spec/61 return-ownership: TRAILING slot = __free_<Class> (deep-free + shell free),
            // at fixed index iface.methods.size(). A kind-5 ScopeDrop on an owned ptr<Iface> box
            // dispatches through this slot on the concrete type. Method slot indices are unchanged
            // (appended at the end), so existing virtual dispatch is unaffected — but the forward-
            // declared vtable array in buildInterfaceUpcast is sized methods.size()+1 to match.
            slots.push_back(llvm::ConstantExpr::getBitCast(
                getOrCreateClassFree(ctx, cls->name), i8p));
            auto* arrTy = llvm::ArrayType::get(i8p, slots.size());
            auto* init = llvm::ConstantArray::get(arrTy, slots);
            std::string vtableName = "__vtable_" + cls->name + "_" + ifaceName;
            // LinkOnceODR statt Private: vtables müssen über .o-Grenzen sichtbar
            // sein, weil buildInterfaceUpcast aus einem ANDEREN compilation unit
            // (z.B. Factory) sie referenziert.  ODR-Dedup vermeidet Linker-
            // Konflikte falls dieselbe Klasse aus mehreren imports auftaucht.
            // Vorher: PrivateLinkage → silent-null in cross-file upcast → SIGSEGV
            // bei interface-method dispatch.
            if (auto* existing = ctx.module.getNamedGlobal(vtableName)) {
                // Forward-decl (from cross-file upcast) wurde schon emitted.
                // Jetzt mit Definition aufrüsten.
                if (!existing->hasInitializer()) {
                    existing->setInitializer(init);
                    existing->setLinkage(llvm::GlobalValue::LinkOnceODRLinkage);
                }
            } else {
                new llvm::GlobalVariable(ctx.module, arrTy, /*isConstant=*/true,
                    llvm::GlobalValue::LinkOnceODRLinkage, init, vtableName);
                LOG_DEBUG("visitTopLevel: emitted " + vtableName + " (" + std::to_string(slots.size()) + " slots)");
            }
        }
    } else if (auto prefab = dynamic_cast<PrefabDecl*>(node)) {
        // Emit a call to aiml_prefab_register() that runs at top-level (program init)
        auto* vd  = ctx.builder.getVoidTy();
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        auto* f32 = ctx.builder.getFloatTy();
        auto* i32 = ctx.builder.getInt32Ty();

        // Serialize children to JSON string
        auto escape_json = [](const std::string& s) {
            std::string out;
            out.reserve(s.size());
            for (char c : s) {
                if (c == '"' || c == '\\') { out += '\\'; out += c; }
                else if (c == '\n') out += "\\n";
                else if (c == '\r') out += "\\r";
                else if (c == '\t') out += "\\t";
                else out += c;
            }
            return out;
        };
        std::string childrenJson = "[";
        for (size_t i = 0; i < prefab->children.size(); i++) {
            auto& c = prefab->children[i];
            // Asset-Registry-Validierung: `texture: "<stem>"` muss zur
            // Compile-Zeit auflösbar sein (gleicher Vertrag wie
            // `gl.tex("<stem>")`).  Pfade mit Slash/Punkt = legacy raw paths
            // — die laufen weiter durchs path-based fallback im Runtime.
            if (!c.texture.empty() &&
                c.texture.find('/') == std::string::npos &&
                c.texture.find('.') == std::string::npos &&
                !AIML::AssetScanner::instance().has_stem(
                    AIML::AssetKind::Texture, c.texture)) {
                LOG_ERROR(ctx.fmtLoc(prefab->sourceNode) +
                          "prefab '" + prefab->name + "' child '" + c.name +
                          "': texture stem '" + c.texture +
                          "' not in texture store");
            }
            // ── Widget↔model binding (spec 44.6 §5d): resolve c.bind against
            //    the prefab's `binds:` model → field byte offset + type tag
            //    (0=i32, 1=f32, 2=string, 3=enum), baked into the child JSON. ──
            int bindOffset = -1, bindTag = -1;
            if (!c.bind.empty()) {
                if (prefab->binds.empty()) {
                    LOG_ERROR(ctx.fmtLoc(prefab->sourceNode) + "prefab '" + prefab->name +
                        "' child '" + c.name + "' has bind: '" + c.bind +
                        "' but the prefab declares no `binds: <Model>`");
                } else if (!ctx.classes.count(prefab->binds) || !ctx.classes[prefab->binds].type) {
                    LOG_ERROR(ctx.fmtLoc(prefab->sourceNode) + "prefab '" + prefab->name +
                        "' binds: '" + prefab->binds + "' is not a known model/class");
                } else {
                    auto& bci = ctx.classes[prefab->binds];
                    auto mit = bci.members.find(c.bind);
                    if (mit == bci.members.end()) {
                        LOG_ERROR(ctx.fmtLoc(prefab->sourceNode) + "prefab '" + prefab->name +
                            "' child '" + c.name + "': bind field '" + c.bind +
                            "' does not exist on model '" + prefab->binds + "'");
                    } else {
                        auto& bmem = mit->second;
                        const llvm::StructLayout* sl =
                            ctx.module.getDataLayout().getStructLayout(bci.type);
                        bindOffset = (int)sl->getElementOffset((unsigned)bmem.index);
                        const std::string& tn = bmem.typeName;
                        if (tn == "i32")            bindTag = 0;
                        else if (tn == "f32")       bindTag = 1;
                        else if (tn == "string")    bindTag = 2;
                        else if (ctx.enums.count(tn)) bindTag = 3;
                        else LOG_ERROR(ctx.fmtLoc(prefab->sourceNode) + "prefab '" + prefab->name +
                            "' child '" + c.name + "': bind field '" + c.bind +
                            "' has unsupported type '" + tn + "' (need i32/f32/string/enum)");
                        if (bindTag >= 0) {
                            bool okPair =
                                ((c.type == "slider" || c.type == "bar") && (bindTag == 0 || bindTag == 1)) ||
                                (c.type == "dropdown" && bindTag == 3) ||
                                ((c.type == "text_input" || c.type == "label") && bindTag == 2);
                            if (!okPair)
                                LOG_ERROR(ctx.fmtLoc(prefab->sourceNode) + "prefab '" + prefab->name +
                                    "' child '" + c.name + "': widget type '" + c.type +
                                    "' cannot bind field '" + c.bind + "' of type '" + tn + "'");
                            if (bindTag == 3 && !c.enum_type.empty() && c.enum_type != tn)
                                LOG_ERROR(ctx.fmtLoc(prefab->sourceNode) + "prefab '" + prefab->name +
                                    "' child '" + c.name + "': dropdown enum '" + c.enum_type +
                                    "' != bound field enum '" + tn + "'");
                        }
                    }
                }
            }

            if (i > 0) childrenJson += ",";
            childrenJson += "{\"name\":\"" + escape_json(c.name) + "\","
                "\"type\":\"" + escape_json(c.type) + "\","
                "\"x\":" + std::to_string(c.x) + ","
                "\"y\":" + std::to_string(c.y) + ","
                "\"width\":" + std::to_string(c.width) + ","
                "\"height\":" + std::to_string(c.height) + ","
                "\"font_size\":" + std::to_string(c.font_size) + ","
                "\"color\":" + std::to_string(c.color) + ","
                "\"texture\":\"" + escape_json(c.texture) + "\","
                "\"anchor\":\"" + escape_json(c.anchor) + "\","
                "\"valign\":\"" + escape_json(c.valign) + "\","
                "\"bg_color\":" + std::to_string(c.bg_color) + ","
                "\"border_color\":" + std::to_string(c.border_color) + ","
                "\"border_width\":" + std::to_string(c.border_width) + ","
                "\"corner_radius\":" + std::to_string(c.corner_radius) + ","
                "\"hover_color\":" + std::to_string(c.hover_color) + ","
                "\"active_color\":" + std::to_string(c.active_color) + ","
                "\"outline_color\":" + std::to_string(c.outline_color) + ","
                "\"outline_px\":" + std::to_string(c.outline_px) + ","
                "\"bg_texture\":\"" + escape_json(c.bg_texture) + "\","
                "\"tile_size\":" + std::to_string(c.tile_size) + ","
                "\"track_texture\":\"" + escape_json(c.track_texture) + "\","
                "\"handle_texture\":\"" + escape_json(c.handle_texture) + "\","
                "\"handle_gem_texture\":\"" + escape_json(c.handle_gem_texture) + "\","
                "\"frame_border_texture\":\"" + escape_json(c.frame_border_texture) + "\","
                "\"frame_border_size\":" + std::to_string(c.frame_border_size) + ","
                "\"flip_h\":" + std::string(c.flip_h ? "1" : "0") + ","
                "\"snap\":" + std::to_string(c.slider_snap) + ","
                "\"label\":\"" + escape_json(c.label) + "\","
                "\"secret\":" + std::string(c.is_secret ? "1" : "0") + ","
                "\"value\":" + std::to_string(c.bar_value) + ","
                "\"max\":" + std::to_string(c.bar_max) + ","
                "\"fill_color\":" + std::to_string(c.fill_color) + ","
                "\"on_click\":\"" + escape_json(c.on_click) + "\","
                "\"on_change\":\"" + escape_json(c.on_change) + "\","
                "\"group\":\"" + escape_json(c.group) + "\","
                "\"options\":\"" + escape_json(c.options) + "\","
                "\"material\":\"" + escape_json(c.material) + "\","
                /* Phase 6: model-binding + container attributes */
                "\"bind\":\"" + escape_json(c.bind) + "\","
                "\"bind_offset\":" + std::to_string(bindOffset) + ","
                "\"bind_tag\":" + std::to_string(bindTag) + ","
                "\"bind_list\":\"" + escape_json(c.bind_list) + "\","
                "\"spawn\":\"" + escape_json(c.spawn) + "\","
                "\"layout\":\"" + escape_json(c.layout) + "\","
                "\"gap\":" + std::to_string(c.gap) + ","
                "\"compact\":" + std::string(c.compact ? "1" : "0") + ","
                "\"cascade_type\":\"" + escape_json(c.cascade_type) + "\","
                "\"payload_spawn\":\"" + escape_json(c.payload_spawn) + "\","
                "\"enum\":\"" + escape_json(c.enum_type) + "\"}";
        }
        childrenJson += "]";

        llvm::Value* nameStr = ctx.builder.CreateGlobalStringPtr(prefab->name, "prefab_name");
        llvm::Value* texStr  = ctx.builder.CreateGlobalStringPtr(prefab->texture, "prefab_tex");
        llvm::Value* childStr = ctx.builder.CreateGlobalStringPtr(childrenJson, "prefab_children");
        llvm::Value* w = llvm::ConstantFP::get(f32, prefab->width);
        llvm::Value* h = llvm::ConstantFP::get(f32, prefab->height);
        llvm::Value* drag = ctx.builder.getInt32(prefab->draggable ? 1 : 0);

        ctx.builder.CreateCall(
            ctx.getFunction("aiml_prefab_register", vd, {i8p, f32, f32, i8p, i32, i8p}),
            {nameStr, w, h, texStr, drag, childStr});

        /* Emit register_callback(name, fn_i8p) for each unique callback referenced
         * by this prefab's children. Forward-declares the function with signature
         * void(i8*) — when visitFunction(handle_submit) runs later, LLVM resolves
         * the existing declaration to the actual definition. */
        std::vector<std::string> callback_names;
        for (auto& c : prefab->children) {
            if (!c.on_click.empty())  callback_names.push_back(c.on_click);
            if (!c.on_change.empty()) callback_names.push_back(c.on_change);
        }
        std::sort(callback_names.begin(), callback_names.end());
        callback_names.erase(std::unique(callback_names.begin(), callback_names.end()),
                             callback_names.end());
        for (const std::string& cb : callback_names) {
            llvm::Function* cb_fn = nullptr;

            // Dotted form `obj.method` — emit a trampoline that calls the
            // class method on the named global instance.  Otherwise free fn.
            size_t dotPos = cb.find('.');
            if (dotPos != std::string::npos) {
                std::string objName    = cb.substr(0, dotPos);
                std::string methodName = cb.substr(dotPos + 1);
                auto symIt = ctx.symbol_table.find(objName);
                if (symIt == ctx.symbol_table.end()) {
                    LOG_ERROR(ctx.fmtLoc(prefab->sourceNode) +
                        "prefab '" + prefab->name + "': on_click target '" + cb +
                        "' — unknown variable '" + objName + "'");
                    continue;
                }
                std::string className = symIt->second.typeName;
                if (ctx.classes.find(className) == ctx.classes.end()) {
                    LOG_ERROR(ctx.fmtLoc(prefab->sourceNode) +
                        "prefab '" + prefab->name + "': on_click target '" + cb +
                        "' — '" + objName + "' has type '" + className +
                        "' which is not a class");
                    continue;
                }
                std::string mangled = className + "_" + methodName;
                llvm::Function* methodFn = ctx.module.getFunction(mangled);
                if (!methodFn) {
                    // Forward-decl: (this*, p*) -> void.
                    auto* fn_ty = llvm::FunctionType::get(vd, {i8p, i8p}, false);
                    methodFn = llvm::Function::Create(
                        fn_ty, llvm::Function::ExternalLinkage,
                        mangled, &ctx.module);
                }
                // Emit trampoline __cb_<obj>__<method>(void* inst):
                //   methodFn(<obj_gv>, inst); ret void
                std::string trampName = "__cb_" + objName + "__" + methodName;
                cb_fn = ctx.module.getFunction(trampName);
                if (!cb_fn) {
                    auto* tr_ty = llvm::FunctionType::get(vd, {i8p}, false);
                    cb_fn = llvm::Function::Create(
                        tr_ty, llvm::Function::InternalLinkage,
                        trampName, &ctx.module);
                    auto* savedBB = ctx.builder.GetInsertBlock();
                    auto* trBB = llvm::BasicBlock::Create(ctx.context, "entry", cb_fn);
                    ctx.builder.SetInsertPoint(trBB);
                    llvm::Value* objPtr = static_cast<llvm::Value*>(
                        ctx.ir_gen->unwrap(symIt->second.ir_alloca_inst));
                    llvm::Argument* instArg = cb_fn->getArg(0);
                    ctx.builder.CreateCall(methodFn, {objPtr, instArg});
                    ctx.builder.CreateRetVoid();
                    ctx.builder.SetInsertPoint(savedBB);
                }
            } else {
                cb_fn = ctx.module.getFunction(cb);
                if (!cb_fn) {
                    /* Forward-declare void(i8*) — definition supplied later by visitFunction. */
                    auto* cb_ty = llvm::FunctionType::get(vd, {i8p}, false);
                    cb_fn = llvm::Function::Create(cb_ty, llvm::Function::ExternalLinkage,
                                                   cb, &ctx.module);
                }
            }
            llvm::Value* cb_name_str = ctx.builder.CreateGlobalStringPtr(cb, "prefab_cb_name");
            llvm::Value* cb_ptr = ctx.builder.CreateBitCast(cb_fn, i8p);
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_register_callback", vd, {i8p, i8p}),
                {cb_name_str, cb_ptr});
        }

        LOG_INFO("visitTopLevel: emitted prefab register for '" + prefab->name + "'"
                 + " (" + std::to_string(callback_names.size()) + " callbacks)");
    } else if (auto enm = dynamic_cast<EnumDecl*>(node)) {
        /* Phase 6f-4: register enum labels for widget-runtime consumption.
         * Cascade payload widgets that reference an enum-typed field can
         * then render as a dropdown pre-populated with these labels. */
        std::string enmName = ctx.qualify(enm->name);
        std::string labels;
        std::string values;   // parallel "v0|v1|v2" of the enum members' i32 values
        auto derive_label = [](const std::string& n) {
            std::string out; out.reserve(n.size());
            bool cap = true;
            for (char c : n) {
                if (c == '_') { out += ' '; cap = true; }
                else if (cap) { out += (char)toupper((unsigned char)c); cap = false; }
                else          { out += c; }
            }
            return out;
        };
        for (size_t i = 0; i < enm->values.size(); ++i) {
            if (i) { labels += "|"; values += "|"; }
            /* Prefer an explicit label if present, else derive from name. */
            auto it = enm->labels.find(enm->values[i].second);
            labels += (it != enm->labels.end() && !it->second.empty())
                      ? it->second : derive_label(enm->values[i].first);
            /* Parallel member value — lets widgets return the real enum value
             * (not selected_index+1) for sparse/non-1-based enums. */
            values += std::to_string(enm->values[i].second);
        }
        auto* vd  = ctx.builder.getVoidTy();
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* nameStr   = ctx.builder.CreateGlobalStringPtr(enmName, "enum_name");
        llvm::Value* labelsStr = ctx.builder.CreateGlobalStringPtr(labels,  "enum_labels");
        llvm::Value* valuesStr = ctx.builder.CreateGlobalStringPtr(values,  "enum_values");
        ctx.builder.CreateCall(
            ctx.getFunction("aiml_prefab_register_enum", vd, {i8p, i8p, i8p}),
            {nameStr, labelsStr, valuesStr});
    } else if (auto casc = dynamic_cast<CascadeDecl*>(node)) {
        /* Phase 6f: emit aiml_prefab_register_cascade(name, nodes_json).
         * Runtime uses this to auto-populate `type: cascade` (and `type:
         * dropdown` with `cascade_type:`) dropdowns and to drive reactive
         * payload sub-widgets when the selected node changes. */
        auto* vd  = ctx.builder.getVoidTy();
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        auto escape_json = [](const std::string& s) {
            std::string out; out.reserve(s.size() + 4);
            for (char c : s) {
                switch (c) {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n";  break;
                    case '\r': out += "\\r";  break;
                    case '\t': out += "\\t";  break;
                    default:   out += c;      break;
                }
            }
            return out;
        };
        std::string casName = ctx.qualify(casc->name);
        std::string j = "[";
        const auto& info = ctx.cascades[casName];
        for (size_t ni = 0; ni < info.nodes.size(); ++ni) {
            const auto& n = info.nodes[ni];
            if (ni) j += ",";
            j += "{\"name\":\""  + escape_json(n.name)  + "\","
                 "\"id\":"       + std::to_string(n.id) + ","
                 "\"label\":\""  + escape_json(n.label) + "\","
                 "\"fields\":[";
            for (size_t fi = 0; fi < n.fields.size(); ++fi) {
                const auto& f = n.fields[fi];
                if (fi) j += ",";
                j += "{\"name\":\""    + escape_json(f.name)         + "\","
                     "\"type\":\""     + escape_json(f.typeName)     + "\","
                     "\"default\":\""  + escape_json(f.defaultValue) + "\"}";
            }
            j += "]}";
        }
        j += "]";
        llvm::Value* nameStr  = ctx.builder.CreateGlobalStringPtr(casName, "cascade_name");
        llvm::Value* nodesStr = ctx.builder.CreateGlobalStringPtr(j,       "cascade_nodes");
        llvm::Value* opcStr   = ctx.builder.CreateGlobalStringPtr(info.opcode, "cascade_opcode");
        ctx.builder.CreateCall(
            ctx.getFunction("aiml_prefab_register_cascade", vd, {i8p, i8p, i8p}),
            {nameStr, nodesStr, opcStr});
        LOG_INFO("visitTopLevel: emitted cascade register for '" + casName + "' (" +
                 std::to_string(info.nodes.size()) + " nodes)");
    } else if (dynamic_cast<CascadeClassDecl*>(node)) {
        // Function-cascade pool (spec/57).  M1a: no codegen yet — the pool is
        // registered in registerTypeDecl and validated in checkCascadeClasses;
        // render() emission is a later milestone (M3).
    } else if (dynamic_cast<ClassGroupDecl*>(node)) {
        // classgroup (spec/58): no codegen — it was desugared in
        // synthesizeClassGroups into subclasses + a container class (which ARE
        // in prog->declarations and get their code generated normally).
    } else if (dynamic_cast<GroupMethodsDecl*>(node)) {
        // groupmethods (spec/58): no codegen — its methods were folded into the
        // target group by mergeGroupMethods() and emitted as subclass methods.
    } else if (auto mat = dynamic_cast<MaterialDecl*>(node)) {
        /* Emit aiml_material_register(name, shader, uniforms_json) at program
         * init. uniforms_json uses a compact tagged format the runtime parser
         * understands; see prefab_runtime.h. */
        /* Asset-Registry-Validierung: `shader: "<stem>"` muss zur Compile-Zeit
         * auflösbar sein (gleicher Vertrag wie texture:/gl.tex).  Ohne diese
         * Prüfung schlägt ein Tippfehler erst zur Laufzeit fehl — und dort nur
         * still (Fallback auf den Default-Shader).  Pfade mit Slash/Punkt =
         * legacy raw paths, die weiter durchs runtime fopen laufen. */
        if (!mat->shader.empty() &&
            mat->shader.find('/') == std::string::npos &&
            mat->shader.find('.') == std::string::npos &&
            !AIML::AssetScanner::instance().has_stem(
                AIML::AssetKind::Shader, mat->shader)) {
            LOG_ERROR(ctx.fmtLoc(mat->sourceNode) +
                      "material '" + mat->name + "': shader stem '" +
                      mat->shader + "' not in shader asset store");
        }
        auto* vd  = ctx.builder.getVoidTy();
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();

        auto escape_json = [](const std::string& s) {
            std::string out; out.reserve(s.size());
            for (char c : s) {
                if (c == '"' || c == '\\') { out += '\\'; out += c; }
                else if (c == '\n') out += "\\n";
                else if (c == '\r') out += "\\r";
                else if (c == '\t') out += "\\t";
                else out += c;
            }
            return out;
        };

        std::string uniformsJson = "{";
        for (size_t i = 0; i < mat->uniforms.size(); i++) {
            const auto& kv = mat->uniforms[i];
            const std::string& uname = kv.first;
            const auto& u = kv.second;
            if (i > 0) uniformsJson += ",";
            uniformsJson += "\"" + escape_json(uname) + "\":{";
            switch (u.kind) {
                case MaterialUniformInfo::K_FLOAT:
                    uniformsJson += "\"k\":\"f\",\"v\":" + std::to_string(u.scalar);
                    break;
                case MaterialUniformInfo::K_INT:
                    uniformsJson += "\"k\":\"i\",\"v\":" + std::to_string(u.integer);
                    break;
                case MaterialUniformInfo::K_TEX:
                    uniformsJson += "\"k\":\"t\",\"v\":\"" + escape_json(u.tex_path) + "\"";
                    break;
                case MaterialUniformInfo::K_VEC2:
                case MaterialUniformInfo::K_VEC3:
                case MaterialUniformInfo::K_VEC4: {
                    const char* tag = u.kind == MaterialUniformInfo::K_VEC2 ? "v2"
                                    : u.kind == MaterialUniformInfo::K_VEC3 ? "v3" : "v4";
                    uniformsJson += "\"k\":\""; uniformsJson += tag;
                    uniformsJson += "\",\"v\":[";
                    for (size_t j = 0; j < u.vec.size(); j++) {
                        if (j > 0) uniformsJson += ",";
                        uniformsJson += std::to_string(u.vec[j]);
                    }
                    uniformsJson += "]";
                    break;
                }
            }
            uniformsJson += "}";
        }
        uniformsJson += "}";

        llvm::Value* nameStr   = ctx.builder.CreateGlobalStringPtr(mat->name,    "mat_name");
        llvm::Value* shaderStr = ctx.builder.CreateGlobalStringPtr(mat->shader,  "mat_shader");
        llvm::Value* uJsonStr  = ctx.builder.CreateGlobalStringPtr(uniformsJson, "mat_uniforms");

        ctx.builder.CreateCall(
            ctx.getFunction("aiml_material_register", vd, {i8p, i8p, i8p}),
            {nameStr, shaderStr, uJsonStr});

        LOG_INFO("visitTopLevel: emitted material register for '" + mat->name + "'"
                 + " (" + std::to_string(mat->uniforms.size()) + " uniforms)");
    }
}
void Compiler::visitFunction(FunctionDecl* func) {
    // Basic mapping using our old context structure for now
    std::string mangledName = ctx.qualify(func->name);
    ctx.currentFunctionName = func->name;
    ctx.traceDeclarationSites.clear();

    /* Snapshot + install decorator context so visitRetStmt can consult it
     * without needing a pointer back to the FunctionDecl.  Cleared on exit
     * of the outer scope (we snapshot the previous values so nested calls
     * during compilation don't leak decorator state up the stack). */
    auto _saved_decorators   = std::move(ctx.currentFunctionDecorators);
    auto _saved_return_type  = std::move(ctx.currentFunctionReturnType);
    ctx.currentFunctionDecorators = func->decorators;
    ctx.currentFunctionReturnType = func->returnType;

    ctx.validateIdentifier(func->name, "function", func->sourceNode);

    std::vector<IRType*> argTypes;
    std::vector<std::string> argNames;
    std::vector<bool>        argIsByref;  // parallel to argTypes; true for list/map args
    std::vector<bool>        argIsRefModel; // parallel to argTypes; true for v2 refcount-model args
    std::vector<std::string> argLifetimes; // parallel to argNames; spec/61 §3.1 ownership

    for (size_t ai = 0; ai < func->args.size(); ++ai) {
        const auto& arg = func->args[ai];
        ctx.validateIdentifier(arg.first, "argument", func->sourceNode);
        // list<T>/map<K:V> args werden implizit by-reference übergeben (Python-/Java-Stil).
        // LLVM-Signatur: opaque pointer statt struct-by-value, damit size++/data-Wachstum
        // zum Caller propagiert.  Forward-decl in registerTypeDecl verwendet die GLEICHE
        // Logik (sonst signature mismatch).  Spec/22_call_semantics.md.
        // AIML v2 (P4): a refcount MODEL arg is likewise passed by-ref as a %M* (borrow) — the
        // callee shares the caller's object; in-place mutation is visible to the caller.
        bool byref = (arg.second.rfind("list<", 0) == 0 || arg.second.rfind("map<", 0) == 0);
        bool refModel = ctx.isRefcountModel(arg.second);
        IRType* t = (byref || refModel) ? ctx.ir_gen->getPointerTy() : ctx.ir_gen->resolveType(arg.second);
        if (t) {
            argTypes.push_back(t);
            argNames.push_back(arg.first);
            argIsByref.push_back(byref);
            argIsRefModel.push_back(refModel);
            argLifetimes.push_back(ai < func->argMods.size() ? func->argMods[ai].lifetime : "scope");
        }
    }

    IRType* retTy = nullptr;
    // AIML v2 (P4): a refcount MODEL return is a %M* pointer (the callee hands its reference to
    // the caller). Otherwise resolve the declared type as usual.
    if (ctx.isRefcountModel(func->returnType)) retTy = ctx.ir_gen->getPointerTy();
    else if (!func->returnType.empty()) retTy = ctx.ir_gen->resolveType(func->returnType);
    if (!retTy) retTy = ctx.ir_gen->resolveType("void");
    
    // Reuse forward-declared function if it exists (from registerTypeDecl), else create new
    IRValue* funcVal = nullptr;
    llvm::Function* existingFunc = ctx.module.getFunction(mangledName);
    if (existingFunc && existingFunc->empty()) {
        // Forward declaration exists — reuse it
        funcVal = ctx.ir_gen->wrap(static_cast<llvm::Value*>(existingFunc));
    } else {
        funcVal = ctx.ir_gen->createFunction(mangledName, retTy, argTypes);
    }

    // Set Personality Function for exception handling — only on definitions, not declarations
    if (!ctx.headerOnlyMode && ctx.ir_gen->getPersonalityFunction()) {
        ctx.ir_gen->setPersonalityFunction(funcVal, ctx.ir_gen->getPersonalityFunction());
    }

    // Register Function Info
    CompilerContext::FunctionInfo funcInfo;
    funcInfo.func = static_cast<llvm::Function*>(ctx.ir_gen->unwrap(funcVal));
    funcInfo.argNames = argNames;
    funcInfo.argLifetimes = argLifetimes;  // spec/61 §3.1 (parallel to argNames)
    funcInfo.version = func->version;
    funcInfo.compiled_version = func->version;  // locked at compile time
    // Assign fptable slot — preserve any pre-existing index (e.g. from an alias entry)
    if (ctx.functions.count(mangledName) && ctx.functions[mangledName].fptable_index >= 0)
        funcInfo.fptable_index = ctx.functions[mangledName].fptable_index;
    else
        funcInfo.fptable_index = ctx.fptable_next_index++;
    funcInfo.sourceYaml = YAML::Dump(func->sourceNode);
    funcInfo.deprecated = std::find(func->meta.begin(), func->meta.end(),
                                    "deprecated") != func->meta.end();
    funcInfo.ownsReturn = func->ownsReturn;   // spec/61 return-ownership
    // Record the ORIGINAL short name for call-site suffix matching. Methods are captured in
    // visitTopLevel BEFORE mangling (oldName); free functions (no enclosing class) here.
    if (func->ownsReturn && ctx.currentClassName.empty()) {
        ctx.ownsReturnFns.insert(func->name);
        ctx.ownsReturnFnTypes[func->name].insert(func->returnType);   // spec/61: name→return type (collision guard)
        if (func->returnType == "string") ctx.stringProducerFns.insert(func->name);
    }
    // Track user-defined arg + return types for sys.bundle (recurses into list<T>/map<K,V>)
    auto trackType = [&](const std::string& typeName) {
        std::set<std::string> found;
        ctx.collectUserTypes(typeName, found);
        funcInfo.referencedTypes.insert(found.begin(), found.end());
    };
    for (const auto& arg : func->args) trackType(arg.second);
    trackType(func->returnType);
    ctx.functions[func->name] = funcInfo;
    ctx.functions[mangledName] = funcInfo;

    // Header Only mode check
    if (ctx.headerOnlyMode) {
        LOG_DEBUG("HeaderOnly: Skipping body for " + mangledName);
        return; 
    }

    // Create Entry Block directly attached to the function (no dependency on current insert point)
    llvm::Function* llvmFunc = funcInfo.func;
    llvm::BasicBlock* llvmEntryBB = llvm::BasicBlock::Create(ctx.context, "entry", llvmFunc);
    
    // Save previous block and symbol table, set to new entry
    IRBlock* oldBlock = ctx.ir_gen->getInsertBlock();
    auto savedSymbols = ctx.symbol_table;
    ctx.ir_gen->setInsertPoint(ctx.ir_gen->wrapBlock(llvmEntryBB));

    // Setup Arguments in Symbol Table — reject shadowing of module-level vars
    bool shadowError = false;
    // Defensive check: forward-decl / current-def signature mismatch causes
    // getFunctionArg to return a null IRValue → segfault in createStore.
    if (llvmFunc->arg_size() != argNames.size()) {
        std::string srcInfo = func->sourceNode.Mark().is_null()
            ? std::string("<no source mark>")
            : (std::string("line ") + std::to_string(func->sourceNode.Mark().line + 1)
               + " col " + std::to_string(func->sourceNode.Mark().column + 1));
        LOG_ERROR("visitFunction: signature mismatch for '" + func->name + "' (mangled='" + mangledName
                  + "'): forward-decl has " + std::to_string(llvmFunc->arg_size())
                  + " args but current definition has " + std::to_string(argNames.size())
                  + " args.  Source: " + srcInfo
                  + ".  Likely cause: function name reused with different signature, or a typed-parameter failed to resolve.");
        ctx.symbol_table = savedSymbols;
        ctx.ir_gen->setInsertPoint(oldBlock);
        return;
    }
    for (size_t i = 0; i < argNames.size(); ++i) {
        if (ctx.symbol_table.count(argNames[i])) {
            LOG_ERROR("Variable shadowing is not allowed: function argument '" + argNames[i]
                      + "' in '" + func->name + "' shadows an existing variable");
            shadowError = true;
        }
        IRValue* argAlloca = ctx.ir_gen->createAlloca(argTypes[i], argNames[i]);
        IRValue* argVal = ctx.ir_gen->getFunctionArg(funcVal, i);
        if (!argVal || !ctx.ir_gen->unwrap(argVal)) {
            LOG_ERROR("visitFunction: getFunctionArg returned null for arg #"
                      + std::to_string(i) + " '" + argNames[i] + "' in '" + func->name + "'");
            ctx.symbol_table = savedSymbols;
            ctx.ir_gen->setInsertPoint(oldBlock);
            return;
        }
        ctx.ir_gen->createStore(argVal, argAlloca);

        Symbol sym;
        sym.ir_alloca_inst = argAlloca;
        sym.ir_type = ctx.ir_gen->getPointerTy();
        sym.typeName = func->args[i].second;
        sym.is_param = true;   // spec/61 §7.1: parameters are borrows (borrow-escape check)
        // list/map are already passed with one level of indirection (by-ref);
        // an explicit ptr<> would add a second one. Silence via
        // AIML_ALLOW_PTR_CONTAINER when double indirection is intended.
        {
            const std::string& ptn = func->args[i].second;
            if ((ptn.rfind("ptr<list<", 0) == 0 || ptn.rfind("ptr<map<", 0) == 0)
                && ptn.size() > 5 && !std::getenv("AIML_ALLOW_PTR_CONTAINER")) {
                LOG_WARNING(func->name + ": parameter '" + argNames[i] + "' is '" + ptn +
                    "'. list/map are already passed by reference (single indirection); "
                    "'" + ptn.substr(4, ptn.size() - 5) + "' is enough — the explicit ptr<> "
                    "adds a second indirection.");
            }
        }
        if (i < argIsRefModel.size() && argIsRefModel[i]) {
            // AIML v2 (P4): a refcount MODEL arg — the alloca holds a %M* (the caller's shared
            // object). Give the symbol a POINTER element type so the existing pointer auto-deref
            // (resolveProperty / resolvePropertyWritePtr: `baseElemTy->isPointer()`) handles field
            // read AND write — same mechanism as a model local. NOT is_byref_arg (whose deref the
            // dotted-write lvalue path does not apply). Borrow: no incref/decref for the arg itself.
            sym.ir_element_type = ctx.ir_gen->getPointerTy();
        } else if (i < argIsByref.size() && argIsByref[i]) {
            // ir_element_type bleibt die echte struct shape (für member-dispatch),
            // aber alloca hält tatsächlich nur einen pointer.  Method-dispatch
            // muss is_byref_arg checken und einen extra-load machen.
            sym.ir_element_type = ctx.ir_gen->resolveType(func->args[i].second);
            sym.is_byref_arg = true;
        } else {
            sym.ir_element_type = argTypes[i];
        }

        // spec/61 §3.1: parameter ownership qualifier (argMods is parallel to func->args).
        // Look up by name — argNames may skip an arg whose type failed to resolve, so the
        // index into argMods is not guaranteed identical.
        {
            std::string paramType;
            const std::string* pLife = nullptr;
            for (size_t j = 0; j < func->args.size() && j < func->argMods.size(); ++j)
                if (func->args[j].first == argNames[i]) {
                    // spec/61 §3.1: const parameter → read-only in the body (the existing
                    // set/store const-write checks honour Symbol.is_const).
                    sym.is_const = func->argMods[j].is_const;
                    pLife = &func->argMods[j].lifetime;
                    paramType = func->args[j].second;
                    break;
                }
            if (pLife) {
                sym.lifetime = *pLife;
                if (*pLife == "consumed") {
                    // Both direct method calls and interface (vtable) dispatch now apply
                    // caller-move — BUT interface dispatch reads the INTERFACE signature's
                    // lifetime, not the concrete method's. So a concrete method that overrides
                    // an interface method may only be `consumed` on a parameter the interface
                    // ALSO declares `consumed`; otherwise the interface path would not move and
                    // the arg would be double-freed. Reject that mismatch. (spec/61 §3.1)
                    bool isMethod = (!func->args.empty() && func->args[0].first == "this");
                    if (isMethod && !ctx.currentClassName.empty()) {
                        const std::string& cls = ctx.currentClassName;
                        std::string prefix = cls + "_";
                        std::string um = (func->name.rfind(prefix, 0) == 0)
                                         ? func->name.substr(prefix.size()) : func->name;
                        auto cit = ctx.classes.find(cls);
                        if (cit != ctx.classes.end()) {
                            for (const auto& ifn : cit->second.interfaces) {
                                auto iit = ctx.interfaces.find(ifn);
                                if (iit == ctx.interfaces.end()) iit = ctx.interfaces.find(ctx.qualify(ifn));
                                if (iit == ctx.interfaces.end()) continue;
                                for (const auto& im : iit->second.methods) {
                                    if (im.name != um) continue;
                                    // Find this param in the interface signature by name; its
                                    // declared lifetime must be consumed too.
                                    bool ifaceConsumed = false;
                                    for (size_t k = 0; k < im.args.size(); ++k)
                                        if (im.args[k].first == argNames[i]) {
                                            ifaceConsumed = (k < im.argLifetimes.size()
                                                             && im.argLifetimes[k] == "consumed");
                                            break;
                                        }
                                    if (!ifaceConsumed)
                                        LOG_ERROR(func->name + ": { lifetime: consumed } on parameter '" +
                                            argNames[i] + "' overrides interface '" + ifn + "." + um +
                                            "' whose signature does NOT declare it consumed — interface "
                                            "dispatch would not move it → double-free. Declare the parameter "
                                            "consumed on the interface method too, or drop the qualifier. (spec/61 §3.1)");
                                }
                            }
                        }
                    }
                    // A consumed parameter must lower to a pointer/handle (ptr<T>, list/map
                    // by-ref, string, or a handle built-in). Reject value structs & scalars:
                    // moving a shallow-copied value struct would double-free its shared heap.
                    bool isPtr = (i < argIsByref.size() && argIsByref[i]);
                    if (!isPtr) {
                        llvm::Type* lt = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(argTypes[i]));
                        isPtr = lt && lt->isPointerTy();
                    }
                    if (!isPtr) {
                        LOG_ERROR(func->name + ": parameter '" + argNames[i] + "' of type '" +
                            paramType + "' cannot be { lifetime: consumed } — consumed requires "
                            "a reference/handle type (ptr<T>, list, map, string, or a handle "
                            "built-in). For a value model/class pass ptr<" + paramType +
                            "> instead; a scalar owns no heap. (spec/61 §3.1)");
                    } else if (paramType == "string" && ctx.lifetimeDropsEnabled) {
                        // callee-drop safety net for an un-consumed `consumed` string param:
                        // decref at function-body exit (kind 1) unless push/return/store moves
                        // it out first.  Only string has a drop primitive today; ptr/list/map
                        // consumed params rely on caller-move + are freed by whoever they are
                        // moved into (leak-on-unconsumed only, never double-free). Staged here,
                        // drained into the body frame by the next visitBlockStmt.
                        CompilerContext::ScopeDrop sd;
                        sd.addr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argAlloca));
                        sd.kind = 1;
                        ctx.pendingParamDrops.push_back(sd);
                    }
                }
            }
        }
        ctx.symbol_table[argNames[i]] = sym;
    }
    if (shadowError) {
        ctx.symbol_table = savedSymbols;
        ctx.ir_gen->setInsertPoint(oldBlock);
        return;
    }

    // ── Trace prologue (AIML_TRACE env var) ──────────────────────────────────
    // if (aiml_trace_active(fname)) { build args_str; aiml_trace(fname, "=>", args_str); }
    // Hot path (trace inactive) = one conditional branch, never taken.
    {
        llvm::PointerType* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type*        i32t = ctx.builder.getInt32Ty();

        llvm::FunctionCallee traceActiveFn = ctx.module.getOrInsertFunction(
            "aiml_trace_active",
            llvm::FunctionType::get(i32t, {i8p}, false));
        llvm::FunctionCallee traceFn = ctx.module.getOrInsertFunction(
            "aiml_trace_format",
            llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, true));

        llvm::Value* fnameStr = ctx.builder.CreateGlobalStringPtr(func->name, "tr_fname");
        llvm::Value* trActive = ctx.builder.CreateCall(traceActiveFn, {fnameStr}, "tr_active");
        llvm::Value* trCond   = ctx.builder.CreateICmpNE(
            trActive, llvm::ConstantInt::get(i32t, 0), "tr_cond");

        llvm::BasicBlock* traceBB = llvm::BasicBlock::Create(ctx.context, "tr_enter", llvmFunc);
        llvm::BasicBlock* bodyBB  = llvm::BasicBlock::Create(ctx.context, "tr_body",  llvmFunc);
        ctx.builder.CreateCondBr(trCond, traceBB, bodyBB);

        // ── trace_bb: build "name=val, ..." string and call aiml_trace ────────
        ctx.builder.SetInsertPoint(traceBB);

        // Pass typed values directly to the runtime formatter: no AIML heap
        // temporaries and no reads through opaque pointers. Source types distinguish
        // strings from objects even when LLVM represents both as opaque pointers.
        std::string format;
        std::vector<llvm::Value*> values;
        for (size_t i = 0; i < argNames.size(); ++i) {
            const std::string& nm = argNames[i];
            if (i) format += ", ";
            format += nm + "=";
            Symbol& sym = ctx.symbol_table[nm];
            auto* slot = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym.ir_alloca_inst));
            auto* ty = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(sym.ir_element_type));
            auto* value = ctx.builder.CreateLoad(ty, slot, nm + "_tr");
            if (sym.typeName == "string" && ty->isPointerTy()) {
                format += "%s";
                auto* nullp = llvm::ConstantPointerNull::get(i8p);
                values.push_back(ctx.builder.CreateSelect(ctx.builder.CreateICmpEQ(value, nullp),
                    ctx.builder.CreateGlobalStringPtr("<null>"), value));
            } else if (ty->isPointerTy()) {
                format += "%p";
                values.push_back(value);
            } else if (ty->isIntegerTy(64)) {
                format += "%lld";
                values.push_back(value);
            } else if (ty->isIntegerTy()) {
                format += "%d";
                values.push_back(ty->isIntegerTy(1)
                    ? ctx.builder.CreateZExt(value, i32t)
                    : ctx.builder.CreateSExtOrTrunc(value, i32t));
            } else if (ty->isFloatingPointTy()) {
                format += ty->isFloatTy() ? "%.6g" : "%.15g";
                values.push_back(ctx.builder.CreateFPCast(value, ctx.builder.getDoubleTy()));
            } else if (auto* st = llvm::dyn_cast<llvm::StructType>(ty);
                       sym.typeName.rfind("ptr<", 0) == 0 &&
                       ctx.interfaces.count(sym.typeName.substr(4, sym.typeName.size() - 5)) &&
                       st && st->getNumElements() == 2 &&
                       st->getElementType(0)->isPointerTy() && st->getElementType(1)->isPointerTy()) {
                format += "{obj=%p,vtable=%p}";
                values.push_back(ctx.builder.CreateExtractValue(value, {0}));
                values.push_back(ctx.builder.CreateExtractValue(value, {1}));
            } else {
                format += "<?>";
            }
        }
        std::vector<llvm::Value*> printArgs = {fnameStr, ctx.builder.CreateGlobalStringPtr(format)};
        printArgs.insert(printArgs.end(), values.begin(), values.end());
        ctx.builder.CreateCall(traceFn, printArgs);
        ctx.builder.CreateBr(bodyBB);

        // ── body_bb: actual function body follows ─────────────────────────────
        ctx.builder.SetInsertPoint(bodyBB);
    }

    // Compile Body (track referenced user types for sys.bundle)
    ctx.bundleTrackingTarget = mangledName;
    if (func->body) {
        // spec/61 §4: compute the intraprocedural ownership plan and make it current for the
        // whole body emission (RAII restore). visitDeclStmt tags ScopeDrops with their slot id;
        // visitBlockStmt then gates each drop through mayDrop() (fail-safe: doubt → no drop).
        // spec/61 §10 (memV3-only): conservative ownership diagnostics (use-after-move/free,
        // double-free). Pure reporting — never affects codegen or the OFF/deployed path.
        AIML::AST::checkBorrowsMemV3(ctx, *func);
        OwnershipPlan ownPlan = buildOwnershipPlan(ctx, *func);
        const OwnershipPlan* prevPlan = ctx.ownershipPlan;
        ctx.ownershipPlan = &ownPlan;
        struct PlanGuard { CompilerContext& c; const OwnershipPlan* p; ~PlanGuard(){ c.ownershipPlan = p; } }
            planGuard{ctx, prevPlan};
        visitBlockStmt(func->body.get());
    }
    ctx.bundleTrackingTarget.clear();

    // Check if the last block has a terminator (like ret)
    llvm::BasicBlock* currentBB = static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
    if (!currentBB->getTerminator()) {
        llvm::Type* unwrappedRetTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(retTy));
        if (unwrappedRetTy->isVoidTy()) {
            ctx.ir_gen->createRet(nullptr);
        } else if (unwrappedRetTy->isIntegerTy()) {
            ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::ConstantInt::get(unwrappedRetTy, 0)));
        } else if (unwrappedRetTy->isPointerTy()) {
            ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(unwrappedRetTy))));
        } else if (unwrappedRetTy->isFloatingPointTy()) {
            ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::ConstantFP::get(unwrappedRetTy, 0.0)));
        } else if (unwrappedRetTy->isStructTy() || unwrappedRetTy->isArrayTy() ||
                   unwrappedRetTy->isVectorTy()) {
            // Aggregate return (list/map/struct) with no explicit return —
            // e.g. a BODYLESS interface method that returns map<string:i32>.
            // Must be a zero-initialized aggregate, NOT undef: an undef map has
            // undef data/size/cap, so the caller's put/free walks a garbage
            // pointer → SIGSEGV/SIGABRT.  Zero = well-defined empty container.
            ctx.ir_gen->createRet(ctx.ir_gen->wrap(
                llvm::ConstantAggregateZero::get(unwrappedRetTy)));
        } else {
            ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::UndefValue::get(unwrappedRetTy)));
        }
    }

    // Restore insertion point and symbol table (function-local vars don't leak)
    ctx.symbol_table = savedSymbols;
    ctx.ir_gen->setInsertPoint(oldBlock);
    ctx.currentFunctionName = "";
    /* Restore decorator context — see snapshot at the top of visitFunction. */
    ctx.currentFunctionDecorators = std::move(_saved_decorators);
    ctx.currentFunctionReturnType = std::move(_saved_return_type);
}
Symbol* Compiler::resolveVariable(const std::string& name) {
    if (ctx.currentScope) {
        CompilerContext::Scope* scope = ctx.currentScope;
        while (scope) {
            if (scope->variables.count(name)) return &scope->variables[name];
            scope = scope->parent;
        }
    }
    
    // Check namespace globals
    if (ctx.currentNs && ctx.currentNs->globals.count(name)) {
        return &ctx.currentNs->globals[name];
    }
    
    // Legacy fallback during transition
    if (ctx.symbol_table.count(name)) return &ctx.symbol_table[name];
    
    // Check FQN fallback (e.g. `math.pi` -> `my.math.pi`)
    std::string qualifiedName = ctx.qualify(name);
    if (ctx.symbol_table.count(qualifiedName)) return &ctx.symbol_table[qualifiedName];
    
    return nullptr;
}
IRValue* Compiler::resolveEnum(const std::string& baseName, const std::string& propName) {
    // Check current namespace.  Registration keys enumDeclMembers by the QUALIFIED
    // name (ctx.qualify), so a namespace-local reference records its use under the
    // same qualified key (spec/49 unused-enumerator analysis).
    if (ctx.currentNs && ctx.currentNs->enums.count(baseName)) {
        auto& enumDef = ctx.currentNs->enums[baseName];
        if (enumDef.count(propName)) {
            ctx.enumUsedMembers[ctx.qualify(baseName)].insert(propName);
            return ctx.ir_gen->getInt32(enumDef[propName]);
        }
    }

    // Legacy fallback: ctx.enums is keyed by the qualified name already, so a hit
    // means baseName IS that key — record the use directly under it.
    if (ctx.enums.count(baseName)) {
        auto& enumDef = ctx.enums[baseName];
        if (enumDef.count(propName)) {
            ctx.enumUsedMembers[baseName].insert(propName);
            return ctx.ir_gen->getInt32(enumDef[propName]);
        }
    }

    LOG_ERROR("Undefined enum variant: " + propName + " on enum " + baseName);
    return nullptr;
}
IRValue* Compiler::resolveProperty(Symbol* baseSym, const std::string& propName) {
    std::string className = baseSym->typeName;
    if ((className.rfind("ptr<", 0) == 0 || className.rfind("ref<", 0) == 0) && className.back() == '>') {
         className = className.substr(4, className.length() - 5);
    } else if (className.rfind("shared<", 0) == 0 && className.back() == '>') {
         className = className.substr(7, className.length() - 8);  // spec/61 §6.2 shared<T> member access
    }
    // buffer<T> → "buffer" (all buffer variants share the same class registration)
    if (className.rfind("buffer<", 0) == 0 && className.back() == '>') {
        className = "buffer";
    }
    // cascade<X> shares the list<i32> layout — normalize before property
    // dispatch so `.len` / `.empty` resolve.
    if (className.rfind("cascade<", 0) == 0 && className.back() == '>') {
        className = "list<i32>";
    }
    
    IRValue* baseAlloca = baseSym->ir_alloca_inst ? baseSym->ir_alloca_inst : ctx.ir_gen->wrap(baseSym->alloca_inst);
    IRType* baseElemTy = baseSym->ir_element_type ? baseSym->ir_element_type : ctx.ir_gen->wrapType(baseSym->element_type);

    // list/map by-ref args: alloca enthält den pointer auf das echte struct.
    // Eine Stufe dereferenzieren bevor wir property-access machen.
    if (baseSym->is_byref_arg) {
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* allocaRaw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
        llvm::Value* loaded = ctx.builder.CreateLoad(i8p, allocaRaw, "byref_deref_prop");
        baseAlloca = ctx.ir_gen->wrap(loaded);
    }

    if (className.find("list<") == 0) {
        if (propName == "len") return ctx.ir_gen->wrap(ListImpl::getSize(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca))));
        if (propName == "empty") {
            IRValue* size = ctx.ir_gen->wrap(ListImpl::getSize(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca))));
            IRValue* cmp = ctx.ir_gen->createICmpEQ(size, ctx.ir_gen->getInt32(0));
            return ctx.ir_gen->wrap(ctx.builder.CreateZExt(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(cmp)), ctx.builder.getInt32Ty(), "empty_zext"));
        }
    } else if (className.find("map<") == 0) {
        llvm::StructType* mapStructTy = static_cast<llvm::StructType*>(ctx.ir_gen->unwrapType(baseElemTy));
        if (propName == "len") return ctx.ir_gen->wrap(MapImpl::getSize(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), mapStructTy));
        if (propName == "empty") {
            IRValue* size = ctx.ir_gen->wrap(MapImpl::getSize(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), mapStructTy));
            IRValue* cmp = ctx.ir_gen->createICmpEQ(size, ctx.ir_gen->getInt32(0));
            return ctx.ir_gen->wrap(ctx.builder.CreateZExt(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(cmp)), ctx.builder.getInt32Ty(), "empty_zext"));
        }
    } else if (className == "string") {
        if (propName == "len") {
            llvm::Function* strlenFunc = ctx.module.getFunction("strlen");
            if (!strlenFunc)
                strlenFunc = llvm::Function::Create(
                    llvm::FunctionType::get(ctx.getSizeType(),
                        {ctx.builder.getInt8Ty()->getPointerTo()}, false),
                    llvm::Function::ExternalLinkage, "strlen", ctx.module);
            llvm::Value* strVal = ctx.builder.CreateLoad(
                ctx.builder.getInt8Ty()->getPointerTo(),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)));
            llvm::Value* len64 = ctx.builder.CreateCall(strlenFunc, {strVal});
            return ctx.ir_gen->wrap(ctx.builder.CreateZExtOrTrunc(len64, ctx.builder.getInt32Ty(), "str_len_i32"));
        }
    }

    // ── array<T:N[:M[:...]]> properties: .len, .data_ptr, .dim0, .dim1, ... ──
    if (className.rfind("array<", 0) == 0 && className.back() == '>') {
        std::string inner = className.substr(6, className.size() - 7);
        std::vector<std::string> parts;
        size_t pos = 0;
        while (pos < inner.size()) {
            size_t sep = inner.find(':', pos);
            if (sep == std::string::npos) { parts.push_back(inner.substr(pos)); break; }
            parts.push_back(inner.substr(pos, sep - pos));
            pos = sep + 1;
        }
        std::vector<uint64_t> dims;
        for (size_t i = 1; i < parts.size(); ++i) dims.push_back(std::stoull(parts[i]));
        uint64_t totalCount = 1;
        for (auto d : dims) totalCount *= d;

        if (propName == "len") {
            return ctx.ir_gen->wrap(ctx.builder.getInt32((uint32_t)totalCount));
        }
        if (propName == "data_ptr") {
            llvm::Type* elemTy = ctx.getLLVMType(parts[0]);
            llvm::Type* arrTy = llvm::ArrayType::get(elemTy, totalCount);
            llvm::Value* arrPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
            llvm::Value* gep = ctx.builder.CreateGEP(arrTy, arrPtr,
                {ctx.builder.getInt32(0), ctx.builder.getInt32(0)}, "arr_data_ptr");
            return ctx.ir_gen->wrap(ctx.builder.CreateBitCast(gep, ctx.builder.getInt8Ty()->getPointerTo()));
        }
        // .dim0, .dim1, ...
        if (propName.rfind("dim", 0) == 0 && propName.size() > 3) {
            size_t dimIdx = std::stoul(propName.substr(3));
            if (dimIdx < dims.size())
                return ctx.ir_gen->wrap(ctx.builder.getInt32((uint32_t)dims[dimIdx]));
            LOG_ERROR(ctx.fmtLoc() + "array property " + propName + " out of range");
            return ctx.ir_gen->getInt32(0);
        }
    }

    // Check classes (legacy + namespace)
    CompilerContext::ClassInfo* classInfo = nullptr;
    if (ctx.currentNs && ctx.currentNs->classes.count(className)) classInfo = &ctx.currentNs->classes[className];
    else if (ctx.classes.count(className)) classInfo = &ctx.classes[className];
    
    if (classInfo) {
        // Handle chained property access: "field.rest" or "field-idx"
        {
            size_t dotP  = propName.find('.');
            size_t dashP = propName.find('-');
            if (dotP != std::string::npos || dashP != std::string::npos) {
                bool dashFirst = (dashP != std::string::npos) &&
                                 (dotP == std::string::npos || dashP < dotP);
                size_t sepP       = dashFirst ? dashP : dotP;
                std::string fieldName = propName.substr(0, sepP);
                std::string rest      = propName.substr(sepP + 1);

                if (classInfo->members.count(fieldName)) {
                    auto& member = classInfo->members[fieldName];
                    IRType* structTy = ctx.ir_gen->wrapType(classInfo->type);
                    IRValue* structPtr = baseAlloca;
                    if (baseElemTy->isPointer())
                        structPtr = ctx.ir_gen->createLoad(baseElemTy, baseAlloca, "chain_base");
                    IRValue* fieldGep   = ctx.ir_gen->createStructGEP(structTy, structPtr, member.index, "chain_gep");
                    IRType*  fieldElemTy = ctx.ir_gen->wrapType(member.type);

                    if (dashFirst) {
                        // List index: field-idx
                        if (member.typeName.rfind("list<", 0) != 0 || member.typeName.back() != '>') {
                            LOG_ERROR("Dash-index on non-list field: " + fieldName + " (type: " + member.typeName + ")");
                            return nullptr;
                        }
                        std::string elemName = member.typeName.substr(5, member.typeName.length() - 6);
                        llvm::Type* elemType = ctx.getLLVMType(elemName);
                        llvm::Value* listAllocaVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(fieldGep));
                        // spec/61 §5 Option B (memV3): the field is a POINTER to the handle → load it.
                        if (ctx.memV3) {
                            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                            listAllocaVal = ctx.builder.CreateLoad(i8p, listAllocaVal, "listh_idx");
                        }

                        llvm::Value* idxVal = nullptr;
                        try {
                            idxVal = ctx.builder.getInt32(std::stoi(rest));
                        } catch (...) {
                            Symbol* idxSym = resolveVariable(rest);
                            if (idxSym) {
                                IRValue* idxA  = idxSym->ir_alloca_inst ? idxSym->ir_alloca_inst : ctx.ir_gen->wrap(idxSym->alloca_inst);
                                IRType*  idxET = idxSym->ir_element_type ? idxSym->ir_element_type : ctx.ir_gen->wrapType(idxSym->element_type);
                                idxVal = ctx.builder.CreateLoad(
                                    static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(idxET)),
                                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(idxA)));
                                if (idxVal->getType()->isIntegerTy(64))
                                    idxVal = ctx.builder.CreateTrunc(idxVal, ctx.builder.getInt32Ty());
                            } else {
                                LOG_ERROR("Invalid list index: " + rest);
                                return ctx.ir_gen->getInt32(0);
                            }
                        }
                        llvm::Value* elemPtr = ListImpl::getElementPtr(ctx, listAllocaVal, idxVal, elemType);
                        size_t dotInRest = rest.find('.');
                        if (dotInRest == std::string::npos) {
                            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(elemType, elemPtr, "chain_elem"));
                        }

                        std::string afterIdx = rest.substr(dotInRest + 1);
                        Symbol tmpSym;
                        tmpSym.typeName = elemName;
                        tmpSym.ir_alloca_inst = ctx.ir_gen->wrap(elemPtr);
                        tmpSym.ir_element_type = ctx.ir_gen->wrapType(elemType);
                        return resolveProperty(&tmpSym, afterIdx);
                    } else {
                        // Dot chain: field.rest — recurse
                        Symbol tmpSym;
                        tmpSym.typeName         = member.typeName;
                        tmpSym.ir_alloca_inst   = fieldGep;
                        tmpSym.ir_element_type  = fieldElemTy;
                        // spec/61 §5 Option B (memV3): a list<T>/cascade<X>/map<K:V> field is a
                        // POINTER to a handle → mark byref so the recursion derefs the slot to the
                        // handle before property access (.len/.empty/…).
                        if (ctx.memV3 &&
                            (member.typeName.rfind("list<", 0) == 0 || member.typeName.rfind("cascade<", 0) == 0)
                            && member.typeName.back() == '>') {
                            tmpSym.is_byref_arg = true;
                            tmpSym.ir_element_type = ctx.ir_gen->wrapType(ListImpl::getListStructType(ctx));
                        } else if (ctx.memV3 && member.typeName.rfind("map<", 0) == 0
                                   && member.typeName.back() == '>') {
                            std::string minner = member.typeName.substr(4, member.typeName.length() - 5);
                            size_t mcolon = minner.find(':');
                            std::string mkName = (mcolon == std::string::npos) ? minner : minner.substr(0, mcolon);
                            std::string mvName = (mcolon == std::string::npos) ? std::string("i32") : minner.substr(mcolon + 1);
                            auto* i8pT = ctx.builder.getInt8Ty()->getPointerTo();
                            llvm::Type* mkTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(mkName)));
                            llvm::Type* mvTy = ctx.isRefcountModel(mvName) ? i8pT
                                : static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(mvName)));
                            if (!mkTy) mkTy = ctx.builder.getInt32Ty();
                            if (!mvTy) mvTy = ctx.builder.getInt32Ty();
                            tmpSym.is_byref_arg = true;
                            tmpSym.ir_element_type = ctx.ir_gen->wrapType(MapImpl::getMapStructType(ctx, mkTy, mvTy));
                        } else if (ctx.memV3 && (member.typeName == "buffer"
                                   || (member.typeName.rfind("buffer<", 0) == 0 && member.typeName.back() == '>'))) {
                            tmpSym.is_byref_arg = true;
                            tmpSym.ir_element_type = ctx.ir_gen->wrapType(ctx.getLLVMType("buffer"));
                        }
                        return resolveProperty(&tmpSym, rest);
                    }
                }
            }
        }

        if (classInfo->members.count(propName)) {
            auto& member = classInfo->members[propName];
            IRType* structTy = ctx.ir_gen->wrapType(classInfo->type);
            IRValue* structPtr = baseAlloca;

            if (baseElemTy->isPointer()) {
                structPtr = ctx.ir_gen->createLoad(baseElemTy, baseAlloca, "prop_ptr_base");
            }

            IRValue* gep = ctx.ir_gen->createStructGEP(structTy, structPtr, member.index, "prop_ptr");
            // For container fields (list<T>/buffer<T>/map<K:V>), return the GEP
            // pointer directly.  visitDeclStmt/visitSetStmt expect a pointer to
            // memcpy from (not a loaded struct value).  Loading the struct and
            // passing it as memcpy src would use its size/inner field as a ptr.
            //
            // Also required for free-function calls that take container-typed
            // args (e.g. `lookup(key, holder.vocab_index)`): the callee's
            // signature is `ptr`, the coerceArgs path at line ~396 in
            // ast_compiler_call.cpp converts ptr→struct if the callee wants
            // value, but it can't reconstruct a ptr from a loaded struct.
            // Repro: aiml/tests/test_class_member_as_func_arg.aiml
            // spec/61 §5 Option B (memV3): a list<T>/cascade<X>/map<K:V> field is a POINTER to a
            // handle — LOAD it so callers get the handle (a pointer to the container struct),
            // exactly the shape ListImpl/MapImpl ops / byref-container params expect. buffer fields
            // stay inline (return gep).
            if (ctx.memV3 &&
                (member.typeName.rfind("list<", 0) == 0 || member.typeName.rfind("cascade<", 0) == 0
                 || member.typeName.rfind("map<", 0) == 0 || member.typeName == "buffer"
                 || member.typeName.rfind("buffer<", 0) == 0)) {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                return ctx.ir_gen->wrap(ctx.builder.CreateLoad(i8p,
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(gep)), propName + "_conh"));
            }
            if (member.typeName.rfind("list<", 0) == 0 ||
                member.typeName.rfind("buffer<", 0) == 0 ||
                member.typeName.rfind("map<", 0) == 0) {
                return gep;
            }
            IRType* propTy = ctx.ir_gen->wrapType(member.type);
            return ctx.ir_gen->createLoad(propTy, gep, propName + "_val");
        }

        // Builtin property getter: dispatch builtinMethod with same name (zero-arg accessor).
        // If preLoadBase, load the handle ptr from alloca first (HTTP handles).
        if (classInfo->isBuiltin && classInfo->builtinMethods.count(propName)) {
            llvm::Value* rawAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
            llvm::Value* baseForHandler = classInfo->preLoadBase
                ? ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), rawAlloca, "h_base_p")
                : rawAlloca;
            auto dummyEval = [](AIML::AST::Expr*) -> AIML::IRValue* { return nullptr; };
            return classInfo->builtinMethods.at(propName)(ctx, baseForHandler, nullptr, dummyEval);
        }
    }

    LOG_ERROR("Undefined property: " + propName + " on type " + className);
    return nullptr;
}

IRValue* Compiler::emitLValueAddress(Expr* e, std::string& outTypeName) {
    auto* ve = dynamic_cast<VariableExpr*>(e);
    if (!ve) return nullptr;
    std::string name = ve->name;
    if (name.rfind("<.", 0) == 0) name = "this." + name.substr(2);
    size_t dot = name.find('.');
    std::string base = (dot == std::string::npos) ? name : name.substr(0, dot);
    Symbol* sym = resolveVariable(base);
    if (!sym) return nullptr;
    IRValue* alloca = sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst);
    if (!alloca) return nullptr;
    if (dot == std::string::npos) {
        outTypeName = sym->typeName;
        if (sym->is_byref_arg) {
            // alloca holds a pointer-to-struct (list/map/by-ref param); the
            // address of the value IS that stored pointer — load it once.
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* rawAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca));
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(i8p, rawAlloca, "lval_byref"));
        }
        return alloca;
    }
    std::string prop = name.substr(dot + 1);
    Symbol tmp;
    tmp.typeName        = sym->typeName;
    tmp.ir_alloca_inst  = alloca;
    tmp.ir_element_type = sym->ir_element_type ? sym->ir_element_type
                                               : ctx.ir_gen->wrapType(sym->element_type);
    return resolvePropertyWritePtr(&tmp, prop, outTypeName);
}

IRValue* Compiler::resolvePropertyWritePtr(Symbol* baseSym, const std::string& propName, std::string& outTypeName) {
    std::string className = baseSym->typeName;
    if ((className.rfind("ptr<", 0) == 0 || className.rfind("ref<", 0) == 0) && className.back() == '>') {
        className = className.substr(4, className.length() - 5);
    } else if (className.rfind("shared<", 0) == 0 && className.back() == '>') {
        className = className.substr(7, className.length() - 8);  // spec/61 §6.2 shared<T> member write
    }

    IRValue* baseAlloca = baseSym->ir_alloca_inst ? baseSym->ir_alloca_inst : ctx.ir_gen->wrap(baseSym->alloca_inst);
    IRType* baseElemTy  = baseSym->ir_element_type ? baseSym->ir_element_type : ctx.ir_gen->wrapType(baseSym->element_type);

    CompilerContext::ClassInfo* classInfo = nullptr;
    if (ctx.currentNs && ctx.currentNs->classes.count(className)) classInfo = &ctx.currentNs->classes[className];
    else if (ctx.classes.count(className)) classInfo = &ctx.classes[className];

    if (!classInfo) { LOG_DEBUG("resolvePropertyWritePtr: no classInfo for " + className); return nullptr; }
    LOG_DEBUG("resolvePropertyWritePtr: class=" + className + " members=" + std::to_string(classInfo->members.size()) + " prop=" + propName);

    // Handle chained access: "field.rest" or "field-idx[.rest]"
    {
        size_t dotP  = propName.find('.');
        size_t dashP = propName.find('-');
        if (dotP != std::string::npos || dashP != std::string::npos) {
            bool dashFirst = (dashP != std::string::npos) &&
                             (dotP == std::string::npos || dashP < dotP);
            size_t sepP       = dashFirst ? dashP : dotP;
            std::string fieldName = propName.substr(0, sepP);
            std::string rest      = propName.substr(sepP + 1);

            if (classInfo->members.count(fieldName)) {
                auto& member = classInfo->members[fieldName];

                if (member.access == AccessLevel::Private && ctx.currentClassName != className) {
                    LOG_ERROR("Cannot access private member " + fieldName);
                    return nullptr;
                } else if (member.access == AccessLevel::Protected && ctx.currentClassName != className) {
                    LOG_ERROR("Cannot access protected member " + fieldName);
                    return nullptr;
                }

                IRType* structTy   = ctx.ir_gen->wrapType(classInfo->type);
                IRValue* structPtr = baseAlloca;
                if (baseElemTy->isPointer())
                    structPtr = ctx.ir_gen->createLoad(baseElemTy, baseAlloca, "chain_base_w");
                IRValue* fieldGep    = ctx.ir_gen->createStructGEP(structTy, structPtr, member.index, "chain_gep_w");
                IRType*  fieldElemTy = ctx.ir_gen->wrapType(member.type);

                if (dashFirst) {
                    // List element: field-idx[.further]
                    if (member.typeName.rfind("list<", 0) != 0 || member.typeName.back() != '>') {
                        LOG_ERROR("Dash-index on non-list field: " + fieldName);
                        return nullptr;
                    }
                    std::string elemName = member.typeName.substr(5, member.typeName.length() - 6);
                    llvm::Type* elemType = ctx.getLLVMType(elemName);
                    llvm::Value* listAllocaVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(fieldGep));

                    // rest may be "idx" or "idx.further"
                    size_t dotInRest = rest.find('.');
                    std::string idxStr   = (dotInRest != std::string::npos) ? rest.substr(0, dotInRest) : rest;
                    std::string afterIdx = (dotInRest != std::string::npos) ? rest.substr(dotInRest + 1) : "";

                    llvm::Value* idxVal = nullptr;
                    try {
                        idxVal = ctx.builder.getInt32(std::stoi(idxStr));
                    } catch (...) {
                        Symbol* idxSym = resolveVariable(idxStr);
                        if (idxSym) {
                            IRValue* idxA  = idxSym->ir_alloca_inst ? idxSym->ir_alloca_inst : ctx.ir_gen->wrap(idxSym->alloca_inst);
                            IRType*  idxET = idxSym->ir_element_type ? idxSym->ir_element_type : ctx.ir_gen->wrapType(idxSym->element_type);
                            idxVal = ctx.builder.CreateLoad(
                                static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(idxET)),
                                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(idxA)));
                            if (idxVal->getType()->isIntegerTy(64))
                                idxVal = ctx.builder.CreateTrunc(idxVal, ctx.builder.getInt32Ty());
                        } else {
                            LOG_ERROR("Invalid list index: " + idxStr);
                            return nullptr;
                        }
                    }
                    llvm::Value* elemPtr = ListImpl::getElementPtr(ctx, listAllocaVal, idxVal, elemType);

                    if (afterIdx.empty()) {
                        outTypeName = elemName;
                        return ctx.ir_gen->wrap(elemPtr);
                    } else {
                        Symbol tmpSym;
                        tmpSym.typeName        = elemName;
                        tmpSym.ir_alloca_inst  = ctx.ir_gen->wrap(elemPtr);
                        tmpSym.ir_element_type = ctx.ir_gen->wrapType(elemType);
                        return resolvePropertyWritePtr(&tmpSym, afterIdx, outTypeName);
                    }
                } else {
                    // Dot chain: recurse into next field
                    Symbol tmpSym;
                    tmpSym.typeName        = member.typeName;
                    tmpSym.ir_alloca_inst  = fieldGep;
                    tmpSym.ir_element_type = fieldElemTy;
                    return resolvePropertyWritePtr(&tmpSym, rest, outTypeName);
                }
            }
        }
    }

    // Direct (leaf) member
    if (classInfo->members.count(propName)) {
        auto& member = classInfo->members[propName];
        if (member.access == AccessLevel::Private && ctx.currentClassName != className) {
            LOG_ERROR("Cannot access private member " + propName);
            return nullptr;
        } else if (member.access == AccessLevel::Protected && ctx.currentClassName != className) {
            LOG_ERROR("Cannot access protected member " + propName);
            return nullptr;
        }
        if (member.is_const) {
            LOG_ERROR("Cannot assign to const member " + propName);
            return nullptr;
        }

        IRType* structTy   = ctx.ir_gen->wrapType(classInfo->type);
        IRValue* structPtr = baseAlloca;
        if (baseElemTy->isPointer())
            structPtr = ctx.ir_gen->createLoad(baseElemTy, baseAlloca, "prop_ptr_base_w");

        outTypeName = member.typeName;
        IRValue* fieldGep = ctx.ir_gen->createStructGEP(structTy, structPtr, member.index, "set_prop_ptr_w");
        // spec/61 §5 Option B (memV3): a list<T>/cascade<X>/map<K:V> field is a POINTER to a
        // handle. As a method RECEIVER (e.g. `obj.tags.push`, `obj.slots.put`) callers want the
        // handle (a pointer to the container struct), so LOAD it. (A whole-container replace
        // `set:[obj.tags, L]` would want the slot address; not yet exercised under memV3.)
        if (ctx.memV3 &&
            ((member.typeName.rfind("list<", 0) == 0 || member.typeName.rfind("cascade<", 0) == 0
              || member.typeName.rfind("map<", 0) == 0 || member.typeName.rfind("buffer<", 0) == 0)
              ? member.typeName.back() == '>'
              : member.typeName == "buffer")) {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(i8p,
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(fieldGep)), "conh_recv_w"));
        }
        return fieldGep;
    }

    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// buildInterfaceUpcast — emit fat-pointer struct {obj_ptr, vtable_ptr} for
// passing a `ptr<ConcreteClass>` into a slot that expects `ptr<Interface>`.
//
// Used by:
//   • visitDeclStmt        (decl: [h, ptr<Iface>, h_concrete])  ← inline today
//   • map.put              (map<string:ptr<Iface>>.put(k, h_concrete))
//   • visitObjectMethodCall (method arg of type ptr<Iface>, passed h_concrete)
//
// Returns the loaded struct value (suitable as call-arg).  Returns nullptr if
// the upcast is invalid — caller should LOG_ERROR.
// ─────────────────────────────────────────────────────────────────────────────
llvm::Value* Compiler::buildInterfaceUpcast(llvm::Value* srcValue,
                                            const std::string& srcTypeName,
                                            llvm::StructType* targetStructTy) {
    if (!srcValue || !targetStructTy) return nullptr;

    // Extract concrete class name from srcTypeName "ptr<Concrete>" / "ref<Concrete>".
    std::string concrete;
    if ((srcTypeName.rfind("ptr<", 0) == 0 || srcTypeName.rfind("ref<", 0) == 0) &&
        srcTypeName.back() == '>') {
        concrete = srcTypeName.substr(4, srcTypeName.size() - 5);
    } else {
        return nullptr;
    }

    // Extract interface name from targetStructTy ("AimlFatPtr_<Iface>").
    std::string structName = targetStructTy->hasName()
        ? targetStructTy->getName().str() : std::string();
    static const std::string kPrefix = "AimlFatPtr_";
    if (structName.rfind(kPrefix, 0) != 0) return nullptr;
    std::string ifaceName = structName.substr(kPrefix.size());

    // Lookup vtable global.  In multi-file builds the vtable is defined in
    // the .o where the implementing class lives; this compilation unit only
    // needs an extern forward-declaration.  Emit one if missing — the linker
    // resolves it against the real LinkOnceODR definition emitted by
    // visitTopLevel(ClassDecl).  Without this, cross-file iface dispatch
    // got a null vtable slot and SIGSEGV'd on first method call.
    std::string vtableName = "__vtable_" + concrete + "_" + ifaceName;
    llvm::GlobalVariable* vtbl = ctx.module.getNamedGlobal(vtableName);
    if (!vtbl) {
        // Validate that the concrete class actually implements the interface
        // in ctx — otherwise emitting an extern would just defer the error to
        // the linker as "undefined symbol".  Better to fail fast here.
        bool implementsIface = false;
        if (ctx.classes.count(concrete)) {
            for (const auto& impl : ctx.classes[concrete].interfaces) {
                if (impl == ifaceName) { implementsIface = true; break; }
            }
        }
        if (!implementsIface) return nullptr;
        auto* i8p_local = ctx.builder.getInt8Ty()->getPointerTo();
        // +1 for the trailing __free_<Class> slot (spec/61 return-ownership). MUST match the
        // vtable DEFINITION's size (visitTopLevel appends the free slot) or the LinkOnceODR
        // globals collide with mismatched array types across compilation units.
        size_t methodCount = (ctx.interfaces.count(ifaceName)
            ? ctx.interfaces[ifaceName].methods.size() : 1) + 1;
        auto* arrTy = llvm::ArrayType::get(i8p_local, methodCount);
        vtbl = new llvm::GlobalVariable(ctx.module, arrTy, /*isConstant=*/true,
            llvm::GlobalValue::ExternalLinkage, /*initializer=*/nullptr, vtableName);
    }

    // Build temp fat-pointer struct, store obj+vtbl, load and return.
    auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
    llvm::AllocaInst* tmpFat = ctx.createEntryAlloca(targetStructTy);
    llvm::Value* objField  = ctx.builder.CreateStructGEP(targetStructTy, tmpFat, 0, "fpup_obj");
    llvm::Value* vtblField = ctx.builder.CreateStructGEP(targetStructTy, tmpFat, 1, "fpup_vtbl");
    ctx.builder.CreateStore(ctx.builder.CreateBitCast(srcValue, i8p), objField);
    ctx.builder.CreateStore(ctx.builder.CreateBitCast(vtbl, i8p), vtblField);
    return ctx.builder.CreateLoad(targetStructTy, tmpFat, "fpup_load");
}

CompilerContext::FunctionInfo* Compiler::resolveFunction(const std::string& name) {
    if (ctx.currentNs && ctx.currentNs->functions.count(name)) return &ctx.currentNs->functions[name];
    if (ctx.functions.count(name)) return &ctx.functions[name];

    // Check FQN fallback (e.g., calling `calc` inside `my.math` resolves to `my.math.calc`)
    std::string qualifiedName = ctx.qualify(name);
    if (ctx.functions.count(qualifiedName)) return &ctx.functions[qualifiedName];

    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// sys.clone(x) — generic deep-copy
//
// Builds TypeNodes for x's declared type, allocates a fresh dst of the same
// type, and calls aiml_ser_deep_copy(dst, &x, nodes, n_nodes).  Independent
// heap allocations are minted for every string/list/map/buffer/struct field
// so mutations on the clone don't leak back to the source (and vice-versa).
//
// Restrictions:
//   • arg must be a single variable (we need the symbol-table type)
//   • arg-type must be serializable (no `any`, `chan<T>`, threads, …)
//
// Returns a struct-typed IRValue (loaded clone) so the caller can use it
// as init-value for `decl: [y, T, { call: [sys.clone, x] }]`.
// ─────────────────────────────────────────────────────────────────────────────
IRValue* Compiler::visitSysCloneCall(CallExpr* call) {
    llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
    llvm::Type* i32Ty = ctx.builder.getInt32Ty();
    llvm::Type* voidTy = ctx.builder.getVoidTy();

    if (call->args.empty()) {
        LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "sys.clone: requires one argument");
        return ctx.ir_gen->getInt32(0);
    }

    // Resolve argument variable + type.
    auto* var = dynamic_cast<VariableExpr*>(call->args[0].get());
    if (!var) {
        LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "sys.clone: argument must be a variable name");
        return ctx.ir_gen->getInt32(0);
    }
    Symbol* argSym = resolveVariable(var->name);
    if (!argSym) {
        LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "sys.clone: unknown variable '" + var->name + "'");
        return ctx.ir_gen->getInt32(0);
    }
    const std::string& argType = argSym->typeName;
    if (argType.empty() || argType == "any") {
        LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "sys.clone: type '" + argType + "' is not cloneable");
        return ctx.ir_gen->getInt32(0);
    }

    // Resolve LLVM struct type so we know dst's size.
    llvm::Type* dstLlvmTy = ctx.getLLVMType(argType);
    if (!dstLlvmTy) {
        LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "sys.clone: unknown LLVM type for '" + argType + "'");
        return ctx.ir_gen->getInt32(0);
    }

    // Build TypeNodes for the argument type and emit them as a global constant.
    llvm::DataLayout dl(&ctx.module);
    std::vector<AimlTypeNode> nodes = serBuildSubtree(argType, 0, ctx, dl);
    if (nodes.empty()) {
        LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "sys.clone: failed to build TypeNodes for '" + argType + "'");
        return ctx.ir_gen->getInt32(0);
    }
    // Emit a private global once per (typeName, callsite) — LinkOnceODR merges duplicates.
    llvm::LLVMContext& lctx = ctx.context;
    llvm::StructType* nodeTy = llvm::StructType::get(lctx, {i32Ty, i32Ty, i32Ty, i32Ty, i32Ty, i32Ty});
    std::vector<llvm::Constant*> elems;
    for (auto& n : nodes) {
        llvm::Constant* f[6] = {
            llvm::ConstantInt::get(i32Ty, (uint64_t)n.kind),
            llvm::ConstantInt::get(i32Ty, (uint64_t)n.param1),
            llvm::ConstantInt::get(i32Ty, (uint64_t)n.param2),
            llvm::ConstantInt::get(i32Ty, (uint64_t)n.offset),
            llvm::ConstantInt::get(i32Ty, (uint64_t)n.elem_size),
            llvm::ConstantInt::get(i32Ty, 0),
        };
        elems.push_back(llvm::ConstantStruct::get(nodeTy, f));
    }
    llvm::ArrayType* arrTy = llvm::ArrayType::get(nodeTy, elems.size());
    // Symbol name mangled from the typeName — duplicates across translation units
    // get merged by the linker (LinkOnceODR).
    std::string symName = "__aiml_sernodes_clone_" + argType;
    for (char& c : symName) if (c=='<'||c=='>'||c==':'||c==' '||c==','||c=='/') c = '_';
    llvm::GlobalVariable* gv = ctx.module.getGlobalVariable(symName);
    if (!gv) {
        gv = new llvm::GlobalVariable(ctx.module, arrTy, /*isConstant=*/true,
            llvm::GlobalValue::LinkOnceODRLinkage,
            llvm::ConstantArray::get(arrTy, elems),
            symName);
        gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
        gv->setVisibility(llvm::GlobalValue::HiddenVisibility);
    }

    // dst alloca, zero-initialized (precondition of aiml_ser_deep_copy).
    llvm::AllocaInst* dstAlloca = ctx.createEntryAlloca(dstLlvmTy);
    uint64_t dstSize = dl.getTypeAllocSize(dstLlvmTy);
    llvm::Function* memsetF = ctx.getFunction("memset", ptrTy,
        {ptrTy, i32Ty, ctx.getSizeType()});
    ctx.builder.CreateCall(memsetF, {
        dstAlloca,
        ctx.builder.getInt32(0),
        ctx.getSizeConst(dstSize)
    });

    // src pointer = address-of caller's alloca.
    IRValue* srcBase = argSym->ir_alloca_inst ? argSym->ir_alloca_inst
                                              : ctx.ir_gen->wrap(argSym->alloca_inst);
    llvm::Value* srcPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(srcBase));

    // spec/61 §5.1: under memV3 use the memoized in-memory clone (shared<T>/%M*
    // sub-objects cloned once → DAG-preserving, cycle-safe). Otherwise fall back
    // to the encode/decode roundtrip (deployed mirror/KV path, byte-identical).
    const char* cloneFn = ctx.memV3 ? "aiml_clone_deep" : "aiml_ser_deep_copy";
    llvm::Function* dcF = ctx.getFunction(cloneFn, voidTy,
        {ptrTy, ptrTy, ptrTy, i32Ty});
    ctx.builder.CreateCall(dcF, {
        dstAlloca,
        srcPtr,
        gv,
        ctx.builder.getInt32((int32_t)nodes.size())
    });

    // Return the loaded dst struct so the caller can use it as init-value for
    // a decl<T>.  visitDeclStmt will memcpy it into the target alloca.
    llvm::Value* loaded = ctx.builder.CreateLoad(dstLlvmTy, dstAlloca, "sys_clone_dst");
    return ctx.ir_gen->wrap(loaded);
}

} // namespace AST
} // namespace AIML
