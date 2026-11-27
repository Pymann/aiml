#define DEBUG_MODE
#include "serialize_impl.h"
#include <cctype>   // std::isdigit (classgroup constructor literals)
#include "ast_compiler.h"
#include "yaml_dump.h"
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
#include "hashmap_impl.h"
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
    if (auto* existing = ctx.module().getFunction(stubName)) return existing;

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
        stubTy, llvm::GlobalValue::PrivateLinkage, stubName, ctx.module());
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

    auto& DL = ctx.module().getDataLayout();
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
    if (auto* existing = ctx.module().getFunction(name)) return existing;
    auto* i8p = llvm::PointerType::get(ctx.context, 0);
    auto* fnTy = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx.context), {i8p}, false);
    auto* fn = llvm::Function::Create(
        fnTy, llvm::GlobalValue::LinkOnceODRLinkage, name, ctx.module());
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
    // spec/58 §12: a classgroup container behind an interface (a group interface)
    // owns its instances — release them through its synthesized __dtor, exactly
    // like `free: <group>` does (ast_compiler_call.cpp), before the shell free.
    if (ctx.classGroupContainers.count(className)) {
        llvm::FunctionCallee dtor = ctx.module().getOrInsertFunction(
            className + "___dtor", llvm::FunctionType::get(llvm::Type::getVoidTy(ctx.context), {i8p}, false));
        ctx.builder.CreateCall(dtor, { obj });
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
                                         const std::string& className,
                                         unsigned fromIndex) {
    auto it = ctx.classes.find(className);
    if (it == ctx.classes.end()) return;
    auto& info = it->second;
    if (!info.type) return;  // builtin / opaque — no struct layout to walk
    for (const auto& [memberName, m] : info.members) {
        if (m.index < fromIndex) continue;   // prefix comes from the base value
        const std::string& mt = m.typeName;
        llvm::Value* fieldPtr = ctx.builder.CreateStructGEP(
            info.type, basePtr, m.index, memberName + "_init_gep");
        bool cascadeField = ctx.memV3 && mt.rfind("cascade<", 0) == 0 && mt.back() == '>';   // a list<i32> row
        if ((mt.rfind("list<", 0) == 0 && mt.back() == '>') || cascadeField) {
            std::string elemName = cascadeField ? std::string("i32") : mt.substr(5, mt.length() - 6);
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
        } else if (HashmapImpl::isHashmapType(mt)) {
            HashmapImpl::initSlot(ctx, fieldPtr, mt);
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
                        : ctx.module().getDataLayout().getTypeAllocSize(ci->second.type);
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
                    // spec/15: the default takes the field's type (a `90.0` literal is f32, the field
                    // may be f64; storing the narrower value would leave half the slot garbage).
                    llvm::Type* slotTy = m.type;
                    llvm::Type* valTy = raw->getType();
                    if (slotTy && valTy != slotTy) {
                        if (slotTy->isFloatingPointTy() && valTy->isFloatingPointTy())
                            raw = ctx.builder.CreateFPCast(raw, slotTy, "default_fp");
                        else if (slotTy->isFloatingPointTy() && valTy->isIntegerTy())
                            raw = ctx.builder.CreateSIToFP(raw, slotTy, "default_itofp");
                        else if (slotTy->isIntegerTy() && valTy->isIntegerTy())
                            raw = ctx.builder.CreateSExtOrTrunc(raw, slotTy, "default_int");
                        else if (slotTy->isIntegerTy() && valTy->isFloatingPointTy())
                            raw = coerceFloatToIntegral(ctx, raw, slotTy, mt);
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
            for (auto* cb : tr->catchBodies()) scanStmt(cb, counts);
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
                    "`static_off: [\"str_cmp\"]` to this def to bypass. (spec/49 §16)");
            } else {
                LOG_WARNING(ctx.fmtLoc(fn->sourceNode) + "variable '" + var +
                    "' is compared against a string literal in '" + fn->name +
                    "' — consider a regular expression for string matching. (spec/49 §16)");
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
    // Indirection depth of a declared type = number of leading pointer wrappers.
    // spec/61: ref<T> and shared<T> are POINTERS at the ABI (a reference/handle to T) — the SAME
    // indirection level as ptr<T>. Counting only `ptr<` made a ref<T>/shared<T> parameter look like
    // depth 0, so a ptr<T> argument (depth 1) or an `&value` (depth 1; for a memV3 refcount value-
    // model `&x` LOADS the stored %M*, not the slot address) passed to a ref<T> parameter falsely
    // tripped "over-indirected" — all correct at codegen (the ref ABI IS a pointer; coerceArgs /
    // visitUnary reconcile ptr↔ref and &value↔ref). Counting ref</shared< equally removes those
    // false positives while REAL over-indirection still exceeds the param depth and warns:
    // `&ptr`/`&ref` (address of a pointer → depth 2) and `ptr<ptr<T>>`/`ref<ptr<T>>` (depth 2) → ref<T>
    // (depth 1). A by-value `T` parameter stays depth 0, so a ptr<T> passed to it still warns.
    auto ptrDepth = [](const std::string& t) {
        int d = 0; size_t p = 0;
        for (;;) {
            if      (t.compare(p, 4, "ptr<")    == 0) { d++; p += 4; }
            else if (t.compare(p, 4, "ref<")    == 0) { d++; p += 4; }
            else if (t.compare(p, 7, "shared<") == 0) { d++; p += 7; }
            else break;
        }
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
    // spec/21 §6.2 F21-S2: the price of `singleton: true`.  An anchor declared
    // this way may be READ and used, but may not go to a
    // ptr<T> parameter: there the callee owns it, its drop
    // frees, and the anchor points into nothing (two WASM crashes in cosmos).
    std::set<std::string> singletonGlobals;
    for (const auto& d : prog->declarations)
        if (auto* dec = dynamic_cast<DeclStmt*>(d.get()))
            if (dec->is_singleton) singletonGlobals.insert(dec->varName);
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
            for (auto* cb : tr->catchBodies()) collectDecls(cb, vt);
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
            // F21-S2 first: an anchor is not in `vt` (those are the
            // LOCAL types), its depth would be UNKNOWN and the check
            // below would skip it.
            if (!singletonGlobals.empty() && haveSig && i < sig->second.size()) {
                if (auto* gvar = dynamic_cast<VariableExpr*>(args[i].get())) {
                    if (singletonGlobals.count(gvar->name) &&
                        sig->second[i].rfind("ptr<", 0) == 0) {
                        LOG_ERROR(ctx.fmtLoc(loc) + "call to '" + fname + "': argument " +
                            std::to_string(i + 1) + " is the singleton global '" + gvar->name +
                            "' and the parameter is " + sig->second[i] + " — an owning parameter "
                            "MOVES it out and the anchor dangles (spec/21 §6.2 F21-S2).  Pass a "
                            "ref<T>, or drop `singleton: true` and own it somewhere.");
                    }
                }
            }
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
                        "Suppress with `static_off: [\"ptr_indirection\"]`. (spec/49 §19)");
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
                        "`static_off: [\"ptr_indirection\"]`. (spec/49 §19)");
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
            for (auto* cb : tr->catchBodies()) scanStmt(cb, vt);
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
        else if (auto* tr = dynamic_cast<TryStmt*>(s)) { collectDecls(tr->tryBlock.get(), vt); for (auto* cb : tr->catchBodies()) collectDecls(cb, vt); }
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
        else if (auto* tr = dynamic_cast<TryStmt*>(s)) { scanStmt(tr->tryBlock.get(), selfId, encClass, vt); for (auto* cb : tr->catchBodies()) scanStmt(cb, selfId, encClass, vt); }
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
                "suppress with `static_off: [\"frame_blocking\"]`. (spec/49 §20)");
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
    // Nearest enclosing statement line.  A CallExpr carries `sourceLine` only
    // when it IS the statement (`- call:`); inside an `if` condition or a
    // `decl:` initializer it carries nothing, and fmtLoc would fall back to
    // `currentLine` — the end of the file in a post-parse pass.  The walks are
    // depth-first and synchronous, so a single cursor updated at each statement
    // yields the right line at every call site.
    int curStmtLine = -1;
    auto locOf = [&](CallExpr* c) -> std::string {
        int ln = (c && c->sourceLine > 0) ? c->sourceLine : curStmtLine;
        return ctx.fmtLoc(c ? c->sourceNode : YAML::Node(), ln);
    };
    // Tier 4 (spec/44_8): which instance methods are called on instances of a
    // prefab at all -- EVERY one, not only the child methods.  A1 needs
    // set_input_transparent as an escape hatch, F44_8-5 the pointing methods, F44_8-6 the
    // reading methods.  A child name that is not a literal makes the read analysis
    // incomplete and switches F44_8-6 off for this prefab.
    auto noteInstMethod = [&](const std::string& prefabName,
                              const std::string& method, CallExpr* call) {
        if (prefabName.empty() || !call) return;
        ctx.prefabInstMethods[prefabName].insert(method);
        if (call->args.empty()) return;
        auto* arg0 = dynamic_cast<LiteralExpr*>(call->args[0].get());
        if (!arg0) { ctx.prefabDynamicChild.insert(prefabName); return; }
        if (method == "set_child_visible" && arg0->type == "string") {
            std::string childName = arg0->value;
            if (childName.size() >= 2 && childName.front() == '"' && childName.back() == '"')
                childName = childName.substr(1, childName.size() - 2);
            ctx.prefabVisibleManaged[prefabName].insert(childName);
        }
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
                            "I\t" + pname + "\t" + locOf(call));
                    if (!ctx.prefabDecls.count(pname)) {
                        LOG_ERROR(locOf(call) +
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
        if (s->sourceLine > 0) curStmtLine = s->sourceLine;
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
            for (auto* cb : tr->catchBodies()) scanStmt(cb);
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
    // ALL methods that send their first parameter through the runtime `find_child`
    // (cross-checked against src/sys/prefab_runtime.cpp, 2026-10-15).  Before,
    // 6 of 30 were listed here — the other 24 had exactly the same error class,
    // just without a check: `set_bg_color("stat_dot")` on a mistyped name
    // throws at runtime (WASM: program abort), `is_checked("hakenx")` silently returns
    // 0.  ONE list for Tier 2 and Tier 2b, so that two halves do not drift
    // apart again.
    const std::set<std::string> childMethods = {
        // writing — a wrong name THROWS
        "set_text", "set_texture", "set_child_visible", "set_child_font_size",
        "set_dropdown_options", "set_dropdown_index", "set_dropdown_selected",
        "set_compact", "set_bg_color", "set_border_color", "set_color",
        "set_color_runs", "clear_color_runs", "set_checked", "set_focus",
        "set_bar_value", "set_bar_max", "set_bar_fill",
        "set_slider_value", "set_slider_min", "set_slider_max",
        "focus", "select_all", "scroll_to_end", "bind_list",
        // reading — a wrong name silently returns a default value
        "child_clicked", "text_value", "child_focused", "is_checked",
        "dropdown_value", "dropdown_index", "dropdown_enum",
        "get_slider_value", "selection", "container_row_count"
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
                // Tier 4: record first, then the narrower tier 2 check.
                if (method.find('.') == std::string::npos && bound.count(base))
                    noteInstMethod(bound[base], method, call);
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
                                locOf(call));
                        const auto& kids = ctx.prefabDecls[bound[base]];
                        if (!kids.count(child)) {
                            LOG_ERROR(locOf(call) +
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
        if (s->sourceLine > 0) curStmtLine = s->sourceLine;
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
            for (auto* cbody : tr->catchBodies()) { BoundMap cb = bound; t2Stmt(cbody, cb); }
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

    // ── Tier 2b: member-held instances + reading methods (spec/44_8) ─────
    // Tier 2 above only sees a scene in a LOCAL variable.  No screen is built
    // that way: it instantiates once in `init`, keeps the instance in a class
    // member and addresses children from every other method — so the entire
    // production shape sat outside the check (found 2026-10-11, closed
    // 2026-10-15, after a stale `<.scene.set_texture("logo_img")` aborted the
    // cosmos WASM client in the first frame: exceptions are off there,
    // the throw from find_child becomes a program abort).
    //
    // Second half of the same gap sits in `childMethods` above: the READING
    // methods.  A typo in child_clicked is a silently dead button and one in
    // text_value a silently empty input field — no throw, no message, because a
    // missing child legitimately reads as "not clicked" / "empty".
    // Generic call walker: invoke `cb` on every CallExpr in a statement tree.
    std::function<void(Expr*, const std::function<void(CallExpr*)>&)> walkCallsE;
    std::function<void(Stmt*, const std::function<void(CallExpr*)>&)> walkCallsS;
    walkCallsE = [&](Expr* e, const std::function<void(CallExpr*)>& cb) {
        if (!e) return;
        if (auto* call = dynamic_cast<CallExpr*>(e)) {
            cb(call);
            for (auto& a : call->args) walkCallsE(a.get(), cb);
        } else if (auto* bin = dynamic_cast<BinaryExpr*>(e)) {
            walkCallsE(bin->lhs.get(), cb); walkCallsE(bin->rhs.get(), cb);
        } else if (auto* un = dynamic_cast<UnaryExpr*>(e)) {
            walkCallsE(un->operand.get(), cb);
        } else if (auto* interp = dynamic_cast<StringInterpolationExpr*>(e)) {
            for (auto& p : interp->parts) walkCallsE(p.get(), cb);
        } else if (auto* ne = dynamic_cast<NewExpr*>(e)) {
            for (auto& kv : ne->initValues) walkCallsE(kv.second.get(), cb);
        } else if (auto* na = dynamic_cast<NewArrayExpr*>(e)) {
            walkCallsE(na->countExpr.get(), cb);
        } else if (auto* sp = dynamic_cast<SpawnExpr*>(e)) {
            for (auto& a : sp->args) walkCallsE(a.get(), cb);
        } else if (auto* at = dynamic_cast<AtExpr*>(e)) {
            walkCallsE(at->container.get(), cb);
            for (auto& i : at->indices) walkCallsE(i.get(), cb);
        } else if (auto* bl = dynamic_cast<BufferLiteralExpr*>(e)) {
            for (auto& el : bl->elements) walkCallsE(el.get(), cb);
        } else if (auto* dl = dynamic_cast<DictLiteralExpr*>(e)) {
            for (auto& kv : dl->entriesExpr) walkCallsE(kv.second.get(), cb);
        }
    };
    walkCallsS = [&](Stmt* s, const std::function<void(CallExpr*)>& cb) {
        if (!s) return;
        if (s->sourceLine > 0) curStmtLine = s->sourceLine;
        if (auto* b = dynamic_cast<BlockStmt*>(s)) {
            for (auto& st : b->statements) walkCallsS(st.get(), cb);
        } else if (auto* d = dynamic_cast<DeclStmt*>(s)) {
            walkCallsE(d->initialValue.get(), cb);
        } else if (auto* st = dynamic_cast<SetStmt*>(s)) {
            walkCallsE(st->value.get(), cb);
        } else if (auto* so = dynamic_cast<StoreStmt*>(s)) {
            walkCallsE(so->value.get(), cb);
        } else if (auto* ix = dynamic_cast<IdxStmt*>(s)) {
            for (auto& i : ix->indices) walkCallsE(i.get(), cb);
            walkCallsE(ix->value.get(), cb);
        } else if (auto* es = dynamic_cast<ExprStmt*>(s)) {
            walkCallsE(es->expr.get(), cb);
        } else if (auto* iff = dynamic_cast<IfStmt*>(s)) {
            walkCallsE(iff->condition.get(), cb);
            walkCallsS(iff->thenBranch.get(), cb);
            walkCallsS(iff->elseBranch.get(), cb);
        } else if (auto* wh = dynamic_cast<WhileStmt*>(s)) {
            walkCallsE(wh->condition.get(), cb);
            walkCallsS(wh->body.get(), cb);
        } else if (auto* rs = dynamic_cast<RetStmt*>(s)) {
            walkCallsE(rs->value.get(), cb);
        } else if (auto* th = dynamic_cast<ThrowStmt*>(s)) {
            walkCallsE(th->value.get(), cb);
        } else if (auto* tr = dynamic_cast<TryStmt*>(s)) {
            walkCallsS(tr->tryBlock.get(), cb);
            for (auto* cbody : tr->catchBodies()) walkCallsS(cbody, cb);
        }
    };
    // Collect `set: [<.field, engine.instantiate("X")]` bindings in a stmt tree.
    std::function<void(Stmt*, const std::function<void(const std::string&, const std::string&)>&)> walkFieldBinds;
    walkFieldBinds = [&](Stmt* s, const std::function<void(const std::string&, const std::string&)>& cb) {
        if (!s) return;
        if (auto* b = dynamic_cast<BlockStmt*>(s)) {
            for (auto& st : b->statements) walkFieldBinds(st.get(), cb);
        } else if (auto* st = dynamic_cast<SetStmt*>(s)) {
            const std::string& t = st->target;
            if (t.size() > 2 && t[0] == '<' && t[1] == '.' &&
                t.find('.', 2) == std::string::npos) {
                std::string pn;
                if (asInstantiate(st->value.get(), pn)) cb(t.substr(2), pn);
            }
        } else if (auto* iff = dynamic_cast<IfStmt*>(s)) {
            walkFieldBinds(iff->thenBranch.get(), cb);
            walkFieldBinds(iff->elseBranch.get(), cb);
        } else if (auto* wh = dynamic_cast<WhileStmt*>(s)) {
            walkFieldBinds(wh->body.get(), cb);
        } else if (auto* tr = dynamic_cast<TryStmt*>(s)) {
            walkFieldBinds(tr->tryBlock.get(), cb);
            for (auto* cbody : tr->catchBodies()) walkFieldBinds(cbody, cb);
        }
    };
    for (const auto& d : prog->declarations) {
        auto* cd = dynamic_cast<ClassDecl*>(d.get());
        if (!cd) continue;
        // Pass 1 — field → prefab, over ALL methods of the class: the binding
        // lives in `init`, the uses live in `render`.  A field bound to two
        // DIFFERENT prefabs is not statically decidable, so it is dropped from
        // tracking entirely — a check that guesses reports falsely and gets
        // switched off.
        std::map<std::string, std::string> fieldPrefab;
        std::set<std::string> ambiguous;
        auto note = [&](const std::string& field, const std::string& pname) {
            auto it = fieldPrefab.find(field);
            if (it == fieldPrefab.end()) fieldPrefab[field] = pname;
            else if (it->second != pname) ambiguous.insert(field);
        };
        for (const auto& m : cd->methods)
            if (m && m->body) walkFieldBinds(m->body.get(), note);
        if (fieldPrefab.empty()) continue;
        // Pass 2 — verify every `<.field.<child-method>("<child>", …)`.
        for (const auto& m : cd->methods) {
            if (!m || !m->body) continue;
            walkCallsS(m->body.get(), [&](CallExpr* call) {
                const std::string& fn = call->functionName;
                if (fn.size() <= 2 || fn[0] != '<' || fn[1] != '.') return;
                size_t ld = fn.rfind('.');
                if (ld == std::string::npos || ld <= 2) return;
                std::string field  = fn.substr(2, ld - 2);
                std::string method = fn.substr(ld + 1);
                // No chained receivers: `<.state.menu.scene.set_text` names a
                // field of ANOTHER object, whose binding lives in another class.
                if (field.find('.') != std::string::npos) return;
                // Tier 4: record here too, BEFORE the child method filter --
                // set_input_transparent and is_hovered are not child methods.
                if (!ambiguous.count(field)) {
                    auto fpIt = fieldPrefab.find(field);
                    if (fpIt != fieldPrefab.end())
                        noteInstMethod(fpIt->second, method, call);
                }
                if (!childMethods.count(method)) return;
                if (ambiguous.count(field)) return;
                auto fit = fieldPrefab.find(field);
                if (fit == fieldPrefab.end() || call->args.empty()) return;
                auto* lit = dynamic_cast<LiteralExpr*>(call->args[0].get());
                if (!lit || lit->type != "string") return;
                std::string child = unquote(lit->value);
                const auto& kids = ctx.prefabDecls[fit->second];
                if (kids.count(child)) return;
                LOG_ERROR(locOf(call) +
                    "'<." + field + "'." + method + "(\"" + child + "\") addresses an "
                    "unknown child of prefab '" + fit->second + "' — no child with that "
                    "name is declared in its `children:` list. Fix the child name or add "
                    "the child to the prefab declaration.");
            });
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
    // See spec/44_8_prefab_static.md (prefab report).
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
            } else if (auto* tr = dynamic_cast<TryStmt*>(s)) { collInstS(tr->tryBlock.get(),out); for (auto* cb : tr->catchBodies()) collInstS(cb,out); }
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
                for (auto* cb : tr->catchBodies()) bwStmt(cb);
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

// round4 §1: which contract a descriptor is being built for. Existing = wire/serialize + scope-drop
// (unchanged, also the OFF path). Clone = the memV3 in-memory deep-clone walker, which needs its own
// leaf semantics for `ref<T>` (WEAK_REF, redirected in a second phase) and `tensor` (TENSOR, cloned
// via aiml_tensor_clone). Clone descriptors are cached/emitted under distinct global names so a
// serialize of the same type never picks up a clone descriptor.
enum class DescriptorPurpose { Existing, Clone };

// Forward declarations for the §5.1 recursive-type registration emitted in __aiml_init (both are
// file-static, defined further down after Compiler::compile).
static std::vector<AimlTypeNode> serBuildSubtree(const std::string& typeName, int32_t offset,
                                                 CompilerContext& ctx, const llvm::DataLayout& dl,
                                                 DescriptorPurpose purpose = DescriptorPurpose::Existing);
static void serEmitGlobal(const std::string& name, const std::vector<AimlTypeNode>& nodes,
                          CompilerContext& ctx, CompilerContext::ClassInfo& classInfo);
// round4 (forward-ref fix): rebuild a type's serNodes after the registration fixpoint (defined below).
static void rebuildSerNodesFor(CompilerContext& ctx, const std::string& typeName);

// spec/21 §6.2 rule 5: a module-wide global is a SMELL, not an error.
// State belongs in a class or a group instance; a global has no
// owner.  An OWNING global (ptr<T>) is the worst case: if it is passed to
// a ptr<T> parameter, ownership moves away and every other reader
// holds a dead pointer (measured in cosmos, two WASM crashes 2026-10-13/14).
//
// ONE function for BOTH registration sites: two copies of the same check
// drift apart (that is how the memV3 double-gate bug came about), and a half
// that stays silent is worse than no check.
static void lintModuleGlobal(CompilerContext& ctx, const DeclStmt* decl) {
    if (!decl) return;
    const char* lintEnv = getenv("AIML_LINT_GLOBALS");
    if (lintEnv && (lintEnv[0] == '0' || lintEnv[0] == 'n' || lintEnv[0] == 'f')) return;
    // A CONSTANT is not state (user 2026-10-14): never assigned, never
    // passed on, owns nothing — a named literal.  Warning about it
    // would punish naming and reward a bare number.
    // Non-const stays reported: a writable primitive is shared
    // mutable state.
    static const std::set<std::string> kPrimitive = {
        "i8","i16","i32","i64","u8","u16","u32","u64",
        "f32","f64","bool","string","char"
    };
    if (decl->is_const && kPrimitive.count(decl->typeName)) return;
    // spec/21 §6.2 F21-S1: a declared anchor.  `engine.run` takes free
    // functions — without a module global there would be no way to the state.  The
    // silence is paid for: F21-S2 forbids passing it to a ptr<T> parameter
    // (checked in the whole-program pass).
    if (decl->is_singleton) return;
    bool owning = decl->typeName.rfind("ptr<", 0) == 0;
    LOG_WARNING(ctx.fmtLoc(decl->sourceNode, decl->sourceLine, decl->sourceColumn) +
        "module-level global '" + decl->varName + "' ("
        + decl->typeName + ")" + (owning
            ? " OWNS its value: passing it to a ptr<T> parameter moves it out and every"
              " other reader is left with a dangling pointer"
            : " has no owner")
        + " — state belongs in a class or a classgroup instance (spec/21 §6.2);"
          " set AIML_LINT_GLOBALS=0 to silence");
}

void Compiler::compile(Program* prog) {
    if (!prog) return;

    // spec/61 — the heap-lifetime feature (scope-drops + Increment C member deep-copy + P2a
    // return-cleanup + producer return-ownership + the §7.0.1 conditional-push dominance rule) is
    // DEFAULT OFF while it hardens against real-world aliasing.  A trial default-ON flip on
    // 2026-09-11 ran the full 9rooms suite drops-on and still hit REAL failures (NOT flakiness,
    // reproduced standalone): teardown double-frees (test_ceremony_cost, test_mkp_math,
    // test_conditional_durations "free(): double free"), a SEGV (test_activate_ability), and a
    // behavioral regression (test_creature_costs "sac1 nicht im Friedhof" [sac1 not in the graveyard] — a card dropped before
    // spec/61 lifetime drops are now the DEFAULT (paired with memV3 as the standard model,
    // decision 2026-09-21). Opt OUT for pre-feature comparison with AIML_LIFETIME_DROPS=0.
    ctx.lifetimeDropsEnabled = true;
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

    // spec/44_5 §7.1: rpc method parameters must be serializable values.
    for (auto& [cls, entries] : ctx.rpcMethodsByClass)
        for (auto& e : entries)
            for (auto& prm : e.params)
                if (!SerializeImpl::isSerializable(ctx, prm.second))
                    LOG_ERROR("class '" + cls + "': rpc method '" + e.method + "' parameter '" + prm.first
                              + "' has non-serializable type '" + prm.second + "' (spec/44_5 §7.1)");

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
    checkGroupInterfaces(prog);   // spec/58 §12.4 GI-1..4
    checkClassGroups(prog);
    synthesizeClassGroups(prog);

    // service (spec/66 §5): validated here, where models and classes are registered.
    // The synthesis itself already ran before registerTypes (compiler.cpp).
    checkServices(prog);

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
            ft_, llvm::Function::ExternalLinkage, initName, ctx.module());
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
            // §5 storage-location invariance: memV3 container global = handle pointer slot (see the
            // main-mode block below). Keep lib-mode and main-mode representations identical so a
            // library-defined container global and its main-mode extern view share one ABI.
            // Compute this BEFORE the `!ty` skip: a container type name whose element enum is defined
            // in a not-yet-resolved import (map<CardType:i32> — CardType from a sibling) makes
            // resolveType() return null, but the global is STILL a ptr handle. Skipping it here left
            // it to be created lazily with the inline struct type (function-body ref) → Phase-2 then
            // set a ptr-null init on a struct-typed global → LLVM verify "initializer type does not
            // match" (mkp_tables' 7 enum-keyed map tables). Always materialise the ptr handle global.
            bool memV3HandleGlobal = ctx.memV3 && ctx.isMemV3ContainerHandle(decl->typeName);
            IRType* ty = ctx.ir_gen->resolveType(decl->typeName);
            if (!ty && !memV3HandleGlobal) continue;
            llvm::Type* llvmTy = memV3HandleGlobal
                ? llvm::PointerType::get(ctx.context, 0)
                : static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ty));
            std::string symName = decl->is_extern
                ? "__lib_" + decl->varName
                : "__lib_" + filePrefix + decl->varName;
            auto linkage = decl->is_extern
                ? llvm::GlobalValue::WeakAnyLinkage
                : llvm::GlobalValue::ExternalLinkage;
            llvm::GlobalVariable* gv = ctx.module().getGlobalVariable(symName);
            // A prior lazy reference may have created this global with the WRONG value type (inline
            // container struct instead of the memV3 ptr handle). Recreate it as the ptr handle so its
            // type and null initializer agree — else module verification rejects the mismatch.
            if (gv && gv->getValueType() != llvmTy) {
                std::string tmpName = symName + ".mismatch";
                gv->setName(tmpName);
                auto* fixed = new llvm::GlobalVariable(
                    ctx.module(), llvmTy, /*isConstant=*/false, linkage,
                    llvm::Constant::getNullValue(llvmTy), symName);
                gv->replaceAllUsesWith(
                    llvm::ConstantExpr::getBitCast(fixed, gv->getType()));
                gv->eraseFromParent();
                gv = fixed;
            }
            if (gv) {
                if (!gv->hasInitializer())
                    gv->setInitializer(llvm::Constant::getNullValue(llvmTy));
                gv->setLinkage(linkage);
            } else {
                gv = new llvm::GlobalVariable(
                    ctx.module(), llvmTy, /*isConstant=*/false,
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
            lintModuleGlobal(ctx, decl);
            if (memV3HandleGlobal) sym.is_byref_arg = true;
            ctx.symbol_table[decl->varName] = sym;

            // Collect container globals (list/map/chan/buffer) AND class-
            // instance globals (their members may be containers and need
            // recursive init) so Phase 2 can prep them before user-code runs.
            // SKIP extern decls: the owning file initialises them.
            const std::string& tn = decl->typeName;
            bool isContainer = (tn.rfind("list<",   0) == 0 ||
                                tn.rfind("map<",    0) == 0 ||
                                tn.rfind("chan<",   0) == 0 ||
                                HashmapImpl::isHashmapType(tn) ||
                                tn == "buffer" ||   // bare buffer is a Class-3 handle too (§5)
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
            // §5 storage-location invariance: a memV3 container global is a handle. Allocate the
            // heap handle, store the pointer into the (pointer-typed) global, and init THROUGH the
            // handle — exactly the local-handle prologue. Non-memV3 keeps the inline-struct global.
            bool memV3HandleGlobal = ctx.memV3 && ctx.isMemV3ContainerHandle(tn);
            // A memV3 container global WITH an initializer gets its handle from that initializer
            // (visitDeclStmt → emitListHandleDecl writes the global var), so Phase-2 must NOT also
            // allocate one here (that handle would leak, overwritten by the initializer's store).
            if (memV3HandleGlobal && decl->initialValue) continue;
            auto handleInitTgt = [&](const char* newFn) -> llvm::Value* {
                if (!memV3HandleGlobal) return static_cast<llvm::Value*>(gv);
                llvm::Function* nw = ctx.getFunction(newFn, ctx.builder.getInt8Ty()->getPointerTo(), {});
                llvm::Value* hnd = ctx.builder.CreateCall(nw, {}, "gcont_h");
                ctx.builder.CreateStore(hnd, gv);
                return hnd;
            };
            if (tn.rfind("list<", 0) == 0) {
                std::string elemName = tn.substr(5, tn.length() - 6);
                llvm::Type* elemTy = static_cast<llvm::Type*>(
                    ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
                if (elemTy) ListImpl::initList(ctx, handleInitTgt("aiml_list_handle_new"), elemTy, 4);
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
                    if (kTy && vTy) MapImpl::initMap(ctx, handleInitTgt("aiml_map_handle_new"), kTy, vTy, 4);
                }
            } else if (tn.rfind("chan<", 0) == 0) {
                ChanImpl::init(ctx, gv, ctx.builder.getInt64(0));
            } else if (HashmapImpl::isHashmapType(tn)) {
                HashmapImpl::initSlot(ctx, gv, tn);
            } else if (tn == "buffer" || tn.rfind("buffer<", 0) == 0) {
                // A memV3 buffer global (bare `buffer` OR `buffer<T>`) needs its handle allocated so
                // the pointer slot is non-null before any .alloc/.write. The bare `buffer` had no
                // Phase-2 init in the OFF path (data alloc'd on demand); under memV3 the HANDLE still
                // must exist. `buffer<T>` keeps the eager 4096-byte data pre-alloc.
                llvm::Value* bufTgt = handleInitTgt("aiml_buffer_handle_new");
                if (tn.rfind("buffer<", 0) == 0 && !decl->initialValue)
                    BufferImpl::alloc(ctx, bufTgt, ctx.builder.getInt32(4096));
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
            llvm::FunctionCallee fc = ctx.module().getOrInsertFunction(libInitName, ft);
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
            // spec/61 §5 "storage location is representation-invariant": under memV3 a Class-3
            // container global is a HANDLE, exactly like a local — the global variable holds a
            // handle pointer (not an inline {size,cap,data} struct). Declare it as a pointer slot
            // and mark the symbol is_byref_arg so every access derefs the handle (like a local).
            // Computed BEFORE the `!ty` skip: an enum-keyed container (map<CardType:i32>) whose enum
            // is a not-yet-resolved import returns null from resolveType but is still a ptr handle.
            bool memV3HandleGlobal = ctx.memV3 && ctx.isMemV3ContainerHandle(decl->typeName);
            IRType* ty = ctx.ir_gen->resolveType(decl->typeName);
            if (!ty && !memV3HandleGlobal) continue;
            llvm::Type* llvmTy = memV3HandleGlobal
                ? llvm::PointerType::get(ctx.context, 0)
                : static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ty));
            std::string symName = decl->is_extern
                ? "__lib_" + decl->varName
                : "__global_" + decl->varName;
            // spec/29_2 §22: remember the source text, so that sys.bundle can send
            // along a global that is used.
            if (decl->sourceNode && ctx.globalSources.find(decl->varName) == ctx.globalSources.end())
                ctx.globalSources[decl->varName] = aiml::dumpPreservingQuotes(decl->sourceNode);
            auto linkage = decl->is_extern
                ? llvm::GlobalValue::WeakAnyLinkage
                : llvm::GlobalValue::ExternalLinkage;
            llvm::GlobalVariable* gv = ctx.module().getGlobalVariable(symName);
            // A prior lazy reference may have created this global with the inline container struct
            // type instead of the memV3 ptr handle. Recreate it as ptr so type + null init agree.
            if (gv && gv->getValueType() != llvmTy) {
                gv->setName(symName + ".mismatch");
                auto* fixed = new llvm::GlobalVariable(
                    ctx.module(), llvmTy, /*isConstant=*/false, linkage,
                    llvm::Constant::getNullValue(llvmTy), symName);
                gv->replaceAllUsesWith(llvm::ConstantExpr::getBitCast(fixed, gv->getType()));
                gv->eraseFromParent();
                gv = fixed;
            }
            if (gv) {
                if (!gv->hasInitializer())
                    gv->setInitializer(llvm::Constant::getNullValue(llvmTy));
                gv->setLinkage(linkage);
            } else {
                gv = new llvm::GlobalVariable(
                    ctx.module(), llvmTy, /*isConstant=*/false,
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
            lintModuleGlobal(ctx, decl);
            // memV3 container global: slot holds the handle pointer → route all access through the
            // same by-ref deref path as local handles (§5 storage-location invariance).
            if (memV3HandleGlobal) sym.is_byref_arg = true;
            ctx.symbol_table[decl->varName] = sym;

            const std::string& tn = decl->typeName;
            bool isContainer = (tn.rfind("list<",   0) == 0 ||
                                tn.rfind("map<",    0) == 0 ||
                                tn.rfind("chan<",   0) == 0 ||
                                HashmapImpl::isHashmapType(tn) ||
                                tn == "buffer" ||   // bare buffer is a Class-3 handle too (§5)
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
            // §5 storage-location invariance: a memV3 container global is a handle. Allocate the
            // heap handle, store the pointer into the (pointer-typed) global, and init THROUGH the
            // handle — exactly the local-handle prologue. Non-memV3 keeps the inline-struct global.
            bool memV3HandleGlobal = ctx.memV3 && ctx.isMemV3ContainerHandle(tn);
            // A memV3 container global WITH an initializer gets its handle from that initializer
            // (visitDeclStmt → emitListHandleDecl writes the global var), so Phase-2 must NOT also
            // allocate one here (that handle would leak, overwritten by the initializer's store).
            if (memV3HandleGlobal && decl->initialValue) continue;
            auto handleInitTgt = [&](const char* newFn) -> llvm::Value* {
                if (!memV3HandleGlobal) return static_cast<llvm::Value*>(gv);
                llvm::Function* nw = ctx.getFunction(newFn, ctx.builder.getInt8Ty()->getPointerTo(), {});
                llvm::Value* hnd = ctx.builder.CreateCall(nw, {}, "gcont_h");
                ctx.builder.CreateStore(hnd, gv);
                return hnd;
            };
            if (tn.rfind("list<", 0) == 0) {
                std::string elemName = tn.substr(5, tn.length() - 6);
                llvm::Type* elemTy = static_cast<llvm::Type*>(
                    ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
                if (elemTy) ListImpl::initList(ctx, handleInitTgt("aiml_list_handle_new"), elemTy, 4);
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
                    if (kTy && vTy) MapImpl::initMap(ctx, handleInitTgt("aiml_map_handle_new"), kTy, vTy, 4);
                }
            } else if (tn.rfind("chan<", 0) == 0) {
                ChanImpl::init(ctx, gv, ctx.builder.getInt64(0));
            } else if (HashmapImpl::isHashmapType(tn)) {
                HashmapImpl::initSlot(ctx, gv, tn);
            } else if (tn == "buffer" || tn.rfind("buffer<", 0) == 0) {
                // A memV3 buffer global (bare `buffer` OR `buffer<T>`) needs its handle allocated so
                // the pointer slot is non-null before any .alloc/.write. The bare `buffer` had no
                // Phase-2 init in the OFF path (data alloc'd on demand); under memV3 the HANDLE still
                // must exist. `buffer<T>` keeps the eager 4096-byte data pre-alloc.
                llvm::Value* bufTgt = handleInitTgt("aiml_buffer_handle_new");
                if (tn.rfind("buffer<", 0) == 0 && !decl->initialValue)
                    BufferImpl::alloc(ctx, bufTgt, ctx.builder.getInt32(4096));
            } else if (ctx.classes.count(tn) && !ctx.classes[tn].isBuiltin &&
                       ctx.classes[tn].type != nullptr) {
                initClassContainerMembers(gv, tn);
            }
        }

        // Visit all declarations in file order (preserving execution order).
        // visitDeclStmt will detect is_global=true and skip alloca creation,
        // only emitting the initializer at the correct point in the sequence.
        // Top-level style check: OPT-IN via env var `AIML_STRICT_TOPLEVEL`.
        // Set (any truthy value) → top-level imperative statements
        // are compile errors.  Default (env unset or "0") → no check.
        //
        // Background: top-level imperatives run in __aiml_init and
        // are subject to the decl hoisting problem (decl with a call initializer
        // fires before imperative statements).  In `main()` the order is
        // deterministic.  `main` is called automatically by the C entry point.
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
            // Allowed are import/class/def/decl/extern_block/model.
            if (auto* stmt = dynamic_cast<Stmt*>(decl.get())) {
                if (auto* dst = dynamic_cast<DeclStmt*>(stmt)) {
                    if (dst->initialValue && dynamic_cast<CallExpr*>(dst->initialValue.get())) {
                        LOG_ERROR(ctx.fmtLoc(dst->sourceNode) + "top-level decl '" + dst->varName +
                            "' has a call() initializer — it is hoisted and runs BEFORE " +
                            "imperative top-level statements.  Move it into main().");
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

        // `main` is called automatically by generateEntryPoint() in compiler.cpp
        // (renamed → __aiml_user_main, then from the C entry point).
        // An explicit `- call: [main]` at top level would call main TWICE.
        if (strict && hasExplicitMainCall) {
            LOG_ERROR(ctx.fmtLoc() + "explicit `- call: [main]` at top-level causes a double-call: "
                      "`main` is already invoked by the C entry-point.  Remove the explicit call.");
        }

        // spec/61 §5.1 (memV3): register each recursive type's descriptor (referenced by an
        // AIML_TD_PTR_REF) under its runtime type-id, so the clone/free walkers can follow the
        // recursive edge. Runs in __aiml_init (insert point is still __aiml_init here). Builds a
        // standalone descriptor for any referenced type that lacks one.
        if (ctx.memV3 && !ctx.modelTypeIds.empty()) {
            auto* bbInit = static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
            if (bbInit && !bbInit->getTerminator()) {
                auto* i32 = ctx.builder.getInt32Ty();
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::DataLayout dl(&ctx.module());
                llvm::Function* regF = ctx.getFunction("aiml_ser_register_type",
                    ctx.builder.getVoidTy(), {i32, i8p, i32, i32});
                for (auto& [tname, tid] : ctx.modelTypeIds) {
                    auto ci = ctx.classes.find(tname);
                    if (ci == ctx.classes.end() || !ci->second.type) continue;
                    if (!ci->second.serNodesVar) {
                        std::vector<AimlTypeNode> nodes = serBuildSubtree(tname, 0, ctx, dl);
                        serEmitGlobal(tname, nodes, ctx, ci->second);
                    }
                    if (!ci->second.serNodesVar) continue;
                    int32_t sz = (int32_t)dl.getTypeAllocSize(ci->second.type);
                    ctx.builder.CreateCall(regF, {
                        ctx.builder.getInt32(tid),
                        static_cast<llvm::Value*>(ci->second.serNodesVar),
                        ctx.builder.getInt32(ci->second.serNodesCount),
                        ctx.builder.getInt32(sz) });
                }
            }
        }

        // Finish __aiml_init
        IRBlock* currentBlock = ctx.ir_gen->getInsertBlock();
        if (!static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(currentBlock))->getTerminator()) {
            ctx.ir_gen->createRet(nullptr);
        }

        // ── Static check: same_arg ─────────────────────────────────────────
        // 3+ free functions that carry the same pair (name, type) somewhere in
        // their arg list are a strong signal that they should be methods
        // on a class.  Can be switched off per function via
        // `static_off: ["same_arg"]`.  Class methods are NOT
        // checked — they are already organised by class.
        {
            std::map<std::string, std::vector<FunctionDecl*>> byArg;
            for (const auto& d : prog->declarations) {
                auto* fn = dynamic_cast<FunctionDecl*>(d.get());
                if (!fn) continue;
                // HTTP route handlers have a shape fixed by srv.route —
                // (http_request_handle, http_response_handle) — and must be
                // free functions (route takes a function, not a method), so
                // "make them methods" is not actionable.  Every server with
                // 3+ routes would otherwise need static_off.  Only the exact
                // handler signature is exempt; helpers that take a request or
                // response plus other args are still grouped.
                if (fn->args.size() == 2 &&
                    fn->args[0].second == "http_request_handle" &&
                    fn->args[1].second == "http_response_handle") continue;
                // One entry per arg pair in this function.  Dedup per
                // function: if the same function has the pair several times
                // (which should not really happen), count it only once.
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
                LOG_ERROR(ctx.fmtLoc(cls->sourceNode, cls->sourceLine, cls->sourceColumn) + "class '" + cls->name + "': has `rules:` but no `renders:` "
                          "(a rule-bundle must name the cascade|enum it renders).");
            for (const auto& r : cls->rules)
                if (!r->def)
                    LOG_ERROR(ctx.fmtLoc(cls->sourceNode, cls->sourceLine, cls->sourceColumn) + "class '" + cls->name + "': a rule has no `def:` handler.");
            if (cls->renders.empty()) continue;
            if (!boundTargetExists(cls->renders, /*wantEnum=*/false) &&
                !boundTargetExists(cls->renders, /*wantEnum=*/true))
                LOG_ERROR(ctx.fmtLoc(cls->sourceNode, cls->sourceLine, cls->sourceColumn) + "class '" + cls->name + "': renders '" + cls->renders +
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

        // Dedup identical diagnostics across the many enumerated paths.
        std::set<std::string> emitted;
        auto emit = [&](const std::string& msg) {
            if (emitted.insert(msg).second) LOG_ERROR(msg);
        };
        // spec/57 §3a: one carrier type, passed by reference; handlers mutate it and return nothing.
        if (pool.carrier != pool.taker)
            emit("cascade_class '" + pool.name + "': carrier '" + pool.carrier + "' and taker '" +
                 pool.taker + "' must be the same type (spec/57 §3a).");
        // A container carrier is already a shared handle → taken as the plain container type.
        const std::string refCarrier = ctx.isMemV3ContainerHandle(pool.carrier)
            ? pool.carrier : "ref<" + pool.carrier + ">";
        for (auto* r : rules) {
            if (!r || !r->def) continue;
            bool refArg = !r->def->args.empty() && r->def->args[0].second == refCarrier;
            bool noRet = r->def->returnType.empty() || r->def->returnType == "void";
            if (!refArg || !noRet)
                emit("cascade_class '" + pool.name + "': handler '" + r->def->name + "' must take [carry, " +
                     refCarrier + "] and return nothing — mutate the carrier in place (spec/57 §3a).");
        }
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

// spec/44_5 §7.1 — desugar a class's `rpc:` methods (parsed snippets → ordinary AST, ordinary
// calling conventions):  the user body becomes `<m>_rpc_body`; a same-signature `<m>` asks the
// runtime whether to route the call (mirror.object_call) and otherwise runs the body locally; a free
// `rpc_thunk_<C>_<m>(obj, args)` is what the receiver runs (straight into the body, never the
// wrapper → no re-send); `RpcArgs_<C>_<m>` carries the parameters.  Runs before registerTypes.
static std::unique_ptr<Node> parseDeclSnippet(const std::string& yamlText) {
    AIML::AST::Builder b;
    std::istringstream ss(yamlText);
    YAML::Parser parser(ss);
    if (!parser.HandleNextDocument(b)) return nullptr;
    auto prog = b.takeProgram();
    if (!prog || prog->declarations.empty()) return nullptr;
    return std::move(prog->declarations.front());
}

void Compiler::synthesizeRpcMethods(Program* prog) {
    if (!prog) return;
    auto q = [](const std::string& t) { return "\"" + t + "\""; };
    std::vector<std::unique_ptr<Node>> toAppend;
    for (auto& d : prog->declarations) {
        if (auto* md = dynamic_cast<ModelDecl*>(d.get())) {
            if (!md->rpcMethods.empty())
                LOG_ERROR(ctx.fmtLoc(md->sourceNode) + "model '" + md->name + "' declares rpc: — models have no "
                          "methods; declare rpc: on a class (spec/44_5 §7.1)");
            continue;
        }
        auto* cd = dynamic_cast<ClassDecl*>(d.get());
        if (!cd || cd->rpcMethods.empty()) continue;
        std::set<std::string> seen;
        for (const std::string& mname : cd->rpcMethods) {
            if (!seen.insert(mname).second) continue;
            FunctionDecl* f = nullptr;
            for (auto& m : cd->methods) if (m && m->name == mname) { f = m.get(); break; }
            if (!f) {
                LOG_ERROR(ctx.fmtLoc(cd->sourceNode) + "class '" + cd->name + "': rpc method '" + mname
                          + "' is not a method of this class (declare rpc: on the class that defines it, spec/44_5 §7.1)");
                continue;
            }
            if (!f->returnType.empty() && f->returnType != "void") {
                LOG_ERROR(ctx.fmtLoc(cd->sourceNode) + "class '" + cd->name + "': rpc method '" + mname
                          + "' returns '" + f->returnType + "' — rpc methods must return void; use "
                          "send_and_await/reply_rpc for results (spec/44_5 §7.1)");
                continue;
            }
            const std::string body  = mname + "_rpc_body";
            const std::string model = "RpcArgs_" + cd->name + "_" + mname;
            const std::string thunk = "rpc_thunk_" + cd->name + "_" + mname;

            // argument model
            std::string my = "- model:\n    name: " + model + "\n    fields:\n";
            if (f->args.empty()) my += "      - [rpc_unused, i32, 0]\n";
            for (auto& a : f->args) my += "      - [" + a.first + ", " + q(a.second) + "]\n";
            auto modelNode = parseDeclSnippet(my);

            // routing wrapper (same signature; args/argMods copied verbatim afterwards)
            std::string callArgs;
            for (auto& a : f->args) callArgs += ", " + a.first;
            std::string wy = "- def:\n    name: " + mname + "\n    return: void\n"
                             "    static_off: [\"same_arg\", \"str_cmp\", \"lines\"]\n    body:\n"
                             "      - decl: [rpcargs, " + model + "]\n";
            for (auto& a : f->args) wy += "      - set: [rpcargs." + a.first + ", " + a.first + "]\n";
            wy += "      - if:\n          condition: { eq: [{ call: [mirror.object_call, <, " + q(mname) + ", { '&': [rpcargs] }] }, 0] }\n"
                  "          then:\n            - call: [<." + body + callArgs + "]\n";
            auto wrapper = parseDefSnippet(wy);

            // receive thunk: straight into the body
            std::string ta;
            for (auto& a : f->args) ta += ", rpcargs." + a.first;
            std::string ty = "- def:\n    name: " + thunk + "\n    return: void\n"
                             "    static_off: [\"same_arg\", \"str_cmp\", \"lines\"]\n"
                             "    args: [[rpcobj, " + q("ref<" + cd->name + ">") + "], [rpcargs, " + q("ref<" + model + ">") + "]]\n"
                             "    body:\n      - call: [rpcobj." + body + ta + "]\n";
            auto thunkNode = parseDefSnippet(ty);

            if (!modelNode || !wrapper || !thunkNode) {
                LOG_ERROR(ctx.fmtLoc(cd->sourceNode) + "class '" + cd->name + "': could not synthesize rpc method '" + mname + "'");
                continue;
            }
            wrapper->args    = f->args;
            wrapper->argMods = f->argMods;
            f->name = body;
            cd->methods.push_back(std::move(wrapper));
            toAppend.push_back(std::move(modelNode));
            toAppend.push_back(std::move(thunkNode));
            auto& entries = ctx.rpcMethodsByClass[cd->name];
            bool known = false;
            for (auto& e : entries) if (e.method == mname) known = true;
            if (!known) entries.push_back({mname, model, thunk, f->args});
        }
    }
    // args models must be registered before the classes that use them
    for (auto it = toAppend.rbegin(); it != toAppend.rend(); ++it)
        if (dynamic_cast<ModelDecl*>(it->get())) prog->declarations.insert(prog->declarations.begin(), std::move(*it));
    for (auto& n : toAppend) if (n) prog->declarations.push_back(std::move(n));
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
                body += "            - call: [<." + handlerMethod(opRule) + ", carry]\n";
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
                        body += "            - call: [<." + subM + ", carry]\n";
                    } else {                                // flat enum/alias field
                        RuleDecl* fr = resolve(f.typeName, curPrefix, after);
                        if (!fr) { ok = false; return dm; }
                        body += "            - call: [<." + handlerMethod(fr) + ", carry]\n";
                        body += "            - set: [<.idx, { +: [<.idx, 1] }]\n";
                    }
                    curPrefix.push_back(f.typeName);
                }
                body += "            - return: []\n";
            }
            dispatchDefs.push_back(
                "- def:\n    name: " + dm + "\n    args: [[carry, " +
                (ctx.isMemV3ContainerHandle(pool.carrier) ? pool.carrier : "ref<" + pool.carrier + ">") +
                "]]\n    body:\n" + body);
            return dm;
        };

        std::string topDispatch = genDispatch(casc, {});
        if (!ok) continue;   // gaps/ties already reported by the semantic pass

        // spec/57 §3a: render clones the entry carrier into an owned local, runs the dispatch on it
        // by reference and returns that local as a fresh `producer` taker (the caller's carrier is
        // never mutated; spec/61 §3.2 — the caller deep-frees the taker at scope exit).
        std::string renderText =
            "- def:\n    name: render\n    args: [[carry, " + pool.carrier +
            "], [block, list<i32>]]\n    return: { type: " + pool.taker + ", lifetime: producer }"
            "\n    body:\n      - set: [<.blk, block]\n      - set: [<.idx, 0]\n"
            "      - decl: [acc, " + pool.carrier + ", { clone: carry }]\n"
            "      - call: [<." + topDispatch + ", acc]\n"
            "      - return: acc\n";
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

// ── spec/66 — services ───────────────────────────────────────────────────────

// The `:name` parameters of an HTTP path, in order.
static std::vector<std::string> servicePathParams(const std::string& path) {
    std::vector<std::string> params;
    size_t pos = 0;
    while (pos < path.size()) {
        size_t colon = path.find(':', pos);
        if (colon == std::string::npos) break;
        size_t end = path.find('/', colon);
        if (end == std::string::npos) end = path.size();
        if (end > colon + 1) params.push_back(path.substr(colon + 1, end - colon - 1));
        pos = end;
    }
    return params;
}

// spec/66 §5 — what a service declaration must satisfy before anything is
// synthesized.  Every check here exists because the alternative is a silent
// hole: an endpoint nobody can reach, a route served twice, or — the expensive
// one — an endpoint without a role check.
// spec/66 §3 — desugar a `- service:` into ordinary declarations, and do it BEFORE
// type registration so the results are known like any hand-written type: each
// endpoint body becomes the free function `<Service>_<endpoint>` (one body, every
// transport reaches it), the declared shape becomes the reflection table
// `<Service>__describe` (§7), and the service name becomes a facade class a program
// can hold.  The checks run AFTER registration, so the metadata they need is captured
// here while the endpoint def is still in hand.
void Compiler::synthesizeServices(Program* prog) {
    if (!prog) return;
    std::vector<ServiceDecl*> services;
    for (auto& d : prog->declarations)
        if (auto* sv = dynamic_cast<ServiceDecl*>(d.get())) services.push_back(sv);
    if (services.empty()) return;

    auto q = [](const std::string& t) { return "\"" + t + "\""; };
    std::vector<std::unique_ptr<Node>> models;     // must be registered before their users
    std::vector<std::unique_ptr<Node>> toAppend;

    // The description row (§7).  Once per compilation unit — an imported service
    // may already have brought it.
    bool haveRow = ctx.classes.count("ServiceEndpoint") > 0;
    for (auto& d : prog->declarations)
        if (auto* md = dynamic_cast<ModelDecl*>(d.get()))
            if (md->name == "ServiceEndpoint") haveRow = true;
    if (!haveRow) {
        // `roles` carries NO default: a `[]` default on a container field breaks the
        // field declared after it.
        std::string my = "- model:\n    name: ServiceEndpoint\n    fields:\n"
                         "      - [name, string, \"\"]\n"
                         "      - [verb, string, \"\"]\n"
                         "      - [path, string, \"\"]\n"
                         "      - [request_type, string, \"\"]\n"
                         "      - [response_type, string, \"\"]\n"
                         "      - [roles, \"list<string>\"]\n";
        if (auto m = parseDeclSnippet(my)) models.push_back(std::move(m));
        else LOG_ERROR("internal: could not synthesize model 'ServiceEndpoint' (spec/66 §7).");
    }

    // §8 — one error shape for every transport.
    bool haveErr = ctx.classes.count("ServiceError") > 0;
    for (auto& d : prog->declarations)
        if (auto* md = dynamic_cast<ModelDecl*>(d.get()))
            if (md->name == "ServiceError") haveErr = true;
    if (!haveErr) {
        std::string ey = "- model:\n    name: ServiceError\n    fields:\n"
                         "      - [code, i32, 0]\n"
                         "      - [message, string, \"\"]\n"
                         "      - [detail, string, \"\"]\n";
        if (auto m = parseDeclSnippet(ey)) models.push_back(std::move(m));
        else LOG_ERROR("internal: could not synthesize model 'ServiceError' (spec/66 §8).");
    }

    // §8 — the two exceptions that are mapped to 404/400.  `Exception` is not
    // built into aiml (spec/22), so we create the root if the project
    // has none; an existing one is NOT replaced.
    auto knownType = [&](const std::string& tn) {
        if (ctx.classes.count(tn)) return true;
        for (auto& d : prog->declarations) {
            if (auto* cd = dynamic_cast<ClassDecl*>(d.get())) if (cd->name == tn) return true;
            if (auto* md = dynamic_cast<ModelDecl*>(d.get())) if (md->name == tn) return true;
        }
        return false;
    };
    if (!knownType("Exception")) {
        if (auto m = parseDeclSnippet("- class:\n    name: Exception\n    members:\n"
                                      "      - [message, string]\n"))
            models.push_back(std::move(m));
    }
    for (const char* ex : {"ServiceNotFound", "ServiceInvalid"}) {
        if (knownType(ex)) continue;
        if (auto m = parseDeclSnippet(std::string("- class:\n    name: ") + ex +
                                      "\n    extends: Exception\n"))
            models.push_back(std::move(m));
        else LOG_ERROR(std::string("internal: could not synthesize exception '") + ex + "' (spec/66 §8).");
    }

    typedef std::vector<std::pair<std::string, std::string>> FieldList;
    auto modelFields = [&](const std::string& tn) -> FieldList {
        FieldList fields;
        for (auto& d : prog->declarations)
            if (auto* md = dynamic_cast<ModelDecl*>(d.get()))
                if (md->name == tn) {
                    for (const auto& fld : md->fields)
                        fields.emplace_back(fld.name, fld.type);
                    return fields;
                }
        auto it = ctx.classes.find(tn);
        if (it != ctx.classes.end())
            for (const auto& mem : it->second.members)
                fields.emplace_back(mem.first, mem.second.typeName);   // ClassInfo-Member: typeName
        return fields;
    };

    for (auto* sv : services) {
        const bool guarded = !sv->auth.empty() && sv->auth != "none";
        bool rpcTransport = sv->transports.empty();      // nothing declared = rpc (§4.1)
        bool restTransport = false;
        bool cliTransport  = false;
        for (const auto& t : sv->transports) {
            if (t == "rpc")  rpcTransport  = true;
            if (t == "rest") restTransport = true;
            if (t == "cli")  cliTransport  = true;
        }
        // The project OWNS the auth object; the facade only borrows it (spec/61).
        const std::string gateType  = "ref<" + sv->auth + ">";
        const std::string gateGlobal = "service_gate_" + sv->name;
        std::string dispatchBody;                        // one branch per endpoint
        std::string clientMethods;                       // §3: one per endpoint
        std::string restRoutes;                          // §4.2: serve_rest body
        std::string cliBranches;                         // §4.3: run_cli body

        std::string describe = "- def:\n    name: " + sv->name + "__describe\n"
                               "    return: \"list<ServiceEndpoint>\"\n"
                               "    static_off: [\"str_cmp\", \"same_arg\", \"lines\"]\n"
                               "    body:\n"
                               "      - decl: [rows, \"list<ServiceEndpoint>\"]\n";
        int rowNo = 0;
        for (auto& ep : sv->endpoints) {
            if (!ep.fn) continue;
            FunctionDecl* f = ep.fn.get();

            ep.name        = f->name;
            ep.verb        = f->http.size() > 0 ? f->http[0] : std::string();
            ep.path        = f->http.size() > 1 ? f->http[1] : std::string();
            ep.roles       = f->roles;
            ep.cli         = f->cli;
            ep.hasRoles    = f->hasRoles;
            ep.argCount    = (int)f->args.size();
            ep.requestType = f->args.empty() ? std::string() : f->args[0].second;
            ep.returnType  = f->returnType;
            ep.captured    = true;

            const std::string row  = "row" + std::to_string(rowNo++);
            const std::string full = ep.path.empty() ? std::string() : sv->base + ep.path;
            describe += "      - decl: [" + row + ", ServiceEndpoint]\n";
            describe += "      - set: [" + row + ".name, "          + q(ep.name)        + "]\n";
            describe += "      - set: [" + row + ".verb, "          + q(ep.verb)        + "]\n";
            describe += "      - set: [" + row + ".path, "          + q(full)           + "]\n";
            describe += "      - set: [" + row + ".request_type, "  + q(ep.requestType) + "]\n";
            describe += "      - set: [" + row + ".response_type, " + q(ep.returnType)  + "]\n";
            for (const auto& r : ep.roles)
                describe += "      - call: [" + row + ".roles.push, " + q(r) + "]\n";
            describe += "      - call: [rows.push, " + row + "]\n";

            const std::string bodyName = sv->name + "_" + ep.name;
            f->name = bodyName;
            toAppend.push_back(std::unique_ptr<Node>(ep.fn.release()));

            if (rpcTransport) {

                // §4.1 — decode the typed payload, run the gate, call the body, answer.
                // A payload that does not decode is answered 400, never dropped.
                const std::string rpcName = bodyName + "_rpc";
                std::string h = "- def:\n    name: " + rpcName + "\n"
                    "    args: [[evt, rpc_event], [sess, mirror_session]" +
                    (guarded ? ", [gate, " + q(gateType) + "]" : std::string()) + "]\n"
                    "    return: i32\n    static_off: [\"str_cmp\", \"same_arg\", \"lines\"]\n"
                    "    body:\n";
                if (guarded) {
                    // An unset gate must not read as an open door (§6).
                    h += "      - if:\n          condition: { peq: [gate, null] }\n          then:\n"
                         "            - decl: [nogate, ServiceError]\n"
                         "            - set: [nogate.code, 401]\n"
                         "            - set: [nogate.message, \"service gate not set\"]\n"
                         "            - call: [sess.reply_rpc, evt, \"set_error\", 401, nogate]\n"
                         "            - return: 1\n";
                }
                h += "      - decl: [req, " + ep.requestType + "]\n"
                     "      - decl: [dec_rc, i32, { call: [sess.decode_rpc, evt, req] }]\n"
                     "      - if:\n          condition: { neq: [dec_rc, 0] }\n          then:\n"
                     "            - decl: [bad, ServiceError]\n"
                     "            - set: [bad.code, 400]\n"
                     "            - set: [bad.message, \"payload is not a " + ep.requestType + "\"]\n"
                     "            - call: [sess.reply_rpc, evt, \"set_error\", 400, bad]\n"
                     "            - return: 1\n";
                if (guarded) {
                    int roleNo = 0;
                    for (const auto& r : ep.roles) {
                        const std::string ok = "role_ok_" + std::to_string(roleNo++);
                        h += "      - decl: [" + ok + ", i32, { call: [gate.has_role, "
                             "{ call: [gate.principal_of, req.token] }, " + q(r) + "] }]\n"
                             "      - if:\n          condition: { eq: [" + ok + ", 0] }\n          then:\n"
                             "            - decl: [denied, ServiceError]\n"
                             "            - set: [denied.code, 403]\n"
                             "            - set: [denied.message, \"role " + r + " required\"]\n"
                             "            - call: [sess.reply_rpc, evt, \"set_error\", 403, denied]\n"
                             "            - return: 1\n";
                    }
                }
                h += "      - decl: [resp, " + ep.returnType + ", { call: [" + bodyName + ", req] }]\n"
                     "      - call: [sess.reply_rpc, evt, \"set_result\", 0, resp]\n"
                     "      - return: 1\n";
                if (auto hf = parseDefSnippet(h)) { hf->serviceSynth = true; toAppend.push_back(std::move(hf)); }
                else LOG_ERROR(ctx.fmtLoc(sv->sourceNode, sv->sourceLine, sv->sourceColumn) + "service '" + sv->name + "': internal — could not synthesize '" +
                               rpcName + "'.");

                // Indentation: the body sits in a method of the facade.
                dispatchBody += "            - if:\n                condition: { eq: [evt.method, " + q(ep.name) + "] }\n"
                                "                then:\n                  - return: { call: [" + rpcName +
                                ", evt, sess" + (guarded ? std::string(", <.auth") : std::string()) + "] }\n";
            }

            if (restTransport && !ep.verb.empty()) {
                // §4.2 — source order: JSON body, then path parameters.  A
                // later source never overwrites an earlier one, which is why the
                // path only fills what the body left empty.
                const std::string httpName = bodyName + "_http";
                std::string ht = "- def:\n    name: " + httpName + "\n"
                    "    args: [[htreq, http_request_handle], [htresp, http_response_handle]]\n"
                    "    static_off: [\"str_cmp\", \"same_arg\", \"lines\"]\n    body:\n"
                    "      - decl: [plain, JsonStyle]\n"
                    "      - set: [plain.bare, 1]\n"
                    "      - call: [htresp.headers.put, \"Content-Type\", \"application/json\"]\n"
                    "      - decl: [raw_body, string]\n"
                    "      - set: [raw_body, htreq.body]\n"
                    "      - decl: [model, " + ep.requestType + "]\n"
                    "      - if:\n          condition: { gt: [raw_body.len, 0] }\n          then:\n"
                    "            - set: [model, { call: [sys.deserialize, raw_body, " +
                    q(ep.requestType) + "] }]\n";
                const auto params = servicePathParams(ep.path);
                if (!params.empty()) {
                    ht += "      - decl: [path_vals, map<string:string>]\n"
                          "      - set: [path_vals, htreq.params]\n";
                    for (const auto& pn : params)
                        ht += "      - if:\n          condition: { eq: [model." + pn + ", \"\"] }\n"
                              "          then:\n            - set: [model." + pn +
                              ", { call: [path_vals.get, " + q(pn) + "] }]\n";
                }
                // §8 — an error response is a serialised ServiceError, not
                // hand-written JSON: that way the message is escaped correctly.
                auto httpFail = [&](const std::string& indent, int code,
                                    const std::string& msgExpr, const std::string& varName) {
                    return indent + "- decl: [" + varName + ", ServiceError]\n" +
                           indent + "- set: [" + varName + ".code, " + std::to_string(code) + "]\n" +
                           indent + "- set: [" + varName + ".message, " + msgExpr + "]\n" +
                           indent + "- set: [htresp.status, " + std::to_string(code) + "]\n" +
                           indent + "- set: [htresp.body, { call: [sys.serialize, " + varName + ", plain] }]\n";
                };
                if (guarded) {
                    // Token: first the field (body/path), otherwise the Authorization header.
                    ht += "      - if:\n          condition: { eq: [model.token, \"\"] }\n          then:\n"
                          "            - decl: [head_val, string]\n"
                          "            - set: [head_val, { call: [htreq.header, \"Authorization\"] }]\n"
                          "            - if:\n                condition: { gt: [head_val.len, 7] }\n"
                          "                then:\n                  - set: [model.token, "
                          "{ call: [head_val.substr, 7] }]\n"
                          "            - if:\n                condition: { eq: [head_val.len, 0] }\n"
                          "                then:\n" +
                          httpFail("                  ", 401, "\"no token\"", "no_token") +
                          "                  - call: [htresp.send]\n"
                          "                  - { return }\n";
                    ht += "      - if:\n          condition: { peq: [" + gateGlobal + ", null] }\n          then:\n" +
                          httpFail("            ", 401, "\"service gate not set\"", "no_gate") +
                          "            - call: [htresp.send]\n"
                          "            - { return }\n";
                    int roleNo = 0;
                    for (const auto& r : ep.roles) {
                        const std::string ok = "http_role_ok_" + std::to_string(roleNo++);
                        ht += "      - decl: [" + ok + ", i32, { call: [" + gateGlobal + ".has_role, "
                              "{ call: [" + gateGlobal + ".principal_of, model.token] }, " + q(r) + "] }]\n"
                              "      - if:\n          condition: { eq: [" + ok + ", 0] }\n          then:\n" +
                              httpFail("            ", 403, q("role " + r + " required"), "denied_" + std::to_string(roleNo)) +
                              "            - call: [htresp.send]\n"
                              "            - { return }\n";
                    }
                }
                ht += "      - decl: [answer, " + ep.returnType + "]\n"
                      "      - try:\n          body:\n"
                      "            - set: [answer, { call: [" + bodyName + ", model] }]\n"
                      "            - set: [htresp.status, 200]\n"
                      "            - set: [htresp.body, { call: [sys.serialize, answer, plain] }]\n"
                      "          catch:\n"
                      "            - type: ServiceNotFound\n              var: miss\n              body:\n" +
                      httpFail("                ", 404, "miss.message", "err_404") +
                      "            - type: ServiceInvalid\n              var: wrong\n              body:\n" +
                      httpFail("                ", 400, "wrong.message", "err_400") +
                      "            - type: Exception\n              var: other\n              body:\n"
                      "                - call: [log.error, \"service " + sv->name + "." + ep.name +
                      ": ${other.message}\"]\n" +
                      httpFail("                ", 500, "\"internal error\"", "err_500") +
                      "      - call: [htresp.send]\n";
                if (auto hf = parseDefSnippet(ht)) { hf->serviceSynth = true; toAppend.push_back(std::move(hf)); }
                else LOG_ERROR(ctx.fmtLoc(sv->sourceNode, sv->sourceLine, sv->sourceColumn) + "service '" + sv->name + "': internal — could not synthesize '" +
                               httpName + "'.");

                std::string verb = ep.verb;
                for (auto& ch : verb) ch = (char)toupper((unsigned char)ch);
                restRoutes += "            - call: [server.route, " + q(verb) + ", " +
                              q(sv->base + ep.path) + ", " + httpName + "]\n";
            }

            if (cliTransport) {
                // The body of the subcommand is created in synthesizeServiceBodies —
                // it needs the FIELDS of the request model, and an imported
                // model is only in the class table after registerTypes.
                const std::string sub = ep.cli.empty() ? ep.name : ep.cli.front();
                cliBranches += "            - if:\n                condition: { eq: [sub, " + q(sub) + "] }\n"
                               "                then:\n                  - return: { call: [" + bodyName +
                               "_cli, args] }\n";
            }

            // §3 client stub: the same method, only via send_and_await.  The
            // response goes into a ptr held by the caller (spec/44_5).
            clientMethods += "      - def:\n          name: " + ep.name + "\n"
                "          args: [[sess, mirror_session], [req, " + ep.requestType +
                "], [out, " + q("ref<" + ep.returnType + ">") + "]]\n"   // buffer belongs to the caller
                "          return: i32\n"
                "          static_off: [\"str_cmp\", \"same_arg\", \"lines\"]\n"
                "          body:\n            - return: { call: [sess.send_and_await, 0, " +
                q(ep.name) + ", req, out, <.timeout_ms] }\n";

        }
        describe += "      - return: rows\n";
        if (auto df = parseDefSnippet(describe)) { df->serviceSynth = true; toAppend.push_back(std::move(df)); }
        else LOG_ERROR(ctx.fmtLoc(sv->sourceNode, sv->sourceLine, sv->sourceColumn) + "service '" + sv->name + "': internal — could not synthesize '__describe'.");

        // Facade (§3): the thing a server holds.  It carries the gate the project
        // hands in and answers `dispatch` with 1 when the method was ours, 0 when it
        // was not — so generated and hand-written handlers can share one loop.
        // `serve_rest` arrives with the REST transport (§10 M4).
        std::string fy = "- class:\n    name: " + sv->name + "\n    members:\n"
                         "      - [endpoint_count, i32, " + std::to_string(rowNo) + "]\n";
        if (guarded) fy += "      - [auth, " + q(gateType) + "]\n";
        if (guarded || rpcTransport || !restRoutes.empty() || !cliBranches.empty()) {
            fy += "    methods:\n";
            if (guarded)
                fy += "      - def:\n          name: set_auth\n"
                      "          args: [[gate, " + q(gateType) + "]]\n"
                      "          body:\n            - set: [<.auth, gate]\n";
            if (!restRoutes.empty() && guarded)
                restRoutes = "            - set: [" + gateGlobal + ", <.auth]\n" + restRoutes;
            if (!cliBranches.empty())
                fy += "      - def:\n          name: run_cli\n"
                      "          args: [[args, " + q("list<string>") + "]]\n"
                      "          return: i32\n"
                      "          static_off: [\"str_cmp\", \"same_arg\", \"lines\"]\n"
                      "          body:\n"
                      "            - if: { condition: { eq: [args.len, 0] }, then: [{ return: 1 }] }\n"
                      "            - decl: [sub, string, { at: [args, 0] }]\n" + cliBranches +
                      "            - return: 1\n";
            if (!restRoutes.empty())
                fy += "      - def:\n          name: serve_rest\n"
                      "          args: [[server, http_server_handle]]\n"
                      "          static_off: [\"str_cmp\", \"same_arg\", \"lines\"]\n"
                      "          body:\n" + restRoutes;
            if (rpcTransport)
                fy += "      - def:\n          name: dispatch\n"
                      "          args: [[sess, mirror_session], [evt, rpc_event]]\n"
                      "          return: i32\n"
                      "          static_off: [\"str_cmp\", \"same_arg\", \"lines\"]\n"
                      "          body:\n" + dispatchBody +
                      "            - return: 0\n";
        }
        if (guarded && !restRoutes.empty()) {
            // A route handler is a function pointer without state; the gate
            // therefore lives in a module global that serve_rest fills (§6).
            std::string gy = "- decl: [" + gateGlobal + ", " + q(gateType) + ", null]\n";
            if (auto gd = parseDeclSnippet(gy)) models.push_back(std::move(gd));
            else LOG_ERROR(ctx.fmtLoc(sv->sourceNode, sv->sourceLine, sv->sourceColumn) + "service '" + sv->name + "': internal — could not synthesize the gate global.");
        }
        if (auto facade = parseDeclSnippet(fy)) {
            if (auto* fc = dynamic_cast<ClassDecl*>(facade.get()))
                for (auto& fm : fc->methods) if (fm) fm->serviceSynth = true;
            toAppend.push_back(std::move(facade));
        }
        else LOG_ERROR("service '" + sv->name + "': internal — could not synthesize the facade.");

        // §3 client facade.  A class of its own, so that a pure client (wasm, §4.4)
        // can hold it without the server part.
        if (!clientMethods.empty()) {
            std::string cy = "- class:\n    name: " + sv->name + "Client\n    members:\n"
                             "      - [timeout_ms, i32, 4000]\n    methods:\n"
                             "      - def:\n          name: set_timeout\n"
                             "          args: [[millis, i32]]\n"
                             "          body:\n            - set: [<.timeout_ms, millis]\n" +
                             clientMethods;
            if (auto cc = parseDeclSnippet(cy)) {
                if (auto* cd = dynamic_cast<ClassDecl*>(cc.get()))
                    for (auto& cm : cd->methods) if (cm) cm->serviceSynth = true;
                toAppend.push_back(std::move(cc));
            } else {
                LOG_ERROR("service '" + sv->name + "': internal — could not synthesize the client.");
            }
        }
        ctx.serviceDecls[sv->name] = ctx.fmtLoc(sv->sourceNode);   // §5.6
        LOG_INFO("synthesized service '" + sv->name + "' (" + std::to_string(rowNo) + " endpoints)");
    }

    for (auto it = models.rbegin(); it != models.rend(); ++it)
        prog->declarations.insert(prog->declarations.begin(), std::move(*it));
    for (auto& n : toAppend) prog->declarations.push_back(std::move(n));
}

// spec/66 §4.3 — second pass, AFTER registerTypes: here imported models are
// known too, and only here can a subcommand be filled from the FIELDS of the
// request model.  The facade (pass 1) already calls these functions.
void Compiler::synthesizeServiceBodies(Program* prog) {
    if (!prog) return;
    std::vector<ServiceDecl*> services;
    for (auto& d : prog->declarations)
        if (auto* sv = dynamic_cast<ServiceDecl*>(d.get())) services.push_back(sv);
    if (services.empty()) return;

    auto q = [](const std::string& t) { return "\"" + t + "\""; };
    typedef std::vector<std::pair<std::string, std::string>> FieldList;
    auto modelFields = [&](const std::string& tn) -> FieldList {
        FieldList fields;
        auto it = ctx.classes.find(tn);
        if (it == ctx.classes.end()) it = ctx.classes.find(ctx.qualify(tn));
        if (it != ctx.classes.end())
            for (const auto& mem : it->second.members)
                fields.emplace_back(mem.first, mem.second.typeName);
        return fields;
    };

    std::vector<std::unique_ptr<Node>> toAppend;
    for (auto* sv : services) {
        bool cliTransport = false;
        for (const auto& t : sv->transports) if (t == "cli") cliTransport = true;
        if (!cliTransport) continue;
        for (const auto& ep : sv->endpoints) {
            if (!ep.captured) continue;
            const std::string bodyName = sv->name + "_" + ep.name;
            {
                // §4.3 — subcommand: flags into the request model, the same body,
                // response as JSON on stdout.  The argument list is PASSED IN
                // (a JIT run has no argv at all, spec/23).
                const std::string cliName = bodyName + "_cli";
                std::string ct = "- def:\n    name: " + cliName + "\n"
                    "    args: [[args, " + q("list<string>") + "]]\n"
                    "    return: i32\n    static_off: [\"str_cmp\", \"same_arg\", \"lines\"]\n"
                    "    body:\n"
                    "      - decl: [model, " + ep.requestType + "]\n"
                    "      - decl: [idx, i32, 1]\n"
                    "      - while:\n          condition: { lt: [idx, args.len] }\n          body:\n"
                    "            - decl: [flag, string, { at: [args, idx] }]\n"
                    "            - decl: [val, string, \"\"]\n"
                    "            - decl: [step, i32, 1]\n"
                    "            - decl: [nxt, i32, { '+': [idx, 1] }]\n"
                    "            - if:\n                condition: { lt: [nxt, args.len] }\n                then:\n"
                    "                  - decl: [peek, string, { at: [args, nxt] }]\n"
                    "                  - if:\n                      condition: { neq: [{ call: [peek.starts_with, \"-\"] }, 1] }\n"
                    "                      then:\n                        - set: [val, peek]\n"
                    "                        - set: [step, 2]\n";
                for (const auto& fld : modelFields(ep.requestType)) {
                    std::string dashed = fld.first;
                    for (auto& ch : dashed) if (ch == '_') ch = '-';
                    std::string hit = "{ '||': [{ eq: [flag, " + q("--" + fld.first) + "] }, "
                                      "{ eq: [flag, " + q("--" + dashed) + "] }] }";
                    if (fld.second == "string") {
                        ct += "            - if:\n                condition: " + hit + "\n                then:\n"
                              "                  - set: [model." + fld.first + ", val]\n";
                    } else if (fld.second == "i32") {
                        ct += "            - if:\n                condition: " + hit + "\n                then:\n"
                              "                  - if:\n                      condition: { gt: [val.len, 0] }\n"
                              "                      then:\n                        - set: [model." + fld.first +
                              ", { call: [val.parse_i32] }]\n"
                              "                      else:\n                        - set: [model." + fld.first + ", 1]\n";
                    }
                    // other types: V1 does not fill them (spec/09 only knows parse_i32/parse_f32)
                }
                ct += "            - set: [idx, { '+': [idx, step] }]\n"
                      "      - decl: [answer, " + ep.returnType + ", { call: [" + bodyName + ", model] }]\n"
                      "      - decl: [plain, JsonStyle]\n"
                      "      - set: [plain.bare, 1]\n"
                      "      - decl: [out_json, string, { call: [sys.serialize, answer, plain] }]\n"
                      "      - call: [io.write, io.stdout, out_json]\n"
                      "      - call: [io.write, io.stdout, \"\\n\"]\n"
                      "      - return: 0\n";
                if (auto cf = parseDefSnippet(ct)) { cf->serviceSynth = true; toAppend.push_back(std::move(cf)); }
                else LOG_ERROR(ctx.fmtLoc(sv->sourceNode, sv->sourceLine, sv->sourceColumn) + "service '" + sv->name + "': internal — could not synthesize '" +
                               cliName + "'.");
            }
        }
    }
    // Insert at the FRONT: the facade (pass 1) calls these functions, and a
    // declaration after it is not visible to it.
    for (auto it = toAppend.rbegin(); it != toAppend.rend(); ++it)
        prog->declarations.insert(prog->declarations.begin(), std::move(*it));
}

void Compiler::checkServices(Program* prog) {
    if (!prog) return;
    std::vector<ServiceDecl*> services;
    for (const auto& d : prog->declarations)
        if (auto* sv = dynamic_cast<ServiceDecl*>(d.get())) services.push_back(sv);
    if (services.empty()) return;

    std::map<std::string, std::string> routeOwner;   // "VERB path" -> "Service.endpoint"

    for (auto* sv : services) {
        const bool guarded = !sv->auth.empty() && sv->auth != "none";

        // §5.5 — the auth class must exist and answer the §6 contract.
        if (guarded) {
            auto ac = ctx.classes.find(sv->auth);
            if (ac == ctx.classes.end()) {
                LOG_ERROR(ctx.fmtLoc() + "service '" + sv->name + "': auth class '" + sv->auth +
                          "' is not declared (spec/66 §6).  Name a class with 'principal_of' and "
                          "'has_role', or write 'auth: none' to declare the service open.");
            } else if (!ac->second.methods.empty()) {
                // Only judge the contract when the method table is actually populated:
                // a class registered from an IMPORTED file is known by name and members
                // long before its methods are compiled, and reporting "has no method"
                // there would be a false accusation (seen 2026-10-18).
                for (const char* needed : {"principal_of", "has_role"}) {
                    if (!ac->second.methods.count(needed))
                        LOG_ERROR(ctx.fmtLoc() + "service '" + sv->name + "': auth class '" + sv->auth +
                                  "' has no method '" + needed + "' (spec/66 §6).");
                }
            }
        }

        std::set<std::string> endpointNames;
        for (const auto& ep : sv->endpoints) {
            if (!ep.captured) continue;      // synthesis has not run on this service
            const std::string where = "service '" + sv->name + "', endpoint '" + ep.name + "': ";

            // §5.1a — one name per service: it is the RPC method and the operation id.
            if (!endpointNames.insert(ep.name).second)
                LOG_ERROR(ctx.fmtLoc() + where + "declared twice in this service (spec/66 §5.1).");

            // §5.4 — exactly one model in, one model out.  That is what makes one
            // body reachable from RPC, REST and CLI alike.
            if (ep.argCount != 1) {
                LOG_ERROR(ctx.fmtLoc() + where + "takes " + std::to_string(ep.argCount) +
                          " arguments; an endpoint takes exactly one request model (spec/66 §5.4).");
            } else if (!ctx.classes.count(ep.requestType)) {
                LOG_ERROR(ctx.fmtLoc() + where + "request type '" + ep.requestType +
                          "' is not a model; a scalar has no wire shape the transports share "
                          "(spec/66 §5.4).");
            }
            if (ep.returnType.empty() || !ctx.classes.count(ep.returnType))
                LOG_ERROR(ctx.fmtLoc() + where + "return type '" + ep.returnType +
                          "' is not a model (spec/66 §5.4).");

            // §6 — the gate reads `req.token`; without the field it could never run.
            if (guarded) {
                auto rq = ctx.classes.find(ep.requestType);
                if (rq != ctx.classes.end() && !rq->second.members.count("token"))
                    LOG_ERROR(ctx.fmtLoc() + where + "request model '" + ep.requestType +
                              "' has no 'token: string' field, but the service declares "
                              "'auth: " + sv->auth + "'.  That is where the RPC gate reads the "
                              "token from (spec/66 §6).");
            }

            // §5.3 — with a gate declared, every endpoint states its roles.
            if (guarded && !ep.hasRoles)
                LOG_ERROR(ctx.fmtLoc() + where + "has no 'roles:' while the service declares "
                          "'auth: " + sv->auth + "'.  Write the roles, or 'roles: []' for an endpoint "
                          "every authenticated caller may use (spec/66 §5.3).");

            if (ep.verb.empty() && ep.path.empty()) continue;
            std::string verb = ep.verb;
            for (auto& ch : verb) ch = (char)toupper((unsigned char)ch);
            const std::string full = sv->base + ep.path;

            // §5.1b — one owner per route, across every service of this build.
            const std::string routeKey = verb + " " + full;
            auto owner = routeOwner.find(routeKey);
            if (owner != routeOwner.end())
                LOG_ERROR(ctx.fmtLoc() + where + "route '" + routeKey + "' is already served by '" +
                          owner->second + "' (spec/66 §5.1).");
            else
                routeOwner[routeKey] = sv->name + "." + ep.name;

            // §5.2 — a `:param` the request model cannot carry would never be filled.
            auto reqIt = ctx.classes.find(ep.requestType);
            if (reqIt != ctx.classes.end())
                for (const auto& param : servicePathParams(ep.path))
                    if (!reqIt->second.members.count(param))
                        LOG_ERROR(ctx.fmtLoc() + where + "path parameter ':" + param +
                                  "' has no field '" + param + "' in request model '" +
                                  ep.requestType + "' (spec/66 §5.2).");
        }
    }
}

// ── spec/58 §12 group interfaces: name resolution helpers ───────────────────
// An interface / group-interface name as written, resolved against the
// registered tables: exact, then qualified in the current namespace, then in
// the namespace `ns` the referring declaration lives in.  "" = unknown.
std::string Compiler::resolveIfaceName(const std::string& raw, const std::string& ns) {
    if (ctx.interfaces.count(raw)) return raw;
    std::string q = ctx.qualify(raw);
    if (ctx.interfaces.count(q)) return q;
    if (!ns.empty() && ctx.interfaces.count(ns + "." + raw)) return ns + "." + raw;
    return "";
}

std::string Compiler::groupInterfaceOf(const std::string& typeName) {
    std::string t = typeName;
    if ((t.rfind("ptr<", 0) == 0 || t.rfind("ref<", 0) == 0) && t.back() == '>') t = t.substr(4, t.size() - 5);
    if (ctx.groupInterfaces.count(t)) return t;
    std::string q = ctx.qualify(t);
    if (ctx.groupInterfaces.count(q)) return q;
    return "";
}

// Does class `cls` (or a parent) implement interface `ifaceQ` (qualified)?
bool Compiler::classImplementsIface(const std::string& cls0, const std::string& ifaceQ) {
    std::string cls = cls0;
    std::set<std::string> seen;
    while (!cls.empty() && seen.insert(cls).second) {
        std::string key = ctx.classes.count(cls) ? cls : ctx.qualify(cls);
        auto it = ctx.classes.find(key);
        if (it == ctx.classes.end()) return false;
        for (const auto& raw : it->second.interfaces)
            if (raw == ifaceQ || ctx.qualify(raw) == ifaceQ || resolveIfaceName(raw, "") == ifaceQ) return true;
        cls = it->second.parentName;
    }
    return false;
}

// GI-1..4: the group interface declarations of this unit.
void Compiler::checkGroupInterfaces(Program* prog) {
    for (const auto& d : prog->declarations) {
        auto* gi = dynamic_cast<InterfaceDecl*>(d.get());
        if (!gi || !gi->groupInterface) continue;
        if (gi->groupExports.empty()) {
            LOG_ERROR(ctx.fmtLoc(gi->sourceNode) + "groupinterface '" + gi->name + "' exports nothing.");
            continue;
        }
        std::set<std::string> seen;
        for (const auto& ex : gi->groupExports) {
            if (!seen.insert(ex.first).second) {
                LOG_ERROR(ctx.fmtLoc(gi->sourceNode) + "groupinterface '" + gi->name + "': export '" +
                          ex.first + "' is listed twice.");
                continue;
            }
            if (!resolveIfaceName(ex.second, ctx.currentNamespace).empty()) continue;
            if (ctx.classes.count(ex.second) || ctx.classes.count(ctx.qualify(ex.second)))
                LOG_ERROR(ctx.fmtLoc(gi->sourceNode) + "groupinterface '" + gi->name + "': export '" +
                          ex.first + "' must be typed by an interface, '" + ex.second + "' is a class.");
            else
                LOG_ERROR(ctx.fmtLoc(gi->sourceNode) + "groupinterface '" + gi->name + "': export '" +
                          ex.first + "' names unknown interface '" + ex.second + "'.");
        }
    }
}

// GI-5..10: a classgroup's group-level `implements:`.
void Compiler::checkGroupImplements(ClassGroupDecl* g) {
    std::map<std::string, std::pair<std::string, std::string>> claimed;   // export → (iface, group iface)
    for (const auto& gname : g->implementsGroups) {
        std::string gq = groupInterfaceOf(gname);
        if (gq.empty()) {
            if (!resolveIfaceName(gname, "").empty())
                LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': '" + gname +
                          "' is an interface, not a group interface — instance interfaces go on the instance (spec/58 §5.2).");
            else
                LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': implements unknown group interface '" + gname + "'.");
            continue;
        }
        const auto& info = ctx.groupInterfaces[gq];
        std::set<std::string> once;
        for (const auto& ex : info.exports) {
            if (!once.insert(ex.first).second) continue;
            std::string iq = resolveIfaceName(ex.second, info.ns);
            if (iq.empty()) continue;   // GI-1/2, reported at the declaration
            auto cl = claimed.find(ex.first);
            if (cl != claimed.end()) {
                if (cl->second.first != iq)
                    LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "' implements '" + cl->second.second +
                              "' and '" + gname + "', which export '" + ex.first + "' as different interfaces ('" +
                              cl->second.first + "' vs '" + iq + "').");
                continue;
            }
            claimed[ex.first] = {iq, gname};
            const ClassGroupInstance* inst = nullptr;
            for (const auto& i : g->instances) if (i.instanceName == ex.first) inst = &i;
            if (!inst) {
                LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "' implements '" + gname +
                          "' but has no instance '" + ex.first + "' (" + gname + " exports " + ex.first + ": " +
                          ex.second + ").");
                continue;
            }
            if (!g->exports.empty() &&
                std::find(g->exports.begin(), g->exports.end(), ex.first) == g->exports.end())
                LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "' implements '" + gname +
                          "' but does not export '" + ex.first + "'.");
            bool viaInstance = false;
            for (const auto& ii : inst->implements)
                if (ii == ex.second || resolveIfaceName(ii, "") == iq) viaInstance = true;
            if (!viaInstance && !classImplementsIface(inst->className, iq))
                LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "' implements '" + gname + "': instance '" +
                          ex.first + "' (class " + inst->className + ") does not implement '" + ex.second + "'.");
        }
    }
}

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
                LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name +
                          "': an instance is missing `instance:`.");
            else if (!instNames.insert(inst.instanceName).second)
                LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name +
                          "': duplicate instance name '" + inst.instanceName + "'.");
            if (inst.className.empty())
                LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': instance '" +
                          inst.instanceName + "' is missing `class:`.");
            else if (!ctx.classes.count(inst.className) &&
                     !ctx.classes.count(ctx.qualify(inst.className)))
                LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': instance '" +
                          inst.instanceName + "' names unknown class '" + inst.className + "'.");
        }

        // subgroups: member name unique (shares the container namespace with
        // instances) + the referenced group type exists.
        for (auto& sg : g->subgroups) {
            if (!instNames.insert(sg.first).second)
                LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name +
                          "': subgroup name '" + sg.first +
                          "' clashes with another instance/subgroup.");
            if (!groupByName.count(sg.second))
                LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': subgroup '" +
                          sg.first + "' names unknown group '" + sg.second + "'.");
        }
        checkGroupImplements(g);   // spec/58 §12.4 GI-5..10
        // exports must name real instances of THIS group.
        for (auto& e : g->exports)
            if (!instNames.count(e))
                LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name +
                          "': export '" + e + "' is not an instance of this group.");

        // requires: PORT names (spec/58 §6) — unique across the instance/subgroup
        // namespace + the port's declared base class exists.  A `sees: [port]` edge
        // (pass 2) resolves against these.
        std::set<std::string> portNames;
        for (auto& p : g->ports) {
            if (p.first.empty())
                LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name +
                          "': a `requires` port is missing its name.");
            else if (instNames.count(p.first) || !portNames.insert(p.first).second)
                LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': port '" + p.first +
                          "' clashes with an instance/subgroup/port of the same name.");
            if (!p.second.empty() && !ctx.classes.count(p.second) &&
                !ctx.classes.count(ctx.qualify(p.second)))
                LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': port '" + p.first +
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
                        LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': subgroup '" + sg.first +
                                  "' binds unknown port '" + b.first + "' of group '" + sg.second + "'.");
                    // bound target is a real instance of THIS group?
                    std::string bc = instClass(b.second);
                    if (bc.empty())
                        LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': subgroup '" + sg.first +
                                  "' binds port '" + b.first + "' to '" + b.second +
                                  "' which is not an instance of this group.");
                    else if (!portClass.empty() && !isSubclassOf(bc, portClass))
                        LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': subgroup '" + sg.first +
                                  "' binds port '" + b.first + "' (needs '" + portClass + "') to '" +
                                  b.second + "' of incompatible class '" + bc + "'.");
                    if (portExists && !bound.insert(b.first).second)
                        LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': subgroup '" + sg.first +
                                  "' binds port '" + b.first + "' more than once.");
                }
            for (auto& cp : cg->ports)
                if (!bound.count(cp.first))
                    LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': subgroup '" + sg.first +
                              "' (group '" + sg.second + "') leaves port '" + cp.first + "' unbound.");
        }

        // pass 2: sees targets (exist / not self / not dup) + no override.
        for (auto& inst : g->instances) {
            std::set<std::string> seenSees;
            for (auto& s : inst.sees) {
                if (!seenSees.insert(s).second)
                    LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': instance '" +
                              inst.instanceName + "' lists sees '" + s + "' twice.");
                // Strip an optional `:Iface` suffix (interface-typed edge, §5.2).
                std::string tpart = s, iface;
                auto colon = tpart.find(':');
                if (colon != std::string::npos) { iface = tpart.substr(colon + 1); tpart = tpart.substr(0, colon); }
                if (!iface.empty() && !ctx.interfaces.count(iface) &&
                    !ctx.interfaces.count(ctx.qualify(iface)))
                    LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': instance '" +
                              inst.instanceName + "' sees '" + s +
                              "' names unknown interface '" + iface + "'.");
                auto dot = tpart.find('.');
                if (dot == std::string::npos) {
                    // Same-group sibling, or a declared PORT (child→parent injection).
                    if (tpart == inst.instanceName)
                        LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': instance '" +
                                  inst.instanceName + "' sees itself.");
                    else if (!instNames.count(tpart) && !portNames.count(tpart))
                        LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': instance '" +
                                  inst.instanceName + "' sees unknown instance '" + tpart + "'.");
                    continue;
                }
                // Cross-group `subgroup.instance` — must resolve to an EXPORTED
                // instance of an embedded subgroup (spec/58 §6 encapsulation).
                std::string sgName = tpart.substr(0, dot), tgt = tpart.substr(dot + 1);
                std::string sgType;
                for (auto& sg : g->subgroups) if (sg.first == sgName) sgType = sg.second;
                if (sgType.empty()) {
                    LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': instance '" +
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
                    LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': sees '" + s +
                              "' — group '" + sgType + "' has no instance '" + tgt + "'.");
                else if (!exported)
                    LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': sees '" + s +
                              "' — '" + tgt + "' is not exported by group '" + sgType +
                              "' (spec/58 §6).");
            }
            std::set<std::string> baseMethods, visited;
            collectMethods(inst.className, baseMethods, visited);
            for (auto& fn : inst.functions)
                if (baseMethods.count(fn->name))
                    LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': function '" +
                              fn->name + "' on instance '" + inst.instanceName +
                              "' overrides a base method of '" + inst.className +
                              "' — overriding is not allowed (spec/58 §7).");
            // A constructor names a base method run in phase 0 (sees-blind).
            if (!inst.constructor.empty() && !baseMethods.count(inst.constructor[0]))
                LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': instance '" +
                          inst.instanceName + "' constructor '" + inst.constructor[0] +
                          "' is not a method of base '" + inst.className + "'.");
            // Declared interfaces must exist (subclass conformance — that a base
            // method or group-function satisfies each — is enforced downstream).
            for (auto& itf : inst.implements)
                if (!ctx.interfaces.count(itf) && !ctx.interfaces.count(ctx.qualify(itf)))
                    LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': instance '" +
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

    // A group under `- namespace:` synthesizes its classes in that namespace: they
    // are registered (registerTypeDecl qualifies with ctx.currentNamespace) and
    // later compiled right after the group, not at the end of the program where
    // another namespace — or none — is in effect.
    const std::string savedNamespace = ctx.currentNamespace;
    ctx.currentNamespace.clear();
    std::map<ClassGroupDecl*, std::vector<std::unique_ptr<Node>>> placedAfter;
    for (auto& d : prog->declarations) {
        if (auto* ns = dynamic_cast<NamespaceDecl*>(d.get())) { ctx.currentNamespace = ns->name; continue; }
        auto* g = dynamic_cast<ClassGroupDecl*>(d.get());
        if (!g) continue;
        size_t firstOfGroup = toAppend.size();

        auto subName = [&](const std::string& inst) { return g->name + "_" + inst; };
        // How generated code names an instance subclass: registered under the
        // group's namespace, and `{new:}` / type lookups take the qualified name.
        auto subType = [&](const std::string& inst) { return ctx.qualify(subName(inst)); };
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
                    ctx.classes[inst.className].members.count(mName)) {
                    // The wired member is a BORROW: the group owns its instances and releases
                    // them at `free: <group>`.  An owning `ptr<T>` member would release the same
                    // instance a second time — a double free in aiml_ser_free_decoded (spec/58
                    // §5.1, spec/61 §5.2: ownership is carried by the type, wiring never changes
                    // it).  A synthesized edge is marked is_collaborator and exempt; an existing
                    // base member keeps its declared type, so it must be declared as a borrow.
                    const std::string& wiredType = ctx.classes[inst.className].members[mName].typeName;
                    if (wiredType.rfind("ptr<", 0) == 0 && wiredType.back() == '>') {
                        std::string inner = wiredType.substr(4, wiredType.size() - 5);
                        LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': instance '" +
                            inst.instanceName + "' sees '" + s + "', which wires the existing member '" +
                            mName + ": " + wiredType + "' of class '" + inst.className + "'. A sees edge is a "
                            "BORROWED collaborator, but ptr<" + inner + "> is a UNIQUE OWNER — the group "
                            "already releases that instance, so freeing the group would free it twice. "
                            "Declare the member as 'ref<" + inner + ">' (spec/58 §5.1, spec/61 §5.2).");
                    }
                    continue;
                }
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
                        ? subType(mName)                                   // same-group instance
                        : [&] { std::string sgT = subgTypeOf(sStrip.substr(0, sd));          // subgroup.instance
                              return (sgT.find('.') == std::string::npos ? ctx.qualify(sgT) : sgT) + "_" + mName; }();
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
            m.type = "ptr<" + subType(inst.instanceName) + ">";
            cont->members.push_back(std::move(m));
        }

        // spec/58 §12.3: group interfaces — the container implements each one and
        // gets one accessor per export (a ref<Iface> borrow of the instance).
        {
            std::set<std::string> madeAccessor;
            for (const auto& gname : g->implementsGroups) {
                std::string gq = groupInterfaceOf(gname);
                if (gq.empty()) continue;                  // GI-5/6, reported
                cont->interfaces.push_back(gq);
                const auto& ii = ctx.interfaces[gq];
                for (const auto& m : ii.methods) {
                    if (m.name.rfind("__gi_", 0) != 0 || !madeAccessor.insert(m.name).second) continue;
                    std::string exp = m.name.substr(5);
                    std::string ifaceQ = m.returnType.substr(4, m.returnType.size() - 5);
                    if (!baseOf.count(exp)) continue;      // GI-7, reported
                    // Through the subclass when the INSTANCE declares the interface (its
                    // vtable lives there, §5.2), else view as the base class first.
                    bool onSub = false;
                    for (auto& inst : g->instances)
                        if (inst.instanceName == exp)
                            for (auto& im : inst.implements)
                                if (im == ifaceQ || resolveIfaceName(im, "") == ifaceQ) onSub = true;
                    std::string acc = "- def:\n    name: " + m.name + "\n    return: " + m.returnType +
                                      "\n    body:\n";
                    acc += "      - decl: [gi_view, ref<" + subType(exp) + ">, <." + exp + "]\n";
                    if (onSub) {
                        acc += "      - decl: [gi_up, ref<" + ifaceQ + ">, gi_view]\n";
                    } else {
                        acc += "      - decl: [gi_base, ref<" + baseOf[exp] + ">, gi_view]\n";
                        acc += "      - decl: [gi_up, ref<" + ifaceQ + ">, gi_base]\n";
                    }
                    acc += "      - return: gi_up\n";
                    if (auto fn = parseDefSnippet(acc)) cont->methods.push_back(std::move(fn));
                    else LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': internal — failed to synthesize " + m.name + ".");
                }
            }
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
                    subType(inst.instanceName) + " }]\n";
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
                        subType(inst.instanceName) + ">, <." + inst.instanceName + "]\n";
            if (!inst.constructor.empty()) {
                std::string line = "      - call: [w_" + inst.instanceName + "." +
                                   inst.constructor[0];
                for (size_t ai = 1; ai < inst.constructor.size(); ++ai) {
                    const std::string& arg = inst.constructor[ai];
                    if (paramNames.count(arg)) { line += ", <." + arg; continue; }
                    // spec/58 §4: what is not a group parameter is a LITERAL.
                    // The YAML analysis has already removed the quotation marks of a string,
                    // so they have to go back here — otherwise
                    // `constructor: [init, "cas"]` emits a bare `cas`,
                    // which the compiler reads as a type name ("Unknown Type: 'cas'").
                    // Numbers, booleans and qualified names (enum members like
                    // `Kind.file`, as well as floating-point numbers) pass through unchanged.
                    auto looksNumeric = [](const std::string& s) {
                        if (s.empty()) return false;
                        size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
                        if (i >= s.size()) return false;
                        for (; i < s.size(); ++i)
                            if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
                        return true;
                    };
                    const bool passthrough = looksNumeric(arg) || arg == "true" ||
                                             arg == "false" ||
                                             arg.find('.') != std::string::npos;
                    line += passthrough ? (", " + arg) : (", \"" + arg + "\"");
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
                        body += "      - decl: [" + sv + ", ptr<" + subType(tpart) + ">, " + srcExpr + "]\n";
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
            LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': internal — failed to synthesize __ctor.");

        // spec/58 + spec/61 §10: synthesized DESTRUCTOR. The __ctor `new`s one box per instance and
        // per subgroup — those are OWNED by the container, so `free`ing the container (or its
        // scope-drop) must release them. Ports/collaborators are WIRED borrows (not owned) and group
        // params are inline value fields, so neither is freed here. Each `free: <.member` runs the box's
        // class-based deep-free (strings decref, nested handles release, a subgroup recurses into its own
        // __dtor) then frees the block. Without this, the container shell freed but every instance leaked.
        {
            std::string dtorBody;
            for (auto& inst : g->instances)
                dtorBody += "      - free: <." + inst.instanceName + "\n";
            for (auto& sg : g->subgroups)
                dtorBody += "      - free: <." + sg.first + "\n";
            if (!dtorBody.empty()) {
                std::string dtorText = "- def:\n    name: __dtor\n    args: []\n    body:\n" + dtorBody;
                if (auto dtorFn = parseDefSnippet(dtorText))
                    cont->methods.push_back(std::move(dtorFn));
                else
                    LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': internal — failed to synthesize __dtor.");
            }
        }

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
                            subType(inst.instanceName) + ">, <." + inst.instanceName + "]\n";
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
                LOG_ERROR(ctx.fmtLoc(g->sourceNode, g->sourceLine, g->sourceColumn) + "classgroup '" + g->name + "': internal — failed to synthesize __wire_ports.");
        }

        registerTypeDecl(cont.get());
        ctx.classGroupContainers.insert(ctx.qualify(g->name));
        toAppend.push_back(std::move(cont));
        LOG_INFO("synthesized classgroup '" + g->name + "' (" +
                 std::to_string(g->instances.size()) + " instances)");
        for (size_t i = firstOfGroup; i < toAppend.size(); ++i)
            placedAfter[g].push_back(std::move(toAppend[i]));
    }
    ctx.currentNamespace = savedNamespace;
    if (!placedAfter.empty()) {
        std::vector<std::unique_ptr<Node>> merged;
        merged.reserve(prog->declarations.size() + toAppend.size());
        for (auto& d : prog->declarations) {
            auto* g = dynamic_cast<ClassGroupDecl*>(d.get());
            merged.push_back(std::move(d));
            auto it = g ? placedAfter.find(g) : placedAfter.end();
            if (it != placedAfter.end())
                for (auto& n : it->second) merged.push_back(std::move(n));
        }
        prog->declarations = std::move(merged);
    }
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

    // Inject built-in enum: YamlTok — the kinds of yaml.tokens (spec/60).
    // Values MUST match YamlTokKind in src/sys/yaml_runtime.cpp.
    if (!ctx.enums.count("YamlTok")) {
        ctx.enums["YamlTok"] = {
            {"text", 0}, {"comment", 1}, {"key", 2}, {"string", 3}, {"number", 4},
            {"keyword", 5}, {"punct", 6}, {"anchor", 7}, {"tag", 8}, {"directive", 9}
        };
    }

    // spec/62 §7: CacheClass — the reuse class of a step.
    // Values MUST match pipeline_runtime.cpp (0=opaque 1=resolved 2=pure).
    if (!ctx.enums.count("CacheClass")) {
        ctx.enums["CacheClass"] = { {"opaque", 0}, {"resolved", 1}, {"pure", 2} };
    }
    // spec/62 §5.6: reach of a failure (F62-39).
    if (!ctx.enums.count("OnFail")) {
        ctx.enums["OnFail"] = { {"lane", 0}, {"pipeline", 1}, {"proceed", 2} };
    }
    // spec/62 §5.7: StepStatus — six values, the meanings are open
    // (F62-42, §16 item 6).  The order here is ONLY the numbering,
    // not the precedence; that is in pipeline_runtime.cpp (F62-93).
    // spec/62 §5.5: does a step run per lane or once for the matrix?
    if (!ctx.enums.count("Per")) {
        ctx.enums["Per"] = { {"lane", 0}, {"once", 1} };
    }
    // spec/62 §5.9: Lebenszyklus, getrennt vom Ausgang (F62-44 ergaenzt).
    if (!ctx.enums.count("Lifecycle")) {
        ctx.enums["Lifecycle"] = { {"pending", 0}, {"running", 1}, {"paused", 2}, {"done", 3} };
    }
    if (!ctx.enums.count("StepStatus")) {
        ctx.enums["StepStatus"] = { {"red", 0}, {"green", 1}, {"blue", 2},
                                    {"yellow", 3}, {"cyan", 4}, {"magenta", 5} };
    }

    // Inject BuiltIn Exception Classes
    std::vector<std::string> builtinExceptions = {
        "Exception", "NullPointerException", "TypeMismatchException", "IndexOutOfBoundsException",
        "DivisionByZeroException", "StackOverflowException", "OutOfMemoryException", "TimeoutException",
        "SerializationException", "DeserializationException", "BufferOverflowException", "BufferUnderflowException",
        "InvalidThreadStateException",
        // spec/29_2 §14a (F29_2-…d2): two errors when grabbing a symbol
        // from a context — the name is unknown, or the named type
        // does not match.  Two names, so that a `catch` can tell them apart;
        // without an entry here the throw carries a tag that no `catch` knows.
        "SymbolMissingException", "SymbolTypeException"
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

    // spec/33_2 §6.4 (F33-2-32): ZimError carries a REASON and a CODE — the table there maps
    // 1..6 onto "cannot open", "not a ZIM file", "corrupt", "redirect loop", "entry is a
    // redirect" and "no such entry", so a handler can branch without parsing the message.
    // Registered apart from the list above because every other builtin exception has the one
    // `message` field.
    if (!ctx.classes.count("ZimError")) {
        CompilerContext::ClassInfo zimErr;
        zimErr.name = "ZimError";
        zimErr.version = 0;
        zimErr.parentName = "Exception";

        CompilerContext::StructMember msgMember;
        msgMember.typeName = "string";
        msgMember.type = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType("string")));
        msgMember.index = 0;
        msgMember.access = AccessLevel::Public;
        zimErr.members["message"] = msgMember;

        CompilerContext::StructMember codeMember;
        codeMember.typeName = "i32";
        codeMember.type = llvm::Type::getInt32Ty(ctx.context);
        codeMember.index = 1;
        codeMember.access = AccessLevel::Public;
        zimErr.members["code"] = codeMember;

        llvm::StructType* zimErrTy = llvm::StructType::create(ctx.context, "ZimError");
        zimErrTy->setBody({msgMember.type, codeMember.type});
        zimErr.type = zimErrTy;
        ctx.classes["ZimError"] = zimErr;
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
    // round4 (forward-ref fix): serNodes built inline during registration cut forward-referenced
    // field types (a type declared later) to opaque RAWBYTES → the scope-drop then never freed those
    // pointees (probe 11). Now that the fixpoint has registered every type, rebuild each type's
    // serNodes so forward ptr/shared/model/container-element fields resolve to their real (freed)
    // descriptors. Idempotent; superseded globals are dead-stripped.
    {
        std::vector<std::string> names;
        names.reserve(ctx.classes.size());
        for (auto& kv : ctx.classes) names.push_back(kv.first);
        for (const auto& nm : names) rebuildSerNodesFor(ctx, nm);
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
    // spec/61 §2.1: cascade<X> is a Class-3 rc container (list<i32> row) like list.
    if (ctx.memV3 && typeName.rfind("cascade<", 0) == 0 && typeName.back() == '>') return true;
    // round4 §3 (memV3): an owned `tensor` field is a Class-4 resource → the model owns it and must
    // close it at scope drop (fre → aiml_tensor_free). So a tensor-field model needs serNodes + drop.
    if (ctx.memV3 && typeName == "tensor") return true;
    // spec/61 Class 5 chan<T> / Class 4 thread_handle field: released with the owning model.
    if (ctx.memV3 && (typeName == "thread_handle" || typeName == "gl_texture" || typeName == "gl_cubemap"
                      || typeName == "gl_timer_query" || typeName == "gl_buffer" || typeName == "gl_vao" || typeName == "process"
                      || typeName == "zim_handle"
                      || (typeName.rfind("chan<", 0) == 0 && typeName.back() == '>')
                      || HashmapImpl::isHashmapType(typeName))) return true;
    // spec/61 §6.2 (memV3): a `shared<T>` (refcounted co-owned) or `ptr<T>` (unique owner) FIELD owns
    // heap → the model must decref/free it at scope drop. Without this a model whose ONLY heap-bearing
    // fields are shared/ptr (no string/list/tensor) got hasHeapFields=false → no drop → leak (probe
    // 33: InnerGraph{owned:shared<InnerLeaf>, view:ref<InnerLeaf>}). `ref<T>` is a WEAK borrow → false.
    if (ctx.memV3 && typeName.back() == '>'
        && (typeName.rfind("shared<", 0) == 0 || typeName.rfind("ptr<", 0) == 0)) return true;
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
    CompilerContext& ctx, const llvm::DataLayout& dl,
    DescriptorPurpose purpose);

// spec/61 §5 Option B (memV3): build a STRUCT FIELD's subtree. A list<T>/cascade<X> field is a
// POINTER to a refcounted handle → wrap the inner LIST subtree in an AIML_TD_LIST_PTR node at the
// pointer slot. Everything else (and the OFF path) builds the field subtree inline as before. Only
// the FIELD level is wrapped — nested list ELEMENTS (list<list<T>>) stay inline via serBuildSubtree.
static std::vector<AimlTypeNode> serFieldSubtree(
    const std::string& typeName, int32_t offset,
    CompilerContext& ctx, const llvm::DataLayout& dl,
    DescriptorPurpose purpose = DescriptorPurpose::Existing)
{
    bool memV3ListField = ctx.memV3 &&
        ((typeName.rfind("list<", 0) == 0 && typeName.back() == '>') ||
         (typeName.rfind("cascade<", 0) == 0 && typeName.back() == '>'));
    bool memV3MapField = ctx.memV3 && typeName.rfind("map<", 0) == 0 && typeName.back() == '>';
    bool memV3BufField = ctx.memV3 && (typeName == "buffer" ||
                                       (typeName.rfind("buffer<", 0) == 0 && typeName.back() == '>'));
    if (!memV3ListField && !memV3MapField && !memV3BufField)
        return serBuildSubtree(typeName, offset, ctx, dl, purpose);
    std::vector<AimlTypeNode> res;
    if (memV3BufField) {
        res.push_back({AIML_TD_BUFFER_PTR, 1, 0, offset, 8, 0});   // idx 0: pointer slot → BUFFER at 1
        if (typeName == "buffer") {
            // Bare byte buffer: BUFFER(param1=2 → i8 element, esz 1). fre recurses fre(param1) per
            // element, so param1 MUST point at a scalar (i8, a fre no-op), not node 0.
            res.push_back({AIML_TD_BUFFER, 2, 0, 0, 1, 0});        // idx 1
            res.push_back({AIML_TD_I8,     0, 0, 0, 0, 0});        // idx 2: element type
        } else {
            for (auto& n : serBuildSubtree(typeName, 0, ctx, dl, purpose)) res.push_back(snShift(n, 1));
        }
        return res;
    }
    std::string inner = (typeName.rfind("cascade<", 0) == 0) ? std::string("list<i32>") : typeName;
    res.push_back({memV3MapField ? AIML_TD_MAP_PTR : AIML_TD_LIST_PTR, 1, 0, offset, 8, 0}); // pointer slot
    for (auto& n : serBuildSubtree(inner, 0, ctx, dl, purpose)) res.push_back(snShift(n, 1));
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
    CompilerContext& ctx, const llvm::DataLayout& dl,
    DescriptorPurpose purpose)
{
    std::vector<AimlTypeNode> res;

    // ── round4 §1/§3: memV3 leaf semantics (before the pointer/builtin/RAWBYTES fallbacks) ──
    // A `tensor` → TENSOR in BOTH purposes: Clone deep-copies it (aiml_tensor_clone), and the
    // scope-drop / model-free (Existing → fre) closes it (aiml_tensor_free) — an owned Class-4
    // resource is torn down with its owner (spec/61; probe 14 "lyr drops → weights tensor closed").
    // enc/dec skip TENSOR (a live resource handle is not wire-serialisable). A `ref<T>` weak edge →
    // WEAK_REF only for Clone (phase-2 redirect, no incref, not freed); for Existing a ref<T> field
    // stays the raw non-owning pointer (unchanged) — a weak edge is never freed at drop.
    if (ctx.memV3) {
        if (typeName == "tensor")
            { res.push_back({AIML_TD_TENSOR, 0,0,offset,0,0}); return res; }
        if (typeName.rfind("chan<", 0) == 0 && typeName.back() == '>')
            { res.push_back({AIML_TD_CHAN, 0,0,offset,0,0}); return res; }
        if (HashmapImpl::isHashmapType(typeName))
            { res.push_back({AIML_TD_HASHMAP, 0,0,offset,0,0}); return res; }
        if (typeName == "thread_handle")
            { res.push_back({AIML_TD_THREAD, 0,0,offset,0,0}); return res; }
        if (typeName == "gl_texture")
            { res.push_back({AIML_TD_GLTEX, 0,0,offset,0,0}); return res; }
        if (typeName == "gl_cubemap")
            { res.push_back({AIML_TD_GLCUBE, 0,0,offset,0,0}); return res; }
        if (typeName == "process")
            { res.push_back({AIML_TD_PROCESS, 0,0,offset,0,0}); return res; }
        if (typeName == "zim_handle")
            { res.push_back({AIML_TD_ZIM, 0,0,offset,0,0}); return res; }
        if (typeName == "gl_buffer")
            { res.push_back({AIML_TD_GLBUFFER, 0,0,offset,0,0}); return res; }
        if (typeName == "gl_vao")
            { res.push_back({AIML_TD_GLVAO, 0,0,offset,0,0}); return res; }
        if (typeName == "gl_timer_query")
            { res.push_back({AIML_TD_GLQUERY, 0,0,offset,0,0}); return res; }
        if (purpose == DescriptorPurpose::Clone
            && typeName.rfind("ref<", 0) == 0 && typeName.back() == '>')
            { res.push_back({AIML_TD_WEAK_REF, 0,0,offset,0,0}); return res; }
        // An OWNING ptr<Iface> field (spec/61, spec/58 §12): freed with its owner through the
        // vtable's free slot.  ref<Iface> stays a non-owning pointer pair (not freed).
        if (typeName.rfind("ptr<", 0) == 0 && typeName.back() == '>') {
            std::string inner = typeName.substr(4, typeName.size() - 5);
            auto iit = ctx.interfaces.find(inner);
            if (iit == ctx.interfaces.end()) iit = ctx.interfaces.find(ctx.qualify(inner));
            if (iit != ctx.interfaces.end())
                { res.push_back({AIML_TD_IFACE, (int32_t)iit->second.methods.size(), 0, offset,
                                 (int32_t)(2 * dl.getPointerSize()), 0}); return res; }
        }
    }

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
        if (ctx.isMemV3ContainerHandle(inner)) {
            esz = (int32_t)dl.getPointerSize();  // §5.7: nested container element = HANDLE (ptr stride)
        } else if (ctx.isRefcountModel(inner)) {
            // spec/61 §5.7: list<Model> stores %M* references — a TARGET-pointer stride
            // (8 on 64-bit, 4 on wasm32), never a literal 8.  A hardcoded 8 let the decoder
            // write element i at base+8i while wasm32 list indexing read it at base+4i:
            // index 0 agreed by accident, every later element was a garbage handle.
            esz = (int32_t)dl.getPointerSize();
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
            // so this branch is a last-resort safety net.  Pointer-sized per §5.7:
            // a user-defined element is a model handle, and the stride is the TARGET's.
            esz = (int32_t)dl.getPointerSize();
        } else {
            llvm::Type* iT = ctx.getLLVMType(inner);
            esz = iT ? (int32_t)dl.getTypeAllocSize(iT) : 8;
        }
        res.push_back({AIML_TD_LIST, 1, 0, offset, esz, 0});
        // spec/61 §5.7: a nested CONTAINER element is a HANDLE → recurse via serFieldSubtree so a
        // list<list<T>>/list<map<…>>/list<buffer> element becomes a *_PTR handle descriptor (not an
        // inlined struct). serFieldSubtree falls back to serBuildSubtree for non-container elements.
        for (auto& n : serFieldSubtree(inner, 0, ctx, dl, purpose)) res.push_back(snShift(n, 1));
        return res;
    }

    // ── buffer<T> ─────────────────────────────────────────────────────────────
    if (typeName.size()>7 && typeName.substr(0,7)=="buffer<") {
        std::string inner = typeName.substr(7, typeName.size()-8);
        llvm::Type* iT = ctx.getLLVMType(inner);
        int32_t esz = iT ? (int32_t)dl.getTypeAllocSize(iT) : 1;
        res.push_back({AIML_TD_BUFFER, 1, 0, offset, esz, 0});
        for (auto& n : serFieldSubtree(inner, 0, ctx, dl, purpose)) res.push_back(snShift(n, 1));   // §5.7 nested handle
        return res;
    }

    // ── map<K:V> ──────────────────────────────────────────────────────────────
    if (typeName.size()>4 && typeName.substr(0,4)=="map<") {
        std::string inner = typeName.substr(4, typeName.size()-5);
        size_t col = inner.find(':');
        std::string kT = (col!=std::string::npos) ? inner.substr(0,col)   : "string";
        std::string vT = (col!=std::string::npos) ? inner.substr(col+1)   : inner;
        auto ksub = serFieldSubtree(kT, 0, ctx, dl, purpose);   // §5.7 nested container value/key = handle
        auto vsub = serFieldSubtree(vT, 0, ctx, dl, purpose);
        int32_t kc = (int32_t)ksub.size();
        // AIML v2 P3/P6: map<K:Model> stores %M* references (pointer value stride).
        int32_t vsz;
        if (ctx.isMemV3ContainerHandle(vT)) vsz = (int32_t)dl.getPointerSize();  // §5.7 nested handle value
        else if (ctx.isRefcountModel(vT)) vsz = (int32_t)dl.getPointerSize();  // §5.7 target stride
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
    // spec/61 §5.1 rule 4 (memV3): a `ref<T>` is a WEAK edge — clone must PRESERVE it (copy the
    // pointer verbatim), never deep-copy the referent. So under memV3 a ref<T> field falls through
    // to the opaque raw-pointer leaf below; only an owning `ptr<T>` is followed (PTR_STRUCT).
    // Under OFF both ptr<>/ref<> keep the historical deep-copy (byte-identical deployed behavior).
    bool followAsOwningPtr = typeName.rfind("ptr<", 0) == 0
        || (typeName.rfind("ref<", 0) == 0 && !ctx.memV3);
    if (followAsOwningPtr && typeName.back() == '>') {
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
            auto sub = serBuildSubtree(inner, 0, ctx, dl, purpose);
            res.push_back({AIML_TD_PTR_STRUCT, 1, 0, offset, inner_sz, 0});
            for (auto& n : sub) res.push_back(snShift(n, 1));
            return res;
        }
        // spec/61 §5.1 (memV3): a RECURSIVE owning ptr<T> (T mid-expansion on the DFS path) cannot
        // be inlined in the static descriptor. Emit an AIML_TD_PTR_REF that references T by runtime
        // type-id; the clone/free walkers look T's descriptor up and recurse (memoized), so a
        // finite recursive chain is deep-cloned/freed instead of shared. OFF keeps the raw-pointer
        // cut (byte-identical). Only for owning ptr<T> — a ref<T> weak edge stays a raw pointer.
        if (ctx.memV3 && typeName.rfind("ptr<", 0) == 0
            && pit != ctx.classes.end() && !pit->second.isBuiltin && pit->second.type
            && g_serActivePath.count(inner)) {
            int32_t inner_sz = (int32_t)dl.getTypeAllocSize(pit->second.type);
            int32_t tid = ctx.getModelTypeId(inner);
            int32_t isModel = ctx.isRefcountModel(inner) ? 1 : 0;
            res.push_back({AIML_TD_PTR_REF, tid, isModel, offset, inner_sz, 0});
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
                (int32_t)sl->getElementOffset(mem.index), ctx, dl, purpose));
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
                    (int32_t)sl->getElementOffset(mem.index), ctx, dl, purpose));
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
    // spec/61 §9.6 (doc/spec61_inline_value_model_release_plan_2026-09-24.md): a fixed ARRAY field
    // whose element type OWNS heap (string / container / heap-bearing user model) falls through to
    // RAWBYTES here — the lifecycle descriptor then treats it as opaque bytes, so aiml_ser_retain /
    // aiml_ser_free_decoded neither retain nor release the elements → LEAK on copy/scope-drop
    // (measured 6780 B/200 iters, tests/_inline_model_red/p2_array_field_leak_RED.aiml). Warn once per
    // element type so the latent leak is visible; the real fix is a STRUCT-composition lifecycle
    // descriptor (package 2, not done here — it risks the schema-fp/wire, so it needs its own pass).
    if (typeName.rfind("array<", 0) == 0 && typeName.back() == '>') {
        std::string inner = typeName.substr(6, typeName.size() - 7);   // "string:3" / "map<string:i32>:3" / "T:3:4"
        // Element type = up to the FIRST top-level ':' (dimensions follow). A ':' inside a nested
        // generic (map<K:V>, list<...>) is NOT the dimension separator — track <> depth (P2/§11.2).
        size_t sep = std::string::npos; int depth = 0;
        for (size_t i = 0; i < inner.size(); ++i) {
            char c = inner[i];
            if (c == '<') depth++;
            else if (c == '>') { if (depth > 0) depth--; }
            else if (c == ':' && depth == 0) { sep = i; break; }
        }
        std::string elem = (sep == std::string::npos) ? inner : inner.substr(0, sep);
        // strip a shared<>/ptr<>/ref<> wrapper to find the underlying class for the heap check
        std::string bare = elem;
        for (const char* w : {"shared<", "ptr<", "ref<"})
            if (bare.rfind(w, 0) == 0 && bare.back() == '>') { bare = bare.substr(std::string(w).size(), bare.size() - std::string(w).size() - 1); break; }
        auto eci = ctx.classes.find(bare);
        bool elemHeap = (elem == "string" || elem == "path")
            || elem.rfind("list<", 0) == 0 || elem.rfind("map<", 0) == 0
            || elem.rfind("buffer", 0) == 0 || elem.rfind("array<", 0) == 0
            || elem.rfind("ptr<", 0) == 0 || elem.rfind("shared<", 0) == 0
            || elem.rfind("dyn_array<", 0) == 0 || elem.rfind("tensor", 0) == 0
            || (eci != ctx.classes.end() && eci->second.hasHeapFields && !eci->second.isBuiltin);
        if (elemHeap) {
            static std::set<std::string> warnedHeapArray;
            if (warnedHeapArray.insert(typeName).second)
                LOG_WARNING("array field of type '" + typeName + "' has heap-owning elements ('" + elem +
                    "') that the lifecycle descriptor cannot track (falls back to opaque RAWBYTES) → its "
                    "elements LEAK on copy/scope-drop (spec/61 §9.6). Avoid heap arrays as model fields "
                    "for now, or use list<" + elem + ">. Tracked by "
                    "doc/spec61_inline_value_model_release_plan_2026-09-24.md.");
        }
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
    llvm::Constant* init = llvm::ConstantArray::get(arrTy, elems);
    // LinkOnceODRLinkage: when two compilation units serialise the same type
    // (e.g. module A opens `Kv<string:T>`, imported module B
    // opens the same Kv), both emit identical ser_nodes arrays.
    // With PrivateLinkage each module would have its own pointer; the
    // db singleton cache (aiml_db_kv_open_typed_persist) registers one of the two on
    // the first open and ignores the second — after that the caller
    // with the MISMATCHED pointer encodes with different offsets. With linkonce_odr
    // the symbols are merged by the linker: all modules point to
    // the same ser_nodes definition.
    //
    // Dedup WITHIN a compilation unit: serEmitGlobal is called several times for the same type
    // — inline in registerTypeDecl and afterwards from rebuildSerNodesFor in EVERY
    // registerTypes phase (imports, siblings, main, compileFromString). Creating a fresh
    // global each time made LLVM number the colliding name
    // (__aiml_sernodes_ServerRuntime.NNNNN); at O0 these LinkOnceODR duplicates are never
    // dead-stripped, so a single .o accumulated hundreds of 59 KB copies (ServerRuntime
    // alone: 495×) → each object ~31 MB → the 5.5 GB server link blew the small code model
    // (relocation truncated to fit). Hence the existing definition is reused.
    // Reuse ONLY with bit-identical contents: LLVM uniques constants, so
    // ConstantArray::get(arrTy, elems) returns the same Constant* for equal elements → `init` is
    // pointer-identical to the existing initializer exactly when the descriptor is unchanged.
    // That is the 185× case (the same registerTypes phase across imports/siblings/main). Then
    // no new global. With DIFFERING contents do NOT mutate the existing initializer (an
    // earlier setInitializer approach, with a coincidentally equal node count, overwrote an already
    // correct descriptor global with different contents → under memV3 the descriptor drives
    // teardown/free → crash, see mkp_* rc139). Instead create a fresh one and redirect the old
    // (forward-ref-incomplete) one via RAUW + remove it — semantically as before the dedup,
    // just without the dead duplicates.
    std::string symName = "__aiml_sernodes_" + name;
    llvm::GlobalVariable* old = ctx.module().getNamedGlobal(symName);
    llvm::GlobalVariable* gv = nullptr;
    if (old && old->hasInitializer() && old->getInitializer() == init) {
        gv = old;
    } else {
        gv = new llvm::GlobalVariable(ctx.module(), arrTy, /*isConstant=*/true,
            llvm::GlobalValue::LinkOnceODRLinkage, init, old ? "" : symName);
        if (old) {
            old->replaceAllUsesWith(llvm::ConstantExpr::getBitCast(gv, old->getType()));
            old->eraseFromParent();
            gv->setName(symName);
        }
    }
    gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    gv->setVisibility(llvm::GlobalValue::HiddenVisibility);
    // hasHeapFields is computed by the caller based on serHasHeap; do not force-set it.
    classInfo.serNodesVar   = gv;
    classInfo.serNodesCount = (int32_t)nodes.size();
}

// Rebuild one class/model's serNodes descriptor from its (now fully-registered) members. serNodes
// are first built inline during registerTypeDecl, but a FIELD that references a type declared LATER
// (a forward reference, e.g. `Zone{cards:list<ptr<Card>>}` before `Card`) resolves to an opaque
// RAWBYTES leaf at that point → the scope-drop never frees the pointee (probe 11: the Card box + its
// title leaked). registerTypes calls this for every type AFTER the registration fixpoint, so the
// descriptor is rebuilt with all types present (PTR_STRUCT/MODEL, correctly freed). Idempotent; the
// superseded global is dead-stripped. Also recomputes hasHeapFields (a forward ptr/shared field now
// counts). Skips builtins / typeless entries.
static void rebuildSerNodesFor(CompilerContext& ctx, const std::string& typeName) {
    auto it = ctx.classes.find(typeName);
    if (it == ctx.classes.end()) return;
    CompilerContext::ClassInfo& ci = it->second;
    if (ci.isBuiltin || !ci.type) return;
    llvm::DataLayout dl(&ctx.module());
    const llvm::StructLayout* sl = dl.getStructLayout(ci.type);
    int32_t sz = (int32_t)dl.getTypeAllocSize(ci.type);
    bool anyHeap = false;
    for (auto& kv : ci.members) if (serHasHeap(kv.second.typeName, ctx)) { anyHeap = true; break; }
    ci.hasHeapFields = anyHeap;
    std::vector<std::pair<std::string, CompilerContext::StructMember>> sm(ci.members.begin(), ci.members.end());
    std::sort(sm.begin(), sm.end(), [](auto& a, auto& b){ return a.second.index < b.second.index; });
    bool onPath = g_serActivePath.insert(typeName).second;
    std::vector<std::vector<AimlTypeNode>> fsubs;
    for (auto& [mn, mem] : sm) {
        if (mem.is_collaborator) continue;   // spec/58 wired collaborators are not owned data
        fsubs.push_back(serFieldSubtree(mem.typeName, (int32_t)sl->getElementOffset(mem.index), ctx, dl));
    }
    if (onPath) g_serActivePath.erase(typeName);
    auto nodes = serBuildStructNodes(fsubs, 0, sz);
    serEmitGlobal(typeName, nodes, ctx, ci);
}

// spec/61 §5.1 (F1): deep-clone a container from srcSlot (holds a handle pointer) into dstSlot,
// via a LIST_PTR/MAP_PTR/BUFFER_PTR-rooted descriptor + the memoized clone walker. One
// aiml_clone_deep call = one memo table for the whole container + its element DAG (so shared
// elements clone once). memV3-only (serFieldSubtree emits the *_PTR root only under the gate).
// spec/61 section 7: GL owners have no deep-clone operation, also when nested
// through otherwise-shareable containers or owning ptr/shared edges.
bool Compiler::containsVfxResource(const std::string& type) {
    std::set<std::string> seen;
    std::function<bool(const std::string&)> visit = [&](const std::string& t) {
        if (t == "gl_buffer" || t == "gl_vao") return true;
        if (!seen.insert(t).second || t.rfind("ref<", 0) == 0) return false;
        auto open = t.find('<');
        if (open != std::string::npos && t.back() == '>') {
            std::string inner = t.substr(open + 1, t.size() - open - 2);
            int depth = 0; size_t start = 0;
            for (size_t i = 0; i <= inner.size(); ++i) {
                if (i == inner.size() || ((inner[i] == ':' || inner[i] == ',') && !depth)) {
                    if (visit(inner.substr(start, i - start))) return true;
                    start = i + 1;
                } else if (inner[i] == '<') ++depth;
                else if (inner[i] == '>') --depth;
            }
            return false;
        }
        auto ci = ctx.classes.find(t);
        if (ci != ctx.classes.end() && !ci->second.isBuiltin)
            for (const auto& member : ci->second.members)
                if (visit(member.second.typeName)) return true;
        return false;
    };
    return visit(type);
}

void Compiler::emitContainerCloneDeep(llvm::Value* dstSlot, llvm::Value* srcSlot,
                                      const std::string& containerType) {
    if (containsVfxResource(containerType)) {
        LOG_ERROR(ctx.fmtLoc() + "spec/61: cannot clone an owned GL resource");
        return;
    }

    if (!dstSlot || !srcSlot) return;
    auto* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
    auto* i32  = ctx.builder.getInt32Ty();
    llvm::DataLayout dl(&ctx.module());
    std::vector<AimlTypeNode> nodes = serFieldSubtree(containerType, 0, ctx, dl, DescriptorPurpose::Clone);
    if (nodes.empty()) return;
    // Emit a LinkOnceODR descriptor global (mirrors visitSysCloneCall).
    llvm::StructType* nodeTy = llvm::StructType::get(ctx.context, {i32,i32,i32,i32,i32,i32});
    std::vector<llvm::Constant*> elems;
    for (auto& n : nodes) {
        llvm::Constant* f[6] = {
            llvm::ConstantInt::get(i32,(uint64_t)n.kind),  llvm::ConstantInt::get(i32,(uint64_t)n.param1),
            llvm::ConstantInt::get(i32,(uint64_t)n.param2), llvm::ConstantInt::get(i32,(uint64_t)n.offset),
            llvm::ConstantInt::get(i32,(uint64_t)n.elem_size), llvm::ConstantInt::get(i32,0) };
        elems.push_back(llvm::ConstantStruct::get(nodeTy, f));
    }
    llvm::ArrayType* arrTy = llvm::ArrayType::get(nodeTy, elems.size());
    std::string sym = "__aiml_clonenodes_" + containerType;
    for (char& c : sym) if (c=='<'||c=='>'||c==':'||c==' '||c==','||c=='/') c='_';
    llvm::GlobalVariable* gv = ctx.module().getGlobalVariable(sym);
    if (!gv) {
        gv = new llvm::GlobalVariable(ctx.module(), arrTy, /*const=*/true,
            llvm::GlobalValue::LinkOnceODRLinkage, llvm::ConstantArray::get(arrTy, elems), sym);
        gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
        gv->setVisibility(llvm::GlobalValue::HiddenVisibility);
    }
    llvm::Function* dcF = ctx.getFunction("aiml_clone_deep", ctx.builder.getVoidTy(), {i8p,i8p,i8p,i32});
    ctx.builder.CreateCall(dcF, { dstSlot, srcSlot, gv, ctx.builder.getInt32((int32_t)nodes.size()) });
}

// serBuildSubtree + a reusable LinkOnceODR sernodes global for a container field type. Shared by
// emitContainerFieldFree and emitContainerFieldDeepCopy. Returns null gv (and sets nodeCount 0)
// when the type has no descriptor.
static llvm::GlobalVariable* getFieldSerNodesGlobal(CompilerContext& ctx, const std::string& typeName,
                                                    int& nodeCount) {
    llvm::DataLayout dl(&ctx.module());
    std::vector<AimlTypeNode> nodes = serBuildSubtree(typeName, 0, ctx, dl);
    nodeCount = (int)nodes.size();
    if (nodes.empty()) return nullptr;
    llvm::LLVMContext& lctx = ctx.context;
    llvm::Type* i32Ty = llvm::Type::getInt32Ty(lctx);
    llvm::StructType* nodeTy = llvm::StructType::get(lctx, {i32Ty,i32Ty,i32Ty,i32Ty,i32Ty,i32Ty});
    std::string symName = "__aiml_sernodes_fieldcopy_" + typeName;
    for (char& c : symName) if (c=='<'||c=='>'||c==':'||c==' '||c==','||c=='/') c = '_';
    llvm::GlobalVariable* gv = ctx.module().getGlobalVariable(symName);
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
        gv = new llvm::GlobalVariable(ctx.module(), arrTy, /*isConstant=*/true,
            llvm::GlobalValue::LinkOnceODRLinkage,
            llvm::ConstantArray::get(arrTy, elems), symName);
        gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
        gv->setVisibility(llvm::GlobalValue::HiddenVisibility);
    }
    return gv;
}

// spec/61 §5.7: a HANDLE-rooted (serFieldSubtree) descriptor global for a container element type,
// e.g. "list<i32>" → [LIST_PTR, LIST, i32]. Used by the outer container's scope-drop (elemKind 5)
// to release each nested-container element via aiml_ser_free_decoded (decref the inner handle,
// recursively releasing at its rc 0). Distinct symbol from the inline getFieldSerNodesGlobal.
static llvm::GlobalVariable* getElemHandleSerNodesGlobal(CompilerContext& ctx, const std::string& typeName,
                                                         int& nodeCount) {
    llvm::DataLayout dl(&ctx.module());
    std::vector<AimlTypeNode> nodes = serFieldSubtree(typeName, 0, ctx, dl);
    nodeCount = (int)nodes.size();
    if (nodes.empty()) return nullptr;
    llvm::LLVMContext& lctx = ctx.context;
    llvm::Type* i32Ty = llvm::Type::getInt32Ty(lctx);
    llvm::StructType* nodeTy = llvm::StructType::get(lctx, {i32Ty,i32Ty,i32Ty,i32Ty,i32Ty,i32Ty});
    std::string symName = "__aiml_sernodes_elemh_" + typeName;
    for (char& c : symName) if (c=='<'||c=='>'||c==':'||c==' '||c==','||c=='/') c = '_';
    llvm::GlobalVariable* gv = ctx.module().getGlobalVariable(symName);
    if (!gv) {
        std::vector<llvm::Constant*> elems;
        for (auto& n : nodes) {
            llvm::Constant* f[6] = {
                llvm::ConstantInt::get(i32Ty, (uint64_t)n.kind),  llvm::ConstantInt::get(i32Ty, (uint64_t)n.param1),
                llvm::ConstantInt::get(i32Ty, (uint64_t)n.param2), llvm::ConstantInt::get(i32Ty, (uint64_t)n.offset),
                llvm::ConstantInt::get(i32Ty, (uint64_t)n.elem_size), llvm::ConstantInt::get(i32Ty, 0) };
            elems.push_back(llvm::ConstantStruct::get(nodeTy, f));
        }
        llvm::ArrayType* arrTy = llvm::ArrayType::get(nodeTy, elems.size());
        gv = new llvm::GlobalVariable(ctx.module(), arrTy, /*isConstant=*/true,
            llvm::GlobalValue::LinkOnceODRLinkage, llvm::ConstantArray::get(arrTy, elems), symName);
        gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
        gv->setVisibility(llvm::GlobalValue::HiddenVisibility);
    }
    return gv;
}

// Public wrapper (Compiler method) so ast_compiler_stmt.cpp can obtain the element-handle descriptor.
llvm::GlobalVariable* Compiler::elemHandleSerNodes(const std::string& typeName, int& nodeCount) {
    return getElemHandleSerNodesGlobal(ctx, typeName, nodeCount);
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
                    elemSize = (int64_t)ctx.module().getDataLayout().getTypeAllocSize(ci->second.type);
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
            uint64_t sz = ctx.module().getDataLayout().getTypeAllocSize(fieldTy);
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
    llvm::DataLayout dl(&ctx.module());
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

// Signature pre-pass: a forward-declared function/method gets its call-relevant FunctionInfo
// (argument names/types/lifetimes, return type, producer) before ANY body is emitted, so a call
// to a later definition resolves ownership exactly like one to an earlier definition.
static void registerForwardInfo(CompilerContext& ctx, const std::string& key, llvm::Function* f,
                                const FunctionDecl* fn, const std::string& thisType) {
    if (!f) return;
    auto& fi = ctx.functions[key];
    fi.func = f;
    fi.argNames.clear();
    fi.argTypeNames.clear();
    fi.argLifetimes.clear();
    if (!thisType.empty()) {
        fi.argNames.push_back("this");
        fi.argTypeNames.push_back(thisType);
        fi.argLifetimes.push_back("scope");
    }
    for (size_t i = 0; i < fn->args.size(); ++i) {
        fi.argNames.push_back(fn->args[i].first);
        fi.argTypeNames.push_back(fn->args[i].second);
        fi.argLifetimes.push_back(i < fn->argMods.size() ? fn->argMods[i].lifetime : "scope");
    }
    fi.returnTypeName = fn->returnType;
    fi.ownsReturn = fn->ownsReturn;
    fi.version = fn->version;
    fi.compiled_version = fn->version;
    ctx.functions[key] = fi;
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
        // spec/61 §8: a method's `lifetime: producer` is part of its signature — register it with
        // the forward decl so callers compiled before the class body (e.g. a synthesized cascade
        // pool appended last) adopt the result. visitTopLevel re-inserts idempotently.
        for (const auto& m : cls->methods) {
            if (ctx.isOwningReturnType(m->returnType)) m->ownsReturn = true;   // §4/§8
            if (!m->ownsReturn) continue;
            ctx.ownsReturnFns.insert(m->name);
            ctx.ownsReturnFnTypes[m->name].insert(m->returnType);
            if (m->returnType == "string") ctx.stringProducerFns.insert(m->name);
        }
        if (!ctx.headerOnlyMode)
            for (const auto& m : cls->members) {
                if (ctx.isRefContainerType(m.type))
                    LOG_ERROR(ctx.fmtLoc(cls->sourceNode) + ctx.refContainerError("member '" + cls->name + "." + m.name + "'", m.type));
                if (!ctx.anyElementContainer(m.type).empty())
                    LOG_ERROR(ctx.fmtLoc(cls->sourceNode) + ctx.anyElementError("member '" + cls->name + "." + m.name + "'", m.type));
            }
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
        classInfo.sourceYaml = aiml::dumpPreservingQuotes(cls->sourceNode);
        
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
            member.alias           = astMember.alias;          // spec/15
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
            int packed = (int)ctx.module().getDataLayout().getTypeAllocSize(structType);
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
            // classgroup sees-members (is_collaborator) are wired BORROWS of sibling
            // instances the container owns (spec/58 §9): not owned data, so neither
            // freed nor serialized with this instance.  Including them here made the
            // deep free of an instance free its collaborator a second time (`free: <group>`
            // with any sees edge — and every drop of a group behind a group interface, §12).
            bool anyHeap = false;
            for (auto& [mn, mem] : classInfo.members)
                if (!mem.is_collaborator && serHasHeap(mem.typeName, ctx)) { anyHeap = true; break; }
            classInfo.hasHeapFields = anyHeap;
            const llvm::DataLayout& dl = ctx.module().getDataLayout();
            const llvm::StructLayout* sl = dl.getStructLayout(structType);
            int32_t sz = (int32_t)dl.getTypeAllocSize(structType);
            bool onPathC = g_serActivePath.insert(className).second;  // seed cycle guard
            std::vector<std::vector<AimlTypeNode>> fsubs;
            for (auto& [mn, mem] : orderedMembers) {
                if (mem.is_collaborator) continue;
                fsubs.push_back(serFieldSubtree(mem.typeName,
                    (int32_t)sl->getElementOffset(mem.index), ctx, dl));
            }
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
                if (auto* existing = ctx.module().getFunction(mangled)) {
                    if (ctx.isOwningReturnType(method->returnType)) method->ownsReturn = true;
                    registerForwardInfo(ctx, mangled, existing, method.get(), "ref<" + className + ">");
                    continue;
                }
                std::vector<llvm::Type*> paramTys;
                paramTys.push_back(i8p);   /* implicit this */
                for (const auto& arg : method->args) {
                    // AIML v2 P4: refcount-MODEL arg is by-ref (%M*), like list/map. MUST match
                    // the method definition (visitFunction) or signatures mismatch at the call.
                    bool byref = CompilerContext::isByrefParamType(arg.second);
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
                } else if (ctx.isMemV3ContainerHandle(method->returnType)) {
                    retTy = i8p;   // spec/61 §5 (memV3): container return = handle pointer
                } else {
                    IRType* irt = ctx.ir_gen->resolveType(method->returnType);
                    retTy = irt
                        ? static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(irt))
                        : i8p;
                }
                llvm::FunctionType* ft =
                    llvm::FunctionType::get(retTy, paramTys, false);
                llvm::Function* mf = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                       mangled, ctx.module());
                if (ctx.isOwningReturnType(method->returnType))
                    method->ownsReturn = true;   // §4/§8 (same rule as visitFunction)
                registerForwardInfo(ctx, mangled, mf, method.get(), "ref<" + className + ">");   // same receiver as visitTopLevel
                LOG_DEBUG("registerTypeDecl: forward-declared class method " +
                          mangled);
            }
        }
    } else if (auto mod = dynamic_cast<ModelDecl*>(node)) {
        std::string modelName = ctx.qualify(mod->name);
        ctx.validateIdentifier(mod->name, "model", mod->sourceNode);
        if (!ctx.headerOnlyMode)
            for (const auto& f : mod->fields) {
                if (ctx.isRefContainerType(f.type))
                    LOG_ERROR(ctx.fmtLoc(mod->sourceNode) + ctx.refContainerError("field '" + mod->name + "." + f.name + "'", f.type));
                if (!ctx.anyElementContainer(f.type).empty())
                    LOG_ERROR(ctx.fmtLoc(mod->sourceNode) + ctx.anyElementError("field '" + mod->name + "." + f.name + "'", f.type));
            }
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
            member.alias                 = astMember.alias;   // spec/15: externer Name

            classInfo.members[fieldName] = member;
        }

        llvm::StructType* structType = llvm::StructType::create(ctx.context, modelName);
        structType->setBody(fieldTypes);
        classInfo.type = structType;

        // Compute packed_size / padded_size for hot-reload calloc padding
        {
            int packed = (int)ctx.module().getDataLayout().getTypeAllocSize(structType);
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
            const llvm::DataLayout& dl = ctx.module().getDataLayout();
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

        // spec/29 §3.3: a bundle that uses this model carries its definition, and
        // the types of its fields (and its parent) with it — as for a class.  Until
        // 2026-11-27 a model had no stored source, so a bundled function that made
        // one failed at the receiver with "Unknown Type" (spec/62 F62-33).
        classInfo.sourceYaml = aiml::dumpPreservingQuotes(mod->sourceNode);
        {
            std::set<std::string> found;
            for (const auto& f : mod->fields) ctx.collectUserTypes(f.type, found);
            if (!mod->parentName.empty()) ctx.collectUserTypes(mod->parentName, found);
            found.erase(modelName);
            classInfo.referencedTypes.insert(found.begin(), found.end());
        }

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
        ctx.enumSources[enumName] = aiml::dumpPreservingQuotes(enm->sourceNode);
        // Unused-enumerator analysis: record every declared member + the enum's
        // source location (block-level; EnumDecl has no per-member source node).
        // Uses are collected in resolveEnum / resolvePropertyOrEnum under the same
        // qualified key; the top-level build diffs decl \ used.  (spec/49)
        {
            auto& declSet = ctx.enumDeclMembers[enumName];
            for (const auto& val : enm->values) declSet.insert(val.first);
            ctx.enumDeclLoc[enumName] = ctx.fmtLoc(enm->sourceNode);
            // spec/49 §18.2a: `wire: true` counts like a reflexive
            // whole-enum use — the other build names the members.
            if (enm->wire) {
                auto& useSet = ctx.enumUsedMembers[enumName];
                for (const auto& val : enm->values) useSet.insert(val.first);
            }
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
                LOG_ERROR(ctx.fmtLoc(casc->sourceNode, casc->sourceLine, casc->sourceColumn) + "cascade '" + casName + "': opcode enum '" + casc->opcode +
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
                        LOG_ERROR(ctx.fmtLoc(casc->sourceNode, casc->sourceLine, casc->sourceColumn) + "cascade '" + casName + "': node '" + n.name +
                                  "' is not a member of opcode enum '" + casc->opcode + "'.");
                    else n.id = mit->second;
                } else if (n.name.empty() && n.id >= 0) {
                    auto vit = opByValue.find(n.id);
                    if (vit == opByValue.end())
                        LOG_ERROR(ctx.fmtLoc(casc->sourceNode, casc->sourceLine, casc->sourceColumn) + "cascade '" + casName + "': node id " + std::to_string(n.id) +
                                  " has no member in opcode enum '" + casc->opcode + "'.");
                    else n.name = vit->second;
                } else if (!n.name.empty() && n.id >= 0) {
                    auto mit = opEnum->find(n.name);
                    if (mit == opEnum->end())
                        LOG_ERROR(ctx.fmtLoc(casc->sourceNode, casc->sourceLine, casc->sourceColumn) + "cascade '" + casName + "': node '" + n.name +
                                  "' is not a member of opcode enum '" + casc->opcode + "'.");
                    else if (mit->second != n.id)
                        LOG_ERROR(ctx.fmtLoc(casc->sourceNode, casc->sourceLine, casc->sourceColumn) + "cascade '" + casName + "': node '" + n.name + "' id " +
                                  std::to_string(n.id) + " != " + casc->opcode + "." + n.name +
                                  " (" + std::to_string(mit->second) + ").");
                }
            } else if (n.id < 0) {
                LOG_ERROR(ctx.fmtLoc(casc->sourceNode, casc->sourceLine, casc->sourceColumn) + "cascade '" + casName + "': node '" + n.name + "' has no numeric "
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
        ctx.cascadeSources[casName] = aiml::dumpPreservingQuotes(casc->sourceNode);
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
        ctx.interfaceSources[ifaceName] = aiml::dumpPreservingQuotes(iface->sourceNode);
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
            // spec/58 §12.3: the accessors of a group interface return the export's
            // interface — qualified here, so a container in another namespace/unit
            // implements exactly the same signature.
            if (iface->groupInterface) {
                for (auto& m : info.methods) {
                    std::string raw = m.returnType.substr(4, m.returnType.size() - 5);   // ref<…>
                    std::string q = ctx.interfaces.count(raw) ? raw
                                  : (ctx.interfaces.count(ctx.qualify(raw)) ? ctx.qualify(raw) : raw);
                    m.returnType = "ref<" + q + ">";
                }
            }
            ctx.interfaces[ifaceName] = std::move(info);
        }
        if (iface->groupInterface && !ctx.groupInterfaces.count(ifaceName)) {
            CompilerContext::GroupInterfaceInfo gi;
            gi.ns = ctx.currentNamespace;
            gi.exports = iface->groupExports;
            ctx.groupInterfaces[ifaceName] = std::move(gi);
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
        ctx.prefabDeclLoc[prefab->name] =
            ctx.fmtLoc(prefab->sourceNode, prefab->sourceLine, prefab->sourceColumn);
        // Tier 4 (spec/44_8): record the declaration itself.  The check
        // only runs in the whole-program pass, where the AST is gone.
        {
            aiml::prefab::Meta meta;
            meta.loc    = ctx.prefabDeclLoc[prefab->name];
            meta.width  = prefab->width;
            meta.height = prefab->height;
            meta.inputTransparent = prefab->inputTransparent;
            meta.draggable        = prefab->draggable;
            for (const auto& ch : prefab->children) {
                aiml::prefab::ChildMeta cm;
                cm.loc = ch.src_line > 0
                    ? ctx.fmtLoc(YAML::Node(), ch.src_line, ch.src_col)
                    : meta.loc;
                cm.name = ch.name; cm.type = ch.type;
                cm.pos_x = ch.x; cm.pos_y = ch.y;
                cm.width = ch.width; cm.height = ch.height;
                cm.visible = ch.is_visible;
                meta.children.push_back(cm);
            }
            ctx.prefabMeta[prefab->name] = std::move(meta);
        }
        auto& kids = ctx.prefabDecls[prefab->name];
        for (const auto& ch : prefab->children)
            kids.insert(ch.name);
        LOG_DEBUG("AST Registered Prefab (static): " + prefab->name +
                  " (" + std::to_string(kids.size()) + " children)");
    } else if (auto func = dynamic_cast<FunctionDecl*>(node)) {
        // spec/61 §8: `lifetime: producer` is part of the signature — register it with the
        // forward decl, before any body is emitted, so callers earlier in the file see it
        // (tests/lifetime/69). visitFunction re-inserts idempotently.
        if (ctx.isOwningReturnType(func->returnType)) func->ownsReturn = true;   // §4/§8
        if (func->ownsReturn) {
            ctx.ownsReturnFns.insert(func->name);
            ctx.ownsReturnFnTypes[func->name].insert(func->returnType);
            if (func->returnType == "string") ctx.stringProducerFns.insert(func->name);
        }
        // Forward-declare function signature so forward references resolve.
        std::string mangledName = ctx.qualify(func->name);
        if (auto* existing = ctx.module().getFunction(mangledName)) {
            registerForwardInfo(ctx, mangledName, existing, func, "");
            return;
        }

        auto* i8p = llvm::PointerType::get(ctx.context, 0);
        std::vector<llvm::Type*> paramTys;
        for (const auto& arg : func->args) {
            // SAME byref policy as visitFunction: list/map args become opaque ptr.
            // AIML v2 (P4): a refcount MODEL arg is likewise an opaque ptr (%M* by-ref). MUST match
            // visitFunction's predicate or the definition/forward-decl signatures mismatch.
            bool byref = CompilerContext::isByrefParamType(arg.second);
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
        else if (ctx.isMemV3ContainerHandle(func->returnType))
            retTy = i8p;   // spec/61 §5 (memV3): container return = refcounted handle pointer
        else {
            IRType* irt = ctx.ir_gen->resolveType(func->returnType);
            retTy = irt ? static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(irt)) : i8p;
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(retTy, paramTys, false);
        llvm::Function* ff = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, mangledName, ctx.module());
        registerForwardInfo(ctx, mangledName, ff, func, "");
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
        if (ctx.module().getFunction(fd.name)) {
            LOG_DEBUG("visitExternBlock: " + fd.name + " already declared, skipping");
            continue;
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(retTy, paramTys, false);
        llvm::Function* f = llvm::Function::Create(
            ft, llvm::Function::ExternalLinkage, fd.name, ctx.module());

        // Store in context so visitCall can find it via resolveFunction / ctx.module().getFunction
        CompilerContext::FunctionInfo fi;
        fi.func           = f;
        fi.fptable_index  = -1;   // external → no fptable slot
        fi.version        = 1;
        fi.compiled_version = 1;
        ctx.functions[fd.name] = fi;

        LOG_DEBUG("visitExternBlock: registered extern " + fd.name);
    }
}

// spec/61 §3.2: forward decl — auto return-ownership classifier (defined before visitFunction).
// Currently DISABLED at its call sites (see notes there); kept for a future, fully behaviourally
// regression-tested re-enable. [[maybe_unused]] so the disabled state does not warn/error.
[[maybe_unused]] static bool classifyOwnedStringReturn(FunctionDecl* fn, CompilerContext& ctx);

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
                fi.sourceYaml = aiml::dumpPreservingQuotes(func->sourceNode);
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
            ctx.classes[qualName].sourceYaml = aiml::dumpPreservingQuotes(cls->sourceNode);
            if (ctx.classes.count(cls->name)) {
                ctx.classes[cls->name].version    = cls->version;
                ctx.classes[cls->name].sourceYaml = aiml::dumpPreservingQuotes(cls->sourceNode);
            }
            LOG_INFO("visitTopLevel: updated sourceYaml for " + cls->name + " to v"
                     + std::to_string(cls->version));
        }
        for (const auto& method : cls->methods) {
            std::string oldName = method->name;
            // spec/61 §3.2: auto-classify an owned string return (see classifyOwnedStringReturn).
            // DISABLED (2026-09-24) — see the note in visitFunction; reverted to targeted annotations.
            // if (!method->ownsReturn && classifyOwnedStringReturn(method.get(), ctx))
            //     method->ownsReturn = true;
            // spec/61 return-ownership: capture the ORIGINAL method name (pre-mangle) so a
            // call site `<.recv.get_base` matches by its trailing identifier.
            if (ctx.isOwningReturnType(method->returnType)) method->ownsReturn = true;   // §4/§8
            if (method->ownsReturn) {
                ctx.ownsReturnFns.insert(oldName);
                ctx.ownsReturnFnTypes[oldName].insert(method->returnType);   // spec/61: name→return type (collision guard)
                if (method->returnType == "string") ctx.stringProducerFns.insert(oldName);
            }
            method->name = cls->name + "_" + method->name;
            // The instance argument behind `<` must carry the class's registered
            // (namespace-qualified) name — classes are registered as
            // ctx.qualify(name), so a short name made every `<.member` /
            // `<.method` lookup fail inside classes declared under `- namespace:`.
            auto it = method->args.insert(method->args.begin(), {"this", "ref<" + ctx.qualify(cls->name) + ">"});
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
                // Methods were defined under ctx.qualify(cls->name + "_" + m) (visitFunction
                // qualifies), so a class declared under `- namespace:` needs the same here.
                std::string mangled = ctx.qualify(cls->name + "_" + m.name);
                llvm::Function* fn = ctx.module().getFunction(mangled);
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
                // Compare return type — mirror visitFunction's lowering so a memV3 container/
                // refcount-model return (lowered to a handle pointer) matches the interface's
                // declared type (also a pointer under memV3).
                std::string ifaceRet = m.returnType.empty() ? "void" : m.returnType;
                llvm::Type* expectedRet =
                    (ctx.isRefcountModel(ifaceRet) || ctx.isMemV3ContainerHandle(ifaceRet))
                    ? static_cast<llvm::Type*>(i8p)
                    : static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(ifaceRet)));
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
                    // Mirror visitFunction's parameter lowering: list<>/map<> are by-ref (pointer),
                    // a refcount-model (incl. memV3 shared<>) is a %M* pointer — otherwise the
                    // declared type. Keeps the interface check consistent under memV3.
                    const std::string& at = m.args[ai].second;
                    bool byref = CompilerContext::isByrefParamType(at);
                    bool refModel = ctx.isRefcountModel(at);
                    llvm::Type* expectedArg = (byref || refModel)
                        ? static_cast<llvm::Type*>(i8p)
                        : static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(at)));
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
            // Qualified like the upcast site's lookup (it names the class by its type).
            std::string vtableName = "__vtable_" + ctx.qualify(cls->name) + "_" + ifaceName;
            // LinkOnceODR instead of Private: vtables must be visible across .o boundaries,
            // because buildInterfaceUpcast references them from a DIFFERENT compilation unit
            // (e.g. Factory).  ODR dedup avoids linker
            // conflicts if the same class shows up from several imports.
            // Before: PrivateLinkage → silent null in cross-file upcast → SIGSEGV
            // on interface method dispatch.
            if (auto* existing = ctx.module().getNamedGlobal(vtableName)) {
                // Forward decl (from cross-file upcast) was already emitted.
                // Now upgrade it with the definition.
                if (!existing->hasInitializer()) {
                    existing->setInitializer(init);
                    existing->setLinkage(llvm::GlobalValue::LinkOnceODRLinkage);
                }
            } else {
                new llvm::GlobalVariable(ctx.module(), arrTy, /*isConstant=*/true,
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
            // Asset registry validation: `texture: "<stem>"` must be
            // resolvable at compile time (same contract as
            // `gl.tex("<stem>")`).  Paths with slash/dot = legacy raw paths
            // — those keep going through the path-based fallback in the runtime.
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
                            ctx.module().getDataLayout().getStructLayout(bci.type);
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
                "\"font\":\"" + escape_json(c.font) + "\","
                "\"gutter\":" + std::string(c.gutter ? "1" : "0") + ","
                "\"min\":" + std::to_string(c.slider_min) + ","
                "\"visible\":" + std::string(c.is_visible ? "1" : "0") + ","
                "\"scrollable\":" + std::string(c.scrollable ? "1" : "0") + ","
                "\"tail\":" + std::string(c.tail ? "1" : "0") + ","
                "\"wrap\":" + std::string(c.wrap ? "1" : "0") + ","
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
                "\"cascade_flow\":\"" + escape_json(c.cascade_flow) + "\","
                "\"payload_scroll\":" + std::string(c.payload_scroll ? "1" : "0") + ","
                "\"enum\":\"" + escape_json(c.enum_type) + "\"}";
        }
        childrenJson += "]";

        llvm::Value* nameStr = ctx.builder.CreateGlobalStringPtr(prefab->name, "prefab_name");
        llvm::Value* texStr  = ctx.builder.CreateGlobalStringPtr(prefab->texture, "prefab_tex");
        llvm::Value* childStr = ctx.builder.CreateGlobalStringPtr(childrenJson, "prefab_children");
        llvm::Value* w = llvm::ConstantFP::get(f32, prefab->width);
        llvm::Value* h = llvm::ConstantFP::get(f32, prefab->height);
        llvm::Value* drag = ctx.builder.getInt32(prefab->draggable ? 1 : 0);
        llvm::Value* itr  = ctx.builder.getInt32(prefab->inputTransparent ? 1 : 0);

        ctx.builder.CreateCall(
            ctx.getFunction("aiml_prefab_register", vd, {i8p, f32, f32, i8p, i32, i8p, i32}),
            {nameStr, w, h, texStr, drag, childStr, itr});

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
                llvm::Function* methodFn = ctx.module().getFunction(mangled);
                if (!methodFn) {
                    // Forward-decl: (this*, p*) -> void.
                    auto* fn_ty = llvm::FunctionType::get(vd, {i8p, i8p}, false);
                    methodFn = llvm::Function::Create(
                        fn_ty, llvm::Function::ExternalLinkage,
                        mangled, &ctx.module());
                }
                // Emit trampoline __cb_<obj>__<method>(void* inst):
                //   methodFn(<obj_gv>, inst); ret void
                std::string trampName = "__cb_" + objName + "__" + methodName;
                cb_fn = ctx.module().getFunction(trampName);
                if (!cb_fn) {
                    auto* tr_ty = llvm::FunctionType::get(vd, {i8p}, false);
                    cb_fn = llvm::Function::Create(
                        tr_ty, llvm::Function::InternalLinkage,
                        trampName, &ctx.module());
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
                cb_fn = ctx.module().getFunction(cb);
                if (!cb_fn) {
                    /* Forward-declare void(i8*) — definition supplied later by visitFunction. */
                    auto* cb_ty = llvm::FunctionType::get(vd, {i8p}, false);
                    cb_fn = llvm::Function::Create(cb_ty, llvm::Function::ExternalLinkage,
                                                   cb, &ctx.module());
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
        /* Asset registry validation: `shader: "<stem>"` must be resolvable at
         * compile time (same contract as texture:/gl.tex).  Without this
         * check a typo only fails at runtime — and there only
         * silently (fallback to the default shader).  Paths with slash/dot =
         * legacy raw paths that keep going through the runtime fopen. */
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
// ── spec/61 §3.2: automatic string return-ownership classification ─────────────────────────────
// A `string`-returning function whose EVERY return path is a provably-OWNED string (a fresh temp,
// an owned string local moved out, or a string field of an owned value-model local) is an owns-return
// producer: its callers must ADOPT the result (no incref). Without this the caller lowering treats a
// returned string as a borrow and adds an incref, releasing only that extra reference on scope exit
// and leaking the callee's original one (the 9rooms owned-string-return leak class:
// avatar_initial/join_pipe/card_sig/zone_sig/take_edit_card_id/pid_for/nick_for/resolve_token_pid…).
// SOUND + conservative: any BORROW path (a bare param, a `<.member`, a borrowed `param.field`, a
// ref<>/ptr<> local field, an at-element view) or any unclassifiable path makes the whole function
// stay a borrow (callers keep increfing) — never a double-free. This only ADDS owns-return, mirroring
// the callee-side move-out exemptions already emitted in visitRetStmt; it does not weaken any borrow.
static bool ownedStringReturnExpr(const Expr* e,
                                  const std::set<std::string>& params,
                                  const std::map<std::string, std::string>& locals,
                                  CompilerContext& ctx) {
    if (!e) return false;
    if (auto* lit = dynamic_cast<const LiteralExpr*>(e)) return lit->type == "string";
    if (dynamic_cast<const StringInterpolationExpr*>(e)) return true;   // fresh alloc
    if (auto* call = dynamic_cast<const CallExpr*>(e)) {
        const std::string& fn = call->functionName;
        size_t dp = fn.rfind('.');
        std::string sn = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
        static const std::set<std::string> fresh = {
            "substr", "lower", "upper", "title", "trim", "replace", "split", "slice",
            "concat", "join", "format", "to_string", "repeat", "reverse", "pad_left", "pad_right" };
        if (fresh.count(sn)) return true;
        return ctx.stringProducerFns.count(sn) > 0;   // a callee already classified owns-return
    }
    if (auto* var = dynamic_cast<const VariableExpr*>(e)) {
        const std::string& n = var->name;
        if (n.find('<') != std::string::npos) return false;   // `<.member` = borrow
        size_t dot = n.find('.');
        if (dot != std::string::npos) {
            std::string base = n.substr(0, dot);
            if (params.count(base)) return false;             // borrowed param.field
            auto li = locals.find(base);
            if (li == locals.end()) return false;             // unknown base → conservative
            const std::string& bt = li->second;
            if (bt.rfind("ref<", 0) == 0 || bt.rfind("ptr<", 0) == 0
                || bt.rfind("shared<", 0) == 0) return false; // borrowed handle field
            auto ci = ctx.classes.find(bt);                   // owned value-model local field
            return ci != ctx.classes.end() && ci->second.isModel
                   && ci->second.hasHeapFields && !ci->second.isBuiltin;
        }
        if (params.count(n)) return false;                    // bare param → borrow (double-free risk)
        auto li = locals.find(n);
        return li != locals.end() && li->second == "string";  // owned string local (move-out)
    }
    return false;   // AtExpr element view / New / unknown → conservative borrow
}
static void collectLocalsAndReturns(const Stmt* s,
                                    std::map<std::string, std::string>& locals,
                                    std::vector<const Expr*>& returns) {
    if (!s) return;
    if (auto* d = dynamic_cast<const DeclStmt*>(s)) { locals[d->varName] = d->typeName; return; }
    if (auto* r = dynamic_cast<const RetStmt*>(s)) { returns.push_back(r->value.get()); return; }
    if (auto* b = dynamic_cast<const BlockStmt*>(s)) {
        for (const auto& st : b->statements) collectLocalsAndReturns(st.get(), locals, returns); return; }
    if (auto* i = dynamic_cast<const IfStmt*>(s)) {
        collectLocalsAndReturns(i->thenBranch.get(), locals, returns);
        collectLocalsAndReturns(i->elseBranch.get(), locals, returns); return; }
    if (auto* w = dynamic_cast<const WhileStmt*>(s)) {
        collectLocalsAndReturns(w->body.get(), locals, returns); return; }
    if (auto* t = dynamic_cast<const TryStmt*>(s)) {
        collectLocalsAndReturns(t->tryBlock.get(), locals, returns);
        for (auto* cb : t->catchBodies()) collectLocalsAndReturns(cb, locals, returns);
        return; }
}
static bool classifyOwnedStringReturn(FunctionDecl* fn, CompilerContext& ctx) {
    if (!fn || fn->returnType != "string" || !fn->body) return false;
    std::set<std::string> params;
    for (const auto& a : fn->args) params.insert(a.first);
    std::map<std::string, std::string> locals;
    std::vector<const Expr*> returns;
    collectLocalsAndReturns(fn->body.get(), locals, returns);
    if (returns.empty()) return false;
    for (const Expr* r : returns)
        if (!ownedStringReturnExpr(r, params, locals, ctx)) return false;
    return true;
}

// spec/61 §8: does the body rebind plain local `name` (set: [name, …]) anywhere?
static bool bodyRebinds(const BlockStmt* b, const std::string& name);
static bool stmtRebinds(const Stmt* s, const std::string& name) {
    if (auto* st = dynamic_cast<const SetStmt*>(s)) return st->target == name;
    if (auto* i = dynamic_cast<const IfStmt*>(s)) return bodyRebinds(i->thenBranch.get(), name) || bodyRebinds(i->elseBranch.get(), name);
    if (auto* w = dynamic_cast<const WhileStmt*>(s)) return bodyRebinds(w->body.get(), name);
    if (auto* t = dynamic_cast<const TryStmt*>(s)) {
        if (bodyRebinds(t->tryBlock.get(), name)) return true;
        for (auto* cb : t->catchBodies()) if (bodyRebinds(cb, name)) return true;
        return false;
    }
    if (auto* b = dynamic_cast<const BlockStmt*>(s)) return bodyRebinds(b, name);
    return false;
}
static bool bodyRebinds(const BlockStmt* b, const std::string& name) {
    if (!b) return false;
    for (auto& st : b->statements) if (stmtRebinds(st.get(), name)) return true;
    return false;
}

void Compiler::visitFunction(FunctionDecl* func) {
    // Basic mapping using our old context structure for now
    std::string mangledName = ctx.qualify(func->name);
    ctx.currentFunctionName = func->name;
    ctx.inServiceSynthFn = func->serviceSynth;        // spec/66 §5.6
    ctx.traceDeclarationSites.clear();
    // spec/61 §3.2: auto-classify an owned string return so callers ADOPT (see classifyOwnedStringReturn).
    // Idempotent + additive (never clears an explicit annotation); runs in the header-only forward-decl
    // pass too, so the flag is set before any call site is lowered.
    // DISABLED (2026-09-24): the broad auto-classification introduced a hard-to-isolate 9rooms
    // client navigation regression (deck-editor → card-editor). Reverted to the targeted, individually
    // valgrind-verified producer annotations + the retStrFieldOfOwnedModel compiler fix. Re-enable only
    // with a full 9rooms game+client behavioural regression pass, not just leak/sanitizer checks.
    // if (!func->ownsReturn && classifyOwnedStringReturn(func, ctx)) func->ownsReturn = true;
    // spec/61 §4/§8 (2026-09-30): every string return hands the caller an owning reference — the
    // producer machinery dups borrowed returns, moves owned locals, increfs fields of dropped owners.
    if (ctx.isOwningReturnType(func->returnType)) func->ownsReturn = true;

    /* Snapshot + install decorator context so visitRetStmt can consult it
     * without needing a pointer back to the FunctionDecl.  Cleared on exit
     * of the outer scope (we snapshot the previous values so nested calls
     * during compilation don't leak decorator state up the stack). */
    auto _saved_decorators   = std::move(ctx.currentFunctionDecorators);
    auto _saved_return_type  = std::move(ctx.currentFunctionReturnType);
    bool _saved_owns_return  = ctx.currentFunctionOwnsReturn;
    auto _saved_static_off   = std::move(ctx.currentFunctionStaticOff);
    ctx.currentFunctionDecorators = func->decorators;
    ctx.currentFunctionReturnType = func->returnType;
    ctx.currentFunctionOwnsReturn = func->ownsReturn;   // spec/61 §3.2 (producer)
    ctx.currentFunctionStaticOff  = func->staticOff;    // per-def static-check opt-outs (diagnostics)

    ctx.validateIdentifier(func->name, "function", func->sourceNode);

    std::vector<IRType*> argTypes;
    std::vector<std::string> argNames;
    std::vector<bool>        argIsByref;  // parallel to argTypes; true for list/map args
    std::vector<bool>        argIsRefModel; // parallel to argTypes; true for v2 refcount-model args
    std::vector<std::string> argLifetimes; // parallel to argNames; spec/61 §3.1 ownership

    for (size_t ai = 0; ai < func->args.size(); ++ai) {
        const auto& arg = func->args[ai];
        ctx.validateIdentifier(arg.first, "argument", func->sourceNode,
                               func->sourceLine, func->sourceColumn);
        // list<T>/map<K:V> args are implicitly passed by reference (Python/Java style).
        // LLVM signature: opaque pointer instead of struct-by-value, so that size++/data growth
        // propagates to the caller.  The forward decl in registerTypeDecl uses the SAME
        // logic (otherwise signature mismatch).  Spec/22_call_semantics.md.
        // AIML v2 (P4): a refcount MODEL arg is likewise passed by-ref as a %M* (borrow) — the
        // callee shares the caller's object; in-place mutation is visible to the caller.
        bool byref = CompilerContext::isByrefParamType(arg.second);
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
    // spec/61 §5 (memV3): a container return is a refcounted HANDLE → returned as a pointer.
    else if (ctx.isMemV3ContainerHandle(func->returnType)) retTy = ctx.ir_gen->getPointerTy();
    else if (!func->returnType.empty()) retTy = ctx.ir_gen->resolveType(func->returnType);
    if (!retTy) retTy = ctx.ir_gen->resolveType("void");
    
    // Reuse forward-declared function if it exists (from registerTypeDecl), else create new
    IRValue* funcVal = nullptr;
    llvm::Function* existingFunc = ctx.module().getFunction(mangledName);
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
    for (const auto& a : func->args) funcInfo.argTypeNames.push_back(a.second);  // §6.2/§8 borrow-check
    funcInfo.returnTypeName = func->returnType;  // §6.2/§10 (value-list push check)
    funcInfo.version = func->version;
    funcInfo.clearance_level  = func->level;    // spec/65 F65-9
    funcInfo.clearance_grants = func->grants;
    funcInfo.compiled_version = func->version;  // locked at compile time
    // Assign fptable slot — preserve any pre-existing index (e.g. from an alias entry)
    if (ctx.functions.count(mangledName) && ctx.functions[mangledName].fptable_index >= 0)
        funcInfo.fptable_index = ctx.functions[mangledName].fptable_index;
    else
        funcInfo.fptable_index = ctx.fptable_next_index++;
    funcInfo.sourceYaml = aiml::dumpPreservingQuotes(func->sourceNode);
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
        LOG_ERROR(ctx.fmtLoc() + "visitFunction: signature mismatch for '" + func->name + "' (mangled='" + mangledName
                  + "'): forward-decl has " + std::to_string(llvmFunc->arg_size())
                  + " args but current definition has " + std::to_string(argNames.size())
                  + " args.  Source: " + srcInfo
                  + ".  Likely cause: function name reused with different signature, or a typed-parameter failed to resolve.");
        ctx.symbol_table = savedSymbols;
        ctx.ir_gen->setInsertPoint(oldBlock);
        return;
    }
    if (ctx.isRefContainerType(func->returnType))
        LOG_ERROR(ctx.fmtLoc(func->sourceNode) + ctx.refContainerError(func->name + ": return", func->returnType));
    if (!ctx.anyElementContainer(func->returnType).empty())
        LOG_ERROR(ctx.fmtLoc(func->sourceNode) + ctx.anyElementError(func->name + ": return", func->returnType));
    for (size_t i = 0; i < argNames.size(); ++i) {
        if (ctx.symbol_table.count(argNames[i])) {
            LOG_ERROR(ctx.fmtLoc() + "Variable shadowing is not allowed: function argument '" + argNames[i]
                      + "' in '" + func->name + "' shadows an existing variable");
            shadowError = true;
        }
        IRValue* argAlloca = ctx.ir_gen->createAlloca(argTypes[i], argNames[i]);
        IRValue* argVal = ctx.ir_gen->getFunctionArg(funcVal, i);
        if (!argVal || !ctx.ir_gen->unwrap(argVal)) {
            LOG_ERROR(ctx.fmtLoc() + "visitFunction: getFunctionArg returned null for arg #"
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
            if (ctx.isRefContainerType(ptn))
                LOG_ERROR(ctx.fmtLoc(func->sourceNode) + ctx.refContainerError(
                    func->name + ": parameter '" + argNames[i] + "'", ptn));
            if (!ctx.anyElementContainer(ptn).empty())
                LOG_ERROR(ctx.fmtLoc(func->sourceNode) + ctx.anyElementError(
                    func->name + ": parameter '" + argNames[i] + "'", ptn));
            if ((ptn.rfind("ptr<list<", 0) == 0 || ptn.rfind("ptr<map<", 0) == 0)
                && ptn.size() > 5 && !std::getenv("AIML_ALLOW_PTR_CONTAINER")) {
                LOG_WARNING(func->name + ": parameter '" + argNames[i] + "' is '" + ptn +
                    "'. list/map are already passed by reference (single indirection); "
                    "'" + ptn.substr(4, ptn.size() - 5) + "' is enough — the explicit ptr<> "
                    "adds a second indirection. (spec/49 §19)");
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
            // ir_element_type stays the real struct shape (for member dispatch),
            // but the alloca actually holds only a pointer.  Method dispatch
            // must check is_byref_arg and do an extra load.
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
                        auto cit = ctx.classes.find(ctx.currentClassKey());
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
                    } else if (!ctx.memV3 && paramType == "string" && ctx.lifetimeDropsEnabled) {
                        // Keep the legacy caller-transfer convention when memV3 is disabled.
                        CompilerContext::ScopeDrop sd;
                        sd.addr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argAlloca));
                        sd.kind = 1;
                        ctx.pendingParamDrops.push_back(sd);
                    }
                }
            }
        }
        // spec/61 §8: a container parameter holds its own reference for the call (incref on entry,
        // released on every exit), including consumed RC parameters (§9.2 co-owned).
        if (ctx.memV3 && ctx.lifetimeDropsEnabled && argAlloca && i < argIsByref.size() && argIsByref[i]) {
            std::string ptn;
            for (auto& fa : func->args) if (fa.first == argNames[i]) { ptn = fa.second; break; }
            CompilerContext::ScopeDrop sd;
            llvm::Value* pslot = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argAlloca));
            if (ctx.isMemV3ContainerHandle(ptn) && makeHandleDrop(ptn, pslot, sd)) {
                {
                    std::string kind = (sd.kind == 3) ? "map" : (sd.kind == 8) ? "buffer" : "list";
                    auto* i8p = llvm::PointerType::get(ctx.context, 0);
                    ctx.builder.CreateCall(ctx.getFunction("aiml_" + kind + "_handle_incref", ctx.builder.getVoidTy(), {i8p}),
                                           { ctx.builder.CreateLoad(i8p, pslot, argNames[i] + "_h") });
                }
                sd.debugName = argNames[i];
                sd.paramRef = true;
                ctx.pendingParamDrops.push_back(sd);
            }
        }
        // spec/61 §8: a string parameter the body rebinds holds its own reference for the call
        // (incref on entry; the managed slot releases whatever it holds on every exit).
        if (ctx.memV3 && ctx.lifetimeDropsEnabled && argAlloca && func->body) {
            std::string ptn;
            for (auto& fa : func->args) if (fa.first == argNames[i]) { ptn = fa.second; break; }
            bool consumedParam = i < argLifetimes.size() && argLifetimes[i] == "consumed";
            if (ptn == "string" && (consumedParam || bodyRebinds(func->body.get(), argNames[i]))) {
                auto* i8p = llvm::PointerType::get(ctx.context, 0);
                llvm::Value* pslot = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argAlloca));
                ctx.builder.CreateCall(ctx.getFunction("aiml_str_incref", ctx.builder.getVoidTy(), {i8p}),
                                       { ctx.builder.CreateLoad(i8p, pslot, argNames[i] + "_s") });
                CompilerContext::ScopeDrop sd;
                sd.addr = pslot;
                sd.kind = 1;
                sd.managedStringSlot = true;
                sd.paramRef = true;
                sd.debugName = argNames[i]; sd.debugType = ptn;
                ctx.pendingParamDrops.push_back(sd);
                sym.paramOwnsRef = true;
            }
        }
        // spec/61 §8: a ptr<T> parameter is a move — the callee owns the box and frees it unless it
        // moves it on (return / store / owning call).
        if (ctx.memV3 && ctx.lifetimeDropsEnabled && argAlloca && !(i < argIsRefModel.size() && argIsRefModel[i])) {
            std::string ptn;
            for (auto& fa : func->args) if (fa.first == argNames[i]) { ptn = fa.second; break; }
            if (ptn.size() > 5 && ptn.rfind("ptr<", 0) == 0 && ptn.back() == '>') {
                std::string boxed = ptn.substr(4, ptn.size() - 5);
                auto bci = ctx.classes.find(boxed);
                llvm::Value* pslot = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argAlloca));
                CompilerContext::ScopeDrop sd;
                sd.addr = pslot;
                sd.ownedParamBox = true;
                sd.debugName = argNames[i]; sd.debugType = ptn;
                bool ok = false;
                if (ctx.interfaces.count(boxed)) {
                    llvm::Type* ft = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(argTypes[i]));
                    if (ft && ft->isStructTy()) {
                        sd.kind = 5; sd.fatPtrType = ft;
                        sd.ifaceMethodCount = (int)ctx.interfaces[boxed].methods.size();
                        ok = true;
                    }
                } else if (bci != ctx.classes.end() && !bci->second.isBuiltin && bci->second.type
                           && !ctx.classGroupContainers.count(boxed) && !ctx.cascadeClasses.count(boxed)) {
                    sd.kind = 4;
                    if (bci->second.hasHeapFields && bci->second.serNodesVar) {
                        sd.serNodes = bci->second.serNodesVar; sd.serNodesCount = bci->second.serNodesCount;
                    }
                    ok = true;
                }
                if (ok) ctx.pendingParamDrops.push_back(sd);
            }
        }
        // spec/61 §8: a shared<T> parameter co-owns for the call (incref, decref on every exit);
        // consumed also co-owns: the caller retains its own reference (§9.2).
        if (ctx.memV3 && ctx.lifetimeDropsEnabled && argAlloca && i < argIsRefModel.size() && argIsRefModel[i]) {
            std::string ptn;
            for (auto& fa : func->args) if (fa.first == argNames[i]) { ptn = fa.second; break; }
            std::string bare = ptn;
            if (bare.rfind("shared<", 0) == 0 && bare.back() == '>') bare = bare.substr(7, bare.size() - 8);
            auto bci = ctx.classes.find(bare);
            auto* i8p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* pslot = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argAlloca));
            ctx.builder.CreateCall(ctx.getFunction("aiml_model_incref", ctx.builder.getVoidTy(), {i8p}),
                                       { ctx.builder.CreateLoad(i8p, pslot, argNames[i] + "_m") });
            CompilerContext::ScopeDrop sd;
            sd.addr = pslot;
            sd.kind = 7;
            if (bci != ctx.classes.end()) { sd.serNodes = bci->second.serNodesVar; sd.serNodesCount = bci->second.serNodesCount; }
            sd.paramRef = true;
            sd.debugName = argNames[i]; sd.debugType = ptn;
            ctx.pendingParamDrops.push_back(sd);
        }
        // spec/61 §8: a copyable value-model parameter is a member-wise share of the caller's value —
        // the callee co-owns its fields for the call (retain on entry, released on every exit).
        if (ctx.memV3 && ctx.lifetimeDropsEnabled && argAlloca && !(i < argIsByref.size() && argIsByref[i])
            && !(i < argIsRefModel.size() && argIsRefModel[i])) {
            std::string ptn;
            for (auto& fa : func->args) if (fa.first == argNames[i]) { ptn = fa.second; break; }
            auto pci = ctx.classes.find(ptn);
            llvm::Type* lt = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(argTypes[i]));
            if (pci != ctx.classes.end() && pci->second.hasHeapFields && pci->second.serNodesVar && pci->second.type
                && !pci->second.isBuiltin && lt && lt->isStructTy() && !ctx.classGroupContainers.count(ptn)
                && !ctx.cascadeClasses.count(ptn) && !aimlModelIsMoveOnly(ctx, ptn)) {
                auto* i8p = llvm::PointerType::get(ctx.context, 0);
                llvm::Value* pslot = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argAlloca));
                ctx.builder.CreateCall(ctx.getFunction("aiml_ser_retain", ctx.builder.getVoidTy(),
                    {i8p, i8p, ctx.builder.getInt32Ty()}),
                    { pslot, pci->second.serNodesVar, ctx.builder.getInt32(pci->second.serNodesCount) });
                CompilerContext::ScopeDrop sd;
                sd.addr = pslot;
                sd.kind = 0;
                sd.serNodes = pci->second.serNodesVar;
                sd.serNodesCount = pci->second.serNodesCount;
                sd.paramRef = true;
                sd.debugName = argNames[i]; sd.debugType = ptn;
                ctx.pendingParamDrops.push_back(sd);
                sym.paramOwnsRef = true;
            }
        }
        // spec/61 class 4: an iterator parameter was moved in → the callee releases it at body exit.
        bool iterParam = false;
        for (auto& fa : func->args)
            if (fa.first == argNames[i]) { iterParam = fa.second.rfind("iterator<", 0) == 0; break; }
        if (iterParam && ctx.lifetimeDropsEnabled && argAlloca) {
            CompilerContext::ScopeDrop sd;
            sd.addr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argAlloca));
            sd.kind = 10;
            ctx.pendingParamDrops.push_back(sd);
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

        llvm::FunctionCallee traceActiveFn = ctx.module().getOrInsertFunction(
            "aiml_trace_active",
            llvm::FunctionType::get(i32t, {i8p}, false));
        llvm::FunctionCallee traceFn = ctx.module().getOrInsertFunction(
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
        // §10 owning-cycle lint — once per compile (types are all registered by now).
        if (ctx.memV3 && !ctx.ownCycleLintDone) { ctx.ownCycleLintDone = true; AIML::AST::checkOwningCyclesMemV3(ctx); }
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
        } else if (ctx.memV3 && ctx.isMemV3ContainerHandle(func->returnType)) {
            // spec/61 §5: a bodyless / fall-off-the-end method returning a container HANDLE must
            // return a valid FRESH EMPTY handle, not null — the caller binds it (aiml_*_handle_release
            // at scope exit) and calls methods on it (.contains/.len). Null → SIGSEGV (the bodyless
            // creature/item/… `get_produces: map<string:i32>` in test_card_base_engine_api, rc139).
            // handle_new callocs a valid empty {size=0,cap=0,data=null} handle (rc=1).
            auto* i8p = llvm::PointerType::get(ctx.context, 0);
            const std::string& rt = func->returnType;
            const char* nf = (rt.rfind("map<", 0) == 0) ? "aiml_map_handle_new"
                : (rt == "buffer" || rt.rfind("buffer<", 0) == 0) ? "aiml_buffer_handle_new"
                : "aiml_list_handle_new";
            llvm::Value* h = ctx.builder.CreateCall(ctx.getFunction(nf, i8p, {}), {}, "default_conh");
            ctx.ir_gen->createRet(ctx.ir_gen->wrap(h));
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
    ctx.inServiceSynthFn = false;
    /* Restore decorator context — see snapshot at the top of visitFunction. */
    ctx.currentFunctionDecorators = std::move(_saved_decorators);
    ctx.currentFunctionReturnType = std::move(_saved_return_type);
    ctx.currentFunctionOwnsReturn = _saved_owns_return;
    ctx.currentFunctionStaticOff  = std::move(_saved_static_off);
}
Symbol* Compiler::resolveVariable(const std::string& name) {
    // spec/29_2 §22: a GLOBAL that is used belongs in the bundle.  This is the
    // only place that treats reading, writing and interpolation the same —
    // that is why it is recorded exactly here, and only for symbols that really
    // are global (Symbol::is_global).
    auto trackGlobal = [this](const std::string& n, Symbol* sym) -> Symbol* {
        if (sym && sym->is_global && !ctx.bundleTrackingTarget.empty() &&
            ctx.functions.count(ctx.bundleTrackingTarget))
            ctx.functions[ctx.bundleTrackingTarget].referencedGlobals.insert(n);
        return sym;
    };
    if (ctx.currentScope) {
        CompilerContext::Scope* scope = ctx.currentScope;
        while (scope) {
            if (scope->variables.count(name))
                return trackGlobal(name, &scope->variables[name]);
            scope = scope->parent;
        }
    }
    
    // Check namespace globals
    if (ctx.currentNs && ctx.currentNs->globals.count(name)) {
        return trackGlobal(name, &ctx.currentNs->globals[name]);
    }
    
    // Legacy fallback during transition
    if (ctx.symbol_table.count(name)) return trackGlobal(name, &ctx.symbol_table[name]);
    
    // Check FQN fallback (e.g. `math.pi` -> `my.math.pi`)
    std::string qualifiedName = ctx.qualify(name);
    if (ctx.symbol_table.count(qualifiedName))
        return trackGlobal(name, &ctx.symbol_table[qualifiedName]);
    
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

    LOG_ERROR(ctx.fmtLoc() + "Undefined enum variant: " + propName + " on enum " + baseName);
    return nullptr;
}
IRValue* Compiler::resolveProperty(Symbol* baseSym, const std::string& propName) {
    // spec/58 §12.2: `x.<export>` on a group-interface value is the export (a
    // ref<Iface> borrow); reaching past it is GI-12, a non-export GI-11.
    {
        std::string gq = groupInterfaceOf(baseSym->typeName);
        if (!gq.empty()) {
            std::string exp = propName.substr(0, propName.find('.'));
            if (exp.size() < propName.size()) {
                for (const auto& e : ctx.groupInterfaces[gq].exports)
                    if (e.first == exp) {
                        LOG_ERROR(ctx.fmtLoc() + "'" + exp + "' of group interface '" + gq + "' is an interface ('" +
                                  e.second + "'): call one of its methods, x." + exp + ".<method>.");
                        return nullptr;
                    }
            }
            std::string ifaceType;
            return emitGroupExport(baseSym, gq, exp, nullptr, ifaceType);
        }
    }
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

    // list/map by-ref args: the alloca contains the pointer to the real struct.
    // Dereference one level before we do the property access.
    if (baseSym->is_byref_arg) {
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* allocaRaw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
        llvm::Value* loaded = ctx.builder.CreateLoad(i8p, allocaRaw, "byref_deref_prop");
        baseAlloca = ctx.ir_gen->wrap(loaded);
    }

    // spec/69: hashmap<K:V>.len / .empty
    if (HashmapImpl::isHashmapType(className) && (propName == "len" || propName == "empty")) {
        bool known = true;
        llvm::Value* n = HashmapImpl::method(ctx, className, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)),
                                             "len", {}, known);
        if (propName == "len") return ctx.ir_gen->wrap(n);
        return ctx.ir_gen->wrap(ctx.builder.CreateZExt(ctx.builder.CreateICmpEQ(n, ctx.builder.getInt32(0)),
                                                       ctx.builder.getInt32Ty(), "hm_empty"));
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
            llvm::Function* strlenFunc = ctx.module().getFunction("strlen");
            if (!strlenFunc)
                strlenFunc = llvm::Function::Create(
                    llvm::FunctionType::get(ctx.getSizeType(),
                        {ctx.builder.getInt8Ty()->getPointerTo()}, false),
                    llvm::Function::ExternalLinkage, "strlen", ctx.module());
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
                        // spec/01: the deprecated hyphen member-index chain (`obj.list-i.field`) is
                        // removed — use { at: [obj.list, i, .field] } to read.
                        LOG_ERROR(ctx.fmtLoc() + "deprecated hyphen index in member chain ('" + fieldName +
                            "-" + rest + "') is removed — use { at: [<container>, <index>, .<member>] } (spec/01).");
                        return nullptr;
                    }
                    if (false) {
                        // List index: field-idx
                        if (member.typeName.rfind("list<", 0) != 0 || member.typeName.back() != '>') {
                            LOG_ERROR(ctx.fmtLoc() + "Dash-index on non-list field: " + fieldName + " (type: " + member.typeName + ")");
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
                                LOG_ERROR(ctx.fmtLoc() + "Invalid list index: " + rest);
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

        // spec/11 §Typed buffers: `count` counts ELEMENTS and needs the element type,
        // which the buffer<T> → buffer normalization above has just discarded -- so it
        // is answered here, from the symbol's own type, before the shared `buffer`
        // class is asked. `byte_len` lands here for the same reason; it is `len`.
        if ((propName == "count" || propName == "byte_len")
            && baseSym->typeName.rfind("buffer<", 0) == 0 && baseSym->typeName.back() == '>') {
            llvm::Value* rawBuf = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca));
            llvm::Value* szPtr  = ctx.builder.CreateStructGEP(ctx.getLLVMType("buffer"), rawBuf, 0, "buf_sz_ptr");
            llvm::Value* bytes  = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), szPtr, "buf_bytes");
            if (propName == "byte_len") return ctx.ir_gen->wrap(bytes);
            std::string bElem = baseSym->typeName.substr(7, baseSym->typeName.size() - 8);
            llvm::Type* bElemTy = ctx.getLLVMType(bElem);
            if (!bElemTy) {
                LOG_ERROR(ctx.fmtLoc() + "buffer<T>.count: unknown elem type " + bElem);
                return ctx.ir_gen->getInt32(0);
            }
            llvm::DataLayout bdl(&ctx.module());
            uint64_t bEsz = bdl.getTypeAllocSize(bElemTy);
            if (bEsz <= 1) return ctx.ir_gen->wrap(bytes);
            return ctx.ir_gen->wrap(ctx.builder.CreateSDiv(bytes, ctx.builder.getInt32((uint32_t)bEsz), "buf_count"));
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

    // Suppress the follow-up message if the TYPE itself was already reported as
    // unknown — otherwise every property reports the same cause again
    // (spec/49: a cascade hides the line that matters).
    if (ctx.unknownTypeNames.count(className)) return nullptr;
    LOG_ERROR(ctx.fmtLoc() + "Undefined property: " + propName + " on type " + className);
    return nullptr;
}

IRValue* Compiler::emitLValueAddress(Expr* e, std::string& outTypeName) {
    // spec/61 §5.4: `at`/`get` on a `list<Model>` value-model element is a `ref` borrow — its
    // lvalue is the ELEMENT ADDRESS inside the container's data buffer. Return that pointer so a
    // `decl:[r, ref<Model>, {at:…}]` binds the borrow (not a loaded struct value → garbage ptr) and
    // `store` can write through it. Only VALUE-model elements have an addressable inline slot;
    // refcount (%M*) / handle / scalar / string elements are returned by value by visitAtExpr.
    if (auto* at = dynamic_cast<AtExpr*>(e)) {
        auto* cv = dynamic_cast<VariableExpr*>(at->container.get());
        if (!cv || at->indices.empty()) return nullptr;
        std::string cname = cv->name;
        if (cname.rfind("<.", 0) == 0) cname = "this." + cname.substr(2);
        Symbol* csym = resolveVariable(cname);
        Symbol fieldSym;   // member path (`<.doc.items`, `obj.list`): resolve like visitAtExpr does
        if (!csym) {
            size_t dot = cname.find('.');
            Symbol* baseSym = (dot == std::string::npos) ? nullptr : resolveVariable(cname.substr(0, dot));
            if (!baseSym) return nullptr;
            std::string fieldType;
            IRValue* fieldPtr = resolvePropertyWritePtr(baseSym, cname.substr(dot + 1), fieldType);
            if (!fieldPtr || fieldType.empty()) return nullptr;
            fieldSym.typeName = fieldType;
            fieldSym.ir_alloca_inst = fieldPtr;   // memV3: already the container handle
            csym = &fieldSym;
        }
        const std::string& ctn = csym->typeName;
        if (ctn.rfind("list<", 0) != 0 || ctn.back() != '>') return nullptr;
        std::string elemName = ctn.substr(5, ctn.size() - 6);
        auto eci = ctx.classes.find(elemName);
        bool valueModel = (eci != ctx.classes.end() && !eci->second.isBuiltin && eci->second.type
                           && !ctx.isRefcountModel(elemName) && !ctx.isMemV3ContainerHandle(elemName));
        if (!valueModel) return nullptr;
        llvm::Type* elemTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
        if (!elemTy) return nullptr;
        llvm::Value* listPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(
            csym->ir_alloca_inst ? csym->ir_alloca_inst : ctx.ir_gen->wrap(csym->alloca_inst)));
        if (csym->is_byref_arg) {   // memV3 list local / by-ref param: slot holds the handle ptr
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            listPtr = ctx.builder.CreateLoad(i8p, listPtr, "lval_at_conh");
        }
        IRValue* idxV = visitExpression(at->indices[0].get());
        llvm::Value* idxVal = idxV ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(idxV)) : nullptr;
        if (!idxVal) return nullptr;
        if (idxVal->getType()->isIntegerTy() && idxVal->getType()->getIntegerBitWidth() != 32)
            idxVal = ctx.builder.CreateIntCast(idxVal, ctx.builder.getInt32Ty(), true, "lval_at_idx");
        llvm::Value* elemPtr = ListImpl::getElementPtr(ctx, listPtr, idxVal, elemTy);
        outTypeName = elemName;
        return ctx.ir_gen->wrap(elemPtr);
    }
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

IRValue* Compiler::resolvePropertyWritePtr(Symbol* baseSym, const std::string& propName, std::string& outTypeName, bool wantContainerSlot) {
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
                    LOG_ERROR(ctx.fmtLoc() + "Cannot access private member " + fieldName);
                    return nullptr;
                } else if (member.access == AccessLevel::Protected && ctx.currentClassName != className) {
                    LOG_ERROR(ctx.fmtLoc() + "Cannot access protected member " + fieldName);
                    return nullptr;
                }

                IRType* structTy   = ctx.ir_gen->wrapType(classInfo->type);
                IRValue* structPtr = baseAlloca;
                if (baseElemTy->isPointer())
                    structPtr = ctx.ir_gen->createLoad(baseElemTy, baseAlloca, "chain_base_w");
                IRValue* fieldGep    = ctx.ir_gen->createStructGEP(structTy, structPtr, member.index, "chain_gep_w");
                IRType*  fieldElemTy = ctx.ir_gen->wrapType(member.type);

                if (dashFirst) {
                    // spec/01: deprecated hyphen member-index write chain (`obj.list-i.field`) removed.
                    LOG_ERROR(ctx.fmtLoc() + "deprecated hyphen index in member write chain ('" + fieldName +
                        "-" + rest + "') is removed — use idx: [<container>, <index>, .<member>, <value>] (spec/01).");
                    return nullptr;
                }
                if (false) {
                    // List element: field-idx[.further]
                    if (member.typeName.rfind("list<", 0) != 0 || member.typeName.back() != '>') {
                        LOG_ERROR(ctx.fmtLoc() + "Dash-index on non-list field: " + fieldName);
                        return nullptr;
                    }
                    std::string elemName = member.typeName.substr(5, member.typeName.length() - 6);
                    llvm::Type* elemType = ctx.getLLVMType(elemName);
                    llvm::Value* listAllocaVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(fieldGep));
                    // spec/61 §5 (memV3): a list FIELD holds a HANDLE pointer, so fieldGep is the
                    // address of that pointer slot — load the handle to reach the {size,cap,data}
                    // struct before indexing. Without this, getElementPtr reads the handle-pointer
                    // bytes as size/data → wrong element address (write lands nowhere, read-back
                    // stale, later access crashes: `set:[bag.items-1.var_hp, …]`, test_field_chain).
                    if (ctx.memV3 && ctx.isMemV3ContainerHandle(member.typeName)) {
                        auto* i8pH = llvm::PointerType::get(ctx.context, 0);
                        listAllocaVal = ctx.builder.CreateLoad(i8pH, listAllocaVal, "chain_field_conh");
                    }

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
                            LOG_ERROR(ctx.fmtLoc() + "Invalid list index: " + idxStr);
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
                        // spec/61 §5: forward wantContainerSlot so a DEEP chain (a.b.c) whose leaf is a
                        // container field returns the SLOT (not the loaded handle) — else the whole-
                        // container set path reads a garbage "old handle" from the loaded value and
                        // releases it → crash (set:[mat.pending_mana_pick.pending_effect, blk], mana_pick).
                        return resolvePropertyWritePtr(&tmpSym, afterIdx, outTypeName, wantContainerSlot);
                    }
                } else {
                    // Dot chain: recurse into next field (forward wantContainerSlot — see above).
                    Symbol tmpSym;
                    tmpSym.typeName        = member.typeName;
                    tmpSym.ir_alloca_inst  = fieldGep;
                    tmpSym.ir_element_type = fieldElemTy;
                    return resolvePropertyWritePtr(&tmpSym, rest, outTypeName, wantContainerSlot);
                }
            }
        }
    }

    // Direct (leaf) member
    if (classInfo->members.count(propName)) {
        auto& member = classInfo->members[propName];
        if (member.access == AccessLevel::Private && ctx.currentClassName != className) {
            LOG_ERROR(ctx.fmtLoc() + "Cannot access private member " + propName);
            return nullptr;
        } else if (member.access == AccessLevel::Protected && ctx.currentClassName != className) {
            LOG_ERROR(ctx.fmtLoc() + "Cannot access protected member " + propName);
            return nullptr;
        }
        if (member.is_const) {
            LOG_ERROR(ctx.fmtLoc() + "Cannot assign to const member " + propName);
            return nullptr;
        }

        IRType* structTy   = ctx.ir_gen->wrapType(classInfo->type);
        IRValue* structPtr = baseAlloca;
        if (baseElemTy->isPointer())
            structPtr = ctx.ir_gen->createLoad(baseElemTy, baseAlloca, "prop_ptr_base_w");

        outTypeName = member.typeName;
        IRValue* fieldGep = ctx.ir_gen->createStructGEP(structTy, structPtr, member.index, "set_prop_ptr_w");
        // spec/61 §5 Option B (memV3): a container field is a POINTER to a handle. As a method
        // RECEIVER (obj.tags.push) callers want the handle → LOAD it. The whole-container SET path
        // (set:[obj.field, X]) computes the slot itself (it detects the container field first).
        if (!wantContainerSlot && ctx.memV3 &&
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
// declaredTypeOfExpr — determine the DECLARED type of an expression without generating IR
// (unlike emitLValueAddress, which emits addresses).  Needed
// at the argument boundary in order to build an interface upcast: there
// the object pointer is already available, only the concrete class name is missing.
// Covers: simple variable, dotted member access (also multi-level, e.g.
// a classgroup instance `grp.handler`) and `{new: Concrete}`.
// Empty string = not resolvable; the caller reports that as an error.
std::string Compiler::declaredTypeOfExpr(Expr* e) {
    auto classMemberType = [&](const std::string& clsName,
                               const std::string& field) -> std::string {
        auto it = ctx.classes.find(clsName);
        if (it == ctx.classes.end()) return "";
        auto mit = it->second.members.find(field);
        return mit == it->second.members.end() ? std::string() : mit->second.typeName;
    };
    auto strip = [](const std::string& t) {
        if ((t.rfind("ptr<", 0) == 0 || t.rfind("ref<", 0) == 0) && t.back() == '>')
            return t.substr(4, t.size() - 5);
        return t;
    };

    if (auto* ne = dynamic_cast<NewExpr*>(e))
        return ctx.classes.count(ne->typeName) ? ("ptr<" + ne->typeName + ">") : std::string();

    auto* ve = dynamic_cast<VariableExpr*>(e);
    if (!ve) return "";
    const std::string& full = ve->name;
    size_t dot = full.find('.');
    if (dot == std::string::npos) {
        Symbol* s = resolveVariable(full);
        if (!s) return "";
        // A class instance by value behaves like ptr<T> at the argument.
        if (s->typeName.rfind("ptr<", 0) != 0 && s->typeName.rfind("ref<", 0) != 0 &&
            ctx.classes.count(s->typeName))
            return "ptr<" + s->typeName + ">";
        return s->typeName;
    }
    // Dotted: resolve the base, then walk through the classes field by field.
    Symbol* base = resolveVariable(full.substr(0, dot));
    if (!base) return "";
    std::string curType = base->typeName;
    size_t pos = dot;
    while (pos != std::string::npos) {
        size_t next = full.find('.', pos + 1);
        const std::string field = full.substr(pos + 1,
            next == std::string::npos ? std::string::npos : next - pos - 1);
        const std::string memberTy = classMemberType(strip(curType), field);
        if (memberTy.empty()) return "";
        curType = memberTy;
        pos = next;
    }
    if (curType.rfind("ptr<", 0) != 0 && curType.rfind("ref<", 0) != 0 &&
        ctx.classes.count(curType))
        return "ptr<" + curType + ">";
    return curType;
}

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
    llvm::GlobalVariable* vtbl = ctx.module().getNamedGlobal(vtableName);
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
        vtbl = new llvm::GlobalVariable(ctx.module(), arrTy, /*isConstant=*/true,
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
        LOG_ERROR(ctx.fmtLoc(call->sourceNode, call->sourceLine) + "sys.clone: requires one argument");
        return ctx.ir_gen->getInt32(0);
    }

    // Resolve argument + type. Two forms:
    //   • a bare variable — clone its declared type (from the symbol table);
    //   • spec/61 §5.1 (review probe 26): an inline element BORROW ({at:[c,i]}/c.at(i)/c.get(k)) —
    //     clone the borrowed value as the DECLARED target type (deserializeTargetType), yielding an
    //     owned, independent value.
    auto* var = dynamic_cast<VariableExpr*>(call->args[0].get());
    Symbol* argSym = var ? resolveVariable(var->name) : nullptr;
    std::string argType;
    bool inlineBorrow = false;
    if (var) {
        if (!argSym) {
            LOG_ERROR(ctx.fmtLoc(call->sourceNode, call->sourceLine) + "sys.clone: unknown variable '" + var->name + "'");
            return ctx.ir_gen->getInt32(0);
        }
        argType = argSym->typeName;
    } else {
        argType = ctx.deserializeTargetType;   // clone of an inline expression → the declared target type
        inlineBorrow = true;
        if (argType.empty()) {
            LOG_ERROR(ctx.fmtLoc(call->sourceNode, call->sourceLine) + "sys.clone: an inline argument (e.g. {at:…}) needs a "
                "typed target — use `decl: [x, <Type>, { clone: {at:…} }]`.");
            return ctx.ir_gen->getInt32(0);
        }
    }
    if (argType.empty() || argType == "any") {
        LOG_ERROR(ctx.fmtLoc(call->sourceNode, call->sourceLine) + "sys.clone: type '" + argType + "' is not cloneable");
        return ctx.ir_gen->getInt32(0);
    }

    if (containsVfxResource(argType)) {
        LOG_ERROR(ctx.fmtLoc() + "spec/61: cannot clone an owned GL resource");
        return nullptr;
    }
    // Resolve LLVM struct type so we know dst's size.
    llvm::Type* dstLlvmTy = ctx.getLLVMType(argType);
    if (!dstLlvmTy) {
        LOG_ERROR(ctx.fmtLoc(call->sourceNode, call->sourceLine) + "sys.clone: unknown LLVM type for '" + argType + "'");
        return ctx.ir_gen->getInt32(0);
    }

    // Build TypeNodes for the argument type and emit them as a global constant. round4 §1: this is a
    // CLONE descriptor (ref<T>→WEAK_REF, tensor→TENSOR under memV3). The Clone leaf branch is itself
    // memV3-gated, so under OFF this yields the identical Existing descriptor (byte-identical deploy).
    llvm::DataLayout dl(&ctx.module());
    std::vector<AimlTypeNode> nodes = serBuildSubtree(argType, 0, ctx, dl, DescriptorPurpose::Clone);
    if (nodes.empty()) {
        LOG_ERROR(ctx.fmtLoc(call->sourceNode, call->sourceLine) + "sys.clone: failed to build TypeNodes for '" + argType + "'");
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
    llvm::GlobalVariable* gv = ctx.module().getGlobalVariable(symName);
    if (!gv) {
        gv = new llvm::GlobalVariable(ctx.module(), arrTy, /*isConstant=*/true,
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
    llvm::Value* srcPtr = nullptr;
    if (inlineBorrow) {
        // Evaluate the borrow expression → a pointer to the borrowed value (the element fields).
        IRValue* bv = visitExpression(call->args[0].get());
        srcPtr = bv ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(bv)) : nullptr;
        if (srcPtr && !srcPtr->getType()->isPointerTy()) {
            // A loaded value (e.g. a %M* element) — spill to a temp so we have an address to clone from.
            llvm::AllocaInst* tmp = ctx.createEntryAlloca(srcPtr->getType());
            ctx.builder.CreateStore(srcPtr, tmp);
            srcPtr = tmp;
        }
        if (!srcPtr) { LOG_ERROR(ctx.fmtLoc(call->sourceNode, call->sourceLine) + "sys.clone: could not evaluate the borrow argument"); return ctx.ir_gen->getInt32(0); }
    } else {
        IRValue* srcBase = argSym->ir_alloca_inst ? argSym->ir_alloca_inst
                                                  : ctx.ir_gen->wrap(argSym->alloca_inst);
        srcPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(srcBase));
    }

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
